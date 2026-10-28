/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_debugger_tables.h"
#include "xxwidgets/xxwidgets_memory_map.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct debugger_descriptor {
    size_t record_size, string_count, offsets[3], flags_offset, columns;
    unsigned flag_mask;
    const char *titles[6];
    size_t widths[6], maximums[6];
} debugger_descriptor;

struct xxwidgets_debugger_table_state {
    const debugger_descriptor *descriptor;
    void *records;
    char **cells;
    size_t count, widths[6];
};

static const debugger_descriptor *descriptor(xxwidgets_kind kind)
{
    static const debugger_descriptor disasm = {
        sizeof(xxwidgets_debugger_instruction), 3,
        {offsetof(xxwidgets_debugger_instruction, bytes), offsetof(xxwidgets_debugger_instruction, instruction), offsetof(xxwidgets_debugger_instruction, comment)},
        offsetof(xxwidgets_debugger_instruction, flags), 5, XXWIDGETS_DEBUGGER_CURRENT | XXWIDGETS_DEBUGGER_BREAKPOINT,
        {"", XXW_N_("Address"), XXW_N_("Bytes"), XXW_N_("Instruction"), XXW_N_("Comment")}, {2, 19, 8, 24, 24}, {2, 19, 48, 96, 96}};
    static const debugger_descriptor registers = {
        sizeof(xxwidgets_debugger_register), 2,
        {offsetof(xxwidgets_debugger_register, name), offsetof(xxwidgets_debugger_register, value), 0},
        offsetof(xxwidgets_debugger_register, flags), 3, XXWIDGETS_DEBUGGER_CHANGED,
        {"", XXW_N_("Register"), XXW_N_("Value"), NULL, NULL}, {1, 8, 18, 0, 0}, {1, 24, 96, 0, 0}};
    static const debugger_descriptor stack = {
        sizeof(xxwidgets_debugger_stack_entry), 2,
        {offsetof(xxwidgets_debugger_stack_entry, value), offsetof(xxwidgets_debugger_stack_entry, comment), 0},
        offsetof(xxwidgets_debugger_stack_entry, flags), 4, XXWIDGETS_DEBUGGER_CURRENT | XXWIDGETS_DEBUGGER_CHANGED,
        {"", XXW_N_("Address"), XXW_N_("Value"), XXW_N_("Comment"), NULL}, {2, 18, 18, 24, 0}, {2, 18, 96, 96, 0}};
    static const debugger_descriptor memory_map = {
        sizeof(xxwidgets_memory_map_record), 2,
        {offsetof(xxwidgets_memory_map_record, name), offsetof(xxwidgets_memory_map_record, type), 0},
        offsetof(xxwidgets_memory_map_record, flags), 6, XXWIDGETS_MEMORY_MAP_VIRTUAL | XXWIDGETS_MEMORY_MAP_INVISIBLE,
        {XXW_N_("File offset"), XXW_N_("Address"), XXW_N_("Size"), XXW_N_("Type"), XXW_N_("Name"), XXW_N_("Attributes")},
        {18, 18, 18, 12, 24, 12}, {18, 18, 18, 32, 80, 32}};
    switch (kind) {
        case XXWIDGETS_MEMORY_MAP: return &memory_map;
        case XXWIDGETS_DEBUGGER_DISASM: return &disasm;
        case XXWIDGETS_DEBUGGER_REGISTERS: return &registers;
        case XXWIDGETS_DEBUGGER_STACK: return &stack;
        default: return NULL;
    }
}

static const char *record_string(const void *record, size_t offset)
{ const char *string; memcpy(&string, (const unsigned char *)record + offset, sizeof(string)); return string; }

static void store_string(void *record, size_t offset, char *string)
{ memcpy((unsigned char *)record + offset, &string, sizeof(string)); }

static unsigned record_flags(const void *record, const debugger_descriptor *d)
{ unsigned flags; memcpy(&flags, (const unsigned char *)record + d->flags_offset, sizeof(flags)); return flags; }

static void dispose_rows(char **rows, size_t count)
{ if (rows) for (size_t i = 0; i < count; ++i) free(rows[i]); free(rows); }

static void dispose_state(struct xxwidgets_debugger_table_state *state)
{
    if (!state) return;
    if (state->records) for (size_t row = 0; row < state->count; ++row)
        for (size_t column = 0; column < state->descriptor->string_count; ++column)
            free((void *)record_string((unsigned char *)state->records + row * state->descriptor->record_size, state->descriptor->offsets[column]));
    dispose_rows(state->cells, state->count * state->descriptor->columns);
    free(state->records); free(state);
}

void xxwidgets_debugger_table_dispose(xxwidgets_widget *widget)
{ dispose_state(widget->debugger_table); widget->debugger_table = NULL; }

static size_t text_columns(const char *text)
{
    const unsigned char *p = (const unsigned char *)text; size_t count = 0;
    while (*p) if ((*p++ & 0xc0) != 0x80) ++count;
    return count;
}

static size_t prefix_bytes(const char *text, size_t columns)
{
    const unsigned char *p = (const unsigned char *)text;
    while (*p && columns) { ++p; while ((*p & 0xc0) == 0x80) ++p; --columns; }
    return (size_t)(p - (const unsigned char *)text);
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
        if (*p < 32 || *p == 127) {
            copy[position++] = '\\'; copy[position++] = 'x'; copy[position++] = digits[*p >> 4]; copy[position++] = digits[*p & 15];
        } else copy[position++] = (char)*p;
    }
    copy[position] = 0; *out = copy; return XXWIDGETS_OK;
}

static const char *cell_text(const xxwidgets_widget *widget, const void *record, size_t column, char buffer[64])
{
    xxwidgets_kind kind = widget->kind;
    const debugger_descriptor *d = descriptor(kind);
    unsigned flags = record_flags(record, d);
    if (kind == XXWIDGETS_MEMORY_MAP) {
        const xxwidgets_memory_map_record *r = record;
        if (column == 0) {
            if (r->offset < 0) return "-";
            snprintf(buffer, 64, "0x%016" PRIX64, (uint64_t)r->offset); return buffer;
        }
        if (column == 1) {
            if (r->address == UINT64_MAX) return "-";
            snprintf(buffer, 64, "0x%016" PRIX64, r->address); return buffer;
        }
        if (column == 2) { snprintf(buffer, 64, "0x%016" PRIX64, r->size); return buffer; }
        if (column == 3) return r->type;
        if (column == 4) return r->name;
        return (flags & XXWIDGETS_MEMORY_MAP_VIRTUAL)
            ? (flags & XXWIDGETS_MEMORY_MAP_INVISIBLE ? "Virtual, invisible" : "Virtual")
            : (flags & XXWIDGETS_MEMORY_MAP_INVISIBLE ? "Physical, invisible" : "Physical");
    }
    if (column == 0) {
        size_t n = 0;
        if (flags & XXWIDGETS_DEBUGGER_CURRENT) buffer[n++] = '>';
        if (flags & XXWIDGETS_DEBUGGER_BREAKPOINT) buffer[n++] = 'B';
        if (flags & XXWIDGETS_DEBUGGER_CHANGED) buffer[n++] = '*';
        buffer[n] = 0; return buffer;
    }
    if (kind == XXWIDGETS_DEBUGGER_DISASM) {
        const xxwidgets_debugger_instruction *r = record;
        if (column == 1) {
            if (widget->app->disasm_options.location_colon)
                snprintf(buffer, 64, "0x%08" PRIX32 ":%08" PRIX32, (uint32_t)(r->address >> 32), (uint32_t)r->address);
            else snprintf(buffer, 64, "0x%016" PRIX64, r->address);
            if (!widget->app->disasm_options.uppercase)
                for (char *p = buffer; *p; ++p) if (*p >= 'A' && *p <= 'F') *p += 'a' - 'A';
            return buffer;
        }
        return column == 2 ? r->bytes : column == 3 ? r->instruction : r->comment;
    }
    if (kind == XXWIDGETS_DEBUGGER_REGISTERS) {
        const xxwidgets_debugger_register *r = record;
        return column == 1 ? r->name : r->value;
    }
    {
        const xxwidgets_debugger_stack_entry *r = record;
        if (column == 1) { snprintf(buffer, 64, "0x%016" PRIX64, r->address); return buffer; }
        return column == 2 ? r->value : r->comment;
    }
}

static xxwidgets_status format_row(const struct xxwidgets_debugger_table_state *state, size_t index, char **out)
{
    size_t length = 0, position = 0, count = state->descriptor->columns;
    char *line;
    for (size_t column = 0; column < count; ++column) {
        size_t bytes = strlen(state->cells[index * count + column]);
        if (bytes > SIZE_MAX - state->widths[column] - 3 || length > SIZE_MAX - bytes - state->widths[column] - 3)
            return XXWIDGETS_INVALID_ARGUMENT;
        length += bytes + state->widths[column] + 2;
    }
    line = (char *)malloc(length + 1);
    if (!line) return XXWIDGETS_OUT_OF_MEMORY;
    for (size_t column = 0; column < count; ++column) {
        const char *cell = state->cells[index * count + column];
        size_t width = text_columns(cell), clipped = width > state->widths[column];
        size_t shown = clipped ? state->widths[column] : width;
        size_t bytes = clipped ? prefix_bytes(cell, shown > 3 ? shown - 3 : shown) : strlen(cell);
        memcpy(line + position, cell, bytes); position += bytes;
        if (clipped && shown > 3) { memcpy(line + position, "...", 3); position += 3; }
        if (column + 1 < count) { size_t padding = state->widths[column] - shown + 2; memset(line + position, ' ', padding); position += padding; }
    }
    line[position] = 0; *out = line; return XXWIDGETS_OK;
}

static xxwidgets_status set_rows(xxwidgets_widget *widget, xxwidgets_kind kind, const void *records, size_t count)
{
    const debugger_descriptor *d = descriptor(kind);
    struct xxwidgets_debugger_table_state *state, *previous;
    char **rows = NULL, **previous_rows; size_t columns = 0, previous_count, previous_columns;
    uint64_t revision; int value, selection = count ? 0 : -1, have_current = 0;
    xxwidgets_status status = XXWIDGETS_OK;
    if (!widget || widget->kind != kind || !d || (!records && count) || count > INT_MAX ||
        count > SIZE_MAX / d->record_size || count > SIZE_MAX / (d->columns * sizeof(char *))) return XXWIDGETS_INVALID_ARGUMENT;
    state = (struct xxwidgets_debugger_table_state *)calloc(1, sizeof(*state));
    if (!state) return XXWIDGETS_OUT_OF_MEMORY;
    state->descriptor = d; state->count = count; memcpy(state->widths, d->widths, sizeof(state->widths));
    if (count) {
        state->records = calloc(count, d->record_size); state->cells = (char **)calloc(count * d->columns, sizeof(char *));
        rows = (char **)calloc(count, sizeof(char *));
        if (!state->records || !state->cells || !rows) { status = XXWIDGETS_OUT_OF_MEMORY; goto failed; }
    }
    for (size_t row = 0; row < count; ++row) {
        const unsigned char *input = (const unsigned char *)records + row * d->record_size;
        unsigned char *copy = (unsigned char *)state->records + row * d->record_size;
        unsigned flags = record_flags(input, d);
        if (flags & ~d->flag_mask) { status = XXWIDGETS_INVALID_ARGUMENT; goto failed; }
        if (kind != XXWIDGETS_MEMORY_MAP && !have_current && (flags & XXWIDGETS_DEBUGGER_CURRENT)) { selection = (int)row; have_current = 1; }
        memcpy(copy, input, d->record_size);
        for (size_t column = 0; column < d->string_count; ++column) store_string(copy, d->offsets[column], NULL);
        for (size_t column = 0; column < d->string_count; ++column) {
            const char *string = record_string(input, d->offsets[column]); char *owned;
            if (string && !xxwidgets_valid_utf8(string)) { status = XXWIDGETS_INVALID_ARGUMENT; goto failed; }
            if (!string) continue;
            owned = xxwidgets_strdup(string);
            if (!owned) { status = XXWIDGETS_OUT_OF_MEMORY; goto failed; }
            store_string(copy, d->offsets[column], owned);
        }
        for (size_t column = 0; column < d->columns; ++column) {
            char buffer[64]; size_t width;
            status = escaped_cell(cell_text(widget, copy, column, buffer), &state->cells[row * d->columns + column]);
            if (status != XXWIDGETS_OK) goto failed;
            if (kind == XXWIDGETS_DEBUGGER_DISASM && (column == 2 || column == 3))
                for (char *p = state->cells[row * d->columns + column]; *p; ++p) {
                    if (widget->app->disasm_options.uppercase) { if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A'; }
                    else if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
                }
            width = text_columns(state->cells[row * d->columns + column]);
            if (width > d->maximums[column]) width = d->maximums[column];
            if (width > state->widths[column]) state->widths[column] = width;
        }
    }
    for (size_t column = 0; column < d->columns; ++column) columns += state->widths[column] + (column + 1 < d->columns ? 2 : 0);
    for (size_t row = 0; row < count; ++row) { status = format_row(state, row, &rows[row]); if (status != XXWIDGETS_OK) goto failed; }
    previous = widget->debugger_table; previous_rows = widget->items; previous_count = widget->item_count;
    previous_columns = widget->archive_columns; revision = widget->debugger_revision; value = widget->value;
    widget->debugger_table = state; widget->items = rows; widget->item_count = count;
    widget->archive_columns = columns; widget->debugger_revision = revision + 1; widget->value = selection;
    ++widget->app->syncing; status = widget->app->ops->sync(widget); --widget->app->syncing;
    if (status != XXWIDGETS_OK) {
        widget->debugger_table = previous; widget->items = previous_rows; widget->item_count = previous_count;
        widget->archive_columns = previous_columns; widget->debugger_revision = revision; widget->value = value;
        ++widget->app->syncing; (void)widget->app->ops->sync(widget); --widget->app->syncing;
        goto failed;
    }
    dispose_state(previous); dispose_rows(previous_rows, previous_count); return XXWIDGETS_OK;
failed:
    dispose_state(state); dispose_rows(rows, count); return status;
}

xxwidgets_status xxwidgets_debugger_disasm_reformat(xxwidgets_widget *widget)
{
    int selected;
    xxwidgets_status status;
    if (!widget || widget->kind != XXWIDGETS_DEBUGGER_DISASM) return XXWIDGETS_INVALID_ARGUMENT;
    if (!widget->debugger_table) return XXWIDGETS_OK;
    selected = widget->value;
    status = set_rows(widget, widget->kind, widget->debugger_table->records, widget->debugger_table->count);
    if (status == XXWIDGETS_OK && selected >= 0 && (size_t)selected < widget->item_count)
        status = xxwidgets_widget_set_value(widget, selected);
    return status;
}

static xxwidgets_status get_row(const xxwidgets_widget *widget, xxwidgets_kind kind, size_t index, void *out)
{
    const debugger_descriptor *d = descriptor(kind);
    if (!widget || widget->kind != kind || !d || !out || index >= widget->item_count || !widget->debugger_table) return XXWIDGETS_INVALID_ARGUMENT;
    memcpy(out, (const unsigned char *)widget->debugger_table->records + index * d->record_size, d->record_size);
    return XXWIDGETS_OK;
}

static xxwidgets_status get_selection(xxwidgets_widget *widget, xxwidgets_kind kind, size_t *index, void *out)
{
    const debugger_descriptor *d = descriptor(kind); int value; xxwidgets_status status;
    if (!widget || widget->kind != kind || !d || !index || !out) return XXWIDGETS_INVALID_ARGUMENT;
    *index = SIZE_MAX; memset(out, 0, d->record_size);
    status = xxwidgets_widget_get_value(widget, &value); if (status != XXWIDGETS_OK) return status;
    if (value >= 0 && (size_t)value < widget->item_count) { *index = (size_t)value; return get_row(widget, kind, *index, out); }
    return XXWIDGETS_OK;
}

#define DEFINE_TABLE(name, type, widget_kind) \
    xxwidgets_status xxwidgets_debugger_##name##_set_rows(xxwidgets_widget *w, const type *rows, size_t count) { return set_rows(w, widget_kind, rows, count); } \
    xxwidgets_status xxwidgets_debugger_##name##_clear(xxwidgets_widget *w) { return set_rows(w, widget_kind, NULL, 0); } \
    size_t xxwidgets_debugger_##name##_count(const xxwidgets_widget *w) { return w && w->kind == widget_kind ? w->item_count : 0; } \
    xxwidgets_status xxwidgets_debugger_##name##_get_row(const xxwidgets_widget *w, size_t i, type *out) { return get_row(w, widget_kind, i, out); } \
    xxwidgets_status xxwidgets_debugger_##name##_get_selection(xxwidgets_widget *w, size_t *i, type *out) { return get_selection(w, widget_kind, i, out); }
DEFINE_TABLE(disasm, xxwidgets_debugger_instruction, XXWIDGETS_DEBUGGER_DISASM)
DEFINE_TABLE(registers, xxwidgets_debugger_register, XXWIDGETS_DEBUGGER_REGISTERS)
DEFINE_TABLE(stack, xxwidgets_debugger_stack_entry, XXWIDGETS_DEBUGGER_STACK)
#undef DEFINE_TABLE

size_t xxwidgets_debugger_table_column_count(const xxwidgets_widget *widget)
{ const debugger_descriptor *d = widget ? descriptor(widget->kind) : NULL; return d ? d->columns : 0; }

const char *xxwidgets_debugger_table_column_title(const xxwidgets_widget *widget, size_t column)
{ const debugger_descriptor *d = widget ? descriptor(widget->kind) : NULL; return d && column < d->columns ? XXW_TR(widget->app, d->titles[column]) : ""; }

const char *xxwidgets_debugger_table_cell(const xxwidgets_widget *widget, size_t row, size_t column)
{
    return xxwidgets_debugger_table_kind(widget) && widget->debugger_table && row < widget->item_count && column < widget->debugger_table->descriptor->columns
        ? widget->debugger_table->cells[row * widget->debugger_table->descriptor->columns + column] : "";
}

size_t xxwidgets_debugger_table_column_width(const xxwidgets_widget *widget, size_t column)
{
    const debugger_descriptor *d = widget ? descriptor(widget->kind) : NULL;
    if (!d || column >= d->columns) return 0;
    return widget->debugger_table ? widget->debugger_table->widths[column] : d->widths[column];
}

xxwidgets_status xxwidgets_memory_map_table_set_rows(xxwidgets_widget *w, const xxwidgets_memory_map_record *rows, size_t count)
{
    if (count > XXWIDGETS_MEMORY_MAP_MAX_RECORDS || (count && !rows)) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i) {
        const xxwidgets_memory_map_record *r = rows + i;
        if (!r->name || !r->type || strlen(r->name) > 512 || strlen(r->type) > 128 ||
            (r->flags & XXWIDGETS_MEMORY_MAP_VIRTUAL ? r->offset != -1 :
             (r->offset < 0 || r->size > (uint64_t)INT64_MAX - (uint64_t)r->offset)) ||
            (r->address != UINT64_MAX && r->size > UINT64_MAX - r->address)) return XXWIDGETS_INVALID_ARGUMENT;
    }
    return set_rows(w, XXWIDGETS_MEMORY_MAP, rows, count);
}
xxwidgets_status xxwidgets_memory_map_table_set_compact(xxwidgets_widget *w, int compact)
{
    xxwidgets_status status;
    uint64_t revision;
    int previous;
    if (!w || w->kind != XXWIDGETS_MEMORY_MAP || (compact != 0 && compact != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    if (w->app->dispatch_depth || w->app->polling || w->app->syncing) return XXWIDGETS_BUSY;
    if (w->memory_map_compact == compact) return XXWIDGETS_OK;
    previous = w->memory_map_compact; revision = w->debugger_revision;
    w->memory_map_compact = compact; ++w->debugger_revision;
    ++w->app->syncing; status = w->app->ops->sync(w); --w->app->syncing;
    if (status != XXWIDGETS_OK) {
        w->memory_map_compact = previous; w->debugger_revision = revision;
        ++w->app->syncing; (void)w->app->ops->sync(w); --w->app->syncing;
    }
    return status;
}
xxwidgets_status xxwidgets_memory_map_table_clear(xxwidgets_widget *w) { return set_rows(w, XXWIDGETS_MEMORY_MAP, NULL, 0); }
size_t xxwidgets_memory_map_table_count(const xxwidgets_widget *w) { return w && w->kind == XXWIDGETS_MEMORY_MAP ? w->item_count : 0; }
xxwidgets_status xxwidgets_memory_map_table_get_row(const xxwidgets_widget *w, size_t i, xxwidgets_memory_map_record *out)
{ return get_row(w, XXWIDGETS_MEMORY_MAP, i, out); }
xxwidgets_status xxwidgets_memory_map_table_get_selection(xxwidgets_widget *w, size_t *i, xxwidgets_memory_map_record *out)
{ return get_selection(w, XXWIDGETS_MEMORY_MAP, i, out); }
