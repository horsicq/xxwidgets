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

/* RESULTS: #, Offset, Address, Region, Size, Type, String. */
#define COLUMNS 7

struct xxwidgets_strings {
    xxwidgets_widget *owner, *controls[XXWIDGETS_STRINGS_CONTROL_COUNT];
    xxwidgets_string_record *records;
    uint64_t *numbers;
    /* The records FILTER lets through: the rows of RESULTS. */
    size_t count, matched_count, *matched;
    char *filter;
};
#define CONTROL(v, id) ((v)->controls[XXWIDGETS_STRINGS_##id])

static void free_records(xxwidgets_string_record *records, size_t count)
{
    if (records)
        for (size_t i = 0; i < count; ++i) {
            free((void *)records[i].encoding);
            free((void *)records[i].text);
            free((void *)records[i].region);
        }
    free(records);
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
        char encoding[4 * 24 + 4], region[4 * 24 + 4], address[64] = "";
        preview(encoding, record->encoding, 24);
        preview(region, record->region ? record->region : "", 24);
        if (record->has_address) snprintf(address, sizeof(address), " | Address 0x%" PRIX64, record->address);
        snprintf(buffer, size, "Offset 0x%" PRIX64 "%s%s%s | Bytes %" PRIu64 " | Characters %" PRIu64 " | %s",
                 record->offset, address, region[0] ? " | " : "", region, record->byte_size, record->character_length, encoding);
    }
}

/* Hex digits for the numbers of a column: 8, or 16 once one needs them. */
static int digits(uint64_t largest) { return largest > UINT32_MAX ? 16 : 8; }

static void free_cells(char **cells, size_t rows)
{
    /* Each row is one allocation that its first cell points to. */
    if (cells) for (size_t i = 0; i < rows; ++i) free(cells[i * COLUMNS]);
    free(cells);
}

/* The cells of the records the filter lets through, and their indexes. */
static xxwidgets_status build_rows(const xxwidgets_string_record *records, const uint64_t *numbers, size_t count, const char *filter,
                                  char ***out_cells, size_t **out_matched, size_t *out_used)
{
    char **cells = count ? calloc(count, COLUMNS * sizeof(char *)) : NULL;
    size_t *matched = count ? malloc(count * sizeof(*matched)) : NULL, used = 0;
    uint64_t offsets = 0, addresses = 0, sizes = 0;
    *out_cells = NULL; *out_matched = NULL; *out_used = 0;
    if (count && (!cells || !matched)) { free(cells); free(matched); return XXWIDGETS_OUT_OF_MEMORY; }
    for (size_t i = 0; i < count; ++i) {
        if (records[i].offset > offsets) offsets = records[i].offset;
        if (records[i].has_address && records[i].address > addresses) addresses = records[i].address;
        if (records[i].byte_size > sizes) sizes = records[i].byte_size;
    }
    for (size_t i = 0; i < count; ++i) {
        const xxwidgets_string_record *r = records + i;
        char number[24], offset[24], address[24] = "", size[24], text[4 * 256 + 4], encoding[4 * 24 + 4];
        const char *parts[COLUMNS];
        size_t lengths[COLUMNS], total = 0;
        char *row;
        if (!matches(r->text, filter) && !matches(r->encoding, filter)) continue;
        snprintf(number, sizeof(number), "%" PRIu64, numbers ? numbers[i] : (uint64_t)i);
        snprintf(offset, sizeof(offset), "%0*" PRIX64, digits(offsets), r->offset);
        if (r->has_address) snprintf(address, sizeof(address), "%0*" PRIX64, digits(addresses), r->address);
        snprintf(size, sizeof(size), "%0*" PRIX64, digits(sizes), r->byte_size);
        preview(text, r->text, 256);
        preview(encoding, r->encoding, 24);
        parts[0] = number; parts[1] = offset; parts[2] = address; parts[3] = r->region ? r->region : "";
        parts[4] = size; parts[5] = encoding; parts[6] = text;
        for (size_t c = 0; c < COLUMNS; ++c) total += (lengths[c] = strlen(parts[c])) + 1;
        row = malloc(total);
        if (!row) { free_cells(cells, used); free(matched); return XXWIDGETS_OUT_OF_MEMORY; }
        for (size_t c = 0, at = 0; c < COLUMNS; ++c) {
            memcpy(row + at, parts[c], lengths[c] + 1);
            cells[used * COLUMNS + c] = row + at;
            at += lengths[c] + 1;
        }
        matched[used++] = i;
    }
    *out_cells = cells; *out_matched = matched; *out_used = used;
    return XXWIDGETS_OK;
}

static int selected_index(const xxwidgets_strings *v)
{
    int value = CONTROL(v, RESULTS)->value;
    return value >= 0 && (size_t)value < v->matched_count ? value : -1;
}

/* Count, details and buttons for the rows RESULTS shows now. */
static xxwidgets_status update_labels(xxwidgets_strings *v)
{
    xxwidgets_widget *table = CONTROL(v, RESULTS);
    size_t shown = xxwidgets_table_visible_count(table);
    int selected = selected_index(v);
    char count_text[96], detail_text[384];
    xxwidgets_status status;
    snprintf(count_text, sizeof(count_text), "%zu / %zu %s", shown, v->count, xxwidgets_app_ngettext(v->owner->app, "string", "strings", v->count));
    describe(v->owner->app, selected < 0 ? NULL : v->records + v->matched[selected], detail_text, sizeof(detail_text));
    status = xxwidgets_widget_set_text(CONTROL(v, COUNT), count_text);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(CONTROL(v, DETAILS), detail_text);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(CONTROL(v, COPY), selected >= 0);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(CONTROL(v, VIEW), selected >= 0);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(CONTROL(v, SAVE), shown != 0);
    return status;
}

/* Shows the records filter lets through, selecting record keep when shown.
 * On failure the rows shown before stay. */
static xxwidgets_status replace_rows(xxwidgets_strings *v, const xxwidgets_string_record *records, const uint64_t *numbers, size_t count,
                                    const char *filter, size_t keep, size_t **out_matched, size_t *out_count)
{
    char **cells = NULL;
    size_t *matched = NULL, used = 0;
    int selection = -1;
    xxwidgets_status status = build_rows(records, numbers, count, filter, &cells, &matched, &used);
    if (status != XXWIDGETS_OK) return status;
    for (size_t i = 0; i < used && keep != SIZE_MAX; ++i)
        if (matched[i] == keep) { selection = (int)i; break; }
    status = xxwidgets_text_table_replace(CONTROL(v, RESULTS), (const char *const *)cells, used, selection < 0 ? 0 : selection);
    free_cells(cells, used);
    if (status != XXWIDGETS_OK) { free(matched); return status; }
    *out_matched = matched; *out_count = used;
    return XXWIDGETS_OK;
}

/* Puts the rows of the current model back after a failure further on. */
static void restore_rows(xxwidgets_strings *v)
{
    size_t *matched = NULL, used = 0;
    int selected = selected_index(v);
    if (replace_rows(v, v->records, v->numbers, v->count, v->filter, selected < 0 ? SIZE_MAX : v->matched[selected], &matched, &used) == XXWIDGETS_OK)
        free(matched);
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
    /* Hidden: the table has its own titles. */
    R(HEADER, 0, 0, b.width, 1);
    R(RESULTS, 0, 0, b.width, b.height - 5);
    R(DETAILS, 0, b.height - 4, b.width, 1);
    R(COUNT, 0, b.height - 2, b.width - 45, 2);
    R(COPY, b.width - 44, b.height - 2, 14, 2);
    R(VIEW, b.width - 29, b.height - 2, 14, 2);
    R(SAVE, b.width - 14, b.height - 2, 14, 2);
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

xxwidgets_status xxwidgets_strings_create_internal(xxwidgets_widget *owner, xxwidgets_rect bounds, int info_header, xxwidgets_strings **out)
{
    static const xxwidgets_kind kinds[] = {XXWIDGETS_LABEL, XXWIDGETS_EDIT, XXWIDGETS_LABEL, XXWIDGETS_TABLE,
        XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_BUTTON, XXWIDGETS_BUTTON, XXWIDGETS_BUTTON};
    static const char *names[] = {XXW_N_("Filter"), "", "", "",
        XXW_N_("No string selected"), XXW_N_("0 / 0 strings"), XXW_N_("Copy"), XXW_N_("View"), XXW_N_("Save CSV")};
    static const char *titles[COLUMNS] = {"#", XXW_N_("Offset"), XXW_N_("Address"), XXW_N_("Region"), XXW_N_("Size"),
        XXW_N_("Type"), XXW_N_("String")};
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
        if (kinds[i] == XXWIDGETS_TABLE) {
            const char *translated[COLUMNS];
            for (size_t c = 0; c < COLUMNS; ++c) translated[c] = XXW_TR(owner->app, titles[c]);
            if (info_header) translated[5] = XXW_TR(owner->app, "Info");
            status = xxwidgets_table_create(owner, (xxwidgets_rect){0, 0, 1, 1}, translated, COLUMNS, v->controls + i);
        } else
            status = xxwidgets_widget_create(owner->app, owner, kinds[i], XXW_TR(owner->app, names[i]), (xxwidgets_rect){0, 0, 1, 1}, v->controls + i);
        if (status != XXWIDGETS_OK) goto fail;
    }
    status = xxwidgets_widget_set_visible(CONTROL(v, HEADER), 0);
    /* Column headers provide the visible filters. Retain the global controls
     * internally for the existing programmatic filtering API. */
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_visible(CONTROL(v, FILTER_LABEL), 0);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_visible(CONTROL(v, FILTER), 0);
    if (status == XXWIDGETS_OK) status = xxwidgets_strings_set_rect(v, bounds);
    if (status == XXWIDGETS_OK) status = xxwidgets_strings_set_records(v, NULL, 0);
    if (status != XXWIDGETS_OK) goto fail;
    *out = v;
    return XXWIDGETS_OK;
fail:
    (void)xxwidgets_strings_destroy(v);
    return status;
}

xxwidgets_status xxwidgets_strings_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_strings **out)
{
    return xxwidgets_strings_create_internal(owner, bounds, 0, out);
}

xxwidgets_status xxwidgets_strings_destroy(xxwidgets_strings *v)
{
    if (!v) return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling) return XXWIDGETS_BUSY;
    for (size_t i = 0; i < XXWIDGETS_STRINGS_CONTROL_COUNT; ++i)
        if (v->controls[i]) (void)xxwidgets_widget_destroy(v->controls[i]);
    free_records(v->records, v->count);
    free(v->numbers); free(v->matched); free(v->filter); free(v);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_strings_set_records(xxwidgets_strings *v, const xxwidgets_string_record *records, size_t count)
{
    return xxwidgets_strings_set_records_numbered(v, records, NULL, count);
}

xxwidgets_status xxwidgets_strings_set_records_numbered(xxwidgets_strings *v, const xxwidgets_string_record *records,
                                                      const uint64_t *numbers, size_t count)
{
    xxwidgets_string_record *copies = NULL, *old_records;
    uint64_t *number_copies = NULL, *old_numbers;
    size_t *matched = NULL, used = 0, *old_matched, old_count, old_matched_count;
    xxwidgets_status status;
    if (!v || (!records && count) || count > INT_MAX || count > SIZE_MAX / sizeof(*copies) || count > SIZE_MAX / (COLUMNS * sizeof(char *)) ||
        count > SIZE_MAX / sizeof(size_t)) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i)
        if (!xxwidgets_valid_utf8(records[i].text) || !xxwidgets_valid_utf8(records[i].encoding) || !records[i].encoding[0] ||
            (records[i].region && !xxwidgets_valid_utf8(records[i].region)) ||
            records[i].byte_size > UINT64_MAX - records[i].offset) return XXWIDGETS_INVALID_ARGUMENT;
    if (count) {
        copies = calloc(count, sizeof(*copies));
        if (!copies) return XXWIDGETS_OUT_OF_MEMORY;
        if (numbers) {
            number_copies = malloc(count * sizeof(*number_copies));
            if (!number_copies) { free(copies); return XXWIDGETS_OUT_OF_MEMORY; }
            memcpy(number_copies, numbers, count * sizeof(*number_copies));
        }
    }
    for (size_t i = 0; i < count; ++i) {
        copies[i] = records[i];
        copies[i].has_address = records[i].has_address != 0;
        copies[i].encoding = xxwidgets_strdup(records[i].encoding);
        copies[i].text = xxwidgets_strdup(records[i].text);
        copies[i].region = records[i].region ? xxwidgets_strdup(records[i].region) : NULL;
        if (!copies[i].encoding || !copies[i].text || (records[i].region && !copies[i].region)) {
            free_records(copies, i + 1); free(number_copies);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
    }
    status = replace_rows(v, copies, number_copies, count, v->filter, SIZE_MAX, &matched, &used);
    if (status != XXWIDGETS_OK) { free_records(copies, count); free(number_copies); return status; }
    old_numbers = v->numbers;
    old_records = v->records; old_count = v->count; old_matched = v->matched; old_matched_count = v->matched_count;
    v->records = copies; v->numbers = number_copies; v->count = count; v->matched = matched; v->matched_count = used;
    status = update_labels(v);
    if (status != XXWIDGETS_OK) {
        v->records = old_records; v->numbers = old_numbers; v->count = old_count; v->matched = old_matched; v->matched_count = old_matched_count;
        restore_rows(v);
        (void)update_labels(v);
        free_records(copies, count); free(number_copies); free(matched);
        return status;
    }
    free_records(old_records, old_count); free(old_numbers); free(old_matched);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_strings_set_filter(xxwidgets_strings *v, const char *filter)
{
    size_t *matched = NULL, used = 0, keep = SIZE_MAX, *old_matched, old_matched_count;
    char *copy, *old_filter, *old_text;
    int selected;
    xxwidgets_status status;
    if (!v || !xxwidgets_valid_utf8(filter)) return XXWIDGETS_INVALID_ARGUMENT;
    copy = xxwidgets_strdup(filter);
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    old_text = xxwidgets_strdup(CONTROL(v, FILTER)->text);
    if (!old_text) { free(copy); return XXWIDGETS_OUT_OF_MEMORY; }
    selected = selected_index(v);
    if (selected >= 0) keep = v->matched[selected];
    status = replace_rows(v, v->records, v->numbers, v->count, copy, keep, &matched, &used);
    if (status != XXWIDGETS_OK) { free(copy); free(old_text); return status; }
    old_filter = v->filter; old_matched = v->matched; old_matched_count = v->matched_count;
    v->filter = copy; v->matched = matched; v->matched_count = used;
    if (strcmp(CONTROL(v, FILTER)->text, filter)) status = xxwidgets_widget_set_text(CONTROL(v, FILTER), filter);
    if (status == XXWIDGETS_OK) status = update_labels(v);
    if (status != XXWIDGETS_OK) {
        v->filter = old_filter; v->matched = old_matched; v->matched_count = old_matched_count;
        (void)xxwidgets_widget_set_text(CONTROL(v, FILTER), old_text);
        restore_rows(v);
        (void)update_labels(v);
        free(copy); free(matched); free(old_text);
        return status;
    }
    free(old_filter); free(old_matched); free(old_text);
    return XXWIDGETS_OK;
}

size_t xxwidgets_strings_count(const xxwidgets_strings *v) { return v ? v->count : 0; }
size_t xxwidgets_strings_visible_count(const xxwidgets_strings *v) { return v ? xxwidgets_table_visible_count(CONTROL(v, RESULTS)) : 0; }

xxwidgets_status xxwidgets_strings_get_record(const xxwidgets_strings *v, size_t index, xxwidgets_string_record *record)
{
    if (!v || !record || index >= v->count) return XXWIDGETS_INVALID_ARGUMENT;
    *record = v->records[index];
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_strings_get_visible_record(const xxwidgets_strings *v, size_t row, size_t *index,
                                                     xxwidgets_string_record *record)
{
    size_t model;
    if (!v || !index || !record || row >= xxwidgets_strings_visible_count(v)) return XXWIDGETS_INVALID_ARGUMENT;
    model = xxwidgets_tabular_model_row(CONTROL(v, RESULTS), row);
    if (model >= v->matched_count) return XXWIDGETS_INVALID_ARGUMENT;
    *index = v->matched[model]; *record = v->records[*index];
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_strings_get_selection(xxwidgets_strings *v, size_t *index, xxwidgets_string_record *record)
{
    int value;
    if (!v || !index || !record) return XXWIDGETS_INVALID_ARGUMENT;
    xxwidgets_status status = xxwidgets_widget_get_value(CONTROL(v, RESULTS), &value);
    if (status != XXWIDGETS_OK) return status;
    if (value < 0 || (size_t)value >= v->matched_count) { *index = SIZE_MAX; memset(record, 0, sizeof(*record)); return XXWIDGETS_OK; }
    *index = v->matched[value]; *record = v->records[*index];
    return XXWIDGETS_OK;
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
    if (event->widget == CONTROL(v, RESULTS)) {
        /* A column filter changed the rows shown, or another row was selected. */
        if (event->type == XXWIDGETS_EVENT_CHANGE) return update_labels(v);
        if (event->type == XXWIDGETS_EVENT_SELECT || event->type == XXWIDGETS_EVENT_ACTIVATE) {
            if (event->value >= 0 && (size_t)event->value >= v->matched_count) return XXWIDGETS_INVALID_ARGUMENT;
            return update_labels(v);
        }
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
    size_t shown;
    if (!v || (!buffer && capacity)) return XXWIDGETS_INVALID_ARGUMENT;
    csv_output out = {buffer, capacity, 0, 0};
    csv_append(&out, "Offset,Bytes,Characters,Encoding,String\r\n");
    shown = xxwidgets_strings_visible_count(v);
    for (size_t i = 0; i < shown; ++i) {
        const xxwidgets_string_record *r;
        size_t index;
        xxwidgets_string_record record;
        char numbers[96];
        if (xxwidgets_strings_get_visible_record(v, i, &index, &record) != XXWIDGETS_OK) continue;
        r = &record;
        snprintf(numbers, sizeof(numbers), "0x%016" PRIX64 ",%" PRIu64 ",%" PRIu64 ",", r->offset, r->byte_size, r->character_length);
        csv_append(&out, numbers); csv_cell(&out, r->encoding); csv_append(&out, ","); csv_cell(&out, r->text); csv_append(&out, "\r\n");
    }
    if (buffer && capacity) buffer[out.length < capacity ? out.length : capacity - 1] = 0;
    if (out.overflow) return XXWIDGETS_OUT_OF_MEMORY;
    if (required) *required = out.length + 1;
    return buffer && capacity <= out.length ? XXWIDGETS_BUFFER_TOO_SMALL : XXWIDGETS_OK;
}
