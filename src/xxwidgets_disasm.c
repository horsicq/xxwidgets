/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets/xxwidgets_disasm.h"
#include "xxwidgets/xxwidgets_combobox.h"
#include "xxwidgets_internal.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct xxwidgets_disasm_browser {
    xxwidgets_widget *owner, *controls[XXWIDGETS_DISASM_CONTROL_COUNT];
    xxwidgets_disasm_load_fn load;
    void *user;
    char *description;
    uint64_t size, offset, next, history[256], offsets[XXWIDGETS_DISASM_MAX_ROWS];
    unsigned sizes[XXWIDGETS_DISASM_MAX_ROWS];
    size_t count, mode, mode_count, history_count;
    int pending;
};
#define C(v, id) ((v)->controls[XXWIDGETS_DISASM_##id])
enum { GO = 1, BACK, NEXT, REFRESH, MODE, COPY };
static int valid_bounds(xxwidgets_rect b)
{
    return b.x >= 0 && b.y >= 0 && b.width >= 96 && b.height >= 20 && b.x <= 32767 - b.width && b.y <= 32767 - b.height;
}
xxwidgets_status xxwidgets_disasm_browser_set_rect(xxwidgets_disasm_browser *v, xxwidgets_rect b)
{
    xxwidgets_rect r[XXWIDGETS_DISASM_CONTROL_COUNT], old[XXWIDGETS_DISASM_CONTROL_COUNT];
    if (!v || !valid_bounds(b))
        return XXWIDGETS_INVALID_ARGUMENT;
#define R(id, dx, dy, w, h) r[XXWIDGETS_DISASM_##id] = (xxwidgets_rect){b.x + (dx), b.y + (dy), w, h}
    R(PATH, 0, 0, b.width, 1);
    R(SUMMARY, 0, 2, b.width, 1);
    R(OFFSET_LABEL, 0, 4, 8, 2);
    R(OFFSET, 9, 4, 22, 2);
    R(GO, 32, 4, 8, 2);
    R(BACK, 42, 4, 10, 2);
    R(NEXT, 54, 4, 10, 2);
    R(REFRESH, 66, 4, 12, 2);
    R(MODE_LABEL, 0, 6, 8, 2);
    R(MODE, 9, 6, 24, 2);
    R(COPY, 36, 6, 14, 2);
    R(TABLE, 0, 9, b.width, b.height - 12);
    R(STATUS, 0, b.height - 2, b.width, 2);
#undef R
    for (size_t i = 0; i < XXWIDGETS_DISASM_CONTROL_COUNT; ++i)
        old[i] = v->controls[i]->rect;
    for (size_t i = 0; i < XXWIDGETS_DISASM_CONTROL_COUNT; ++i) {
        xxwidgets_status status = xxwidgets_widget_set_rect(v->controls[i], r[i]);
        if (status != XXWIDGETS_OK) {
            for (size_t j = 0; j < i; ++j)
                (void)xxwidgets_widget_set_rect(v->controls[j], old[j]);
            return status;
        }
    }
    return XXWIDGETS_OK;
}
static xxwidgets_status set_modes(xxwidgets_widget *widget, const xxwidgets_disasm_source *s)
{
    xx_meta_string records[16] = {0};
    xx_str_w_s labels[16] = {0};
    wchar_t text[16][32] = {{0}};
    for (size_t i = 0; i < s->mode_count; ++i) {
        size_t n = strlen(s->mode_names[i]);
        for (size_t j = 0; j < n; ++j)
            text[i][j] = (unsigned char)s->mode_names[i][j];
        labels[i].data = text[i];
        labels[i].length = n;
        labels[i].capacity = 32;
        labels[i].is_view = true;
        records[i].meta_string = labels + i;
        records[i].var.type = XX_VAR_TYPE_UINT64;
        records[i].var.val.u64 = i;
    }
    return xxwidgets_combobox_set_records(widget, records, s->mode_count);
}
static xxwidgets_status apply_page(xxwidgets_disasm_browser *v, const xxwidgets_disasm_page *p, size_t mode, int action)
{
    xxwidgets_debugger_instruction rows[XXWIDGETS_DISASM_MAX_ROWS];
    uint64_t end = p->offset;
    char offset_text[32], summary[512], status_text[128];
    char *texts[3] = {0}, *old[3];
    xxwidgets_widget *labels[3] = {C(v, OFFSET), C(v, SUMMARY), C(v, STATUS)};
    xxwidgets_widget *buttons[3] = {C(v, BACK), C(v, NEXT), C(v, COPY)};
    int enabled[3], wanted[3];
    xxwidgets_status status = XXWIDGETS_OUT_OF_MEMORY;
    if (p->offset > p->file_size || p->next_offset > p->file_size || p->count > XXWIDGETS_DISASM_MAX_ROWS || (p->count && !p->records) ||
        (!p->count && p->offset != p->file_size))
        return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < p->count; ++i) {
        const xxwidgets_disasm_record *r = p->records + i;
        if (r->offset != end || !r->size || r->size > 15 || r->size > p->file_size - end || !xxwidgets_valid_utf8(r->bytes) ||
            !xxwidgets_valid_utf8(r->instruction) || !xxwidgets_valid_utf8(r->comment) || strlen(r->bytes) > 512 ||
            strlen(r->instruction) > 512 || strlen(r->comment) > 512)
            return XXWIDGETS_INVALID_ARGUMENT;
        rows[i] = (xxwidgets_debugger_instruction){r->address, r->bytes, r->instruction, r->comment, 0};
        end += r->size;
    }
    if (p->arm != 0 && p->arm != 1) return XXWIDGETS_INVALID_ARGUMENT;
    if (end != p->next_offset)
        return XXWIDGETS_INVALID_ARGUMENT;
    snprintf(offset_text, sizeof(offset_text), "0x%" PRIX64, p->offset);
    snprintf(summary, sizeof(summary), "%s | %" PRIu64 " bytes", v->description, p->file_size);
    snprintf(status_text, sizeof(status_text), "%s | File offset 0x%" PRIX64 " | %zu rows", p->count ? "Linear disassembly" : "End of file",
             p->offset, p->count);
    texts[0] = xxwidgets_strdup(offset_text);
    texts[1] = xxwidgets_strdup(summary);
    texts[2] = xxwidgets_strdup(status_text);
    for (size_t i = 0; i < 3; ++i)
        if (!texts[i])
            goto done;
    wanted[0] = action == NEXT ? 1 : action == BACK ? v->history_count > 1 : action == GO || action == MODE ? 0 : v->history_count != 0;
    wanted[1] = p->next_offset < p->file_size;
    wanted[2] = p->count != 0;
    for (size_t i = 0; i < 3; ++i) {
        old[i] = labels[i]->text;
        labels[i]->text = texts[i];
        enabled[i] = buttons[i]->enabled;
        buttons[i]->enabled = wanted[i];
    }
    status = XXWIDGETS_OK;
    ++v->owner->app->syncing;
    for (size_t i = 0; i < 3 && status == XXWIDGETS_OK; ++i)
        status = v->owner->app->ops->sync(labels[i]);
    for (size_t i = 0; i < 3 && status == XXWIDGETS_OK; ++i)
        status = v->owner->app->ops->sync(buttons[i]);
    --v->owner->app->syncing;
    if (status == XXWIDGETS_OK) {
        int previous_arm = C(v, TABLE)->disasm_arm;
        C(v, TABLE)->disasm_arm = p->arm;
        status = xxwidgets_debugger_disasm_set_rows(C(v, TABLE), rows, p->count);
        if (status != XXWIDGETS_OK) C(v, TABLE)->disasm_arm = previous_arm;
    }
    if (status != XXWIDGETS_OK) {
        for (size_t i = 0; i < 3; ++i) {
            labels[i]->text = old[i];
            buttons[i]->enabled = enabled[i];
        }
        ++v->owner->app->syncing;
        for (size_t i = 0; i < 3; ++i) {
            (void)v->owner->app->ops->sync(labels[i]);
            (void)v->owner->app->ops->sync(buttons[i]);
        }
        --v->owner->app->syncing;
        goto done;
    }
    for (size_t i = 0; i < 3; ++i) {
        free(old[i]);
        texts[i] = NULL;
    }
    if (action == NEXT) {
        if (v->history_count == 256) {
            memmove(v->history, v->history + 1, 255 * sizeof(*v->history));
            --v->history_count;
        }
        v->history[v->history_count++] = v->offset;
    } else if (action == BACK && v->history_count)
        --v->history_count;
    else if (action == GO || action == MODE)
        v->history_count = 0;
    v->size = p->file_size;
    v->offset = p->offset;
    v->next = p->next_offset;
    v->mode = mode;
    v->count = p->count;
    for (size_t i = 0; i < p->count; ++i) {
        v->offsets[i] = p->records[i].offset;
        v->sizes[i] = p->records[i].size;
    }
    status = XXWIDGETS_OK;
done:
    for (size_t i = 0; i < 3; ++i)
        free(texts[i]);
    return status;
}
static xxwidgets_status load_page(xxwidgets_disasm_browser *v, uint64_t offset, size_t mode, int action)
{
    xxwidgets_disasm_page page = {0};
    xxwidgets_status status;
    if (v->owner->app->dispatch_depth || v->owner->app->polling || v->owner->app->syncing)
        return XXWIDGETS_BUSY;
    if (mode >= v->mode_count)
        return XXWIDGETS_INVALID_ARGUMENT;
    status = v->load(v->user, offset, mode, &page);
    if (status == XXWIDGETS_OK && page.offset != offset)
        return XXWIDGETS_INVALID_ARGUMENT;
    return status == XXWIDGETS_OK ? apply_page(v, &page, mode, action) : status;
}
xxwidgets_status xxwidgets_disasm_browser_create(xxwidgets_widget *owner, xxwidgets_rect bounds, const xxwidgets_disasm_source *s,
                                                 xxwidgets_disasm_browser **out)
{
    static const xxwidgets_kind kinds[] = {XXWIDGETS_LABEL,  XXWIDGETS_LABEL,    XXWIDGETS_LABEL,  XXWIDGETS_EDIT,
                                           XXWIDGETS_BUTTON, XXWIDGETS_BUTTON,   XXWIDGETS_BUTTON, XXWIDGETS_BUTTON,
                                           XXWIDGETS_LABEL,  XXWIDGETS_COMBOBOX, XXWIDGETS_BUTTON, XXWIDGETS_DEBUGGER_DISASM,
                                           XXWIDGETS_LABEL};
    static const char *const names[] = {"",        "",     "Offset", "0x0",      "Go",          "Back",         "Next",
                                        "Refresh", "Mode", "",       "Copy row", "Disassembly", "Select a mode"};
    xxwidgets_disasm_browser *v;
    xxwidgets_status status;
    if (!out)
        return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !valid_bounds(bounds) || !s || !s->load || !s->mode_names || !s->mode_count ||
        s->mode_count > 16 || s->initial_mode >= s->mode_count || s->initial_offset > s->file_size || !xxwidgets_valid_utf8(s->path) ||
        !xxwidgets_valid_utf8(s->description) || strlen(s->description) > 256)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing)
        return XXWIDGETS_BUSY;
    for (size_t i = 0; i < s->mode_count; ++i) {
        const unsigned char *p = (const unsigned char *)s->mode_names[i];
        if (!p || !*p || strlen((const char *)p) > 31)
            return XXWIDGETS_INVALID_ARGUMENT;
        for (; *p; ++p)
            if (*p < 32 || *p >= 127)
                return XXWIDGETS_INVALID_ARGUMENT;
    }
    v = calloc(1, sizeof(*v));
    if (!v)
        return XXWIDGETS_OUT_OF_MEMORY;
    v->owner = owner;
    v->load = s->load;
    v->user = s->user;
    v->size = s->file_size;
    v->offset = s->initial_offset;
    v->mode = s->initial_mode;
    v->mode_count = s->mode_count;
    v->description = xxwidgets_strdup(s->description);
    if (!v->description) {
        free(v);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < XXWIDGETS_DISASM_CONTROL_COUNT; ++i) {
        status = xxwidgets_widget_create(owner->app, owner, kinds[i], names[i], (xxwidgets_rect){0, 0, 1, 1}, &v->controls[i]);
        if (status != XXWIDGETS_OK)
            goto fail;
    }
    status = xxwidgets_disasm_browser_set_rect(v, bounds);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_set_text(C(v, PATH), s->path);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_set_text(C(v, SUMMARY), s->description);
    if (status == XXWIDGETS_OK)
        status = set_modes(C(v, MODE), s);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_set_value(C(v, MODE), (int)s->initial_mode);
    if (status == XXWIDGETS_OK)
        status = load_page(v, v->offset, v->mode, GO);
    if (status == XXWIDGETS_UNAVAILABLE) {
        status = xxwidgets_widget_set_text(C(v, STATUS), "Choose an instruction mode for this file.");
        (void)xxwidgets_widget_set_enabled(C(v, NEXT), 0);
        (void)xxwidgets_widget_set_enabled(C(v, BACK), 0);
        (void)xxwidgets_widget_set_enabled(C(v, COPY), 0);
    }
    if (status != XXWIDGETS_OK)
        goto fail;
    *out = v;
    return XXWIDGETS_OK;
fail:
    xxwidgets_disasm_browser_destroy(v);
    return status;
}
xxwidgets_status xxwidgets_disasm_browser_destroy(xxwidgets_disasm_browser *v)
{
    if (!v)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling || v->owner->app->syncing)
        return XXWIDGETS_BUSY;
    for (size_t i = 0; i < XXWIDGETS_DISASM_CONTROL_COUNT; ++i)
        if (v->controls[i])
            (void)xxwidgets_widget_destroy(v->controls[i]);
    free(v->description);
    free(v);
    return XXWIDGETS_OK;
}
xxwidgets_widget *xxwidgets_disasm_browser_control(const xxwidgets_disasm_browser *v, xxwidgets_disasm_control_id id)
{
    return v && id >= 0 && id < XXWIDGETS_DISASM_CONTROL_COUNT ? v->controls[id] : NULL;
}
size_t xxwidgets_disasm_browser_count(const xxwidgets_disasm_browser *v)
{
    return v ? v->count : 0;
}
xxwidgets_status xxwidgets_disasm_browser_get_record(const xxwidgets_disasm_browser *v, size_t index, xxwidgets_disasm_record *r)
{
    xxwidgets_debugger_instruction row;
    xxwidgets_status status;
    if (!v || !r || index >= v->count)
        return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_debugger_disasm_get_row(C(v, TABLE), index, &row);
    if (status == XXWIDGETS_OK)
        *r = (xxwidgets_disasm_record){v->offsets[index], row.address, v->sizes[index], row.bytes, row.instruction, row.comment};
    return status;
}
static int parse_offset(const char *text, uint64_t *offset)
{
    const unsigned char *p = (const unsigned char *)text;
    uint64_t value = 0;
    unsigned base = 10, digit;
    int any = 0;
    if (!p)
        return 0;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        ++p;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    }
    for (; *p; ++p) {
        if (*p >= '0' && *p <= '9')
            digit = *p - '0';
        else if (*p >= 'a' && *p <= 'f')
            digit = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F')
            digit = *p - 'A' + 10;
        else
            break;
        if (digit >= base || value > (UINT64_MAX - digit) / base)
            return 0;
        value = value * base + digit;
        any = 1;
    }
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        ++p;
    if (!any || *p)
        return 0;
    *offset = value;
    return 1;
}
xxwidgets_status xxwidgets_disasm_browser_go_to(xxwidgets_disasm_browser *v, const char *text)
{
    uint64_t offset;
    if (!v || !parse_offset(text, &offset))
        return XXWIDGETS_INVALID_ARGUMENT;
    return load_page(v, offset, v->mode, GO);
}
xxwidgets_status xxwidgets_disasm_browser_handle_event(xxwidgets_disasm_browser *v, const xxwidgets_event *e)
{
    if (!v || !e)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (e->type == XXWIDGETS_EVENT_CLICK) {
        if (e->widget == C(v, GO))
            v->pending = GO;
        else if (e->widget == C(v, BACK))
            v->pending = BACK;
        else if (e->widget == C(v, NEXT))
            v->pending = NEXT;
        else if (e->widget == C(v, REFRESH))
            v->pending = REFRESH;
        else if (e->widget == C(v, COPY))
            v->pending = COPY;
    } else if (e->type == XXWIDGETS_EVENT_ACTIVATE && e->widget == C(v, OFFSET))
        v->pending = GO;
    else if (e->type == XXWIDGETS_EVENT_SELECT && e->widget == C(v, MODE))
        v->pending = MODE;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_disasm_browser_process(xxwidgets_disasm_browser *v)
{
    int action, value;
    xxwidgets_status status = XXWIDGETS_OK;
    if (!v)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling || v->owner->app->syncing)
        return XXWIDGETS_BUSY;
    action = v->pending;
    v->pending = 0;
    if (!action)
        return XXWIDGETS_OK;
    if (action == GO) {
        char text[128];
        status = xxwidgets_widget_get_text(C(v, OFFSET), text, sizeof(text), NULL);
        if (status == XXWIDGETS_OK)
            status = xxwidgets_disasm_browser_go_to(v, text);
    } else if (action == BACK) {
        if (v->history_count)
            status = load_page(v, v->history[v->history_count - 1], v->mode, BACK);
    } else if (action == NEXT) {
        if (v->next < v->size)
            status = load_page(v, v->next, v->mode, NEXT);
    } else if (action == REFRESH)
        status = load_page(v, v->offset, v->mode, REFRESH);
    else if (action == MODE) {
        status = xxwidgets_widget_get_value(C(v, MODE), &value);
        if (status == XXWIDGETS_OK && value >= 0)
            status = load_page(v, v->offset, (size_t)value, MODE);
        else
            status = XXWIDGETS_INVALID_ARGUMENT;
        if (status != XXWIDGETS_OK)
            (void)xxwidgets_widget_set_value(C(v, MODE), (int)v->mode);
    } else if (action == COPY) {
        size_t index;
        xxwidgets_debugger_instruction row;
        char text[2048];
        status = xxwidgets_debugger_disasm_get_selection(C(v, TABLE), &index, &row);
        if (status == XXWIDGETS_OK && index != SIZE_MAX) {
            snprintf(text, sizeof(text), "0x%" PRIX64 "\t%s\t%s\t%s", row.address, row.bytes, row.instruction, row.comment);
            status = xxwidgets_clipboard_set_text(v->owner, text);
            return xxwidgets_widget_set_text(C(v, STATUS), status == XXWIDGETS_OK ? "Copied" : "Clipboard unavailable");
        }
    }
    if (status != XXWIDGETS_OK)
        return xxwidgets_widget_set_text(C(v, STATUS), status == XXWIDGETS_UNAVAILABLE
                                                           ? "Choose a supported instruction mode."
                                                           : "Cannot read/decode that offset; previous page retained.");
    return status;
}
