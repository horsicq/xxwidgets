/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets/xxwidgets_memory_map.h"
#include "xxwidgets_internal.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct xxwidgets_memory_map {
    xxwidgets_widget *owner, *controls[XXWIDGETS_MEMORY_MAP_CONTROL_COUNT];
    int pending;
};
#define C(v, id) ((v)->controls[XXWIDGETS_MEMORY_MAP_##id])
enum { FIND_OFFSET = 1, FIND_ADDRESS, COPY, COPY_ALL };
static int valid_bounds(xxwidgets_rect b)
{
    return b.x >= 0 && b.y >= 0 && b.width >= 96 && b.height >= 20 && b.x <= 32767 - b.width && b.y <= 32767 - b.height;
}
xxwidgets_status xxwidgets_memory_map_set_rect(xxwidgets_memory_map *v, xxwidgets_rect b)
{
    xxwidgets_rect rects[XXWIDGETS_MEMORY_MAP_CONTROL_COUNT], old[XXWIDGETS_MEMORY_MAP_CONTROL_COUNT];
    if (!v || !valid_bounds(b))
        return XXWIDGETS_INVALID_ARGUMENT;
#define R(id, dx, dy, w, h) rects[XXWIDGETS_MEMORY_MAP_##id] = (xxwidgets_rect){b.x + (dx), b.y + (dy), w, h}
    R(PATH, 0, 0, b.width, 1);
    R(SUMMARY, 0, 2, b.width, 1);
    R(METADATA, 0, 3, b.width, 1);
    R(FIND_LABEL, 0, 5, 6, 2);
    R(FIND, 7, 5, 24, 2);
    R(FIND_OFFSET, 33, 5, 16, 2);
    R(FIND_ADDRESS, 51, 5, 18, 2);
    R(TABLE, 0, 8, b.width, b.height - 14);
    R(DETAILS, 0, b.height - 5, b.width, 1);
    R(COPY, 0, b.height - 3, 16, 2);
    R(COPY_ALL, 18, b.height - 3, 14, 2);
    R(STATUS, 34, b.height - 3, b.width - 34, 2);
#undef R
    for (size_t i = 0; i < XXWIDGETS_MEMORY_MAP_CONTROL_COUNT; ++i)
        old[i] = v->controls[i]->rect;
    for (size_t i = 0; i < XXWIDGETS_MEMORY_MAP_CONTROL_COUNT; ++i) {
        xxwidgets_status status = xxwidgets_widget_set_rect(v->controls[i], rects[i]);
        if (status != XXWIDGETS_OK) {
            for (size_t j = 0; j < i; ++j)
                (void)xxwidgets_widget_set_rect(v->controls[j], old[j]);
            return status;
        }
    }
    return XXWIDGETS_OK;
}
static void number(char *out, uint64_t value)
{
    if (value == UINT64_MAX)
        strcpy(out, "-");
    else
        snprintf(out, 32, "0x%" PRIX64, value);
}
static void describe(const xxwidgets_memory_map_record *r, char *out, size_t capacity)
{
    char offset[32], address[32];
    if (!r) {
        snprintf(out, capacity, "No region selected");
        return;
    }
    number(offset, r->offset < 0 ? UINT64_MAX : (uint64_t)r->offset);
    number(address, r->address);
    snprintf(out, capacity, "Offset %s | Address %s | %" PRIu64 " bytes | %s%s", offset, address, r->size,
             r->flags & XXWIDGETS_MEMORY_MAP_VIRTUAL ? "Virtual-only" : "Physical",
             r->flags & XXWIDGETS_MEMORY_MAP_INVISIBLE ? " | Invisible" : "");
}
xxwidgets_status xxwidgets_memory_map_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_memory_map **out)
{
    static const xxwidgets_kind kinds[] = {XXWIDGETS_LABEL, XXWIDGETS_LABEL,  XXWIDGETS_LABEL,  XXWIDGETS_LABEL,
                                           XXWIDGETS_EDIT,  XXWIDGETS_BUTTON, XXWIDGETS_BUTTON, XXWIDGETS_MEMORY_MAP,
                                           XXWIDGETS_LABEL, XXWIDGETS_BUTTON, XXWIDGETS_BUTTON, XXWIDGETS_LABEL};
    static const char *const names[] = {
        "", "", "", "Find", "0x0", "File offset", "Address", "Memory Map", "No region selected", "Copy row", "Copy all", "Select a region"};
    xxwidgets_memory_map *v;
    xxwidgets_status status;
    if (!out)
        return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !valid_bounds(bounds))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing)
        return XXWIDGETS_BUSY;
    v = calloc(1, sizeof(*v));
    if (!v)
        return XXWIDGETS_OUT_OF_MEMORY;
    v->owner = owner;
    for (size_t i = 0; i < XXWIDGETS_MEMORY_MAP_CONTROL_COUNT; ++i) {
        status = xxwidgets_widget_create(owner->app, owner, kinds[i], names[i], (xxwidgets_rect){0, 0, 1, 1}, v->controls + i);
        if (status != XXWIDGETS_OK)
            goto fail;
    }
    status = xxwidgets_memory_map_set_rect(v, bounds);
    if (status == XXWIDGETS_OK) {
        xxwidgets_memory_map_data empty = {"", "", 0, UINT64_MAX, UINT64_MAX, NULL, 0};
        status = xxwidgets_memory_map_set_data(v, &empty);
    }
    if (status != XXWIDGETS_OK)
        goto fail;
    *out = v;
    return XXWIDGETS_OK;
fail:
    xxwidgets_memory_map_destroy(v);
    return status;
}
xxwidgets_status xxwidgets_memory_map_destroy(xxwidgets_memory_map *v)
{
    if (!v)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling || v->owner->app->syncing)
        return XXWIDGETS_BUSY;
    for (size_t i = 0; i < XXWIDGETS_MEMORY_MAP_CONTROL_COUNT; ++i)
        if (v->controls[i])
            (void)xxwidgets_widget_destroy(v->controls[i]);
    free(v);
    return XXWIDGETS_OK;
}
xxwidgets_widget *xxwidgets_memory_map_control(const xxwidgets_memory_map *v, xxwidgets_memory_map_control_id id)
{
    return v && id >= 0 && id < XXWIDGETS_MEMORY_MAP_CONTROL_COUNT ? v->controls[id] : NULL;
}
xxwidgets_status xxwidgets_memory_map_set_data(xxwidgets_memory_map *v, const xxwidgets_memory_map_data *data)
{
    char summary[256], metadata[128], details[256], base[32], entry[32];
    char *texts[5] = {0}, *old[5];
    xxwidgets_widget *labels[5], *buttons[4];
    int enabled[4];
    xxwidgets_status status = XXWIDGETS_OUT_OF_MEMORY;
    if (!v || !data || !xxwidgets_valid_utf8(data->path) || !xxwidgets_valid_utf8(data->format) || strlen(data->format) > 128 ||
        data->count > XXWIDGETS_MEMORY_MAP_MAX_RECORDS || (data->count && !data->records))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling || v->owner->app->syncing)
        return XXWIDGETS_BUSY;
    for (size_t i = 0; i < data->count; ++i) {
        const xxwidgets_memory_map_record *r = data->records + i;
        if (!(r->flags & XXWIDGETS_MEMORY_MAP_VIRTUAL) &&
            (r->offset < 0 || (uint64_t)r->offset > data->file_size || r->size > data->file_size - (uint64_t)r->offset))
            return XXWIDGETS_INVALID_ARGUMENT;
    }
    snprintf(summary, sizeof(summary), "%s | %" PRIu64 " bytes | %zu regions", data->format, data->file_size, data->count);
    number(base, data->module_address);
    number(entry, data->entry_point_address);
    snprintf(metadata, sizeof(metadata), "Image base %s | Entry address %s", base, entry);
    describe(data->count ? data->records : NULL, details, sizeof(details));
    labels[0] = C(v, PATH);
    labels[1] = C(v, SUMMARY);
    labels[2] = C(v, METADATA);
    labels[3] = C(v, DETAILS);
    labels[4] = C(v, STATUS);
    buttons[0] = C(v, COPY);
    buttons[1] = C(v, COPY_ALL);
    buttons[2] = C(v, FIND_OFFSET);
    buttons[3] = C(v, FIND_ADDRESS);
    texts[0] = xxwidgets_strdup(data->path);
    texts[1] = xxwidgets_strdup(summary);
    texts[2] = xxwidgets_strdup(metadata);
    texts[3] = xxwidgets_strdup(details);
    texts[4] = xxwidgets_strdup(data->count ? "Select a region" : "No regions");
    for (size_t i = 0; i < 5; ++i)
        if (!texts[i])
            goto finish;
    for (size_t i = 0; i < 5; ++i) {
        old[i] = labels[i]->text;
        labels[i]->text = texts[i];
    }
    for (size_t i = 0; i < 4; ++i) {
        enabled[i] = buttons[i]->enabled;
        buttons[i]->enabled = data->count != 0;
    }
    status = XXWIDGETS_OK;
    ++v->owner->app->syncing;
    for (size_t i = 0; i < 5 && status == XXWIDGETS_OK; ++i)
        status = v->owner->app->ops->sync(labels[i]);
    for (size_t i = 0; i < 4 && status == XXWIDGETS_OK; ++i)
        status = v->owner->app->ops->sync(buttons[i]);
    --v->owner->app->syncing;
    if (status == XXWIDGETS_OK)
        status = xxwidgets_memory_map_table_set_rows(C(v, TABLE), data->records, data->count);
    if (status != XXWIDGETS_OK) {
        for (size_t i = 0; i < 5; ++i)
            labels[i]->text = old[i];
        for (size_t i = 0; i < 4; ++i)
            buttons[i]->enabled = enabled[i];
        ++v->owner->app->syncing;
        for (size_t i = 0; i < 5; ++i)
            (void)v->owner->app->ops->sync(labels[i]);
        for (size_t i = 0; i < 4; ++i)
            (void)v->owner->app->ops->sync(buttons[i]);
        --v->owner->app->syncing;
        goto finish;
    }
    for (size_t i = 0; i < 5; ++i) {
        free(old[i]);
        texts[i] = NULL;
    }
    v->pending = 0;
finish:
    for (size_t i = 0; i < 5; ++i)
        free(texts[i]);
    return status;
}
static int parse_number(const char *text, uint64_t *out)
{
    const unsigned char *p = (const unsigned char *)text;
    uint64_t value = 0;
    unsigned base = 10;
    int any = 0;
    if (!p)
        return 0;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        ++p;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    }
    while (*p) {
        unsigned digit = *p >= '0' && *p <= '9'   ? *p - '0'
                         : *p >= 'a' && *p <= 'f' ? *p - 'a' + 10
                         : *p >= 'A' && *p <= 'F' ? *p - 'A' + 10
                                                  : 16;
        if (digit == 16)
            break;
        if (digit >= base || value > (UINT64_MAX - digit) / base)
            return 0;
        value = value * base + digit;
        ++p;
        any = 1;
    }
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        ++p;
    if (!any || *p)
        return 0;
    *out = value;
    return 1;
}
xxwidgets_status xxwidgets_memory_map_find(xxwidgets_memory_map *v, const char *text, int address_lookup)
{
    uint64_t value;
    xxwidgets_status status;
    if (!v || (address_lookup != 0 && address_lookup != 1) || !parse_number(text, &value))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling || v->owner->app->syncing)
        return XXWIDGETS_BUSY;
    for (size_t i = 0; i < xxwidgets_memory_map_table_count(C(v, TABLE)); ++i) {
        xxwidgets_memory_map_record r;
        uint64_t start;
        char details[256];
        status = xxwidgets_memory_map_table_get_row(C(v, TABLE), i, &r);
        if (status != XXWIDGETS_OK)
            return status;
        if (address_lookup) {
            if (r.address == UINT64_MAX)
                continue;
            start = r.address;
        } else {
            if (r.offset < 0 || (r.flags & XXWIDGETS_MEMORY_MAP_VIRTUAL))
                continue;
            start = (uint64_t)r.offset;
        }
        if (value < start || value - start >= r.size)
            continue;
        status = xxwidgets_widget_set_value(C(v, TABLE), (int)i);
        if (status == XXWIDGETS_OK) {
            describe(&r, details, sizeof(details));
            status = xxwidgets_widget_set_text(C(v, DETAILS), details);
        }
        return status;
    }
    return XXWIDGETS_UNAVAILABLE;
}
xxwidgets_status xxwidgets_memory_map_export_text(const xxwidgets_memory_map *v, size_t index, char *buffer, size_t capacity,
                                                  size_t *required)
{
    static const char header[] = "File offset\tAddress\tSize\tType\tName\tAttributes\n";
    size_t count, first, last, length = sizeof(header) - 1, pos = 0;
    xxwidgets_widget *table;
    if (!v || (!buffer && capacity))
        return XXWIDGETS_INVALID_ARGUMENT;
    table = C(v, TABLE);
    count = xxwidgets_memory_map_table_count(table);
    if (index != SIZE_MAX && index >= count)
        return XXWIDGETS_INVALID_ARGUMENT;
    first = index == SIZE_MAX ? 0 : index;
    last = index == SIZE_MAX ? count : index + 1;
    for (size_t i = first; i < last; ++i)
        for (size_t j = 0; j < 6; ++j)
            length += strlen(xxwidgets_debugger_table_cell(table, i, j)) + 1;
    if (required)
        *required = length + 1;
    if (!buffer && !capacity)
        return XXWIDGETS_OK;
    if (capacity <= length) {
        if (capacity)
            buffer[0] = 0;
        return XXWIDGETS_BUFFER_TOO_SMALL;
    }
    memcpy(buffer, header, sizeof(header) - 1);
    pos = sizeof(header) - 1;
    for (size_t i = first; i < last; ++i)
        for (size_t j = 0; j < 6; ++j) {
            const char *cell = xxwidgets_debugger_table_cell(table, i, j);
            size_t n = strlen(cell);
            memcpy(buffer + pos, cell, n);
            pos += n;
            buffer[pos++] = j == 5 ? '\n' : '\t';
        }
    buffer[pos] = 0;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_memory_map_handle_event(xxwidgets_memory_map *v, const xxwidgets_event *event)
{
    if (!v || !event)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == C(v, TABLE)) {
        size_t index;
        xxwidgets_memory_map_record r;
        char details[256];
        xxwidgets_status status = xxwidgets_memory_map_table_get_selection(C(v, TABLE), &index, &r);
        if (status != XXWIDGETS_OK)
            return status;
        describe(index == SIZE_MAX ? NULL : &r, details, sizeof(details));
        return xxwidgets_widget_set_text(C(v, DETAILS), details);
    }
    if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == C(v, FIND_OFFSET))
            v->pending = FIND_OFFSET;
        else if (event->widget == C(v, FIND_ADDRESS))
            v->pending = FIND_ADDRESS;
        else if (event->widget == C(v, COPY))
            v->pending = COPY;
        else if (event->widget == C(v, COPY_ALL))
            v->pending = COPY_ALL;
    } else if (event->type == XXWIDGETS_EVENT_ACTIVATE && event->widget == C(v, FIND))
        v->pending = FIND_OFFSET;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_memory_map_process(xxwidgets_memory_map *v)
{
    int action;
    xxwidgets_status status = XXWIDGETS_OK;
    if (!v)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling || v->owner->app->syncing)
        return XXWIDGETS_BUSY;
    action = v->pending;
    v->pending = 0;
    if (!action)
        return XXWIDGETS_OK;
    if (action == FIND_OFFSET || action == FIND_ADDRESS) {
        char text[128];
        status = xxwidgets_widget_get_text(C(v, FIND), text, sizeof(text), NULL);
        if (status == XXWIDGETS_OK)
            status = xxwidgets_memory_map_find(v, text, action == FIND_ADDRESS);
        return xxwidgets_widget_set_text(C(v, STATUS), status == XXWIDGETS_OK            ? "Region selected"
                                                       : status == XXWIDGETS_UNAVAILABLE ? "No region contains that value"
                                                                                         : "Enter a decimal or 0x value");
    }
    if (action == COPY || action == COPY_ALL) {
        size_t index = SIZE_MAX, required;
        char *text;
        xxwidgets_memory_map_record r;
        if (action == COPY) {
            status = xxwidgets_memory_map_table_get_selection(C(v, TABLE), &index, &r);
            if (status != XXWIDGETS_OK || index == SIZE_MAX)
                return status;
        }
        status = xxwidgets_memory_map_export_text(v, index, NULL, 0, &required);
        if (status != XXWIDGETS_OK)
            return status;
        text = malloc(required);
        if (!text)
            return XXWIDGETS_OUT_OF_MEMORY;
        status = xxwidgets_memory_map_export_text(v, index, text, required, NULL);
        if (status == XXWIDGETS_OK)
            status = xxwidgets_clipboard_set_text(v->owner, text);
        free(text);
        return xxwidgets_widget_set_text(C(v, STATUS), status == XXWIDGETS_OK ? "Copied" : "Clipboard unavailable");
    }
    return status;
}
