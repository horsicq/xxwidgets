/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_strings.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct xxwidgets_strings {
    xxwidgets_widget *owner, *controls[XXWIDGETS_STRINGS_CONTROL_COUNT];
    xxwidgets_string_record *records;
    size_t count, visible_count, *visible;
    char *filter;
};
#define CONTROL(v, id) ((v)->controls[XXWIDGETS_STRINGS_##id])

static xxwidgets_status sync_control(xxwidgets_widget *widget)
{
    xxwidgets_status status;
    ++widget->app->syncing;
    status = widget->app->ops->sync(widget);
    --widget->app->syncing;
    return status;
}

static void free_records(xxwidgets_string_record *records, size_t count)
{
    if (records)
        for (size_t i = 0; i < count; ++i) {
            free((void *)records[i].encoding);
            free((void *)records[i].text);
        }
    free(records);
}

static void free_rows(char **rows, size_t count)
{
    if (rows)
        for (size_t i = 0; i < count; ++i)
            free(rows[i]);
    free(rows);
}

static void free_entries(xxwidgets_archive_entry *entries, size_t count)
{
    if (entries)
        for (size_t i = 0; i < count; ++i) free((void *)entries[i].path);
    free(entries);
}

static int matches(const char *text, const char *filter)
{
    size_t n = strlen(filter);
    if (!n) return 1;
    for (; *text; ++text) {
        size_t i;
        for (i = 0; i < n && text[i]; ++i) {
            unsigned char a = (unsigned char)text[i], b = (unsigned char)filter[i];
            if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
            if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
            if (a != b) break;
        }
        if (i == n) return 1;
    }
    return 0;
}

/* Keep result rows bounded and single-line even for megabyte strings. The
 * model, clipboard and CSV retain the complete original decoded text. */
static void preview(char *buffer, const char *text, size_t limit)
{
    static const char digits[] = "0123456789ABCDEF";
    size_t length = 0, characters = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p && characters++ < limit) {
        size_t bytes = *p < 0x80 ? 1 : *p < 0xe0 ? 2 : *p < 0xf0 ? 3 : 4;
        if (*p < 32 || *p == 127) {
            buffer[length++] = '\\'; buffer[length++] = 'x';
            buffer[length++] = digits[*p >> 4]; buffer[length++] = digits[*p & 15];
        } else {
            memcpy(buffer + length, p, bytes);
            length += bytes;
        }
        p += bytes;
    }
    if (*p) { memcpy(buffer + length, "...", 3); length += 3; }
    buffer[length] = 0;
}

static void describe(const xxwidgets_app *app, const xxwidgets_string_record *record, char *buffer, size_t size)
{
    if (!record) snprintf(buffer, size, "%s", XXW_TR(app, "No string selected"));
    else {
        char encoding[4 * 24 + 4];
        preview(encoding, record->encoding, 24);
        snprintf(buffer, size, "Offset 0x%" PRIX64 " | Bytes %" PRIu64 " | Characters %" PRIu64 " | %s",
                 record->offset, record->byte_size, record->character_length, encoding);
    }
}

/* Commit all presentation fields together, rolling them back on any backend
 * error. Models/mappings are swapped only after this succeeds. */
static xxwidgets_status replace_rows(xxwidgets_strings *v, const xxwidgets_string_record *records, size_t count,
                                    const char *filter, size_t keep, size_t **out_visible, size_t *out_count)
{
    char **rows = count ? calloc(count, sizeof(*rows)) : NULL;
    xxwidgets_archive_entry *entries = count ? calloc(count, sizeof(*entries)) : NULL;
    size_t *visible = count ? malloc(count * sizeof(*visible)) : NULL, used = 0;
    size_t columns = 0;
    char *texts[3] = {NULL, NULL, NULL}, *old_texts[3];
    xxwidgets_widget *labels[] = {CONTROL(v, FILTER), CONTROL(v, COUNT), CONTROL(v, DETAILS)};
    xxwidgets_widget *buttons[] = {CONTROL(v, COPY), CONTROL(v, VIEW), CONTROL(v, SAVE)};
    int previous_enabled[3], selected = -1;
    xxwidgets_status status = XXWIDGETS_OK;
    if (count && (!rows || !visible || !entries)) { status = XXWIDGETS_OUT_OF_MEMORY; goto fail; }
    for (size_t i = 0; i < count; ++i) {
        char text[4 * 160 + 4], encoding[4 * 12 + 4], row[900];
        if (!matches(records[i].text, filter) && !matches(records[i].encoding, filter)) continue;
        preview(text, records[i].text, 160);
        preview(encoding, records[i].encoding, 12);
        snprintf(row, sizeof(row), "%016" PRIX64 "  %8" PRIu64 "  %8" PRIu64 "  %-12s  %s",
                 records[i].offset, records[i].byte_size, records[i].character_length, encoding, text);
        rows[used] = xxwidgets_strdup(row);
        if (!rows[used]) { status = XXWIDGETS_OUT_OF_MEMORY; goto fail; }
        entries[used].path = xxwidgets_strdup(row);
        entries[used].size = records[i].byte_size;
        if (!entries[used].path) { free(rows[used]); rows[used] = NULL; status = XXWIDGETS_OUT_OF_MEMORY; goto fail; }
        if (strlen(row) > columns) columns = strlen(row);
        if (i == keep) selected = (int)used;
        visible[used++] = i;
    }
    if (selected < 0 && used) selected = 0;
    {
        char count_text[96], detail_text[256];
        snprintf(count_text, sizeof(count_text), "%zu / %zu %s", used, count, xxwidgets_app_ngettext(v->owner->app, "string", "strings", count));
        describe(v->owner->app, selected < 0 ? NULL : records + visible[selected], detail_text, sizeof(detail_text));
        texts[0] = xxwidgets_strdup(filter);
        texts[1] = xxwidgets_strdup(count_text);
        texts[2] = xxwidgets_strdup(detail_text);
    }
    if (!texts[0] || !texts[1] || !texts[2]) { status = XXWIDGETS_OUT_OF_MEMORY; goto fail; }
    xxwidgets_widget *table = CONTROL(v, RESULTS);
    char **old_rows = table->items;
    xxwidgets_archive_entry *old_entries = table->archive_entries;
    size_t old_count = table->item_count;
    size_t old_columns = table->archive_columns;
    int old_value = table->value;
    uint64_t old_revision = table->archive_revision;
    table->items = rows; table->item_count = used; table->value = selected;
    table->archive_entries = entries; table->archive_columns = columns;
    table->archive_revision = old_revision + 1;
    for (size_t i = 0; i < 3; ++i) {
        old_texts[i] = labels[i]->text; labels[i]->text = texts[i];
        previous_enabled[i] = buttons[i]->enabled;
        buttons[i]->enabled = i == 2 ? used != 0 : selected >= 0;
    }
    status = sync_control(table);
    for (size_t i = 0; i < 3 && status == XXWIDGETS_OK; ++i) status = sync_control(labels[i]);
    for (size_t i = 0; i < 3 && status == XXWIDGETS_OK; ++i) status = sync_control(buttons[i]);
    if (status != XXWIDGETS_OK) {
        table->items = old_rows; table->item_count = old_count; table->value = old_value;
        table->archive_entries = old_entries; table->archive_columns = old_columns;
        table->archive_revision = old_revision;
        for (size_t i = 0; i < 3; ++i) {
            labels[i]->text = old_texts[i]; buttons[i]->enabled = previous_enabled[i];
        }
        (void)sync_control(table);
        for (size_t i = 0; i < 3; ++i) { (void)sync_control(labels[i]); (void)sync_control(buttons[i]); }
        goto fail;
    }
    free_rows(old_rows, old_count);
    free_entries(old_entries, old_count);
    for (size_t i = 0; i < 3; ++i) free(old_texts[i]);
    *out_visible = visible; *out_count = used;
    return XXWIDGETS_OK;
fail:
    free_rows(rows, used); free_entries(entries, used); free(visible);
    for (size_t i = 0; i < 3; ++i) free(texts[i]);
    return status;
}

xxwidgets_widget *xxwidgets_strings_control(const xxwidgets_strings *v, xxwidgets_strings_control_id id)
{
    return v && id >= 0 && id < XXWIDGETS_STRINGS_CONTROL_COUNT ? v->controls[id] : NULL;
}

xxwidgets_status xxwidgets_strings_set_rect(xxwidgets_strings *v, xxwidgets_rect b)
{
    xxwidgets_rect rects[XXWIDGETS_STRINGS_CONTROL_COUNT], previous[XXWIDGETS_STRINGS_CONTROL_COUNT];
    if (!v || b.x < 0 || b.y < 0 || b.width < 72 || b.height < 14 || b.x > 32767 - b.width || b.y > 32767 - b.height)
        return XXWIDGETS_INVALID_ARGUMENT;
#define R(id, dx, dy, w, h) rects[XXWIDGETS_STRINGS_##id] = (xxwidgets_rect){b.x + (dx), b.y + (dy), (w), (h)}
    R(FILTER_LABEL, 0, 0, 7, 2);
    R(FILTER, 8, 0, b.width - 8, 2);
    R(HEADER, 0, 3, b.width, 1);
    R(RESULTS, 0, 4, b.width, b.height - 9);
    R(DETAILS, 0, b.height - 4, b.width, 1);
    R(COUNT, 0, b.height - 2, b.width - 39, 2);
    R(COPY, b.width - 38, b.height - 2, 12, 2);
    R(VIEW, b.width - 25, b.height - 2, 12, 2);
    R(SAVE, b.width - 12, b.height - 2, 12, 2);
#undef R
    for (size_t i = 0; i < XXWIDGETS_STRINGS_CONTROL_COUNT; ++i) previous[i] = v->controls[i]->rect;
    for (size_t i = 0; i < XXWIDGETS_STRINGS_CONTROL_COUNT; ++i) {
        xxwidgets_status status = xxwidgets_widget_set_rect(v->controls[i], rects[i]);
        if (status != XXWIDGETS_OK) {
            for (size_t j = 0; j < i; ++j) (void)xxwidgets_widget_set_rect(v->controls[j], previous[j]);
            return status;
        }
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_strings_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_strings **out)
{
    static const xxwidgets_kind kinds[] = {XXWIDGETS_LABEL, XXWIDGETS_EDIT, XXWIDGETS_LABEL, XXWIDGETS_ARCHIVEVIEW,
        XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_BUTTON, XXWIDGETS_BUTTON, XXWIDGETS_BUTTON};
    static const char *names[] = {XXW_N_("Filter"), "", XXW_N_("Offset (hex)         Bytes     Chars  Encoding      String"), "",
        XXW_N_("No string selected"), XXW_N_("0 / 0 strings"), XXW_N_("Copy"), XXW_N_("View"), XXW_N_("Save CSV")};
    xxwidgets_status status;
    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW) return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling) return XXWIDGETS_BUSY;
    xxwidgets_strings *v = calloc(1, sizeof(*v));
    if (!v) return XXWIDGETS_OUT_OF_MEMORY;
    v->owner = owner; v->filter = xxwidgets_strdup("");
    if (!v->filter) { free(v); return XXWIDGETS_OUT_OF_MEMORY; }
    for (size_t i = 0; i < XXWIDGETS_STRINGS_CONTROL_COUNT; ++i) {
        status = xxwidgets_widget_create(owner->app, owner, kinds[i], XXW_TR(owner->app, names[i]), (xxwidgets_rect){0, 0, 1, 1}, v->controls + i);
        if (status != XXWIDGETS_OK) goto fail;
    }
    status = xxwidgets_strings_set_rect(v, bounds);
    if (status != XXWIDGETS_OK) goto fail;
    status = xxwidgets_strings_set_records(v, NULL, 0);
    if (status != XXWIDGETS_OK) goto fail;
    *out = v;
    return XXWIDGETS_OK;
fail:
    (void)xxwidgets_strings_destroy(v);
    return status;
}

xxwidgets_status xxwidgets_strings_destroy(xxwidgets_strings *v)
{
    if (!v) return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling) return XXWIDGETS_BUSY;
    for (size_t i = 0; i < XXWIDGETS_STRINGS_CONTROL_COUNT; ++i)
        if (v->controls[i]) (void)xxwidgets_widget_destroy(v->controls[i]);
    free_records(v->records, v->count);
    free(v->visible); free(v->filter); free(v);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_strings_set_records(xxwidgets_strings *v, const xxwidgets_string_record *records, size_t count)
{
    xxwidgets_string_record *copies = NULL;
    size_t *visible = NULL, used = 0;
    if (!v || (!records && count) || count > INT_MAX || count > SIZE_MAX / sizeof(*copies) || count > SIZE_MAX / sizeof(char *) ||
        count > SIZE_MAX / sizeof(size_t)) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i)
        if (!xxwidgets_valid_utf8(records[i].text) || !xxwidgets_valid_utf8(records[i].encoding) || !records[i].encoding[0] ||
            records[i].byte_size > UINT64_MAX - records[i].offset) return XXWIDGETS_INVALID_ARGUMENT;
    if (count) {
        copies = calloc(count, sizeof(*copies));
        if (!copies) return XXWIDGETS_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < count; ++i) {
        copies[i] = records[i];
        copies[i].encoding = xxwidgets_strdup(records[i].encoding);
        copies[i].text = xxwidgets_strdup(records[i].text);
        if (!copies[i].encoding || !copies[i].text) { free_records(copies, i + 1); return XXWIDGETS_OUT_OF_MEMORY; }
    }
    xxwidgets_status status = replace_rows(v, copies, count, v->filter, SIZE_MAX, &visible, &used);
    if (status != XXWIDGETS_OK) { free_records(copies, count); return status; }
    free_records(v->records, v->count); free(v->visible);
    v->records = copies; v->count = count; v->visible = visible; v->visible_count = used;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_strings_set_filter(xxwidgets_strings *v, const char *filter)
{
    size_t *visible = NULL, used = 0, keep = SIZE_MAX;
    if (!v || !xxwidgets_valid_utf8(filter)) return XXWIDGETS_INVALID_ARGUMENT;
    char *copy = xxwidgets_strdup(filter);
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    int selected = CONTROL(v, RESULTS)->value;
    if (selected >= 0 && (size_t)selected < v->visible_count) keep = v->visible[selected];
    xxwidgets_status status = replace_rows(v, v->records, v->count, copy, keep, &visible, &used);
    if (status != XXWIDGETS_OK) { free(copy); return status; }
    free(v->filter); free(v->visible);
    v->filter = copy; v->visible = visible; v->visible_count = used;
    return XXWIDGETS_OK;
}

size_t xxwidgets_strings_count(const xxwidgets_strings *v) { return v ? v->count : 0; }
size_t xxwidgets_strings_visible_count(const xxwidgets_strings *v) { return v ? v->visible_count : 0; }

xxwidgets_status xxwidgets_strings_get_record(const xxwidgets_strings *v, size_t index, xxwidgets_string_record *record)
{
    if (!v || !record || index >= v->count) return XXWIDGETS_INVALID_ARGUMENT;
    *record = v->records[index];
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_strings_get_visible_record(const xxwidgets_strings *v, size_t row, size_t *index,
                                                     xxwidgets_string_record *record)
{
    if (!v || !index || !record || row >= v->visible_count) return XXWIDGETS_INVALID_ARGUMENT;
    *index = v->visible[row]; *record = v->records[*index];
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_strings_get_selection(xxwidgets_strings *v, size_t *index, xxwidgets_string_record *record)
{
    int value;
    if (!v || !index || !record) return XXWIDGETS_INVALID_ARGUMENT;
    xxwidgets_status status = xxwidgets_widget_get_value(CONTROL(v, RESULTS), &value);
    if (status != XXWIDGETS_OK) return status;
    if (value < 0 || (size_t)value >= v->visible_count) { *index = SIZE_MAX; memset(record, 0, sizeof(*record)); return XXWIDGETS_OK; }
    return xxwidgets_strings_get_visible_record(v, (size_t)value, index, record);
}

xxwidgets_status xxwidgets_strings_copy_selection(xxwidgets_strings *v)
{
    size_t index;
    xxwidgets_string_record record;
    xxwidgets_status status = xxwidgets_strings_get_selection(v, &index, &record);
    if (status != XXWIDGETS_OK) return status;
    return index == SIZE_MAX ? XXWIDGETS_INVALID_ARGUMENT : xxwidgets_clipboard_set_text(v->owner, record.text);
}

xxwidgets_status xxwidgets_strings_handle_event(xxwidgets_strings *v, const xxwidgets_event *event)
{
    if (!v || !event) return XXWIDGETS_INVALID_ARGUMENT;
    if (event->widget == CONTROL(v, FILTER) && event->type == XXWIDGETS_EVENT_CHANGE)
        return xxwidgets_strings_set_filter(v, CONTROL(v, FILTER)->text);
    if (event->widget == CONTROL(v, COPY) && event->type == XXWIDGETS_EVENT_CLICK) {
        xxwidgets_status status = xxwidgets_strings_copy_selection(v);
        if (status == XXWIDGETS_UNAVAILABLE)
            return xxwidgets_widget_set_text(CONTROL(v, DETAILS), "Clipboard unavailable");
        return status;
    }
    if (event->widget == CONTROL(v, RESULTS) && (event->type == XXWIDGETS_EVENT_SELECT || event->type == XXWIDGETS_EVENT_ACTIVATE)) {
        char text[256];
        if (event->value < 0 || (size_t)event->value >= v->visible_count) return XXWIDGETS_INVALID_ARGUMENT;
        describe(v->owner->app, v->records + v->visible[event->value], text, sizeof(text));
        return xxwidgets_widget_set_text(CONTROL(v, DETAILS), text);
    }
    return XXWIDGETS_OK;
}

typedef struct csv_output { char *buffer; size_t capacity, length; int overflow; } csv_output;
static void csv_append(csv_output *out, const char *text)
{
    size_t n = strlen(text);
    if (n >= SIZE_MAX - out->length) { out->overflow = 1; return; }
    if (out->buffer && out->length < out->capacity) {
        size_t available = out->capacity - out->length - 1;
        size_t copied = n < available ? n : available;
        memcpy(out->buffer + out->length, text, copied);
    }
    out->length += n;
}
static void csv_cell(csv_output *out, const char *text)
{
    csv_append(out, "\"");
    for (; *text; ++text) {
        char byte[] = {*text, 0};
        if (*text == '"') csv_append(out, "\"");
        csv_append(out, byte);
    }
    csv_append(out, "\"");
}

xxwidgets_status xxwidgets_strings_export_csv(const xxwidgets_strings *v, char *buffer, size_t capacity, size_t *required)
{
    if (!v || (!buffer && capacity)) return XXWIDGETS_INVALID_ARGUMENT;
    csv_output out = {buffer, capacity, 0, 0};
    csv_append(&out, "Offset,Bytes,Characters,Encoding,String\r\n");
    for (size_t i = 0; i < v->visible_count; ++i) {
        const xxwidgets_string_record *r = v->records + v->visible[i];
        char numbers[96];
        snprintf(numbers, sizeof(numbers), "0x%016" PRIX64 ",%" PRIu64 ",%" PRIu64 ",", r->offset, r->byte_size, r->character_length);
        csv_append(&out, numbers); csv_cell(&out, r->encoding); csv_append(&out, ","); csv_cell(&out, r->text); csv_append(&out, "\r\n");
    }
    if (buffer && capacity) buffer[out.length < capacity ? out.length : capacity - 1] = 0;
    if (out.overflow) return XXWIDGETS_OUT_OF_MEMORY;
    if (required) *required = out.length + 1;
    return buffer && capacity <= out.length ? XXWIDGETS_BUFFER_TOO_SMALL : XXWIDGETS_OK;
}
