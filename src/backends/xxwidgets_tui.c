/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "../xxwidgets_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <errno.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <locale.h>
#include <wchar.h>
#ifdef __APPLE__
#include <xlocale.h> /* newlocale and uselocale */
#endif
/* XSI, so plain POSIX feature macros do not declare it. */
int wcwidth(wchar_t character);
#endif

/* Text occupies one cell per Unicode scalar, and two for an East Asian wide
 * one on POSIX terminals (its second cell holds TUI_WIDE_TAIL). Combining
 * glyphs need a shaping layer this backend does not have. The Windows console
 * keeps one cell per scalar. Edits are single-line; terminal mouse input is
 * deliberately not required. */
#define TUI_WIDE_TAIL 0x110000U
typedef struct tui_cell {
    uint32_t glyph;
    uint32_t background; /* UINT32_MAX = ordinary style background. */
    unsigned char style;
} tui_cell;

typedef struct tui_edit {
    size_t caret;
    size_t start; /* First character shown; moves only when the caret leaves the field. */
    char *seen;   /* Text as last typed or shown; a different text came from the program. */
} tui_edit;

/* Every list kind: its view keeps its place between redraws. */
typedef struct tui_hexview {
    size_t horizontal;
    uint64_t revision;
    size_t first; /* First row shown; moves only to bring the selected row into view. */
    const unsigned char *hex_data;
    size_t hex_size, hex_cursor;
    uint64_t hex_base;
    unsigned int hex_columns, hex_nibble;
    int hex_editable;
    /* The pan limit, kept between redraws: the most glyphs any of the first
     * measured rows must skip in a view measured_visible cells wide. A log
     * grows only at its end, so only its new lines are measured. */
    size_t maximum, measured, measured_visible;
    int grown; /* LISTBOX: lines came at the last sync while an older one was selected. */
} tui_hexview;

typedef struct tui_state {
    xxwidgets_widget *focus;
    xxwidgets_widget *dropdown;
    int dropdown_index; /* Highlighted item of an open COMBOBOX list. */
    tui_cell *cells;
    size_t capacity;
    int columns, rows, dirty;
    int cursor_x, cursor_y, cursor_visible;
#ifdef _WIN32
    HANDLE input, original_output, output;
    DWORD original_input_mode;
    WCHAR pending_surrogate;
    INPUT_RECORD records[64];
    DWORD record_count, record_index;
    uint32_t pending_key;
    unsigned int pending_modifiers;
    unsigned int pending_repeats;
#else
    struct termios original_termios;
    unsigned char input[256];
    size_t input_length;
    uint64_t escape_since;
#endif
} tui_state;

enum {
    TUI_NORMAL,
    TUI_DISABLED,
    TUI_FOCUS,
    TUI_TITLE, TUI_RED, TUI_GREEN, TUI_BLUE, TUI_OLIVE, TUI_TEAL, TUI_GRAY, TUI_MALWARE, TUI_FOCUS_DISABLED
};
/* Independent apps cannot safely own the same process terminal concurrently. */
static int tui_active;
enum {
    TUI_KEY_TAB = 0x110000,
    TUI_KEY_BACKTAB,
    TUI_KEY_ENTER,
    TUI_KEY_ESCAPE,
    TUI_KEY_BACKSPACE,
    TUI_KEY_DELETE,
    TUI_KEY_LEFT,
    TUI_KEY_RIGHT,
    TUI_KEY_UP,
    TUI_KEY_DOWN,
    TUI_KEY_HOME,
    TUI_KEY_END,
    TUI_KEY_PAGEUP,
    TUI_KEY_PAGEDOWN,
    TUI_KEY_CONTEXT,
    TUI_KEY_INSERT,
    TUI_KEY_F1 = 0x110100
};

typedef struct tui_clip {
    long long left, top, right, bottom;
} tui_clip;

static uint32_t tui_decode(const char *text, size_t available, size_t *length)
{
    const unsigned char *s = (const unsigned char *)text;
    uint32_t cp;
    size_t count, i;
    *length = 0;
    if (!available) return 0;
    if (s[0] < 0x80) {
        *length = 1;
        return s[0];
    }
    if (s[0] >= 0xc2 && s[0] <= 0xdf) {
        count = 2;
        cp = s[0] & 31;
    } else if (s[0] >= 0xe0 && s[0] <= 0xef) {
        count = 3;
        cp = s[0] & 15;
    } else if (s[0] >= 0xf0 && s[0] <= 0xf4) {
        count = 4;
        cp = s[0] & 7;
    } else {
        *length = 1;
        return 0xfffd;
    }
    /* A non-continuation byte after an incomplete leading byte belongs to the
     * next key. Consume the broken prefix instead of indefinitely buffering it. */
    for (i = 1; i < count && i < available; ++i) {
        if ((s[i] & 0xc0) != 0x80) {
            *length = 1;
            return 0xfffd;
        }
    }
    if (available > 1 && ((s[0] == 0xe0 && s[1] < 0xa0) || (s[0] == 0xed && s[1] > 0x9f) || (s[0] == 0xf0 && s[1] < 0x90) || (s[0] == 0xf4 && s[1] > 0x8f))) {
        *length = 1;
        return 0xfffd;
    }
    if (available < count) return 0;
    for (i = 1; i < count; ++i) cp = (cp << 6) | (s[i] & 63);
    if ((count == 2 && cp < 0x80) || (count == 3 && cp < 0x800) || (count == 4 && cp < 0x10000) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
        *length = 1;
        return 0xfffd;
    }
    *length = count;
    return cp;
}

static size_t tui_encode(uint32_t cp, char *buffer)
{
    if (cp < 0x80) {
        buffer[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        buffer[0] = (char)(0xc0 | (cp >> 6));
        buffer[1] = (char)(0x80 | (cp & 63));
        return 2;
    }
    if (cp < 0x10000) {
        buffer[0] = (char)(0xe0 | (cp >> 12));
        buffer[1] = (char)(0x80 | ((cp >> 6) & 63));
        buffer[2] = (char)(0x80 | (cp & 63));
        return 3;
    }
    buffer[0] = (char)(0xf0 | (cp >> 18));
    buffer[1] = (char)(0x80 | ((cp >> 12) & 63));
    buffer[2] = (char)(0x80 | ((cp >> 6) & 63));
    buffer[3] = (char)(0x80 | (cp & 63));
    return 4;
}

static size_t tui_previous(const char *text, size_t offset)
{
    if (!offset) return 0;
    --offset;
    while (offset && (((unsigned char)text[offset] & 0xc0) == 0x80)) --offset;
    return offset;
}

static size_t tui_next(const char *text, size_t offset)
{
    size_t length, remaining = 0;
    /* A UTF-8 sequence is at most 4 bytes: no need to measure the rest. */
    while (remaining < 4 && text[offset + remaining]) ++remaining;
    if (!remaining) return offset;
    tui_decode(text + offset, remaining, &length);
    return offset + (length ? length : 1);
}

static int tui_eligible(const xxwidgets_widget *widget)
{
    return xxwidgets_focusable(widget) && widget->visible && widget->enabled && widget->parent && widget->parent->visible && widget->parent->enabled;
}

static void tui_focus_next(xxwidgets_app *app, int backwards)
{
    tui_state *state = (tui_state *)app->platform;
    xxwidgets_widget *widget, *first = NULL, *last = NULL, *previous = NULL;
    int found = 0;
    for (widget = app->widgets; widget; widget = widget->next) {
        if (!tui_eligible(widget)) continue;
        /* An empty list box draws nothing to focus, as in the GTK backend. */
        if (widget != state->focus && widget->kind == XXWIDGETS_LISTBOX && !widget->item_count) continue;
        if (!first) first = widget;
        last = widget;
        if (backwards) {
            if (widget == state->focus) {
                if (previous) {
                    state->focus = previous;
                    state->dirty = 1;
                    return;
                }
                found = 1;
            }
            previous = widget;
        } else {
            if (found) {
                state->focus = widget;
                state->dirty = 1;
                return;
            }
            if (widget == state->focus) found = 1;
        }
    }
    widget = backwards ? last : first;
    if (state->focus != widget) state->dirty = 1;
    state->focus = widget;
}

static tui_clip tui_intersect(tui_clip a, tui_clip b)
{
    if (a.left < b.left) a.left = b.left;
    if (a.top < b.top) a.top = b.top;
    if (a.right > b.right) a.right = b.right;
    if (a.bottom > b.bottom) a.bottom = b.bottom;
    return a;
}

/* Terminal cells a glyph takes: 2 for East Asian wide and full-width ones
 * (CJK, Hangul, full-width forms, most emoji), else 1. */
static int tui_width(uint32_t glyph)
{
#ifdef _WIN32
    (void)glyph;
    return 1;
#else
    static locale_t utf8 = (locale_t)0;
    static int looked;
    /* The code points glibc's wcwidth() counts two cells: East Asian Width
     * W and F (Unicode 16.0.0) without the zero-width ones (U+302A-302D,
     * U+3099-309A, U+3164, U+16FE4), plus U+3248-324F; generated, not
     * hand-picked by block. */
    static const uint32_t wide[][2] = {
        {0x1100, 0x115F},   {0x231A, 0x231B},   {0x2329, 0x232A},   {0x23E9, 0x23EC},   {0x23F0, 0x23F0},   {0x23F3, 0x23F3},   {0x25FD, 0x25FE},   {0x2614, 0x2615},
        {0x2630, 0x2637},   {0x2648, 0x2653},   {0x267F, 0x267F},   {0x268A, 0x268F},   {0x2693, 0x2693},   {0x26A1, 0x26A1},   {0x26AA, 0x26AB},   {0x26BD, 0x26BE},
        {0x26C4, 0x26C5},   {0x26CE, 0x26CE},   {0x26D4, 0x26D4},   {0x26EA, 0x26EA},   {0x26F2, 0x26F3},   {0x26F5, 0x26F5},   {0x26FA, 0x26FA},   {0x26FD, 0x26FD},
        {0x2705, 0x2705},   {0x270A, 0x270B},   {0x2728, 0x2728},   {0x274C, 0x274C},   {0x274E, 0x274E},   {0x2753, 0x2755},   {0x2757, 0x2757},   {0x2795, 0x2797},
        {0x27B0, 0x27B0},   {0x27BF, 0x27BF},   {0x2B1B, 0x2B1C},   {0x2B50, 0x2B50},   {0x2B55, 0x2B55},   {0x2E80, 0x2E99},   {0x2E9B, 0x2EF3},   {0x2F00, 0x2FD5},
        {0x2FF0, 0x3029},   {0x302E, 0x303E},   {0x3041, 0x3096},   {0x309B, 0x30FF},   {0x3105, 0x312F},   {0x3131, 0x3163},   {0x3165, 0x318E},   {0x3190, 0x31E5},
        {0x31EF, 0x321E},   {0x3220, 0xA48C},   {0xA490, 0xA4C6},   {0xA960, 0xA97C},   {0xAC00, 0xD7A3},   {0xF900, 0xFAFF},   {0xFE10, 0xFE19},   {0xFE30, 0xFE52},
        {0xFE54, 0xFE66},   {0xFE68, 0xFE6B},   {0xFF01, 0xFF60},   {0xFFE0, 0xFFE6},   {0x16FE0, 0x16FE3}, {0x16FF0, 0x16FF1}, {0x17000, 0x187F7}, {0x18800, 0x18CD5},
        {0x18CFF, 0x18D08}, {0x1AFF0, 0x1AFF3}, {0x1AFF5, 0x1AFFB}, {0x1AFFD, 0x1AFFE}, {0x1B000, 0x1B122}, {0x1B132, 0x1B132}, {0x1B150, 0x1B152}, {0x1B155, 0x1B155},
        {0x1B164, 0x1B167}, {0x1B170, 0x1B2FB}, {0x1D300, 0x1D356}, {0x1D360, 0x1D376}, {0x1F004, 0x1F004}, {0x1F0CF, 0x1F0CF}, {0x1F18E, 0x1F18E}, {0x1F191, 0x1F19A},
        {0x1F200, 0x1F202}, {0x1F210, 0x1F23B}, {0x1F240, 0x1F248}, {0x1F250, 0x1F251}, {0x1F260, 0x1F265}, {0x1F300, 0x1F320}, {0x1F32D, 0x1F335}, {0x1F337, 0x1F37C},
        {0x1F37E, 0x1F393}, {0x1F3A0, 0x1F3CA}, {0x1F3CF, 0x1F3D3}, {0x1F3E0, 0x1F3F0}, {0x1F3F4, 0x1F3F4}, {0x1F3F8, 0x1F43E}, {0x1F440, 0x1F440}, {0x1F442, 0x1F4FC},
        {0x1F4FF, 0x1F53D}, {0x1F54B, 0x1F54E}, {0x1F550, 0x1F567}, {0x1F57A, 0x1F57A}, {0x1F595, 0x1F596}, {0x1F5A4, 0x1F5A4}, {0x1F5FB, 0x1F64F}, {0x1F680, 0x1F6C5},
        {0x1F6CC, 0x1F6CC}, {0x1F6D0, 0x1F6D2}, {0x1F6D5, 0x1F6D7}, {0x1F6DC, 0x1F6DF}, {0x1F6EB, 0x1F6EC}, {0x1F6F4, 0x1F6FC}, {0x1F7E0, 0x1F7EB}, {0x1F7F0, 0x1F7F0},
        {0x1F90C, 0x1F93A}, {0x1F93C, 0x1F945}, {0x1F947, 0x1F9FF}, {0x1FA70, 0x1FA7C}, {0x1FA80, 0x1FA89}, {0x1FA8F, 0x1FAC6}, {0x1FACE, 0x1FADC}, {0x1FADF, 0x1FAE9},
        {0x1FAF0, 0x1FAF8}, {0x20000, 0x2FFFD}, {0x30000, 0x3FFFD}};
    size_t low = 0, high = sizeof(wide) / sizeof(wide[0]);
    if (glyph < 0x1100) return 1;
    /* Terminals (and tmux) take their widths from the C library, which can
     * follow a newer Unicode than the table above: ask it, in a UTF-8 locale
     * whatever the program's own locale is. A code point it does not know
     * (-1) is one cell, as terminals draw it. The table answers only where
     * there is no UTF-8 locale. */
    if (!looked) {
        looked = 1;
        utf8 = newlocale(LC_CTYPE_MASK, "C.UTF-8", (locale_t)0);
        if (!utf8) utf8 = newlocale(LC_CTYPE_MASK, "en_US.UTF-8", (locale_t)0);
    }
    if (utf8 && sizeof(wchar_t) >= 4) {
        locale_t previous = uselocale(utf8);
        int width = wcwidth((wchar_t)glyph);
        uselocale(previous);
        return width == 2 ? 2 : 1;
    }
    while (low < high) {
        size_t mid = (low + high) / 2;
        if (glyph < wide[mid][0]) high = mid;
        else if (glyph > wide[mid][1]) low = mid + 1;
        else return 2;
    }
    return 1;
#endif
}

/* Cells taken by the glyph that starts at byte offset at. */
static int tui_cell_width(const char *text, size_t at)
{
    size_t length, available = 0;
    uint32_t glyph;
    while (available < 4 && text[at + available]) ++available;
    glyph = tui_decode(text + at, available, &length);
    return length ? tui_width(glyph) : 1;
}

/* Places a field's view around its caret, counted in terminal cells. As in a
 * native field the view keeps its place: it moves only when the caret, with
 * the glyph under it, would leave it, or to fill room freed at the end. Runs
 * after every key as well as on redraw, so keys that arrive together see the
 * view each one leaves. Returns the caret's cell inside the view. */
static size_t tui_edit_view(xxwidgets_widget *widget)
{
    tui_edit *edit = (tui_edit *)widget->platform;
    const char *text = widget->text;
    size_t length = strlen(text), caret = edit ? edit->caret : length;
    size_t start, pos, count = 0, tail = 0, here, visible = widget->rect.width > 2 ? (size_t)widget->rect.width - 2 : 0;
    if (caret > length) caret = length;
    start = edit && edit->start <= caret ? edit->start : caret;
    while (start && ((unsigned char)text[start] & 0xc0) == 0x80) --start;
    here = caret < length ? (size_t)tui_cell_width(text, caret) : 1;
    for (pos = start; pos < caret && text[pos]; pos = tui_next(text, pos)) count += (size_t)tui_cell_width(text, pos);
    while (visible && count + here > visible && start < caret) {
        count -= (size_t)tui_cell_width(text, start);
        start = tui_next(text, start);
    }
    for (pos = start; text[pos]; pos = tui_next(text, pos)) tail += (size_t)tui_cell_width(text, pos);
    while (start) {
        size_t before = tui_previous(text, start), width = (size_t)tui_cell_width(text, before);
        if (tail + width + 1 > visible) break;
        start = before;
        tail += width;
        count += width;
    }
    if (edit) edit->start = start;
    return count;
}

/* Cells needed to skip from a row's start so its end fits in visible cells,
 * in glyphs (the unit of a list's sideways offset). */
static size_t tui_row_skip(const char *row, size_t visible)
{
    size_t total = 0, at, skip = 0;
    for (at = 0; row[at]; at = tui_next(row, at)) total += (size_t)tui_cell_width(row, at);
    for (at = 0; total > visible && row[at]; at = tui_next(row, at), ++skip) total -= (size_t)tui_cell_width(row, at);
    return skip;
}

static void tui_put(tui_state *state, tui_clip clip, long long x, long long y, uint32_t glyph, unsigned char style)
{
    if (x < clip.left || x >= clip.right || y < clip.top || y >= clip.bottom || x < 0 || y < 0 || x >= state->columns || y >= state->rows) return;
    state->cells[(size_t)y * (size_t)state->columns + (size_t)x].glyph = glyph;
    state->cells[(size_t)y * (size_t)state->columns + (size_t)x].style = style;
    state->cells[(size_t)y * (size_t)state->columns + (size_t)x].background = UINT32_MAX;
}

static void tui_fill(tui_state *state, tui_clip clip, unsigned char style)
{
    tui_clip screen = {0, 0, state->columns, state->rows};
    long long x, y;
    clip = tui_intersect(clip, screen);
    for (y = clip.top; y < clip.bottom; ++y)
        for (x = clip.left; x < clip.right; ++x) tui_put(state, clip, x, y, ' ', style);
}

#define TUI_TEXT_MULTILINE 1
#define TUI_TEXT_ELLIPSIS 2 /* A line cut at the edge ends in an ellipsis (not for edits: the caret cell). */

/* Format characters (Unicode Cf, such as the bidirectional U+202E), line and
 * paragraph separators and other code points that draw nothing: written to
 * the terminal, they could show text such as a typed path reversed or hide a
 * difference. They take a cell as U+FFFD instead. U+200C and U+200D stay;
 * the variation selectors are tui_hidden_selector's. The same code points
 * as CXFileUnpacker's ui.c and cli.c escape in member names. */
static int tui_invisible(uint32_t glyph)
{
    static const uint32_t ranges[][2] = {{0x00AD, 0x00AD},   {0x034F, 0x034F},   {0x0600, 0x0605},   {0x061C, 0x061C},   {0x06DD, 0x06DD},   {0x070F, 0x070F},
                                         {0x0890, 0x0891},   {0x08E2, 0x08E2},   {0x115F, 0x1160},   {0x17B4, 0x17B5},   {0x180E, 0x180E},   {0x200B, 0x200B},
                                         {0x200E, 0x200F},   {0x2028, 0x202E},   {0x2060, 0x206F},   {0x3164, 0x3164},   {0xFEFF, 0xFEFF},   {0xFFA0, 0xFFA0},
                                         {0xFFF0, 0xFFFB},   {0x110BD, 0x110BD}, {0x110CD, 0x110CD}, {0x13430, 0x1343F}, {0x1BCA0, 0x1BCA3}, {0x1D173, 0x1D17A},
                                         {0xE0000, 0xE00FF}, {0xE01F0, 0xE0FFF}};
    size_t i;
    for (i = 0; i < sizeof(ranges) / sizeof(ranges[0]); ++i)
        if (glyph >= ranges[i][0] && glyph <= ranges[i][1]) return 1;
    return 0;
}

/* Whether glyph, a variation selector (U+FE00-U+FE0F, U+E0100-U+E01EF, the
 * Mongolian U+180B-U+180D and U+180F), draws nothing after previous, the
 * code point drawn before it (0 for none), and before next (0 for none), and
 * so could hide a difference. One stays only after a base it applies to:
 * U+FE0E and U+FE0F after an emoji or a symbol with an emoji form, or between
 * a keycap's 0-9, # or * and U+20E3; U+FE00-U+FE02 and U+E0100-U+E01EF after
 * a CJK ideograph; the Mongolian ones after a Mongolian letter. As ui.c's and
 * cli.c's hidden_selector, which escape such a one in member names. */
static int tui_hidden_selector(uint32_t previous, uint32_t glyph, uint32_t next)
{
    static const uint32_t emoji[][2] = {{0x00A9, 0x00A9}, {0x00AE, 0x00AE}, {0x203C, 0x203C}, {0x2049, 0x2049}, {0x2122, 0x2122}, {0x2139, 0x2139}, {0x2194, 0x2199},
                                        {0x21A9, 0x21AA}, {0x231A, 0x231B}, {0x2328, 0x2328}, {0x23CF, 0x23CF}, {0x23E9, 0x23FA}, {0x24C2, 0x24C2}, {0x25AA, 0x25AB},
                                        {0x25B6, 0x25B6}, {0x25C0, 0x25C0}, {0x25FB, 0x25FE}, {0x2600, 0x27BF}, {0x2934, 0x2935}, {0x2B05, 0x2B07}, {0x2B1B, 0x2B1C},
                                        {0x2B50, 0x2B50}, {0x2B55, 0x2B55}, {0x3030, 0x3030}, {0x303D, 0x303D}, {0x3297, 0x3297}, {0x3299, 0x3299}, {0x1F000, 0x1FAFF}};
    static const uint32_t ideographs[][2] = {{0x3400, 0x4DBF}, {0x4E00, 0x9FFF}, {0xF900, 0xFAFF}, {0x20000, 0x3FFFF}};
    size_t i;
    if ((glyph >= 0x180B && glyph <= 0x180D) || glyph == 0x180F) return previous < 0x1820 || previous > 0x18AA;
    if (glyph == 0xFE0E || glyph == 0xFE0F) {
        if ((previous >= '0' && previous <= '9') || previous == '#' || previous == '*') return next != 0x20E3;
        for (i = 0; i < sizeof(emoji) / sizeof(emoji[0]); ++i)
            if (previous >= emoji[i][0] && previous <= emoji[i][1]) return 0;
        return 1;
    }
    if ((glyph >= 0xFE00 && glyph <= 0xFE02) || (glyph >= 0xE0100 && glyph <= 0xE01EF)) {
        for (i = 0; i < sizeof(ideographs) / sizeof(ideographs[0]); ++i)
            if (previous >= ideographs[i][0] && previous <= ideographs[i][1]) return 0;
        return 1;
    }
    return glyph >= 0xFE03 && glyph <= 0xFE0D;
}

/* Text as the terminal shows it, one glyph a cell (two for a wide one). What
 * CXFileUnpacker's ui.c escapes in a member name takes a cell as U+FFFD here,
 * in typed or program-set text alike: control characters (in multiline text
 * '\n' breaks the line and '\r' before it is dropped), the code points of
 * tui_invisible and hidden variation selectors. */
static void tui_text(tui_state *state, tui_clip clip, long long x, long long y, const char *text, unsigned char style, int flags)
{
    int multiline = flags & TUI_TEXT_MULTILINE, cells, more;
    size_t length, remaining = strlen(text);
    long long start = x;
    uint32_t previous = 0;
    while (remaining && y < clip.bottom) {
        uint32_t glyph = tui_decode(text, remaining, &length);
        if (!length) {
            length = 1;
            glyph = 0xfffd;
        }
        text += length;
        remaining -= length;
        if (multiline && glyph == '\n') {
            ++y;
            x = start;
            previous = 0;
            continue;
        }
        if (multiline && glyph == '\r') continue;
        if (glyph < 32 || (glyph >= 127 && glyph < 160) || tui_invisible(glyph)) {
            glyph = 0xfffd; /* C0, DEL and C1 controls too */
        } else if (glyph >= 0x180B) {
            size_t next_length;
            uint32_t next = remaining ? tui_decode(text, remaining, &next_length) : 0;
            if (tui_hidden_selector(previous, glyph, next)) glyph = 0xfffd;
        }
        previous = glyph == 0xfffd ? 0 : glyph;
        cells = tui_width(glyph);
        more = remaining && (!multiline || (*text != '\n' && *text != '\r'));
        if (x + cells > clip.right || ((flags & TUI_TEXT_ELLIPSIS) && x > start && x + cells >= clip.right && more)) {
            /* The line goes on past the edge: say so instead of cutting it,
             * and never let a wide glyph straddle the edge. */
            if (x < clip.right) tui_put(state, clip, x, y, (flags & TUI_TEXT_ELLIPSIS) && x > start ? 0x2026 : ' ', style);
            if (!multiline) break;
            while (remaining && *text != '\n') {
                ++text;
                --remaining;
            }
            x = clip.right;
            continue;
        }
        tui_put(state, clip, x++, y, glyph, style);
        if (cells == 2) tui_put(state, clip, x++, y, TUI_WIDE_TAIL, style);
        if (!multiline && x >= clip.right) break;
    }
}

static void tui_box(tui_state *state, tui_clip clip, long long x, long long y, long long width, long long height, unsigned char style)
{
    long long pos, end;
    if (width < 2 || height < 2) return;
    pos = x > 0 ? x : 0;
    end = x + width;
    if (end > state->columns) end = state->columns;
    for (; pos < end; ++pos) {
        tui_put(state, clip, pos, y, '-', style);
        tui_put(state, clip, pos, y + height - 1, '-', style);
    }
    pos = y > 0 ? y : 0;
    end = y + height;
    if (end > state->rows) end = state->rows;
    for (; pos < end; ++pos) {
        tui_put(state, clip, x, pos, '|', style);
        tui_put(state, clip, x + width - 1, pos, '|', style);
    }
    tui_put(state, clip, x, y, '+', style);
    tui_put(state, clip, x + width - 1, y, '+', style);
    tui_put(state, clip, x, y + height - 1, '+', style);
    tui_put(state, clip, x + width - 1, y + height - 1, '+', style);
}

static size_t tui_hexview_horizontal_max(tui_state *state, xxwidgets_widget *widget)
{
    tui_hexview *hexview = (tui_hexview *)widget->platform;
    long long x = (long long)widget->parent->rect.x + 1 + widget->rect.x;
    int width = widget->rect.width;
    size_t visible;
    if (width > widget->parent->rect.width - widget->rect.x) width = widget->parent->rect.width - widget->rect.x;
    if ((long long)width > state->columns - x) width = (int)(state->columns - x);
    visible = width > 1 ? (size_t)width - 1 : 1;
    /* Rows are measured in cells (a wide glyph takes two) and the offset
     * counts glyphs: the most glyphs any row must skip to show its end. A hex
     * view's rows are plain ASCII of one width. */
    if (widget->kind == XXWIDGETS_HEXVIEW) {
        size_t length = xxwidgets_hexview_column_width(widget, 0) + xxwidgets_hexview_column_width(widget, 1) + xxwidgets_hexview_column_width(widget, 2) + 3;
        return length > visible ? length - visible : 0;
    }
    if (hexview->measured > widget->item_count || hexview->measured_visible != visible) {
        hexview->maximum = hexview->measured = 0;
        hexview->measured_visible = visible;
    }
    for (; hexview->measured < widget->item_count; ++hexview->measured) {
        size_t skip = tui_row_skip(widget->items[hexview->measured], visible);
        if (skip > hexview->maximum) hexview->maximum = skip;
    }
    return hexview->maximum;
}

static size_t tui_hexview_visible(tui_state *state, xxwidgets_widget *widget)
{
    long long x = (long long)widget->parent->rect.x + 1 + widget->rect.x;
    int width = widget->rect.width;
    if (width > widget->parent->rect.width - widget->rect.x) width = widget->parent->rect.width - widget->rect.x;
    if ((long long)width > state->columns - x) width = (int)(state->columns - x);
    return width > 1 ? (size_t)width - 1 : 0;
}

static void tui_hexview_reveal_cursor(tui_state *state, xxwidgets_widget *widget)
{
    tui_hexview *view = (tui_hexview *)widget->platform;
    size_t column, visible, maximum;
    if (!view || widget->kind != XXWIDGETS_HEXVIEW || widget->hex_cursor == SIZE_MAX || !widget->hex_columns) return;
    column = xxwidgets_hexview_cursor_column(widget);
    if (column >= xxwidgets_hexview_column_width(widget, 0) + 1 + xxwidgets_hexview_column_width(widget, 1) + 1 +
        xxwidgets_hexview_column_width(widget, 2)) return;
    visible = tui_hexview_visible(state, widget);
    if (!visible) return;
    if (column < view->horizontal)
        view->horizontal = column < visible ? 0 : column - 1 - (widget->hex_header == 2 ? 0 : widget->hex_nibble);
    else if (column - view->horizontal >= visible) view->horizontal = column - visible + 1;
    maximum = tui_hexview_horizontal_max(state, widget);
    if (view->horizontal > maximum) view->horizontal = maximum;
}

static unsigned int tui_hexview_key(uint32_t key)
{
    switch (key) {
        case TUI_KEY_LEFT: return XXWIDGETS_KEY_LEFT;
        case TUI_KEY_RIGHT: return XXWIDGETS_KEY_RIGHT;
        case TUI_KEY_UP: return XXWIDGETS_KEY_UP;
        case TUI_KEY_DOWN: return XXWIDGETS_KEY_DOWN;
        case TUI_KEY_HOME: return XXWIDGETS_KEY_HOME;
        case TUI_KEY_END: return XXWIDGETS_KEY_END;
        case TUI_KEY_PAGEUP: return XXWIDGETS_KEY_PAGEUP;
        case TUI_KEY_PAGEDOWN: return XXWIDGETS_KEY_PAGEDOWN;
        default: return (unsigned int)key;
    }
}

typedef struct tui_plot_context { tui_state *state; tui_clip clip; long long x, y; } tui_plot_context;
static void tui_plot_draw(void *user, int kind, double x, double y, double x2, double y2, uint32_t rgb, const char *text)
{
    tui_plot_context *p = user;
    unsigned char style = rgb == 0xd62222 ? TUI_RED : rgb == 0x2778c4 ? TUI_BLUE : rgb == 0xdce7f5 ? TUI_FOCUS : TUI_NORMAL;
    if (kind == 2) { tui_text(p->state,p->clip,p->x+(long long)x,p->y+(long long)y,text,style,0); return; }
    if (kind == 1) {
        for (long long row=(long long)y;row<(long long)y2;++row)
            for (long long col=(long long)x;col<(long long)x2;++col)
                tui_put(p->state,p->clip,p->x+col,p->y+row,rgb==0x2778c4?'#':' ',style);
    } else {
        double dx=x2-x,dy=y2-y;
        int steps=(int)((dx<0?-dx:dx)>(dy<0?-dy:dy)?(dx<0?-dx:dx):(dy<0?-dy:dy))+1;
        for(int i=0;i<=steps;++i) tui_put(p->state,p->clip,p->x+(long long)(x+dx*i/steps+0.5),p->y+(long long)(y+dy*i/steps+0.5),rgb==0xd62222?'*':(rgb==0xd8d8d8?'.':(dx==0?'|':'-')),style);
    }
}

static void tui_control(tui_state *state, xxwidgets_widget *widget, tui_clip client)
{
    long long x = client.left + widget->rect.x, y = client.top + widget->rect.y;
    tui_clip bounds = {x, y, x + widget->rect.width, y + widget->rect.height};
    tui_clip screen = {0, 0, state->columns, state->rows};
    tui_clip clip = tui_intersect(tui_intersect(bounds, client), screen);
    unsigned char style = (!widget->enabled || !widget->parent->enabled) ? TUI_DISABLED : (state->focus == widget ? TUI_FOCUS : TUI_NORMAL);
    long long col, row;
    char label[32];
    if (!widget->visible || clip.left >= clip.right || clip.top >= clip.bottom) return;
    if ((widget->kind == XXWIDGETS_BUTTON || widget->kind == XXWIDGETS_CHECKBOX || widget->kind == XXWIDGETS_COMBOBOX || widget->kind == XXWIDGETS_CHECKCOMBOBOX) &&
        clip.bottom > y + 1)
        clip.bottom = y + 1;
    if (widget->kind == XXWIDGETS_HEXVIEW && style == TUI_FOCUS)
        style = TUI_NORMAL;
    tui_fill(state, clip, style);
    switch (widget->kind) {
        case XXWIDGETS_SPLITTER:
            for(row=0;row<widget->rect.height;++row)for(col=0;col<widget->rect.width;++col)tui_put(state,clip,x+col,y+row,widget->rect.height>widget->rect.width?'|':'=',style);break;
        case XXWIDGETS_CANVAS: {
            if(!widget->canvas_pixels||!widget->canvas_width||!widget->canvas_height)break;double ox,oy,rw,rh;xxwidgets_canvas_fit(widget,widget->rect.width,widget->rect.height,&ox,&oy,&rw,&rh);static const char shades[]=" .:-=+*#%@";
            for(row=0;row<widget->rect.height;++row)for(col=0;col<widget->rect.width;++col){if(col<ox||row<oy||col>=ox+rw||row>=oy+rh)continue;unsigned px=(unsigned)((col-ox)*widget->canvas_width/rw),py=(unsigned)((row-oy)*widget->canvas_height/rh);uint32_t c=widget->canvas_pixels[(size_t)py*widget->canvas_width+px];unsigned r=c>>24,g=(c>>16)&255,b=(c>>8)&255,gray=(r*3+g*6+b)/10;unsigned char tint=r>g*3/2&&r>b*3/2?TUI_RED:b>r*3/2&&b>g*3/2?TUI_BLUE:style;tui_put(state,clip,x+col,y+row,shades[gray*9/255],tint);}
            for(size_t i=0;i<widget->canvas_label_count;++i){xxwidgets_canvas_label *l=widget->canvas_labels+i;tui_text(state,clip,x+(long long)(ox+l->x*rw/widget->canvas_width),y+(long long)(oy+l->y*rh/widget->canvas_height),l->text,style,TUI_TEXT_ELLIPSIS);}
            if(widget->value>=0)tui_put(state,clip,x+(long long)(ox+(widget->value%widget->canvas_width)*rw/widget->canvas_width),y+(long long)(oy+(widget->value/widget->canvas_width)*rh/widget->canvas_height),'X',TUI_FOCUS);break;
        }
        case XXWIDGETS_ENTROPY_PLOT: {
            tui_plot_context plot = {state,clip,x,y};
            xxwidgets_entropy_draw(widget,widget->rect.width,widget->rect.height,1,1,tui_plot_draw,&plot);
            break;
        }
        case XXWIDGETS_LABEL: tui_text(state, clip, x, y, widget->text, style, TUI_TEXT_MULTILINE | TUI_TEXT_ELLIPSIS); break;
        case XXWIDGETS_BUTTON:
            tui_put(state, clip, x, y, '[', style);
            tui_put(state, clip, bounds.right - 1, y, ']', style);
            bounds.left = x + 1;
            bounds.right--;
            tui_text(state, tui_intersect(clip, bounds), x + 1, y, widget->text, style, TUI_TEXT_ELLIPSIS);
            break;
        case XXWIDGETS_CHECKBOX:
            tui_text(state, clip, x, y, widget->value ? "[x] " : "[ ] ", style, 0);
            tui_text(state, clip, x + 4, y, widget->text, style, TUI_TEXT_ELLIPSIS);
            break;
        case XXWIDGETS_COMBOBOX:
        case XXWIDGETS_CHECKCOMBOBOX:
            tui_put(state, clip, x, y, '[', style);
            tui_put(state, clip, bounds.right - 2, y, 0x25be, style);
            tui_put(state, clip, bounds.right - 1, y, ']', style);
            bounds.left = x + 1;
            bounds.right -= 3;
            tui_text(state, tui_intersect(clip, bounds), x + 1, y, xxwidgets_combobox_caption(widget), style, TUI_TEXT_ELLIPSIS);
            break;
        case XXWIDGETS_EDIT: {
            tui_edit *edit = (tui_edit *)widget->platform;
            size_t count = tui_edit_view(widget), start = edit ? edit->start : 0, visible = widget->rect.width > 2 ? (size_t)widget->rect.width - 2 : 0;
            tui_put(state, clip, x, y, '[', style);
            tui_put(state, clip, bounds.right - 1, y, ']', style);
            bounds.left = x + 1;
            bounds.right--;
            tui_text(state, tui_intersect(clip, bounds), x + 1, y, widget->text + start, style, 0);
            col = x + 1 + (long long)count;
            if (state->focus == widget && visible && col >= clip.left && col < clip.right && y >= clip.top && y < clip.bottom && col < bounds.right) {
                state->cursor_x = (int)col;
                state->cursor_y = (int)y;
                state->cursor_visible = 1;
            }
            break;
        }
        case XXWIDGETS_LISTBOX:
        case XXWIDGETS_ARCHIVEVIEW:
        case XXWIDGETS_ARCHIVEBROWSER:
        case XXWIDGETS_SCANRESULTS:
        case XXWIDGET_RESOURCESVIEWER:
        case XXWIDGET_IMPORTSVIEWER:
        case XXWIDGET_EXPORTSVIEWER:
        case XXWIDGET_SYMBOLSVIEWER:
        case XXWIDGET_SYSTEMPROCESSESVIEWER:
        case XXWIDGETS_ENTROPY_REGIONS:
        case XXWIDGETS_ENTROPY_BYTES:
        case XXWIDGETS_DATA_STRUCT_TABLE:
        case XXWIDGETS_TREEVIEW:
        case XXWIDGETS_HEXVIEW: {
            size_t first = 0;
            size_t byte_cursor = SIZE_MAX;
            unsigned int cursor_nibble = 0;
            int hex_editable = 0;
            size_t selected_row = widget->kind == XXWIDGETS_TREEVIEW ? xxwidgets_treeview_row_of_node(widget, (size_t)widget->value)
                                  : widget->value < 0                ? SIZE_MAX
                                                                     : (size_t)widget->value;
            int header_rows = widget->kind == XXWIDGETS_ARCHIVEBROWSER
                                  ? 2
                                  : (xxwidgets_tabular_kind(widget)) ? 1 : 0;
            int row_height = widget->rect.height - header_rows;
            tui_hexview *hexview = (tui_hexview *)widget->platform;
            if (widget->kind == XXWIDGETS_HEXVIEW) {
                (void)xxwidgets_hexview_get_editable(widget, &hex_editable);
                (void)xxwidgets_hexview_get_cursor(widget, &byte_cursor, &cursor_nibble);
            }
            if (hexview && hexview->horizontal) {
                /* Only while panned, and rows measured before are not walked again. */
                size_t maximum = tui_hexview_horizontal_max(state, widget);
                if (hexview->horizontal > maximum) hexview->horizontal = maximum;
            }
            if (widget->kind == XXWIDGETS_ARCHIVEBROWSER) {
                const char *archive = xxwidgets_archivebrowser_archive(widget);
                const char *directory = xxwidgets_archivebrowser_directory(widget);
                size_t address_length = strlen(archive) + strlen(directory) + 8;
                char *address = (char *)malloc(address_length);
                if (address) {
                    snprintf(address, address_length, "[^] %s:/%s", archive, directory);
                    tui_text(state, clip, x, y, address, style, 0);
                    free(address);
                }
                long long name_width = (long long)xxwidgets_archivebrowser_name_columns(widget);
                long long header_x = x + 1 - (long long)(hexview ? hexview->horizontal : 0);
                tui_clip header_clip = clip;
                header_clip.top = y + 1;
                header_clip.bottom = y + 2;
                header_clip = tui_intersect(clip, header_clip);
                tui_fill(state, header_clip, TUI_TITLE);
                tui_text(state, header_clip, header_x + 4, y + 1, "Name", TUI_TITLE, 0);
                tui_text(state, header_clip, header_x + 4 + name_width + 18, y + 1, "Size", TUI_TITLE, 0);
                tui_text(state, header_clip, header_x + 4 + name_width + 33, y + 1, "Packed Size", TUI_TITLE, 0);
                tui_text(state, header_clip, header_x + 4 + name_width + 46, y + 1, "Modified", TUI_TITLE, 0);
                tui_text(state, header_clip, header_x + 4 + name_width + 67, y + 1, "Attributes", TUI_TITLE, 0);
            } else if (xxwidgets_tabular_kind(widget)) {
                long long header_x = x + 1 - (long long)(hexview ? hexview->horizontal : 0);
                size_t column, column_count = xxwidgets_tabular_column_count(widget);
                tui_clip header_clip = clip;
                header_clip.top = y;
                header_clip.bottom = y + 1;
                header_clip = tui_intersect(clip, header_clip);
                tui_fill(state, header_clip, TUI_TITLE);
                for (column = 0; column < column_count; ++column) {
                    size_t width = xxwidgets_tabular_column_width(widget, column);
                    tui_clip cell_clip = header_clip;
                    if (widget->kind == XXWIDGETS_HEXVIEW) {
                        cell_clip.left = header_x; cell_clip.right = header_x + (long long)width;
                        cell_clip = tui_intersect(header_clip, cell_clip);
                        tui_put(state, header_clip, header_x + (long long)width, y, '|', TUI_TITLE);
                    }
                    tui_text(state, cell_clip, header_x, y, xxwidgets_tabular_column_title(widget, column),
                        widget->kind == XXWIDGETS_HEXVIEW && state->focus == widget && column == widget->hex_header ? TUI_FOCUS : TUI_TITLE, 0);
                    header_x += (long long)width + (widget->kind == XXWIDGETS_HEXVIEW ? 1 : widget->kind == XXWIDGETS_DATA_STRUCT_TABLE ? 3 : 2);
                }
            }
            if (row_height <= 0) break;
            /* The view keeps its place; it moves only to bring the selected row
             * into view, so Up from the bottom moves the highlight, not the list. */
            first = hexview ? hexview->first : 0;
            if (selected_row != SIZE_MAX) {
                if (selected_row < first) first = selected_row;
                else if (selected_row >= first + (size_t)row_height) first = selected_row - (size_t)row_height + 1;
            }
            if (widget->item_count <= (size_t)row_height) first = 0;
            else if (first > widget->item_count - (size_t)row_height) first = widget->item_count - (size_t)row_height;
            if (hexview) hexview->first = first;
            for (row = clip.top; row < clip.bottom; ++row) {
                size_t item;
                size_t offset = 0, remaining = hexview ? hexview->horizontal : 0;
                unsigned char item_style = widget->kind == XXWIDGETS_HEXVIEW && widget->enabled && widget->parent->enabled ? TUI_NORMAL : style;
                if (row < y + header_rows) continue;
                item = first + (size_t)(row - y - header_rows);
                if (item >= widget->item_count) break;
                while (remaining && widget->items[item][offset]) {
                    offset = tui_next(widget->items[item], offset);
                    --remaining;
                }
                if (widget->kind == XXWIDGETS_TREEVIEW && widget->enabled && widget->parent->enabled) {
                    xxwidgets_tree_color colors = xxwidgets_treeview_node_color(widget, xxwidgets_treeview_node_at_row(widget, item));
                    uint32_t color = colors.dark_foreground;
                    switch (color) {
                    case 0xff0000: case 0x800000: item_style = TUI_RED; break;
                    case 0x00ff00: case 0x008000: item_style = TUI_GREEN; break;
                    case 0x0000ff: case 0x000080: item_style = TUI_BLUE; break;
                    case 0xffff00: case 0x808000: item_style = TUI_OLIVE; break;
                    case 0x00ffff: case 0x008080: item_style = TUI_TEAL; break;
                    case 0xc0c0c0: case 0x808080: item_style = TUI_GRAY; break;
                    default: break;
                    }
                    if (colors.dark_background == 0xff0000 || colors.dark_background == 0x800000) item_style = TUI_MALWARE;
                }
                if (widget->kind != XXWIDGETS_HEXVIEW &&
                    (item == selected_row || (widget->kind == XXWIDGETS_ARCHIVEBROWSER && xxwidgets_archivebrowser_row_selected(widget, item))) &&
                    widget->enabled && widget->parent->enabled)
                    item_style = TUI_FOCUS;
                bounds.top = row;
                bounds.bottom = row + 1;
                tui_fill(state, tui_intersect(clip, bounds), item_style);
                tui_put(state, clip, x, row,
                        item == selected_row                                                                              ? '>'
                        : widget->kind == XXWIDGETS_ARCHIVEBROWSER && xxwidgets_archivebrowser_row_selected(widget, item) ? '*'
                                                                                                                          : ' ',
                        item_style);
                if (widget->kind == XXWIDGETS_HEXVIEW) {
                    long long cell_x = x + 1 - (long long)(hexview ? hexview->horizontal : 0);
                    for (size_t column = 0; column < 3; ++column) {
                        size_t width = xxwidgets_hexview_column_width(widget, column);
                        tui_clip cell_clip = clip;
                        cell_clip.left = cell_x; cell_clip.right = cell_x + (long long)width;
                        cell_clip = tui_intersect(clip, cell_clip);
                        const char *text = xxwidgets_hexview_cell(widget, item, column);
                        if (column == 2) {
                            xxwidgets_hex_span symbols[64];
                            size_t count = xxwidgets_hexview_spans(widget, item, column, symbols);
                            for (size_t n = 0; n < count; ++n) {
                                xxwidgets_hex_span symbol = symbols[n];
                                size_t cell, cells;
                                xxwidgets_hexview_symbol_layout(widget, item, &symbol, &cell, &cells);
                                long long left = cell_x + (long long)cell;
                                tui_clip slot = {left, row, left + (long long)cells, row + 1};
                                slot = tui_intersect(cell_clip, slot);
                                unsigned char symbol_style = xxwidgets_hexview_contains(widget, symbol.offset, symbol.bytes) ? TUI_FOCUS : item_style;
                                tui_fill(state, slot, symbol_style);
                                size_t continuation = 0;
                                if (symbol.length) {
                                    char glyph_text[8];
                                    memcpy(glyph_text, text + symbol.start, symbol.length);
                                    glyph_text[symbol.length] = 0;
                                    size_t length;
                                    uint32_t glyph = tui_decode(glyph_text, symbol.length, &length);
                                    if (tui_invisible(glyph) || tui_hidden_selector(0, glyph, 0)) glyph = 0xfffd;
                                    int glyph_cells = tui_width(glyph);
                                    tui_text(state, slot, left, row, glyph_text, symbol_style, 0);
                                    /* Wide terminal glyphs necessarily occupy two cells.
                                     * Mark every continuation cell beyond the glyph. */
                                    continuation = glyph_cells > 1 ? (size_t)glyph_cells : 1;
                                }
                                for (; continuation < cells; ++continuation)
                                    tui_put(state, slot, left + (long long)continuation, row, 0x00b7,
                                        symbol_style == TUI_FOCUS ? TUI_FOCUS_DISABLED : TUI_DISABLED);
                                uint32_t tint;
                                if (symbol_style != TUI_FOCUS && xxwidgets_hexview_highlight_span(widget, symbol.offset, symbol.bytes, &tint))
                                    for (long long px = slot.left; px < slot.right; ++px)
                                        if (px >= 0 && px < state->columns && row >= 0 && row < state->rows)
                                            state->cells[(size_t)row * (size_t)state->columns + (size_t)px].background = tint;

                            }
                            tui_put(state, clip, cell_x + (long long)width, row, '|', item_style);
                            cell_x += (long long)width + 1;
                            continue;
                        }
                        tui_text(state, cell_clip, cell_x, row, text, item_style, 0);
                        xxwidgets_hex_span spans[64];
                        size_t all_count = xxwidgets_hexview_spans(widget, item, column, spans);
                        for (size_t n = 0; n < all_count; ++n) {
                            uint32_t tint;
                            if (!xxwidgets_hexview_highlight_span(widget, spans[n].offset, spans[n].bytes, &tint)) continue;
                            long long left = cell_x + (long long)spans[n].start;
                            for (size_t k = 0; k < spans[n].length; ++k) {
                                long long px = left + (long long)k;
                                if (px >= cell_clip.left && px < cell_clip.right && px >= 0 && px < state->columns && row >= 0 && row < state->rows)
                                    state->cells[(size_t)row * (size_t)state->columns + (size_t)px].background = tint;
                            }
                        }
                        size_t count = xxwidgets_hexview_selection_spans(widget, item, column, spans);
                        for (size_t n = 0; n < count; ++n) {
                            long long dx = 0;
                            for (size_t k = 0; k < spans[n].start;) {
                                size_t length;
                                uint32_t glyph = tui_decode(text + k, spans[n].start - k, &length);
                                if (!length)
                                    break;
                                dx += tui_width(tui_invisible(glyph) ? 0xfffd : glyph);
                                k += length;
                            }
                            char selected[1024];
                            memcpy(selected, text + spans[n].start, spans[n].length);
                            selected[spans[n].length] = 0;
                            tui_text(state, cell_clip, cell_x + dx, row, selected, TUI_FOCUS, 0);
                        }
                        tui_put(state, clip, cell_x + (long long)width, row, '|', item_style);
                        cell_x += (long long)width + 1;
                    }
                } else {
                tui_text(state, clip, x + 1, row, widget->items[item] + offset, item_style, TUI_TEXT_ELLIPSIS);
                }
                if (widget->kind == XXWIDGETS_HEXVIEW && hex_editable && state->focus == widget && widget->enabled && widget->parent->enabled &&
                    byte_cursor != SIZE_MAX && item == byte_cursor / widget->hex_columns) {
                    size_t cursor_column = xxwidgets_hexview_cursor_column(widget);
                    size_t columns_end = xxwidgets_hexview_column_width(widget, 0) + 1 + xxwidgets_hexview_column_width(widget, 1) + 1 +
                        xxwidgets_hexview_column_width(widget, 2);
                    if (cursor_column < columns_end && !widget->hex_menu_open) {
                        long long cursor_x = x + 1 + (long long)cursor_column - (long long)(hexview ? hexview->horizontal : 0);
                        if (cursor_column >= (hexview ? hexview->horizontal : 0) && cursor_x >= clip.left && cursor_x < clip.right) {
                            state->cursor_x = (int)cursor_x;
                            state->cursor_y = (int)row;
                            state->cursor_visible = 1;
                        }
                    }
                }
            }
            if (widget->kind == XXWIDGETS_HEXVIEW && widget->hex_menu_open && state->focus == widget) {
                size_t count = xxwidgets_hexview_menu_count(widget->hex_menu_open == 2 ? 3 : widget->hex_header);
                size_t height = row_height > 0 ? (size_t)row_height : 1;
                size_t first_item = widget->hex_menu_index >= height ? widget->hex_menu_index - height + 1 : 0;
                for (size_t i = first_item; i < count && i < first_item + height; ++i) {
                    char label[128];
                    xxwidgets_hex_menu_item entry = xxwidgets_hexview_menu_item(widget, widget->hex_menu_open == 2 ? 3 : widget->hex_header, i);
                    long long menu_y = y + 1 + (long long)(i - first_item);
                    unsigned char menu_style = entry.disabled ? TUI_DISABLED : i == widget->hex_menu_index ? TUI_FOCUS : TUI_TITLE;
                    tui_clip menu_clip = clip; menu_clip.top = menu_y; menu_clip.bottom = menu_y + 1;
                    menu_clip = tui_intersect(clip, menu_clip); tui_fill(state, menu_clip, menu_style);
                    snprintf(label, sizeof(label), " [%c] %s%s%s", entry.checked ? 'x' : ' ', entry.group, entry.group[0] ? " > " : "", entry.title);
                    tui_text(state, menu_clip, x, menu_y, label, menu_style, TUI_TEXT_ELLIPSIS);
                }
            }
            break;
        }
        case XXWIDGETS_PROGRESS: {
            long long width = widget->rect.width > 2 ? widget->rect.width - 2 : 0;
            long long filled = width * widget->value / 100;
            tui_put(state, clip, x, y, '[', style);
            tui_put(state, clip, bounds.right - 1, y, ']', style);
            for (col = clip.left; col < clip.right; ++col)
                if (col > x && col < bounds.right - 1) tui_put(state, clip, col, y, col - x - 1 < filled ? '#' : '-', style);
            if (width >= 6) {
                snprintf(label, sizeof(label), " %d%% ", widget->value);
                tui_text(state, clip, x + 1 + (width - (long long)strlen(label)) / 2, y, label, style, 0);
            }
            break;
        }
        default: break;
    }
}

/* A modal window stays on the terminal: one centred in a main window taller
 * than a short terminal would hang off the bottom. One taller than the
 * terminal starts at its top, so its title and upper rows show. */
static long long tui_window_top(const tui_state *state, const xxwidgets_widget *window)
{
    long long y = window->rect.y, height = (long long)window->rect.height + 2;
    if (window == window->app->modal_window && y + height > state->rows) y = height <= state->rows ? state->rows - height : 0;
    return y;
}

static void tui_window(xxwidgets_app *app, xxwidgets_widget *window)
{
    tui_state *state = (tui_state *)app->platform;
    long long x = window->rect.x, y = tui_window_top(state, window);
    tui_clip screen = {0, 0, state->columns, state->rows};
    tui_clip outer = {x, y, x + (long long)window->rect.width + 2, y + (long long)window->rect.height + 2};
    tui_clip client = {x + 1, y + 1, outer.right - 1, outer.bottom - 1};
    tui_clip title = {x + 2, y, outer.right - 2, y + 1};
    xxwidgets_widget *widget;
    if (!window->visible) return;
    tui_fill(state, outer, TUI_NORMAL);
    tui_box(state, screen, x, y, (long long)window->rect.width + 2, (long long)window->rect.height + 2, window->enabled ? TUI_TITLE : TUI_DISABLED);
    tui_text(state, tui_intersect(title, screen), x + 2, y, window->text, window->enabled ? TUI_TITLE : TUI_DISABLED, 0);
    for (widget = app->widgets; widget; widget = widget->next)
        if (widget->parent == window) tui_control(state, widget, client);
}

#ifndef _WIN32
static int tui_write(const char *buffer, size_t length)
{
    while (length) {
        ssize_t written = write(STDOUT_FILENO, buffer, length);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return 0;
        buffer += written;
        length -= (size_t)written;
    }
    return 1;
}

static uint64_t tui_now(void)
{
    struct timespec time = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000;
}
#endif

static xxwidgets_status tui_dimensions(tui_state *state)
{
    int columns = 80, rows = 25;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(state->output, &info)) return XXWIDGETS_PLATFORM_ERROR;
    columns = info.srWindow.Right - info.srWindow.Left + 1;
    rows = info.srWindow.Bottom - info.srWindow.Top + 1;
#else
    struct winsize size;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col && size.ws_row) {
        columns = size.ws_col;
        rows = size.ws_row;
    }
#endif
    /* Defensive allocation cap for unusual or malicious terminal dimensions. */
    if (columns > 1000) columns = 1000;
    if (rows > 500) rows = 500;
    if (columns < 1) columns = 1;
    if (rows < 1) rows = 1;
    if (columns != state->columns || rows != state->rows) {
        size_t needed = (size_t)columns * (size_t)rows;
        if (needed > state->capacity) {
            tui_cell *cells = (tui_cell *)realloc(state->cells, needed * sizeof(*cells));
            if (!cells) return XXWIDGETS_OUT_OF_MEMORY;
            state->cells = cells;
            state->capacity = needed;
        }
        state->columns = columns;
        state->rows = rows;
        state->dirty = 1;
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status tui_present(tui_state *state)
{
#ifdef _WIN32
    static const WORD colors[] = {FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, FOREGROUND_INTENSITY,
                                  FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY | BACKGROUND_BLUE,
                                  FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY | BACKGROUND_BLUE | BACKGROUND_GREEN,
                                  FOREGROUND_RED | FOREGROUND_INTENSITY, FOREGROUND_GREEN | FOREGROUND_INTENSITY,
                                  FOREGROUND_BLUE | FOREGROUND_INTENSITY, FOREGROUND_RED | FOREGROUND_GREEN,
                                  FOREGROUND_GREEN | FOREGROUND_BLUE, FOREGROUND_INTENSITY,
                                  FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY | BACKGROUND_RED,
                                  FOREGROUND_INTENSITY | BACKGROUND_BLUE};
    size_t count = (size_t)state->columns * (size_t)state->rows, i;
    CHAR_INFO *output = (CHAR_INFO *)malloc(count * sizeof(*output));
    CONSOLE_SCREEN_BUFFER_INFO info;
    CONSOLE_CURSOR_INFO cursor;
    COORD dimensions, origin = {0, 0}, position;
    SMALL_RECT region;
    if (!output) return XXWIDGETS_OUT_OF_MEMORY;
    if (!GetConsoleScreenBufferInfo(state->output, &info)) {
        free(output);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    for (i = 0; i < count; ++i) {
        output[i].Char.UnicodeChar = (WCHAR)(state->cells[i].glyph <= 0xffff ? state->cells[i].glyph : 0xfffd);
        output[i].Attributes = colors[state->cells[i].style];
        uint32_t bg = state->cells[i].background;
        if (bg != UINT32_MAX) {
            WORD value = ((bg >> 16) & 255) > 96 ? BACKGROUND_RED : 0;
            if (((bg >> 8) & 255) > 96) value |= BACKGROUND_GREEN;
            if ((bg & 255) > 96) value |= BACKGROUND_BLUE;
            output[i].Attributes = (output[i].Attributes & 15) | value;
        }
    }
    dimensions.X = (SHORT)state->columns;
    dimensions.Y = (SHORT)state->rows;
    region.Left = info.srWindow.Left;
    region.Top = info.srWindow.Top;
    region.Right = (SHORT)(region.Left + state->columns - 1);
    region.Bottom = (SHORT)(region.Top + state->rows - 1);
    if (!WriteConsoleOutputW(state->output, output, dimensions, origin, &region)) {
        free(output);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    free(output);
    cursor.dwSize = 25;
    cursor.bVisible = state->cursor_visible;
    if (!SetConsoleCursorInfo(state->output, &cursor)) return XXWIDGETS_PLATFORM_ERROR;
    if (state->cursor_visible) {
        position.X = (SHORT)(info.srWindow.Left + state->cursor_x);
        position.Y = (SHORT)(info.srWindow.Top + state->cursor_y);
        if (!SetConsoleCursorPosition(state->output, position)) return XXWIDGETS_PLATFORM_ERROR;
    }
#else
    static const char *styles[] = {"\033[0m", "\033[0;90m", "\033[0;97;44m", "\033[0;97;46m", "\033[0;91m", "\033[0;92m", "\033[0;94m", "\033[0;93m", "\033[0;96m", "\033[0;37m", "\033[0;97;41m", "\033[0;90;44m"};
    size_t capacity = (size_t)state->columns * (size_t)state->rows * 64 + (size_t)state->rows * 32 + 128, length = 0;
    char *output = (char *)malloc(capacity);
    int x, y, previous_style = -1;
    uint32_t previous_background = UINT32_MAX;
    if (!output) return XXWIDGETS_OUT_OF_MEMORY;
    length += (size_t)snprintf(output + length, capacity - length, "\033[?25l");
    for (y = 0; y < state->rows; ++y) {
        length += (size_t)snprintf(output + length, capacity - length, "\033[%d;1H", y + 1);
        for (x = 0; x < state->columns; ++x) {
            const tui_cell *cell = state->cells + (size_t)y * (size_t)state->columns + (size_t)x;
            if (cell->style != previous_style || cell->background != previous_background) {
                size_t size = strlen(styles[cell->style]);
                memcpy(output + length, styles[cell->style], size);
                length += size;
                previous_style = cell->style;
                previous_background = cell->background;
                if (cell->background != UINT32_MAX)
                    length += (size_t)snprintf(output + length, capacity - length, "\033[48;2;%u;%u;%um", (cell->background >> 16) & 255, (cell->background >> 8) & 255, cell->background & 255);
            }
            /* A wide glyph is written with its tail cell and the terminal
             * advances two columns; one whose other half was overwritten
             * is drawn as spaces, so the row never shifts. */
            if (cell->glyph == TUI_WIDE_TAIL) {
                if (!x || tui_width(cell[-1].glyph) != 2) output[length++] = ' ';
            } else if (tui_width(cell->glyph) == 2 && (x + 1 >= state->columns || cell[1].glyph != TUI_WIDE_TAIL)) output[length++] = ' ';
            else length += tui_encode(cell->glyph, output + length);
        }
    }
    length += (size_t)snprintf(output + length, capacity - length, "\033[0m");
    if (state->cursor_visible) length += (size_t)snprintf(output + length, capacity - length, "\033[%d;%dH\033[?25h", state->cursor_y + 1, state->cursor_x + 1);
    if (!tui_write(output, length)) {
        free(output);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    free(output);
#endif
    return XXWIDGETS_OK;
}

/* The highlighted item of an open list: a COMBOBOX keeps its value until an
 * item is picked, a CHECKCOMBOBOX moves its value as its toggle cursor. */
static int tui_dropdown_row(const tui_state *state, const xxwidgets_widget *widget)
{
    int index = widget->kind == XXWIDGETS_COMBOBOX ? state->dropdown_index : widget->value;
    if (index >= (int)widget->item_count) index = (int)widget->item_count - 1;
    return index < 0 ? 0 : index;
}

/* Close the open list. Picking emits SELECT once, for an item that differs. */
static void tui_dropdown_pick(tui_state *state, int pick)
{
    xxwidgets_widget *widget = state->dropdown;
    int index;
    state->dropdown = NULL;
    state->dirty = 1;
    if (!pick || !widget || widget->kind != XXWIDGETS_COMBOBOX || !widget->item_count) return;
    index = tui_dropdown_row(state, widget);
    if (index != widget->value) {
        widget->value = index;
        xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, index);
    }
}

static void tui_dropdown(xxwidgets_app *app)
{
    tui_state *state = app->platform;
    xxwidgets_widget *widget = state->dropdown;
    tui_clip screen = {0, 0, state->columns, state->rows}, client;
    size_t start, row, visible;
    long long x, y, width;
    if (!widget || !tui_eligible(widget) || !widget->item_count || state->rows < 3) {
        state->dropdown = NULL;
        return;
    }
    visible = widget->item_count < 8 ? widget->item_count : 8;
    if (visible > (size_t)state->rows - 2) visible = (size_t)state->rows - 2;
    start = tui_dropdown_row(state, widget) >= (int)visible ? (size_t)tui_dropdown_row(state, widget) - visible + 1 : 0;
    x = widget->parent->rect.x + widget->rect.x + 1;
    y = tui_window_top(state, widget->parent) + widget->rect.y + 2;
    width = widget->rect.width > 20 ? widget->rect.width : 20;
    if (width > state->columns) width = state->columns;
    if (x + width > state->columns) x = state->columns - width;
    if (y + (long long)visible + 2 > state->rows) y = y - (long long)visible - 3;
    if (y < 0) y = 0;
    client.left = x;
    client.top = y;
    client.right = x + width;
    client.bottom = y + (long long)visible + 2;
    tui_fill(state, client, TUI_NORMAL);
    tui_box(state, screen, x, y, width, (long long)visible + 2, TUI_TITLE);
    client.left++;
    client.top++;
    client.right--;
    client.bottom--;
    for (row = 0; row < visible && start + row < widget->item_count; ++row) {
        size_t index = start + row;
        unsigned char style = tui_dropdown_row(state, widget) == (int)index ? TUI_FOCUS : TUI_NORMAL;
        tui_clip line = {client.left, client.top + (long long)row, client.right, client.top + (long long)row + 1};
        tui_fill(state, line, style);
        if (widget->kind == XXWIDGETS_CHECKCOMBOBOX) {
            tui_text(state, line, line.left, line.top, xxwidgets_checkcombobox_checked(widget, index) ? "[x] " : "[ ] ", style, 0);
            tui_text(state, line, line.left + 4, line.top, widget->items[index], style, TUI_TEXT_ELLIPSIS);
        } else tui_text(state, line, line.left, line.top, widget->items[index], style, TUI_TEXT_ELLIPSIS);
    }
}

static xxwidgets_status tui_render(xxwidgets_app *app)
{
    tui_state *state = (tui_state *)app->platform;
    xxwidgets_widget *widget, *active_window;
    xxwidgets_status status = tui_dimensions(state);
    tui_clip screen = {0, 0, state->columns, state->rows};
    if (status != XXWIDGETS_OK) return status;
    if (state->focus && !tui_eligible(state->focus)) state->focus = NULL;
    if (!state->focus) tui_focus_next(app, 0);
    if (!state->dirty) return XXWIDGETS_OK;
    state->cursor_visible = 0;
    tui_fill(state, screen, TUI_NORMAL);
    active_window = state->focus ? state->focus->parent : NULL;
    for (widget = app->widgets; widget; widget = widget->next)
        if (widget->kind == XXWIDGETS_WINDOW && widget != active_window) tui_window(app, widget);
    if (active_window) tui_window(app, active_window);
    tui_dropdown(app);
    status = tui_present(state);
    if (status == XXWIDGETS_OK) state->dirty = 0;
    return status;
}

static xxwidgets_status tui_change_edit(xxwidgets_widget *widget, size_t from, size_t to, const char *insert, size_t inserted)
{
    tui_edit *edit = (tui_edit *)widget->platform;
    size_t length = strlen(widget->text);
    size_t kept = length - (to - from);
    char *text;
    xxwidgets_status status;
    if (inserted > SIZE_MAX - kept - 1) return XXWIDGETS_OUT_OF_MEMORY;
    text = (char *)malloc(kept + inserted + 1);
    if (!text) return XXWIDGETS_OUT_OF_MEMORY;
    memcpy(text, widget->text, from);
    if (inserted) memcpy(text + from, insert, inserted);
    memcpy(text + from + inserted, widget->text + to, length - to + 1);
    status = xxwidgets_store_text(widget, text);
    free(text);
    if (status != XXWIDGETS_OK) return status;
    free(edit->seen);
    edit->seen = xxwidgets_strdup(widget->text);
    edit->caret = from + inserted;
    tui_edit_view(widget);
    ((tui_state *)widget->app->platform)->dirty = 1;
    xxwidgets_emit(widget, XXWIDGETS_EVENT_CHANGE, 0);
    return XXWIDGETS_OK;
}

static int tui_shortcut(xxwidgets_widget *widget, uint32_t key, unsigned int modifiers)
{
    if (!widget) return 0;
    if (key >= 1 && key <= 26) {
        key += 'A' - 1;
        modifiers |= XXWIDGETS_MOD_CTRL;
    } else if (key >= TUI_KEY_F1 && key < TUI_KEY_F1 + 24) key = XXWIDGETS_KEY_F1 + key - TUI_KEY_F1;
    else switch (key) {
            case TUI_KEY_ESCAPE: key = XXWIDGETS_KEY_ESCAPE; break;
            case TUI_KEY_ENTER: key = XXWIDGETS_KEY_ENTER; break;
            case TUI_KEY_BACKTAB: modifiers |= XXWIDGETS_MOD_SHIFT; /* fall through */
            case TUI_KEY_TAB: key = XXWIDGETS_KEY_TAB; break;
            case TUI_KEY_BACKSPACE: key = XXWIDGETS_KEY_BACKSPACE; break;
            case TUI_KEY_DELETE: key = XXWIDGETS_KEY_DELETE; break;
            case TUI_KEY_INSERT: key = XXWIDGETS_KEY_INSERT; break;
            case TUI_KEY_HOME: key = XXWIDGETS_KEY_HOME; break;
            case TUI_KEY_END: key = XXWIDGETS_KEY_END; break;
            case TUI_KEY_PAGEUP: key = XXWIDGETS_KEY_PAGEUP; break;
            case TUI_KEY_PAGEDOWN: key = XXWIDGETS_KEY_PAGEDOWN; break;
            case TUI_KEY_UP: key = XXWIDGETS_KEY_UP; break;
            case TUI_KEY_DOWN: key = XXWIDGETS_KEY_DOWN; break;
            case TUI_KEY_LEFT: key = XXWIDGETS_KEY_LEFT; break;
            case TUI_KEY_RIGHT: key = XXWIDGETS_KEY_RIGHT; break;
            case TUI_KEY_CONTEXT:
                key = XXWIDGETS_KEY_F1 + 9;
                modifiers |= XXWIDGETS_MOD_SHIFT;
                break;
            default: break;
        }
    return xxwidgets_shortcut_dispatch(widget->parent ? widget->parent : widget, key, modifiers);
}

static xxwidgets_status tui_key(xxwidgets_app *app, uint32_t key, unsigned int modifiers)
{
    tui_state *state = (tui_state *)app->platform;
    xxwidgets_widget *widget = state->focus;
    if (widget && widget->kind == XXWIDGETS_HEXVIEW && tui_eligible(widget)) {
        if (widget->hex_menu_open) {
            size_t count = xxwidgets_hexview_menu_count(widget->hex_menu_open == 2 ? 3 : widget->hex_header);
            state->dirty = 1;
            if (key == TUI_KEY_ESCAPE) widget->hex_menu_open = 0;
            else if (key == TUI_KEY_UP && widget->hex_menu_index) --widget->hex_menu_index;
            else if (key == TUI_KEY_DOWN && widget->hex_menu_index + 1 < count) ++widget->hex_menu_index;
            else if (key == TUI_KEY_HOME) widget->hex_menu_index = 0;
            else if (key == TUI_KEY_END) widget->hex_menu_index = (unsigned)count - 1;
            else if (key == TUI_KEY_ENTER || key == ' ') {
                unsigned column = widget->hex_menu_open == 2 ? 3 : widget->hex_header;
                if (xxwidgets_hexview_menu_item(widget, column, widget->hex_menu_index).disabled)
                    return XXWIDGETS_OK;
                widget->hex_menu_open = 0;
                return xxwidgets_hexview_menu_apply(widget, column, widget->hex_menu_index);
            } else if (widget->hex_menu_open == 1 && (key == TUI_KEY_LEFT || key == TUI_KEY_RIGHT || key == TUI_KEY_F1 + 5)) {
                xxwidgets_hexview_activate_column(widget, (widget->hex_header + (key == TUI_KEY_LEFT ? 2 : 1)) % 3);
                widget->hex_menu_index = 0;
            }
            return XXWIDGETS_OK;
        }
        if (key == TUI_KEY_F1 + 5) {
            xxwidgets_hexview_activate_column(widget, (widget->hex_header + 1) % 3);
            tui_hexview_reveal_cursor(state, widget);
            state->dirty = 1; return XXWIDGETS_OK;
        }
        if (key == TUI_KEY_CONTEXT || key == TUI_KEY_F1 + 9) {
            widget->hex_menu_open = key == TUI_KEY_CONTEXT || (modifiers & XXWIDGETS_MOD_SHIFT) ? 2 : 1;
            widget->hex_menu_index = 0;
            state->dirty = 1; return XXWIDGETS_OK;
        }
        if (key == 1 || key == 3 || key == 4) {
            int handled = 0;
            xxwidgets_status status = xxwidgets_hexview_key_input(widget, key, XXWIDGETS_MOD_CTRL, &handled);
            if (handled) {
                state->dirty = 1;
                return status;
            }
        }
        if ((key == '+' || key == '-' || key == '=') && !widget->hex_input_handler) {
            size_t width = xxwidgets_hexview_column_width(widget, widget->hex_header);
            if (key == '-' && width > 4) --width;
            else if (key != '-' && width < 4096) ++width;
            xxwidgets_status status = xxwidgets_hexview_set_column_width(widget, widget->hex_header, width);
            if (status == XXWIDGETS_OK) xxwidgets_emit(widget, XXWIDGETS_EVENT_VIEW_CHANGE, widget->hex_header);
            return status;
        }
    }
    if (tui_shortcut(widget, key, modifiers)) return XXWIDGETS_OK;
    if (key == TUI_KEY_TAB || key == TUI_KEY_BACKTAB) {
        /* A pick that started an operation has already moved focus. */
        tui_dropdown_pick(state, 1);
        if (state->focus == widget) tui_focus_next(app, key == TUI_KEY_BACKTAB);
        return XXWIDGETS_OK;
    }
    if (key == TUI_KEY_ESCAPE || key == 3 || key == 4) {
        if (state->dropdown) {
            state->dropdown = NULL;
            state->dirty = 1;
            return XXWIDGETS_OK;
        }
        xxwidgets_widget *window = widget ? widget->parent : NULL;
        if (!window)
            for (window = app->widgets; window; window = window->next)
                if (window->kind == XXWIDGETS_WINDOW && window->visible && window->enabled) break;
        if (window) xxwidgets_emit(window, XXWIDGETS_EVENT_CLOSE, 0);
        return XXWIDGETS_OK;
    }
    if (key == 12) {
        state->dirty = 1;
        return XXWIDGETS_OK;
    }
    /* Enter on an open list picks from it, even in a dialog with a default button. */
    if (key == TUI_KEY_ENTER && state->dropdown && state->dropdown == widget) {
        tui_dropdown_pick(state, 1);
        return XXWIDGETS_OK;
    }
    /* A focused dialog button owns Enter. Other controls retain the modal
     * default action, so Enter in a field still accepts the dialog. */
    if (key == TUI_KEY_ENTER && widget && tui_eligible(widget) && widget->kind == XXWIDGETS_BUTTON) {
        xxwidgets_emit(widget, XXWIDGETS_EVENT_CLICK, 0);
        return XXWIDGETS_OK;
    }
    if (key == TUI_KEY_ENTER && app->modal_default) {
        xxwidgets_emit(app->modal_default, XXWIDGETS_EVENT_CLICK, 0);
        return XXWIDGETS_OK;
    }
    if (!widget || !tui_eligible(widget)) return XXWIDGETS_OK;
    if(widget->kind==XXWIDGETS_CANVAS||widget->kind==XXWIDGETS_SPLITTER){int mapped=(int)tui_hexview_key(key);if(widget->kind==XXWIDGETS_CANVAS)xxwidgets_canvas_key(widget,mapped);else xxwidgets_splitter_key(widget,mapped);state->dirty=1;return XXWIDGETS_OK;}
    if (xxwidgets_combo_kind(widget)) {
        /* An open COMBOBOX list moves only its highlight; Enter, Space or Tab
         * picks it and Escape keeps the old item, as native lists do. Arrows
         * on a closed one change the item at once. */
        int open = state->dropdown == widget, highlight = open && widget->kind == XXWIDGETS_COMBOBOX;
        int index = highlight ? tui_dropdown_row(state, widget) : widget->value;
        if (key == TUI_KEY_ENTER || key == ' ') {
            if (!open) {
                if (widget->item_count) {
                    if (widget->kind == XXWIDGETS_CHECKCOMBOBOX && widget->value < 0) widget->value = 0;
                    state->dropdown = widget;
                    state->dropdown_index = widget->value;
                }
            } else if (key == ' ' && widget->kind == XXWIDGETS_CHECKCOMBOBOX) return xxwidgets_checkcombobox_user_toggle(widget, (size_t)widget->value);
            else {
                tui_dropdown_pick(state, 1);
                return XXWIDGETS_OK;
            }
        } else if (widget->item_count) {
            if (key == TUI_KEY_DOWN && index + 1 < (int)widget->item_count) ++index;
            else if (key == TUI_KEY_UP && index > 0) --index;
            else if (key == TUI_KEY_HOME) index = 0;
            else if (key == TUI_KEY_END) index = (int)widget->item_count - 1;
            if (highlight) state->dropdown_index = index;
            else if (index != widget->value) {
                widget->value = index;
                if (widget->kind == XXWIDGETS_COMBOBOX) xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, index);
            }
        }
        state->dirty = 1;
        return XXWIDGETS_OK;
    }
    if (widget->kind == XXWIDGETS_EDIT) {
        tui_edit *edit = (tui_edit *)widget->platform;
        size_t length = strlen(widget->text);
        char encoded[4];
        if (edit->caret > length) edit->caret = length;
        switch (key) {
            case TUI_KEY_LEFT: edit->caret = tui_previous(widget->text, edit->caret); break;
            case TUI_KEY_RIGHT: edit->caret = tui_next(widget->text, edit->caret); break;
            case TUI_KEY_HOME: edit->caret = 0; break;
            case TUI_KEY_END: edit->caret = length; break;
            case TUI_KEY_ENTER:
                /* As in the native backends: Enter in a field is its default action. */
                xxwidgets_emit(widget, XXWIDGETS_EVENT_ACTIVATE, 0);
                return XXWIDGETS_OK;
            case TUI_KEY_BACKSPACE:
                if (edit->caret) return tui_change_edit(widget, tui_previous(widget->text, edit->caret), edit->caret, NULL, 0);
                break;
            case TUI_KEY_DELETE:
                if (edit->caret < length) return tui_change_edit(widget, edit->caret, tui_next(widget->text, edit->caret), NULL, 0);
                break;
            default:
                if (key >= 32 && key < 0x110000 && key != 127 && !(key >= 0xd800 && key <= 0xdfff))
                    return tui_change_edit(widget, edit->caret, edit->caret, encoded, tui_encode(key, encoded));
                break;
        }
        tui_edit_view(widget);
        state->dirty = 1;
    } else if (widget->kind == XXWIDGETS_TREEVIEW) {
        tui_hexview *tree = (tui_hexview *)widget->platform;
        size_t row, next, node;
        xxwidgets_tree_node entry;
        int expanded, page_rows = widget->rect.height > 0 ? widget->rect.height : 1;
        if ((modifiers & XXWIDGETS_MOD_SHIFT) && (key == TUI_KEY_LEFT || key == TUI_KEY_RIGHT)) {
            size_t maximum = tui_hexview_horizontal_max(state, widget);
            if (tree->horizontal > maximum) tree->horizontal = maximum;
            if (key == TUI_KEY_LEFT && tree->horizontal) --tree->horizontal;
            if (key == TUI_KEY_RIGHT && tree->horizontal < maximum) ++tree->horizontal;
            state->dirty = 1;
            return XXWIDGETS_OK;
        }
        if (!widget->item_count) return XXWIDGETS_OK;
        row = xxwidgets_treeview_row_of_node(widget, (size_t)widget->value);
        if (row == SIZE_MAX) row = 0;
        next = row;
        node = xxwidgets_treeview_node_at_row(widget, row);
        if (key == TUI_KEY_UP) next = row ? row - 1 : 0;
        else if (key == TUI_KEY_DOWN) next = row + 1 < widget->item_count ? row + 1 : row;
        else if (key == TUI_KEY_HOME) next = 0;
        else if (key == TUI_KEY_END) next = widget->item_count - 1;
        else if (key == TUI_KEY_PAGEUP) next = row > (size_t)page_rows ? row - (size_t)page_rows : 0;
        else if (key == TUI_KEY_PAGEDOWN) next = widget->item_count - row > (size_t)page_rows ? row + (size_t)page_rows : widget->item_count - 1;
        else if (key == TUI_KEY_LEFT) {
            if (xxwidgets_treeview_has_children(widget, node) && xxwidgets_treeview_get_expanded(widget, node, &expanded) == XXWIDGETS_OK && expanded)
                return xxwidgets_treeview_user_expand(widget, node, 0);
            if (xxwidgets_treeview_get_node(widget, node, &entry) == XXWIDGETS_OK && entry.parent != SIZE_MAX)
                next = xxwidgets_treeview_row_of_node(widget, entry.parent);
        } else if (key == TUI_KEY_RIGHT) {
            if (xxwidgets_treeview_has_children(widget, node)) {
                if (xxwidgets_treeview_get_expanded(widget, node, &expanded) == XXWIDGETS_OK && !expanded) return xxwidgets_treeview_user_expand(widget, node, 1);
                if (row + 1 < widget->item_count) next = row + 1;
            }
        } else if (key == TUI_KEY_ENTER) {
            xxwidgets_emit(widget, XXWIDGETS_EVENT_ACTIVATE, (int)node);
            return XXWIDGETS_OK;
        } else if (key == ' ') {
            if (xxwidgets_treeview_has_children(widget, node) && xxwidgets_treeview_get_expanded(widget, node, &expanded) == XXWIDGETS_OK)
                return xxwidgets_treeview_user_expand(widget, node, !expanded);
            xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, (int)node);
            return XXWIDGETS_OK;
        } else return XXWIDGETS_OK;
        if (next != SIZE_MAX && (int)(node = xxwidgets_treeview_node_at_row(widget, next)) != widget->value) {
            widget->value = (int)node;
            state->dirty = 1;
            xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, widget->value);
        }
    } else if (xxwidgets_list_kind(widget)) {
        int selection = widget->value;
        int page_rows = widget->rect.height -
                        (widget->kind == XXWIDGETS_ARCHIVEBROWSER
                             ? 2
                             : (xxwidgets_tabular_kind(widget)) ? 1 : 0);
        if (widget->kind == XXWIDGETS_HEXVIEW && !((modifiers & XXWIDGETS_MOD_ALT) && (key == TUI_KEY_LEFT || key == TUI_KEY_RIGHT))) {
            int handled = 0;
            xxwidgets_status status = xxwidgets_hexview_key_input(widget, tui_hexview_key(key), modifiers, &handled);
            if (status != XXWIDGETS_OK) return status;
            if (handled) {
                tui_hexview_reveal_cursor(state, widget);
                state->dirty = 1;
                return XXWIDGETS_OK;
            }
        }
        if (page_rows < 1) page_rows = 1;
        if (widget->kind == XXWIDGETS_ARCHIVEBROWSER && key == 1) {
            xxwidgets_status status = xxwidgets_archivebrowser_select_all(widget);
            if (status == XXWIDGETS_OK) xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, widget->value);
            return status;
        }
        if ((widget->kind == XXWIDGETS_ARCHIVEBROWSER && key == TUI_KEY_CONTEXT) ||
            (widget->kind == XXWIDGET_SYSTEMPROCESSESVIEWER && (key == TUI_KEY_CONTEXT || key == TUI_KEY_F1 + 9))) {
            /* The first row as the next render places it: keys read in the
             * same batch may have moved the selection since the last one. */
            int first = widget->platform ? (int)((tui_hexview *)widget->platform)->first : 0;
            int anchor_y;
            if (selection >= 0 && selection < first) first = selection;
            else if (selection >= first + page_rows) first = selection - page_rows + 1;
            int header_rows = widget->kind == XXWIDGET_SYSTEMPROCESSESVIEWER ? 1 : 2;
            anchor_y = selection >= 0 ? selection - first + header_rows : header_rows;
            if (anchor_y >= widget->rect.height) anchor_y = widget->rect.height - 1;
            xxwidgets_emit_context(widget, selection, widget->rect.width > 1 ? 1 : 0, anchor_y);
            return XXWIDGETS_OK;
        }
        if (widget->kind == XXWIDGETS_ARCHIVEBROWSER && key == TUI_KEY_BACKSPACE) return xxwidgets_archivebrowser_user_up(widget);
        if (widget->kind == XXWIDGETS_ARCHIVEBROWSER && key >= '1' && key <= '5')
            return xxwidgets_archivebrowser_user_sort(widget, (xxwidgets_archive_column)(key - '1'));
        if (widget->platform && (key == TUI_KEY_LEFT || key == TUI_KEY_RIGHT)) {
            /* Sideways, for rows wider than the view (the operation log too). */
            tui_hexview *hexview = (tui_hexview *)widget->platform;
            size_t maximum = tui_hexview_horizontal_max(state, widget);
            if (hexview->horizontal > maximum) hexview->horizontal = maximum;
            if (key == TUI_KEY_LEFT && hexview->horizontal) --hexview->horizontal;
            if (key == TUI_KEY_RIGHT && hexview->horizontal < maximum) ++hexview->horizontal;
            state->dirty = 1;
            return XXWIDGETS_OK;
        }
        if (!widget->item_count) return XXWIDGETS_OK;
        if (key == TUI_KEY_UP) selection = selection <= 0 ? 0 : selection - 1;
        else if (key == TUI_KEY_DOWN) selection = selection < 0 ? 0 : ((size_t)selection + 1 < widget->item_count ? selection + 1 : selection);
        else if (key == TUI_KEY_HOME) selection = 0;
        else if (key == TUI_KEY_END) selection = (int)widget->item_count - 1;
        else if (key == TUI_KEY_PAGEUP) selection = selection <= page_rows ? 0 : selection - page_rows;
        else if (key == TUI_KEY_PAGEDOWN) {
            size_t next = selection < 0 ? 0 : (size_t)selection + (size_t)page_rows;
            selection = (int)(next < widget->item_count ? next : widget->item_count - 1);
        } else if (key == TUI_KEY_ENTER || key == ' ') {
            xxwidgets_status status;
            if (selection < 0) selection = 0;
            status = widget->kind == XXWIDGETS_HEXVIEW ? xxwidgets_hexview_selection_input(widget, selection) : XXWIDGETS_OK;
            if (status != XXWIDGETS_OK) return status;
            if (widget->kind != XXWIDGETS_HEXVIEW) widget->value = selection;
            state->dirty = 1;
            if ((widget->kind == XXWIDGET_SYSTEMPROCESSESVIEWER || xxwidgets_binaryviewer_kind(widget)) && key == TUI_KEY_ENTER) {
                xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, selection);
                xxwidgets_emit(widget, XXWIDGETS_EVENT_ACTIVATE, selection);
                return XXWIDGETS_OK;
            }
            if (widget->kind == XXWIDGETS_ARCHIVEBROWSER && key == TUI_KEY_ENTER) return xxwidgets_archivebrowser_user_activate(widget, selection);
            if (widget->kind == XXWIDGETS_ARCHIVEBROWSER && key == ' ')
                xxwidgets_archivebrowser_selection_input(widget, (size_t)selection, !xxwidgets_archivebrowser_row_selected(widget, (size_t)selection));
            xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, selection);
            return XXWIDGETS_OK;
        } else return XXWIDGETS_OK;
        if (selection != widget->value) {
            xxwidgets_status status = widget->kind == XXWIDGETS_HEXVIEW ? xxwidgets_hexview_selection_input(widget, selection) : XXWIDGETS_OK;
            if (status != XXWIDGETS_OK) return status;
            if (widget->kind != XXWIDGETS_HEXVIEW) widget->value = selection;
            state->dirty = 1;
            if (widget->kind == XXWIDGETS_ARCHIVEBROWSER) {
                xxwidgets_archivebrowser_selection_clear(widget);
                xxwidgets_archivebrowser_selection_input(widget, (size_t)selection, 1);
            }
            xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, selection);
        }
    } else if (key == TUI_KEY_ENTER || key == ' ') {
        if (widget->kind == XXWIDGETS_BUTTON) xxwidgets_emit(widget, XXWIDGETS_EVENT_CLICK, 0);
        else if (widget->kind == XXWIDGETS_CHECKBOX) {
            widget->value = !widget->value;
            state->dirty = 1;
            xxwidgets_emit(widget, XXWIDGETS_EVENT_CHANGE, widget->value);
        }
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status tui_init(xxwidgets_app *app)
{
    tui_state *state;
    if (tui_active) return XXWIDGETS_BUSY;
    state = (tui_state *)calloc(1, sizeof(*state));
    if (!state) return XXWIDGETS_OUT_OF_MEMORY;
#ifdef _WIN32
    DWORD mode;
    CONSOLE_SCREEN_BUFFER_INFO info;
    state->input = GetStdHandle(STD_INPUT_HANDLE);
    state->original_output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleMode(state->input, &state->original_input_mode) || !GetConsoleMode(state->original_output, &mode)) {
        free(state);
        return XXWIDGETS_UNAVAILABLE;
    }
    state->output = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
    if (state->output == INVALID_HANDLE_VALUE) {
        free(state);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    if (GetConsoleScreenBufferInfo(state->original_output, &info)) {
        COORD size;
        SMALL_RECT window;
        size.X = (SHORT)(info.srWindow.Right - info.srWindow.Left + 1);
        size.Y = (SHORT)(info.srWindow.Bottom - info.srWindow.Top + 1);
        window.Left = window.Top = 0;
        window.Right = (SHORT)(size.X - 1);
        window.Bottom = (SHORT)(size.Y - 1);
        SetConsoleWindowInfo(state->output, TRUE, &window);
        SetConsoleScreenBufferSize(state->output, size);
    }
    mode = ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
    if (!SetConsoleMode(state->input, mode) || !SetConsoleActiveScreenBuffer(state->output)) {
        SetConsoleMode(state->input, state->original_input_mode);
        CloseHandle(state->output);
        free(state);
        return XXWIDGETS_PLATFORM_ERROR;
    }
#else
    struct termios raw;
    const char *term = getenv("TERM");
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) || (term && !strcmp(term, "dumb"))) {
        free(state);
        return XXWIDGETS_UNAVAILABLE;
    }
    if (tcgetattr(STDIN_FILENO, &state->original_termios) != 0) {
        free(state);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    raw = state->original_termios;
    raw.c_iflag &= (tcflag_t) ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= (tcflag_t)~OPOST;
    raw.c_cflag &= (tcflag_t) ~(CSIZE | PARENB);
    raw.c_cflag |= CS8;
    raw.c_lflag &= (tcflag_t) ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        free(state);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    if (!tui_write("\033[?1049h\033[?25l\033[2J", sizeof("\033[?1049h\033[?25l\033[2J") - 1)) {
        tcsetattr(STDIN_FILENO, TCSANOW, &state->original_termios);
        tui_write("\033[0m\033[?25h\033[?1049l", sizeof("\033[0m\033[?25h\033[?1049l") - 1);
        free(state);
        return XXWIDGETS_PLATFORM_ERROR;
    }
#endif
    state->dirty = 1;
    app->platform = state;
    tui_active = 1;
    return XXWIDGETS_OK;
}

static void tui_shutdown(xxwidgets_app *app)
{
    tui_state *state = (tui_state *)app->platform;
    if (!state) return;
#ifdef _WIN32
    SetConsoleMode(state->input, state->original_input_mode);
    SetConsoleActiveScreenBuffer(state->original_output);
    CloseHandle(state->output);
#else
    tui_write("\033[0m\033[?25h\033[?1049l", sizeof("\033[0m\033[?25h\033[?1049l") - 1);
    tcsetattr(STDIN_FILENO, TCSANOW, &state->original_termios);
#endif
    free(state->cells);
    free(state);
    app->platform = NULL;
    tui_active = 0;
}

#ifndef _WIN32
static xxwidgets_status tui_parse(xxwidgets_app *app)
{
    tui_state *state = (tui_state *)app->platform;
    while (state->input_length && !app->quit) {
        size_t consumed = 1;
        uint32_t key = state->input[0];
        unsigned int modifiers = 0;
        xxwidgets_status status;
        if (key == 27) {
            if (!state->escape_since) state->escape_since = tui_now();
            if (state->input_length == 1 && tui_now() - state->escape_since < 30) break;
            if (state->input_length >= 2 && (state->input[1] == '[' || state->input[1] == 'O')) {
                size_t end = 2;
                while (end < state->input_length && !(state->input[end] >= 0x40 && state->input[end] <= 0x7e)) ++end;
                if (end == state->input_length) {
                    if (state->input_length < sizeof(state->input) && tui_now() - state->escape_since < 30) break;
                    consumed = state->input_length;
                    key = 0;
                } else {
                    consumed = end + 1;
                    unsigned int parameters[2] = {0, 0}, parameter = 0;
                    int valid = 1;
                    for (size_t i = 2; i < end; ++i) {
                        unsigned char c = state->input[i];
                        if (c == ';' && !parameter)
                            parameter = 1;
                        else if (c >= '0' && c <= '9' && parameters[parameter] < 1000)
                            parameters[parameter] = parameters[parameter] * 10 + c - '0';
                        else {
                            valid = 0;
                            break;
                        }
                    }
                    if (parameter && parameters[1] >= 1 && parameters[1] <= 16) {
                        unsigned int mask = parameters[1] - 1;
                        if (mask & 1)
                            modifiers |= XXWIDGETS_MOD_SHIFT;
                        if (mask & 2)
                            modifiers |= XXWIDGETS_MOD_ALT;
                        if (mask & 4)
                            modifiers |= XXWIDGETS_MOD_CTRL;
                        if (mask & 8)
                            modifiers |= XXWIDGETS_MOD_META;
                    } else if (parameter)
                        valid = 0;
                    switch (state->input[end]) {
                        case 'A': key = TUI_KEY_UP; break;
                        case 'B': key = TUI_KEY_DOWN; break;
                        case 'C': key = TUI_KEY_RIGHT; break;
                        case 'D': key = TUI_KEY_LEFT; break;
                        case 'H': key = TUI_KEY_HOME; break;
                        case 'F': key = TUI_KEY_END; break;
                        case 'Z': key = TUI_KEY_BACKTAB; break;
                        case 'P':
                        case 'Q':
                        case 'R':
                        case 'S': key = TUI_KEY_F1 + state->input[end] - 'P'; break;
                        case '~': {
                            static const unsigned char numbers[] = {11, 12, 13, 14, 15, 17, 18, 19, 20, 21, 23, 24};
                            key = 0;
                            for (size_t i = 0; i < sizeof(numbers); ++i)
                                if (parameters[0] == numbers[i]) {
                                    key = TUI_KEY_F1 + (uint32_t)i;
                                    break;
                                }
                            if (parameters[0] == 2)
                                key = TUI_KEY_INSERT;
                            else if (parameters[0] == 1 || parameters[0] == 7)
                                key = TUI_KEY_HOME;
                            else if (parameters[0] == 4 || parameters[0] == 8)
                                key = TUI_KEY_END;
                            else if (parameters[0] == 3)
                                key = TUI_KEY_DELETE;
                            else if (parameters[0] == 5)
                                key = TUI_KEY_PAGEUP;
                            else if (parameters[0] == 6)
                                key = TUI_KEY_PAGEDOWN;
                            break;
                        }
                        default: key = 0; break;
                    }
                    if (!valid)
                        key = 0;
                }
            } else if (state->input_length >= 2 && state->input[1] != 27) {
                /* POSIX terminals encode Alt+keys as an Escape prefix.
                 * Keep a split UTF-8 scalar pending just as ordinary input;
                 * a lone Escape still uses the existing 30 ms timeout. */
                size_t scalar_bytes = 1;
                key = state->input[1];
                if (key >= 0x80) {
                    key = tui_decode((const char *)state->input + 1, state->input_length - 1, &scalar_bytes);
                    if (!scalar_bytes) break;
                    consumed = scalar_bytes + 1;
                    modifiers |= XXWIDGETS_MOD_ALT;
                } else {
                    consumed = 2;
                    modifiers |= XXWIDGETS_MOD_ALT;
                    if (key == '\t') key = TUI_KEY_TAB;
                    else if (key == '\r' || key == '\n') key = TUI_KEY_ENTER;
                    else if (key == 8 || key == 127) key = TUI_KEY_BACKSPACE;
                }
            } else key = TUI_KEY_ESCAPE;
            state->escape_since = 0;
        } else if (key == '\t') key = TUI_KEY_TAB;
        else if (key == '\r' || key == '\n') key = TUI_KEY_ENTER;
        else if (key == 8 || key == 127) key = TUI_KEY_BACKSPACE;
        else if (key >= 0x80) {
            key = tui_decode((const char *)state->input, state->input_length, &consumed);
            if (!consumed) break;
        }
        memmove(state->input, state->input + consumed, state->input_length - consumed);
        state->input_length -= consumed;
        status = tui_key(app, key, modifiers);
        if (status != XXWIDGETS_OK) return status;
    }
    return XXWIDGETS_OK;
}
#endif

static xxwidgets_status tui_poll(xxwidgets_app *app, int timeout_ms)
{
    tui_state *state = (tui_state *)app->platform;
    xxwidgets_status status = tui_render(app);
    if (status != XXWIDGETS_OK || app->quit) return status;
#ifdef _WIN32
    unsigned int processed = 0;
    if (!state->pending_repeats && state->record_index >= state->record_count) {
        DWORD result = WaitForSingleObject(state->input, (DWORD)timeout_ms);
        if (result == WAIT_TIMEOUT) return tui_render(app);
        if (result != WAIT_OBJECT_0) return XXWIDGETS_PLATFORM_ERROR;
        if (!ReadConsoleInputW(state->input, state->records, 64, &state->record_count)) return XXWIDGETS_PLATFORM_ERROR;
        state->record_index = 0;
    }
    /* Retain both the batch and remaining repeats between polls. A repeated
     * key may exceed this work budget without silently losing user input. */
    while (processed < 128 && !app->quit) {
        INPUT_RECORD *record;
        KEY_EVENT_RECORD *event;
        uint32_t key = 0;
        if (state->pending_repeats) {
            --state->pending_repeats;
            ++processed;
            status = tui_key(app, state->pending_key, state->pending_modifiers);
            if (status != XXWIDGETS_OK) return status;
            continue;
        }
        if (state->record_index >= state->record_count) break;
        record = &state->records[state->record_index++];
        if (record->EventType == WINDOW_BUFFER_SIZE_EVENT) {
            state->dirty = 1;
            continue;
        }
        if (record->EventType != KEY_EVENT || !record->Event.KeyEvent.bKeyDown) continue;
        event = &record->Event.KeyEvent;
        state->pending_modifiers = 0;
        if (event->dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) state->pending_modifiers |= XXWIDGETS_MOD_CTRL;
        if (event->dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) state->pending_modifiers |= XXWIDGETS_MOD_ALT;
        if (event->dwControlKeyState & SHIFT_PRESSED) state->pending_modifiers |= XXWIDGETS_MOD_SHIFT;
        switch (event->wVirtualKeyCode) {
            case VK_TAB: key = event->dwControlKeyState & SHIFT_PRESSED ? TUI_KEY_BACKTAB : TUI_KEY_TAB; break;
            case VK_RETURN: key = TUI_KEY_ENTER; break;
            case VK_ESCAPE: key = TUI_KEY_ESCAPE; break;
            case VK_BACK: key = TUI_KEY_BACKSPACE; break;
            case VK_DELETE: key = TUI_KEY_DELETE; break;
            case VK_INSERT: key = TUI_KEY_INSERT; break;
            case VK_LEFT: key = TUI_KEY_LEFT; break;
            case VK_RIGHT: key = TUI_KEY_RIGHT; break;
            case VK_UP: key = TUI_KEY_UP; break;
            case VK_DOWN: key = TUI_KEY_DOWN; break;
            case VK_HOME: key = TUI_KEY_HOME; break;
            case VK_END: key = TUI_KEY_END; break;
            case VK_PRIOR: key = TUI_KEY_PAGEUP; break;
            case VK_NEXT: key = TUI_KEY_PAGEDOWN; break;
            case VK_APPS: key = TUI_KEY_CONTEXT; break;
            case VK_F10: key = event->dwControlKeyState & SHIFT_PRESSED ? TUI_KEY_CONTEXT : TUI_KEY_F1 + 9; break;
            default:
                key = event->wVirtualKeyCode >= VK_F1 && event->wVirtualKeyCode <= VK_F24 ? TUI_KEY_F1 + event->wVirtualKeyCode - VK_F1 : event->uChar.UnicodeChar;
                break;
        }
        if (key >= 0xd800 && key <= 0xdbff) {
            state->pending_surrogate = (WCHAR)key;
            continue;
        }
        if (key >= 0xdc00 && key <= 0xdfff) {
            if (!state->pending_surrogate) continue;
            key = 0x10000 + (((uint32_t)state->pending_surrogate - 0xd800) << 10) + key - 0xdc00;
        }
        state->pending_surrogate = 0;
        if (!key) continue;
        state->pending_key = key;
        state->pending_repeats = event->wRepeatCount;
    }
#else
    struct pollfd input;
    uint64_t deadline = tui_now() + (uint64_t)timeout_ms;
    uint64_t wait_deadline = deadline, now;
    int result, wait = timeout_ms;
    status = tui_parse(app);
    if (status != XXWIDGETS_OK || app->quit) return status;
    if (state->escape_since) {
        uint64_t escape_deadline = state->escape_since + 30;
        if (wait_deadline > escape_deadline) wait_deadline = escape_deadline;
    }
    now = tui_now();
    wait = now < wait_deadline ? (int)(wait_deadline - now) : 0;
    input.fd = STDIN_FILENO;
    input.events = POLLIN;
    input.revents = 0;
    do {
        result = poll(&input, 1, wait);
        if (result < 0 && errno == EINTR) {
            now = tui_now();
            if (now >= wait_deadline) {
                result = 0;
                break;
            }
            wait = (int)(wait_deadline - now);
        } else break;
    } while (1);
    if (result < 0 || (input.revents & (POLLERR | POLLNVAL | POLLHUP))) return XXWIDGETS_PLATFORM_ERROR;
    if (result > 0 && (input.revents & POLLIN)) {
        ssize_t received = read(STDIN_FILENO, state->input + state->input_length, sizeof(state->input) - state->input_length);
        if (received < 0 && errno != EINTR && errno != EAGAIN) return XXWIDGETS_PLATFORM_ERROR;
        if (received > 0) state->input_length += (size_t)received;
    }
    status = tui_parse(app);
    if (status != XXWIDGETS_OK) return status;
#endif
    return tui_render(app);
}

static xxwidgets_status tui_create(xxwidgets_widget *widget)
{
    if (widget->kind == XXWIDGETS_EDIT) {
        tui_edit *edit = (tui_edit *)calloc(1, sizeof(*edit));
        if (!edit) return XXWIDGETS_OUT_OF_MEMORY;
        edit->caret = strlen(widget->text);
        widget->platform = edit;
    } else if (xxwidgets_list_kind(widget)) {
        tui_hexview *hexview = (tui_hexview *)calloc(1, sizeof(*hexview));
        if (!hexview) return XXWIDGETS_OUT_OF_MEMORY;
        hexview->revision = xxwidgets_row_revision(widget);
        if (widget->kind == XXWIDGETS_HEXVIEW) {
            hexview->hex_data = widget->hex_data;
            hexview->hex_size = widget->hex_size;
            hexview->hex_base = widget->hex_base;
            hexview->hex_columns = widget->hex_columns;
            hexview->hex_cursor = widget->hex_cursor;
            hexview->hex_nibble = widget->hex_nibble;
            hexview->hex_editable = widget->hex_editable;
        }
        widget->platform = hexview;
    }
    ((tui_state *)widget->app->platform)->dirty = 1;
    return XXWIDGETS_OK;
}

static void tui_destroy(xxwidgets_widget *widget)
{
    tui_state *state = (tui_state *)widget->app->platform;
    if (state->focus == widget) state->focus = NULL;
    if (state->dropdown == widget) state->dropdown = NULL;
    if (widget->kind == XXWIDGETS_EDIT && widget->platform) free(((tui_edit *)widget->platform)->seen);
    free(widget->platform);
    widget->platform = NULL;
    state->dirty = 1;
}

static xxwidgets_status tui_sync(xxwidgets_widget *widget)
{
    tui_state *state = (tui_state *)widget->app->platform;
    if (widget->kind == XXWIDGETS_EDIT && widget->platform) {
        tui_edit *edit = (tui_edit *)widget->platform;
        if (!edit->seen || strcmp(edit->seen, widget->text)) {
            /* Text set by the program (e.g. a path completed): caret to the
             * end, as the native backends do. In the focused field it keeps
             * its place around a small change, as when a '~' is expanded. */
            size_t caret = SIZE_MAX;
            if (edit->seen && widget == state->focus) caret = xxwidgets_edit_caret(edit->seen, edit->caret, widget->text);
            if (caret == SIZE_MAX) caret = strlen(widget->text);
            edit->caret = caret;
            free(edit->seen);
            edit->seen = xxwidgets_strdup(widget->text);
        }
        if (edit->caret > strlen(widget->text)) edit->caret = strlen(widget->text);
        while (edit->caret && (((unsigned char)widget->text[edit->caret] & 0xc0) == 0x80)) --edit->caret;
    } else if (xxwidgets_formatted_rows(widget) && widget->platform) {
        tui_hexview *hexview = (tui_hexview *)widget->platform;
        if (hexview->revision != xxwidgets_row_revision(widget)) {
            int reset_horizontal = widget->kind != XXWIDGETS_HEXVIEW;
            int reveal_cursor = 0;
            if (widget->kind == XXWIDGETS_HEXVIEW) {
                reset_horizontal = hexview->hex_data != widget->hex_data || hexview->hex_size != widget->hex_size ||
                    hexview->hex_base != widget->hex_base || hexview->hex_columns != widget->hex_columns;
                reveal_cursor = reset_horizontal || hexview->hex_cursor != widget->hex_cursor || hexview->hex_nibble != widget->hex_nibble ||
                    hexview->hex_editable != widget->hex_editable;
                hexview->hex_data = widget->hex_data;
                hexview->hex_size = widget->hex_size;
                hexview->hex_base = widget->hex_base;
                hexview->hex_columns = widget->hex_columns;
                hexview->hex_cursor = widget->hex_cursor;
                hexview->hex_nibble = widget->hex_nibble;
                hexview->hex_editable = widget->hex_editable;
            }
            if (reset_horizontal) hexview->horizontal = 0;
            hexview->maximum = hexview->measured = 0;
            hexview->revision = xxwidgets_row_revision(widget);
            if (reveal_cursor) tui_hexview_reveal_cursor(state, widget);
        }
    } else if (widget->kind == XXWIDGETS_LISTBOX && widget->platform) {
        /* A list box (the operation log) has no row revision: its line count
         * stands in. A clear, or new lines while the newest one was selected
         * or once the newest one is selected (lines are added before they
         * are), bring the view back to the left edge, so the new output is
         * not cut off. */
        tui_hexview *hexview = (tui_hexview *)widget->platform;
        if (hexview->revision != widget->item_count) {
            if (widget->item_count < hexview->revision) {
                hexview->horizontal = 0;
                hexview->maximum = hexview->measured = 0;
                hexview->grown = 0;
            } else if (widget->value >= 0 && (uint64_t)widget->value + 1 == hexview->revision) {
                hexview->horizontal = 0;
                hexview->grown = 0;
            } else {
                hexview->grown = widget->value >= 0;
            }
            hexview->revision = widget->item_count;
        } else {
            /* The sync after new lines: the program selected the newest. A
             * list filled without a selection (details) never gets here set. */
            if (hexview->grown && widget->value >= 0 && (size_t)widget->value + 1 == widget->item_count) hexview->horizontal = 0;
            hexview->grown = 0;
        }
    }
    state->dirty = 1;
    return tui_render(widget->app);
}

static xxwidgets_status tui_read(xxwidgets_widget *widget)
{
    (void)widget;
    return XXWIDGETS_OK;
}

static xxwidgets_status tui_focus(xxwidgets_widget *widget)
{
    tui_state *state = (tui_state *)widget->app->platform;
    if (!tui_eligible(widget)) return XXWIDGETS_INVALID_ARGUMENT;
    if (state->dropdown != widget) state->dropdown = NULL;
    state->focus = widget;
    state->dirty = 1;
    return tui_render(widget->app);
}

static xxwidgets_status tui_apply_fonts(xxwidgets_app *app, const xxwidgets_font_options *options)
{
    /* The common layer stores the accepted settings. A terminal's font belongs
     * to its host, so font options do not alter cells or escape sequences. */
    (void)app;
    (void)options;
    return XXWIDGETS_OK;
}

static int tui_has_focus(const xxwidgets_widget *widget)
{
    const tui_state *state = (const tui_state *)widget->app->platform;
    return state && state->focus == widget;
}

static xxwidgets_status tui_copy_text(xxwidgets_widget *window, const char *text)
{
#ifdef _WIN32
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    HGLOBAL memory = length ? GlobalAlloc(GMEM_MOVEABLE, (size_t)length * sizeof(wchar_t)) : NULL;
    (void)window;
    if (!memory)
        return XXWIDGETS_OUT_OF_MEMORY;
    wchar_t *wide = GlobalLock(memory);
    if (!wide) {
        GlobalFree(memory);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    int converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, length);
    GlobalUnlock(memory);
    if (!converted || !OpenClipboard(GetConsoleWindow())) {
        GlobalFree(memory);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    int ok = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory) != NULL;
    CloseClipboard();
    if (!ok)
        GlobalFree(memory);
    return ok ? XXWIDGETS_OK : XXWIDGETS_PLATFORM_ERROR;
#else
    static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t length = strlen(text), at = 7;
    (void)window;
    if (length > (SIZE_MAX - 16) / 4 * 3)
        return XXWIDGETS_OUT_OF_MEMORY;
    char *output = malloc(4 * ((length + 2) / 3) + 9);
    if (!output)
        return XXWIDGETS_OUT_OF_MEMORY;
    memcpy(output, "\033]52;c;", 7);
    for (size_t i = 0; i < length; i += 3) {
        unsigned value = (unsigned char)text[i] << 16;
        if (i + 1 < length)
            value |= (unsigned char)text[i + 1] << 8;
        if (i + 2 < length)
            value |= (unsigned char)text[i + 2];
        output[at++] = digits[value >> 18];
        output[at++] = digits[(value >> 12) & 63];
        output[at++] = i + 1 < length ? digits[(value >> 6) & 63] : '=';
        output[at++] = i + 2 < length ? digits[value & 63] : '=';
    }
    output[at++] = '\a';
    int ok = tui_write(output, at);
    free(output);
    return ok ? XXWIDGETS_OK : XXWIDGETS_PLATFORM_ERROR;
#endif
}
static size_t tui_hexview_page_rows(const xxwidgets_widget *widget)
{
    const tui_state *state = widget->app->platform;
    const xxwidgets_widget *window = widget->parent;
    if (!state || !window)
        return (size_t)(widget->rect.height > 1 ? widget->rect.height - 1 : 1);
    long long x = (long long)window->rect.x + 1, y = tui_window_top(state, window) + 1;
    tui_clip client = {x, y, x + window->rect.width, y + window->rect.height};
    tui_clip bounds = {x + widget->rect.x, y + widget->rect.y,
                       x + widget->rect.x + widget->rect.width, y + widget->rect.y + widget->rect.height};
    tui_clip screen = {0, 0, state->columns, state->rows};
    tui_clip clip = tui_intersect(tui_intersect(bounds, client), screen);
    long long top = clip.top > bounds.top + 1 ? clip.top : bounds.top + 1;
    return clip.left < clip.right && clip.bottom > top ? (size_t)(clip.bottom - top) : 1;
}

const xxwidgets_backend_ops xxwidgets_tui_ops = {"tui",    tui_init, tui_shutdown, tui_poll, tui_create, tui_destroy,   tui_sync,
                                                 tui_read, tui_read, tui_focus,    NULL,     NULL,       tui_copy_text, tui_apply_fonts,
                                                 NULL,     NULL,     NULL,         NULL,     NULL,       tui_has_focus, NULL, tui_hexview_page_rows, NULL};
