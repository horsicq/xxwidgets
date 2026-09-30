#include "xxwidgets_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static const size_t minimum_widths[4] = {12, 24, 12, 4};
static const size_t maximum_widths[3] = {24, 48, 24};

static int scan_widget(const xxwidgets_widget *widget)
{
    return widget && widget->kind == XXWIDGETS_SCANRESULTS;
}

static const char *stored_field(const xxwidgets_scan_result *result, size_t column)
{
    return column == 0 ? result->type : column == 1 ? result->name :
        column == 2 ? result->version : result->info;
}

static const char *result_field(const xxwidgets_scan_result *result, size_t column)
{
    const char *text = stored_field(result, column);
    return text ? text : "";
}

static void set_field(xxwidgets_scan_result *result, size_t column, const char *text)
{
    if (column == 0) result->type = text;
    else if (column == 1) result->name = text;
    else if (column == 2) result->version = text;
    else result->info = text;
}

static void free_results(xxwidgets_scan_result *results, char **cells, size_t count)
{
    size_t row, column;
    for (row = 0; row < count; ++row)
        for (column = 0; column < 4; ++column) {
            if (results) free((void *)stored_field(&results[row], column));
            if (cells) free(cells[row * 4 + column]);
        }
    free(results); free(cells);
}

static void free_rows(char **rows, size_t count)
{
    size_t row;
    if (rows) for (row = 0; row < count; ++row) free(rows[row]);
    free(rows);
}

void xxwidgets_scanresults_dispose(xxwidgets_widget *widget)
{
    /* Generic destruction releases widget->items separately. */
    free_results(widget->scan_results, widget->scan_cells, widget->item_count);
    widget->scan_results = NULL; widget->scan_cells = NULL;
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
        if (added == 4) width += 4;
        else if ((*source & 0xc0) != 0x80) ++width;
        ++source;
    }
    copy = (char *)malloc(length + 1);
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    source = (const unsigned char *)text;
    while (*source) {
        if (*source < 32 || *source == 127) {
            copy[position++] = '\\'; copy[position++] = 'x';
            copy[position++] = digits[*source >> 4]; copy[position++] = digits[*source & 15];
        } else copy[position++] = (char)*source;
        ++source;
    }
    copy[position] = 0;
    *out = copy; *columns = width;
    return XXWIDGETS_OK;
}

static size_t utf8_columns(const char *text)
{
    const unsigned char *source = (const unsigned char *)text;
    size_t columns = 0;
    while (*source) if ((*source++ & 0xc0) != 0x80) ++columns;
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

static xxwidgets_status format_row(char **cells, const size_t widths[4], char **out,
    size_t *out_columns)
{
    size_t column, length = 6, columns = 6, position = 0;
    char *row;
    for (column = 0; column < 4; ++column) {
        size_t bytes = strlen(cells[column]);
        if (bytes > SIZE_MAX - length - 1 || widths[column] > SIZE_MAX - length - bytes - 1)
            return XXWIDGETS_INVALID_ARGUMENT;
        length += bytes + widths[column];
    }
    row = (char *)malloc(length + 1);
    if (!row) return XXWIDGETS_OUT_OF_MEMORY;
    for (column = 0; column < 4; ++column) {
        const char *cell = cells[column];
        size_t width = utf8_columns(cell), bytes = strlen(cell), displayed = width;
        int clipped = column < 3 && width > widths[column];
        if (clipped) { displayed = widths[column]; bytes = prefix_bytes(cell, displayed - 3); }
        memcpy(row + position, cell, bytes); position += bytes;
        if (clipped) { memcpy(row + position, "...", 3); position += 3; }
        if (column < 3) {
            size_t padding = widths[column] > displayed ? widths[column] - displayed : 0;
            memset(row + position, ' ', padding + 2); position += padding + 2;
            columns += widths[column];
        } else columns += displayed;
    }
    row[position] = 0; *out = row; *out_columns = columns;
    return XXWIDGETS_OK;
}

static xxwidgets_status sync_scan(xxwidgets_widget *widget)
{
    xxwidgets_status status;
    ++widget->app->syncing;
    status = widget->app->ops->sync(widget);
    --widget->app->syncing;
    return status;
}

xxwidgets_status xxwidgets_scanresults_set_results(xxwidgets_widget *widget,
    const xxwidgets_scan_result *results, size_t count)
{
    xxwidgets_scan_result *copies = NULL, *previous_results;
    char **cells = NULL, **rows = NULL, **previous_cells, **previous_rows;
    size_t row, column, widths[4], previous_widths[4], columns = 0;
    size_t previous_count, previous_columns;
    uint64_t previous_revision;
    int previous_value;
    xxwidgets_status status = XXWIDGETS_OK;
    if (!scan_widget(widget) || (!results && count) || count > INT_MAX ||
        count > SIZE_MAX / sizeof(*copies) || count > SIZE_MAX / (4 * sizeof(*cells)))
        return XXWIDGETS_INVALID_ARGUMENT;
    for (row = 0; row < count; ++row)
        for (column = 0; column < 4; ++column)
            if (!xxwidgets_valid_utf8(result_field(&results[row], column))) return XXWIDGETS_INVALID_ARGUMENT;
    memcpy(widths, minimum_widths, sizeof(widths));
    if (count) {
        copies = (xxwidgets_scan_result *)calloc(count, sizeof(*copies));
        cells = (char **)calloc(count * 4, sizeof(*cells));
        rows = (char **)calloc(count, sizeof(*rows));
        if (!copies || !cells || !rows) { status = XXWIDGETS_OUT_OF_MEMORY; goto failed; }
    }
    for (row = 0; row < count; ++row)
        for (column = 0; column < 4; ++column) {
            char *copy = xxwidgets_strdup(result_field(&results[row], column));
            size_t width;
            if (!copy) { status = XXWIDGETS_OUT_OF_MEMORY; goto failed; }
            set_field(&copies[row], column, copy);
            status = escape_cell(copy, &cells[row * 4 + column], &width);
            if (status != XXWIDGETS_OK) goto failed;
            if (column < 3 && width > maximum_widths[column]) width = maximum_widths[column];
            if (width > widths[column]) widths[column] = width;
        }
    for (row = 0; row < count; ++row) {
        size_t row_columns;
        status = format_row(&cells[row * 4], widths, &rows[row], &row_columns);
        if (status != XXWIDGETS_OK) goto failed;
        if (row_columns > columns) columns = row_columns;
    }
    previous_results = widget->scan_results; previous_cells = widget->scan_cells;
    previous_rows = widget->items; previous_count = widget->item_count;
    previous_columns = widget->archive_columns; previous_value = widget->value;
    previous_revision = widget->scan_revision;
    memcpy(previous_widths, widget->scan_column_width, sizeof(previous_widths));
    widget->scan_results = copies; widget->scan_cells = cells;
    widget->items = rows; widget->item_count = count; widget->archive_columns = columns;
    widget->scan_revision = previous_revision + 1; widget->value = count ? 0 : -1;
    memcpy(widget->scan_column_width, widths, sizeof(widths));
    status = sync_scan(widget);
    if (status != XXWIDGETS_OK) {
        widget->scan_results = previous_results; widget->scan_cells = previous_cells;
        widget->items = previous_rows; widget->item_count = previous_count;
        widget->archive_columns = previous_columns; widget->value = previous_value;
        widget->scan_revision = previous_revision;
        memcpy(widget->scan_column_width, previous_widths, sizeof(previous_widths));
        sync_scan(widget);
        goto failed;
    }
    free_results(previous_results, previous_cells, previous_count);
    free_rows(previous_rows, previous_count);
    return XXWIDGETS_OK;
failed:
    free_results(copies, cells, count); free_rows(rows, count);
    return status;
}

xxwidgets_status xxwidgets_scanresults_clear(xxwidgets_widget *widget)
{
    return xxwidgets_scanresults_set_results(widget, NULL, 0);
}

size_t xxwidgets_scanresults_count(const xxwidgets_widget *widget)
{
    return scan_widget(widget) ? widget->item_count : 0;
}

xxwidgets_status xxwidgets_scanresults_get_result(const xxwidgets_widget *widget,
    size_t index, xxwidgets_scan_result *result)
{
    if (!scan_widget(widget) || !result || index >= widget->item_count) return XXWIDGETS_INVALID_ARGUMENT;
    *result = widget->scan_results[index];
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_scanresults_get_selection(xxwidgets_widget *widget,
    size_t *index, xxwidgets_scan_result *result)
{
    int value;
    xxwidgets_status status;
    if (!scan_widget(widget) || !index || !result) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_widget_get_value(widget, &value);
    if (status != XXWIDGETS_OK) return status;
    if (value < 0 || (size_t)value >= widget->item_count) {
        *index = SIZE_MAX; memset(result, 0, sizeof(*result));
    } else { *index = (size_t)value; *result = widget->scan_results[value]; }
    return XXWIDGETS_OK;
}

const char *xxwidgets_scanresults_cell(const xxwidgets_widget *widget, size_t row, size_t column)
{
    return scan_widget(widget) && row < widget->item_count && column < 4 ?
        widget->scan_cells[row * 4 + column] : "";
}

size_t xxwidgets_scanresults_column_width(const xxwidgets_widget *widget, size_t column)
{
    return scan_widget(widget) && column < 4 ? widget->scan_column_width[column] : 0;
}

static void report_append(char *buffer, size_t capacity, size_t *position, int *full, const char *text)
{
    size_t length = strlen(text), copied;
    if (*full || !capacity) return;
    copied = length < capacity - *position ? length : capacity - *position - 1;
    while (copied && (((unsigned char)text[copied] & 0xc0) == 0x80)) --copied;
    memcpy(buffer + *position, text, copied); *position += copied;
    buffer[*position] = 0;
    if (copied < length) *full = 1;
}

xxwidgets_status xxwidgets_scanresults_get_report(const xxwidgets_widget *widget,
    char *buffer, size_t capacity, size_t *required)
{
    static const char header[] = "Type\tName\tVersion\tInfo\n";
    size_t row, column, length = sizeof(header), position = 0;
    int full = 0;
    if (!scan_widget(widget) || (!buffer && capacity)) return XXWIDGETS_INVALID_ARGUMENT;
    for (row = 0; row < widget->item_count; ++row)
        for (column = 0; column < 4; ++column) {
            size_t added = strlen(widget->scan_cells[row * 4 + column]);
            if (added >= SIZE_MAX - length) return XXWIDGETS_INVALID_ARGUMENT;
            length += added + 1;
        }
    if (required) *required = length;
    if (!buffer && !capacity) return XXWIDGETS_OK;
    if (capacity) buffer[0] = 0;
    report_append(buffer, capacity, &position, &full, header);
    for (row = 0; row < widget->item_count; ++row)
        for (column = 0; column < 4; ++column) {
            report_append(buffer, capacity, &position, &full, widget->scan_cells[row * 4 + column]);
            report_append(buffer, capacity, &position, &full, column == 3 ? "\n" : "\t");
        }
    return capacity >= length ? XXWIDGETS_OK : XXWIDGETS_BUFFER_TOO_SMALL;
}

xxwidgets_status xxwidgets_scanresults_copy(xxwidgets_widget *widget)
{
    size_t required;
    char *report;
    xxwidgets_status status;
    if (!scan_widget(widget)) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->app->backend == XXWIDGETS_BACKEND_TUI || !widget->app->ops->copy_text)
        return XXWIDGETS_UNAVAILABLE;
    status = xxwidgets_scanresults_get_report(widget, NULL, 0, &required);
    if (status != XXWIDGETS_OK) return status;
    report = (char *)malloc(required);
    if (!report) return XXWIDGETS_OUT_OF_MEMORY;
    status = xxwidgets_scanresults_get_report(widget, report, required, NULL);
    if (status == XXWIDGETS_OK) status = widget->app->ops->copy_text(widget->parent, report);
    free(report); return status;
}
