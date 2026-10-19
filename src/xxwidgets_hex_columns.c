/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxfclib/codepages/xx_codepage.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *const modes[] = {"Hex",    "byte",  "word",   "dword", "qword",  "uint8", "int8",
                                    "uint16", "int16", "uint32", "int32", "uint64", "int64"};
static const unsigned int widths[] = {8, 16, 24, 32, 48, 64};
static const char *const width_names[] = {"8", "16", "24", "32", "48", "64"};
static size_t unit_size(xxwidgets_hex_mode m)
{
    static const unsigned char sizes[] = {1, 1, 2, 4, 8, 1, 1, 2, 2, 4, 4, 8, 8};
    return sizes[m];
}
static size_t unit_width(xxwidgets_hex_mode m)
{
    static const unsigned char sizes[] = {2, 2, 4, 8, 16, 3, 4, 5, 6, 10, 11, 20, 20};
    return sizes[m];
}
static xxwidgets_status sync_display(xxwidgets_widget *w)
{
    xxwidgets_status s;
    ++w->hex_revision;
    w->hex_sync_full = 1;
    ++w->app->syncing;
    s = w->app->ops->sync(w);
    --w->app->syncing;
    w->hex_sync_full = 0;
    return s;
}
xxwidgets_status xxwidgets_hexview_get_layout(const xxwidgets_widget *w, uint64_t *base, unsigned int *columns)
{
    if (!w || w->kind != XXWIDGETS_HEXVIEW || !base || !columns)
        return XXWIDGETS_INVALID_ARGUMENT;
    *base = w->hex_base;
    *columns = w->hex_columns;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_hexview_set_display(xxwidgets_widget *w, const xxwidgets_hex_display *d)
{
    xxwidgets_hex_display old;
    const xx_codepage *cp;
    xxwidgets_status s;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || !d || (d->radix != 10 && d->radix != 16) || (d->offsets != 0 && d->offsets != 1) ||
        (unsigned)d->mode >= sizeof(modes) / sizeof(modes[0]))
        return XXWIDGETS_INVALID_ARGUMENT;
    cp = xx_codepage_find(d->encoding);
    if (!cp)
        return XXWIDGETS_INVALID_ARGUMENT;
    old = w->hex_display;
    w->hex_display = *d;
    w->hex_display.encoding = cp->name;
    s = sync_display(w);
    if (s != XXWIDGETS_OK) {
        w->hex_display = old;
        (void)sync_display(w);
    } else {
        w->hex_vertical_active = 0;
    }
    return s;
}
xxwidgets_status xxwidgets_hexview_get_display(const xxwidgets_widget *w, xxwidgets_hex_display *d)
{
    if (!w || w->kind != XXWIDGETS_HEXVIEW || !d)
        return XXWIDGETS_INVALID_ARGUMENT;
    *d = w->hex_display;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_hexview_get_active_column(const xxwidgets_widget *w, unsigned int *column)
{
    if (!w || w->kind != XXWIDGETS_HEXVIEW || !column)
        return XXWIDGETS_INVALID_ARGUMENT;
    *column = w->hex_header;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_hexview_set_column_width(xxwidgets_widget *w, size_t column, size_t cells)
{
    size_t old;
    xxwidgets_status s;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || column > 2 || (cells && cells < 4) || cells > 4096)
        return XXWIDGETS_INVALID_ARGUMENT;
    old = w->hex_widths[column];
    w->hex_widths[column] = cells;
    s = sync_display(w);
    if (s != XXWIDGETS_OK) {
        w->hex_widths[column] = old;
        (void)sync_display(w);
    }
    return s;
}
size_t xxwidgets_hexview_column_width(const xxwidgets_widget *w, size_t column)
{
    if (!w || w->kind != XXWIDGETS_HEXVIEW || column > 2)
        return 0;
    if (w->hex_widths[column])
        return w->hex_widths[column];
    if (!column)
        return w->hex_display.radix == 10 ? 22 : 19;
    if (column == 2)
        return w->hex_columns + 2;
    return (w->hex_columns / unit_size(w->hex_display.mode)) * (unit_width(w->hex_display.mode) + 1) + 2;
}
const char *xxwidgets_hexview_title(const xxwidgets_widget *w, size_t column)
{
    char *s = ((xxwidgets_widget *)w)->hex_title;
    size_t i, pos = 1, unit = unit_size(w->hex_display.mode);
    if (!column)
        return w->hex_display.offsets ? "Offset" : "Address";
    if (column == 2) {
        snprintf(s, sizeof(w->hex_title), "Symbols (%s)", w->hex_display.encoding);
        return s;
    }
    if (column != 1)
        return "";
    s[0] = ' ';
    for (i = 0; i < w->hex_columns; i += unit) {
        int n = snprintf(s + pos, sizeof(w->hex_title) - pos, "%0*zx ", (int)unit_width(w->hex_display.mode), i);
        if (n < 0 || (size_t)n >= sizeof(w->hex_title) - pos)
            break;
        pos += (size_t)n;
    }
    return s;
}
size_t xxwidgets_hexview_cursor_column(const xxwidgets_widget *w)
{
    if (w->hex_header == 2)
        return xxwidgets_hexview_column_width(w, 0) + 1 + xxwidgets_hexview_column_width(w, 1) + 1 + w->hex_cursor % w->hex_columns;
    size_t unit = unit_size(w->hex_display.mode), at = w->hex_cursor % w->hex_columns;
    size_t pos = (at / unit) * (unit_width(w->hex_display.mode) + 1) + 1;
    if (w->hex_display.mode <= XXWIDGETS_HEX_QWORD)
        pos += (unit - 1 - at % unit) * 2 + w->hex_nibble;
    return xxwidgets_hexview_column_width(w, 0) + 1 + pos;
}
static size_t symbol_decode_available(const xx_codepage *cp, size_t offset, size_t end)
{
    size_t available = end - offset;
    if (available > cp->max_bytes)
        available = cp->max_bytes;
    return available;
}
static size_t symbol_row(const xxwidgets_widget *w, size_t row, char *text, xxwidgets_hex_span *spans)
{
    const xx_codepage *cp = xx_codepage_find(w->hex_display.encoding), *utf8 = xx_codepage_find("UTF-8");
    size_t offset = row * w->hex_columns, end = offset + (w->hex_size - offset < w->hex_columns ? w->hex_size - offset : w->hex_columns);
    size_t at = offset, count = 0, pos = 0;
    int is_utf8 = !strcmp(cp->name, "UTF-8");
    int is_utf16 = !strcmp(cp->name, "UTF-16LE") || !strcmp(cp->name, "UTF-16BE");
    int is_utf32 = !strcmp(cp->name, "UTF-32LE") || !strcmp(cp->name, "UTF-32BE");
    int unicode = is_utf8 || is_utf16 || is_utf32;
    size_t unit = is_utf16 ? 2 : is_utf32 ? 4 : 1;
    size_t previous = offset;
    /* UTF-8 is self-synchronizing; only a valid sequence that owns the
     * first byte of this row can contribute a continuation-only span. */
    if (is_utf8)
        while (previous && offset - previous < 3 && (w->hex_data[previous] & 0xc0) == 0x80)
            --previous;
    else if (is_utf16 && offset >= 2)
        previous -= 2;
    if (previous < offset) {
        uint32_t scalar;
        size_t used = 0;
        if (xx_codepage_decode(cp, w->hex_data + previous, symbol_decode_available(cp, previous, w->hex_size), &used, &scalar) ==
                XX_CODEPAGE_OK && used > offset - previous) {
            if (spans)
                spans[count] = (xxwidgets_hex_span){0, 0, previous, used};
            ++count;
            at = previous + used;
        }
    }
    for (; at < end;) {
        uint32_t scalar;
        unsigned char encoded[4];
        size_t used = 0, written = 0;
        size_t available = symbol_decode_available(cp, at, unicode ? w->hex_size : end);
        /* Invalid code units remain byte dots. Resume decoding only on the
         * next unit boundary, including a window starting on a low surrogate. */
        if (at % unit || xx_codepage_decode(cp, w->hex_data + at, available, &used, &scalar) != XX_CODEPAGE_OK) {
            used = 1;
            scalar = '.';
        }
        if (scalar < 32 || (scalar >= 127 && scalar < 160) || scalar == 0x2028 || scalar == 0x2029 || (scalar >= 0x202a && scalar <= 0x202e) ||
            (scalar >= 0x2066 && scalar <= 0x2069) || scalar == 0xfeff)
            scalar = '.';
        if (xx_codepage_encode(utf8, scalar, encoded, sizeof(encoded), &written) == XX_CODEPAGE_OK) {
            if (text)
                memcpy(text + pos, encoded, written);
            if (spans)
                spans[count] = (xxwidgets_hex_span){pos, written, at, used};
            pos += written;
            ++count;
        }
        at += used;
    }
    if (text)
        text[pos] = 0;
    return count;
}
void xxwidgets_hexview_symbol_layout(const xxwidgets_widget *w, size_t row, const xxwidgets_hex_span *span, size_t *cell, size_t *cells)
{
    size_t offset = row * w->hex_columns;
    size_t available = w->hex_size - offset < w->hex_columns ? w->hex_size - offset : w->hex_columns;
    size_t start = span->offset > offset ? span->offset - offset : 0;
    size_t skipped = offset > span->offset ? offset - span->offset : 0;
    size_t length = skipped < span->bytes ? span->bytes - skipped : 0;
    if (start > available)
        start = available;
    if (length > available - start)
        length = available - start;
    *cell = start;
    *cells = length;
}
const char *xxwidgets_hexview_cell(const xxwidgets_widget *w, size_t row, size_t column)
{
    char *s = ((xxwidgets_widget *)w)->hex_cell;
    size_t offset, available, pos = 0, i, unit;
    xxwidgets_hex_mode mode = w->hex_display.mode;
    s[0] = 0;
    if (row >= w->item_count || column > 2)
        return s;
    offset = row * w->hex_columns;
    available = w->hex_size - offset;
    if (available > w->hex_columns)
        available = w->hex_columns;
    if (!column) {
        uint64_t address = (w->hex_display.offsets ? 0 : w->hex_base) + offset;
        if (w->hex_display.radix == 10)
            snprintf(s, sizeof(w->hex_cell), "%020" PRIu64, address);
        else
            snprintf(s, sizeof(w->hex_cell), "%08" PRIx32 ":%08" PRIx32, (uint32_t)(address >> 32), (uint32_t)address);
        return s;
    }
    if (column == 2) {
        (void)symbol_row(w, row, s, NULL);
        return s;
    }
    unit = unit_size(mode);
    size_t stride = unit_width(mode) + 1;
    size_t length = ((available + unit - 1) / unit) * stride + 1;
    memset(s, ' ', length);
    for (i = 0; i < available; i += unit) {
        size_t j, amount = available - i < unit ? available - i : unit;
        uint64_t value = 0;
        int count;
        char number[32];
        pos = (i / unit) * stride + 1;
        for (j = 0; j < amount; ++j)
            value |= (uint64_t)w->hex_data[offset + i + j] << (8 * j);
        if (amount < unit)
            count = snprintf(number, sizeof(number), "%*s", (int)unit_width(mode), "?");
        else if (mode <= XXWIDGETS_HEX_QWORD)
            count = snprintf(number, sizeof(number), "%0*" PRIx64, (int)(2 * unit), value);
        else if (mode == XXWIDGETS_HEX_INT8 || mode == XXWIDGETS_HEX_INT16 || mode == XXWIDGETS_HEX_INT32 || mode == XXWIDGETS_HEX_INT64) {
            /* Avoid implementation-defined unsigned-to-signed conversion. */
            int64_t signed_value =
                (value & (UINT64_C(1) << (8 * unit - 1))) ? -1 - (int64_t)((~value) & (UINT64_MAX >> (64 - 8 * unit))) : (int64_t)value;
            count = snprintf(number, sizeof(number), "%*" PRId64, (int)unit_width(mode), signed_value);
        } else
            count = snprintf(number, sizeof(number), "%*" PRIu64, (int)unit_width(mode), value);
        if (count < 0)
            break;
        memcpy(s + pos, number, (size_t)count);
    }
    if (w->hex_editable && w->hex_cursor >= offset && w->hex_cursor < offset + available) {
        pos = ((w->hex_cursor - offset) / unit) * stride;
        s[pos] = '[';
        s[pos + stride] = ']';
    }
    s[length] = 0;
    return s;
}
size_t xxwidgets_hexview_spans(const xxwidgets_widget *w, size_t row, size_t column, xxwidgets_hex_span spans[64])
{
    size_t count = 0, offset, available, i, pos = 0;
    if (row >= w->item_count || column < 1 || column > 2)
        return 0;
    offset = row * w->hex_columns;
    available = w->hex_size - offset;
    if (available > w->hex_columns)
        available = w->hex_columns;
    if (column == 2) {
        count = symbol_row(w, row, NULL, spans);
    } else {
        size_t unit = unit_size(w->hex_display.mode), width = unit_width(w->hex_display.mode);
        for (i = 0; i < available; i += unit) {
            size_t amount = available - i < unit ? available - i : unit;
            pos = i / unit * (width + 1) + 1;
            if (w->hex_display.mode <= XXWIDGETS_HEX_QWORD && amount == unit) {
                for (size_t j = 0; j < unit; ++j)
                    spans[count++] = (xxwidgets_hex_span){pos + j * 2, 2, offset + i + unit - 1 - j, 1};
            } else
                spans[count++] = (xxwidgets_hex_span){pos, width, offset + i, amount};
        }
    }
    return count;
}
size_t xxwidgets_hexview_selection_spans(const xxwidgets_widget *w, size_t row, size_t column, xxwidgets_hex_span spans[64])
{
    size_t count = xxwidgets_hexview_spans(w, row, column, spans), selected = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!xxwidgets_hexview_contains(w, spans[i].offset, spans[i].bytes))
            continue;
        if (selected && spans[i].start <= spans[selected - 1].start + spans[selected - 1].length + 1)
            spans[selected - 1].length = spans[i].start + spans[i].length - spans[selected - 1].start;
        else
            spans[selected++] = spans[i];
    }
    return selected;
}
size_t xxwidgets_hexview_hit(const xxwidgets_widget *w, size_t row, size_t column, size_t text_index)
{
    xxwidgets_hex_span spans[64];
    size_t count = xxwidgets_hexview_spans(w, row, column, spans);
    if (!column)
        return row < w->item_count ? row * w->hex_columns : SIZE_MAX;
    for (size_t i = 0; i < count; ++i)
        if (text_index < spans[i].start + spans[i].length)
            return spans[i].offset;
    return count ? spans[count - 1].offset : SIZE_MAX;
}
int xxwidgets_hexview_hit_span(const xxwidgets_widget *w, size_t row, size_t column, size_t cell_index, xxwidgets_hex_span *out)
{
    xxwidgets_hex_span spans[64];
    size_t count, row_offset;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || !out || row >= w->item_count || column > 2)
        return 0;
    row_offset = row * w->hex_columns;
    if (!column) {
        *out = (xxwidgets_hex_span){0, 1, row_offset, 1};
        return 1;
    }
    count = xxwidgets_hexview_spans(w, row, column, spans);
    for (size_t i = 0; i < count; ++i) {
        size_t end = spans[i].start + spans[i].length;
        if (column == 2) {
            size_t cell, cells;
            xxwidgets_hexview_symbol_layout(w, row, spans + i, &cell, &cells);
            end = cell + cells;
        }
        if (cell_index < end) {
            *out = spans[i];
            return 1;
        }
    }
    if (count) *out = spans[count - 1];
    return count != 0;
}
size_t xxwidgets_hexview_menu_count(unsigned int column)
{
    return column == 0 ? 4 : column == 1 ? 19 : column == 2 ? xx_codepage_count() : column == 3 ? 12 : 0;
}
xxwidgets_hex_menu_item xxwidgets_hexview_menu_item(const xxwidgets_widget *w, unsigned int column, size_t index)
{
    xxwidgets_hex_menu_item item = {"", "", 0, 0};
    if (index >= xxwidgets_hexview_menu_count(column))
        return item;
    if (column == 3) {
        static const char *const titles[] = {"Dump to file", "Beginning", "End",  "Selection start", "Selection end", "All", "Row",
                                             "Clear",        "Hex",       "Text", "C array",         "Offset"};
        item.title = titles[index];
        item.group = !index ? "" : index < 5 ? "Go to" : index < 8 ? "Select" : "Copy";
        item.disabled = !w->hex_size || ((index == 0 || index == 3 || index == 4 || index >= 7) &&
                        !(w->hex_source_read ? w->hex_source_length : w->hex_selection_length)) ||
                        (index == 6 && w->hex_cursor >= w->hex_size) || (index >= 8 && !w->app->ops->copy_text && !w->hex_copy_handler);
    } else if (!column) {
        static const char *const titles[] = {"10", "16", "Address", "Offset"};
        item.group = index < 2 ? "Base" : "Location";
        item.title = titles[index];
        item.checked = index < 2 ? w->hex_display.radix == (index ? 16 : 10) : w->hex_display.offsets == (index == 3);
    } else if (column == 1) {
        item.group = index < 13 ? "Mode" : "Width";
        item.title = index < 13 ? modes[index] : width_names[index - 13];
        item.checked = index < 13 ? (size_t)w->hex_display.mode == index : w->hex_columns == widths[index - 13];
    } else {
        item.title = xx_codepage_at(index)->name;
        item.checked = !strcmp(w->hex_display.encoding, item.title);
    }
    return item;
}
xxwidgets_status xxwidgets_hexview_menu_apply(xxwidgets_widget *w, unsigned int column, size_t index)
{
    xxwidgets_hex_display d;
    xxwidgets_status status;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || index >= xxwidgets_hexview_menu_count(column))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (column == 3)
        return xxwidgets_hexview_body_action(w, index);
    d = w->hex_display;
    if (!column) {
        if (index < 2)
            d.radix = index ? 16 : 10;
        else
            d.offsets = index == 3;
    } else if (column == 1) {
        if (index >= 13) {
            status = xxwidgets_hexview_set_layout(w, w->hex_base, widths[index - 13]);
            if (status == XXWIDGETS_OK)
                xxwidgets_emit(w, XXWIDGETS_EVENT_VIEW_CHANGE, (int)column);
            return status;
        }
        d.mode = (xxwidgets_hex_mode)index;
    } else
        d.encoding = xx_codepage_at(index)->name;
    status = xxwidgets_hexview_set_display(w, &d);
    if (status == XXWIDGETS_OK)
        xxwidgets_emit(w, XXWIDGETS_EVENT_VIEW_CHANGE, (int)column);
    return status;
}
