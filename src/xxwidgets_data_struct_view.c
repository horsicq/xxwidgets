/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_data_struct_view.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct xxwidgets_data_struct_view {
    xxwidgets_widget *owner;
    xxwidgets_widget *controls[XXWIDGETS_DATA_STRUCT_VIEW_CONTROL_COUNT];
    xxwidgets_data_struct_item *items;
    size_t count;
    size_t selection;
};

static int valid_bounds(xxwidgets_rect bounds)
{
    return bounds.x >= 0 && bounds.y >= 0 && bounds.width >= 48 && bounds.height >= 4 && bounds.x <= INT_MAX - bounds.width &&
           bounds.y <= INT_MAX - bounds.height;
}

static void control_rects(xxwidgets_rect bounds, xxwidgets_rect rects[XXWIDGETS_DATA_STRUCT_VIEW_CONTROL_COUNT])
{
    int left = bounds.width / 3;
    if (left < 18) left = 18;
    if (left > bounds.width - 29) left = bounds.width - 29;
    rects[XXWIDGETS_DATA_STRUCT_VIEW_LIST] = (xxwidgets_rect){bounds.x, bounds.y, left, bounds.height};
    rects[XXWIDGETS_DATA_STRUCT_VIEW_TABLE] = (xxwidgets_rect){bounds.x + left + 1, bounds.y, bounds.width - left - 1, bounds.height};
    rects[XXWIDGETS_DATA_STRUCT_VIEW_DETAILS] = rects[XXWIDGETS_DATA_STRUCT_VIEW_TABLE];
}

static const char *safe_string(const char *text)
{
    return text ? text : "";
}

static void free_field(xxwidgets_data_struct_field *field)
{
    free((void *)field->name);
    free((void *)field->type);
    free((void *)field->raw);
    free((void *)field->value);
    free((void *)field->comment);
}

static void free_items(xxwidgets_data_struct_item *items, size_t count)
{
    size_t item, field;
    if (items)
        for (item = 0; item < count; ++item) {
            free((void *)items[item].name);
            if (items[item].fields)
                for (field = 0; field < items[item].field_count; ++field) free_field((xxwidgets_data_struct_field *)items[item].fields + field);
            free((void *)items[item].fields);
        }
    free(items);
}

static xxwidgets_status copy_items(const xxwidgets_data_struct_item *items, size_t count, xxwidgets_data_struct_item **out)
{
    xxwidgets_data_struct_item *copies;
    size_t item, field;
    if ((!items && count) || count > INT_MAX || count > SIZE_MAX / sizeof(*copies)) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!count) return XXWIDGETS_OK;
    copies = (xxwidgets_data_struct_item *)calloc(count, sizeof(*copies));
    if (!copies) return XXWIDGETS_OUT_OF_MEMORY;
    for (item = 0; item < count; ++item) {
        xxwidgets_data_struct_field *fields = NULL;
        if (!xxwidgets_valid_utf8(safe_string(items[item].name)) || (!items[item].fields && items[item].field_count) ||
            items[item].field_count > INT_MAX || items[item].field_count > SIZE_MAX / sizeof(*fields) ||
            items[item].data_struct.type < XX_DATA_STRUCT_TYPE_UNKNOWN || items[item].data_struct.type > XX_DATA_STRUCT_TYPE_RAW_DATA) {
            free_items(copies, count);
            return XXWIDGETS_INVALID_ARGUMENT;
        }
        copies[item].name = xxwidgets_strdup(safe_string(items[item].name));
        if (!copies[item].name) {
            free_items(copies, count);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        copies[item].data_struct = items[item].data_struct;
        copies[item].field_count = items[item].field_count;
        if (items[item].field_count) {
            fields = (xxwidgets_data_struct_field *)calloc(items[item].field_count, sizeof(*fields));
            if (!fields) {
                free_items(copies, count);
                return XXWIDGETS_OUT_OF_MEMORY;
            }
            copies[item].fields = fields;
        }
        for (field = 0; field < items[item].field_count; ++field) {
            const xxwidgets_data_struct_field *source = items[item].fields + field;
            if (!xxwidgets_valid_utf8(safe_string(source->name)) || !xxwidgets_valid_utf8(safe_string(source->type)) ||
                !xxwidgets_valid_utf8(safe_string(source->raw)) || !xxwidgets_valid_utf8(safe_string(source->value)) ||
                !xxwidgets_valid_utf8(safe_string(source->comment)) ||
                source->relative_offset < -1 || source->file_offset < -1 || source->size < -1) {
                free_items(copies, count);
                return XXWIDGETS_INVALID_ARGUMENT;
            }
            fields[field].relative_offset = source->relative_offset;
            fields[field].file_offset = source->file_offset;
            fields[field].size = source->size;
            fields[field].property = source->property;
            fields[field].name = xxwidgets_strdup(safe_string(source->name));
            fields[field].type = xxwidgets_strdup(safe_string(source->type));
            fields[field].raw = xxwidgets_strdup(safe_string(source->raw));
            fields[field].value = xxwidgets_strdup(safe_string(source->value));
            fields[field].comment = xxwidgets_strdup(safe_string(source->comment));
            if (!fields[field].name || !fields[field].type || !fields[field].raw || !fields[field].value || !fields[field].comment) {
                free_items(copies, count);
                return XXWIDGETS_OUT_OF_MEMORY;
            }
        }
    }
    *out = copies;
    return XXWIDGETS_OK;
}

static const char *data_struct_type_name(xx_data_struct_type_t type)
{
    switch (type) {
        case XX_DATA_STRUCT_TYPE_STRUCT: return "struct";
        case XX_DATA_STRUCT_TYPE_ENTRY: return "entry";
        case XX_DATA_STRUCT_TYPE_FOOTER: return "footer";
        case XX_DATA_STRUCT_TYPE_LOCATOR: return "locator";
        case XX_DATA_STRUCT_TYPE_RAW_DATA: return "raw_data";
        default: return "unknown";
    }
}

static xxwidgets_status sync_control(xxwidgets_widget *widget)
{
    xxwidgets_status status;
    ++widget->app->syncing;
    status = widget->app->ops->sync(widget);
    --widget->app->syncing;
    return status;
}

/* Replace a LISTBOX's already-owned rows without exposing a partially rebuilt
 * model. The intermediate empty sync makes every backend rebuild even when the
 * old and new row counts match. Neither old nor new rows are freed here. */
static xxwidgets_status replace_list_rows(xxwidgets_widget *widget, char **rows, size_t count, int value, char ***old_rows, size_t *old_count, int *old_value)
{
    char **previous_rows;
    size_t previous_count;
    int previous_value;
    xxwidgets_status status;
    if (!widget || widget->kind != XXWIDGETS_LISTBOX || count > INT_MAX || value < -1 || (value >= 0 && (size_t)value >= count))
        return XXWIDGETS_INVALID_ARGUMENT;
    previous_rows = widget->items;
    previous_count = widget->item_count;
    previous_value = widget->value;
    widget->items = NULL;
    widget->item_count = 0;
    widget->value = -1;
    status = sync_control(widget);
    if (status == XXWIDGETS_OK) {
        widget->items = rows;
        widget->item_count = count;
        widget->value = value;
        status = sync_control(widget);
    }
    if (status != XXWIDGETS_OK) {
        widget->items = NULL;
        widget->item_count = 0;
        widget->value = -1;
        (void)sync_control(widget);
        widget->items = previous_rows;
        widget->item_count = previous_count;
        widget->value = previous_value;
        (void)sync_control(widget);
        return status;
    }
    if (old_rows) *old_rows = previous_rows;
    if (old_count) *old_count = previous_count;
    if (old_value) *old_value = previous_value;
    return XXWIDGETS_OK;
}

static void free_string_rows(char **rows, size_t count)
{
    size_t i;
    if (rows)
        for (i = 0; i < count; ++i) free(rows[i]);
    free(rows);
}

static xxwidgets_status set_detail_rows(xxwidgets_widget *details, const xxwidgets_data_struct_item *item)
{
    char text[7][96], **rows = NULL, **old_rows = NULL;
    const xx_data_struct *data = &item->data_struct;
    size_t i, old_count = 0;
    xxwidgets_status status;
    snprintf(text[0], sizeof(text[0]), "Type: %s", data_struct_type_name(data->type));
    snprintf(text[1], sizeof(text[1]), "ID: 0x%08" PRIX32, data->id);
    if (data->offset < 0) snprintf(text[2], sizeof(text[2]), "File offset: -");
    else snprintf(text[2], sizeof(text[2]), "File offset: 0x%" PRIX64, (uint64_t)data->offset);
    if (data->address < 0) snprintf(text[3], sizeof(text[3]), "Address: -");
    else snprintf(text[3], sizeof(text[3]), "Address: 0x%" PRIX64, (uint64_t)data->address);
    if (data->entry_size < 0) snprintf(text[4], sizeof(text[4]), "Entry size: -");
    else snprintf(text[4], sizeof(text[4]), "Entry size: 0x%" PRIX64, (uint64_t)data->entry_size);
    if (data->total_size < 0) snprintf(text[5], sizeof(text[5]), "Total size: -");
    else snprintf(text[5], sizeof(text[5]), "Total size: 0x%" PRIX64, (uint64_t)data->total_size);
    snprintf(text[6], sizeof(text[6]), "Count: %" PRIu64, data->count);
    rows = (char **)calloc(7, sizeof(*rows));
    if (!rows) return XXWIDGETS_OUT_OF_MEMORY;
    for (i = 0; i < 7; ++i) {
        rows[i] = xxwidgets_strdup(text[i]);
        if (!rows[i]) {
            free_string_rows(rows, 7);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
    }
    status = replace_list_rows(details, rows, 7, -1, &old_rows, &old_count, NULL);
    if (status != XXWIDGETS_OK) free_string_rows(rows, 7);
    else free_string_rows(old_rows, old_count);
    return status;
}

static xxwidgets_status show_item(xxwidgets_data_struct_view *view, size_t index)
{
    xxwidgets_widget *table = view->controls[XXWIDGETS_DATA_STRUCT_VIEW_TABLE];
    xxwidgets_widget *details = view->controls[XXWIDGETS_DATA_STRUCT_VIEW_DETAILS];
    xxwidgets_status status;
    if (index >= view->count) {
        status = xxwidgets_data_struct_table_clear(table);
        if (status == XXWIDGETS_OK) status = xxwidgets_listbox_clear(details);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_visible(table, 1);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_visible(details, 0);
        if (status == XXWIDGETS_OK) view->selection = SIZE_MAX;
        return status;
    }
    if (view->items[index].data_struct.type == XX_DATA_STRUCT_TYPE_STRUCT) {
        status = xxwidgets_data_struct_table_set_fields(table, view->items[index].fields, view->items[index].field_count);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_visible(details, 0);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_visible(table, 1);
    } else {
        status = set_detail_rows(details, view->items + index);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_visible(table, 0);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_visible(details, 1);
    }
    if (status == XXWIDGETS_OK) view->selection = index;
    return status;
}

xxwidgets_widget *xxwidgets_data_struct_view_control(const xxwidgets_data_struct_view *view, xxwidgets_data_struct_view_control_id control)
{
    return view && control >= 0 && control < XXWIDGETS_DATA_STRUCT_VIEW_CONTROL_COUNT ? view->controls[control] : NULL;
}

xxwidgets_status xxwidgets_data_struct_view_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_data_struct_view **out_view)
{
    static const xxwidgets_kind kinds[XXWIDGETS_DATA_STRUCT_VIEW_CONTROL_COUNT] = {XXWIDGETS_LISTBOX, XXWIDGETS_DATA_STRUCT_TABLE, XXWIDGETS_LISTBOX};
    static const char *const names[XXWIDGETS_DATA_STRUCT_VIEW_CONTROL_COUNT] = {"Data structures", "Structure fields", "Data structure details"};
    xxwidgets_data_struct_view *view;
    xxwidgets_rect rects[XXWIDGETS_DATA_STRUCT_VIEW_CONTROL_COUNT];
    xxwidgets_status status;
    size_t i;
    if (!out_view) return XXWIDGETS_INVALID_ARGUMENT;
    *out_view = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !valid_bounds(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing) return XXWIDGETS_BUSY;
    view = (xxwidgets_data_struct_view *)calloc(1, sizeof(*view));
    if (!view) return XXWIDGETS_OUT_OF_MEMORY;
    view->owner = owner;
    view->selection = SIZE_MAX;
    control_rects(bounds, rects);
    for (i = 0; i < XXWIDGETS_DATA_STRUCT_VIEW_CONTROL_COUNT; ++i) {
        status = xxwidgets_widget_create(owner->app, owner, kinds[i], names[i], rects[i], &view->controls[i]);
        if (status != XXWIDGETS_OK) goto failed;
    }
    status = xxwidgets_widget_set_visible(view->controls[XXWIDGETS_DATA_STRUCT_VIEW_DETAILS], 0);
    if (status != XXWIDGETS_OK) goto failed;
    *out_view = view;
    return XXWIDGETS_OK;
failed:
    xxwidgets_data_struct_view_destroy(view);
    return status;
}

xxwidgets_status xxwidgets_data_struct_view_destroy(xxwidgets_data_struct_view *view)
{
    size_t i;
    xxwidgets_status status;
    if (!view) return XXWIDGETS_INVALID_ARGUMENT;
    if (view->owner->app->dispatch_depth || view->owner->app->polling || view->owner->app->syncing) return XXWIDGETS_BUSY;
    for (i = 0; i < XXWIDGETS_DATA_STRUCT_VIEW_CONTROL_COUNT; ++i) {
        if (!view->controls[i]) continue;
        status = xxwidgets_widget_destroy(view->controls[i]);
        if (status != XXWIDGETS_OK) return status;
        view->controls[i] = NULL;
    }
    free_items(view->items, view->count);
    free(view);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_data_struct_view_set_rect(xxwidgets_data_struct_view *view, xxwidgets_rect bounds)
{
    xxwidgets_rect rects[XXWIDGETS_DATA_STRUCT_VIEW_CONTROL_COUNT], previous[XXWIDGETS_DATA_STRUCT_VIEW_CONTROL_COUNT];
    xxwidgets_status status;
    size_t i, j;
    if (!view || !valid_bounds(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    control_rects(bounds, rects);
    for (i = 0; i < XXWIDGETS_DATA_STRUCT_VIEW_CONTROL_COUNT; ++i) previous[i] = view->controls[i]->rect;
    for (i = 0; i < XXWIDGETS_DATA_STRUCT_VIEW_CONTROL_COUNT; ++i) {
        status = xxwidgets_widget_set_rect(view->controls[i], rects[i]);
        if (status != XXWIDGETS_OK) {
            for (j = 0; j < i; ++j) (void)xxwidgets_widget_set_rect(view->controls[j], previous[j]);
            return status;
        }
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_data_struct_view_set_items(xxwidgets_data_struct_view *view, const xxwidgets_data_struct_item *items, size_t count)
{
    xxwidgets_data_struct_item *copies = NULL, *previous;
    char **labels = NULL, **previous_labels = NULL, **replaced_labels = NULL;
    size_t previous_count, previous_label_count = 0, replaced_count = 0, i;
    int previous_list_value = -1;
    size_t previous_selection;
    xxwidgets_status status;
    if (!view) return XXWIDGETS_INVALID_ARGUMENT;
    status = copy_items(items, count, &copies);
    if (status != XXWIDGETS_OK) return status;
    if (count) {
        labels = (char **)calloc(count, sizeof(*labels));
        if (!labels) {
            free_items(copies, count);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        for (i = 0; i < count; ++i) {
            labels[i] = xxwidgets_strdup(copies[i].name);
            if (!labels[i]) {
                free_string_rows(labels, count);
                free_items(copies, count);
                return XXWIDGETS_OUT_OF_MEMORY;
            }
        }
    }
    status = replace_list_rows(view->controls[XXWIDGETS_DATA_STRUCT_VIEW_LIST], labels, count, count ? 0 : -1, &previous_labels,
                               &previous_label_count, &previous_list_value);
    if (status != XXWIDGETS_OK) {
        free_string_rows(labels, count);
        free_items(copies, count);
        return status;
    }
    previous = view->items;
    previous_count = view->count;
    previous_selection = view->selection;
    view->items = copies;
    view->count = count;
    status = show_item(view, count ? 0 : SIZE_MAX);
    if (status != XXWIDGETS_OK) {
        xxwidgets_status rollback;
        xxwidgets_widget *list = view->controls[XXWIDGETS_DATA_STRUCT_VIEW_LIST];
        view->items = previous;
        view->count = previous_count;
        view->selection = previous_selection;
        rollback = replace_list_rows(list, previous_labels, previous_label_count, previous_list_value, &replaced_labels, &replaced_count, NULL);
        if (rollback != XXWIDGETS_OK) {
            /* Keep the public model coherent even when backend recovery also
             * fails. A later ordinary sync can repaint this logical state. */
            list->items = previous_labels;
            list->item_count = previous_label_count;
            list->value = previous_list_value;
            (void)sync_control(list);
            replaced_labels = labels;
            replaced_count = count;
        }
        (void)show_item(view, previous_selection);
        free_string_rows(replaced_labels, replaced_count);
        free_items(copies, count);
        return status;
    }
    free_string_rows(previous_labels, previous_label_count);
    free_items(previous, previous_count);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_data_struct_view_clear(xxwidgets_data_struct_view *view)
{
    return xxwidgets_data_struct_view_set_items(view, NULL, 0);
}

size_t xxwidgets_data_struct_view_count(const xxwidgets_data_struct_view *view)
{
    return view ? view->count : 0;
}

xxwidgets_status xxwidgets_data_struct_view_select(xxwidgets_data_struct_view *view, size_t index)
{
    size_t previous;
    xxwidgets_status status;
    if (!view || index >= view->count) return XXWIDGETS_INVALID_ARGUMENT;
    previous = view->selection;
    status = xxwidgets_widget_set_value(view->controls[XXWIDGETS_DATA_STRUCT_VIEW_LIST], (int)index);
    if (status == XXWIDGETS_OK) status = show_item(view, index);
    if (status != XXWIDGETS_OK && previous < view->count) {
        (void)xxwidgets_widget_set_value(view->controls[XXWIDGETS_DATA_STRUCT_VIEW_LIST], (int)previous);
        (void)show_item(view, previous);
    }
    return status;
}

xxwidgets_status xxwidgets_data_struct_view_handle_event(xxwidgets_data_struct_view *view, const xxwidgets_event *event)
{
    size_t previous;
    xxwidgets_status status;
    if (!view || !event) return XXWIDGETS_INVALID_ARGUMENT;
    if (event->widget != view->controls[XXWIDGETS_DATA_STRUCT_VIEW_LIST] || event->type != XXWIDGETS_EVENT_SELECT) return XXWIDGETS_OK;
    if (event->value < 0 || (size_t)event->value >= view->count) return XXWIDGETS_INVALID_ARGUMENT;
    previous = view->selection;
    status = show_item(view, (size_t)event->value);
    if (status != XXWIDGETS_OK && previous < view->count) {
        (void)xxwidgets_widget_set_value(view->controls[XXWIDGETS_DATA_STRUCT_VIEW_LIST], (int)previous);
        (void)show_item(view, previous);
    }
    return status;
}

xxwidgets_status xxwidgets_data_struct_view_get_selection(const xxwidgets_data_struct_view *view, size_t *index, xxwidgets_data_struct_item *item)
{
    if (!view || !index || !item) return XXWIDGETS_INVALID_ARGUMENT;
    if (view->selection >= view->count) {
        *index = SIZE_MAX;
        memset(item, 0, sizeof(*item));
    } else {
        *index = view->selection;
        *item = view->items[view->selection];
    }
    return XXWIDGETS_OK;
}
