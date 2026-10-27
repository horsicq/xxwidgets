/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets/xxwidgets_exports.h"
#include "xxwidgets_internal.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct xxwidgets_exports {
    xxwidgets_widget *owner, *controls[XXWIDGETS_EXPORTS_CONTROL_COUNT];
    xx_export_record *records;
    size_t count;
    char *path, *format, *filter;
    uint64_t file_size;
    int pending;
};
#define C(v, id) ((v)->controls[XXWIDGETS_EXPORTS_##id])
enum { FILTER = 1, RESET, COPY, COPY_ALL };
static int valid_bounds(xxwidgets_rect b)
{
    return b.x >= 0 && b.y >= 0 && b.width >= 96 && b.height >= 20 && b.x <= 32767 - b.width && b.y <= 32767 - b.height;
}
xxwidgets_status xxwidgets_exports_set_rect(xxwidgets_exports *v, xxwidgets_rect b)
{
    xxwidgets_rect r[XXWIDGETS_EXPORTS_CONTROL_COUNT], old[XXWIDGETS_EXPORTS_CONTROL_COUNT];
    if (!v || !valid_bounds(b))
        return XXWIDGETS_INVALID_ARGUMENT;
#define R(id, dx, dy, w, h) r[XXWIDGETS_EXPORTS_##id] = (xxwidgets_rect){b.x + (dx), b.y + (dy), w, h}
    R(PATH, 0, 0, b.width, 1);
    R(SUMMARY, 0, 2, b.width, 1);
    R(FILTER_LABEL, 0, 4, 8, 2);
    R(FILTER, 9, 4, b.width - 26, 2);
    R(RESET, b.width - 15, 4, 15, 2);
    R(TABLE, 0, 7, b.width, b.height - 13);
    R(DETAILS, 0, b.height - 5, b.width, 1);
    R(COPY, 0, b.height - 3, 16, 2);
    R(COPY_ALL, 18, b.height - 3, 16, 2);
    R(STATUS, 36, b.height - 3, b.width - 36, 2);
#undef R
    for (size_t i = 0; i < XXWIDGETS_EXPORTS_CONTROL_COUNT; ++i)
        old[i] = v->controls[i]->rect;
    for (size_t i = 0; i < XXWIDGETS_EXPORTS_CONTROL_COUNT; ++i) {
        xxwidgets_status status = xxwidgets_widget_set_rect(v->controls[i], r[i]);
        if (status != XXWIDGETS_OK) {
            for (size_t j = 0; j < i; ++j)
                (void)xxwidgets_widget_set_rect(v->controls[j], old[j]);
            return status;
        }
    }
    return XXWIDGETS_OK;
}
static void free_records(xx_export_record *records, size_t count)
{
    if (records)
        for (size_t i = 0; i < count; ++i) {
            free(records[i].name);
            free(records[i].forwarder);
        }
    free(records);
}
static void describe(const xx_export_record *r, char *out, size_t capacity)
{
    char offset[32], address[32], ordinal[32];
    if (!r) {
        snprintf(out, capacity, "No export selected");
        return;
    }
    if (r->offset < 0)
        strcpy(offset, "-");
    else
        snprintf(offset, sizeof(offset), "0x%" PRIX64, (uint64_t)r->offset);
    if (r->address == UINT64_MAX)
        strcpy(address, "-");
    else
        snprintf(address, sizeof(address), "0x%" PRIX64, r->address);
    if (r->ordinal == UINT64_MAX)
        strcpy(ordinal, "-");
    else
        snprintf(ordinal, sizeof(ordinal), "%" PRIu64, r->ordinal);
    snprintf(out, capacity, "File offset %s | Address %s | Ordinal %s | %s", offset, address, ordinal,
             r->forwarder && *r->forwarder ? "Forwarded"
             : r->name && *r->name         ? "Named"
                                           : "Ordinal-only");
}
static int matches(const char *text, const char *filter)
{
    size_t length = strlen(filter);
    if (!length)
        return 1;
    if (!text)
        return 0;
    for (; *text; ++text) {
        size_t i;
        for (i = 0; i < length && text[i]; ++i) {
            unsigned char a = (unsigned char)text[i], b = (unsigned char)filter[i];
            if (a >= 'A' && a <= 'Z')
                a += 'a' - 'A';
            if (b >= 'A' && b <= 'Z')
                b += 'a' - 'A';
            if (a != b)
                break;
        }
        if (i == length)
            return 1;
    }
    return 0;
}
static xxwidgets_status present(xxwidgets_exports *v, const xxwidgets_exports_data *data, const char *filter)
{
    xx_export_record *rows = data->count ? malloc(data->count * sizeof(*rows)) : NULL;
    size_t count = 0;
    char summary[256], details[256];
    char *filter_copy = xxwidgets_strdup(filter);
    char *texts[5] = {0}, *old[5];
    xxwidgets_widget *labels[] = {C(v, PATH), C(v, SUMMARY), C(v, FILTER), C(v, DETAILS), C(v, STATUS)};
    xxwidgets_widget *buttons[] = {C(v, COPY), C(v, COPY_ALL)};
    int enabled[2];
    xxwidgets_status status = XXWIDGETS_OUT_OF_MEMORY;
    if ((data->count && !rows) || !filter_copy) {
        free(rows);
        free(filter_copy);
        return status;
    }
    for (size_t i = 0; i < data->count; ++i)
        if (matches(data->records[i].forwarder, filter) || matches(data->records[i].name, filter))
            rows[count++] = data->records[i];
    snprintf(summary, sizeof(summary), "%s | %" PRIu64 " bytes | %zu / %zu exports", data->format, data->file_size, count, data->count);
    describe(count ? rows : NULL, details, sizeof(details));
    texts[0] = xxwidgets_strdup(data->path);
    texts[1] = xxwidgets_strdup(summary);
    texts[2] = xxwidgets_strdup(filter);
    texts[3] = xxwidgets_strdup(details);
    texts[4] = xxwidgets_strdup(count ? "Select an export" : "No exports match");
    for (size_t i = 0; i < 5; ++i)
        if (!texts[i])
            goto finish;
    for (size_t i = 0; i < 5; ++i) {
        old[i] = labels[i]->text;
        labels[i]->text = texts[i];
    }
    for (size_t i = 0; i < 2; ++i) {
        enabled[i] = buttons[i]->enabled;
        buttons[i]->enabled = count != 0;
    }
    status = XXWIDGETS_OK;
    ++v->owner->app->syncing;
    for (size_t i = 0; i < 5 && status == XXWIDGETS_OK; ++i)
        status = v->owner->app->ops->sync(labels[i]);
    for (size_t i = 0; i < 2 && status == XXWIDGETS_OK; ++i)
        status = v->owner->app->ops->sync(buttons[i]);
    --v->owner->app->syncing;
    if (status == XXWIDGETS_OK)
        status = xxwidget_exportsviewer_set_records(C(v, TABLE), rows, count);
    if (status != XXWIDGETS_OK) {
        for (size_t i = 0; i < 5; ++i)
            labels[i]->text = old[i];
        for (size_t i = 0; i < 2; ++i)
            buttons[i]->enabled = enabled[i];
        ++v->owner->app->syncing;
        for (size_t i = 0; i < 5; ++i)
            (void)v->owner->app->ops->sync(labels[i]);
        for (size_t i = 0; i < 2; ++i)
            (void)v->owner->app->ops->sync(buttons[i]);
        --v->owner->app->syncing;
        goto finish;
    }
    for (size_t i = 0; i < 5; ++i) {
        free(old[i]);
        texts[i] = NULL;
    }
    free(v->filter);
    v->filter = filter_copy;
    filter_copy = NULL;
finish:
    for (size_t i = 0; i < 5; ++i)
        free(texts[i]);
    free(rows);
    free(filter_copy);
    return status;
}
xxwidgets_status xxwidgets_exports_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_exports **out)
{
    static const xxwidgets_kind kinds[] = {XXWIDGETS_LABEL,        XXWIDGETS_LABEL, XXWIDGETS_LABEL,  XXWIDGETS_EDIT,   XXWIDGETS_BUTTON,
                                           XXWIDGET_EXPORTSVIEWER, XXWIDGETS_LABEL, XXWIDGETS_BUTTON, XXWIDGETS_BUTTON, XXWIDGETS_LABEL};
    static const char *const names[] = {"", "", "Filter", "", "Reset", "Exports", "No export selected", "Copy row", "Copy visible", ""};
    xxwidgets_exports *v;
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
    for (size_t i = 0; i < XXWIDGETS_EXPORTS_CONTROL_COUNT; ++i) {
        status = xxwidgets_widget_create(owner->app, owner, kinds[i], names[i], (xxwidgets_rect){0, 0, 1, 1}, v->controls + i);
        if (status != XXWIDGETS_OK)
            goto fail;
    }
    status = xxwidgets_exports_set_rect(v, bounds);
    if (status == XXWIDGETS_OK) {
        xxwidgets_exports_data empty = {"", "", 0, NULL, 0};
        status = xxwidgets_exports_set_data(v, &empty);
    }
    if (status != XXWIDGETS_OK)
        goto fail;
    *out = v;
    return XXWIDGETS_OK;
fail:
    xxwidgets_exports_destroy(v);
    return status;
}
xxwidgets_status xxwidgets_exports_destroy(xxwidgets_exports *v)
{
    if (!v)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling || v->owner->app->syncing)
        return XXWIDGETS_BUSY;
    for (size_t i = 0; i < XXWIDGETS_EXPORTS_CONTROL_COUNT; ++i)
        if (v->controls[i])
            (void)xxwidgets_widget_destroy(v->controls[i]);
    free_records(v->records, v->count);
    free(v->path);
    free(v->format);
    free(v->filter);
    free(v);
    return XXWIDGETS_OK;
}
xxwidgets_widget *xxwidgets_exports_control(const xxwidgets_exports *v, xxwidgets_exports_control_id id)
{
    return v && id >= 0 && id < XXWIDGETS_EXPORTS_CONTROL_COUNT ? v->controls[id] : NULL;
}
size_t xxwidgets_exports_count(const xxwidgets_exports *v)
{
    return v ? v->count : 0;
}
size_t xxwidgets_exports_visible_count(const xxwidgets_exports *v)
{
    return v ? xxwidget_exportsviewer_count(C(v, TABLE)) : 0;
}
xxwidgets_status xxwidgets_exports_set_data(xxwidgets_exports *v, const xxwidgets_exports_data *data)
{
    xx_export_record *copy = NULL;
    char *path = NULL, *format = NULL;
    size_t total = 0;
    xxwidgets_status status = XXWIDGETS_OUT_OF_MEMORY;
    if (!v || !data || !xxwidgets_valid_utf8(data->path) || !xxwidgets_valid_utf8(data->format) || strlen(data->format) > 128 ||
        data->count > XXWIDGETS_EXPORTS_MAX_RECORDS || (data->count && !data->records))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling || v->owner->app->syncing)
        return XXWIDGETS_BUSY;
    for (size_t i = 0; i < data->count; ++i) {
        const xx_export_record *r = data->records + i;
        const char *strings[] = {r->forwarder ? r->forwarder : "", r->name ? r->name : ""};
        if (r->offset < -1 || (r->offset >= 0 && (uint64_t)r->offset >= data->file_size))
            return XXWIDGETS_INVALID_ARGUMENT;
        for (size_t j = 0; j < 2; ++j) {
            size_t n = strlen(strings[j]);
            if (n > XXWIDGETS_EXPORTS_MAX_NAME || !xxwidgets_valid_utf8(strings[j]) || n + 1 > XXWIDGETS_EXPORTS_MAX_TEXT - total)
                return XXWIDGETS_INVALID_ARGUMENT;
            total += n + 1;
        }
    }
    path = xxwidgets_strdup(data->path);
    format = xxwidgets_strdup(data->format);
    copy = data->count ? calloc(data->count, sizeof(*copy)) : NULL;
    if (!path || !format || (data->count && !copy))
        goto finish;
    for (size_t i = 0; i < data->count; ++i) {
        copy[i] = data->records[i];
        copy[i].name = xxwidgets_strdup(data->records[i].name ? data->records[i].name : "");
        copy[i].forwarder = xxwidgets_strdup(data->records[i].forwarder ? data->records[i].forwarder : "");
        if (!copy[i].name || !copy[i].forwarder)
            goto finish;
    }
    {
        xxwidgets_exports_data owned = {path, format, data->file_size, copy, data->count};
        status = present(v, &owned, "");
    }
    if (status != XXWIDGETS_OK)
        goto finish;
    free_records(v->records, v->count);
    free(v->path);
    free(v->format);
    v->records = copy;
    v->count = data->count;
    v->path = path;
    v->format = format;
    v->file_size = data->file_size;
    v->pending = 0;
    copy = NULL;
    path = format = NULL;
finish:
    free_records(copy, copy ? data->count : 0);
    free(path);
    free(format);
    return status;
}
xxwidgets_status xxwidgets_exports_set_filter(xxwidgets_exports *v, const char *filter)
{
    xxwidgets_exports_data data;
    if (!v || !xxwidgets_valid_utf8(filter) || strlen(filter) > 256)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling || v->owner->app->syncing)
        return XXWIDGETS_BUSY;
    data = (xxwidgets_exports_data){v->path, v->format, v->file_size, v->records, v->count};
    return present(v, &data, filter);
}
static size_t row_text(xxwidgets_widget *table, size_t i, char *out, size_t capacity)
{
    xx_export_record r;
    char offset[32];
    xxwidget_exportsviewer_get_record(table, i, &r);
    if (r.offset < 0)
        strcpy(offset, "-");
    else
        snprintf(offset, sizeof(offset), "0x%" PRIX64, (uint64_t)r.offset);
    return (size_t)snprintf(out, capacity, "%s\t%s\t%s\t%s\t%s\n", xxwidgets_binaryviewer_cell(table, i, 0),
                            xxwidgets_binaryviewer_cell(table, i, 1), offset, xxwidgets_binaryviewer_cell(table, i, 2),
                            xxwidgets_binaryviewer_cell(table, i, 3));
}
xxwidgets_status xxwidgets_exports_export_text(const xxwidgets_exports *v, size_t index, char *buffer, size_t capacity, size_t *required)
{
    static const char header[] = "Name\tOrdinal\tFile offset\tAddress\tForwarder\n";
    size_t count, first, last, length = sizeof(header) - 1, pos = 0;
    xxwidgets_widget *table;
    if (!v || (!buffer && capacity))
        return XXWIDGETS_INVALID_ARGUMENT;
    table = C(v, TABLE);
    count = xxwidget_exportsviewer_count(table);
    if (index != SIZE_MAX && index >= count)
        return XXWIDGETS_INVALID_ARGUMENT;
    first = index == SIZE_MAX ? 0 : index;
    last = index == SIZE_MAX ? count : index + 1;
    for (size_t i = first; i < last; ++i) {
        length += row_text(table, i, NULL, 0);
        if (length > 32U * 1024U * 1024U)
            return XXWIDGETS_UNAVAILABLE;
    }
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
        pos += row_text(table, i, buffer + pos, capacity - pos);
    buffer[pos] = 0;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_exports_handle_event(xxwidgets_exports *v, const xxwidgets_event *event)
{
    if (!v || !event)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == C(v, TABLE)) {
        size_t index;
        xx_export_record r;
        char details[256];
        xxwidgets_status status = xxwidget_exportsviewer_get_selection(C(v, TABLE), &index, &r);
        if (status != XXWIDGETS_OK)
            return status;
        describe(index == SIZE_MAX ? NULL : &r, details, sizeof(details));
        return xxwidgets_widget_set_text(C(v, DETAILS), details);
    }
    if (event->type == XXWIDGETS_EVENT_CHANGE && event->widget == C(v, FILTER))
        v->pending = FILTER;
    if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == C(v, RESET))
            v->pending = RESET;
        else if (event->widget == C(v, COPY))
            v->pending = COPY;
        else if (event->widget == C(v, COPY_ALL))
            v->pending = COPY_ALL;
    }
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_exports_process(xxwidgets_exports *v)
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
    if (action == FILTER || action == RESET) {
        char filter[257] = {0};
        if (action == FILTER)
            status = xxwidgets_widget_get_text(C(v, FILTER), filter, sizeof(filter), NULL);
        if (status == XXWIDGETS_OK)
            status = xxwidgets_exports_set_filter(v, filter);
        if (status == XXWIDGETS_OK)
            return status;
        (void)xxwidgets_widget_set_text(C(v, FILTER), v->filter ? v->filter : "");
        return xxwidgets_widget_set_text(C(v, STATUS), "Cannot apply filter (maximum 256 bytes)");
    }
    if (action == COPY || action == COPY_ALL) {
        size_t index = SIZE_MAX, required;
        char *text;
        xx_export_record r;
        if (action == COPY) {
            status = xxwidget_exportsviewer_get_selection(C(v, TABLE), &index, &r);
            if (status != XXWIDGETS_OK || index == SIZE_MAX)
                return status;
        }
        status = xxwidgets_exports_export_text(v, index, NULL, 0, &required);
        if (status != XXWIDGETS_OK)
            return xxwidgets_widget_set_text(C(v, STATUS), "Cannot copy that many exports");
        text = malloc(required);
        if (!text)
            return XXWIDGETS_OUT_OF_MEMORY;
        status = xxwidgets_exports_export_text(v, index, text, required, NULL);
        if (status == XXWIDGETS_OK)
            status = xxwidgets_clipboard_set_text(v->owner, text);
        free(text);
        return xxwidgets_widget_set_text(C(v, STATUS), status == XXWIDGETS_OK ? "Copied" : "Clipboard unavailable");
    }
    return status;
}
