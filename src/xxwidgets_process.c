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

#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_process.h"
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#ifdef XXWIDGETS_TEST_CLOCK
/* Deterministic delay tests supply a monotonic clock without sleeping. */
extern xxwidgets_status xxwidgets_process_clock(uint64_t *milliseconds);
#else
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
static xxwidgets_status xxwidgets_process_clock(uint64_t *milliseconds)
{
#ifdef _WIN32
    *milliseconds = (uint64_t)GetTickCount64();
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return XXWIDGETS_PLATFORM_ERROR;
    *milliseconds = (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
#endif
    return XXWIDGETS_OK;
}
#endif

typedef struct process_state {
    xxwidgets_widget *owner, *window, *cancel;
    xxwidgets_widget *bars[XX_PD_LEVELS], *labels[XX_PD_LEVELS], *elapsed;
    int stop_requested, minimum_rows;
    char elapsed_text[160]; /* Shown text (same size as the render buffer); not set again unchanged. */
    /* A level is shown once busy for PROCESS_SETTLE_MS and hidden once idle as
     * long, so one that flashes by for a pass does not resize the dialog. */
    unsigned char shown[XX_PD_LEVELS];
    uint64_t changed_at[XX_PD_LEVELS]; /* Elapsed ms when busy last differed from shown; 0: it did not. */
    int rendered;                      /* The first pass shows what is busy at once. */
} process_state;

#define PROCESS_SETTLE_MS 200U

static void process_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    process_state *state = (process_state *)user;
    (void)app;
    if ((event->type == XXWIDGETS_EVENT_CLOSE && (event->widget == state->window || event->widget == state->owner)) ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == state->cancel))
        state->stop_requested = 1;
}

static xxwidgets_status process_update(xxwidgets_app *app, process_state *state, xx_pd_struct *progress, xxwidgets_process_update_fn update, void *user, int *finished)
{
    xxwidgets_status status;
    state->stop_requested |= progress->is_stop != 0;
    progress->is_stop = state->stop_requested != 0;
    *finished = 0;
    ++app->dispatch_depth;
    status = update(user, state->stop_requested, progress, finished);
    --app->dispatch_depth;
    state->stop_requested |= progress->is_stop != 0;
    progress->is_stop = state->stop_requested != 0;
    return status;
}

static int process_percent(uint64_t current, uint64_t total)
{
    int low = 0, high = 100;
    if (!total) return 0;
    if (current >= total) return 100;
    /* Compare against ceil(total * percent / 100), without a 64-bit product
     * overflow or rounding 99.999... up to 100 on platforms with 64-bit double. */
    while (high - low > 1) {
        int middle = (low + high) / 2;
        uint64_t threshold = (total / 100) * (unsigned int)middle + ((total % 100) * (unsigned int)middle + 99) / 100;
        if (current >= threshold) low = middle;
        else high = middle;
    }
    return low;
}

static void process_status(char *text, const char *status, size_t capacity)
{
    size_t i;
    /* xxfclib copies status into a fixed byte buffer. Bound the read even if
     * a producer omitted NUL, and tolerate a UTF-8 sequence cut at its end. */
    memcpy(text, status, capacity);
    text[capacity] = '\0';
    while (text[0] && !xxwidgets_valid_utf8(text)) text[strlen(text) - 1] = '\0';
    for (i = 0; text[i]; ++i)
        if ((unsigned char)text[i] < 32 || text[i] == 127) text[i] = ' ';
}

/* Cells a glyph is taken to need, by its UTF-8 lead byte: two from U+1000 on,
 * where the East Asian wide ones begin. Some symbols are overcounted, which
 * only cuts a little early; each backend measures what it draws. */
static size_t process_cells(unsigned char lead)
{
    return lead >= 0xe1 ? 2 : 1;
}

/* The UTF-8 scalar at text into *glyph; its length, or 0 at the end or at
 * an invalid sequence. */
static size_t process_decode(const char *text, uint32_t *glyph)
{
    const unsigned char *p = (const unsigned char *)text;
    size_t length, i;
    if (p[0] < 0x80) {
        *glyph = p[0];
        return p[0] ? 1 : 0;
    }
    if (p[0] >= 0xf5 || p[0] < 0xc2) return 0;
    length = p[0] >= 0xf0 ? 4 : p[0] >= 0xe0 ? 3 : 2;
    *glyph = p[0] & (0x7f >> length);
    for (i = 1; i < length; ++i) {
        if ((p[i] & 0xc0) != 0x80) return 0;
        *glyph = (*glyph << 6) | (p[i] & 0x3f);
    }
    return length;
}

/* Bytes of a scalar at text that never starts a character of its own (it
 * extends the one before it); 0 for anything else. */
static size_t process_extender(const char *text)
{
    /* Grapheme_Cluster_Break Extend, SpacingMark, ZWJ, V and T (Unicode
     * 17.0), generated; outside the BMP only emoji modifiers, tags and
     * variation selectors. */
    static const uint32_t extenders[][2] = {
        {0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD}, {0x05BF, 0x05BF}, {0x05C1, 0x05C2},   {0x05C4, 0x05C5},  {0x05C7, 0x05C7}, {0x0610, 0x061A},
        {0x064B, 0x065F}, {0x0670, 0x0670}, {0x06D6, 0x06DC}, {0x06DF, 0x06E4}, {0x06E7, 0x06E8},   {0x06EA, 0x06ED},  {0x0711, 0x0711}, {0x0730, 0x074A},
        {0x07A6, 0x07B0}, {0x07EB, 0x07F3}, {0x07FD, 0x07FD}, {0x0816, 0x0819}, {0x081B, 0x0823},   {0x0825, 0x0827},  {0x0829, 0x082D}, {0x0859, 0x085B},
        {0x0897, 0x089F}, {0x08CA, 0x08E1}, {0x08E3, 0x0903}, {0x093A, 0x093C}, {0x093E, 0x094F},   {0x0951, 0x0957},  {0x0962, 0x0963}, {0x0981, 0x0983},
        {0x09BC, 0x09BC}, {0x09BE, 0x09CD}, {0x09D7, 0x09D7}, {0x09E2, 0x09E3}, {0x09FE, 0x0A03},   {0x0A3C, 0x0A51},  {0x0A70, 0x0A71}, {0x0A75, 0x0A75},
        {0x0A81, 0x0A83}, {0x0ABC, 0x0ABC}, {0x0ABE, 0x0ACD}, {0x0AE2, 0x0AE3}, {0x0AFA, 0x0B03},   {0x0B3C, 0x0B3C},  {0x0B3E, 0x0B57}, {0x0B62, 0x0B63},
        {0x0B82, 0x0B82}, {0x0BBE, 0x0BCD}, {0x0BD7, 0x0BD7}, {0x0C00, 0x0C04}, {0x0C3C, 0x0C3C},   {0x0C3E, 0x0C56},  {0x0C62, 0x0C63}, {0x0C81, 0x0C83},
        {0x0CBC, 0x0CBC}, {0x0CBE, 0x0CD6}, {0x0CE2, 0x0CE3}, {0x0CF3, 0x0D03}, {0x0D3B, 0x0D3C},   {0x0D3E, 0x0D4D},  {0x0D57, 0x0D57}, {0x0D62, 0x0D63},
        {0x0D81, 0x0D83}, {0x0DCA, 0x0DDF}, {0x0DF2, 0x0DF3}, {0x0E31, 0x0E31}, {0x0E33, 0x0E3A},   {0x0E47, 0x0E4E},  {0x0EB1, 0x0EB1}, {0x0EB3, 0x0EBC},
        {0x0EC8, 0x0ECE}, {0x0F18, 0x0F19}, {0x0F35, 0x0F35}, {0x0F37, 0x0F37}, {0x0F39, 0x0F39},   {0x0F3E, 0x0F3F},  {0x0F71, 0x0F84}, {0x0F86, 0x0F87},
        {0x0F8D, 0x0FBC}, {0x0FC6, 0x0FC6}, {0x102D, 0x1037}, {0x1039, 0x103E}, {0x1056, 0x1059},   {0x105E, 0x1060},  {0x1071, 0x1074}, {0x1082, 0x1082},
        {0x1084, 0x1086}, {0x108D, 0x108D}, {0x109D, 0x109D}, {0x1160, 0x11FF}, {0x135D, 0x135F},   {0x1712, 0x1715},  {0x1732, 0x1734}, {0x1752, 0x1753},
        {0x1772, 0x1773}, {0x17B4, 0x17D3}, {0x17DD, 0x17DD}, {0x180B, 0x180D}, {0x180F, 0x180F},   {0x1885, 0x1886},  {0x18A9, 0x18A9}, {0x1920, 0x193B},
        {0x1A17, 0x1A1B}, {0x1A55, 0x1A60}, {0x1A62, 0x1A62}, {0x1A65, 0x1A7F}, {0x1AB0, 0x1B04},   {0x1B34, 0x1B44},  {0x1B6B, 0x1B73}, {0x1B80, 0x1B82},
        {0x1BA1, 0x1BAD}, {0x1BE6, 0x1BF3}, {0x1C24, 0x1C37}, {0x1CD0, 0x1CD2}, {0x1CD4, 0x1CE8},   {0x1CED, 0x1CED},  {0x1CF4, 0x1CF4}, {0x1CF7, 0x1CF9},
        {0x1DC0, 0x1DFF}, {0x200C, 0x200D}, {0x20D0, 0x20F0}, {0x2CEF, 0x2CF1}, {0x2D7F, 0x2D7F},   {0x2DE0, 0x2DFF},  {0x302A, 0x302F}, {0x3099, 0x309A},
        {0xA66F, 0xA672}, {0xA674, 0xA67D}, {0xA69E, 0xA69F}, {0xA6F0, 0xA6F1}, {0xA802, 0xA802},   {0xA806, 0xA806},  {0xA80B, 0xA80B}, {0xA823, 0xA827},
        {0xA82C, 0xA82C}, {0xA880, 0xA881}, {0xA8B4, 0xA8C5}, {0xA8E0, 0xA8F1}, {0xA8FF, 0xA8FF},   {0xA926, 0xA92D},  {0xA947, 0xA953}, {0xA980, 0xA983},
        {0xA9B3, 0xA9C0}, {0xA9E5, 0xA9E5}, {0xAA29, 0xAA36}, {0xAA43, 0xAA43}, {0xAA4C, 0xAA4D},   {0xAA7C, 0xAA7C},  {0xAAB0, 0xAAB0}, {0xAAB2, 0xAAB4},
        {0xAAB7, 0xAAB8}, {0xAABE, 0xAABF}, {0xAAC1, 0xAAC1}, {0xAAEB, 0xAAEF}, {0xAAF5, 0xAAF6},   {0xABE3, 0xABEA},  {0xABEC, 0xABED}, {0xD7B0, 0xD7FB},
        {0xFB1E, 0xFB1E}, {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F}, {0xFF9E, 0xFF9F}, {0x1F3FB, 0x1F3FF}, {0xE0020, 0xE01EF}};
    size_t low = 0, high = sizeof(extenders) / sizeof(extenders[0]), length;
    uint32_t glyph = 0;
    length = process_decode(text, &glyph);
    if (length < 2) return 0;
    while (low < high) {
        size_t mid = (low + high) / 2;
        if (glyph < extenders[mid][0]) high = mid;
        else if (glyph > extenders[mid][1]) low = mid + 1;
        else return length;
    }
    return 0;
}

/* The byte a '\\xNN' escape at text stands for, or -1. */
static int process_escape(const char *text)
{
    static const char digits[] = "0123456789ABCDEF";
    const char *high, *low;
    if (text[0] != '\\' || text[1] != 'x' || !text[2] || !text[3]) return -1;
    high = strchr(digits, toupper((unsigned char)text[2]));
    low = strchr(digits, toupper((unsigned char)text[3]));
    if (!high || !low || !*high || !*low) return -1;
    return (int)((high - digits) * 16 + (low - digits));
}

/* Moves at, a cut in text, to where a whole character starts: past the
 * rest of a '\xNN' escape (a control byte in a name) it falls inside, and
 * past combining marks, which would sit on the ellipsis put before them. */
static size_t process_whole(const char *text, size_t at)
{
    size_t back, extender;
    for (back = 1; back <= 3 && back <= at; ++back)
        if (text[at - back] == '\\' && text[at - back + 1] == 'x' && isxdigit((unsigned char)text[at - back + 2]) && isxdigit((unsigned char)text[at - back + 3])) {
            at += 4 - back;
            break;
        }
    /* A character escaped byte by byte that the cut went through goes
     * whole. Such a run is a complete UTF-8 sequence escaped because it is
     * refused as text (a C1 control, an overlong form, a surrogate, beyond
     * U+10FFFF); anything else, such as a lead-like byte of a code-page name
     * followed by fewer continuation bytes, is bytes that each stand alone. */
    for (back = 0; at >= 4 * (back + 1) && process_escape(text + at - 4 * (back + 1)) >= 0x80 && process_escape(text + at - 4 * (back + 1)) <= 0xbf; ++back) {
    }
    if (at >= 4 * (back + 1)) {
        size_t lead_at = at - 4 * (back + 1), length, i;
        int lead = process_escape(text + lead_at), second;
        length = lead >= 0xc2 && lead <= 0xdf ? 2 : lead >= 0xe0 && lead <= 0xef ? 3 : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
        for (i = 1; i < length; ++i) {
            int next = process_escape(text + lead_at + 4 * i);
            if (next < 0x80 || next > 0xbf) break;
        }
        second = length > 1 ? process_escape(text + lead_at + 4) : -1;
        if (length && i == length && back + 1 < length &&
            ((lead == 0xc2 && second < 0xa0) || (lead == 0xe0 && second < 0xa0) || (lead == 0xed && second >= 0xa0) || (lead == 0xf0 && second < 0x90) ||
             (lead == 0xf4 && second >= 0x90)))
            at = lead_at + 4 * length;
    }
    while ((extender = process_extender(text + at)) != 0) at += extender;
    return at;
}

void xxwidgets_process_status_tail(char *status, size_t size, const char *text)
{
    size_t length, at;
    if (!status || !size) return;
    if (!text) text = "";
    length = strlen(text);
    if (length < size) {
        memcpy(status, text, length + 1);
        return;
    }
    if (size < 4) {
        status[0] = '\0';
        return;
    }
    at = length - (size - 4); /* room for the 3-byte ellipsis and the NUL */
    while (((unsigned char)text[at] & 0xc0) == 0x80) ++at;
    at = process_whole(text, at);
    memcpy(status, "\xe2\x80\xa6", 3);
    memcpy(status + 3, text + at, length - at + 1);
}

/* Shortens label at its start, behind an ellipsis, until it fits in width
 * cells after a prefix of prefix cells: the end of a path names the member.
 * label has room for 3 more bytes than it holds. */
static void process_fit(char *label, size_t prefix, int width)
{
    size_t cells = 0, at;
    for (at = 0; label[at]; ++at)
        if (((unsigned char)label[at] & 0xc0) != 0x80) cells += process_cells((unsigned char)label[at]);
    if (width <= 0 || prefix + cells <= (size_t)width) return;
    for (at = 0; label[at] && prefix + 1 + cells > (size_t)width;) {
        cells -= process_cells((unsigned char)label[at]);
        do ++at;
        while (((unsigned char)label[at] & 0xc0) == 0x80);
    }
    at = process_whole(label, at);
    memmove(label + 3, label + at, strlen(label + at) + 1);
    memcpy(label, "\xe2\x80\xa6", 3);
}

static xxwidgets_status process_render(process_state *state, const xx_pd_struct *progress, uint64_t elapsed)
{
    /* Laid out for the width the window has: a small screen can give it less
     * than the 64 columns it asks for, and it must not ask again each pass. */
    int columns = state->window->rect.width < 24 ? 24 : state->window->rect.width;
    xxwidgets_rect rect = {2, 1, 0, 1};
    xxwidgets_status status;
    char text[160], prefix[48], label[sizeof(progress->records[0].status) + 4];
    int i, row = 0, fit_width;
    rect.width = columns - 4;
#ifdef _WIN32
    /* A native font is proportional: capitals and digits are wider than the
     * average character a cell is measured from, and a Win32 static label's
     * path ellipsis keeps the end of backslash paths only. Leave room, so it
     * is rarely needed. GTK and AppKit cut these labels in the middle. */
    fit_width = state->window->app->backend == XXWIDGETS_BACKEND_TUI ? rect.width : rect.width * 3 / 4;
#else
    fit_width = rect.width;
#endif
    for (i = 0; i < XX_PD_LEVELS; ++i) {
        const xx_pd_record *record = &progress->records[i];
        int busy = record->is_busy != 0;
        if (busy == state->shown[i]) state->changed_at[i] = 0;
        else if (!state->rendered || (state->changed_at[i] && elapsed + 1 >= state->changed_at[i] + PROCESS_SETTLE_MS)) {
            state->shown[i] = (unsigned char)busy;
            state->changed_at[i] = 0;
        } else if (!state->changed_at[i]) state->changed_at[i] = elapsed + 1;
        if (state->shown[i]) {
            if (busy) {
                /* A level shown while it settles to idle keeps its last text. */
                int prefix_length;
                process_status(label, record->status, sizeof(record->status));
                if (record->total) prefix_length = snprintf(prefix, sizeof(prefix), "[%" PRIu64 "/%" PRIu64 "] ", record->current, record->total);
                else prefix_length = snprintf(prefix, sizeof(prefix), "[%" PRIu64 "/?] ", record->current);
                process_fit(label, prefix_length > 0 ? (size_t)prefix_length : 0, fit_width);
                snprintf(text, sizeof(text), "%s%s", prefix, label);
                status = xxwidgets_widget_set_text(state->labels[i], text);
                if (status != XXWIDGETS_OK) return status;
            }
            rect.y = 1 + row * 3;
            status = xxwidgets_widget_set_rect(state->labels[i], rect);
            if (status != XXWIDGETS_OK) return status;
            ++rect.y;
            status = xxwidgets_widget_set_rect(state->bars[i], rect);
            if (status != XXWIDGETS_OK) return status;
            if (busy) {
                status = xxwidgets_widget_set_value(state->bars[i], process_percent(record->current, record->total));
                if (status != XXWIDGETS_OK) return status;
            }
            ++row;
        }
        status = xxwidgets_widget_set_visible(state->bars[i], state->shown[i]);
        if (status != XXWIDGETS_OK) return status;
        status = xxwidgets_widget_set_visible(state->labels[i], state->shown[i]);
        if (status != XXWIDGETS_OK) return status;
    }
    state->rendered = 1;
    /* Tenths of a second: a millisecond counter changed on every pass of the
     * dialog loop, so the toolkit always had a redraw pending and the loop
     * never waited. */
    snprintf(text, sizeof(text), "Elapsed: %" PRIu64 ".%u s", elapsed / 1000, (unsigned int)(elapsed % 1000 / 100));
    if (strcmp(text, state->elapsed_text)) {
        status = xxwidgets_widget_set_text(state->elapsed, text);
        if (status != XXWIDGETS_OK) return status;
        snprintf(state->elapsed_text, sizeof(state->elapsed_text), "%s", text);
    }
    rect.y = row * 3 + 1;
    rect.width = columns - 22;
    status = xxwidgets_widget_set_rect(state->elapsed, rect);
    if (status != XXWIDGETS_OK) return status;
    rect.x = columns - 16;
    rect.width = 14;
    rect.height = 2;
    status = xxwidgets_widget_set_rect(state->cancel, rect);
    if (status != XXWIDGETS_OK) return status;
    if (state->stop_requested) {
        status = xxwidgets_widget_set_text(state->cancel, "Stopping...");
        if (status != XXWIDGETS_OK) return status;
        status = xxwidgets_widget_set_enabled(state->cancel, 0);
        if (status != XXWIDGETS_OK) return status;
    }
    /* The window is asked for its height only when that changes. Asking on
     * every pass fought the size a user, the window manager or a small screen
     * gave it, and each answer started the next pass at once. */
    rect = state->window->rect;
    rect.height = row * 3 + 5;
    if (rect.height == state->minimum_rows) return XXWIDGETS_OK;
    /* The 64 columns as a minimum, which the backend fits to the screen:
     * a width clamped on a small screen grows back when the screen does. */
    status = xxwidgets_window_set_minimum_size(state->window, 64, rect.height);
    if (status != XXWIDGETS_OK) return status;
    state->minimum_rows = rect.height;
    if (state->window->app->backend == XXWIDGETS_BACKEND_TUI) {
        /* Centred for the height it has, not the 20 rows it was created with. */
        const xxwidgets_rect *owner = &state->owner->rect;
        rect.y = owner->y + (owner->height > rect.height ? (owner->height - rect.height) / 2 : 0);
    }
    /* The frame keeps its place, also left of or above the primary monitor
     * (negative cells); only the bounds a window rect allows are kept. */
    if (rect.x < -32767) rect.x = -32767;
    if (rect.y < -32767) rect.y = -32767;
    if (rect.x > 32767) rect.x = 32767;
    if (rect.y > 32767) rect.y = 32767;
    return xxwidgets_widget_set_rect(state->window, rect);
}

static xxwidgets_status process_create(xxwidgets_app *app, process_state *state)
{
    xxwidgets_rect rect = {2, 1, 60, 1};
    xxwidgets_status status;
    int i;
    for (i = 0; i < XX_PD_LEVELS; ++i) {
        status = xxwidgets_widget_create(app, state->window, XXWIDGETS_LABEL, "", rect, &state->labels[i]);
        if (status != XXWIDGETS_OK) return status;
        state->labels[i]->keep_end = 1;
        status = xxwidgets_widget_create(app, state->window, XXWIDGETS_PROGRESS, "", rect, &state->bars[i]);
        if (status != XXWIDGETS_OK) return status;
    }
    status = xxwidgets_widget_create(app, state->window, XXWIDGETS_LABEL, "", rect, &state->elapsed);
    if (status != XXWIDGETS_OK) return status;
    rect.x = 48;
    rect.width = 14;
    rect.height = 2;
    status = xxwidgets_widget_create(app, state->window, XXWIDGETS_BUTTON, XXW_TR(app, "Cancel"), rect, &state->cancel);
    if (status == XXWIDGETS_OK) app->modal_default = state->cancel;
    return status;
}

xxwidgets_status xxwidgets_process_dialog(xxwidgets_widget *owner, const char *title, xx_pd_struct *progress, xxwidgets_process_update_fn update, void *user)
{
    process_state state = {0};
    xxwidgets_app *app;
    xxwidgets_widget *previous_focus = NULL, *widget;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    xxwidgets_rect rect;
    xxwidgets_status status, cleanup;
    uint64_t started, now;
    int owner_enabled, modal_attached = 0, finished = 0, updated = 0;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !title || !xxwidgets_valid_utf8(title) || !progress || !update) return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit) return XXWIDGETS_BUSY;
    status = xxwidgets_process_clock(&started);
    if (status != XXWIDGETS_OK) return status;
    state.owner = owner;
    previous_event = app->on_event;
    previous_user = app->user_data;
    owner_enabled = owner->enabled;
    for (widget = app->widgets; widget; widget = widget->next)
        if (widget->parent == owner && xxwidgets_focusable(widget)) {
            if (!previous_focus) previous_focus = widget;
            if (xxwidgets_widget_has_focus(widget) == 1) {
                previous_focus = widget;
                break;
            }
        }
    app->on_event = process_event;
    app->user_data = &state;
    status = xxwidgets_widget_set_enabled(owner, 0);
    while (status == XXWIDGETS_OK && !app->quit) {
        updated = 1;
        status = process_update(app, &state, progress, update, user, &finished);
        if (status != XXWIDGETS_OK || finished || app->quit) break;
        status = xxwidgets_process_clock(&now);
        if (status != XXWIDGETS_OK) break;
        if (!state.window && now - started > 1000) {
            rect = owner->rect;
            rect.width = 64;
            rect.height = 20;
            if (app->backend == XXWIDGETS_BACKEND_TUI) {
                if (owner->rect.width > rect.width) rect.x += (owner->rect.width - rect.width) / 2;
                if (owner->rect.height > rect.height) rect.y += (owner->rect.height - rect.height) / 2;
                if (rect.x > 32767) rect.x = 32767;
                if (rect.y > 32767) rect.y = 32767;
            }
            status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title, rect, &state.window);
            if (status != XXWIDGETS_OK) break;
            app->modal_window = state.window;
            status = xxwidgets_widget_set_visible(state.window, 0);
            if (status != XXWIDGETS_OK) break;
            status = process_create(app, &state);
            if (status != XXWIDGETS_OK) break;
            status = process_render(&state, progress, now - started);
            if (status != XXWIDGETS_OK) break;
            if (app->ops->modal_owner) {
                status = app->ops->modal_owner(state.window, owner, 1);
                if (status != XXWIDGETS_OK) break;
                modal_attached = 1;
            }
            status = xxwidgets_widget_set_visible(state.window, 1);
            if (status != XXWIDGETS_OK) break;
            if (!state.stop_requested) status = xxwidgets_widget_focus(state.cancel);
        } else if (state.window) status = process_render(&state, progress, now - started);
        if (status == XXWIDGETS_OK) status = xxwidgets_app_poll(app, 30);
    }
    if (updated && !finished) {
        /* UI shutdown/failure must not silently abandon the caller's worker.
         * Joining belongs to the caller; deliver the stop before returning. */
        state.stop_requested = 1;
        cleanup = process_update(app, &state, progress, update, user, &finished);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    cleanup = xxwidgets_widget_set_enabled(owner, owner_enabled);
    if (status == XXWIDGETS_OK) status = cleanup;
    if (modal_attached) {
        cleanup = app->ops->modal_owner(state.window, owner, 0);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (state.window) {
        cleanup = xxwidgets_widget_destroy(state.window);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    app->modal_window = NULL;
    app->modal_default = NULL;
    app->on_event = previous_event;
    app->user_data = previous_user;
    if (previous_focus) xxwidgets_widget_focus(previous_focus);
    return status;
}
