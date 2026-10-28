/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* XXWIDGETS_TABLE: rows of text cells under the column titles given at
 * creation. The backends draw it like the debugger tables (fixed-pitch cells,
 * columns that grow to their content) and every column has a filter. */
#include "xxwidgets_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct xxwidgets_text_table_state {
    size_t columns, rows, widths[XXWIDGETS_TABLE_MAX_COLUMNS];
    char *titles[XXWIDGETS_TABLE_MAX_COLUMNS];
    char **cells;   /* rows * columns, as given. */
    char **display; /* The same with control characters escaped; NULL when equal. */
};

/* Cells wider than this are clipped with "..." in the rows of the TUI and in
 * the starting column widths. */
#define CELL_MAXIMUM 96

static size_t text_columns(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    size_t count = 0;
    while (*p) if ((*p++ & 0xc0) != 0x80) ++count;
    return count;
}

static size_t prefix_bytes(const char *text, size_t columns)
{
    const unsigned char *p = (const unsigned char *)text;
    while (*p && columns) {
        ++p;
        while ((*p & 0xc0) == 0x80) ++p;
        --columns;
    }
    return (size_t)(p - (const unsigned char *)text);
}

/* NULL when the text has no control characters to escape. */
static xxwidgets_status escaped(const char *text, char **out)
{
    static const char digits[] = "0123456789ABCDEF";
    const unsigned char *p = (const unsigned char *)text;
    size_t length = 0, position = 0, controls = 0;
    char *copy;
    *out = NULL;
    for (; *p; ++p, ++length) if (*p < 32 || *p == 127) ++controls;
    if (!controls) return XXWIDGETS_OK;
    if (length > (SIZE_MAX - 1) / 4) return XXWIDGETS_INVALID_ARGUMENT;
    copy = (char *)malloc(length + 3 * controls + 1);
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    for (p = (const unsigned char *)text; *p; ++p) {
        if (*p < 32 || *p == 127) {
            copy[position++] = '\\';
            copy[position++] = 'x';
            copy[position++] = digits[*p >> 4];
            copy[position++] = digits[*p & 15];
        } else copy[position++] = (char)*p;
    }
    copy[position] = 0;
    *out = copy;
    return XXWIDGETS_OK;
}

static void free_cells(char **cells, size_t count)
{
    if (cells) for (size_t i = 0; i < count; ++i) free(cells[i]);
    free(cells);
}

static void dispose_rows(char **rows, size_t count)
{
    if (rows) for (size_t i = 0; i < count; ++i) free(rows[i]);
    free(rows);
}

static const char *shown_cell(const struct xxwidgets_text_table_state *state, size_t index)
{
    return state->display && state->display[index] ? state->display[index] : state->cells[index];
}

static int table_widget(const xxwidgets_widget *widget)
{
    return widget && widget->kind == XXWIDGETS_TABLE && widget->text_table;
}

xxwidgets_status xxwidgets_text_table_init(xxwidgets_widget *widget)
{
    struct xxwidgets_text_table_state *state = calloc(1, sizeof(*state));
    const char *title = widget->text;
    xxwidgets_status status = XXWIDGETS_OK;
    if (!state) return XXWIDGETS_OUT_OF_MEMORY;
    /* The creation text holds the titles, separated by tabs. */
    for (;;) {
        const char *end = strchr(title, '\t');
        size_t length = end ? (size_t)(end - title) : strlen(title);
        if (state->columns == XXWIDGETS_TABLE_MAX_COLUMNS) { status = XXWIDGETS_INVALID_ARGUMENT; break; }
        state->titles[state->columns] = (char *)malloc(length + 1);
        if (!state->titles[state->columns]) { status = XXWIDGETS_OUT_OF_MEMORY; break; }
        memcpy(state->titles[state->columns], title, length);
        state->titles[state->columns][length] = 0;
        state->widths[state->columns] = text_columns(state->titles[state->columns]);
        if (state->widths[state->columns] < 4) state->widths[state->columns] = 4;
        ++state->columns;
        if (!end) break;
        title = end + 1;
    }
    if (status == XXWIDGETS_OK) {
        char *empty = xxwidgets_strdup("");
        if (!empty) status = XXWIDGETS_OUT_OF_MEMORY;
        else { free(widget->text); widget->text = empty; }
    }
    if (status != XXWIDGETS_OK) {
        for (size_t i = 0; i < state->columns; ++i) free(state->titles[i]);
        free(state);
        return status;
    }
    widget->text_table = state;
    return XXWIDGETS_OK;
}

void xxwidgets_text_table_dispose(xxwidgets_widget *widget)
{
    struct xxwidgets_text_table_state *state = widget->text_table;
    if (!state) return;
    for (size_t i = 0; i < state->columns; ++i) free(state->titles[i]);
    free_cells(state->cells, state->rows * state->columns);
    free_cells(state->display, state->rows * state->columns);
    free(state);
    widget->text_table = NULL;
}

static xxwidgets_status format_row(const struct xxwidgets_text_table_state *state, const size_t *widths, size_t row, char **out)
{
    size_t length = 0, position = 0;
    char *line;
    for (size_t column = 0; column < state->columns; ++column) {
        size_t bytes = strlen(shown_cell(state, row * state->columns + column));
        if (bytes > SIZE_MAX / 2 - widths[column] - 3 || length > SIZE_MAX / 2 - bytes - widths[column] - 3)
            return XXWIDGETS_INVALID_ARGUMENT;
        length += bytes + widths[column] + 2;
    }
    line = (char *)malloc(length + 1);
    if (!line) return XXWIDGETS_OUT_OF_MEMORY;
    for (size_t column = 0; column < state->columns; ++column) {
        const char *cell = shown_cell(state, row * state->columns + column);
        size_t width = text_columns(cell), clipped = width > widths[column];
        size_t shown = clipped ? widths[column] : width;
        size_t bytes = clipped ? prefix_bytes(cell, shown > 3 ? shown - 3 : shown) : strlen(cell);
        memcpy(line + position, cell, bytes);
        position += bytes;
        if (clipped && shown > 3) { memcpy(line + position, "...", 3); position += 3; }
        if (column + 1 < state->columns) {
            size_t padding = widths[column] - shown + 2;
            memset(line + position, ' ', padding);
            position += padding;
        }
    }
    line[position] = 0;
    *out = line;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_table_create(xxwidgets_widget *owner, xxwidgets_rect bounds, const char *const *titles, size_t columns,
                                       xxwidgets_widget **out)
{
    size_t length = 0, position = 0;
    char *text;
    xxwidgets_status status;
    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || !titles || !columns || columns > XXWIDGETS_TABLE_MAX_COLUMNS) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < columns; ++i) {
        if (!titles[i] || strchr(titles[i], '\t') || !xxwidgets_valid_utf8(titles[i])) return XXWIDGETS_INVALID_ARGUMENT;
        length += strlen(titles[i]) + 1;
    }
    text = (char *)malloc(length);
    if (!text) return XXWIDGETS_OUT_OF_MEMORY;
    for (size_t i = 0; i < columns; ++i) {
        size_t bytes = strlen(titles[i]);
        memcpy(text + position, titles[i], bytes);
        position += bytes;
        text[position++] = i + 1 < columns ? '\t' : 0;
    }
    status = xxwidgets_widget_create(owner->app, owner, XXWIDGETS_TABLE, text, bounds, out);
    free(text);
    return status;
}

xxwidgets_status xxwidgets_text_table_replace(xxwidgets_widget *widget, const char *const *cells, size_t rows, int selection)
{
    struct xxwidgets_text_table_state *state;
    char **copies = NULL, **display = NULL, **lines = NULL, **previous_lines;
    size_t widths[XXWIDGETS_TABLE_MAX_COLUMNS], columns, count, line_columns = 0, previous_count, previous_line_columns;
    size_t previous_widths[XXWIDGETS_TABLE_MAX_COLUMNS], previous_rows;
    char **previous_cells, **previous_display;
    uint64_t revision;
    int value, displayed = 0;
    xxwidgets_status status = XXWIDGETS_OK;
    if (!table_widget(widget) || (!cells && rows) || rows > INT_MAX) return XXWIDGETS_INVALID_ARGUMENT;
    state = widget->text_table;
    columns = state->columns;
    if (rows > SIZE_MAX / columns / sizeof(char *)) return XXWIDGETS_INVALID_ARGUMENT;
    count = rows * columns;
    for (size_t i = 0; i < count; ++i)
        if (cells[i] && !xxwidgets_valid_utf8(cells[i])) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t column = 0; column < columns; ++column) {
        widths[column] = text_columns(state->titles[column]);
        if (widths[column] < 4) widths[column] = 4;
    }
    if (rows) {
        copies = (char **)calloc(count, sizeof(char *));
        display = (char **)calloc(count, sizeof(char *));
        lines = (char **)calloc(rows, sizeof(char *));
        if (!copies || !display || !lines) { status = XXWIDGETS_OUT_OF_MEMORY; goto failed; }
    }
    for (size_t i = 0; i < count; ++i) {
        size_t width;
        copies[i] = xxwidgets_strdup(cells[i] ? cells[i] : "");
        if (!copies[i]) { status = XXWIDGETS_OUT_OF_MEMORY; goto failed; }
        status = escaped(copies[i], &display[i]);
        if (status != XXWIDGETS_OK) goto failed;
        if (display[i]) displayed = 1;
        width = text_columns(display[i] ? display[i] : copies[i]);
        if (width > CELL_MAXIMUM) width = CELL_MAXIMUM;
        if (width > widths[i % columns]) widths[i % columns] = width;
    }
    if (!displayed) { free(display); display = NULL; }
    for (size_t column = 0; column < columns; ++column) line_columns += widths[column] + (column + 1 < columns ? 2 : 0);
    /* The rows below read these cells through the state. */
    previous_cells = state->cells; previous_display = state->display; previous_rows = state->rows;
    memcpy(previous_widths, state->widths, sizeof(previous_widths));
    state->cells = copies; state->display = display; state->rows = rows;
    memcpy(state->widths, widths, sizeof(widths));
    for (size_t row = 0; row < rows && status == XXWIDGETS_OK; ++row) status = format_row(state, widths, row, &lines[row]);
    if (status == XXWIDGETS_OK) {
        previous_lines = widget->items; previous_count = widget->item_count; previous_line_columns = widget->archive_columns;
        revision = widget->debugger_revision; value = widget->value;
        widget->items = lines; widget->item_count = rows; widget->archive_columns = line_columns;
        widget->debugger_revision = revision + 1;
        /* The row asked for when the filters show it, else the first one shown. */
        if (selection >= 0 && (size_t)selection < rows && xxwidgets_tabular_row(widget, selection) >= 0) widget->value = selection;
        else widget->value = xxwidgets_tabular_rows(widget) ? xxwidgets_tabular_value(widget, 0) : -1;
        ++widget->app->syncing;
        status = widget->app->ops->sync(widget);
        --widget->app->syncing;
        if (status == XXWIDGETS_OK) {
            free_cells(previous_cells, previous_rows * columns);
            free_cells(previous_display, previous_rows * columns);
            dispose_rows(previous_lines, previous_count);
            return XXWIDGETS_OK;
        }
        widget->items = previous_lines; widget->item_count = previous_count; widget->archive_columns = previous_line_columns;
        widget->debugger_revision = revision; widget->value = value;
    }
    state->cells = previous_cells; state->display = previous_display; state->rows = previous_rows;
    memcpy(state->widths, previous_widths, sizeof(previous_widths));
    ++widget->app->syncing;
    (void)widget->app->ops->sync(widget);
    --widget->app->syncing;
failed:
    free_cells(copies, count);
    free_cells(display, count);
    dispose_rows(lines, rows);
    return status;
}

xxwidgets_status xxwidgets_table_set_rows(xxwidgets_widget *widget, const char *const *cells, size_t rows)
{
    return xxwidgets_text_table_replace(widget, cells, rows, 0);
}

size_t xxwidgets_table_row_count(const xxwidgets_widget *widget)
{
    return table_widget(widget) ? widget->text_table->rows : 0;
}

size_t xxwidgets_table_column_count(const xxwidgets_widget *widget)
{
    return table_widget(widget) ? widget->text_table->columns : 0;
}

const char *xxwidgets_table_cell(const xxwidgets_widget *widget, size_t row, size_t column)
{
    const struct xxwidgets_text_table_state *state = table_widget(widget) ? widget->text_table : NULL;
    return state && row < state->rows && column < state->columns ? state->cells[row * state->columns + column] : "";
}

/* Backend accessors: titles, escaped cells and widths. */
const char *xxwidgets_text_table_title(const xxwidgets_widget *widget, size_t column)
{
    const struct xxwidgets_text_table_state *state = table_widget(widget) ? widget->text_table : NULL;
    return state && column < state->columns ? state->titles[column] : "";
}

const char *xxwidgets_text_table_cell(const xxwidgets_widget *widget, size_t row, size_t column)
{
    const struct xxwidgets_text_table_state *state = table_widget(widget) ? widget->text_table : NULL;
    return state && row < state->rows && column < state->columns ? shown_cell(state, row * state->columns + column) : "";
}

size_t xxwidgets_text_table_column_width(const xxwidgets_widget *widget, size_t column)
{
    const struct xxwidgets_text_table_state *state = table_widget(widget) ? widget->text_table : NULL;
    return state && column < state->columns ? state->widths[column] : 0;
}
