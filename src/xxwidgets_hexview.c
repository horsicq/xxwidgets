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

#include <limits.h>
#include <stdio.h>
#include <inttypes.h>
#include "xxfclib/codepages/xx_codepage.h"
#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#else
#include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>

static xxwidgets_status sync_hexview(xxwidgets_widget *widget)
{
    xxwidgets_status status;
    ++widget->app->syncing;
    status = widget->app->ops->sync(widget);
    --widget->app->syncing;
    return status;
}

static void source_local_selection(xxwidgets_widget *w)
{
    if (!w->hex_source_read || w->hex_source_seeking)
        return;
    w->hex_source_anchor = w->hex_anchor < w->hex_size ? w->hex_base + w->hex_anchor : UINT64_MAX;
    w->hex_source_anchor_bytes = w->hex_anchor < w->hex_size ? w->hex_anchor_bytes : 0;
    w->hex_source_selection = w->hex_selection_length ? w->hex_base + w->hex_selection : UINT64_MAX;
    w->hex_source_length = w->hex_selection_length;
}

static void free_hex_rows(char **rows, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) free(rows[i]);
    free(rows);
}

void xxwidgets_hexview_init(xxwidgets_widget *widget)
{
    widget->hex_columns = 16;
    widget->hex_display.radix = 16;
    widget->hex_display.encoding = "ASCII";
    widget->hex_cursor = SIZE_MAX;
    widget->hex_anchor = widget->hex_selection = SIZE_MAX;
    widget->hex_vertical_active = 0;
}

void xxwidgets_hexview_activate_column(xxwidgets_widget *widget, unsigned int column)
{
    if (!widget || widget->kind != XXWIDGETS_HEXVIEW || column > 2)
        return;
    if (widget->hex_header != column)
        widget->hex_vertical_active = 0;
    widget->hex_header = column;
}

xxwidgets_status xxwidgets_hexview_set_active_column(xxwidgets_widget *widget, unsigned int column)
{
    unsigned int previous;
    int previous_vertical;
    xxwidgets_status status;
    if (!widget || widget->kind != XXWIDGETS_HEXVIEW || column > 2) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->hex_header == column) return XXWIDGETS_OK;
    previous = widget->hex_header;
    previous_vertical = widget->hex_vertical_active;
    xxwidgets_hexview_activate_column(widget, column);
    widget->hex_sync_full = 1;
    status = sync_hexview(widget);
    if (status != XXWIDGETS_OK) {
        widget->hex_header = previous;
        widget->hex_vertical_active = previous_vertical;
        sync_hexview(widget);
    }
    widget->hex_sync_full = 0;
    return status;
}

void xxwidgets_hexview_dispose(xxwidgets_widget *widget)
{
    free(widget->hex_highlights); widget->hex_highlights = NULL; widget->hex_highlight_count = 0;
    free(widget->hex_data);
    widget->hex_data = NULL;
    widget->hex_size = 0;
}

int xxwidgets_hexview_highlight_at(const xxwidgets_widget *w, uint64_t at, uint32_t *color)
{
    int found = 0, priority = 0;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || !color) return 0;
    for (size_t i = 0; i < w->hex_highlight_count; ++i) {
        const xxwidgets_hexview_highlight *r = w->hex_highlights + i;
        if (at >= r->offset && at - r->offset < r->length && (!found || r->priority >= priority)) {
            *color = r->color; priority = r->priority; found = 1;
        }
    }
    return found;
}
int xxwidgets_hexview_highlight_span(const xxwidgets_widget *w, size_t offset, size_t length, uint32_t *color)
{
    int found = 0, priority = 0;
    if (!w || !length || offset > UINT64_MAX - w->hex_base || !color) return 0;
    uint64_t at = w->hex_base + offset;
    for (size_t i = 0; i < w->hex_highlight_count; ++i) {
        const xxwidgets_hexview_highlight *r = w->hex_highlights + i;
        int overlap = r->length && (at >= r->offset ? at - r->offset < r->length : r->offset - at < length);
        if (overlap && (!found || r->priority >= priority)) { *color = r->color; priority = r->priority; found = 1; }
    }
    return found;
}
xxwidgets_status xxwidgets_hexview_set_highlights(xxwidgets_widget *w, const xxwidgets_hexview_highlight *ranges, size_t count)
{
    xxwidgets_hexview_highlight *copy = NULL;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || (!ranges && count) || count > SIZE_MAX / sizeof(*copy)) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i)
        if ((ranges[i].length && ranges[i].length - 1 > UINT64_MAX - ranges[i].offset) || ranges[i].color > 0xFFFFFF) return XXWIDGETS_INVALID_ARGUMENT;
    if (count) { copy = malloc(count * sizeof(*copy)); if (!copy) return XXWIDGETS_OUT_OF_MEMORY; memcpy(copy, ranges, count * sizeof(*copy)); }
    xxwidgets_hexview_highlight *old = w->hex_highlights; size_t old_count = w->hex_highlight_count;
    w->hex_highlights = copy; w->hex_highlight_count = count; w->hex_sync_full = 1;
    xxwidgets_status status = sync_hexview(w);
    if (status != XXWIDGETS_OK) { w->hex_highlights = old; w->hex_highlight_count = old_count; free(copy); sync_hexview(w); }
    else free(old);
    w->hex_sync_full = 0; return status;
}
static xxwidgets_status hex_row(const unsigned char *data, size_t size, uint64_t base, unsigned int columns, size_t row, int editable, size_t cursor,
                                size_t changed_offset, unsigned char changed_byte, char **out_row)
{
    static const char digits[] = "0123456789ABCDEF";
    size_t offset = row * columns, available = size - offset, pos = 0;
    uint64_t address = base + (uint64_t)offset;
    unsigned int i;
    char *line;
    *out_row = NULL;
    line = (char *)malloc(22 + 4 * (size_t)columns);
    if (!line) return XXWIDGETS_OUT_OF_MEMORY;
    if (available > columns) available = columns;
    for (i = 0; i < 16; ++i) line[pos++] = digits[(address >> ((15 - i) * 4)) & 15];
    line[pos++] = ' ';
    line[pos++] = ' ';
    for (i = 0; i < columns; ++i) {
        size_t at = offset + i;
        unsigned char byte = i < available ? (at == changed_offset ? changed_byte : data[at]) : 0;
        line[pos++] = i < available ? digits[byte >> 4] : ' ';
        line[pos++] = i < available ? digits[byte & 15] : ' ';
        line[pos++] = ' ';
    }
    line[pos++] = ' ';
    line[pos++] = '|';
    for (i = 0; i < columns; ++i) {
        size_t at = offset + i;
        unsigned char byte = i < available ? (at == changed_offset ? changed_byte : data[at]) : 0;
        line[pos++] = i >= available ? ' ' : (byte >= 32 && byte <= 126 ? (char)byte : '.');
    }
    line[pos++] = '|';
    line[pos] = 0;
    if (editable && cursor < size && cursor / columns == row) {
        size_t column = cursor % columns;
        line[17 + 3 * column] = '[';
        line[20 + 3 * column] = ']';
    }
    *out_row = line;
    return XXWIDGETS_OK;
}

static xxwidgets_status hex_rows(const unsigned char *data, size_t size, uint64_t base, unsigned int columns, int editable, size_t cursor, char ***out_rows,
                                  size_t *out_count)
{
    size_t count = size / columns + (size % columns != 0), row;
    char **rows;
    *out_rows = NULL;
    *out_count = 0;
    if (size && (uintmax_t)(size - 1) > UINT64_MAX - base) return XXWIDGETS_INVALID_ARGUMENT;
    if (!count) return XXWIDGETS_OK;
    if (count > INT_MAX || count > SIZE_MAX / sizeof(*rows)) return XXWIDGETS_INVALID_ARGUMENT;
    rows = (char **)calloc(count, sizeof(*rows));
    if (!rows) return XXWIDGETS_OUT_OF_MEMORY;
    for (row = 0; row < count; ++row) {
        xxwidgets_status status = hex_row(data, size, base, columns, row, editable, cursor, SIZE_MAX, 0, &rows[row]);
        if (status != XXWIDGETS_OK) {
            free_hex_rows(rows, row);
            return status;
        }
    }
    *out_rows = rows;
    *out_count = count;
    return XXWIDGETS_OK;
}

static xxwidgets_status replace_hex(xxwidgets_widget *widget, unsigned char *data, size_t size, uint64_t base, unsigned int columns, int reset_selection)
{
    char **rows, **previous_rows = widget->items;
    unsigned char *previous_data = widget->hex_data;
    size_t count, previous_count = widget->item_count, previous_size = widget->hex_size;
    uint64_t previous_base = widget->hex_base, previous_revision = widget->hex_revision;
    unsigned int previous_columns = widget->hex_columns;
    size_t previous_cursor = widget->hex_cursor, cursor;
    unsigned int previous_nibble = widget->hex_nibble, nibble;
    size_t old_anchor = widget->hex_anchor, old_anchor_bytes = widget->hex_anchor_bytes;
    size_t old_selection = widget->hex_selection, old_length = widget->hex_selection_length;
    int old_dragging = widget->hex_dragging, old_pending_dump = widget->hex_pending_dump;
    int previous_value = widget->value;
    size_t selected_offset = previous_value < 0 ? 0 : (size_t)previous_value * previous_columns;
    int value;
    xxwidgets_status status;
    if (!size) {
        cursor = SIZE_MAX;
        nibble = 0;
        value = -1;
    } else if (reset_selection) {
        cursor = 0;
        nibble = 0;
        value = 0;
    } else if (previous_cursor < size) {
        cursor = previous_cursor;
        nibble = previous_nibble;
        value = (int)(cursor / columns);
    } else if (previous_value >= 0) {
        cursor = selected_offset < size ? selected_offset : size - 1;
        nibble = 0;
        value = (int)(cursor / columns);
    } else {
        cursor = SIZE_MAX;
        nibble = 0;
        value = -1;
    }
    status = hex_rows(data, size, base, columns, widget->hex_editable, cursor, &rows, &count);
    if (status != XXWIDGETS_OK) return status;
    widget->items = rows;
    widget->item_count = count;
    widget->hex_data = data;
    widget->hex_size = size;
    widget->hex_base = base;
    widget->hex_columns = columns;
    widget->hex_revision = previous_revision + 1;
    widget->hex_cursor = cursor;
    widget->hex_nibble = nibble;
    widget->value = value;
    if (reset_selection) {
        widget->hex_anchor = widget->hex_selection = cursor;
        widget->hex_anchor_bytes = size ? 1 : 0;
        widget->hex_selection_length = size ? 1 : 0;
        widget->hex_pending_dump = widget->hex_dragging = 0;
    }
    widget->hex_sync_full = 1;
    widget->hex_sync_dirty_count = 0;
    status = sync_hexview(widget);
    if (status != XXWIDGETS_OK) {
        widget->hex_anchor = old_anchor;
        widget->hex_anchor_bytes = old_anchor_bytes;
        widget->hex_selection = old_selection;
        widget->hex_selection_length = old_length;
        widget->hex_dragging = old_dragging;
        widget->hex_pending_dump = old_pending_dump;
        widget->items = previous_rows;
        widget->item_count = previous_count;
        widget->hex_data = previous_data;
        widget->hex_size = previous_size;
        widget->hex_base = previous_base;
        widget->hex_columns = previous_columns;
        widget->hex_revision = previous_revision;
        widget->hex_cursor = previous_cursor;
        widget->hex_nibble = previous_nibble;
        widget->value = previous_value;
        widget->hex_sync_full = 1;
        widget->hex_sync_dirty_count = 0;
        sync_hexview(widget);
        free_hex_rows(rows, count);
    } else {
        widget->hex_vertical_active = 0;
        if (reset_selection || base != previous_base)
            source_local_selection(widget);
        free_hex_rows(previous_rows, previous_count);
        if (data != previous_data) free(previous_data);
    }
    widget->hex_sync_full = 0;
    widget->hex_sync_dirty_count = 0;
    return status;
}

typedef struct hex_refresh_row {
    size_t index;
    char *previous;
    char *replacement;
} hex_refresh_row;

static void add_hex_refresh_row(hex_refresh_row rows[3], size_t *count, size_t index)
{
    size_t i;
    for (i = 0; i < *count; ++i)
        if (rows[i].index == index) return;
    rows[*count].index = index;
    rows[*count].previous = NULL;
    rows[*count].replacement = NULL;
    ++*count;
}

/* Change cursor/edit mode and optionally one byte as a single backend-visible
 * transaction. At most the old cursor, new cursor and changed-byte rows need
 * reformatting; the backend row revision still makes the update observable. */
static xxwidgets_status refresh_hex(xxwidgets_widget *widget, int editable, size_t cursor, unsigned int nibble, int value, size_t changed_offset,
                                    unsigned char changed_byte, size_t anchor, size_t anchor_bytes, size_t selection, size_t selection_length)
{
    hex_refresh_row rows[3];
    size_t count = 0, i;
    size_t previous_cursor = widget->hex_cursor;
    size_t old_anchor = widget->hex_anchor, old_anchor_bytes = widget->hex_anchor_bytes;
    size_t old_selection = widget->hex_selection, old_length = widget->hex_selection_length;
    unsigned int previous_nibble = widget->hex_nibble;
    uint64_t previous_revision = widget->hex_revision;
    int previous_editable = widget->hex_editable, previous_value = widget->value;
    unsigned char previous_byte = 0;
    xxwidgets_status status;
    if (widget->hex_editable && previous_cursor < widget->hex_size)
        add_hex_refresh_row(rows, &count, previous_cursor / widget->hex_columns);
    if (editable && cursor < widget->hex_size) add_hex_refresh_row(rows, &count, cursor / widget->hex_columns);
    if (changed_offset < widget->hex_size) add_hex_refresh_row(rows, &count, changed_offset / widget->hex_columns);
    for (i = 0; i < count; ++i) {
        status = hex_row(widget->hex_data, widget->hex_size, widget->hex_base, widget->hex_columns, rows[i].index, editable, cursor, changed_offset, changed_byte,
                         &rows[i].replacement);
        if (status != XXWIDGETS_OK) {
            while (i) free(rows[--i].replacement);
            return status;
        }
        rows[i].previous = widget->items[rows[i].index];
    }
    if (changed_offset < widget->hex_size) {
        previous_byte = widget->hex_data[changed_offset];
        widget->hex_data[changed_offset] = changed_byte;
    }
    for (i = 0; i < count; ++i) widget->items[rows[i].index] = rows[i].replacement;
    widget->hex_anchor = anchor;
    widget->hex_anchor_bytes = anchor_bytes;
    widget->hex_selection = selection;
    widget->hex_selection_length = selection_length;
    widget->hex_editable = editable;
    widget->hex_cursor = cursor;
    widget->hex_nibble = nibble;
    widget->value = value;
    widget->hex_revision = previous_revision + (count != 0);
    widget->hex_sync_full = 0;
    widget->hex_sync_dirty_count = count;
    for (i = 0; i < count; ++i) widget->hex_sync_dirty_rows[i] = rows[i].index;
    status = sync_hexview(widget);
    if (status != XXWIDGETS_OK) {
        if (changed_offset < widget->hex_size) widget->hex_data[changed_offset] = previous_byte;
        for (i = 0; i < count; ++i) widget->items[rows[i].index] = rows[i].previous;
        widget->hex_anchor = old_anchor;
        widget->hex_anchor_bytes = old_anchor_bytes;
        widget->hex_selection = old_selection;
        widget->hex_selection_length = old_length;
        widget->hex_editable = previous_editable;
        widget->hex_cursor = previous_cursor;
        widget->hex_nibble = previous_nibble;
        widget->value = previous_value;
        widget->hex_revision = previous_revision;
        widget->hex_sync_full = 1;
        widget->hex_sync_dirty_count = 0;
        sync_hexview(widget);
        for (i = 0; i < count; ++i) free(rows[i].replacement);
    } else {
        widget->hex_vertical_active = 0;
        for (i = 0; i < count; ++i) free(rows[i].previous);
    }
    widget->hex_sync_full = 0;
    widget->hex_sync_dirty_count = 0;
    return status;
}

xxwidgets_status xxwidgets_hexview_set_data(xxwidgets_widget *widget, const void *data, size_t size)
{
    unsigned char *copy = NULL;
    xxwidgets_status status;
    if (!widget || widget->kind != XXWIDGETS_HEXVIEW || (!data && size) || (size && (uintmax_t)(size - 1) > UINT64_MAX - widget->hex_base) ||
        size / widget->hex_columns + (size % widget->hex_columns != 0) > INT_MAX ||
        (widget->hex_source_read && !widget->hex_source_seeking &&
         (widget->hex_base > widget->hex_source_size || size > widget->hex_source_size - widget->hex_base)))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (size) {
        copy = (unsigned char *)malloc(size);
        if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
        memcpy(copy, data, size);
    }
    status = replace_hex(widget, copy, size, widget->hex_base, widget->hex_columns, 1);
    if (status != XXWIDGETS_OK) free(copy);
    return status;
}

xxwidgets_status xxwidgets_hexview_set_source(xxwidgets_widget *w, uint64_t size, xxwidgets_hexview_source_read_fn read,
                                             xxwidgets_hexview_source_seek_fn seek, void *user)
{
    if (!w || w->kind != XXWIDGETS_HEXVIEW || ((read == NULL) != (seek == NULL)) || (!read && size) || w->hex_source_seeking ||
        (read && (w->hex_editable || w->hex_base > size || w->hex_size > size - w->hex_base)))
        return XXWIDGETS_INVALID_ARGUMENT;
    w->hex_source_read = read;
    w->hex_source_seek = seek;
    w->hex_source_user = user;
    w->hex_source_size = size;
    w->hex_source_anchor = w->hex_source_selection = UINT64_MAX;
    w->hex_source_anchor_bytes = w->hex_source_length = 0;
    w->hex_vertical_active = 0;
    source_local_selection(w);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_hexview_get_source_selection(const xxwidgets_widget *w, uint64_t *offset, uint64_t *length)
{
    if (!w || w->kind != XXWIDGETS_HEXVIEW || !offset || !length)
        return XXWIDGETS_INVALID_ARGUMENT;
    *offset = w->hex_source_read ? w->hex_source_selection : w->hex_selection_length ? w->hex_base + w->hex_selection : UINT64_MAX;
    *length = w->hex_source_read ? w->hex_source_length : w->hex_selection_length;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_hexview_set_layout(xxwidgets_widget *widget, uint64_t base_address, unsigned int bytes_per_row)
{
    if (!widget || widget->kind != XXWIDGETS_HEXVIEW || (bytes_per_row != 8 && bytes_per_row != 16 && bytes_per_row != 24 && bytes_per_row != 32 && bytes_per_row != 48 && bytes_per_row != 64)) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->hex_source_read && !widget->hex_source_seeking &&
        (base_address > widget->hex_source_size || widget->hex_size > widget->hex_source_size - base_address))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (base_address == widget->hex_base && bytes_per_row == widget->hex_columns) return XXWIDGETS_OK;
    return replace_hex(widget, widget->hex_data, widget->hex_size, base_address, bytes_per_row, 0);
}

size_t xxwidgets_hexview_size(const xxwidgets_widget *widget)
{
    return widget && widget->kind == XXWIDGETS_HEXVIEW ? widget->hex_size : 0;
}

xxwidgets_status xxwidgets_hexview_get_data(const xxwidgets_widget *widget, void *data, size_t capacity, size_t *required)
{
    if (!widget || widget->kind != XXWIDGETS_HEXVIEW || !required || (!data && capacity)) return XXWIDGETS_INVALID_ARGUMENT;
    *required = widget->hex_size;
    if (!data && !capacity) return XXWIDGETS_OK;
    if (capacity < widget->hex_size) return XXWIDGETS_BUFFER_TOO_SMALL;
    if (widget->hex_size) memcpy(data, widget->hex_data, widget->hex_size);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_hexview_set_editable(xxwidgets_widget *widget, int editable)
{
    if (!widget || widget->kind != XXWIDGETS_HEXVIEW) return XXWIDGETS_INVALID_ARGUMENT;
    editable = editable != 0;
    if (editable && widget->hex_source_read)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (editable == widget->hex_editable) return XXWIDGETS_OK;
    return refresh_hex(widget, editable, widget->hex_cursor, widget->hex_nibble, widget->value, SIZE_MAX, 0, widget->hex_anchor,
                       widget->hex_anchor_bytes, widget->hex_selection, widget->hex_selection_length);
}

xxwidgets_status xxwidgets_hexview_get_editable(const xxwidgets_widget *widget, int *editable)
{
    if (!widget || widget->kind != XXWIDGETS_HEXVIEW || !editable) return XXWIDGETS_INVALID_ARGUMENT;
    *editable = widget->hex_editable;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_hexview_set_cursor(xxwidgets_widget *widget, size_t offset, unsigned int nibble)
{
    int value;
    xxwidgets_status status;
    if (!widget || widget->kind != XXWIDGETS_HEXVIEW || nibble > 1 || (offset == SIZE_MAX ? nibble != 0 : offset >= widget->hex_size))
        return XXWIDGETS_INVALID_ARGUMENT;
    value = offset == SIZE_MAX ? -1 : (int)(offset / widget->hex_columns);
    if (offset == widget->hex_cursor && nibble == widget->hex_nibble && value == widget->value &&
        widget->hex_anchor == offset && widget->hex_selection == offset &&
        widget->hex_anchor_bytes == (offset < widget->hex_size ? 1u : 0u) &&
        widget->hex_selection_length == (offset < widget->hex_size ? 1u : 0u)) {
        widget->hex_vertical_active = 0;
        source_local_selection(widget);
        return XXWIDGETS_OK;
    }
    status = refresh_hex(widget, widget->hex_editable, offset, nibble, value, SIZE_MAX, 0, offset,
                         offset < widget->hex_size ? 1 : 0, offset, offset < widget->hex_size ? 1 : 0);
    if (status == XXWIDGETS_OK)
        source_local_selection(widget);
    return status;
}

xxwidgets_status xxwidgets_hexview_get_cursor(const xxwidgets_widget *widget, size_t *offset, unsigned int *nibble)
{
    if (!widget || widget->kind != XXWIDGETS_HEXVIEW || !offset || !nibble) return XXWIDGETS_INVALID_ARGUMENT;
    *offset = widget->hex_cursor;
    *nibble = widget->hex_nibble;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_hexview_selection_input(xxwidgets_widget *widget, int row)
{
    size_t cursor;
    if (!widget || widget->kind != XXWIDGETS_HEXVIEW || row < -1 || (row >= 0 && (size_t)row >= widget->item_count)) return XXWIDGETS_INVALID_ARGUMENT;
    cursor = row < 0 ? SIZE_MAX : (size_t)row * widget->hex_columns;
    if (row < 0)
        return xxwidgets_hexview_set_selection(widget, SIZE_MAX, 0);
    size_t length = widget->hex_size - cursor;
    if (length > widget->hex_columns)
        length = widget->hex_columns;
    return xxwidgets_hexview_set_selection(widget, cursor, length);
}

static xxwidgets_status select_bytes(xxwidgets_widget *w, size_t anchor, size_t anchor_bytes, size_t cursor, size_t start, size_t length);

static int symbol_at(const xxwidgets_widget *w, size_t offset, xxwidgets_hex_span *span)
{
    return offset < w->hex_size && xxwidgets_hexview_hit_span(w, offset / w->hex_columns, 2, offset % w->hex_columns, span);
}

xxwidgets_status xxwidgets_hexview_set_symbol_cursor(xxwidgets_widget *w, size_t offset)
{
    xxwidgets_hex_span symbol;
    xxwidgets_status status;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || !symbol_at(w, offset, &symbol))
        return XXWIDGETS_INVALID_ARGUMENT;
    status = select_bytes(w, symbol.offset, symbol.bytes, symbol.offset, symbol.offset, symbol.bytes);
    if (status == XXWIDGETS_OK)
        source_local_selection(w);
    return status;
}

static xxwidgets_status select_source_range(xxwidgets_widget *w, uint64_t anchor, uint64_t anchor_bytes, size_t cursor,
                                            uint64_t start, uint64_t length)
{
    uint64_t visible_start = start > w->hex_base ? start : w->hex_base;
    uint64_t end = start + length, window_end = w->hex_base + w->hex_size;
    uint64_t visible_end = end < window_end ? end : window_end;
    size_t local_anchor = anchor >= w->hex_base && anchor - w->hex_base < w->hex_size ? (size_t)(anchor - w->hex_base) : SIZE_MAX;
    size_t local_start = length && visible_start < visible_end ? (size_t)(visible_start - w->hex_base) : SIZE_MAX;
    size_t local_length = local_start == SIZE_MAX ? 0 : (size_t)(visible_end - visible_start);
    xxwidgets_status status;
    if (start > w->hex_source_size || length > w->hex_source_size - start ||
        (anchor_bytes && (anchor >= w->hex_source_size || anchor_bytes > w->hex_source_size - anchor)))
        return XXWIDGETS_INVALID_ARGUMENT;
    status = select_bytes(w, local_anchor, local_anchor == SIZE_MAX ? 0 : (size_t)anchor_bytes, cursor, local_start, local_length);
    if (status == XXWIDGETS_OK) {
        w->hex_source_anchor = anchor_bytes ? anchor : UINT64_MAX;
        w->hex_source_anchor_bytes = anchor_bytes;
        w->hex_source_selection = length ? start : UINT64_MAX;
        w->hex_source_length = length;
    }
    return status;
}

static int source_needs_seek(const xxwidgets_widget *w, uint64_t offset)
{
    uint64_t before, after;
    if (offset < w->hex_base || offset - w->hex_base >= w->hex_size ||
        w->hex_base > w->hex_source_size || w->hex_size > w->hex_source_size - w->hex_base)
        return 1;
    before = offset < 3 ? offset : 3;
    after = w->hex_source_size - offset - 1;
    if (after > 3) after = 3;
    return offset - w->hex_base < before || w->hex_size - 1 - (size_t)(offset - w->hex_base) < after;
}

/* Only a loaded window is copied, never the source or its global selection.
 * A seek callback may use several data/layout/cursor setters; retain the old
 * view until both the callback and the final selection transaction succeed. */
typedef struct source_guard {
    xxwidgets_widget previous;
    unsigned char *data;
    char **rows;
    int active;
} source_guard;

static void source_guard_dispose(source_guard *guard)
{
    free(guard->data);
    free_hex_rows(guard->rows, guard->rows ? guard->previous.item_count : 0);
    guard->data = NULL; guard->rows = NULL; guard->active = 0;
}

static xxwidgets_status source_guard_begin(xxwidgets_widget *w, uint64_t offset, source_guard *guard)
{
    size_t i;
    memset(guard, 0, sizeof(*guard));
    if (!source_needs_seek(w, offset)) return XXWIDGETS_OK;
    guard->previous = *w;
    if (w->hex_size) {
        guard->data = (unsigned char *)malloc(w->hex_size);
        if (!guard->data) return XXWIDGETS_OUT_OF_MEMORY;
        memcpy(guard->data, w->hex_data, w->hex_size);
    }
    if (w->item_count) {
        guard->rows = (char **)calloc(w->item_count, sizeof(*guard->rows));
        if (!guard->rows) { source_guard_dispose(guard); return XXWIDGETS_OUT_OF_MEMORY; }
        for (i = 0; i < w->item_count; ++i) {
            guard->rows[i] = xxwidgets_strdup(w->items[i]);
            if (!guard->rows[i]) { source_guard_dispose(guard); return XXWIDGETS_OUT_OF_MEMORY; }
        }
    }
    guard->active = 1;
    return XXWIDGETS_OK;
}

static void source_guard_restore(xxwidgets_widget *w, source_guard *guard)
{
    const xxwidgets_widget *old = &guard->previous;
    if (!guard->active) return;
    free(w->hex_data); free_hex_rows(w->items, w->item_count);
    w->hex_data = guard->data; w->items = guard->rows;
    guard->data = NULL; guard->rows = NULL;
    w->hex_size = old->hex_size; w->item_count = old->item_count;
    w->hex_base = old->hex_base; w->hex_columns = old->hex_columns;
    w->hex_revision = old->hex_revision; w->hex_display = old->hex_display;
    w->hex_cursor = old->hex_cursor; w->hex_nibble = old->hex_nibble; w->value = old->value;
    w->hex_anchor = old->hex_anchor; w->hex_anchor_bytes = old->hex_anchor_bytes;
    w->hex_selection = old->hex_selection; w->hex_selection_length = old->hex_selection_length;
    w->hex_source_anchor = old->hex_source_anchor; w->hex_source_anchor_bytes = old->hex_source_anchor_bytes;
    w->hex_source_selection = old->hex_source_selection; w->hex_source_length = old->hex_source_length;
    w->hex_vertical_active = old->hex_vertical_active;
    w->hex_vertical_row = old->hex_vertical_row; w->hex_vertical_column = old->hex_vertical_column;
    w->hex_source_vertical_row = old->hex_source_vertical_row;
    w->hex_pending_dump = old->hex_pending_dump; w->hex_dragging = old->hex_dragging;
    w->hex_sync_full = 1; w->hex_sync_dirty_count = 0;
    sync_hexview(w);
    w->hex_sync_full = 0;
    guard->active = 0;
}

static xxwidgets_status source_seek(xxwidgets_widget *w, uint64_t offset)
{
    xxwidgets_status status;
    if (offset >= w->hex_source_size)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (!source_needs_seek(w, offset))
        return XXWIDGETS_OK;
    ++w->hex_source_seeking;
    status = w->hex_source_seek(w, offset, w->hex_source_user);
    --w->hex_source_seeking;
    if (status == XXWIDGETS_OK && (offset < w->hex_base || offset - w->hex_base >= w->hex_size ||
                                 w->hex_base > w->hex_source_size || w->hex_size > w->hex_source_size - w->hex_base))
        return XXWIDGETS_INVALID_ARGUMENT;
    return status;
}

xxwidgets_status xxwidgets_hexview_set_source_selection(xxwidgets_widget *w, uint64_t offset, uint64_t length)
{
    return xxwidgets_hexview_set_source_range(w, offset, offset, length);
}

xxwidgets_status xxwidgets_hexview_set_source_range(xxwidgets_widget *w, uint64_t cursor, uint64_t offset, uint64_t length)
{
    xxwidgets_status status;
    source_guard guard;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || !w->hex_source_read ||
        offset > w->hex_source_size || length > w->hex_source_size - offset || (length && cursor >= w->hex_source_size))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (!length) return xxwidgets_hexview_set_selection(w, SIZE_MAX, 0);
    status = source_guard_begin(w, cursor, &guard);
    if (status != XXWIDGETS_OK) return status;
    status = source_seek(w, cursor);
    if (status == XXWIDGETS_OK) status = select_source_range(w, offset, 1, (size_t)(cursor - w->hex_base), offset, length);
    if (status != XXWIDGETS_OK) source_guard_restore(w, &guard);
    source_guard_dispose(&guard);
    return status;
}

static xxwidgets_status source_symbol_at(const xxwidgets_widget *w, uint64_t offset, uint64_t *start, uint64_t *length)
{
    unsigned char data[12];
    xxwidgets_widget view = {0};
    xxwidgets_hex_span symbol;
    uint64_t base, remaining;
    if (offset >= w->hex_source_size) return XXWIDGETS_INVALID_ARGUMENT;
    base = offset > 4 ? (offset - 4) & ~UINT64_C(3) : 0;
    remaining = w->hex_source_size - offset;
    size_t bytes = (size_t)(offset - base) + (size_t)(remaining < 4 ? remaining : 4);
    xxwidgets_status status = w->hex_source_read(w->hex_source_user, base, data, bytes);
    if (status != XXWIDGETS_OK) return status;
    view.kind = XXWIDGETS_HEXVIEW; view.hex_data = data; view.hex_size = bytes;
    view.hex_columns = 16; view.item_count = 1; view.hex_display = w->hex_display;
    if (!symbol_at(&view, (size_t)(offset - base), &symbol)) return XXWIDGETS_INVALID_ARGUMENT;
    *start = base + symbol.offset; *length = symbol.bytes;
    return XXWIDGETS_OK;
}

static xxwidgets_status move_source(xxwidgets_widget *w, unsigned int key, unsigned int modifiers)
{
    int symbols = w->hex_header == 2, valid_cursor = w->hex_cursor < w->hex_size;
    int page = key == XXWIDGETS_KEY_PAGEUP || key == XXWIDGETS_KEY_PAGEDOWN;
    int up = key == XXWIDGETS_KEY_UP || key == XXWIDGETS_KEY_PAGEUP;
    int vertical = key == XXWIDGETS_KEY_UP || key == XXWIDGETS_KEY_DOWN || page;
    int edge = key == XXWIDGETS_KEY_HOME || key == XXWIDGETS_KEY_END;
    int extend = (modifiers & XXWIDGETS_MOD_SHIFT) != 0;
    uint64_t current, current_bytes = 1, at, target, bytes = 1, row = 0, last_row;
    uint64_t anchor, anchor_bytes, start, end;
    size_t column = 0, rows = 1;
    source_guard guard;
    xxwidgets_status status;
    if (!w->hex_source_size) return XXWIDGETS_OK;
    current = valid_cursor ? w->hex_base + w->hex_cursor : 0;
    at = current;
    anchor = extend && w->hex_source_anchor_bytes ? w->hex_source_anchor : current;
    anchor_bytes = extend && w->hex_source_anchor_bytes ? w->hex_source_anchor_bytes : 1;
    if (symbols) {
        status = source_symbol_at(w, current, &current, &current_bytes);
        if (status != XXWIDGETS_OK) return status;
        if (extend && anchor_bytes) {
            status = source_symbol_at(w, anchor, &anchor, &anchor_bytes);
            if (status != XXWIDGETS_OK) return status;
        }
    }
    last_row = (w->hex_source_size - 1) / w->hex_columns;
    if (vertical || edge) {
        row = w->hex_vertical_active ? w->hex_source_vertical_row : at / w->hex_columns;
        if (row > last_row) row = last_row;
        column = w->hex_vertical_active && w->hex_vertical_column < w->hex_columns ? w->hex_vertical_column : (size_t)(at % w->hex_columns);
        if (vertical) {
            if (page) rows = w->app->ops->hexview_page_rows ? w->app->ops->hexview_page_rows(w)
                : (size_t)(w->rect.height > 2 ? w->rect.height - 1 : 1);
            if (!rows) rows = 1;
            if (valid_cursor) row = up ? row > rows ? row - rows : 0 : rows < last_row - row ? row + rows : last_row;
            at = row * w->hex_columns;
            at += column < w->hex_source_size - at ? column : w->hex_source_size - at - 1;
        } else {
            if (modifiers & (XXWIDGETS_MOD_CTRL | XXWIDGETS_MOD_META)) at = key == XXWIDGETS_KEY_HOME ? 0 : w->hex_source_size - 1;
            else {
                at = row * w->hex_columns;
                if (key == XXWIDGETS_KEY_END) at += w->hex_source_size - at < w->hex_columns ? w->hex_source_size - at - 1 : w->hex_columns - 1;
            }
            row = at / w->hex_columns;
        }
    } else if (key == XXWIDGETS_KEY_LEFT) at = current ? current - 1 : 0;
    else if (key == XXWIDGETS_KEY_RIGHT) at = valid_cursor && current_bytes < w->hex_source_size - current ? current + current_bytes : current;
    target = at;
    if (symbols) {
        status = source_symbol_at(w, at, &target, &bytes);
        if (status != XXWIDGETS_OK) return status;
    }
    if (!extend) { anchor = target; anchor_bytes = bytes; }
    if (edge) column = target / w->hex_columns < row ? 0 : (size_t)(target % w->hex_columns);
    start = anchor < target ? anchor : target;
    end = anchor + anchor_bytes > target + bytes ? anchor + anchor_bytes : target + bytes;
    status = source_guard_begin(w, target + bytes - 1, &guard);
    if (status != XXWIDGETS_OK) return status;
    status = source_seek(w, target + bytes - 1);
    if (status == XXWIDGETS_OK && (target < w->hex_base || target - w->hex_base >= w->hex_size ||
        bytes > w->hex_size - (size_t)(target - w->hex_base))) status = XXWIDGETS_INVALID_ARGUMENT;
    if (status == XXWIDGETS_OK) status = select_source_range(w, anchor, anchor_bytes, (size_t)(target - w->hex_base), start, end - start);
    if (status != XXWIDGETS_OK) source_guard_restore(w, &guard);
    source_guard_dispose(&guard);
    if (status == XXWIDGETS_OK) {
        w->hex_vertical_active = vertical || edge;
        if (vertical || edge) {
            w->hex_source_vertical_row = row; w->hex_vertical_column = column;
            w->hex_vertical_row = row * w->hex_columns >= w->hex_base ? (size_t)((row * w->hex_columns - w->hex_base) / w->hex_columns) : 0;
        }
        xxwidgets_emit(w, XXWIDGETS_EVENT_SELECT, w->value);
    }
    return status;
}

static xxwidgets_status move_symbol(xxwidgets_widget *w, unsigned int key, unsigned int modifiers)
{
    xxwidgets_hex_span current, target, anchor;
    size_t start, end, row = 0, column = 0;
    int page = key == XXWIDGETS_KEY_PAGEUP || key == XXWIDGETS_KEY_PAGEDOWN;
    int vertical = key == XXWIDGETS_KEY_UP || key == XXWIDGETS_KEY_DOWN || page;
    int row_edge = key == XXWIDGETS_KEY_HOME || key == XXWIDGETS_KEY_END;
    int remember_row = vertical || row_edge;
    int extend = (modifiers & XXWIDGETS_MOD_SHIFT) != 0;
    uint64_t absolute_anchor = UINT64_MAX, absolute_anchor_bytes = 0;
    xxwidgets_status status;
    if (w->hex_source_read) {
        if (!w->hex_source_size)
            return XXWIDGETS_OK;
        if (extend && w->hex_source_anchor < w->hex_source_size) {
            absolute_anchor = w->hex_source_anchor;
            absolute_anchor_bytes = w->hex_source_anchor_bytes;
            if (absolute_anchor >= w->hex_base && absolute_anchor - w->hex_base < w->hex_size &&
                symbol_at(w, (size_t)(absolute_anchor - w->hex_base), &anchor)) {
                absolute_anchor = w->hex_base + anchor.offset;
                absolute_anchor_bytes = anchor.bytes;
            }
        }
        if (row_edge && (modifiers & (XXWIDGETS_MOD_CTRL | XXWIDGETS_MOD_META))) {
            status = source_seek(w, key == XXWIDGETS_KEY_HOME ? 0 : w->hex_source_size - 1);
            if (status != XXWIDGETS_OK)
                return status;
        }
    }
    if (!w->hex_size)
        return XXWIDGETS_OK;
    if (!symbol_at(w, w->hex_cursor < w->hex_size ? w->hex_cursor : 0, &current))
        return XXWIDGETS_INVALID_ARGUMENT;
    target = current;
    if (vertical) {
        size_t cursor = w->hex_cursor < w->hex_size ? w->hex_cursor : 0;
        size_t at, available, rows = 1;
        if (page) {
            rows = w->app->ops->hexview_page_rows ? w->app->ops->hexview_page_rows(w)
                                                  : (size_t)(w->rect.height > 2 ? w->rect.height - 1 : 1);
            if (!rows)
                rows = 1;
        }
        if (w->hex_vertical_active && w->hex_vertical_row < w->item_count && w->hex_vertical_column < w->hex_columns) {
            row = w->hex_vertical_row;
            column = w->hex_vertical_column;
        } else {
            row = cursor / w->hex_columns;
            column = cursor % w->hex_columns;
        }
        if (w->hex_cursor < w->hex_size) {
            if (key == XXWIDGETS_KEY_UP || key == XXWIDGETS_KEY_PAGEUP)
                row = row > rows ? row - rows : 0;
            else
                row = rows < w->item_count - 1 - row ? row + rows : w->item_count - 1;
        }
        at = row * w->hex_columns;
        available = w->hex_size - at < w->hex_columns ? w->hex_size - at : w->hex_columns;
        at += column < available ? column : available - 1;
        if (!symbol_at(w, at, &target))
            return XXWIDGETS_INVALID_ARGUMENT;
    } else if (row_edge) {
        size_t cursor = w->hex_cursor < w->hex_size ? w->hex_cursor : 0;
        size_t at, available;
        if (modifiers & (XXWIDGETS_MOD_CTRL | XXWIDGETS_MOD_META)) {
            at = w->hex_source_read ? (size_t)((key == XXWIDGETS_KEY_HOME ? 0 : w->hex_source_size - 1) - w->hex_base)
                                    : key == XXWIDGETS_KEY_HOME ? 0 : w->hex_size - 1;
            row = at / w->hex_columns;
        } else {
            row = w->hex_vertical_active && w->hex_vertical_row < w->item_count ? w->hex_vertical_row : cursor / w->hex_columns;
            at = row * w->hex_columns;
            available = w->hex_size - at < w->hex_columns ? w->hex_size - at : w->hex_columns;
            if (key == XXWIDGETS_KEY_END)
                at += available - 1;
        }
        if (!symbol_at(w, at, &target))
            return XXWIDGETS_INVALID_ARGUMENT;
        /* A row-edge move starts a fresh vertical run at the selected
         * glyph's leading visible position, including a continuation prefix. */
        column = target.offset > row * w->hex_columns ? target.offset - row * w->hex_columns : 0;
    } else if (key == XXWIDGETS_KEY_LEFT && current.offset) {
        if (!symbol_at(w, current.offset - 1, &target))
            return XXWIDGETS_INVALID_ARGUMENT;
    } else if (key == XXWIDGETS_KEY_RIGHT && w->hex_cursor < w->hex_size && current.bytes < w->hex_size - current.offset) {
        if (!symbol_at(w, current.offset + current.bytes, &target))
            return XXWIDGETS_INVALID_ARGUMENT;
    }
    if (w->hex_source_read) {
        uint64_t target_offset = w->hex_base + target.offset;
        uint64_t absolute_start, absolute_end;
        if (!absolute_anchor_bytes) {
            absolute_anchor = target_offset;
            absolute_anchor_bytes = target.bytes;
        }
        absolute_start = absolute_anchor < target_offset ? absolute_anchor : target_offset;
        absolute_end = absolute_anchor + absolute_anchor_bytes > target_offset + target.bytes
                           ? absolute_anchor + absolute_anchor_bytes : target_offset + target.bytes;
        status = select_source_range(w, absolute_anchor, absolute_anchor_bytes, target.offset, absolute_start, absolute_end - absolute_start);
        if (status == XXWIDGETS_OK) {
            w->hex_vertical_active = remember_row;
            if (remember_row) {
                w->hex_vertical_column = column;
                w->hex_vertical_row = row;
            }
            xxwidgets_emit(w, XXWIDGETS_EVENT_SELECT, w->value);
        }
        return status;
    }
    anchor = target;
    if (extend && w->hex_anchor < w->hex_size && !symbol_at(w, w->hex_anchor, &anchor))
        return XXWIDGETS_INVALID_ARGUMENT;
    start = anchor.offset < target.offset ? anchor.offset : target.offset;
    end = anchor.offset + anchor.bytes > target.offset + target.bytes ? anchor.offset + anchor.bytes : target.offset + target.bytes;
    if (w->hex_cursor == target.offset && !w->hex_nibble && w->hex_anchor == anchor.offset && w->hex_anchor_bytes == anchor.bytes &&
        w->hex_selection == start && w->hex_selection_length == end - start) {
        w->hex_vertical_active = remember_row;
        if (remember_row) {
            w->hex_vertical_column = column;
            w->hex_vertical_row = row;
        }
        return XXWIDGETS_OK;
    }
    status = select_bytes(w, anchor.offset, anchor.bytes, target.offset, start, end - start);
    if (status == XXWIDGETS_OK) {
        w->hex_vertical_active = remember_row;
        if (remember_row) {
            w->hex_vertical_column = column;
            w->hex_vertical_row = row;
        }
        xxwidgets_emit(w, XXWIDGETS_EVENT_SELECT, w->value);
    }
    return status;
}

xxwidgets_status xxwidgets_hexview_set_input_handler(xxwidgets_widget *widget, xxwidgets_hexview_input_fn handler, void *user)
{
    if (!widget || widget->kind != XXWIDGETS_HEXVIEW) return XXWIDGETS_INVALID_ARGUMENT;
    widget->hex_input_handler = handler; widget->hex_input_user = handler ? user : NULL;
    return XXWIDGETS_OK;
}

static xxwidgets_status application_input(xxwidgets_widget *w, unsigned int key, unsigned int modifiers, int *handled)
{
    xxwidgets_hex_input in = {0};
    int command = (modifiers & (XXWIDGETS_MOD_CTRL | XXWIDGETS_MOD_META)) != 0;
    if (!w->hex_input_handler || ((modifiers & XXWIDGETS_MOD_ALT) && (key < 128 || key > 0x10ffff)) || w->hex_header == 0) return XXWIDGETS_OK;
    in.offset = w->hex_base + (w->hex_cursor < w->hex_size ? w->hex_cursor : w->hex_size);
    in.column = w->hex_header; in.nibble = w->hex_nibble;
    (void)xxwidgets_hexview_get_source_selection(w, &in.selection_offset, &in.selection_length);
    if (command && (key == 'v' || key == 'V' || key == 22)) in.kind = XXWIDGETS_HEX_INPUT_PASTE;
    else if (!command && key == XXWIDGETS_KEY_DELETE) in.kind = XXWIDGETS_HEX_INPUT_DELETE;
    else if (!command && key == XXWIDGETS_KEY_BACKSPACE) in.kind = XXWIDGETS_HEX_INPUT_BACKSPACE;
    else if (!command && in.column == 1 && ((key >= '0' && key <= '9') || (key >= 'a' && key <= 'f') || (key >= 'A' && key <= 'F'))) {
        in.kind = XXWIDGETS_HEX_INPUT_NIBBLE; in.value = key <= '9' ? key - '0' : (key & ~32u) - 'A' + 10;
    } else if (!command && in.column == 2 && key >= 32 && key <= 0x10ffff && !(key >= 0xd800 && key <= 0xdfff)) {
        in.kind = XXWIDGETS_HEX_INPUT_SCALAR; in.value = key;
    } else return XXWIDGETS_OK;
    *handled = 1;
    return w->hex_input_handler(w, &in, w->hex_input_user);
}

xxwidgets_status xxwidgets_hexview_key_input(xxwidgets_widget *widget, unsigned int key, unsigned int modifiers, int *handled)
{
    size_t cursor, old_cursor, changed_offset = SIZE_MAX, row_start, row_end;
    unsigned int nibble, digit = 0;
    unsigned char changed_byte = 0;
    int digit_key = 0, old_value, value;
    uint64_t revision;
    xxwidgets_status status;
    if (!handled) return XXWIDGETS_INVALID_ARGUMENT;
    *handled = 0;
    if (!widget || widget->kind != XXWIDGETS_HEXVIEW) return XXWIDGETS_INVALID_ARGUMENT;
    if ((modifiers & (XXWIDGETS_MOD_CTRL | XXWIDGETS_MOD_META)) && !(modifiers & XXWIDGETS_MOD_ALT)) {
        if (key >= 1 && key <= 26)
            key += 'A' - 1;
        if (key == 'a' || key == 'A' || key == 'c' || key == 'C' || key == 'd' || key == 'D') {
            *handled = 1;
            return xxwidgets_hexview_body_action(widget, key == 'a' || key == 'A' ? 5
                : key == 'c' || key == 'C' ? (widget->hex_header == 2 ? 9 : 8) : 0);
        }
    }
    status = application_input(widget, key, modifiers, handled);
    if (status != XXWIDGETS_OK || *handled) return status;
    if (modifiers & XXWIDGETS_MOD_ALT)
        return XXWIDGETS_OK;
    if (widget->hex_source_read && (key == XXWIDGETS_KEY_LEFT || key == XXWIDGETS_KEY_RIGHT ||
        key == XXWIDGETS_KEY_UP || key == XXWIDGETS_KEY_DOWN || key == XXWIDGETS_KEY_PAGEUP || key == XXWIDGETS_KEY_PAGEDOWN ||
        key == XXWIDGETS_KEY_HOME || key == XXWIDGETS_KEY_END)) {
        *handled = 1;
        return move_source(widget, key, modifiers);
    }
    if (widget->hex_header == 2 && (key == XXWIDGETS_KEY_LEFT || key == XXWIDGETS_KEY_RIGHT ||
                                  key == XXWIDGETS_KEY_UP || key == XXWIDGETS_KEY_DOWN ||
                                  key == XXWIDGETS_KEY_PAGEUP || key == XXWIDGETS_KEY_PAGEDOWN ||
                                  key == XXWIDGETS_KEY_HOME || key == XXWIDGETS_KEY_END)) {
        *handled = 1;
        return move_symbol(widget, key, modifiers);
    }
    if (key == XXWIDGETS_KEY_LEFT || key == XXWIDGETS_KEY_RIGHT || key == XXWIDGETS_KEY_UP || key == XXWIDGETS_KEY_DOWN ||
        key == XXWIDGETS_KEY_HOME || key == XXWIDGETS_KEY_END || key == XXWIDGETS_KEY_PAGEUP || key == XXWIDGETS_KEY_PAGEDOWN) {
        if (!widget->hex_editable || (modifiers & (XXWIDGETS_MOD_SHIFT | XXWIDGETS_MOD_CTRL | XXWIDGETS_MOD_META)) || key == XXWIDGETS_KEY_PAGEUP ||
            key == XXWIDGETS_KEY_PAGEDOWN) {
            size_t at = widget->hex_cursor < widget->hex_size ? widget->hex_cursor : 0;
            size_t step = widget->hex_columns;
            *handled = 1;
            if (!widget->hex_size)
                return XXWIDGETS_OK;
            if (key == XXWIDGETS_KEY_PAGEUP || key == XXWIDGETS_KEY_PAGEDOWN)
                step *= (size_t)(widget->rect.height > 2 ? widget->rect.height - 1 : 1);
            if (key == XXWIDGETS_KEY_LEFT)
                at = at ? at - 1 : 0;
            if (key == XXWIDGETS_KEY_RIGHT && widget->hex_cursor < widget->hex_size && at + 1 < widget->hex_size)
                ++at;
            if (key == XXWIDGETS_KEY_UP || key == XXWIDGETS_KEY_PAGEUP)
                at = at >= step ? at - step : 0;
            if ((key == XXWIDGETS_KEY_DOWN || key == XXWIDGETS_KEY_PAGEDOWN) && widget->hex_cursor < widget->hex_size)
                at = step < widget->hex_size - at ? at + step : widget->hex_size - 1;
            if (key == XXWIDGETS_KEY_HOME)
                at = modifiers & (XXWIDGETS_MOD_CTRL | XXWIDGETS_MOD_META) ? 0 : at / widget->hex_columns * widget->hex_columns;
            if (key == XXWIDGETS_KEY_END) {
                at = modifiers & (XXWIDGETS_MOD_CTRL | XXWIDGETS_MOD_META) ? widget->hex_size - 1
                                                                           : (at / widget->hex_columns + 1) * widget->hex_columns - 1;
                if (at >= widget->hex_size)
                    at = widget->hex_size - 1;
            }
            return xxwidgets_hexview_pointer(widget, at, (modifiers & XXWIDGETS_MOD_SHIFT) != 0);
        }
    }
    if (!widget->hex_editable || (modifiers & (XXWIDGETS_MOD_CTRL | XXWIDGETS_MOD_META)))
        return XXWIDGETS_OK;
    if (key >= '0' && key <= '9') {
        digit = key - '0';
        digit_key = 1;
    } else if (key >= 'A' && key <= 'F') {
        digit = key - 'A' + 10;
        digit_key = 1;
    } else if (key >= 'a' && key <= 'f') {
        digit = key - 'a' + 10;
        digit_key = 1;
    } else if (key != XXWIDGETS_KEY_LEFT && key != XXWIDGETS_KEY_RIGHT && key != XXWIDGETS_KEY_UP && key != XXWIDGETS_KEY_DOWN &&
               key != XXWIDGETS_KEY_HOME && key != XXWIDGETS_KEY_END)
        return XXWIDGETS_OK;
    *handled = 1;
    old_cursor = cursor = widget->hex_cursor;
    nibble = widget->hex_nibble;
    if (cursor >= widget->hex_size) return XXWIDGETS_OK;
    old_value = widget->value;
    if (digit_key) {
        changed_offset = cursor;
        changed_byte = widget->hex_data[cursor];
        if (!nibble) {
            changed_byte = (unsigned char)((changed_byte & 0x0f) | (digit << 4));
            nibble = 1;
        } else {
            changed_byte = (unsigned char)((changed_byte & 0xf0) | digit);
            if (cursor + 1 < widget->hex_size) {
                ++cursor;
                nibble = 0;
            }
        }
    } else if (key == XXWIDGETS_KEY_LEFT) {
        if (nibble) nibble = 0;
        else if (cursor) {
            --cursor;
            nibble = 1;
        }
    } else if (key == XXWIDGETS_KEY_RIGHT) {
        if (!nibble) nibble = 1;
        else if (cursor + 1 < widget->hex_size) {
            ++cursor;
            nibble = 0;
        }
    } else if (key == XXWIDGETS_KEY_UP) {
        if (cursor >= widget->hex_columns) cursor -= widget->hex_columns;
    } else if (key == XXWIDGETS_KEY_DOWN) {
        row_start = (cursor / widget->hex_columns + 1) * widget->hex_columns;
        if (row_start < widget->hex_size) {
            size_t column = cursor % widget->hex_columns;
            cursor = row_start + column;
            if (cursor >= widget->hex_size) cursor = widget->hex_size - 1;
        }
    } else {
        row_start = cursor / widget->hex_columns * widget->hex_columns;
        row_end = row_start + widget->hex_columns;
        if (row_end > widget->hex_size) row_end = widget->hex_size;
        if (key == XXWIDGETS_KEY_HOME) {
            cursor = row_start;
            nibble = 0;
        } else {
            cursor = row_end - 1;
            nibble = 1;
        }
    }
    value = (int)(cursor / widget->hex_columns);
    if (!digit_key && cursor == widget->hex_cursor && nibble == widget->hex_nibble && value == widget->value) return XXWIDGETS_OK;
    status = refresh_hex(widget, widget->hex_editable, cursor, nibble, value, changed_offset, changed_byte, cursor, 1, cursor, 1);
    if (status != XXWIDGETS_OK) return status;
    source_local_selection(widget);
    revision = widget->hex_revision;
    if (digit_key) xxwidgets_emit(widget, XXWIDGETS_EVENT_CHANGE, (int)(changed_offset / widget->hex_columns));
    if ((value != old_value || (!digit_key && cursor != old_cursor)) && widget->hex_revision == revision && widget->value == value)
        xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, value);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_hexview_get_selection(xxwidgets_widget *w, size_t *offset, size_t *length)
{
    if (!w || w->kind != XXWIDGETS_HEXVIEW || !offset || !length)
        return XXWIDGETS_INVALID_ARGUMENT;
    *offset = w->hex_selection_length ? w->hex_selection : SIZE_MAX;
    *length = w->hex_selection_length;
    return XXWIDGETS_OK;
}

static xxwidgets_status select_bytes(xxwidgets_widget *w, size_t anchor, size_t anchor_bytes, size_t cursor, size_t start, size_t length)
{
    return refresh_hex(w, w->hex_editable, cursor, 0, cursor == SIZE_MAX ? -1 : (int)(cursor / w->hex_columns), SIZE_MAX, 0, anchor, anchor_bytes, start, length);
}
xxwidgets_status xxwidgets_hexview_set_selection(xxwidgets_widget *w, size_t offset, size_t length)
{
    xxwidgets_status status;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || (length && (offset >= w->hex_size || length > w->hex_size - offset)))
        return XXWIDGETS_INVALID_ARGUMENT;
    status = select_bytes(w, length ? offset : SIZE_MAX, length ? 1 : 0, length ? offset : SIZE_MAX, length ? offset : SIZE_MAX, length);
    if (status == XXWIDGETS_OK)
        source_local_selection(w);
    return status;
}
int xxwidgets_hexview_contains(const xxwidgets_widget *w, size_t offset, size_t length)
{
    return length && w->hex_selection_length && offset < w->hex_selection + w->hex_selection_length && w->hex_selection < offset + length;
}
xxwidgets_status xxwidgets_hexview_pointer(xxwidgets_widget *w, size_t offset, int extend)
{
    return xxwidgets_hexview_pointer_span(w, offset, 1, extend);
}

xxwidgets_status xxwidgets_hexview_pointer_span(xxwidgets_widget *w, size_t offset, size_t bytes, int extend)
{
    size_t anchor = offset, anchor_bytes = bytes, start, end;
    xxwidgets_status status;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || !bytes || offset >= w->hex_size || bytes > w->hex_size - offset)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (w->hex_source_read) {
        uint64_t absolute = w->hex_base + offset;
        uint64_t absolute_anchor = extend && w->hex_source_anchor_bytes ? w->hex_source_anchor : absolute;
        uint64_t absolute_bytes = extend && w->hex_source_anchor_bytes ? w->hex_source_anchor_bytes : bytes;
        uint64_t absolute_start = absolute_anchor < absolute ? absolute_anchor : absolute;
        uint64_t absolute_end = absolute_anchor + absolute_bytes > absolute + bytes ? absolute_anchor + absolute_bytes : absolute + bytes;
        status = select_source_range(w, absolute_anchor, absolute_bytes, offset, absolute_start, absolute_end - absolute_start);
        if (status == XXWIDGETS_OK)
            xxwidgets_emit(w, XXWIDGETS_EVENT_SELECT, w->value);
        return status;
    }
    if (extend && w->hex_anchor < w->hex_size) {
        anchor = w->hex_anchor;
        anchor_bytes = w->hex_anchor_bytes ? w->hex_anchor_bytes : 1;
    }
    start = anchor < offset ? anchor : offset;
    end = anchor + anchor_bytes > offset + bytes ? anchor + anchor_bytes : offset + bytes;
    if (w->hex_cursor == offset && w->hex_anchor == anchor && w->hex_anchor_bytes == anchor_bytes &&
        w->hex_selection == start && w->hex_selection_length == end - start) {
        w->hex_vertical_active = 0;
        return XXWIDGETS_OK;
    }
    status = select_bytes(w, anchor, anchor_bytes, offset, start, end - start);
    if (status == XXWIDGETS_OK)
        xxwidgets_emit(w, XXWIDGETS_EVENT_SELECT, w->value);
    return status;
}

xxwidgets_status xxwidgets_hexview_set_copy_handler(xxwidgets_widget *w, xxwidgets_hexview_copy_fn handler, void *user_data)
{
    if (!w || w->kind != XXWIDGETS_HEXVIEW) return XXWIDGETS_INVALID_ARGUMENT;
    w->hex_copy_handler = handler;
    w->hex_copy_user = handler ? user_data : NULL;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_hexview_set_dump_handler(xxwidgets_widget *w, xxwidgets_hexview_dump_fn handler, void *user_data)
{
    if (!w || w->kind != XXWIDGETS_HEXVIEW) return XXWIDGETS_INVALID_ARGUMENT;
    w->hex_dump_handler = handler;
    w->hex_dump_user = handler ? user_data : NULL;
    w->hex_pending_dump = 0;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_hexview_copy_selection(xxwidgets_widget *w, xxwidgets_hex_copy_format format)
{
    char *text, *p;
    size_t length, start, i;
    uint64_t absolute_start, absolute_length;
    unsigned char *source_data = NULL;
    const unsigned char *bytes;
    xxwidgets_status status;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || (unsigned)format > XXWIDGETS_HEX_COPY_OFFSET)
        return XXWIDGETS_INVALID_ARGUMENT;
    (void)xxwidgets_hexview_get_source_selection(w, &absolute_start, &absolute_length);
    if (!absolute_length)
        return XXWIDGETS_UNAVAILABLE;
    if (w->hex_copy_handler) return w->hex_copy_handler(w, format, w->hex_copy_user);
    if (!w->app->ops->copy_text) return XXWIDGETS_UNAVAILABLE;
    if (format == XXWIDGETS_HEX_COPY_OFFSET) {
        char offset_text[32];
        uint64_t value = w->hex_source_read ? absolute_start : (uint64_t)w->hex_selection + (w->hex_display.offsets ? 0 : w->hex_base);
        sprintf(offset_text, "0x%" PRIx64, value);
        return w->app->ops->copy_text(w->parent, offset_text);
    }
    if (absolute_length > (SIZE_MAX - 64) / 6)
        return XXWIDGETS_OUT_OF_MEMORY;
    length = (size_t)absolute_length;
    start = w->hex_selection;
    text = malloc(length * 6 + 64);
    if (!text)
        return XXWIDGETS_OUT_OF_MEMORY;
    bytes = w->hex_source_read ? NULL : w->hex_data + start;
    if (w->hex_source_read) {
        source_data = malloc(length);
        if (!source_data) {
            free(text);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        for (i = 0; i < length;) {
            size_t count = length - i < 65536 ? length - i : 65536;
            status = w->hex_source_read(w->hex_source_user, absolute_start + i, source_data + i, count);
            if (status != XXWIDGETS_OK) {
                free(source_data);
                free(text);
                return status;
            }
            i += count;
        }
        bytes = source_data;
    }
    p = text;
    if (format == XXWIDGETS_HEX_COPY_TEXT) {
        const xx_codepage *cp = xx_codepage_find(w->hex_display.encoding), *utf8 = xx_codepage_find("UTF-8");
        size_t unit = !strcmp(cp->name, "UTF-16LE") || !strcmp(cp->name, "UTF-16BE") ? 2
                    : !strcmp(cp->name, "UTF-32LE") || !strcmp(cp->name, "UTF-32BE") ? 4 : 1;
        size_t alignment = (size_t)((w->hex_source_read ? absolute_start : (uint64_t)start) % unit);
        for (i = 0; i < length;) {
            size_t used = 0, written = 0;
            uint32_t scalar;
            if ((alignment + i % unit) % unit || xx_codepage_decode(cp, bytes + i, length - i, &used, &scalar) != XX_CODEPAGE_OK) {
                used = 1;
                scalar = '.';
            }
            if (scalar < 32 || (scalar >= 127 && scalar < 160))
                scalar = '.';
            if (xx_codepage_encode(utf8, scalar, (unsigned char *)p, 4, &written) == XX_CODEPAGE_OK)
                p += written;
            i += used;
        }
    } else {
        if (format == XXWIDGETS_HEX_COPY_C_ARRAY) {
            *p++ = '{';
            *p++ = ' ';
        }
        for (i = 0; i < length; ++i) {
            if (i) {
                if (format == XXWIDGETS_HEX_COPY_C_ARRAY)
                    *p++ = ',';
                *p++ = ' ';
            }
            p += sprintf(p, format == XXWIDGETS_HEX_COPY_C_ARRAY ? "0x%02X" : "%02X", bytes[i]);
        }
        if (format == XXWIDGETS_HEX_COPY_C_ARRAY) {
            *p++ = ' ';
            *p++ = '}';
        }
    }
    *p = 0;
    status = w->app->ops->copy_text(w->parent, text);
    free(source_data);
    free(text);
    return status;
}
static xxwidgets_status save_hex_selection(xxwidgets_widget *w, const char *path, int overwrite)
{
    FILE *file = NULL;
    int ok;
    int atomic_save;
    uint64_t start, length;
    xxwidgets_status status = XXWIDGETS_OK;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || !path || !*path || !xxwidgets_valid_utf8(path))
        return XXWIDGETS_INVALID_ARGUMENT;
    (void)xxwidgets_hexview_get_source_selection(w, &start, &length);
    if (!length)
        return XXWIDGETS_INVALID_ARGUMENT;
    atomic_save = w->hex_source_read && overwrite;
#ifdef _WIN32
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *wide = count ? malloc((size_t)count * sizeof(*wide)) : NULL;
    wchar_t *temporary = NULL;
    if (!wide)
        return XXWIDGETS_OUT_OF_MEMORY;
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, count);
    if (atomic_save) {
        wchar_t *slash = wcsrchr(wide, L'\\'), *forward = wcsrchr(wide, L'/');
        size_t directory, capacity;
        if (forward && (!slash || forward > slash))
            slash = forward;
        directory = slash ? (size_t)(slash - wide) + 1 : count > 2 && wide[1] == L':' ? 2 : 0;
        capacity = directory + 64;
        if (capacity > SIZE_MAX / sizeof(*temporary)) {
            free(wide);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        temporary = malloc(capacity * sizeof(*temporary));
        if (!temporary) {
            free(wide);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        memcpy(temporary, wide, directory * sizeof(*temporary));
        for (unsigned int attempt = 0; attempt < 128 && !file; ++attempt) {
            swprintf(temporary + directory, capacity - directory, L".xxwidgets-%lu-%lu-%u.tmp",
                     (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount(), attempt);
            file = _wfopen(temporary, L"wbx");
        }
    } else
        file = _wfopen(wide, overwrite ? L"wb" : L"wbx");
#else
    char *temporary = NULL;
    if (atomic_save) {
        const char *slash = strrchr(path, '/');
        static const char suffix[] = ".xxwidgets-XXXXXX";
        size_t directory = slash ? (size_t)(slash - path) + 1 : 0;
        int descriptor;
        if (directory > SIZE_MAX - sizeof(suffix))
            return XXWIDGETS_OUT_OF_MEMORY;
        temporary = malloc(directory + sizeof(suffix));
        if (!temporary)
            return XXWIDGETS_OUT_OF_MEMORY;
        memcpy(temporary, path, directory);
        memcpy(temporary + directory, suffix, sizeof(suffix));
        descriptor = mkstemp(temporary);
        if (descriptor >= 0) {
            file = fdopen(descriptor, "wb");
            if (!file) {
                close(descriptor);
                (void)remove(temporary);
            }
        }
    } else
        file = fopen(path, overwrite ? "wb" : "wbx");
#endif
    if (!file) {
#ifdef _WIN32
        free(wide);
#endif
        free(temporary);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    ok = 1;
    if (w->hex_source_read) {
        unsigned char buffer[16384];
        while (length) {
            size_t count = length < sizeof(buffer) ? (size_t)length : sizeof(buffer);
            status = w->hex_source_read(w->hex_source_user, start, buffer, count);
            if (status != XXWIDGETS_OK || fwrite(buffer, 1, count, file) != count) {
                ok = 0;
                break;
            }
            start += count;
            length -= count;
        }
    } else
        ok = fwrite(w->hex_data + w->hex_selection, 1, w->hex_selection_length, file) == w->hex_selection_length;
    if (fclose(file))
        ok = 0;
    if (atomic_save) {
#ifdef _WIN32
        if (ok && !MoveFileExW(temporary, wide, MOVEFILE_REPLACE_EXISTING))
            ok = 0;
        if (!ok)
            (void)DeleteFileW(temporary);
#else
        if (ok && rename(temporary, path))
            ok = 0;
        if (!ok)
            (void)remove(temporary);
#endif
    } else if (!ok && w->hex_source_read && !overwrite) {
#ifdef _WIN32
        (void)DeleteFileW(wide);
#else
        (void)remove(path);
#endif
    }
#ifdef _WIN32
    free(wide);
#endif
    free(temporary);
    return ok ? XXWIDGETS_OK : status != XXWIDGETS_OK ? status : XXWIDGETS_PLATFORM_ERROR;
}
xxwidgets_status xxwidgets_hexview_save_selection(xxwidgets_widget *w, const char *path) { return save_hex_selection(w, path, 1); }
xxwidgets_status xxwidgets_hexview_body_action(xxwidgets_widget *w, size_t index)
{
    xxwidgets_status status;
    size_t start, length;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || index >= 12)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (xxwidgets_hexview_menu_item(w, 3, index).disabled)
        return XXWIDGETS_OK;
    if (!index) {
        if (w->hex_dump_handler) return w->hex_dump_handler(w, w->hex_dump_user);
        w->hex_pending_dump = 1;
        return XXWIDGETS_OK;
    }
    if (index >= 8)
        return xxwidgets_hexview_copy_selection(w, (xxwidgets_hex_copy_format)(index - 8));
    if (w->hex_source_read && index >= 1 && index <= 5) {
        uint64_t absolute = index == 1 || index == 5 ? 0 : index == 2 ? w->hex_source_size - 1
                            : index == 3 ? w->hex_source_selection : w->hex_source_selection + w->hex_source_length - 1;
        uint64_t selection = w->hex_source_selection, selected_length = w->hex_source_length;
        uint64_t anchor = w->hex_source_anchor, anchor_bytes = w->hex_source_anchor_bytes;
        xxwidgets_hex_span target;
        status = source_seek(w, absolute);
        if (status != XXWIDGETS_OK)
            return status;
        start = (size_t)(absolute - w->hex_base);
        target.offset = start;
        target.bytes = 1;
        if (w->hex_header == 2 && !symbol_at(w, start, &target))
            return XXWIDGETS_INVALID_ARGUMENT;
        if (index <= 2)
            return xxwidgets_hexview_pointer_span(w, target.offset, target.bytes, 0);
        if (index == 5) {
            anchor = 0;
            anchor_bytes = target.bytes;
            selection = 0;
            selected_length = w->hex_source_size;
        }
        status = select_source_range(w, anchor, anchor_bytes, target.offset, selection, selected_length);
        if (status == XXWIDGETS_OK)
            xxwidgets_emit(w, XXWIDGETS_EVENT_SELECT, w->value);
        return status;
    }
    if (index <= 4) {
        start = index == 1 ? 0 : index == 2 ? w->hex_size - 1 : index == 3 ? w->hex_selection : w->hex_selection + w->hex_selection_length - 1;
        if (index < 3)
            return xxwidgets_hexview_pointer(w, start, 0);
        status = select_bytes(w, w->hex_anchor, w->hex_anchor_bytes, start, w->hex_selection, w->hex_selection_length);
        if (status == XXWIDGETS_OK)
            xxwidgets_emit(w, XXWIDGETS_EVENT_SELECT, w->value);
        return status;
    }
    start = index == 5 ? 0 : w->hex_cursor / w->hex_columns * w->hex_columns;
    length = index == 5 ? w->hex_size : index == 7 ? 0 : w->hex_size - start;
    if (index == 6 && length > w->hex_columns)
        length = w->hex_columns;
    if (index == 5 && w->hex_header == 2) {
        xxwidgets_hex_span first;
        if (!symbol_at(w, 0, &first))
            return XXWIDGETS_INVALID_ARGUMENT;
        status = select_bytes(w, first.offset, first.bytes, first.offset, start, length);
    } else
        status = xxwidgets_hexview_set_selection(w, start, length);
    if (status == XXWIDGETS_OK)
        xxwidgets_emit(w, XXWIDGETS_EVENT_SELECT, w->value);
    return status;
}
typedef struct hex_dump_form {
    xxwidgets_widget *window, *save, *cancel;
    int done, accepted;
} hex_dump_form;
static void hex_dump_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    hex_dump_form *form = user;
    (void)app;
    if (event->widget == form->window && event->type == XXWIDGETS_EVENT_CLOSE)
        form->done = 1;
    if (event->type == XXWIDGETS_EVENT_CLICK && (event->widget == form->save || event->widget == form->cancel)) {
        form->accepted = event->widget == form->save;
        form->done = 1;
    }
}
/* A path field keeps export available in terminals without a native chooser.
 * Existing files require the explicit Replace checkbox; exclusive create avoids
 * a check/open race when replacement was not requested. */
static xxwidgets_status hex_dump_path(xxwidgets_widget *w, char **path, int *accepted, int *overwrite)
{
    xxwidgets_app *app = w->app;
    xxwidgets_widget *owner = w->parent, *edit = NULL, *replace = NULL, *label = NULL;
    hex_dump_form form = {0};
    xxwidgets_event_fn callback = app->on_event;
    void *user = app->user_data;
    int enabled = owner->enabled;
    xxwidgets_status status, cleanup;
    *path = NULL;
    *accepted = *overwrite = 0;
    app->on_event = hex_dump_event;
    app->user_data = &form;
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, "Dump selected bytes", (xxwidgets_rect){2, 2, 64, 8}, &form.window);
    if (status != XXWIDGETS_OK)
        goto finish;
    app->modal_window = form.window;
    status = xxwidgets_widget_create(app, form.window, XXWIDGETS_LABEL, "Output file:", (xxwidgets_rect){2, 1, 60, 1}, &label);
    if (status != XXWIDGETS_OK)
        goto finish;
    status = xxwidgets_widget_create(app, form.window, XXWIDGETS_EDIT, "", (xxwidgets_rect){2, 2, 60, 1}, &edit);
    if (status != XXWIDGETS_OK)
        goto finish;
    status = xxwidgets_widget_create(app, form.window, XXWIDGETS_CHECKBOX, "Replace existing file", (xxwidgets_rect){2, 4, 60, 1}, &replace);
    if (status != XXWIDGETS_OK)
        goto finish;
    status = xxwidgets_widget_create(app, form.window, XXWIDGETS_BUTTON, "Save", (xxwidgets_rect){38, 6, 10, 1}, &form.save);
    if (status != XXWIDGETS_OK)
        goto finish;
    status = xxwidgets_widget_create(app, form.window, XXWIDGETS_BUTTON, "Cancel", (xxwidgets_rect){50, 6, 10, 1}, &form.cancel);
    if (status != XXWIDGETS_OK)
        goto finish;
    app->modal_default = form.save;
    status = xxwidgets_widget_set_enabled(owner, 0);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_focus(edit);
    while (status == XXWIDGETS_OK && !form.done && !app->quit)
        status = xxwidgets_app_poll(app, 30);
    if (status == XXWIDGETS_OK && form.accepted && !app->quit) {
        size_t required = 0;
        status = xxwidgets_widget_get_text(edit, NULL, 0, &required);
        if (status == XXWIDGETS_OK) {
            *path = malloc(required);
            if (!*path)
                status = XXWIDGETS_OUT_OF_MEMORY;
            else
                status = xxwidgets_widget_get_text(edit, *path, required, &required);
        }
        if (status == XXWIDGETS_OK)
            status = xxwidgets_widget_get_value(replace, overwrite);
        if (status == XXWIDGETS_OK)
            *accepted = 1;
    }
finish:
    cleanup = xxwidgets_widget_set_enabled(owner, enabled);
    if (status == XXWIDGETS_OK)
        status = cleanup;
    if (form.window) {
        cleanup = xxwidgets_widget_destroy(form.window);
        if (status == XXWIDGETS_OK)
            status = cleanup;
    }
    app->modal_window = app->modal_default = NULL;
    app->on_event = callback;
    app->user_data = user;
    (void)xxwidgets_widget_focus(w);
    return status;
}
/* File choosers run after event dispatch, so applications can use app_run. */
void xxwidgets_hexview_deferred(xxwidgets_app *app)
{
    for (xxwidgets_widget *w = app->widgets; w && !app->quit; w = w->next) {
        if (w->kind != XXWIDGETS_HEXVIEW || !w->hex_pending_dump)
            continue;
        char *path = NULL;
        int accepted = 0, overwrite = 1;
        xxwidgets_status status;
        w->hex_pending_dump = 0;
        if (xxwidgets_file_dialog_available(app))
            status = xxwidgets_file_dialog(w->parent, XXWIDGETS_FILE_DIALOG_SAVE, "Dump selected bytes", "selection.bin", &path, &accepted);
        else
            status = hex_dump_path(w, &path, &accepted, &overwrite);
        if (status == XXWIDGETS_OK && accepted)
            status = save_hex_selection(w, path, overwrite);
        free(path);
        if (status != XXWIDGETS_OK)
            (void)xxwidgets_text_dialog(
                w->parent, "Dump selected bytes",
                "Could not save selected bytes. Check the path and write permissions; enable replacement to overwrite an existing file.");
    }
}
