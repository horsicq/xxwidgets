/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_hash.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct xxwidgets_hash {
    xxwidgets_widget *owner, *controls[XXWIDGETS_HASH_CONTROL_COUNT];
    char *path, *summary;
    xxwidgets_hash_entry *entries;
    size_t count;
};
#define CONTROL(v, id) ((v)->controls[XXWIDGETS_HASH_##id])
static void free_entries(xxwidgets_hash_entry *entries, size_t count)
{
    for (size_t i = 0; i < count; ++i) { free((void *)entries[i].algorithm); free((void *)entries[i].value); }
    free(entries);
}
static int valid_bounds(xxwidgets_rect b)
{
    return b.x >= 0 && b.y >= 0 && b.width >= 72 && b.height >= 12 && b.x <= 32767 - b.width && b.y <= 32767 - b.height;
}
xxwidgets_status xxwidgets_hash_set_rect(xxwidgets_hash *v, xxwidgets_rect b)
{
    xxwidgets_rect r[XXWIDGETS_HASH_CONTROL_COUNT], old[XXWIDGETS_HASH_CONTROL_COUNT];
    if (!v || !valid_bounds(b)) return XXWIDGETS_INVALID_ARGUMENT;
    r[0] = (xxwidgets_rect){b.x, b.y, b.width, 1};
    r[1] = (xxwidgets_rect){b.x, b.y + 2, b.width, 1};
    r[2] = (xxwidgets_rect){b.x, b.y + 4, b.width, b.height - 10};
    r[3] = (xxwidgets_rect){b.x, b.y + b.height - 5, b.width, 1};
    r[4] = (xxwidgets_rect){b.x, b.y + b.height - 3, 18, 2};
    r[5] = (xxwidgets_rect){b.x + 20, b.y + b.height - 3, 14, 2};
    r[6] = (xxwidgets_rect){b.x + 36, b.y + b.height - 3, b.width - 36, 2};
    for (size_t i = 0; i < XXWIDGETS_HASH_CONTROL_COUNT; ++i) old[i] = v->controls[i]->rect;
    for (size_t i = 0; i < XXWIDGETS_HASH_CONTROL_COUNT; ++i) {
        xxwidgets_status status = xxwidgets_widget_set_rect(v->controls[i], r[i]);
        if (status != XXWIDGETS_OK) {
            for (size_t j = 0; j < i; ++j) (void)xxwidgets_widget_set_rect(v->controls[j], old[j]);
            return status;
        }
    }
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_hash_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_hash **out)
{
    xxwidgets_hash *v;
    xxwidgets_status status;
    static const xxwidgets_kind kinds[] = {XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_TREEVIEW, XXWIDGETS_LABEL,
        XXWIDGETS_BUTTON, XXWIDGETS_BUTTON, XXWIDGETS_LABEL};
    static const char *const text[] = {"", "", "Hashes", "", "Copy selected", "Copy all", "Select a hash"};
    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !valid_bounds(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing) return XXWIDGETS_BUSY;
    v = calloc(1, sizeof(*v));
    if (!v) return XXWIDGETS_OUT_OF_MEMORY;
    v->owner = owner;
    for (size_t i = 0; i < XXWIDGETS_HASH_CONTROL_COUNT; ++i) {
        status = xxwidgets_widget_create(owner->app, owner, kinds[i], text[i], (xxwidgets_rect){0, 0, 1, 1}, &v->controls[i]);
        if (status != XXWIDGETS_OK) goto fail;
    }
    status = xxwidgets_hash_set_rect(v, bounds);
    if (status != XXWIDGETS_OK) goto fail;
    status = xxwidgets_hash_set_data(v, "", 0, NULL, 0);
    if (status != XXWIDGETS_OK) goto fail;
    *out = v; return XXWIDGETS_OK;
fail:
    xxwidgets_hash_destroy(v); return status;
}
xxwidgets_status xxwidgets_hash_destroy(xxwidgets_hash *v)
{
    if (!v) return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling || v->owner->app->syncing) return XXWIDGETS_BUSY;
    for (size_t i = 0; i < XXWIDGETS_HASH_CONTROL_COUNT; ++i) if (v->controls[i]) xxwidgets_widget_destroy(v->controls[i]);
    free_entries(v->entries, v->count); free(v->path); free(v->summary); free(v);
    return XXWIDGETS_OK;
}
xxwidgets_widget *xxwidgets_hash_control(const xxwidgets_hash *v, xxwidgets_hash_control_id id)
{
    return v && id >= 0 && id < XXWIDGETS_HASH_CONTROL_COUNT ? v->controls[id] : NULL;
}
size_t xxwidgets_hash_count(const xxwidgets_hash *v) { return v ? v->count : 0; }
xxwidgets_status xxwidgets_hash_get_entry(const xxwidgets_hash *v, size_t index, xxwidgets_hash_entry *entry)
{
    if (!v || !entry || index >= v->count) return XXWIDGETS_INVALID_ARGUMENT;
    *entry = v->entries[index]; return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_hash_set_data(xxwidgets_hash *v, const char *path, uint64_t size,
    const xxwidgets_hash_entry *entries, size_t count)
{
    xxwidgets_hash_entry *copy = NULL;
    xxwidgets_tree_node nodes[64];
    char *rows[64] = {0}, *new_path = NULL, *summary = NULL;
    char old_value[513];
    xxwidgets_status status = XXWIDGETS_OUT_OF_MEMORY;
    if (!v || !path || !xxwidgets_valid_utf8(path) || count > 64 || (count && !entries)) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i) {
        const char *name = entries[i].algorithm, *value = entries[i].value;
        size_t length;
        if (!name || !value || !*name || strlen(name) > 64 || !(length = strlen(value)) || length > 512 || length % 2)
            return XXWIDGETS_INVALID_ARGUMENT;
        for (const unsigned char *p = (const unsigned char *)name; *p; ++p) if (*p < 32 || *p >= 127) return XXWIDGETS_INVALID_ARGUMENT;
        for (const char *p = value; *p; ++p) if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')))
            return XXWIDGETS_INVALID_ARGUMENT;
    }
    new_path = xxwidgets_strdup(path); summary = malloc(128); copy = count ? calloc(count, sizeof(*copy)) : NULL;
    if (!new_path || !summary || (count && !copy)) goto finish;
    snprintf(summary, 128, "%" PRIu64 " bytes | %zu hashes | Whole file", size, count);
    for (size_t i = 0; i < count; ++i) {
        size_t capacity = strlen(entries[i].algorithm) + strlen(entries[i].value) + 12;
        copy[i].algorithm = xxwidgets_strdup(entries[i].algorithm); copy[i].value = xxwidgets_strdup(entries[i].value);
        rows[i] = malloc(capacity);
        if (!copy[i].algorithm || !copy[i].value || !rows[i]) goto finish;
        snprintf(rows[i], capacity, "%-8s  %s", entries[i].algorithm, entries[i].value);
        nodes[i] = (xxwidgets_tree_node){SIZE_MAX, rows[i], 0};
    }
    status = xxwidgets_widget_get_text(CONTROL(v, VALUE), old_value, sizeof(old_value), NULL);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_widget_set_text(CONTROL(v, PATH), new_path);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(CONTROL(v, SUMMARY), summary);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(CONTROL(v, VALUE), count ? copy[0].value : "");
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(CONTROL(v, COPY), count != 0);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(CONTROL(v, COPY_ALL), count != 0);
    if (status == XXWIDGETS_OK) status = xxwidgets_treeview_set_nodes(CONTROL(v, LIST), nodes, count);
    if (status != XXWIDGETS_OK) {
        (void)xxwidgets_widget_set_text(CONTROL(v, PATH), v->path ? v->path : "");
        (void)xxwidgets_widget_set_text(CONTROL(v, SUMMARY), v->summary ? v->summary : "");
        (void)xxwidgets_widget_set_text(CONTROL(v, VALUE), old_value);
        (void)xxwidgets_widget_set_enabled(CONTROL(v, COPY), v->count != 0);
        (void)xxwidgets_widget_set_enabled(CONTROL(v, COPY_ALL), v->count != 0);
        goto finish;
    }
    free_entries(v->entries, v->count); free(v->path); free(v->summary);
    v->entries = copy; v->count = count; v->path = new_path; v->summary = summary;
    copy = NULL; new_path = summary = NULL;
    (void)xxwidgets_widget_set_text(CONTROL(v, STATUS), count ? "Select a hash" : "No hashes");
finish:
    for (size_t i = 0; i < count; ++i) free(rows[i]);
    free_entries(copy, copy ? count : 0); free(new_path); free(summary);
    return status;
}
xxwidgets_status xxwidgets_hash_export_text(const xxwidgets_hash *v, char *buffer, size_t capacity, size_t *required)
{
    size_t length = 0;
    if (!v || (!buffer && capacity)) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < v->count; ++i) length += strlen(v->entries[i].algorithm) + strlen(v->entries[i].value) + 3;
    if (required) *required = length + 1;
    if (!buffer && !capacity) return XXWIDGETS_OK;
    if (capacity <= length) { if (capacity) buffer[0] = 0; return XXWIDGETS_BUFFER_TOO_SMALL; }
    for (size_t i = 0, pos = 0; i < v->count; ++i)
        pos += (size_t)snprintf(buffer + pos, capacity - pos, "%s: %s\n", v->entries[i].algorithm, v->entries[i].value);
    if (!v->count) buffer[0] = 0;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_hash_handle_event(xxwidgets_hash *v, const xxwidgets_event *event)
{
    size_t index;
    xxwidgets_tree_node node;
    xxwidgets_status status;
    if (!v || !event) return XXWIDGETS_INVALID_ARGUMENT;
    if (event->widget == CONTROL(v, LIST) && event->type == XXWIDGETS_EVENT_SELECT) {
        status = xxwidgets_treeview_get_selection(CONTROL(v, LIST), &index, &node);
        if (status != XXWIDGETS_OK) return status;
        return xxwidgets_widget_set_text(CONTROL(v, VALUE), index < v->count ? v->entries[index].value : "");
    }
    if (event->type == XXWIDGETS_EVENT_CLICK && (event->widget == CONTROL(v, COPY) || event->widget == CONTROL(v, COPY_ALL))) {
        char *text = NULL;
        if (event->widget == CONTROL(v, COPY)) {
            status = xxwidgets_treeview_get_selection(CONTROL(v, LIST), &index, &node);
            if (status != XXWIDGETS_OK) return status;
            if (index >= v->count) return XXWIDGETS_OK;
            status = xxwidgets_clipboard_set_text(v->owner, v->entries[index].value);
        } else {
            size_t required;
            xxwidgets_hash_export_text(v, NULL, 0, &required);
            text = malloc(required);
            if (!text) return XXWIDGETS_OUT_OF_MEMORY;
            status = xxwidgets_hash_export_text(v, text, required, NULL);
            if (status == XXWIDGETS_OK) status = xxwidgets_clipboard_set_text(v->owner, text);
            free(text);
        }
        return xxwidgets_widget_set_text(CONTROL(v, STATUS), status == XXWIDGETS_OK ? "Copied" : "Clipboard unavailable");
    }
    return XXWIDGETS_OK;
}
