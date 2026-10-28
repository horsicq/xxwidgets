/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* Per-column filters of tables. The model keeps every row; the backends see
 * the rows the filters let through ("shown" rows) and map them back through
 * xxwidgets_tabular_model_row, so values and events keep model indexes. */
#include "xxwidgets_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

int xxwidgets_table_filterable(const xxwidgets_widget *widget)
{
    return widget && (xxwidgets_binaryviewer_kind(widget) || widget->kind == XXWIDGETS_MEMORY_MAP || widget->kind == XXWIDGETS_TABLE ||
        widget->kind == XXWIDGETS_DATA_STRUCT_TABLE || widget->kind == XXWIDGETS_ENTROPY_REGIONS ||
        widget->kind == XXWIDGETS_SCANRESULTS || widget->kind == XXWIDGET_SYSTEMPROCESSESVIEWER);
}

static int filters_active(const xxwidgets_widget *widget)
{
    size_t i;
    if (widget && widget->table_external_filters) return 0;
    if (!xxwidgets_table_filterable(widget)) return 0;
    for (i = 0; i < widget->table_filter_count; ++i)
        if (widget->table_filters[i] && widget->table_filters[i][0]) return 1;
    return 0;
}

/* Case-insensitive for ASCII; other bytes must match exactly. */
static int contains(const char *text, const char *needle)
{
    size_t length = strlen(needle), i;
    for (; *text; ++text) {
        for (i = 0; i < length && text[i]; ++i) {
            unsigned char a = (unsigned char)text[i], b = (unsigned char)needle[i];
            if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + 32);
            if (a != b) break;
        }
        if (i == length) return 1;
    }
    return !length;
}

static uint64_t rows_key(const xxwidgets_widget *widget)
{
    /* The row revision without the filters' share, and the row count. */
    return (xxwidgets_row_revision(widget) - widget->table_filter_revision) * 1000003u + widget->item_count;
}

/* Rebuilds the shown rows when the rows or the filters changed. A failed
 * allocation shows every row rather than none. */
static void ensure_rows(const xxwidgets_widget *constant)
{
    xxwidgets_widget *widget = (xxwidgets_widget *)constant;
    size_t row, column, shown = 0, columns;
    uint64_t key = rows_key(widget);
    if (widget->table_rows_valid && widget->table_rows_key == key && widget->table_rows_filter == widget->table_filter_revision) return;
    free(widget->table_rows);
    widget->table_rows = NULL;
    widget->table_row_count = 0;
    widget->table_rows_valid = 1;
    widget->table_rows_key = key;
    widget->table_rows_filter = widget->table_filter_revision;
    if (!filters_active(widget) || !widget->item_count) return;
    widget->table_rows = (size_t *)malloc(widget->item_count * sizeof(*widget->table_rows));
    if (!widget->table_rows) { widget->table_rows_valid = 0; return; }
    columns = xxwidgets_tabular_column_count(widget);
    for (row = 0; row < widget->item_count; ++row) {
        int keep = 1;
        for (column = 0; keep && column < widget->table_filter_count && column < columns; ++column) {
            const char *filter = widget->table_filters[column];
            if (filter && filter[0]) keep = contains(xxwidgets_tabular_model_cell(widget, row, column), filter);
        }
        if (keep) widget->table_rows[shown++] = row;
    }
    widget->table_row_count = shown;
}

size_t xxwidgets_tabular_rows(const xxwidgets_widget *widget)
{
    if (!widget) return 0;
    if (!filters_active(widget)) return widget->item_count;
    ensure_rows(widget);
    return widget->table_rows ? widget->table_row_count : widget->item_count;
}

size_t xxwidgets_tabular_model_row(const xxwidgets_widget *widget, size_t row)
{
    if (!filters_active(widget)) return row;
    ensure_rows(widget);
    if (!widget->table_rows) return row;
    return row < widget->table_row_count ? widget->table_rows[row] : SIZE_MAX;
}

size_t xxwidgets_tabular_display_row(const xxwidgets_widget *widget, size_t model)
{
    size_t low = 0, high;
    if (!filters_active(widget)) return model < widget->item_count ? model : SIZE_MAX;
    ensure_rows(widget);
    if (!widget->table_rows) return model < widget->item_count ? model : SIZE_MAX;
    high = widget->table_row_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (widget->table_rows[middle] < model) low = middle + 1;
        else high = middle;
    }
    return low < widget->table_row_count && widget->table_rows[low] == model ? low : SIZE_MAX;
}

const char *xxwidgets_tabular_cell(const xxwidgets_widget *widget, size_t row, size_t column)
{
    size_t model = xxwidgets_tabular_model_row(widget, row);
    return model == SIZE_MAX ? "" : xxwidgets_tabular_model_cell(widget, model, column);
}

const char *xxwidgets_tabular_row_text(const xxwidgets_widget *widget, size_t row)
{
    size_t model = xxwidgets_tabular_model_row(widget, row);
    return widget && widget->items && model < widget->item_count && widget->items[model] ? widget->items[model] : "";
}

int xxwidgets_tabular_value(const xxwidgets_widget *widget, int row)
{
    size_t model;
    if (row < 0) return -1;
    model = xxwidgets_tabular_model_row(widget, (size_t)row);
    return model == SIZE_MAX || model > INT_MAX ? -1 : (int)model;
}

int xxwidgets_tabular_row(const xxwidgets_widget *widget, int value)
{
    size_t row;
    if (value < 0) return -1;
    row = xxwidgets_tabular_display_row(widget, (size_t)value);
    return row == SIZE_MAX || row > INT_MAX ? -1 : (int)row;
}

void xxwidgets_table_filter_dispose(xxwidgets_widget *widget)
{
    size_t i;
    for (i = 0; i < widget->table_filter_count; ++i) free(widget->table_filters[i]);
    free(widget->table_filters);
    free(widget->table_rows);
    widget->table_filters = NULL;
    widget->table_rows = NULL;
    widget->table_filter_count = widget->table_row_count = 0;
    widget->table_rows_valid = 0;
}

static xxwidgets_status store_filter(xxwidgets_widget *widget, size_t column, const char *text, int *changed)
{
    char *copy;
    *changed = 0;
    if (!xxwidgets_table_filterable(widget) || !text || !xxwidgets_valid_utf8(text) || column >= xxwidgets_tabular_column_count(widget))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (column < widget->table_filter_count && widget->table_filters[column] ? !strcmp(widget->table_filters[column], text) : !text[0])
        return XXWIDGETS_OK;
    if (column >= widget->table_filter_count) {
        char **grown = (char **)realloc(widget->table_filters, (column + 1) * sizeof(*grown));
        if (!grown) return XXWIDGETS_OUT_OF_MEMORY;
        memset(grown + widget->table_filter_count, 0, (column + 1 - widget->table_filter_count) * sizeof(*grown));
        widget->table_filters = grown;
        widget->table_filter_count = column + 1;
    }
    copy = xxwidgets_strdup(text);
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    free(widget->table_filters[column]);
    widget->table_filters[column] = copy;
    ++widget->table_filter_revision;
    widget->table_rows_valid = 0;
    *changed = 1;
    return XXWIDGETS_OK;
}

/* The shown rows changed: a selection they hide is dropped, then the native
 * view is rebuilt. Returns whether the value changed. */
static xxwidgets_status refilter(xxwidgets_widget *widget, int *selection_changed)
{
    xxwidgets_status status;
    *selection_changed = 0;
    if (widget->value >= 0 && xxwidgets_tabular_row(widget, widget->value) < 0) {
        widget->value = -1;
        *selection_changed = 1;
    }
    ++widget->app->syncing;
    status = widget->app->ops->sync(widget);
    --widget->app->syncing;
    return status;
}

xxwidgets_status xxwidgets_table_set_filter(xxwidgets_widget *table, size_t column, const char *text)
{
    int changed, selection;
    xxwidgets_status status = store_filter(table, column, text, &changed);
    return status == XXWIDGETS_OK && changed ? refilter(table, &selection) : status;
}

xxwidgets_status xxwidgets_table_filter_input(xxwidgets_widget *widget, size_t column, const char *text)
{
    int changed, selection = 0;
    xxwidgets_status status = store_filter(widget, column, text, &changed);
    if (status == XXWIDGETS_OK && changed) status = refilter(widget, &selection);
    if (status == XXWIDGETS_OK && selection) xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, -1);
    if (status == XXWIDGETS_OK && changed) xxwidgets_emit(widget, XXWIDGETS_EVENT_CHANGE, (int)column);
    return status;
}

const char *xxwidgets_table_filter(const xxwidgets_widget *table, size_t column)
{
    return xxwidgets_table_filterable(table) && column < table->table_filter_count && table->table_filters[column]
        ? table->table_filters[column] : "";
}

xxwidgets_status xxwidgets_table_clear_filters(xxwidgets_widget *table)
{
    int selection;
    size_t i;
    if (!xxwidgets_table_filterable(table)) return XXWIDGETS_INVALID_ARGUMENT;
    if (!filters_active(table) && !table->table_external_filters) return XXWIDGETS_OK;
    for (i = 0; i < table->table_filter_count; ++i) { free(table->table_filters[i]); table->table_filters[i] = NULL; }
    ++table->table_filter_revision;
    table->table_rows_valid = 0;
    return refilter(table, &selection);
}

size_t xxwidgets_table_visible_count(const xxwidgets_widget *table)
{
    return xxwidgets_table_filterable(table) ? xxwidgets_tabular_rows(table) : 0;
}
