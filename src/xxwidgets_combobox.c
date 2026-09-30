#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_combobox.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

struct xxwidgets_combo_state {
    xx_meta_string *records;
    unsigned char *checked;
    size_t count;
    char summary[64];
};

static void dispose_state(struct xxwidgets_combo_state *state)
{
    size_t i;
    if (!state) return;
    for (i = 0; i < state->count; ++i) {
        xx_meta_string *record = state->records + i;
        if (record->meta_string) { free(record->meta_string->data); free(record->meta_string); }
        switch (record->var.type) {
        case XX_VAR_TYPE_STRING: case XX_VAR_TYPE_STRING_VIEW: free(record->var.val.str.ptr); break;
        case XX_VAR_TYPE_WSTRING: case XX_VAR_TYPE_WSTRING_VIEW: free(record->var.val.wstr.ptr); break;
        case XX_VAR_TYPE_BYTES: case XX_VAR_TYPE_BYTES_VIEW: free(record->var.val.bytes.data); break;
        default: break;
        }
    }
    free(state->records); free(state->checked); free(state);
}

void xxwidgets_combobox_dispose(xxwidgets_widget *widget)
{
    dispose_state(widget->combo); widget->combo = NULL;
}

/* Labels are validated as Unicode, including surrogate pairs on Windows. */
static char *wide_utf8(const wchar_t *wide, size_t length, xxwidgets_status *status)
{
    size_t i, used = 0;
    char *text;
    *status = XXWIDGETS_OUT_OF_MEMORY;
    if (length > (SIZE_MAX - 1) / 4) { *status = XXWIDGETS_INVALID_ARGUMENT; return NULL; }
    text = (char *)malloc(length * 4 + 1);
    if (!text) return NULL;
    for (i = 0; i < length; ++i) {
        uint32_t cp = (uint32_t)wide[i];
        if (sizeof(wchar_t) == 2 && cp >= 0xd800 && cp <= 0xdbff) {
            uint32_t low;
            if (++i >= length) goto invalid;
            low = (uint32_t)wide[i];
            if (low < 0xdc00 || low > 0xdfff) goto invalid;
            cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
        }
        if (!cp || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) goto invalid;
        if (cp < 0x80) text[used++] = (char)cp;
        else if (cp < 0x800) {
            text[used++] = (char)(0xc0 | (cp >> 6)); text[used++] = (char)(0x80 | (cp & 63));
        } else if (cp < 0x10000) {
            text[used++] = (char)(0xe0 | (cp >> 12)); text[used++] = (char)(0x80 | ((cp >> 6) & 63));
            text[used++] = (char)(0x80 | (cp & 63));
        } else {
            text[used++] = (char)(0xf0 | (cp >> 18)); text[used++] = (char)(0x80 | ((cp >> 12) & 63));
            text[used++] = (char)(0x80 | ((cp >> 6) & 63)); text[used++] = (char)(0x80 | (cp & 63));
        }
    }
    text[used] = 0; *status = XXWIDGETS_OK; return text;
invalid:
    free(text); *status = XXWIDGETS_INVALID_ARGUMENT; return NULL;
}

static xxwidgets_status copy_var(xx_var *to, const xx_var *from)
{
    size_t size = 0, extra = 0;
    const void *data = NULL;
    void *copy;
    if (from->type > XX_VAR_TYPE_PTR) return XXWIDGETS_INVALID_ARGUMENT;
    *to = *from; to->is_allocated = false; to->free_fn = NULL;
    switch (from->type) {
    case XX_VAR_TYPE_STRING: case XX_VAR_TYPE_STRING_VIEW:
        data = from->val.str.ptr; size = from->val.str.len; extra = 1; to->val.str.ptr = NULL; break;
    case XX_VAR_TYPE_WSTRING: case XX_VAR_TYPE_WSTRING_VIEW:
        to->val.wstr.ptr = NULL;
        if (from->val.wstr.len > SIZE_MAX / sizeof(wchar_t) - 1) return XXWIDGETS_INVALID_ARGUMENT;
        data = from->val.wstr.ptr; size = from->val.wstr.len * sizeof(wchar_t); extra = sizeof(wchar_t);
        break;
    case XX_VAR_TYPE_BYTES: case XX_VAR_TYPE_BYTES_VIEW:
        data = from->val.bytes.data; size = from->val.bytes.size; to->val.bytes.data = NULL; break;
    default: return XXWIDGETS_OK;
    }
    if ((!data && size) || size > SIZE_MAX - extra) return XXWIDGETS_INVALID_ARGUMENT;
    copy = malloc((size + extra) ? size + extra : 1);
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    if (size) memcpy(copy, data, size);
    if (extra) memset((unsigned char *)copy + size, 0, extra);
    if (from->type == XX_VAR_TYPE_STRING || from->type == XX_VAR_TYPE_STRING_VIEW) to->val.str.ptr = copy;
    else if (from->type == XX_VAR_TYPE_WSTRING || from->type == XX_VAR_TYPE_WSTRING_VIEW) to->val.wstr.ptr = copy;
    else to->val.bytes.data = copy;
    return XXWIDGETS_OK;
}

static xxwidgets_status sync_combo(xxwidgets_widget *widget)
{
    xxwidgets_status status;
    ++widget->app->syncing; status = widget->app->ops->sync(widget); --widget->app->syncing;
    return status;
}

xxwidgets_status xxwidgets_combobox_set_records(xxwidgets_widget *widget, const xx_meta_string *records, size_t count)
{
    struct xxwidgets_combo_state *state, *previous;
    char **items, **old_items;
    size_t i, old_count;
    int old_value;
    uint64_t revision;
    xxwidgets_status status = XXWIDGETS_OK;
    if (!widget || !xxwidgets_combo_kind(widget) || (!records && count) || count > INT_MAX ||
        count > SIZE_MAX / sizeof(*records) || count > SIZE_MAX / sizeof(*items)) return XXWIDGETS_INVALID_ARGUMENT;
    state = (struct xxwidgets_combo_state *)calloc(1, sizeof(*state));
    if (!state) return XXWIDGETS_OUT_OF_MEMORY;
    items = count ? (char **)calloc(count, sizeof(*items)) : NULL;
    state->records = count ? (xx_meta_string *)calloc(count, sizeof(*records)) : NULL;
    state->checked = count ? (unsigned char *)calloc(count, 1) : NULL;
    if (count && (!items || !state->records || !state->checked)) { status = XXWIDGETS_OUT_OF_MEMORY; goto fail; }
    state->count = count;
    for (i = 0; i < count; ++i) {
        const xx_str_w_s *label = records[i].meta_string;
        xx_str_w_s *copy;
        if (!label || (!label->data && label->length) || label->length > SIZE_MAX / sizeof(wchar_t) - 1)
            { status = XXWIDGETS_INVALID_ARGUMENT; goto fail; }
        items[i] = wide_utf8(label->data, label->length, &status);
        if (!items[i]) goto fail;
        copy = (xx_str_w_s *)calloc(1, sizeof(*copy));
        if (!copy) { status = XXWIDGETS_OUT_OF_MEMORY; goto fail; }
        state->records[i].meta_string = copy;
        copy->data = (wchar_t *)malloc((label->length + 1) * sizeof(wchar_t));
        if (!copy->data) { status = XXWIDGETS_OUT_OF_MEMORY; goto fail; }
        if (label->length) memcpy(copy->data, label->data, label->length * sizeof(wchar_t));
        copy->data[label->length] = L'\0';
        copy->length = label->length; copy->capacity = label->length + 1; copy->is_view = true;
        status = copy_var(&state->records[i].var, &records[i].var);
        if (status != XXWIDGETS_OK) goto fail;
    }
    previous = widget->combo; old_items = widget->items; old_count = widget->item_count;
    old_value = widget->value; revision = widget->combo_revision;
    widget->combo = state; widget->items = items; widget->item_count = count; widget->value = count ? 0 : -1;
    ++widget->combo_revision;
    status = sync_combo(widget);
    if (status != XXWIDGETS_OK) {
        widget->combo = previous; widget->items = old_items; widget->item_count = old_count; widget->value = old_value;
        /* A partially rebuilt native control must rebuild again during rollback. */
        ++widget->combo_revision; sync_combo(widget); widget->combo_revision = revision + 2;
        goto fail;
    }
    dispose_state(previous);
    for (i = 0; i < old_count; ++i) free(old_items[i]);
    free(old_items);
    return XXWIDGETS_OK;
fail:
    if (items) { for (i = 0; i < count; ++i) free(items[i]); free(items); }
    dispose_state(state); return status;
}

size_t xxwidgets_combobox_count(const xxwidgets_widget *widget)
{
    return widget && xxwidgets_combo_kind(widget) ? widget->item_count : 0;
}

xxwidgets_status xxwidgets_combobox_get_record(const xxwidgets_widget *widget, size_t index, const xx_meta_string **record)
{
    if (!widget || !xxwidgets_combo_kind(widget) || !record || index >= widget->item_count) return XXWIDGETS_INVALID_ARGUMENT;
    *record = widget->combo->records + index; return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_combobox_get_current(xxwidgets_widget *widget, xx_var *value)
{
    xxwidgets_status status;
    int index;
    if (!widget || widget->kind != XXWIDGETS_COMBOBOX || !value) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_widget_get_value(widget, &index);
    if (status != XXWIDGETS_OK) return status;
    if (index < 0 || (size_t)index >= widget->item_count) memset(value, 0, sizeof(*value));
    else *value = widget->combo->records[index].var;
    return XXWIDGETS_OK;
}

int xxwidgets_checkcombobox_checked(const xxwidgets_widget *widget, size_t index)
{
    return widget->combo && index < widget->item_count && widget->combo->checked[index];
}

xxwidgets_status xxwidgets_checkcombobox_is_checked(const xxwidgets_widget *widget, size_t index, int *checked)
{
    if (!widget || widget->kind != XXWIDGETS_CHECKCOMBOBOX || !checked || index >= widget->item_count)
        return XXWIDGETS_INVALID_ARGUMENT;
    *checked = xxwidgets_checkcombobox_checked(widget, index); return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_checkcombobox_set_checked(xxwidgets_widget *widget, size_t index, int checked)
{
    int previous;
    xxwidgets_status status;
    if (!widget || widget->kind != XXWIDGETS_CHECKCOMBOBOX || index >= widget->item_count || (checked != 0 && checked != 1))
        return XXWIDGETS_INVALID_ARGUMENT;
    previous = widget->combo->checked[index];
    if (previous == checked) return XXWIDGETS_OK;
    widget->combo->checked[index] = (unsigned char)checked;
    status = sync_combo(widget);
    if (status != XXWIDGETS_OK) { widget->combo->checked[index] = (unsigned char)previous; sync_combo(widget); }
    return status;
}

xxwidgets_status xxwidgets_checkcombobox_user_toggle(xxwidgets_widget *widget, size_t index)
{
    xxwidgets_status status;
    if (index >= widget->item_count) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_checkcombobox_set_checked(widget, index, !xxwidgets_checkcombobox_checked(widget, index));
    if (status == XXWIDGETS_OK) xxwidgets_emit(widget, XXWIDGETS_EVENT_CHANGE, (int)index);
    return status;
}

xxwidgets_status xxwidgets_checkcombobox_get_checked(const xxwidgets_widget *widget,
    const xx_meta_string **records, size_t capacity, size_t *count)
{
    size_t i, total = 0;
    if (!widget || widget->kind != XXWIDGETS_CHECKCOMBOBOX || !count || (!records && capacity))
        return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < widget->item_count; ++i) total += (size_t)xxwidgets_checkcombobox_checked(widget, i);
    *count = total;
    if (!records && !capacity) return XXWIDGETS_OK;
    if (capacity < total) return XXWIDGETS_BUFFER_TOO_SMALL;
    total = 0;
    for (i = 0; i < widget->item_count; ++i)
        if (xxwidgets_checkcombobox_checked(widget, i)) records[total++] = widget->combo->records + i;
    return XXWIDGETS_OK;
}

const char *xxwidgets_combobox_caption(xxwidgets_widget *widget)
{
    size_t i, count = 0, first = 0;
    if (widget->kind == XXWIDGETS_COMBOBOX)
        return widget->value >= 0 && (size_t)widget->value < widget->item_count ? widget->items[widget->value] : widget->text;
    for (i = 0; i < widget->item_count; ++i)
        if (xxwidgets_checkcombobox_checked(widget, i)) { if (!count) first = i; ++count; }
    if (!count) return widget->text[0] ? widget->text : "None selected";
    if (count == 1) return widget->items[first];
    snprintf(widget->combo->summary, sizeof(widget->combo->summary), "%zu selected", count);
    return widget->combo->summary;
}
