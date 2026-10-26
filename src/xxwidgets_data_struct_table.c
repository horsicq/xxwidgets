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
#include "xxwidgets/xxwidgets_data_struct_table.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DATA_STRUCT_COLUMNS 9

static const size_t minimum_widths[DATA_STRUCT_COLUMNS] = {12, 10, 15, 12, 8, 10, 12, 12, 7};
static const size_t maximum_widths[DATA_STRUCT_COLUMNS] = {28, 20, 18, 18, 12, 24, 32, 32, 0};

static int table_widget(const xxwidgets_widget *widget)
{
    return widget && widget->kind == XXWIDGETS_DATA_STRUCT_TABLE;
}

static const char *field_string(const xxwidgets_data_struct_field *field, size_t column)
{
    const char *value = column == 0 ? field->name : column == 1 ? field->type : column == 6 ? field->raw : column == 7 ? field->value : field->comment;
    return value ? value : "";
}

static void set_field_string(xxwidgets_data_struct_field *field, size_t column, const char *value)
{
    if (column == 0) field->name = value;
    else if (column == 1) field->type = value;
    else if (column == 6) field->raw = value;
    else if (column == 7) field->value = value;
    else field->comment = value;
}

static void free_fields(xxwidgets_data_struct_field *fields, size_t count)
{
    size_t row;
    if (fields)
        for (row = 0; row < count; ++row) {
            free((void *)fields[row].name);
            free((void *)fields[row].type);
            free((void *)fields[row].raw);
            free((void *)fields[row].value);
            free((void *)fields[row].comment);
        }
    free(fields);
}

static void free_cells(char **cells, size_t count)
{
    size_t i;
    if (cells)
        for (i = 0; i < count * DATA_STRUCT_COLUMNS; ++i) free(cells[i]);
    free(cells);
}

static void free_rows(char **rows, size_t count)
{
    size_t row;
    if (rows)
        for (row = 0; row < count; ++row) free(rows[row]);
    free(rows);
}

void xxwidgets_data_struct_table_init(xxwidgets_widget *widget)
{
    size_t column;
    for (column = 0; column < DATA_STRUCT_COLUMNS; ++column) widget->data_struct_column_width[column] = minimum_widths[column];
}

void xxwidgets_data_struct_table_dispose(xxwidgets_widget *widget)
{
    if (!widget) return;
    free_fields(widget->data_struct_fields, widget->item_count);
    free_cells(widget->data_struct_cells, widget->item_count);
    widget->data_struct_fields = NULL;
    widget->data_struct_cells = NULL;
}

static xxwidgets_status escape_cell(const char *text, char **out, size_t *columns)
{
    static const char digits[] = "0123456789ABCDEF";
    const unsigned char *source = (const unsigned char *)text;
    size_t length = 0, width = 0, position = 0;
    char *copy;
    while (*source) {
        size_t added = *source < 32 || *source == 127 ? 4 : 1;
        if (added >= SIZE_MAX - length) return XXWIDGETS_INVALID_ARGUMENT;
        length += added;
        width += added == 4 ? 4 : ((*source & 0xc0) != 0x80 ? 1 : 0);
        ++source;
    }
    copy = (char *)malloc(length + 1);
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    source = (const unsigned char *)text;
    while (*source) {
        if (*source < 32 || *source == 127) {
            copy[position++] = '\\';
            copy[position++] = 'x';
            copy[position++] = digits[*source >> 4];
            copy[position++] = digits[*source & 15];
        } else copy[position++] = (char)*source;
        ++source;
    }
    copy[position] = 0;
    *out = copy;
    *columns = width;
    return XXWIDGETS_OK;
}

static xxwidgets_status number_cell(int64_t value, char **out, size_t *columns)
{
    char buffer[32];
    if (value < 0) snprintf(buffer, sizeof(buffer), "-");
    else snprintf(buffer, sizeof(buffer), "0x%" PRIX64, (uint64_t)value);
    *out = xxwidgets_strdup(buffer);
    if (!*out) return XXWIDGETS_OUT_OF_MEMORY;
    *columns = strlen(buffer);
    return XXWIDGETS_OK;
}

typedef struct property_name {
    xx_data_struct_record_property_t flag;
    const char *name;
} property_name;

static xxwidgets_status property_cell(xx_data_struct_record_property_t property, char **out, size_t *columns)
{
    static const property_name names[] = {{XX_DATA_STRUCT_RECORD_PROPERTY_SIGNATURE, "signature"},
    {XX_DATA_STRUCT_RECORD_PROPERTY_CHECKSUM, "checksum"},
                                          {XX_DATA_STRUCT_RECORD_PROPERTY_ID, "id"},
                                          {XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS, "address"},
                                          {XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET, "offset"},
                                          {XX_DATA_STRUCT_RECORD_PROPERTY_SIZE, "size"},
                                          {XX_DATA_STRUCT_RECORD_PROPERTY_COUNT, "count"},
                                          {XX_DATA_STRUCT_RECORD_PROPERTY_POINTER, "pointer"},
                                          {XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS, "flags"},
                                          {XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP, "timestamp"},
                                          {XX_DATA_STRUCT_RECORD_PROPERTY_STRING, "string"},
                                          {XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED, "reserved"}};
    char buffer[160];
    xx_data_struct_record_property_t known = 0;
    size_t i, used = 0;
    if (!property) snprintf(buffer, sizeof(buffer), "data");
    else {
        buffer[0] = 0;
        for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
            size_t length;
            if (!(property & names[i].flag)) continue;
            known |= names[i].flag;
            length = strlen(names[i].name);
            if (used && used + 1 < sizeof(buffer)) buffer[used++] = ',';
            if (length > sizeof(buffer) - used - 1) length = sizeof(buffer) - used - 1;
            memcpy(buffer + used, names[i].name, length);
            used += length;
            buffer[used] = 0;
        }
        if (property & ~known) {
            int written;
            if (used && used + 1 < sizeof(buffer)) buffer[used++] = ',';
            written = snprintf(buffer + used, sizeof(buffer) - used, "0x%08" PRIX32, property & ~known);
            if (written > 0) used += (size_t)written < sizeof(buffer) - used ? (size_t)written : sizeof(buffer) - used - 1;
        }
        if (!used) snprintf(buffer, sizeof(buffer), "data");
    }
    *out = xxwidgets_strdup(buffer);
    if (!*out) return XXWIDGETS_OUT_OF_MEMORY;
    *columns = strlen(buffer);
    return XXWIDGETS_OK;
}

static size_t utf8_columns(const char *text)
{
    const unsigned char *source = (const unsigned char *)text;
    size_t columns = 0;
    while (*source)
        if ((*source++ & 0xc0) != 0x80) ++columns;
    return columns;
}

static size_t prefix_bytes(const char *text, size_t columns)
{
    const unsigned char *source = (const unsigned char *)text;
    while (*source && columns) {
        ++source;
        while ((*source & 0xc0) == 0x80) ++source;
        --columns;
    }
    return (size_t)(source - (const unsigned char *)text);
}

static xxwidgets_status format_row(char **cells, const size_t widths[DATA_STRUCT_COLUMNS], char **out, size_t *out_columns)
{
    size_t column, length = (DATA_STRUCT_COLUMNS - 1) * 3, columns = (DATA_STRUCT_COLUMNS - 1) * 3, position = 0;
    char *row;
    for (column = 0; column < DATA_STRUCT_COLUMNS; ++column) {
        size_t bytes = strlen(cells[column]);
        if (bytes > SIZE_MAX - length - widths[column] - 1) return XXWIDGETS_INVALID_ARGUMENT;
        length += bytes + widths[column];
    }
    row = (char *)malloc(length + 1);
    if (!row) return XXWIDGETS_OUT_OF_MEMORY;
    for (column = 0; column < DATA_STRUCT_COLUMNS; ++column) {
        const char *cell = cells[column];
        size_t width = utf8_columns(cell), bytes = strlen(cell), displayed = width;
        int clipped = column + 1 < DATA_STRUCT_COLUMNS && width > widths[column];
        if (clipped) {
            displayed = widths[column];
            bytes = prefix_bytes(cell, displayed > 3 ? displayed - 3 : 0);
        }
        memcpy(row + position, cell, bytes);
        position += bytes;
        if (clipped && displayed >= 3) {
            memcpy(row + position, "...", 3);
            position += 3;
        }
        if (column + 1 < DATA_STRUCT_COLUMNS) {
            size_t padding = widths[column] > displayed ? widths[column] - displayed : 0;
            memset(row + position, ' ', padding);
            position += padding;
            memcpy(row + position, " | ", 3);
            position += 3;
            columns += widths[column];
        } else columns += displayed;
    }
    row[position] = 0;
    *out = row;
    *out_columns = columns;
    return XXWIDGETS_OK;
}

static xxwidgets_status sync_table(xxwidgets_widget *widget)
{
    xxwidgets_status status;
    ++widget->app->syncing;
    status = widget->app->ops->sync(widget);
    --widget->app->syncing;
    return status;
}

xxwidgets_status xxwidgets_data_struct_table_set_fields(xxwidgets_widget *widget, const xxwidgets_data_struct_field *fields, size_t count)
{
    xxwidgets_data_struct_field *copies = NULL, *previous_fields;
    char **cells = NULL, **rows = NULL, **previous_cells, **previous_rows;
    size_t row, column, widths[DATA_STRUCT_COLUMNS], previous_widths[DATA_STRUCT_COLUMNS], columns = 0;
    size_t previous_count, previous_columns;
    uint64_t previous_revision;
    int previous_value;
    xxwidgets_status status = XXWIDGETS_OK;
    if (!table_widget(widget) || (!fields && count) || count > INT_MAX || count > SIZE_MAX / sizeof(*copies) ||
        count > SIZE_MAX / (DATA_STRUCT_COLUMNS * sizeof(*cells)))
        return XXWIDGETS_INVALID_ARGUMENT;
    for (row = 0; row < count; ++row) {
        if (fields[row].relative_offset < -1 || fields[row].file_offset < -1 || fields[row].size < -1) return XXWIDGETS_INVALID_ARGUMENT;
        for (column = 0; column < DATA_STRUCT_COLUMNS; ++column)
            if ((column == 0 || column == 1 || column >= 6) && !xxwidgets_valid_utf8(field_string(fields + row, column)))
                return XXWIDGETS_INVALID_ARGUMENT;
    }
    memcpy(widths, minimum_widths, sizeof(widths));
    if (count) {
        copies = (xxwidgets_data_struct_field *)calloc(count, sizeof(*copies));
        cells = (char **)calloc(count * DATA_STRUCT_COLUMNS, sizeof(*cells));
        rows = (char **)calloc(count, sizeof(*rows));
        if (!copies || !cells || !rows) {
            status = XXWIDGETS_OUT_OF_MEMORY;
            goto failed;
        }
    }
    for (row = 0; row < count; ++row) {
        copies[row].relative_offset = fields[row].relative_offset;
        copies[row].file_offset = fields[row].file_offset;
        copies[row].size = fields[row].size;
        copies[row].property = fields[row].property;
        for (column = 0; column < DATA_STRUCT_COLUMNS; ++column)
            if (column == 0 || column == 1 || column >= 6) {
                char *copy = xxwidgets_strdup(field_string(fields + row, column));
                if (!copy) {
                    status = XXWIDGETS_OUT_OF_MEMORY;
                    goto failed;
                }
                set_field_string(copies + row, column, copy);
            }
        for (column = 0; column < DATA_STRUCT_COLUMNS; ++column) {
            size_t width = 0;
            if (column == 0 || column == 1 || column >= 6)
                status = escape_cell(field_string(copies + row, column), &cells[row * DATA_STRUCT_COLUMNS + column], &width);
            else if (column >= 2 && column <= 4)
                status = number_cell(column == 2 ? copies[row].relative_offset : column == 3 ? copies[row].file_offset : copies[row].size,
                                     &cells[row * DATA_STRUCT_COLUMNS + column], &width);
            else status = property_cell(copies[row].property, &cells[row * DATA_STRUCT_COLUMNS + column], &width);
            if (status != XXWIDGETS_OK) goto failed;
            if (maximum_widths[column] && width > maximum_widths[column]) width = maximum_widths[column];
            if (width > widths[column]) widths[column] = width;
        }
    }
    for (row = 0; row < count; ++row) {
        size_t row_columns;
        status = format_row(&cells[row * DATA_STRUCT_COLUMNS], widths, &rows[row], &row_columns);
        if (status != XXWIDGETS_OK) goto failed;
        if (row_columns > columns) columns = row_columns;
    }
    previous_fields = widget->data_struct_fields;
    previous_cells = widget->data_struct_cells;
    previous_rows = widget->items;
    previous_count = widget->item_count;
    previous_columns = widget->archive_columns;
    previous_value = widget->value;
    previous_revision = widget->data_struct_revision;
    memcpy(previous_widths, widget->data_struct_column_width, sizeof(previous_widths));
    widget->data_struct_fields = copies;
    widget->data_struct_cells = cells;
    widget->items = rows;
    widget->item_count = count;
    widget->archive_columns = columns;
    widget->data_struct_revision = previous_revision + 1;
    widget->value = count ? 0 : -1;
    memcpy(widget->data_struct_column_width, widths, sizeof(widths));
    status = sync_table(widget);
    if (status != XXWIDGETS_OK) {
        widget->data_struct_fields = previous_fields;
        widget->data_struct_cells = previous_cells;
        widget->items = previous_rows;
        widget->item_count = previous_count;
        widget->archive_columns = previous_columns;
        widget->value = previous_value;
        widget->data_struct_revision = previous_revision;
        memcpy(widget->data_struct_column_width, previous_widths, sizeof(previous_widths));
        sync_table(widget);
        goto failed;
    }
    free_fields(previous_fields, previous_count);
    free_cells(previous_cells, previous_count);
    free_rows(previous_rows, previous_count);
    return XXWIDGETS_OK;
failed:
    free_fields(copies, count);
    free_cells(cells, count);
    free_rows(rows, count);
    return status;
}

xxwidgets_status xxwidgets_data_struct_table_clear(xxwidgets_widget *widget)
{
    return xxwidgets_data_struct_table_set_fields(widget, NULL, 0);
}

size_t xxwidgets_data_struct_table_count(const xxwidgets_widget *widget)
{
    return table_widget(widget) ? widget->item_count : 0;
}

xxwidgets_status xxwidgets_data_struct_table_get_field(const xxwidgets_widget *widget, size_t index, xxwidgets_data_struct_field *field)
{
    if (!table_widget(widget) || !field || index >= widget->item_count) return XXWIDGETS_INVALID_ARGUMENT;
    *field = widget->data_struct_fields[index];
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_data_struct_table_get_selection(xxwidgets_widget *widget, size_t *index, xxwidgets_data_struct_field *field)
{
    int value;
    xxwidgets_status status;
    if (!table_widget(widget) || !index || !field) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_widget_get_value(widget, &value);
    if (status != XXWIDGETS_OK) return status;
    if (value < 0 || (size_t)value >= widget->item_count) {
        *index = SIZE_MAX;
        memset(field, 0, sizeof(*field));
    } else {
        *index = (size_t)value;
        *field = widget->data_struct_fields[value];
    }
    return XXWIDGETS_OK;
}

const char *xxwidgets_data_struct_table_cell(const xxwidgets_widget *widget, size_t row, size_t column)
{
    return table_widget(widget) && row < widget->item_count && column < DATA_STRUCT_COLUMNS ? widget->data_struct_cells[row * DATA_STRUCT_COLUMNS + column] : "";
}

size_t xxwidgets_data_struct_table_column_width(const xxwidgets_widget *widget, size_t column)
{
    return table_widget(widget) && column < DATA_STRUCT_COLUMNS ? widget->data_struct_column_width[column] : 0;
}

size_t xxwidgets_tabular_column_count(const xxwidgets_widget *widget)
{
    if (xxwidgets_debugger_table_kind(widget)) return xxwidgets_debugger_table_column_count(widget);
    if (xxwidgets_binaryviewer_kind(widget)) return xxwidgets_binaryviewer_column_count(widget);
    if (widget && widget->kind == XXWIDGETS_HEXVIEW) return 3;
    if (widget && widget->kind == XXWIDGET_SYSTEMPROCESSESVIEWER) return 2;
    return xxwidgets_entropy_table(widget) ? (widget->kind == XXWIDGETS_ENTROPY_BYTES ? 3 : 5) : widget && widget->kind == XXWIDGETS_SCANRESULTS ? 4 : table_widget(widget) ? DATA_STRUCT_COLUMNS : 0;
}

const char *xxwidgets_tabular_column_title(const xxwidgets_widget *widget, size_t column)
{
    if (xxwidgets_debugger_table_kind(widget)) return xxwidgets_debugger_table_column_title(widget, column);
    if (xxwidgets_binaryviewer_kind(widget)) return xxwidgets_binaryviewer_column_title(widget, column);
    static const char *scan_titles[4] = {"Type", "Name", "Version", "Info"};
    static const char *data_titles[DATA_STRUCT_COLUMNS] = {"Name", "Type", "Relative offset", "File offset", "Size", "Role", "Raw", "Value", "Comment"};
    size_t count = xxwidgets_tabular_column_count(widget);
    static const char *regions[] = {"Offset", "Size", "Entropy", "Status", "Name"};
    static const char *bytes[] = {"Byte", "Count", "%"};
    if (column >= count) return "";
    if (widget->kind == XXWIDGET_SYSTEMPROCESSESVIEWER) return column == 0 ? "Name" : "PID";
    if (widget->kind == XXWIDGETS_HEXVIEW) return xxwidgets_hexview_title(widget, column);
    if (xxwidgets_entropy_table(widget)) return widget->kind == XXWIDGETS_ENTROPY_BYTES ? bytes[column] : regions[column];
    return widget->kind == XXWIDGETS_SCANRESULTS ? scan_titles[column] : data_titles[column];
}

const char *xxwidgets_tabular_cell(const xxwidgets_widget *widget, size_t row, size_t column)
{
    if (xxwidgets_debugger_table_kind(widget)) return xxwidgets_debugger_table_cell(widget, row, column);
    if (xxwidgets_binaryviewer_kind(widget)) return xxwidgets_binaryviewer_cell(widget, row, column);
    if (widget && widget->kind == XXWIDGETS_HEXVIEW) return xxwidgets_hexview_cell(widget, row, column);
    if (xxwidgets_entropy_table(widget)) return xxwidgets_entropy_cell(widget, row, column);
    return widget && (widget->kind == XXWIDGETS_SCANRESULTS || widget->kind == XXWIDGET_SYSTEMPROCESSESVIEWER) ? xxwidgets_scanresults_cell(widget, row, column)
                                                            : xxwidgets_data_struct_table_cell(widget, row, column);
}

size_t xxwidgets_tabular_column_width(const xxwidgets_widget *widget, size_t column)
{
    if (xxwidgets_debugger_table_kind(widget)) return xxwidgets_debugger_table_column_width(widget, column);
    if (xxwidgets_binaryviewer_kind(widget)) return xxwidgets_binaryviewer_column_width(widget, column);
    if (widget && widget->kind == XXWIDGETS_HEXVIEW) return xxwidgets_hexview_column_width(widget, column);
    if (xxwidgets_entropy_table(widget)) {
        static const size_t widths[] = {12, 12, 12, 14, 28};
        return column < xxwidgets_tabular_column_count(widget) ? widths[column] : 0;
    }
    return widget && (widget->kind == XXWIDGETS_SCANRESULTS || widget->kind == XXWIDGET_SYSTEMPROCESSESVIEWER) ? xxwidgets_scanresults_column_width(widget, column)
                                                            : xxwidgets_data_struct_table_column_width(widget, column);
}
