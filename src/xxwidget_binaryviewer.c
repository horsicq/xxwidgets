/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidget_binaryviewer.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct binary_descriptor {
    size_t record_size, string_count, offsets[3], columns;
    const char *titles[5];
    size_t widths[5];
} binary_descriptor;

struct xxwidgets_binaryviewer_state {
    const binary_descriptor *descriptor;
    void *records;
    char **cells;
    size_t count, widths[5];
};

static const binary_descriptor *descriptor(xxwidgets_kind kind)
{
    static const binary_descriptor resources = {sizeof(xx_resource_record), 3,
        {offsetof(xx_resource_record, type_name), offsetof(xx_resource_record, name), offsetof(xx_resource_record, language_name)},
        5, {XXW_N_("Type"), XXW_N_("Name"), XXW_N_("Language"), XXW_N_("Size"), XXW_N_("Offset")}, {16, 24, 12, 12, 18}};
    static const binary_descriptor imports = {sizeof(xx_import_record), 2,
        {offsetof(xx_import_record, library_name), offsetof(xx_import_record, name), 0},
        5, {XXW_N_("Library"), XXW_N_("Name"), XXW_N_("Ordinal"), XXW_N_("Address"), XXW_N_("Kind")}, {24, 32, 12, 18, 16}};
    static const binary_descriptor exports = {sizeof(xx_export_record), 2,
        {offsetof(xx_export_record, name), offsetof(xx_export_record, forwarder), 0},
        4, {XXW_N_("Name"), XXW_N_("Ordinal"), XXW_N_("Address"), XXW_N_("Forwarder"), NULL}, {32, 12, 18, 32, 0}};
    static const binary_descriptor symbols = {sizeof(xx_symbol_record), 2,
        {offsetof(xx_symbol_record, name), offsetof(xx_symbol_record, file_name), 0},
        5, {XXW_N_("Name"), XXW_N_("Value"), XXW_N_("Size"), XXW_N_("Type"), XXW_N_("Binding")}, {32, 18, 12, 12, 12}};
    switch (kind) {
        case XXWIDGET_RESOURCESVIEWER: return &resources;
        case XXWIDGET_IMPORTSVIEWER: return &imports;
        case XXWIDGET_EXPORTSVIEWER: return &exports;
        case XXWIDGET_SYMBOLSVIEWER: return &symbols;
        default: return NULL;
    }
}

static char *record_string(const void *record, size_t offset)
{
    char *string; memcpy(&string, (const unsigned char *)record + offset, sizeof(string)); return string;
}

static void store_string(void *record, size_t offset, char *string)
{ memcpy((unsigned char *)record + offset, &string, sizeof(string)); }

static void dispose_state(struct xxwidgets_binaryviewer_state *state)
{
    size_t row, column;
    if (!state) return;
    if (state->records) for (row = 0; row < state->count; ++row)
        for (column = 0; column < state->descriptor->string_count; ++column)
            free(record_string((unsigned char *)state->records + row * state->descriptor->record_size, state->descriptor->offsets[column]));
    if (state->cells) for (row = 0; row < state->count * state->descriptor->columns; ++row) free(state->cells[row]);
    free(state->records); free(state->cells); free(state);
}

void xxwidgets_binaryviewer_dispose(xxwidgets_widget *widget)
{
    dispose_state(widget->binaryviewer); widget->binaryviewer = NULL;
}

static void dispose_rows(char **rows, size_t count)
{
    size_t i; if (rows) for (i = 0; i < count; ++i) free(rows[i]); free(rows);
}

static size_t text_columns(const char *string)
{
    const unsigned char *p = (const unsigned char *)string; size_t count = 0;
    while (*p) if ((*p++ & 0xc0) != 0x80) ++count;
    return count;
}

static size_t prefix_bytes(const char *string, size_t columns)
{
    const unsigned char *p = (const unsigned char *)string;
    while (*p && columns) { ++p; while ((*p & 0xc0) == 0x80) ++p; --columns; }
    return (size_t)(p - (const unsigned char *)string);
}

static xxwidgets_status escaped_cell(const char *text, char **out)
{
    static const char digits[] = "0123456789ABCDEF";
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    size_t length = 0, position = 0; char *copy;
    for (const unsigned char *s = p; *s; ++s) {
        size_t extra = *s < 32 || *s == 127 ? 4 : 1;
        if (length > SIZE_MAX - extra - 1) return XXWIDGETS_INVALID_ARGUMENT;
        length += extra;
    }
    copy = (char *)malloc(length + 1);
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    for (; *p; ++p) {
        if (*p < 32 || *p == 127) { copy[position++] = '\\'; copy[position++] = 'x'; copy[position++] = digits[*p >> 4]; copy[position++] = digits[*p & 15]; }
        else copy[position++] = (char)*p;
    }
    copy[position] = 0; *out = copy; return XXWIDGETS_OK;
}

static const char *symbol_type(xx_symbol_kind_t type)
{
    switch (type) {
        case XX_SYMBOL_KIND_FUNCTION: return "function";
        case XX_SYMBOL_KIND_DATA: return "data";
        case XX_SYMBOL_KIND_SECTION: return "section";
        case XX_SYMBOL_KIND_FILE: return "file";
        case XX_SYMBOL_KIND_LABEL: return "label";
        default: return "unknown";
    }
}

static const char *symbol_binding(xx_symbol_binding_t binding)
{
    switch (binding) {
        case XX_SYMBOL_BINDING_LOCAL: return "local";
        case XX_SYMBOL_BINDING_GLOBAL: return "global";
        case XX_SYMBOL_BINDING_WEAK: return "weak";
        default: return "unknown";
    }
}

static const char *identifier(const char *name, uint32_t id, char buffer[64])
{
    if (name && *name) return name;
    if (id == UINT32_MAX) return "-";
    snprintf(buffer, 64, "%" PRIu32, id); return buffer;
}

static const char *hex_value(uint64_t value, char buffer[64])
{
    if (value == UINT64_MAX) return "-";
    snprintf(buffer, 64, "0x%" PRIX64, value); return buffer;
}

static const char *cell_text(xxwidgets_kind kind, const void *record, size_t column, char buffer[64])
{
    if (kind == XXWIDGET_RESOURCESVIEWER) {
        const xx_resource_record *r = (const xx_resource_record *)record;
        if (column == 0) return identifier(r->type_name, r->type_id, buffer);
        if (column == 1) return identifier(r->name, r->name_id, buffer);
        if (column == 2) return identifier(r->language_name, r->language_id, buffer);
        if (column == 3) { if (r->size < 0) return "-"; snprintf(buffer, 64, "%" PRId64, r->size); return buffer; }
        return r->offset < 0 ? "-" : hex_value((uint64_t)r->offset, buffer);
    }
    if (kind == XXWIDGET_IMPORTSVIEWER) {
        const xx_import_record *r = (const xx_import_record *)record;
        if (column == 0) return r->library_name;
        if (column == 1) return r->name;
        if (column == 2) { if (!r->by_ordinal) return "-"; snprintf(buffer, 64, "%" PRIu64, r->ordinal); return buffer; }
        if (column == 3) return hex_value(r->address, buffer);
        return r->is_delay ? (r->by_ordinal ? "delay ordinal" : "delay name") : (r->by_ordinal ? "ordinal" : "name");
    }
    if (kind == XXWIDGET_EXPORTSVIEWER) {
        const xx_export_record *r = (const xx_export_record *)record;
        if (column == 0) return r->name;
        if (column == 1) { if (r->ordinal == UINT64_MAX) return "-"; snprintf(buffer, 64, "%" PRIu64, r->ordinal); return buffer; }
        if (column == 2) return hex_value(r->address, buffer);
        return r->forwarder;
    }
    {
        const xx_symbol_record *r = (const xx_symbol_record *)record;
        if (column == 0) return r->name;
        if (column == 1) { snprintf(buffer, 64, "0x%" PRIX64, r->value); return buffer; }
        if (column == 2) { snprintf(buffer, 64, "%" PRIu64, r->size); return buffer; }
        if (column == 3) return symbol_type(r->kind);
        return symbol_binding(r->binding);
    }
}

static xxwidgets_status format_row(const struct xxwidgets_binaryviewer_state *state, size_t index, char **out)
{
    size_t column, length = 0, position = 0, count = state->descriptor->columns;
    char *line;
    for (column = 0; column < count; ++column) {
        size_t bytes = strlen(state->cells[index * count + column]);
        if (bytes > SIZE_MAX - state->widths[column] - 3 || length > SIZE_MAX - bytes - state->widths[column] - 3)
            return XXWIDGETS_INVALID_ARGUMENT;
        length += bytes + state->widths[column] + 2;
    }
    line = (char *)malloc(length + 1);
    if (!line) return XXWIDGETS_OUT_OF_MEMORY;
    for (column = 0; column < count; ++column) {
        const char *cell = state->cells[index * count + column];
        size_t width = text_columns(cell), clipped = width > state->widths[column];
        size_t shown = clipped ? state->widths[column] : width;
        size_t bytes = clipped ? prefix_bytes(cell, shown - 3) : strlen(cell);
        memcpy(line + position, cell, bytes); position += bytes;
        if (clipped) { memcpy(line + position, "...", 3); position += 3; }
        if (column + 1 < count) { size_t padding = state->widths[column] - shown + 2; memset(line + position, ' ', padding); position += padding; }
    }
    line[position] = 0; *out = line; return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_binaryviewer_set_records(xxwidgets_widget *widget, xxwidgets_kind kind, const void *records, size_t count)
{
    const binary_descriptor *d = descriptor(kind);
    struct xxwidgets_binaryviewer_state *state, *previous;
    char **rows = NULL, **previous_rows; size_t row, column, columns = 0, previous_count, previous_columns;
    uint64_t revision; int value; xxwidgets_status status = XXWIDGETS_OK;
    if (!widget || widget->kind != kind || !d || (!records && count) || count > INT_MAX || count > SIZE_MAX / d->record_size || count > SIZE_MAX / (d->columns * sizeof(char *)))
        return XXWIDGETS_INVALID_ARGUMENT;
    state = (struct xxwidgets_binaryviewer_state *)calloc(1, sizeof(*state));
    if (!state) return XXWIDGETS_OUT_OF_MEMORY;
    state->descriptor = d; state->count = count; memcpy(state->widths, d->widths, sizeof(state->widths));
    if (count) {
        state->records = calloc(count, d->record_size); state->cells = (char **)calloc(count * d->columns, sizeof(char *));
        rows = (char **)calloc(count, sizeof(char *));
        if (!state->records || !state->cells || !rows) { status = XXWIDGETS_OUT_OF_MEMORY; goto failed; }
    }
    for (row = 0; row < count; ++row) {
        const unsigned char *input = (const unsigned char *)records + row * d->record_size;
        unsigned char *copy = (unsigned char *)state->records + row * d->record_size;
        memcpy(copy, input, d->record_size);
        for (column = 0; column < d->string_count; ++column) store_string(copy, d->offsets[column], NULL);
        for (column = 0; column < d->string_count; ++column) {
            const char *string = record_string(input, d->offsets[column]); char *owned;
            if (string && !xxwidgets_valid_utf8(string)) { status = XXWIDGETS_INVALID_ARGUMENT; goto failed; }
            if (!string) continue;
            owned = xxwidgets_strdup(string); if (!owned) { status = XXWIDGETS_OUT_OF_MEMORY; goto failed; }
            store_string(copy, d->offsets[column], owned);
        }
        for (column = 0; column < d->columns; ++column) {
            char buffer[64]; size_t width;
            status = escaped_cell(cell_text(kind, copy, column, buffer), &state->cells[row * d->columns + column]);
            if (status != XXWIDGETS_OK) goto failed;
            width = text_columns(state->cells[row * d->columns + column]); if (width > 48) width = 48;
            if (width > state->widths[column]) state->widths[column] = width;
        }
    }
    for (column = 0; column < d->columns; ++column) columns += state->widths[column] + (column + 1 < d->columns ? 2 : 0);
    for (row = 0; row < count; ++row) { status = format_row(state, row, &rows[row]); if (status != XXWIDGETS_OK) goto failed; }
    previous = widget->binaryviewer; previous_rows = widget->items; previous_count = widget->item_count;
    previous_columns = widget->archive_columns; revision = widget->binary_revision; value = widget->value;
    widget->binaryviewer = state; widget->items = rows; widget->item_count = count;
    widget->archive_columns = columns; widget->binary_revision = revision + 1; widget->value = count ? 0 : -1;
    ++widget->app->syncing; status = widget->app->ops->sync(widget); --widget->app->syncing;
    if (status != XXWIDGETS_OK) {
        widget->binaryviewer = previous; widget->items = previous_rows; widget->item_count = previous_count;
        widget->archive_columns = previous_columns; widget->binary_revision = revision; widget->value = value;
        ++widget->app->syncing; (void)widget->app->ops->sync(widget); --widget->app->syncing;
        goto failed;
    }
    dispose_state(previous); dispose_rows(previous_rows, previous_count); return XXWIDGETS_OK;
failed:
    dispose_state(state); dispose_rows(rows, count); return status;
}

xxwidgets_status xxwidgets_binaryviewer_get_record(const xxwidgets_widget *widget, xxwidgets_kind kind, size_t index, void *out)
{
    if (!widget || widget->kind != kind || !descriptor(kind) || !out || index >= widget->item_count || !widget->binaryviewer) return XXWIDGETS_INVALID_ARGUMENT;
    memcpy(out, (unsigned char *)widget->binaryviewer->records + index * widget->binaryviewer->descriptor->record_size, widget->binaryviewer->descriptor->record_size);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_binaryviewer_get_selection(xxwidgets_widget *widget, xxwidgets_kind kind, size_t *index, void *out)
{
    const binary_descriptor *d = descriptor(kind); int value; xxwidgets_status status;
    if (!widget || widget->kind != kind || !d || !index || !out) return XXWIDGETS_INVALID_ARGUMENT;
    *index = SIZE_MAX; memset(out, 0, d->record_size);
    status = xxwidgets_widget_get_value(widget, &value); if (status != XXWIDGETS_OK) return status;
    if (value >= 0 && (size_t)value < widget->item_count) { *index = (size_t)value; return xxwidgets_binaryviewer_get_record(widget, kind, *index, out); }
    return XXWIDGETS_OK;
}

size_t xxwidgets_binaryviewer_column_count(const xxwidgets_widget *widget)
{ const binary_descriptor *d = widget ? descriptor(widget->kind) : NULL; return d ? d->columns : 0; }

const char *xxwidgets_binaryviewer_column_title(const xxwidgets_widget *widget, size_t column)
{ const binary_descriptor *d = widget ? descriptor(widget->kind) : NULL; return d && column < d->columns ? XXW_TR(widget->app, d->titles[column]) : ""; }

const char *xxwidgets_binaryviewer_cell(const xxwidgets_widget *widget, size_t row, size_t column)
{
    return xxwidgets_binaryviewer_kind(widget) && widget->binaryviewer && row < widget->item_count && column < widget->binaryviewer->descriptor->columns
        ? widget->binaryviewer->cells[row * widget->binaryviewer->descriptor->columns + column] : "";
}

size_t xxwidgets_binaryviewer_column_width(const xxwidgets_widget *widget, size_t column)
{
    const binary_descriptor *d = widget ? descriptor(widget->kind) : NULL;
    if (!d || column >= d->columns) return 0;
    return widget->binaryviewer ? widget->binaryviewer->widths[column] : d->widths[column];
}
