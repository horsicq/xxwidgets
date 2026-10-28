/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets/xxwidgets_memory_map.h"
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_combobox.h"
#include <xxfclib/io/xx_io.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct map_snapshot {
    xxwidgets_memory_map_data data;
    xxwidgets_memory_map_record *rows;
    char *strings[6];
} map_snapshot;
struct xxwidgets_memory_map {
    xxwidgets_widget *owner, *controls[XXWIDGETS_MEMORY_MAP_CONTROL_COUNT];
    xxwidgets_rect bounds;
    map_snapshot *snapshot;
    xx_io_device *device;
    xxwidgets_memory_map_reload_fn reload;
    void *user;
    char detected[129];
    int pending, active, left, top, current_valid, input_location;
    uint64_t position;
    size_t selected;
    int search_active, search_pass;
    uint64_t search_size, search_start, search_position;
    unsigned search_width;
    unsigned char search_pattern[8];
};
#define C(v, id) ((v)->controls[XXWIDGETS_MEMORY_MAP_##id])
/* This is a small linked preview. Bound native/accessibility row enumeration;
 * source navigation and exports still address the complete borrowed device. */
#define MAP_PREVIEW_WINDOW 1024U
enum {
    NAV_OFFSET = 1,
    NAV_ADDRESS,
    NAV_RVA,
    SELECT_ROW,
    SELECT_HEX,
    SHOW_ALL,
    RELOAD,
    FORMAT,
    MODE,
    SAVE,
    DUMP_ALL,
    DUMP,
    COPY,
    COPY_ALL,
    SEARCH_OFFSET,
    SEARCH_ADDRESS,
    SEARCH_RVA
};

static void free_snapshot(map_snapshot *s)
{
    if (!s) return;
    for (size_t i = 0; i < s->data.count; ++i) {
        free((char *)s->rows[i].name);
        free((char *)s->rows[i].type);
    }
    free(s->rows);
    for (size_t i = 0; i < 6; ++i) free(s->strings[i]);
    free(s);
}
static map_snapshot *copy_snapshot(const xxwidgets_memory_map_data *d)
{
    map_snapshot *s = calloc(1, sizeof(*s));
    const char *strings[6] = {d->path,
                              d->format,
                              d->bitness ? d->bitness : "Unknown",
                              d->endianness ? d->endianness : "Unknown",
                              d->architecture ? d->architecture : "Unknown",
                              d->mode ? d->mode : "Data"};
    if (!s) return NULL;
    s->data = *d;
    s->data.count = 0;
    for (size_t i = 0; i < 6; ++i) {
        s->strings[i] = xxwidgets_strdup(strings[i]);
        if (!s->strings[i]) {
            free_snapshot(s);
            return NULL;
        }
    }
    if (d->count) {
        s->rows = calloc(d->count, sizeof(*s->rows));
        if (!s->rows) {
            free_snapshot(s);
            return NULL;
        }
        for (size_t i = 0; i < d->count; ++i) {
            s->rows[i] = d->records[i];
            s->rows[i].name = xxwidgets_strdup(d->records[i].name);
            s->rows[i].type = xxwidgets_strdup(d->records[i].type);
            ++s->data.count;
            if (!s->rows[i].name || !s->rows[i].type) {
                free_snapshot(s);
                return NULL;
            }
        }
    }
    s->data.path = s->strings[0];
    s->data.format = s->strings[1];
    s->data.bitness = s->strings[2];
    s->data.endianness = s->strings[3];
    s->data.architecture = s->strings[4];
    s->data.mode = s->strings[5];
    s->data.records = s->rows;
    return s;
}
static int valid_bounds(xxwidgets_rect b)
{
    return b.x >= 0 && b.y >= 0 && b.width >= 96 && b.height >= 20 && b.x <= 32767 - b.width && b.y <= 32767 - b.height;
}
xxwidgets_status xxwidgets_memory_map_set_rect(xxwidgets_memory_map *v, xxwidgets_rect b)
{
    xxwidgets_rect r[XXWIDGETS_MEMORY_MAP_CONTROL_COUNT];
    int left, top, field;
    xxwidgets_status status = XXWIDGETS_OK;
    if (!v || !valid_bounds(b)) return XXWIDGETS_INVALID_ARGUMENT;
    left = v->left ? v->left : b.width / 3;
    if (left < 54) left = 54;
    if (left > b.width - 40) left = b.width - 40;
    top = v->top ? v->top : b.height / 2;
    if (top < 12) top = 12;
    if (top > b.height - 6) top = b.height - 6;
    field = left - 31;
    for (size_t i = 0; i < XXWIDGETS_MEMORY_MAP_CONTROL_COUNT; ++i) r[i] = (xxwidgets_rect){b.x, b.y, 1, 1};
#define R(id, dx, dy, w, h) r[XXWIDGETS_MEMORY_MAP_##id] = (xxwidgets_rect){b.x + (dx), b.y + (dy), w, h}
    R(FORMAT, 0, 0, 13, 2);
    R(MODE, 14, 0, 14, 2);
    R(SHOW_ALL, 29, 0, 13, 2);
    R(READONLY, b.width - 46, 0, 11, 2);
    R(RELOAD, b.width - 34, 0, 10, 2);
    R(DUMP_ALL, b.width - 23, 0, 12, 2);
    R(SAVE, b.width - 10, 0, 10, 2);
    R(RADIO_OFFSET, 0, 3, 25, 2);
    R(FIND, 26, 3, field, 2);
    R(FIND_OFFSET, left - 4, 3, 4, 2);
    R(RADIO_ADDRESS, 0, 5, 25, 2);
    R(ADDRESS, 26, 5, field, 2);
    R(FIND_ADDRESS, left - 4, 5, 4, 2);
    R(RADIO_RVA, 0, 7, 25, 2);
    R(RVA, 26, 7, field, 2);
    R(FIND_RVA, left - 4, 7, 4, 2);
    R(BITNESS_LABEL, 0, 10, left / 3, 1);
    R(BITNESS, 0, 11, left / 3 - 1, 2);
    R(ENDIAN_LABEL, left / 3, 10, left / 3, 1);
    R(ENDIAN, left / 3, 11, left / 3 - 1, 2);
    R(ARCH_LABEL, 2 * (left / 3), 10, left - 2 * (left / 3), 1);
    R(ARCH, 2 * (left / 3), 11, left - 2 * (left / 3) - 1, 2);
    R(VERTICAL_SPLITTER, left, 3, 1, top - 3);
    R(TABLE, left + 1, 3, b.width - left - 1, top - 3);
    R(HORIZONTAL_SPLITTER, 0, top + 1, b.width, 1);
    R(HEX_LABEL, 0, top + 2, 8, 1);
    R(HEX, 0, top + 3, b.width, b.height - top - 4);
    R(NO_DATA, 2, top + 4, b.width - 4, 2);
    R(STATUS, 0, b.height - 1, b.width, 1);
#undef R
    for (size_t i = 0; i < XXWIDGETS_MEMORY_MAP_CONTROL_COUNT && status == XXWIDGETS_OK; ++i) status = xxwidgets_widget_set_rect(v->controls[i], r[i]);
    if (status == XXWIDGETS_OK) {
        v->bounds = b;
        v->left = left;
        v->top = top;
    }
    return status;
}
static void hex_number(char *out, uint64_t value)
{
    if (value == UINT64_MAX) out[0] = 0;
    else snprintf(out, 32, value > UINT32_MAX ? "%016" PRIx64 : "%08" PRIx64, value);
}
static int parse_number(const char *text, uint64_t *out, int hexadecimal)
{
    const unsigned char *p = (const unsigned char *)text;
    uint64_t value = 0;
    unsigned base = hexadecimal ? 16 : 10;
    int any = 0;
    if (!p) return 0;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    }
    while (*p) {
        unsigned d = *p >= '0' && *p <= '9' ? *p - '0' : *p >= 'a' && *p <= 'f' ? *p - 'a' + 10 : *p >= 'A' && *p <= 'F' ? *p - 'A' + 10 : 16;
        if (d == 16) break;
        if (d >= base || value > (UINT64_MAX - d) / base) return 0;
        value = value * base + d;
        ++p;
        any = 1;
    }
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (!any || *p) return 0;
    *out = value;
    return 1;
}
static xxwidgets_status active_field(xxwidgets_memory_map *v, int active)
{
    const xxwidgets_memory_map_control_id radios[] = {XXWIDGETS_MEMORY_MAP_RADIO_OFFSET, XXWIDGETS_MEMORY_MAP_RADIO_ADDRESS, XXWIDGETS_MEMORY_MAP_RADIO_RVA};
    const xxwidgets_memory_map_control_id edits[] = {XXWIDGETS_MEMORY_MAP_FIND, XXWIDGETS_MEMORY_MAP_ADDRESS, XXWIDGETS_MEMORY_MAP_RVA};
    xxwidgets_status s = XXWIDGETS_OK;
    v->active = active;
    for (int i = 0; i < 3 && s == XXWIDGETS_OK; ++i) {
        s = xxwidgets_widget_set_value(v->controls[radios[i]], i == active);
        if (s == XXWIDGETS_OK) s = xxwidgets_widget_set_enabled(v->controls[edits[i]], i == active);
    }
    return s;
}
static xxwidgets_status clear_preview(xxwidgets_memory_map *v)
{
    xxwidgets_status s = xxwidgets_hexview_set_device(C(v, HEX), NULL, 0, 0, 0);
    if (s == XXWIDGETS_OK) s = xxwidgets_hexview_set_data(C(v, HEX), NULL, 0);
    if (s == XXWIDGETS_OK) s = xxwidgets_widget_set_visible(C(v, HEX), 0);
    if (s == XXWIDGETS_OK) s = xxwidgets_widget_set_visible(C(v, NO_DATA), 1);
    return s;
}
static xxwidgets_status location(xxwidgets_memory_map *v, const xxwidgets_memory_map_record *r, uint64_t delta, int preview, int whole)
{
    uint64_t offset = UINT64_MAX, address = UINT64_MAX, rva = UINT64_MAX;
    char text[32];
    xxwidgets_status s = XXWIDGETS_OK;
    if (r) {
        if (r->offset >= 0 && !(r->flags & XXWIDGETS_MEMORY_MAP_VIRTUAL)) offset = (uint64_t)r->offset + delta;
        if (r->address != UINT64_MAX) address = r->address + delta;
        if (address != UINT64_MAX && v->snapshot && v->snapshot->data.module_address != UINT64_MAX && address >= v->snapshot->data.module_address)
            rva = address - v->snapshot->data.module_address;
    }
    /* Read a proposed physical location before committing its coordinate
     * fields. Failed reads keep the previous bytes and navigation position. */
    if (preview) {
        if (!v->device || offset == UINT64_MAX || !r || !r->size) s = clear_preview(v);
        else {
            int64_t size = xx_io_total_size(v->device);
            uint64_t length = whole ? r->size : 1;
            if (size < 0 || offset > (uint64_t)size || length > (uint64_t)size - offset) return XXWIDGETS_UNAVAILABLE;
            if (xxwidgets_hexview_get_device(C(v, HEX)) != v->device) s = xxwidgets_hexview_set_device(C(v, HEX), v->device, offset, MAP_PREVIEW_WINDOW, 0);
            else s = xxwidgets_hexview_seek_device(C(v, HEX), offset);
            if (s == XXWIDGETS_OK) s = xxwidgets_hexview_set_source_selection(C(v, HEX), offset, length);
            if (s == XXWIDGETS_OK) s = xxwidgets_widget_set_visible(C(v, NO_DATA), 0);
            if (s == XXWIDGETS_OK) s = xxwidgets_widget_set_visible(C(v, HEX), 1);
        }
    }
    if (s != XXWIDGETS_OK) return s;
    if (v->input_location != 0) {
        hex_number(text, offset);
        s = xxwidgets_widget_set_text(C(v, FIND), text);
    }
    if (s == XXWIDGETS_OK && v->input_location != 1) {
        hex_number(text, address);
        s = xxwidgets_widget_set_text(C(v, ADDRESS), text);
    }
    if (s == XXWIDGETS_OK && v->input_location != 2) {
        hex_number(text, rva);
        s = xxwidgets_widget_set_text(C(v, RVA), text);
    }
    if (s == XXWIDGETS_OK) {
        v->current_valid = offset != UINT64_MAX;
        v->position = offset;
    }
    return s;
}
static xxwidgets_status selected_location(xxwidgets_memory_map *v, int whole)
{
    size_t i;
    xxwidgets_memory_map_record r;
    xxwidgets_status s = xxwidgets_memory_map_table_get_selection(C(v, TABLE), &i, &r);
    if (s == XXWIDGETS_OK) s = location(v, i == SIZE_MAX ? NULL : &r, 0, 1, whole);
    if (s == XXWIDGETS_OK) v->selected = i;
    else (void)xxwidgets_widget_set_value(C(v, TABLE), v->selected == SIZE_MAX ? -1 : (int)v->selected);
    return s;
}
static xxwidgets_status show_rows(xxwidgets_memory_map *v, const map_snapshot *snapshot)
{
    xxwidgets_memory_map_record *rows = NULL;
    size_t count = 0;
    int all = 1;
    xxwidgets_status s;
    if (snapshot->data.count) {
        rows = malloc(snapshot->data.count * sizeof(*rows));
        if (!rows) return XXWIDGETS_OUT_OF_MEMORY;
    }
    xxwidgets_widget_get_value(C(v, SHOW_ALL), &all);
    for (size_t i = 0; i < snapshot->data.count; ++i)
        if (all || !(snapshot->rows[i].flags & XXWIDGETS_MEMORY_MAP_VIRTUAL)) rows[count++] = snapshot->rows[i];
    s = xxwidgets_memory_map_table_set_rows(C(v, TABLE), rows, count);
    free(rows);
    return s;
}
static xxwidgets_status choices(xxwidgets_widget *widget, const char *const *names, size_t count, int selected)
{
    xx_meta_string records[3] = {0};
    xx_str_w_s labels[3] = {0};
    wchar_t text[3][130] = {{0}};
    if (count > 3) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i) {
        size_t n = strlen(names[i]);
        if (n > 129) return XXWIDGETS_INVALID_ARGUMENT;
        for (size_t j = 0; j < n; ++j) text[i][j] = (unsigned char)names[i][j];
        labels[i].data = text[i];
        labels[i].length = n;
        labels[i].capacity = 130;
        labels[i].is_view = true;
        records[i].meta_string = labels + i;
        records[i].var.type = XX_VAR_TYPE_UINT64;
        records[i].var.val.u64 = i;
    }
    xxwidgets_status s = xxwidgets_combobox_set_records(widget, records, count);
    return s == XXWIDGETS_OK ? xxwidgets_widget_set_value(widget, selected) : s;
}
static xxwidgets_status metadata(xxwidgets_memory_map *v)
{
    const xxwidgets_memory_map_data *d = &v->snapshot->data;
    char summary[256];
    int selected = 0, index = 0;
    xxwidgets_status s;
    snprintf(summary, sizeof(summary), "%s | %" PRIu64 " bytes | %zu regions", d->format, d->file_size, d->count);
    s = xxwidgets_widget_set_text(C(v, PATH), d->path);
    if (s == XXWIDGETS_OK) s = xxwidgets_widget_set_text(C(v, SUMMARY), summary);
    if (s == XXWIDGETS_OK) s = xxwidgets_widget_set_text(C(v, BITNESS), d->bitness);
    if (s == XXWIDGETS_OK) s = xxwidgets_widget_set_text(C(v, ENDIAN), d->endianness);
    if (s == XXWIDGETS_OK) s = xxwidgets_widget_set_text(C(v, ARCH), d->architecture);
    {
        const char *names[2];
        size_t count = 0;
        if (v->detected[0] && strcmp(v->detected, "Binary")) names[count++] = v->detected;
        if (!strcmp(d->format, "Binary")) selected = (int)count;
        names[count++] = "Binary";
        if (s == XXWIDGETS_OK) s = choices(C(v, FORMAT), names, count, selected);
    }
    {
        const char *names[] = {"Data", "Sections", "Segments"}, *shown[3];
        unsigned masks[] = {1, 2, 4};
        index = 0;
        selected = 0;
        unsigned available = d->available_modes ? d->available_modes : 1;
        for (int i = 0; i < 3 && s == XXWIDGETS_OK; ++i)
            if (available & masks[i]) {
                shown[index] = names[i];
                if (!strcmp(d->mode, names[i])) selected = index;
                ++index;
            }
        if (s == XXWIDGETS_OK) s = choices(C(v, MODE), shown, (size_t)index, selected);
    }
    if (s == XXWIDGETS_OK) s = xxwidgets_widget_set_enabled(C(v, FORMAT), v->reload != NULL);
    if (s == XXWIDGETS_OK) s = xxwidgets_widget_set_enabled(C(v, MODE), v->reload != NULL);
    if (s == XXWIDGETS_OK) s = xxwidgets_widget_set_enabled(C(v, RELOAD), v->reload != NULL || v->device != NULL);
    return s;
}
xxwidgets_status xxwidgets_memory_map_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_memory_map **out)
{
    xxwidgets_memory_map *v;
    xxwidgets_status s;
    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !valid_bounds(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing) return XXWIDGETS_BUSY;
    v = calloc(1, sizeof(*v));
    if (!v) return XXWIDGETS_OUT_OF_MEMORY;
    v->owner = owner;
    v->input_location = -1;
    v->selected = SIZE_MAX;
    for (size_t i = 0; i < XXWIDGETS_MEMORY_MAP_CONTROL_COUNT; ++i) {
        xxwidgets_kind kind = XXWIDGETS_LABEL;
        const char *text = "";
        switch (i) {
            case XXWIDGETS_MEMORY_MAP_FIND:
            case XXWIDGETS_MEMORY_MAP_ADDRESS:
            case XXWIDGETS_MEMORY_MAP_RVA: kind = XXWIDGETS_EDIT; break;
            case XXWIDGETS_MEMORY_MAP_FIND_OFFSET:
            case XXWIDGETS_MEMORY_MAP_FIND_ADDRESS:
            case XXWIDGETS_MEMORY_MAP_FIND_RVA:
                kind = XXWIDGETS_BUTTON;
                text = "\xE2\x8C\x95";
                break;
            case XXWIDGETS_MEMORY_MAP_COPY:
                kind = XXWIDGETS_BUTTON;
                text = "Copy row";
                break;
            case XXWIDGETS_MEMORY_MAP_COPY_ALL:
                kind = XXWIDGETS_BUTTON;
                text = "Copy all";
                break;
            case XXWIDGETS_MEMORY_MAP_RELOAD:
                kind = XXWIDGETS_BUTTON;
                text = "Reload";
                break;
            case XXWIDGETS_MEMORY_MAP_DUMP_ALL:
                kind = XXWIDGETS_BUTTON;
                text = "Dump all";
                break;
            case XXWIDGETS_MEMORY_MAP_DUMP:
                kind = XXWIDGETS_BUTTON;
                text = "Dump region";
                break;
            case XXWIDGETS_MEMORY_MAP_SAVE:
                kind = XXWIDGETS_BUTTON;
                text = "Save";
                break;
            case XXWIDGETS_MEMORY_MAP_FORMAT:
            case XXWIDGETS_MEMORY_MAP_MODE: kind = XXWIDGETS_COMBOBOX; break;
            case XXWIDGETS_MEMORY_MAP_SHOW_ALL:
                kind = XXWIDGETS_CHECKBOX;
                text = "Show all";
                break;
            case XXWIDGETS_MEMORY_MAP_READONLY:
                kind = XXWIDGETS_CHECKBOX;
                text = "Readonly";
                break;
            case XXWIDGETS_MEMORY_MAP_RADIO_OFFSET:
                kind = XXWIDGETS_CHECKBOX;
                text = "File offset";
                break;
            case XXWIDGETS_MEMORY_MAP_RADIO_ADDRESS:
                kind = XXWIDGETS_CHECKBOX;
                text = "Virtual address";
                break;
            case XXWIDGETS_MEMORY_MAP_RADIO_RVA:
                kind = XXWIDGETS_CHECKBOX;
                text = "Relative virtual address";
                break;
            case XXWIDGETS_MEMORY_MAP_BITNESS_LABEL: text = "Mode"; break;
            case XXWIDGETS_MEMORY_MAP_ENDIAN_LABEL: text = "Endianness"; break;
            case XXWIDGETS_MEMORY_MAP_ARCH_LABEL: text = "Architecture"; break;
            case XXWIDGETS_MEMORY_MAP_HEX_LABEL: text = "Hex"; break;
            case XXWIDGETS_MEMORY_MAP_NO_DATA: text = "No file data at this location"; break;
            case XXWIDGETS_MEMORY_MAP_HEX: kind = XXWIDGETS_HEXVIEW; break;
            case XXWIDGETS_MEMORY_MAP_TABLE:
                kind = XXWIDGETS_MEMORY_MAP;
                text = "Memory Map";
                break;
            case XXWIDGETS_MEMORY_MAP_VERTICAL_SPLITTER:
            case XXWIDGETS_MEMORY_MAP_HORIZONTAL_SPLITTER: kind = XXWIDGETS_SPLITTER; break;
        }
        s = xxwidgets_widget_create(owner->app, owner, kind, XXW_TR(owner->app, text), (xxwidgets_rect){0, 0, 1, 1}, v->controls + i);
        if (s != XXWIDGETS_OK) goto fail;
    }
    {
        const xxwidgets_memory_map_control_id hidden[] = {XXWIDGETS_MEMORY_MAP_PATH,       XXWIDGETS_MEMORY_MAP_SUMMARY, XXWIDGETS_MEMORY_MAP_METADATA,
                                                          XXWIDGETS_MEMORY_MAP_FIND_LABEL, XXWIDGETS_MEMORY_MAP_DETAILS, XXWIDGETS_MEMORY_MAP_COPY,
                                                          XXWIDGETS_MEMORY_MAP_COPY_ALL,   XXWIDGETS_MEMORY_MAP_DUMP};
        for (size_t i = 0; i < sizeof(hidden) / sizeof(hidden[0]); ++i) {
            s = xxwidgets_widget_set_visible(v->controls[hidden[i]], 0);
            if (s != XXWIDGETS_OK) goto fail;
        }
    }
    s = xxwidgets_memory_map_table_set_compact(C(v, TABLE), 1);
    if (s == XXWIDGETS_OK) {
        const xxwidgets_hex_display display = {16, 1, XXWIDGETS_HEX_HEX, "ASCII"};
        s = xxwidgets_hexview_set_display(C(v, HEX), &display);
    }
    if (s == XXWIDGETS_OK) s = xxwidgets_widget_set_value(C(v, SHOW_ALL), 1);
    if (s == XXWIDGETS_OK) s = xxwidgets_widget_set_value(C(v, READONLY), 1);
    if (s == XXWIDGETS_OK) s = xxwidgets_widget_set_enabled(C(v, READONLY), 0);
    for (int i = 0; i < 3 && s == XXWIDGETS_OK; ++i) s = xxwidgets_checkbox_set_radio(v->controls[XXWIDGETS_MEMORY_MAP_RADIO_OFFSET + i], 1);
    if (s == XXWIDGETS_OK) s = active_field(v, 0);
    if (s == XXWIDGETS_OK) s = xxwidgets_memory_map_set_rect(v, bounds);
    if (s == XXWIDGETS_OK) {
        xxwidgets_memory_map_data empty = {"", "Binary", 0, UINT64_MAX, UINT64_MAX, NULL, 0};
        s = xxwidgets_memory_map_set_data(v, &empty);
    }
    if (s != XXWIDGETS_OK) goto fail;
    *out = v;
    return XXWIDGETS_OK;
fail:
    xxwidgets_memory_map_destroy(v);
    return s;
}
xxwidgets_status xxwidgets_memory_map_destroy(xxwidgets_memory_map *v)
{
    if (!v) return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling || v->owner->app->syncing) return XXWIDGETS_BUSY;
    for (size_t i = 0; i < XXWIDGETS_MEMORY_MAP_CONTROL_COUNT; ++i)
        if (v->controls[i]) xxwidgets_widget_destroy(v->controls[i]);
    free_snapshot(v->snapshot);
    free(v);
    return XXWIDGETS_OK;
}
xxwidgets_widget *xxwidgets_memory_map_control(const xxwidgets_memory_map *v, xxwidgets_memory_map_control_id id)
{
    return v && id >= 0 && id < XXWIDGETS_MEMORY_MAP_CONTROL_COUNT ? v->controls[id] : NULL;
}
xxwidgets_status xxwidgets_memory_map_set_data_device(xxwidgets_memory_map *v, const xxwidgets_memory_map_data *d, xx_io_device *device)
{
    map_snapshot *s, *old;
    xx_io_device *old_device;
    xxwidgets_status status;
    char detected[129], fields[3][32];
    uint64_t position;
    int valid;
    size_t selected;
    int64_t size;
    const xxwidgets_memory_map_control_id edits[] = {XXWIDGETS_MEMORY_MAP_FIND, XXWIDGETS_MEMORY_MAP_ADDRESS, XXWIDGETS_MEMORY_MAP_RVA};
    if (!v || !d || !xxwidgets_valid_utf8(d->path) || !xxwidgets_valid_utf8(d->format) || strlen(d->format) > 128 || d->count > XXWIDGETS_MEMORY_MAP_MAX_RECORDS ||
        (d->count && !d->records))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling || v->owner->app->syncing) return XXWIDGETS_BUSY;
    if (device) {
        size = xx_io_total_size(device);
        if (size < 0 || (uint64_t)size < d->file_size) return XXWIDGETS_INVALID_ARGUMENT;
    }
    if (d->available_modes & ~7u) return XXWIDGETS_INVALID_ARGUMENT;
    {
        const char *meta[] = {d->bitness, d->endianness, d->architecture, d->mode};
        for (int i = 0; i < 4; ++i)
            if (meta[i] && (!xxwidgets_valid_utf8(meta[i]) || strlen(meta[i]) > 128)) return XXWIDGETS_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < d->count; ++i) {
        const xxwidgets_memory_map_record *r = d->records + i;
        if (!r->name || !r->type || !xxwidgets_valid_utf8(r->name) || !xxwidgets_valid_utf8(r->type) || strlen(r->name) > 512 || strlen(r->type) > 128 ||
            (r->flags & ~3u) || (r->flags & 1 ? r->offset != -1 : r->offset < 0 || (uint64_t)r->offset > d->file_size || r->size > d->file_size - (uint64_t)r->offset) ||
            (r->address != UINT64_MAX && r->size > UINT64_MAX - r->address))
            return XXWIDGETS_INVALID_ARGUMENT;
    }
    s = copy_snapshot(d);
    if (!s) return XXWIDGETS_OUT_OF_MEMORY;
    for (int i = 0; i < 3; ++i) {
        status = xxwidgets_widget_get_text(v->controls[edits[i]], fields[i], sizeof(fields[i]), NULL);
        if (status != XXWIDGETS_OK) {
            free_snapshot(s);
            return status;
        }
    }
    old = v->snapshot;
    old_device = v->device;
    selected = v->selected;
    position = v->position;
    valid = v->current_valid;
    memcpy(detected, v->detected, sizeof(detected));
    status = show_rows(v, s);
    if (status != XXWIDGETS_OK) {
        free_snapshot(s);
        return status;
    }
    if (!v->snapshot || strcmp(v->snapshot->data.path, d->path)) v->detected[0] = 0;
    if (strcmp(d->format, "Binary")) snprintf(v->detected, sizeof(v->detected), "%s", d->format);
    v->snapshot = s;
    v->device = device;
    status = metadata(v);
    if (status == XXWIDGETS_OK) status = selected_location(v, 1);
    if (status == XXWIDGETS_OK) {
        free_snapshot(old);
        v->pending = 0;
        v->search_active = 0;
        (void)xxwidgets_widget_set_enabled(C(v, DUMP_ALL), device != NULL);
        return XXWIDGETS_OK;
    }
    /* Parser metadata and its borrowed source form one replacement. Keep the
     * previous snapshot alive until the new preview has also been read. */
    v->snapshot = old;
    v->device = old_device;
    v->selected = selected;
    v->position = position;
    v->current_valid = valid;
    memcpy(v->detected, detected, sizeof(detected));
    if (old) {
        (void)show_rows(v, old);
        (void)metadata(v);
        (void)xxwidgets_widget_set_value(C(v, TABLE), selected == SIZE_MAX ? -1 : (int)selected);
    }
    for (int i = 0; i < 3; ++i) (void)xxwidgets_widget_set_text(v->controls[edits[i]], fields[i]);
    free_snapshot(s);
    return status;
}
xxwidgets_status xxwidgets_memory_map_set_data(xxwidgets_memory_map *v, const xxwidgets_memory_map_data *d)
{
    return xxwidgets_memory_map_set_data_device(v, d, v ? v->device : NULL);
}
xxwidgets_status xxwidgets_memory_map_set_device(xxwidgets_memory_map *v, xx_io_device *device)
{
    xx_io_device *old;
    xxwidgets_status s;
    uint64_t offset = 0;
    int64_t size;
    if (!v) return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling || v->owner->app->syncing) return XXWIDGETS_BUSY;
    if (!device) {
        v->device = NULL;
        return clear_preview(v);
    }
    size = xx_io_total_size(device);
    if (size < 0 || (v->snapshot && (uint64_t)size < v->snapshot->data.file_size)) return XXWIDGETS_INVALID_ARGUMENT;
    if (v->current_valid && v->position < (uint64_t)size) offset = v->position;
    s = xxwidgets_hexview_set_device(C(v, HEX), device, offset, MAP_PREVIEW_WINDOW, 0);
    if (s != XXWIDGETS_OK) return s;
    old = v->device;
    v->device = device;
    s = selected_location(v, 1);
    if (s != XXWIDGETS_OK) {
        v->device = old;
        if (old) xxwidgets_hexview_set_device(C(v, HEX), old, 0, MAP_PREVIEW_WINDOW, 0);
        else clear_preview(v);
    }
    if (s == XXWIDGETS_OK) {
        xxwidgets_widget_set_enabled(C(v, DUMP_ALL), 1);
        s = metadata(v);
    }
    return s;
}
xxwidgets_status xxwidgets_memory_map_set_reload_handler(xxwidgets_memory_map *v, xxwidgets_memory_map_reload_fn fn, void *user)
{
    if (!v) return XXWIDGETS_INVALID_ARGUMENT;
    v->reload = fn;
    v->user = user;
    return v->snapshot ? metadata(v) : XXWIDGETS_OK;
}
static xxwidgets_status navigate_value(xxwidgets_memory_map *v, uint64_t value, int kind)
{
    size_t displayed = 0;
    int all = 1;
    if (!v->snapshot) return XXWIDGETS_UNAVAILABLE;
    if (kind == 2) {
        uint64_t base = v->snapshot->data.module_address;
        if (base == UINT64_MAX || value > UINT64_MAX - base) return XXWIDGETS_INVALID_ARGUMENT;
        value += base;
        kind = 1;
    }
    (void)xxwidgets_widget_get_value(C(v, SHOW_ALL), &all);
    for (size_t i = 0; i < v->snapshot->data.count; ++i) {
        const xxwidgets_memory_map_record *r = v->snapshot->rows + i;
        size_t row = all || !(r->flags & XXWIDGETS_MEMORY_MAP_VIRTUAL) ? displayed++ : SIZE_MAX;
        uint64_t start;
        xxwidgets_status s;
        if (kind) {
            if (r->address == UINT64_MAX) continue;
            start = r->address;
        } else {
            if (r->offset < 0 || (r->flags & XXWIDGETS_MEMORY_MAP_VIRTUAL)) continue;
            start = (uint64_t)r->offset;
        }
        if (value < start || value - start >= r->size) continue;
        s = xxwidgets_widget_set_value(C(v, TABLE), row == SIZE_MAX ? -1 : (int)row);
        if (s == XXWIDGETS_OK) s = location(v, r, value - start, 1, 0);
        if (s == XXWIDGETS_OK) v->selected = row;
        else (void)xxwidgets_widget_set_value(C(v, TABLE), v->selected == SIZE_MAX ? -1 : (int)v->selected);
        return s;
    }
    return XXWIDGETS_UNAVAILABLE;
}
xxwidgets_status xxwidgets_memory_map_navigate(xxwidgets_memory_map *v, const char *text, int kind)
{
    uint64_t value;
    if (!v || kind < 0 || kind > 2 || !parse_number(text, &value, 0)) return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling || v->owner->app->syncing) return XXWIDGETS_BUSY;
    return navigate_value(v, value, kind);
}
xxwidgets_status xxwidgets_memory_map_find(xxwidgets_memory_map *v, const char *text, int address_lookup)
{
    if (address_lookup != 0 && address_lookup != 1) return XXWIDGETS_INVALID_ARGUMENT;
    return xxwidgets_memory_map_navigate(v, text, address_lookup);
}
static xxwidgets_status ui_navigate(xxwidgets_memory_map *v, int kind)
{
    char text[128];
    uint64_t value;
    const xxwidgets_memory_map_control_id edits[] = {XXWIDGETS_MEMORY_MAP_FIND, XXWIDGETS_MEMORY_MAP_ADDRESS, XXWIDGETS_MEMORY_MAP_RVA};
    xxwidgets_status s = xxwidgets_widget_get_text(v->controls[edits[kind]], text, sizeof(text), NULL);
    if (s != XXWIDGETS_OK) return s;
    if (!parse_number(text, &value, 1)) return XXWIDGETS_INVALID_ARGUMENT;
    /* Keep the active edit's draft and caret while the other coordinates and
     * preview track it. Rewriting it on every keystroke interrupts typing. */
    v->input_location = kind;
    s = navigate_value(v, value, kind);
    v->input_location = -1;
    return s;
}
static xxwidgets_status sync_hex_location(xxwidgets_memory_map *v)
{
    uint64_t offset, length;
    xxwidgets_status s = xxwidgets_hexview_get_source_selection(C(v, HEX), &offset, &length);
    if (s != XXWIDGETS_OK || !length) return s;
    for (size_t i = 0; i < xxwidgets_memory_map_table_count(C(v, TABLE)); ++i) {
        xxwidgets_memory_map_record r;
        xxwidgets_memory_map_table_get_row(C(v, TABLE), i, &r);
        if (r.offset >= 0 && !(r.flags & 1) && offset >= (uint64_t)r.offset && offset - (uint64_t)r.offset < r.size) {
            s = xxwidgets_widget_set_value(C(v, TABLE), (int)i);
            return s == XXWIDGETS_OK ? location(v, &r, offset - (uint64_t)r.offset, 0, 0) : s;
        }
    }
    return XXWIDGETS_OK;
}
static xxwidgets_status reload_map(xxwidgets_memory_map *v, int action)
{
    char format[160], mode[160];
    uint64_t old = v->position;
    int valid = v->current_valid;
    xxwidgets_status s;
    int selected;
    s = xxwidgets_widget_get_value(C(v, FORMAT), &selected);
    if (s != XXWIDGETS_OK) return s;
    if (selected < 0 || (size_t)selected >= C(v, FORMAT)->item_count) return XXWIDGETS_INVALID_ARGUMENT;
    snprintf(format, sizeof(format), "%s", C(v, FORMAT)->items[selected]);
    s = xxwidgets_widget_get_value(C(v, MODE), &selected);
    if (s != XXWIDGETS_OK) return s;
    if (selected < 0 || (size_t)selected >= C(v, MODE)->item_count) return XXWIDGETS_INVALID_ARGUMENT;
    snprintf(mode, sizeof(mode), "%s", C(v, MODE)->items[selected]);
    if (v->reload) s = v->reload(v, format, action == FORMAT ? "" : mode, v->user);
    else s = v->device ? xxwidgets_hexview_refresh_device(C(v, HEX)) : XXWIDGETS_UNAVAILABLE;
    if (s == XXWIDGETS_OK) {
        active_field(v, 0);
        if (valid) (void)navigate_value(v, old, 0);
    } else (void)metadata(v);
    return s;
}
static xxwidgets_status search_step(xxwidgets_memory_map *v)
{
    unsigned char buffer[65536 + 8];
    size_t budget = 1048576;
    while (v->search_active && budget) {
        uint64_t end = v->search_pass ? v->search_start : v->search_size;
        uint64_t position = v->search_position;
        if (position >= end) {
            if (!v->search_pass && v->search_start) {
                v->search_pass = 1;
                v->search_position = 0;
                continue;
            }
            v->search_active = 0;
            return XXWIDGETS_UNAVAILABLE;
        }
        if (budget < v->search_width) break;
        size_t n = (size_t)(end - position > 65536 ? 65536 : end - position);
        if (n > budget) n = budget;
        size_t extra = (size_t)(v->search_size - position - n > v->search_width - 1 ? v->search_width - 1 : v->search_size - position - n);
        if (n + extra > budget) {
            n = budget - (v->search_width - 1);
            extra = v->search_width - 1;
        }
        if (!xx_io_read_at(v->device, (int64_t)position, buffer, n + extra)) {
            v->search_active = 0;
            return XXWIDGETS_UNAVAILABLE;
        }
        for (size_t i = 0; i < n && i + v->search_width <= n + extra; ++i)
            if (!memcmp(buffer + i, v->search_pattern, v->search_width)) {
                v->search_active = 0;
                return navigate_value(v, position + i, 0);
            }
        v->search_position += n;
        budget -= n + extra;
    }
    return XXWIDGETS_OK;
}
static xxwidgets_status search_value(xxwidgets_memory_map *v, int kind)
{
    char text[128];
    uint64_t value;
    int64_t source_size;
    unsigned width = v->snapshot && strstr(v->snapshot->data.bitness, "64") ? 8 : 4;
    const xxwidgets_memory_map_control_id edits[] = {XXWIDGETS_MEMORY_MAP_FIND, XXWIDGETS_MEMORY_MAP_ADDRESS, XXWIDGETS_MEMORY_MAP_RVA};
    if (!v->device) return XXWIDGETS_UNAVAILABLE;
    if (xxwidgets_widget_get_text(v->controls[edits[kind]], text, sizeof(text), NULL) != XXWIDGETS_OK || !parse_number(text, &value, 1))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (width == 4 && value > UINT32_MAX) width = 8;
    for (unsigned i = 0; i < width; ++i)
        v->search_pattern[v->snapshot && !strcmp(v->snapshot->data.endianness, "BE") ? width - 1 - i : i] = (unsigned char)(value >> (i * 8));
    source_size = xx_io_total_size(v->device);
    if (source_size < 0) return XXWIDGETS_UNAVAILABLE;
    v->search_size = (uint64_t)source_size;
    v->search_start = v->current_valid && v->position < v->search_size ? v->position + 1 : 0;
    v->search_position = v->search_start;
    v->search_pass = 0;
    v->search_width = width;
    v->search_active = 1;
    return search_step(v);
}
xxwidgets_status xxwidgets_memory_map_handle_event(xxwidgets_memory_map *v, const xxwidgets_event *e)
{
    if (!v || !e) return XXWIDGETS_INVALID_ARGUMENT;
    if (e->type == XXWIDGETS_EVENT_DRAG) {
        if (e->widget == C(v, VERTICAL_SPLITTER)) v->left = e->x - v->bounds.x;
        else if (e->widget == C(v, HORIZONTAL_SPLITTER)) v->top = e->y - v->bounds.y - 1;
        else return XXWIDGETS_OK;
        return xxwidgets_memory_map_set_rect(v, v->bounds);
    }
    if (e->type == XXWIDGETS_EVENT_SELECT) {
        if (e->widget == C(v, TABLE)) v->pending = SELECT_ROW;
        else if (e->widget == C(v, HEX)) v->pending = SELECT_HEX;
        else if (e->widget == C(v, FORMAT)) v->pending = FORMAT;
        else if (e->widget == C(v, MODE)) v->pending = MODE;
    }
    if (e->type == XXWIDGETS_EVENT_CHANGE && e->widget == C(v, TABLE)) v->pending = SELECT_ROW;
    if (e->type == XXWIDGETS_EVENT_CHANGE || e->type == XXWIDGETS_EVENT_ACTIVATE) {
        if (e->widget == C(v, FIND)) v->pending = NAV_OFFSET;
        else if (e->widget == C(v, ADDRESS)) v->pending = NAV_ADDRESS;
        else if (e->widget == C(v, RVA)) v->pending = NAV_RVA;
    }
    if (e->type == XXWIDGETS_EVENT_CLICK || e->type == XXWIDGETS_EVENT_CHANGE) {
        if (e->widget == C(v, RADIO_OFFSET)) return active_field(v, 0);
        if (e->widget == C(v, RADIO_ADDRESS)) return active_field(v, 1);
        if (e->widget == C(v, RADIO_RVA)) return active_field(v, 2);
        if (e->widget == C(v, SHOW_ALL)) v->pending = SHOW_ALL;
        else if (e->widget == C(v, RELOAD)) v->pending = RELOAD;
        else if (e->widget == C(v, SAVE)) v->pending = SAVE;
        else if (e->widget == C(v, DUMP_ALL)) v->pending = DUMP_ALL;
        else if (e->widget == C(v, DUMP)) v->pending = DUMP;
        else if (e->widget == C(v, COPY)) v->pending = COPY;
        else if (e->widget == C(v, COPY_ALL)) v->pending = COPY_ALL;
        else if (e->widget == C(v, FIND_OFFSET)) v->pending = SEARCH_OFFSET;
        else if (e->widget == C(v, FIND_ADDRESS)) v->pending = SEARCH_ADDRESS;
        else if (e->widget == C(v, FIND_RVA)) v->pending = SEARCH_RVA;
    }
    if (e->type == XXWIDGETS_EVENT_READ_ERROR && e->widget == C(v, HEX))
        return xxwidgets_widget_set_text(C(v, STATUS), "Cannot read file data; reload or choose another region.");
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_memory_map_process(xxwidgets_memory_map *v)
{
    int action;
    xxwidgets_status s = XXWIDGETS_OK;
    char *path = NULL;
    int accepted = 0;
    size_t index = SIZE_MAX;
    xxwidgets_memory_map_record r;
    if (!v) return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling || v->owner->app->syncing) return XXWIDGETS_BUSY;
    action = v->pending;
    v->pending = 0;
    if (!action) {
        if (!v->search_active) return XXWIDGETS_OK;
        s = search_step(v);
        goto report;
    }
    v->search_active = 0;
    if (action >= NAV_OFFSET && action <= NAV_RVA) s = ui_navigate(v, action - NAV_OFFSET);
    else if (action == SELECT_ROW) s = selected_location(v, 1);
    else if (action == SELECT_HEX) s = sync_hex_location(v);
    else if (action == SHOW_ALL) {
        uint64_t old = v->position;
        int valid = v->current_valid;
        s = show_rows(v, v->snapshot);
        if (s == XXWIDGETS_OK) s = selected_location(v, 1);
        if (s == XXWIDGETS_OK && valid) (void)navigate_value(v, old, 0);
    } else if (action == RELOAD || action == FORMAT || action == MODE) s = reload_map(v, action);
    else if (action >= SEARCH_OFFSET && action <= SEARCH_RVA) s = search_value(v, action - SEARCH_OFFSET);
    else if (action == SAVE || action == DUMP) {
        s = xxwidgets_file_dialog(v->owner, XXWIDGETS_FILE_DIALOG_SAVE, action == SAVE ? "Save Memory Map" : "Dump region",
                                  action == SAVE ? "Memory map.txt" : "region.bin", &path, &accepted);
        if (s == XXWIDGETS_OK && accepted) {
            if (action == SAVE) s = xxwidgets_memory_map_save(v, path);
            else {
                s = xxwidgets_memory_map_table_get_selection(C(v, TABLE), &index, &r);
                if (s == XXWIDGETS_OK) s = xxwidgets_memory_map_dump(v, v->device, index, path);
            }
        }
    } else if (action == DUMP_ALL) {
        s = xxwidgets_memory_map_choose_directory(v->owner, &path, &accepted);
        if (s == XXWIDGETS_OK && accepted) s = xxwidgets_memory_map_dump_all(v, v->device, path);
    } else if (action == COPY || action == COPY_ALL) {
        size_t required = 0;
        char *text;
        if (action == COPY) {
            s = xxwidgets_memory_map_table_get_selection(C(v, TABLE), &index, &r);
            if (index == SIZE_MAX) s = XXWIDGETS_UNAVAILABLE;
        }
        if (s == XXWIDGETS_OK) s = xxwidgets_memory_map_export_text(v, index, NULL, 0, &required);
        if (s == XXWIDGETS_OK) {
            text = malloc(required);
            if (!text) s = XXWIDGETS_OUT_OF_MEMORY;
            else {
                s = xxwidgets_memory_map_export_text(v, index, text, required, NULL);
                if (s == XXWIDGETS_OK) s = xxwidgets_clipboard_set_text(v->owner, text);
                free(text);
            }
        }
    }
    free(path);
report:
    if (s == XXWIDGETS_OUT_OF_MEMORY || s == XXWIDGETS_BUSY) return s;
    return xxwidgets_widget_set_text(C(v, STATUS), v->search_active                  ? XXW_TR(v->owner->app, "Searching file data...")
                                                   : s == XXWIDGETS_OK               ? ""
                                                   : s == XXWIDGETS_INVALID_ARGUMENT ? "Enter a valid hexadecimal location; unavailable addresses are blank."
                                                                                     : "Requested location, value or output is unavailable; previous data retained.");
}
xxwidgets_status xxwidgets_memory_map_export_text(const xxwidgets_memory_map *v, size_t index, char *buffer, size_t capacity, size_t *required)
{
    static const char header[] = "File offset\tAddress\tSize\tType\tName\tAttributes\n";
    size_t count, first, last, length = sizeof(header) - 1, pos = 0;
    xxwidgets_widget *table;
    if (!v || (!buffer && capacity)) return XXWIDGETS_INVALID_ARGUMENT;
    table = C(v, TABLE);
    count = xxwidgets_memory_map_table_count(table);
    if (index != SIZE_MAX && index >= count) return XXWIDGETS_INVALID_ARGUMENT;
    first = index == SIZE_MAX ? 0 : index;
    last = index == SIZE_MAX ? count : index + 1;
    for (size_t i = first; i < last; ++i)
        for (size_t j = 0; j < 6; ++j) length += strlen(xxwidgets_debugger_table_cell(table, i, j)) + 1;
    if (required) *required = length + 1;
    if (!buffer && !capacity) return XXWIDGETS_OK;
    if (capacity <= length) {
        if (capacity) buffer[0] = 0;
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
