/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_hexview_browser.h"
#include "xxwidgets/xxwidgets_combobox.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

struct xxwidgets_hexview_browser {
    xxwidgets_widget *owner, *controls[XXWIDGETS_HEX_BROWSER_CONTROL_COUNT];
    uint64_t size;
    int pending;
    xxwidgets_hex_copy_format copy_format;
};
#define C(v, id) ((v)->controls[XXWIDGETS_HEX_BROWSER_##id])
enum { ACTION_GO = 1, ACTION_PREVIOUS, ACTION_NEXT, ACTION_REFRESH, ACTION_ENCODING, ACTION_ROWS,
       ACTION_COPY, ACTION_DUMP, ACTION_LOCATION, ACTION_READ_ERROR };
static const char *const encodings[] = {"ASCII", "UTF-8", "UTF-16LE", "UTF-16BE"};
static const unsigned rows[] = {8, 16, 24, 32, 48, 64};
static xxwidgets_status choices(xxwidgets_widget *widget, const char *const *names, size_t count)
{
    xx_meta_string records[6] = {0};
    xx_str_w_s labels[6] = {0};
    wchar_t texts[6][16] = {{0}};
    if (count > 6) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i) {
        size_t n = strlen(names[i]);
        if (n > 15) return XXWIDGETS_INVALID_ARGUMENT;
        for (size_t j = 0; j < n; ++j) texts[i][j] = (unsigned char)names[i][j];
        labels[i].data = texts[i]; labels[i].length = n; labels[i].capacity = 16; labels[i].is_view = true;
        records[i].meta_string = labels + i; records[i].var.type = XX_VAR_TYPE_UINT64; records[i].var.val.u64 = i;
    }
    return xxwidgets_combobox_set_records(widget, records, count);
}
static xxwidgets_status copy_request(xxwidgets_widget *hex, xxwidgets_hex_copy_format format, void *user)
{
    xxwidgets_hexview_browser *v = user;
    (void)hex; v->pending = ACTION_COPY; v->copy_format = format; return XXWIDGETS_OK;
}
static xxwidgets_status dump_request(xxwidgets_widget *hex, void *user)
{
    (void)hex; ((xxwidgets_hexview_browser *)user)->pending = ACTION_DUMP; return XXWIDGETS_OK;
}
static int bounds_valid(xxwidgets_rect b)
{
    return b.x >= 0 && b.y >= 0 && b.width >= 96 && b.height >= 20 && b.x <= 32767 - b.width && b.y <= 32767 - b.height;
}
xxwidgets_status xxwidgets_hexview_browser_set_rect(xxwidgets_hexview_browser *v, xxwidgets_rect b)
{
    xxwidgets_rect r[XXWIDGETS_HEX_BROWSER_CONTROL_COUNT], old[XXWIDGETS_HEX_BROWSER_CONTROL_COUNT];
    if (!v || !bounds_valid(b)) return XXWIDGETS_INVALID_ARGUMENT;
#define R(id, dx, dy, w, h) r[XXWIDGETS_HEX_BROWSER_##id] = (xxwidgets_rect){b.x+(dx), b.y+(dy), w, h}
    R(PATH, 0, 0, b.width, 1); R(SUMMARY, 0, 2, b.width, 1);
    R(OFFSET_LABEL, 0, 4, 8, 2); R(OFFSET, 9, 4, 22, 2); R(GO, 32, 4, 8, 2);
    R(PREVIOUS, 42, 4, 12, 2); R(NEXT, 56, 4, 12, 2); R(REFRESH, 70, 4, 12, 2);
    R(ENCODING_LABEL, 0, 6, 11, 2); R(ENCODING, 12, 6, 16, 2);
    R(ROWS_LABEL, 30, 6, 6, 2); R(ROWS, 37, 6, 7, 2);
    R(COPY_HEX, 46, 6, 14, 2); R(COPY_TEXT, 62, 6, 14, 2); R(DUMP, 78, 6, 18, 2);
    R(HEX, 0, 9, b.width, b.height - 12); R(STATUS, 0, b.height - 2, b.width, 2);
#undef R
    for (size_t i = 0; i < XXWIDGETS_HEX_BROWSER_CONTROL_COUNT; ++i) old[i] = v->controls[i]->rect;
    for (size_t i = 0; i < XXWIDGETS_HEX_BROWSER_CONTROL_COUNT; ++i) {
        xxwidgets_status status = xxwidgets_widget_set_rect(v->controls[i], r[i]);
        if (status != XXWIDGETS_OK) {
            for (size_t j = 0; j < i; ++j) (void)xxwidgets_widget_set_rect(v->controls[j], old[j]);
            return status;
        }
    }
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_hexview_browser_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_hexview_browser **out)
{
    xxwidgets_hexview_browser *v;
    xxwidgets_status status;
    static const xxwidgets_kind kinds[] = {XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_EDIT,
        XXWIDGETS_BUTTON, XXWIDGETS_BUTTON, XXWIDGETS_BUTTON, XXWIDGETS_BUTTON, XXWIDGETS_LABEL, XXWIDGETS_COMBOBOX,
        XXWIDGETS_LABEL, XXWIDGETS_COMBOBOX, XXWIDGETS_BUTTON, XXWIDGETS_BUTTON, XXWIDGETS_BUTTON, XXWIDGETS_HEXVIEW, XXWIDGETS_LABEL};
    static const char *const text[] = {"", "Read only", "Offset", "0x0", "Go", "Previous", "Next", "Refresh", "Encoding", "",
        "Bytes", "", "Copy hex", "Copy text", "Save selection", "Hex bytes", "Choose a file"};
    static const char *const row_names[] = {"8", "16", "24", "32", "48", "64"};
    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !bounds_valid(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing) return XXWIDGETS_BUSY;
    v = calloc(1, sizeof(*v)); if (!v) return XXWIDGETS_OUT_OF_MEMORY;
    v->owner = owner;
    for (size_t i = 0; i < XXWIDGETS_HEX_BROWSER_CONTROL_COUNT; ++i) {
        status = xxwidgets_widget_create(owner->app, owner, kinds[i], text[i], (xxwidgets_rect){0,0,1,1}, &v->controls[i]);
        if (status != XXWIDGETS_OK) goto fail;
    }
    status = xxwidgets_hexview_browser_set_rect(v, bounds);
    if (status == XXWIDGETS_OK) status = choices(C(v, ENCODING), encodings, 4);
    if (status == XXWIDGETS_OK) status = choices(C(v, ROWS), row_names, 6);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(C(v, ENCODING), 0);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(C(v, ROWS), 1);
    if (status == XXWIDGETS_OK) status = xxwidgets_hexview_set_copy_handler(C(v, HEX), copy_request, v);
    if (status == XXWIDGETS_OK) status = xxwidgets_hexview_set_dump_handler(C(v, HEX), dump_request, v);
    if (status != XXWIDGETS_OK) goto fail;
    *out = v; return XXWIDGETS_OK;
fail:
    xxwidgets_hexview_browser_destroy(v); return status;
}
xxwidgets_status xxwidgets_hexview_browser_destroy(xxwidgets_hexview_browser *v)
{
    if (!v) return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling || v->owner->app->syncing) return XXWIDGETS_BUSY;
    for (size_t i = 0; i < XXWIDGETS_HEX_BROWSER_CONTROL_COUNT; ++i) if (v->controls[i]) (void)xxwidgets_widget_destroy(v->controls[i]);
    free(v); return XXWIDGETS_OK;
}
xxwidgets_widget *xxwidgets_hexview_browser_control(const xxwidgets_hexview_browser *v, xxwidgets_hexview_browser_control_id id)
{
    return v && id >= 0 && id < XXWIDGETS_HEX_BROWSER_CONTROL_COUNT ? v->controls[id] : NULL;
}
static xxwidgets_status location(xxwidgets_hexview_browser *v)
{
    uint64_t base, offset, length;
    unsigned columns;
    char text[192];
    xx_io_device *device = xxwidgets_hexview_get_device(C(v, HEX));
    int64_t size = xx_io_total_size(device);
    xxwidgets_status status;
    if (size < 0) return XXWIDGETS_PLATFORM_ERROR;
    v->size = (uint64_t)size;
    status = xxwidgets_hexview_get_layout(C(v, HEX), &base, &columns);
    if (status == XXWIDGETS_OK) status = xxwidgets_hexview_get_source_selection(C(v, HEX), &offset, &length);
    if (status != XXWIDGETS_OK) return status;
    {
        xxwidgets_hex_display display;
        int encoding_index = -1, row_index = -1;
        status = xxwidgets_hexview_get_display(C(v, HEX), &display);
        if (status != XXWIDGETS_OK) return status;
        for (size_t i = 0; i < 4; ++i) if (!strcmp(display.encoding, encodings[i])) encoding_index = (int)i;
        for (size_t i = 0; i < 6; ++i) if (columns == rows[i]) row_index = (int)i;
        status = xxwidgets_widget_set_value(C(v, ENCODING), encoding_index);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(C(v, ROWS), row_index);
        if (status != XXWIDGETS_OK) return status;
    }
    snprintf(text, sizeof(text), "0x%" PRIX64, length ? offset : base);
    status = xxwidgets_widget_set_text(C(v, OFFSET), text);
    if (status != XXWIDGETS_OK) return status;
    snprintf(text, sizeof(text), "%" PRIu64 " bytes | Read only", v->size);
    status = xxwidgets_widget_set_text(C(v, SUMMARY), text);
    if (status != XXWIDGETS_OK) return status;
    if (length) snprintf(text, sizeof(text), "Offset 0x%" PRIX64 " | Selection %" PRIu64 " bytes | Window 0x%" PRIX64, offset, length, base);
    else snprintf(text, sizeof(text), "%s | Offset 0x%" PRIX64, v->size ? "End of file" : "Empty file", base);
    status = xxwidgets_widget_set_text(C(v, STATUS), text);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(C(v, PREVIOUS), base > 0);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(C(v, NEXT), base < v->size && v->size - base > XXWIDGETS_HEXVIEW_BROWSER_WINDOW);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(C(v, COPY_HEX), length > 0);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(C(v, COPY_TEXT), length > 0);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(C(v, DUMP), length > 0);
    return status;
}
xxwidgets_status xxwidgets_hexview_browser_set_device(xxwidgets_hexview_browser *v, const char *path, xx_io_device *device)
{
    xxwidgets_widget *fields[4];
    char *texts[4] = {0}, *old[4];
    xxwidgets_widget *buttons[5];
    int enabled[5], wanted[5];
    char summary[96];
    int64_t size;
    xxwidgets_status status = XXWIDGETS_OUT_OF_MEMORY;
    if (!v || !device || !path || !xxwidgets_valid_utf8(path) || (size = xx_io_total_size(device)) < 0) return XXWIDGETS_INVALID_ARGUMENT;
    fields[0] = C(v, PATH); fields[1] = C(v, SUMMARY); fields[2] = C(v, OFFSET); fields[3] = C(v, STATUS);
    snprintf(summary, sizeof(summary), "%" PRIu64 " bytes | Read only", (uint64_t)size);
    texts[0] = xxwidgets_strdup(path); texts[1] = xxwidgets_strdup(summary); texts[2] = xxwidgets_strdup("0x0");
    texts[3] = xxwidgets_strdup(size ? "Offset 0x0 | Selection 1 byte" : "Empty file");
    for (size_t i = 0; i < 4; ++i) if (!texts[i]) goto finish;
    buttons[0] = C(v, PREVIOUS); buttons[1] = C(v, NEXT); buttons[2] = C(v, COPY_HEX); buttons[3] = C(v, COPY_TEXT); buttons[4] = C(v, DUMP);
    wanted[0] = 0; wanted[1] = (uint64_t)size > XXWIDGETS_HEXVIEW_BROWSER_WINDOW;
    wanted[2] = wanted[3] = wanted[4] = size > 0;
    for (size_t i = 0; i < 4; ++i) { old[i] = fields[i]->text; fields[i]->text = texts[i]; }
    for (size_t i = 0; i < 5; ++i) { enabled[i] = buttons[i]->enabled; buttons[i]->enabled = wanted[i]; }
    status = XXWIDGETS_OK;
    ++v->owner->app->syncing;
    for (size_t i = 0; i < 4 && status == XXWIDGETS_OK; ++i) status = v->owner->app->ops->sync(fields[i]);
    for (size_t i = 0; i < 5 && status == XXWIDGETS_OK; ++i) status = v->owner->app->ops->sync(buttons[i]);
    --v->owner->app->syncing;
    if (status == XXWIDGETS_OK) status = xxwidgets_hexview_set_device(C(v, HEX), device, 0, XXWIDGETS_HEXVIEW_BROWSER_WINDOW, 0);
    if (status == XXWIDGETS_OK) {
        for (size_t i = 0; i < 4; ++i) { free(old[i]); texts[i] = NULL; }
        v->size = (uint64_t)size; v->pending = 0;
    } else {
        for (size_t i = 0; i < 4; ++i) fields[i]->text = old[i];
        for (size_t i = 0; i < 5; ++i) buttons[i]->enabled = enabled[i];
        ++v->owner->app->syncing;
        for (size_t i = 0; i < 4; ++i) (void)v->owner->app->ops->sync(fields[i]);
        for (size_t i = 0; i < 5; ++i) (void)v->owner->app->ops->sync(buttons[i]);
        --v->owner->app->syncing;
    }
finish:
    for (size_t i = 0; i < 4; ++i) free(texts[i]);
    return status;
}
xxwidgets_status xxwidgets_hexview_browser_set_options(xxwidgets_hexview_browser *v, const xxwidgets_hex_options *options)
{
    uint64_t base;
    unsigned columns;
    int row_index = -1;
    xxwidgets_status status;
    if (!v || !options || (options->bold_nonzero != 0 && options->bold_nonzero != 1) || !xxwidgets_font_valid(&options->font))
        return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < 6; ++i) if (options->bytes_per_row == rows[i]) row_index = (int)i;
    if (row_index < 0) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_hexview_get_layout(C(v, HEX), &base, &columns);
    if (status == XXWIDGETS_OK && columns != options->bytes_per_row) status = xxwidgets_hexview_set_layout(C(v, HEX), base, options->bytes_per_row);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(C(v, ROWS), row_index);
    if (status == XXWIDGETS_OK) status = xxwidgets_hexview_set_bold_nonzero(C(v, HEX), options->bold_nonzero);
    if (status == XXWIDGETS_OK) {
        status = xxwidgets_hexview_set_font(C(v, HEX), &options->font);
        if (status == XXWIDGETS_UNAVAILABLE) status = XXWIDGETS_OK;
    }
    return status;
}
static int parse_offset(const char *text, uint64_t *offset)
{
    uint64_t value = 0;
    unsigned radix = 10, digit;
    int any = 0;
    const unsigned char *p = (const unsigned char *)text;
    if (!text) return 0;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { radix = 16; p += 2; }
    for (; *p; ++p) {
        if (*p >= '0' && *p <= '9') digit = *p - '0';
        else if (*p >= 'a' && *p <= 'f') digit = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') digit = *p - 'A' + 10;
        else break;
        if (digit >= radix || value > (UINT64_MAX - digit) / radix) return 0;
        value = value * radix + digit; any = 1;
    }
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (!any || *p) return 0;
    *offset = value; return 1;
}
xxwidgets_status xxwidgets_hexview_browser_go_to(xxwidgets_hexview_browser *v, const char *text)
{
    uint64_t offset;
    xxwidgets_status status;
    if (!v || !parse_offset(text, &offset)) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_hexview_seek_device(C(v, HEX), offset);
    return status == XXWIDGETS_OK ? location(v) : status;
}
xxwidgets_status xxwidgets_hexview_browser_refresh(xxwidgets_hexview_browser *v)
{
    uint64_t base;
    unsigned columns;
    int64_t size;
    xxwidgets_status status;
    if (!v) return XXWIDGETS_INVALID_ARGUMENT;
    size = xx_io_total_size(xxwidgets_hexview_get_device(C(v, HEX)));
    status = xxwidgets_hexview_get_layout(C(v, HEX), &base, &columns);
    if (status != XXWIDGETS_OK) return status;
    if (size >= 0 && base > (uint64_t)size) status = xxwidgets_hexview_seek_device(C(v, HEX), (uint64_t)size);
    else status = xxwidgets_hexview_refresh_device(C(v, HEX));
    return status == XXWIDGETS_OK ? location(v) : status;
}
xxwidgets_status xxwidgets_hexview_browser_handle_event(xxwidgets_hexview_browser *v, const xxwidgets_event *event)
{
    if (!v || !event) return XXWIDGETS_INVALID_ARGUMENT;
    if (event->widget == C(v, HEX)) {
        if (event->type == XXWIDGETS_EVENT_READ_ERROR) v->pending = ACTION_READ_ERROR;
        else if (event->type == XXWIDGETS_EVENT_SELECT || event->type == XXWIDGETS_EVENT_VIEW_CHANGE) v->pending = ACTION_LOCATION;
    } else if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == C(v, GO)) v->pending = ACTION_GO;
        else if (event->widget == C(v, PREVIOUS)) v->pending = ACTION_PREVIOUS;
        else if (event->widget == C(v, NEXT)) v->pending = ACTION_NEXT;
        else if (event->widget == C(v, REFRESH)) v->pending = ACTION_REFRESH;
        else if (event->widget == C(v, DUMP)) v->pending = ACTION_DUMP;
        else if (event->widget == C(v, COPY_HEX) || event->widget == C(v, COPY_TEXT)) {
            v->pending = ACTION_COPY; v->copy_format = event->widget == C(v, COPY_HEX) ? XXWIDGETS_HEX_COPY_HEX : XXWIDGETS_HEX_COPY_TEXT;
        }
    } else if (event->type == XXWIDGETS_EVENT_ACTIVATE && event->widget == C(v, OFFSET)) v->pending = ACTION_GO;
    else if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == C(v, ENCODING)) v->pending = ACTION_ENCODING;
    else if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == C(v, ROWS)) v->pending = ACTION_ROWS;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_hexview_browser_process(xxwidgets_hexview_browser *v)
{
    xxwidgets_status status = XXWIDGETS_OK;
    uint64_t base, offset, length;
    unsigned columns;
    int value, action;
    if (!v) return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling || v->owner->app->syncing) return XXWIDGETS_BUSY;
    action = v->pending; v->pending = 0;
    if (!action) return XXWIDGETS_OK;
    if (action == ACTION_READ_ERROR)
        return xxwidgets_widget_set_text(C(v, STATUS), "Cannot read this location; previous bytes retained. Try another offset or Refresh.");
    if (action == ACTION_GO) {
        char text[128];
        status = xxwidgets_widget_get_text(C(v, OFFSET), text, sizeof(text), NULL);
        if (status == XXWIDGETS_OK) status = xxwidgets_hexview_browser_go_to(v, text);
    } else if (action == ACTION_REFRESH) status = xxwidgets_hexview_browser_refresh(v);
    else if (action == ACTION_LOCATION) return location(v);
    else if (action == ACTION_PREVIOUS || action == ACTION_NEXT) {
        status = xxwidgets_hexview_get_layout(C(v, HEX), &base, &columns);
        if (status == XXWIDGETS_OK) {
            offset = action == ACTION_PREVIOUS ? (base ? (base - 1) / XXWIDGETS_HEXVIEW_BROWSER_WINDOW * XXWIDGETS_HEXVIEW_BROWSER_WINDOW : 0) :
                (base / XXWIDGETS_HEXVIEW_BROWSER_WINDOW + 1) * XXWIDGETS_HEXVIEW_BROWSER_WINDOW;
            if (offset > v->size) offset = v->size;
            status = xxwidgets_hexview_seek_device(C(v, HEX), offset);
        }
        if (status == XXWIDGETS_OK) status = location(v);
    } else if (action == ACTION_ENCODING) {
        xxwidgets_hex_display display;
        status = xxwidgets_widget_get_value(C(v, ENCODING), &value);
        if (status == XXWIDGETS_OK && value >= 0 && value < 4) {
            status = xxwidgets_hexview_get_display(C(v, HEX), &display);
            display.encoding = encodings[value];
            if (status == XXWIDGETS_OK) status = xxwidgets_hexview_set_display(C(v, HEX), &display);
        } else status = XXWIDGETS_INVALID_ARGUMENT;
    } else if (action == ACTION_ROWS) {
        status = xxwidgets_widget_get_value(C(v, ROWS), &value);
        if (status == XXWIDGETS_OK && value >= 0 && value < 6) {
            status = xxwidgets_hexview_get_layout(C(v, HEX), &base, &columns);
            if (status == XXWIDGETS_OK) status = xxwidgets_hexview_set_layout(C(v, HEX), base, rows[value]);
        } else status = XXWIDGETS_INVALID_ARGUMENT;
    } else if (action == ACTION_DUMP) status = xxwidgets_hexview_dump_dialog(C(v, HEX));
    else if (action == ACTION_COPY) {
        status = xxwidgets_hexview_get_source_selection(C(v, HEX), &offset, &length);
        if (status == XXWIDGETS_OK && v->copy_format != XXWIDGETS_HEX_COPY_OFFSET && length > XXWIDGETS_HEXVIEW_BROWSER_COPY_LIMIT)
            return xxwidgets_widget_set_text(C(v, STATUS), "Select at most 1 MiB to copy. Save selection can export larger ranges.");
        if (status == XXWIDGETS_OK) status = xxwidgets_hexview_set_copy_handler(C(v, HEX), NULL, NULL);
        if (status == XXWIDGETS_OK) status = xxwidgets_hexview_copy_selection(C(v, HEX), v->copy_format);
        (void)xxwidgets_hexview_set_copy_handler(C(v, HEX), copy_request, v);
        if (status == XXWIDGETS_OK) return xxwidgets_widget_set_text(C(v, STATUS), "Copied");
    }
    if (status != XXWIDGETS_OK)
        return xxwidgets_widget_set_text(C(v, STATUS), "Cannot complete action. Check the offset or file access; previous bytes retained on read failure.");
    return status;
}
