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
#endif

/* Text occupies one cell per Unicode scalar. Full-width and combining glyphs
 * therefore need a terminal-aware width/shaping layer for precise alignment.
 * Edits are single-line; terminal mouse input is deliberately not required. */
typedef struct tui_cell {
    uint32_t glyph;
    unsigned char style;
} tui_cell;

typedef struct tui_edit {
    size_t caret;
    char *seen; /* Text as last typed or shown; a different text came from the program. */
} tui_edit;

typedef struct tui_hexview {
    size_t horizontal;
    uint64_t revision;
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
    TUI_TITLE
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
    size_t length, remaining = strlen(text + offset);
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

static void tui_put(tui_state *state, tui_clip clip, long long x, long long y, uint32_t glyph, unsigned char style)
{
    if (x < clip.left || x >= clip.right || y < clip.top || y >= clip.bottom || x < 0 || y < 0 || x >= state->columns || y >= state->rows) return;
    state->cells[(size_t)y * (size_t)state->columns + (size_t)x].glyph = glyph;
    state->cells[(size_t)y * (size_t)state->columns + (size_t)x].style = style;
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

static void tui_text(tui_state *state, tui_clip clip, long long x, long long y, const char *text, unsigned char style, int flags)
{
    int multiline = flags & TUI_TEXT_MULTILINE;
    size_t length, remaining = strlen(text);
    long long start = x;
    while (remaining && y < clip.bottom) {
        uint32_t glyph = tui_decode(text, remaining, &length);
        if (!length) {
            length = 1;
            glyph = 0xfffd;
        }
        text += length;
        remaining -= length;
        if (glyph == '\n') {
            if (!multiline) break;
            ++y;
            x = start;
            continue;
        }
        if (glyph == '\r') continue;
        if (glyph < 32 || glyph == 127) glyph = ' ';
        if ((flags & TUI_TEXT_ELLIPSIS) && x == clip.right - 1 && x > start && remaining && *text != '\n' && *text != '\r') {
            /* The line goes on past the edge: say so instead of cutting it. */
            tui_put(state, clip, x, y, 0x2026, style);
            if (!multiline) break;
            while (remaining && *text != '\n') {
                ++text;
                --remaining;
            }
            continue;
        }
        tui_put(state, clip, x++, y, glyph, style);
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
    long long x = (long long)widget->parent->rect.x + 1 + widget->rect.x;
    int width = widget->rect.width;
    size_t length =
        (widget->kind == XXWIDGETS_ARCHIVEVIEW || widget->kind == XXWIDGETS_ARCHIVEBROWSER || widget->kind == XXWIDGETS_SCANRESULTS || widget->kind == XXWIDGETS_TREEVIEW)
            ? widget->archive_columns
        : widget->item_count ? strlen(widget->items[0])
                             : 0;
    size_t visible;
    /* Formatted rows vary in width; horizontal offsets count Unicode scalars. */
    if (width > widget->parent->rect.width - widget->rect.x) width = widget->parent->rect.width - widget->rect.x;
    if ((long long)width > state->columns - x) width = (int)(state->columns - x);
    visible = width > 1 ? (size_t)width - 1 : 1;
    return length > visible ? length - visible : 0;
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
    tui_fill(state, clip, style);
    switch (widget->kind) {
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
            size_t start = 0, pos = 0, caret = edit ? edit->caret : strlen(widget->text);
            size_t count = 0, visible = widget->rect.width > 2 ? (size_t)widget->rect.width - 2 : 0;
            tui_put(state, clip, x, y, '[', style);
            tui_put(state, clip, bounds.right - 1, y, ']', style);
            while (pos < caret && widget->text[pos]) {
                pos = tui_next(widget->text, pos);
                ++count;
            }
            while (visible && count >= visible && start < caret) {
                start = tui_next(widget->text, start);
                --count;
            }
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
        case XXWIDGETS_TREEVIEW:
        case XXWIDGETS_HEXVIEW: {
            size_t first = 0;
            size_t selected_row = widget->kind == XXWIDGETS_TREEVIEW ? xxwidgets_treeview_row_of_node(widget, (size_t)widget->value)
                                  : widget->value < 0                ? SIZE_MAX
                                                                     : (size_t)widget->value;
            int header_rows = widget->kind == XXWIDGETS_ARCHIVEBROWSER ? 2 : widget->kind == XXWIDGETS_SCANRESULTS ? 1 : 0;
            int row_height = widget->rect.height - header_rows;
            tui_hexview *hexview = xxwidgets_formatted_rows(widget) ? (tui_hexview *)widget->platform : NULL;
            if (hexview) {
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
            } else if (widget->kind == XXWIDGETS_SCANRESULTS) {
                static const char *titles[] = {"Type", "Name", "Version", "Info"};
                long long header_x = x + 1 - (long long)(hexview ? hexview->horizontal : 0);
                size_t column;
                tui_clip header_clip = clip;
                header_clip.top = y;
                header_clip.bottom = y + 1;
                header_clip = tui_intersect(clip, header_clip);
                tui_fill(state, header_clip, TUI_TITLE);
                for (column = 0; column < 4; ++column) {
                    tui_text(state, header_clip, header_x, y, titles[column], TUI_TITLE, 0);
                    header_x += (long long)xxwidgets_scanresults_column_width(widget, column) + 2;
                }
            }
            if (row_height <= 0) break;
            if (selected_row != SIZE_MAX && selected_row >= (size_t)row_height) first = selected_row - (size_t)row_height + 1;
            for (row = clip.top; row < clip.bottom; ++row) {
                size_t item;
                size_t offset = 0, remaining = hexview ? hexview->horizontal : 0;
                unsigned char item_style = style;
                if (row < y + header_rows) continue;
                item = first + (size_t)(row - y - header_rows);
                if (item >= widget->item_count) break;
                while (remaining && widget->items[item][offset]) {
                    offset = tui_next(widget->items[item], offset);
                    --remaining;
                }
                if ((item == selected_row || (widget->kind == XXWIDGETS_ARCHIVEBROWSER && xxwidgets_archivebrowser_row_selected(widget, item))) && widget->enabled &&
                    widget->parent->enabled)
                    item_style = TUI_FOCUS;
                bounds.top = row;
                bounds.bottom = row + 1;
                tui_fill(state, tui_intersect(clip, bounds), item_style);
                tui_put(state, clip, x, row,
                        item == selected_row                                                                              ? '>'
                        : widget->kind == XXWIDGETS_ARCHIVEBROWSER && xxwidgets_archivebrowser_row_selected(widget, item) ? '*'
                                                                                                                          : ' ',
                        item_style);
                tui_text(state, clip, x + 1, row, widget->items[item] + offset, item_style, TUI_TEXT_ELLIPSIS);
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
                                  FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY | BACKGROUND_BLUE | BACKGROUND_GREEN};
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
    static const char *styles[] = {"\033[0m", "\033[0;90m", "\033[0;97;44m", "\033[0;97;46m"};
    size_t capacity = (size_t)state->columns * (size_t)state->rows * 20 + (size_t)state->rows * 32 + 128, length = 0;
    char *output = (char *)malloc(capacity);
    int x, y, previous_style = -1;
    if (!output) return XXWIDGETS_OUT_OF_MEMORY;
    length += (size_t)snprintf(output + length, capacity - length, "\033[?25l");
    for (y = 0; y < state->rows; ++y) {
        length += (size_t)snprintf(output + length, capacity - length, "\033[%d;1H", y + 1);
        for (x = 0; x < state->columns; ++x) {
            const tui_cell *cell = state->cells + (size_t)y * (size_t)state->columns + (size_t)x;
            if (cell->style != previous_style) {
                size_t size = strlen(styles[cell->style]);
                memcpy(output + length, styles[cell->style], size);
                length += size;
                previous_style = cell->style;
            }
            length += tui_encode(cell->glyph, output + length);
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
    if (key == TUI_KEY_ENTER && app->modal_default) {
        xxwidgets_emit(app->modal_default, XXWIDGETS_EVENT_CLICK, 0);
        return XXWIDGETS_OK;
    }
    if (!widget || !tui_eligible(widget)) return XXWIDGETS_OK;
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
        int page_rows = widget->rect.height - (widget->kind == XXWIDGETS_ARCHIVEBROWSER ? 2 : widget->kind == XXWIDGETS_SCANRESULTS ? 1 : 0);
        if (page_rows < 1) page_rows = 1;
        if (widget->kind == XXWIDGETS_ARCHIVEBROWSER && key == 1) {
            xxwidgets_status status = xxwidgets_archivebrowser_select_all(widget);
            if (status == XXWIDGETS_OK) xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, widget->value);
            return status;
        }
        if (widget->kind == XXWIDGETS_ARCHIVEBROWSER && key == TUI_KEY_CONTEXT) {
            int first = selection >= page_rows ? selection - page_rows + 1 : 0;
            int anchor_y = selection >= 0 ? selection - first + 2 : 2;
            if (anchor_y >= widget->rect.height) anchor_y = widget->rect.height - 1;
            xxwidgets_emit_context(widget, selection, widget->rect.width > 1 ? 1 : 0, anchor_y);
            return XXWIDGETS_OK;
        }
        if (widget->kind == XXWIDGETS_ARCHIVEBROWSER && key == TUI_KEY_BACKSPACE) return xxwidgets_archivebrowser_user_up(widget);
        if (widget->kind == XXWIDGETS_ARCHIVEBROWSER && key >= '1' && key <= '5')
            return xxwidgets_archivebrowser_user_sort(widget, (xxwidgets_archive_column)(key - '1'));
        if (xxwidgets_formatted_rows(widget) && (key == TUI_KEY_LEFT || key == TUI_KEY_RIGHT)) {
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
            if (selection < 0) selection = 0;
            widget->value = selection;
            state->dirty = 1;
            if (widget->kind == XXWIDGETS_ARCHIVEBROWSER && key == TUI_KEY_ENTER) return xxwidgets_archivebrowser_user_activate(widget, selection);
            if (widget->kind == XXWIDGETS_ARCHIVEBROWSER && key == ' ')
                xxwidgets_archivebrowser_selection_input(widget, (size_t)selection, !xxwidgets_archivebrowser_row_selected(widget, (size_t)selection));
            xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, selection);
            return XXWIDGETS_OK;
        } else return XXWIDGETS_OK;
        if (selection != widget->value) {
            widget->value = selection;
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
                        case '~':
                            if (end == 6 && !memcmp(state->input + 2, "21;2", 4)) key = TUI_KEY_CONTEXT; /* xterm Shift+F10. */
                            else if (end == 4) {
                                static const unsigned char numbers[] = {11, 12, 13, 14, 15, 17, 18, 19, 20, 21, 23, 24};
                                int number = (state->input[2] - '0') * 10 + state->input[3] - '0';
                                key = 0;
                                for (size_t i = 0; i < sizeof(numbers); ++i)
                                    if (number == numbers[i]) {
                                        key = TUI_KEY_F1 + (uint32_t)i;
                                        break;
                                    }
                            } else if (end != 3) key = 0;
                            else if (state->input[2] == '2') key = TUI_KEY_INSERT;
                            else if (state->input[2] == '1' || state->input[2] == '7') key = TUI_KEY_HOME;
                            else if (state->input[2] == '4' || state->input[2] == '8') key = TUI_KEY_END;
                            else if (state->input[2] == '3') key = TUI_KEY_DELETE;
                            else if (state->input[2] == '5') key = TUI_KEY_PAGEUP;
                            else if (state->input[2] == '6') key = TUI_KEY_PAGEDOWN;
                            else key = 0;
                            break;
                        default: key = 0; break;
                    }
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
        status = tui_key(app, key, 0);
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
    } else if (xxwidgets_formatted_rows(widget)) {
        tui_hexview *hexview = (tui_hexview *)calloc(1, sizeof(*hexview));
        if (!hexview) return XXWIDGETS_OUT_OF_MEMORY;
        hexview->revision = xxwidgets_row_revision(widget);
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
            hexview->horizontal = 0;
            hexview->revision = xxwidgets_row_revision(widget);
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

const xxwidgets_backend_ops xxwidgets_tui_ops = {"tui", tui_init, tui_shutdown, tui_poll,        tui_create, tui_destroy, tui_sync, tui_read, tui_read, tui_focus,
                                                 NULL,  NULL,     NULL,         tui_apply_fonts, NULL,       NULL,        NULL,     NULL,     NULL,     tui_has_focus};
