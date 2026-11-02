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

#include "xxwidgets_components_internal.h"
#include "xxwidgets/xxwidgets_side_panel.h"

#include <stdlib.h>
#include <string.h>

#define SIDE_PANEL_MAX_ITEMS 64

typedef struct side_item {
    int id;
    xxwidgets_kind kind;
    char *label; /* untranslated */
    xxwidgets_icon_id icon;
    int bottom;
    int shown; /* the application's visibility, combined with the panel's */
    xxwidgets_widget *widget;
} side_item;

struct xxwidgets_side_panel {
    xxwidgets_widget *owner;
    xxwidgets_widget *frame;
    side_item *items;
    size_t count;
    int visible;
    xxwidgets_rect bounds;
    int has_bounds;
};

static side_item *find_item(const xxwidgets_side_panel *panel, int id)
{
    size_t i;
    if (!panel) return NULL;
    for (i = 0; i < panel->count; ++i)
        if (panel->items[i].id == id) return &panel->items[i];
    return NULL;
}

static xxwidgets_status apply_texts(xxwidgets_side_panel *panel)
{
    xxwidgets_app *app = panel->owner->app;
    size_t i;
    for (i = 0; i < panel->count; ++i) {
        const char *text = XXW_TR(app, panel->items[i].label);
        xxwidgets_status status = xxwidgets_widget_set_text(panel->items[i].widget, text);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_tooltip(panel->items[i].widget, text);
        if (status != XXWIDGETS_OK) return status;
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_side_panel_create(xxwidgets_widget *owner, const xxwidgets_side_panel_item *items, size_t count, xxwidgets_side_panel **out)
{
    static const xxwidgets_rect start = {0, 0, 1, 1};
    xxwidgets_side_panel *panel;
    xxwidgets_app *app;
    xxwidgets_status status = XXWIDGETS_OK;
    size_t i, j;
    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || (!items && count) || count > SIDE_PANEL_MAX_ITEMS) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < count; ++i) {
        if ((items[i].kind != XXWIDGETS_CHECKBOX && items[i].kind != XXWIDGETS_BUTTON) || !items[i].label || !xxwidgets_valid_utf8(items[i].label) ||
            (items[i].bottom != 0 && items[i].bottom != 1) || items[i].icon < XXWIDGETS_ICON_NONE || items[i].icon >= XXWIDGETS_ICON_COUNT ||
            (items[i].kind == XXWIDGETS_CHECKBOX && items[i].icon != XXWIDGETS_ICON_NONE))
            return XXWIDGETS_INVALID_ARGUMENT;
        for (j = 0; j < i; ++j)
            if (items[j].id == items[i].id) return XXWIDGETS_INVALID_ARGUMENT;
    }
    app = owner->app;
    if (xxwidgets_component_busy(app)) return XXWIDGETS_BUSY;
    panel = (xxwidgets_side_panel *)calloc(1, sizeof(*panel));
    if (!panel) return XXWIDGETS_OUT_OF_MEMORY;
    panel->owner = owner;
    panel->visible = 1;
    if (count) {
        panel->items = (side_item *)calloc(count, sizeof(*panel->items));
        if (!panel->items) {
            free(panel);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
    }
    /* The frame first, then the items in the order given (the Tab order). */
    if (xxwidgets_app_supports_kind(app, XXWIDGETS_GROUP)) status = xxwidgets_widget_create(app, owner, XXWIDGETS_GROUP, "", start, &panel->frame);
    for (i = 0; status == XXWIDGETS_OK && i < count; ++i) {
        side_item *item = &panel->items[i];
        item->id = items[i].id;
        item->kind = items[i].kind;
        item->icon = items[i].icon;
        item->bottom = items[i].bottom;
        item->shown = 1;
        item->label = xxwidgets_strdup(items[i].label);
        panel->count = i + 1;
        if (!item->label) {
            status = XXWIDGETS_OUT_OF_MEMORY;
            break;
        }
        status = xxwidgets_widget_create(app, owner, item->kind, XXW_TR(app, item->label), start, &item->widget);
        if (status == XXWIDGETS_OK && item->icon != XXWIDGETS_ICON_NONE) status = xxwidgets_button_set_icon_id(item->widget, item->icon, 0);
    }
    if (status == XXWIDGETS_OK) status = apply_texts(panel);
    if (status != XXWIDGETS_OK) {
        xxwidgets_side_panel_destroy(panel);
        return status;
    }
    *out = panel;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_side_panel_destroy(xxwidgets_side_panel *panel)
{
    size_t i;
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    if (xxwidgets_component_busy(panel->owner->app)) return XXWIDGETS_BUSY;
    for (i = panel->count; i-- > 0;) {
        if (panel->items[i].widget) {
            xxwidgets_status status = xxwidgets_widget_destroy(panel->items[i].widget);
            if (status != XXWIDGETS_OK) return status;
            panel->items[i].widget = NULL;
        }
    }
    if (panel->frame) {
        xxwidgets_status status = xxwidgets_widget_destroy(panel->frame);
        if (status != XXWIDGETS_OK) return status;
        panel->frame = NULL;
    }
    for (i = 0; i < panel->count; ++i) free(panel->items[i].label);
    free(panel->items);
    free(panel);
    return XXWIDGETS_OK;
}

xxwidgets_widget *xxwidgets_side_panel_control(const xxwidgets_side_panel *panel, int id)
{
    side_item *item = find_item(panel, id);
    return item ? item->widget : NULL;
}

xxwidgets_widget *xxwidgets_side_panel_frame(const xxwidgets_side_panel *panel)
{
    return panel ? panel->frame : NULL;
}

static int item_shown(const xxwidgets_side_panel *panel, const side_item *item)
{
    return panel->visible && item->shown;
}

xxwidgets_status xxwidgets_side_panel_get_preferred_width(xxwidgets_side_panel *panel, int *width)
{
    xxwidgets_metrics metrics;
    xxwidgets_status status;
    int widest = 0;
    size_t i;
    if (!panel || !width) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_app_get_metrics(panel->owner->app, &metrics);
    if (status != XXWIDGETS_OK) return status;
    for (i = 0; i < panel->count; ++i) {
        int w, h;
        if (!panel->items[i].shown) continue;
        status = xxwidgets_widget_get_preferred_size(panel->items[i].widget, &w, &h);
        if (status != XXWIDGETS_OK) return status;
        widest = xxwidgets_component_max(widest, w);
    }
    *width = widest + 2 * metrics.panel_inset;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_side_panel_get_minimum_height(xxwidgets_side_panel *panel, int *height)
{
    xxwidgets_metrics metrics;
    xxwidgets_status status;
    int top = 0, bottom = 0;
    size_t i;
    if (!panel || !height) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_app_get_metrics(panel->owner->app, &metrics);
    if (status != XXWIDGETS_OK) return status;
    for (i = 0; i < panel->count; ++i)
        if (panel->items[i].shown) {
            if (panel->items[i].bottom) ++bottom;
            else ++top;
        }
    /* Items H high, S apart, and at least S between the two groups. */
    *height = 2 * metrics.panel_inset + (top + bottom) * metrics.control_height +
              (top + bottom > 0 ? (top + bottom - 1) * metrics.spacing : 0);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_side_panel_set_bounds(xxwidgets_side_panel *panel, xxwidgets_rect units)
{
    xxwidgets_metrics metrics;
    xxwidgets_status status;
    int inset, pitch, x, width, y;
    size_t i;
    if (!panel || units.x < 0 || units.y < 0 || units.width < 1 || units.height < 1) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_app_get_metrics(panel->owner->app, &metrics);
    if (status != XXWIDGETS_OK) return status;
    inset = metrics.panel_inset;
    pitch = metrics.control_height + metrics.spacing;
    x = units.x + inset;
    width = units.width - 2 * inset;
    status = xxwidgets_layout_place(panel->frame, units);
    /* Top group downwards from the top. */
    y = units.y + inset;
    for (i = 0; status == XXWIDGETS_OK && i < panel->count; ++i) {
        side_item *item = &panel->items[i];
        if (item->bottom || !item->shown) continue;
        status = xxwidgets_layout_place(item->widget, xxwidgets_layout_rect(x, y, width, metrics.control_height));
        y += pitch;
    }
    /* Bottom group upwards from the bottom, keeping the given order. */
    y = units.y + units.height - inset - metrics.control_height;
    for (i = panel->count; status == XXWIDGETS_OK && i-- > 0;) {
        side_item *item = &panel->items[i];
        if (!item->bottom || !item->shown) continue;
        status = xxwidgets_layout_place(item->widget, xxwidgets_layout_rect(x, y, width, metrics.control_height));
        y -= pitch;
    }
    if (status == XXWIDGETS_OK) {
        panel->bounds = units;
        panel->has_bounds = 1;
    }
    return status;
}

static xxwidgets_status relayout(xxwidgets_side_panel *panel)
{
    return panel->has_bounds ? xxwidgets_side_panel_set_bounds(panel, panel->bounds) : XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_side_panel_set_item_visible(xxwidgets_side_panel *panel, int id, int visible)
{
    side_item *item = find_item(panel, id);
    xxwidgets_status status;
    if (!item || (visible != 0 && visible != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    item->shown = visible;
    status = xxwidgets_widget_set_visible(item->widget, item_shown(panel, item));
    return status == XXWIDGETS_OK ? relayout(panel) : status;
}

xxwidgets_status xxwidgets_side_panel_set_item_enabled(xxwidgets_side_panel *panel, int id, int enabled)
{
    side_item *item = find_item(panel, id);
    if (!item || (enabled != 0 && enabled != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    return xxwidgets_widget_set_enabled(item->widget, enabled);
}

xxwidgets_status xxwidgets_side_panel_set_checked(xxwidgets_side_panel *panel, int id, int checked)
{
    side_item *item = find_item(panel, id);
    if (!item || item->kind != XXWIDGETS_CHECKBOX || (checked != 0 && checked != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    return xxwidgets_widget_set_value(item->widget, checked);
}

xxwidgets_status xxwidgets_side_panel_get_checked(xxwidgets_side_panel *panel, int id, int *checked)
{
    side_item *item = find_item(panel, id);
    if (!item || item->kind != XXWIDGETS_CHECKBOX || !checked) return XXWIDGETS_INVALID_ARGUMENT;
    return xxwidgets_widget_get_value(item->widget, checked);
}

xxwidgets_status xxwidgets_side_panel_set_visible(xxwidgets_side_panel *panel, int visible)
{
    xxwidgets_status status = XXWIDGETS_OK;
    size_t i;
    if (!panel || (visible != 0 && visible != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    panel->visible = visible;
    if (panel->frame) status = xxwidgets_widget_set_visible(panel->frame, visible);
    for (i = 0; status == XXWIDGETS_OK && i < panel->count; ++i)
        status = xxwidgets_widget_set_visible(panel->items[i].widget, item_shown(panel, &panel->items[i]));
    return status;
}

xxwidgets_status xxwidgets_side_panel_retranslate(xxwidgets_side_panel *panel)
{
    xxwidgets_status status;
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    status = apply_texts(panel);
    return status == XXWIDGETS_OK ? relayout(panel) : status;
}

int xxwidgets_side_panel_handle_event(xxwidgets_side_panel *panel, const xxwidgets_event *event, int *id, int *value)
{
    size_t i;
    if (!panel || !event || !event->widget) return 0;
    for (i = 0; i < panel->count; ++i) {
        side_item *item = &panel->items[i];
        if (event->widget != item->widget || !xxwidgets_component_live(item->widget)) continue;
        if (item->kind == XXWIDGETS_BUTTON && event->type == XXWIDGETS_EVENT_CLICK) {
            if (id) *id = item->id;
            if (value) *value = 0;
            return 1;
        }
        if (item->kind == XXWIDGETS_CHECKBOX && event->type == XXWIDGETS_EVENT_CHANGE) {
            if (id) *id = item->id;
            if (value) *value = event->value ? 1 : 0;
            return 1;
        }
    }
    return 0;
}
