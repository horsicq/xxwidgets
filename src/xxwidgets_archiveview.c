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

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static xxwidgets_status sync_archiveview(xxwidgets_widget *widget)
{
    xxwidgets_status status;
    ++widget->app->syncing;
    status = widget->app->ops->sync(widget);
    --widget->app->syncing;
    return status;
}

static void free_rows(char **rows, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) free(rows[i]);
    free(rows);
}

static void free_archive_entries(xxwidgets_archive_entry *entries, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) free((void *)entries[i].path);
    free(entries);
}

void xxwidgets_archiveview_dispose(xxwidgets_widget *widget)
{
    if (!widget->archive_entries) return;
    free_archive_entries(widget->archive_entries, widget->item_count);
    widget->archive_entries = NULL;
}

static xxwidgets_status archive_row(const xxwidgets_archive_entry *entry, char **out_row, size_t *out_columns)
{
    static const char digits[] = "0123456789ABCDEF";
    const unsigned char *path = (const unsigned char *)entry->path;
    size_t length = 28, columns = 28, pos;
    char *line;
    while (*path) {
        size_t added = (*path < 32 || *path == 127) ? 4 : 1;
        if (added > SIZE_MAX - length - 1) return XXWIDGETS_INVALID_ARGUMENT;
        length += added;
        if (added == 4) columns += 4;
        else if ((*path & 0xc0) != 0x80) ++columns;
        ++path;
    }
    line = (char *)malloc(length + 1);
    if (!line) return XXWIDGETS_OUT_OF_MEMORY;
    if (entry->is_directory) snprintf(line, 29, "<DIR> %20s  ", "");
    else snprintf(line, 29, "      %20" PRIu64 "  ", entry->size);
    pos = 28;
    path = (const unsigned char *)entry->path;
    while (*path) {
        if (*path < 32 || *path == 127) {
            line[pos++] = '\\';
            line[pos++] = 'x';
            line[pos++] = digits[*path >> 4];
            line[pos++] = digits[*path & 15];
        } else line[pos++] = (char)*path;
        ++path;
    }
    line[pos] = 0;
    *out_row = line;
    *out_columns = columns;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_archiveview_set_entries(xxwidgets_widget *widget, const xxwidgets_archive_entry *entries, size_t count)
{
    xxwidgets_archive_entry *copies = NULL, *previous_entries;
    char **rows = NULL, **previous_rows;
    size_t i, previous_count, previous_columns, columns = 0;
    uint64_t previous_revision;
    int previous_value;
    xxwidgets_status status;
    if (!widget || widget->kind != XXWIDGETS_ARCHIVEVIEW || (!entries && count) || count > INT_MAX || count > SIZE_MAX / sizeof(*copies) ||
        count > SIZE_MAX / sizeof(*rows))
        return XXWIDGETS_INVALID_ARGUMENT;
    /* Validate the complete input before allocating or touching widget state. */
    for (i = 0; i < count; ++i)
        if (!xxwidgets_valid_utf8(entries[i].path) || !entries[i].path[0] || (entries[i].is_directory != 0 && entries[i].is_directory != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    if (count) {
        copies = (xxwidgets_archive_entry *)calloc(count, sizeof(*copies));
        rows = (char **)calloc(count, sizeof(*rows));
        if (!copies || !rows) {
            free(copies);
            free(rows);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
    }
    for (i = 0; i < count; ++i) {
        size_t row_columns;
        copies[i] = entries[i];
        copies[i].path = xxwidgets_strdup(entries[i].path);
        if (!copies[i].path) {
            free_archive_entries(copies, i);
            free_rows(rows, i);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        status = archive_row(&copies[i], &rows[i], &row_columns);
        if (status != XXWIDGETS_OK) {
            free_archive_entries(copies, i + 1);
            free_rows(rows, i);
            return status;
        }
        if (row_columns > columns) columns = row_columns;
    }
    previous_entries = widget->archive_entries;
    previous_rows = widget->items;
    previous_count = widget->item_count;
    previous_value = widget->value;
    previous_revision = widget->archive_revision;
    previous_columns = widget->archive_columns;
    widget->archive_entries = copies;
    widget->items = rows;
    widget->item_count = count;
    widget->archive_columns = columns;
    widget->archive_revision = previous_revision + 1;
    widget->value = count ? 0 : -1;
    status = sync_archiveview(widget);
    if (status != XXWIDGETS_OK) {
        widget->archive_entries = previous_entries;
        widget->items = previous_rows;
        widget->item_count = previous_count;
        widget->value = previous_value;
        widget->archive_revision = previous_revision;
        widget->archive_columns = previous_columns;
        sync_archiveview(widget);
        free_archive_entries(copies, count);
        free_rows(rows, count);
    } else {
        free_archive_entries(previous_entries, previous_count);
        free_rows(previous_rows, previous_count);
    }
    return status;
}

xxwidgets_status xxwidgets_archiveview_clear(xxwidgets_widget *widget)
{
    return xxwidgets_archiveview_set_entries(widget, NULL, 0);
}

size_t xxwidgets_archiveview_count(const xxwidgets_widget *widget)
{
    return widget && widget->kind == XXWIDGETS_ARCHIVEVIEW ? widget->item_count : 0;
}

xxwidgets_status xxwidgets_archiveview_get_entry(const xxwidgets_widget *widget, size_t index, xxwidgets_archive_entry *entry)
{
    if (!widget || widget->kind != XXWIDGETS_ARCHIVEVIEW || !entry || index >= widget->item_count) return XXWIDGETS_INVALID_ARGUMENT;
    *entry = widget->archive_entries[index];
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_archiveview_get_selection(xxwidgets_widget *widget, size_t *index, xxwidgets_archive_entry *entry)
{
    int value;
    xxwidgets_status status;
    if (!widget || widget->kind != XXWIDGETS_ARCHIVEVIEW || !index || !entry) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_widget_get_value(widget, &value);
    if (status != XXWIDGETS_OK) return status;
    if (value < 0 || (size_t)value >= widget->item_count) {
        *index = SIZE_MAX;
        entry->path = NULL;
        entry->size = 0;
        entry->is_directory = 0;
    } else {
        *index = (size_t)value;
        *entry = widget->archive_entries[value];
    }
    return XXWIDGETS_OK;
}
