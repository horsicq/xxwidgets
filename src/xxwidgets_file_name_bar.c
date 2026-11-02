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
#include "xxwidgets/xxwidgets_file_name_bar.h"
#include "xxwidgets/xxwidgets_icons.h"

#include <stdlib.h>
#include <string.h>

struct xxwidgets_file_name_bar {
    xxwidgets_widget *owner;
    xxwidgets_widget *controls[XXWIDGETS_FILE_NAME_BAR_CONTROL_COUNT];
    char *loaded; /* the loaded path (PATH's tooltip), or NULL */
    xxwidgets_rect bounds;
    int has_bounds;
};

static const char *const caption_text = XXW_N_("File name");
static const char *const recent_text = XXW_N_("Recent files");
static const char *const browse_text = XXW_N_("Open file");

static xxwidgets_status apply_texts(xxwidgets_file_name_bar *bar)
{
    xxwidgets_app *app = bar->owner->app;
    xxwidgets_metrics metrics;
    xxwidgets_status status = xxwidgets_app_get_metrics(app, &metrics);
    /* Icon-only buttons; without icons they show Qt 3.11's ">" and "...". */
    int icons = status == XXWIDGETS_OK && metrics.small_icon > 0;
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(bar->controls[XXWIDGETS_FILE_NAME_BAR_CAPTION], XXW_TR(app, caption_text));
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_set_text(bar->controls[XXWIDGETS_FILE_NAME_BAR_RECENT], icons ? XXW_TR(app, recent_text) : ">");
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_tooltip(bar->controls[XXWIDGETS_FILE_NAME_BAR_RECENT], XXW_TR(app, recent_text));
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_set_text(bar->controls[XXWIDGETS_FILE_NAME_BAR_BROWSE], icons ? XXW_TR(app, browse_text) : "...");
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_tooltip(bar->controls[XXWIDGETS_FILE_NAME_BAR_BROWSE], XXW_TR(app, browse_text));
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_set_tooltip(bar->controls[XXWIDGETS_FILE_NAME_BAR_PATH], bar->loaded ? bar->loaded : XXW_TR(app, caption_text));
    return status;
}

xxwidgets_status xxwidgets_file_name_bar_create(xxwidgets_widget *owner, unsigned int flags, xxwidgets_file_name_bar **out)
{
    static const xxwidgets_rect start = {0, 0, 1, 1};
    xxwidgets_file_name_bar *bar;
    xxwidgets_app *app;
    xxwidgets_status status;
    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || flags) return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (xxwidgets_component_busy(app)) return XXWIDGETS_BUSY;
    bar = (xxwidgets_file_name_bar *)calloc(1, sizeof(*bar));
    if (!bar) return XXWIDGETS_OUT_OF_MEMORY;
    bar->owner = owner;
    /* Creation (Tab) order: caption, frame, Recent, path, Browse. */
    status = xxwidgets_widget_create(app, owner, XXWIDGETS_LABEL, XXW_TR(app, caption_text), start, &bar->controls[XXWIDGETS_FILE_NAME_BAR_CAPTION]);
    if (status == XXWIDGETS_OK && xxwidgets_app_supports_kind(app, XXWIDGETS_GROUP))
        status = xxwidgets_widget_create(app, owner, XXWIDGETS_GROUP, "", start, &bar->controls[XXWIDGETS_FILE_NAME_BAR_FRAME]);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, owner, XXWIDGETS_BUTTON, XXW_TR(app, recent_text), start, &bar->controls[XXWIDGETS_FILE_NAME_BAR_RECENT]);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_button_set_icon_id(bar->controls[XXWIDGETS_FILE_NAME_BAR_RECENT], XXWIDGETS_ICON_LIST, XXWIDGETS_ICON_ONLY);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(app, owner, XXWIDGETS_EDIT, "", start, &bar->controls[XXWIDGETS_FILE_NAME_BAR_PATH]);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, owner, XXWIDGETS_BUTTON, XXW_TR(app, browse_text), start, &bar->controls[XXWIDGETS_FILE_NAME_BAR_BROWSE]);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_button_set_icon_id(bar->controls[XXWIDGETS_FILE_NAME_BAR_BROWSE], XXWIDGETS_ICON_OPEN, XXWIDGETS_ICON_ONLY);
    /* The recent list starts empty. */
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(bar->controls[XXWIDGETS_FILE_NAME_BAR_RECENT], 0);
    if (status == XXWIDGETS_OK) status = apply_texts(bar);
    if (status != XXWIDGETS_OK) {
        xxwidgets_file_name_bar_destroy(bar);
        return status;
    }
    *out = bar;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_file_name_bar_destroy(xxwidgets_file_name_bar *bar)
{
    size_t i;
    if (!bar) return XXWIDGETS_INVALID_ARGUMENT;
    if (xxwidgets_component_busy(bar->owner->app)) return XXWIDGETS_BUSY;
    for (i = XXWIDGETS_FILE_NAME_BAR_CONTROL_COUNT; i-- > 0;) {
        xxwidgets_status status;
        if (!bar->controls[i]) continue;
        status = xxwidgets_widget_destroy(bar->controls[i]);
        if (status != XXWIDGETS_OK) return status;
        bar->controls[i] = NULL;
    }
    free(bar->loaded);
    free(bar);
    return XXWIDGETS_OK;
}

xxwidgets_widget *xxwidgets_file_name_bar_control(const xxwidgets_file_name_bar *bar, xxwidgets_file_name_bar_control_id control)
{
    return bar && control >= 0 && control < XXWIDGETS_FILE_NAME_BAR_CONTROL_COUNT ? bar->controls[control] : NULL;
}

/* Widths of the two buttons: Recent is a small square, Browse at least Qt's
 * 80 (a push button's minimum width). */
static xxwidgets_status button_widths(xxwidgets_file_name_bar *bar, const xxwidgets_metrics *metrics, int *recent, int *browse)
{
    int w, h;
    xxwidgets_status status = xxwidgets_widget_get_preferred_size(bar->controls[XXWIDGETS_FILE_NAME_BAR_RECENT], &w, &h);
    if (status != XXWIDGETS_OK) return status;
    *recent = metrics->unit_is_cell ? w : xxwidgets_component_max(xxwidgets_component_icon_button(metrics, w), xxwidgets_component_qt(metrics, 24));
    status = xxwidgets_widget_get_preferred_size(bar->controls[XXWIDGETS_FILE_NAME_BAR_BROWSE], &w, &h);
    if (status != XXWIDGETS_OK) return status;
    *browse = metrics->unit_is_cell ? w : xxwidgets_component_max(xxwidgets_component_icon_button(metrics, w), xxwidgets_component_qt(metrics, 80));
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_file_name_bar_set_bounds(xxwidgets_file_name_bar *bar, xxwidgets_rect units)
{
    xxwidgets_metrics metrics;
    xxwidgets_layout_item items[3];
    xxwidgets_status status;
    int recent = 0, browse = 0, top, row, inset;
    if (!bar || units.x < 0 || units.y < 0 || units.width < 1 || units.height < 1) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_app_get_metrics(bar->owner->app, &metrics);
    if (status == XXWIDGETS_OK) status = button_widths(bar, &metrics, &recent, &browse);
    if (status != XXWIDGETS_OK) return status;
    inset = metrics.panel_inset;
    top = metrics.label_height + metrics.caption_gap;
    row = units.y + top + inset;
    memset(items, 0, sizeof(items));
    items[0].minimum = items[0].preferred = recent;
    items[1].minimum = 1;
    items[1].preferred = 1;
    items[1].stretch = 1;
    items[2].minimum = items[2].preferred = browse;
    xxwidgets_layout_line(items, 3, units.x + inset, units.width - 2 * inset, metrics.spacing);
    status = xxwidgets_layout_place(bar->controls[XXWIDGETS_FILE_NAME_BAR_CAPTION],
                                    xxwidgets_layout_rect(units.x, units.y, units.width, metrics.label_height));
    if (status == XXWIDGETS_OK)
        status = xxwidgets_layout_place(bar->controls[XXWIDGETS_FILE_NAME_BAR_FRAME],
                                        xxwidgets_layout_rect(units.x, units.y + top, units.width, units.height - top));
    if (status == XXWIDGETS_OK)
        status = xxwidgets_layout_place(bar->controls[XXWIDGETS_FILE_NAME_BAR_RECENT],
                                        xxwidgets_layout_rect(items[0].position, row, items[0].size, metrics.control_height));
    if (status == XXWIDGETS_OK)
        status = xxwidgets_layout_place(bar->controls[XXWIDGETS_FILE_NAME_BAR_PATH],
                                        xxwidgets_layout_rect(items[1].position, row, items[1].size, metrics.control_height));
    if (status == XXWIDGETS_OK)
        status = xxwidgets_layout_place(bar->controls[XXWIDGETS_FILE_NAME_BAR_BROWSE],
                                        xxwidgets_layout_rect(items[2].position, row, items[2].size, metrics.control_height));
    if (status == XXWIDGETS_OK) {
        bar->bounds = units;
        bar->has_bounds = 1;
    }
    return status;
}

xxwidgets_status xxwidgets_file_name_bar_get_preferred_height(xxwidgets_file_name_bar *bar, int *height)
{
    xxwidgets_metrics metrics;
    xxwidgets_status status;
    if (!bar || !height) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_app_get_metrics(bar->owner->app, &metrics);
    if (status == XXWIDGETS_OK)
        *height = metrics.label_height + metrics.caption_gap + 2 * metrics.panel_inset + metrics.control_height;
    return status;
}

xxwidgets_status xxwidgets_file_name_bar_get_minimum_width(xxwidgets_file_name_bar *bar, int *width)
{
    xxwidgets_metrics metrics;
    xxwidgets_status status;
    int recent = 0, browse = 0, caption = 0, h;
    if (!bar || !width) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_app_get_metrics(bar->owner->app, &metrics);
    if (status == XXWIDGETS_OK) status = button_widths(bar, &metrics, &recent, &browse);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_preferred_size(bar->controls[XXWIDGETS_FILE_NAME_BAR_CAPTION], &caption, &h);
    if (status != XXWIDGETS_OK) return status;
    /* A short path: about Qt's 80 units. */
    *width = xxwidgets_component_max(caption, 2 * metrics.panel_inset + recent + browse + 2 * metrics.spacing + xxwidgets_component_qt(&metrics, 80));
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_file_name_bar_set_path(xxwidgets_file_name_bar *bar, const char *path)
{
    if (!bar) return XXWIDGETS_INVALID_ARGUMENT;
    return xxwidgets_widget_set_text(bar->controls[XXWIDGETS_FILE_NAME_BAR_PATH], path ? path : "");
}

xxwidgets_status xxwidgets_file_name_bar_get_path(xxwidgets_file_name_bar *bar, char *buffer, size_t capacity, size_t *required)
{
    if (!bar) return XXWIDGETS_INVALID_ARGUMENT;
    return xxwidgets_widget_get_text(bar->controls[XXWIDGETS_FILE_NAME_BAR_PATH], buffer, capacity, required);
}

xxwidgets_status xxwidgets_file_name_bar_set_loaded_path(xxwidgets_file_name_bar *bar, const char *path)
{
    char *copy = NULL, *previous;
    xxwidgets_status status;
    if (!bar || (path && !xxwidgets_valid_utf8(path))) return XXWIDGETS_INVALID_ARGUMENT;
    if (path && *path) {
        copy = xxwidgets_strdup(path);
        if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    }
    status = xxwidgets_widget_set_text(bar->controls[XXWIDGETS_FILE_NAME_BAR_PATH], copy ? copy : "");
    if (status != XXWIDGETS_OK) {
        free(copy);
        return status;
    }
    previous = bar->loaded;
    bar->loaded = copy;
    free(previous);
    return xxwidgets_widget_set_tooltip(bar->controls[XXWIDGETS_FILE_NAME_BAR_PATH], copy ? copy : XXW_TR(bar->owner->app, caption_text));
}

xxwidgets_status xxwidgets_file_name_bar_set_recent_available(xxwidgets_file_name_bar *bar, int available)
{
    if (!bar || (available != 0 && available != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    return xxwidgets_widget_set_enabled(bar->controls[XXWIDGETS_FILE_NAME_BAR_RECENT], available);
}

xxwidgets_status xxwidgets_file_name_bar_set_visible(xxwidgets_file_name_bar *bar, int visible)
{
    size_t i;
    if (!bar || (visible != 0 && visible != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < XXWIDGETS_FILE_NAME_BAR_CONTROL_COUNT; ++i) {
        xxwidgets_status status;
        if (!bar->controls[i]) continue;
        status = xxwidgets_widget_set_visible(bar->controls[i], visible);
        if (status != XXWIDGETS_OK) return status;
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_file_name_bar_retranslate(xxwidgets_file_name_bar *bar)
{
    xxwidgets_status status;
    if (!bar) return XXWIDGETS_INVALID_ARGUMENT;
    status = apply_texts(bar);
    if (status == XXWIDGETS_OK && bar->has_bounds) status = xxwidgets_file_name_bar_set_bounds(bar, bar->bounds);
    return status;
}

int xxwidgets_file_name_bar_handle_event(xxwidgets_file_name_bar *bar, const xxwidgets_event *event, xxwidgets_file_name_bar_action *action)
{
    xxwidgets_file_name_bar_action result = XXWIDGETS_FILE_NAME_BAR_ACTION_NONE;
    xxwidgets_widget *widget;
    if (!bar || !event || !event->widget) return 0;
    widget = event->widget;
    if (widget == bar->controls[XXWIDGETS_FILE_NAME_BAR_RECENT]) {
        if (event->type == XXWIDGETS_EVENT_CLICK && xxwidgets_component_live(widget)) result = XXWIDGETS_FILE_NAME_BAR_ACTION_RECENT;
    } else if (widget == bar->controls[XXWIDGETS_FILE_NAME_BAR_BROWSE]) {
        if (event->type == XXWIDGETS_EVENT_CLICK && xxwidgets_component_live(widget)) result = XXWIDGETS_FILE_NAME_BAR_ACTION_BROWSE;
    } else if (widget == bar->controls[XXWIDGETS_FILE_NAME_BAR_PATH]) {
        if (xxwidgets_component_live(widget)) {
            if (event->type == XXWIDGETS_EVENT_ACTIVATE) result = XXWIDGETS_FILE_NAME_BAR_ACTION_OPEN;
            else if (event->type == XXWIDGETS_EVENT_CHANGE) result = XXWIDGETS_FILE_NAME_BAR_ACTION_EDITED;
        }
    } else if (widget != bar->controls[XXWIDGETS_FILE_NAME_BAR_CAPTION] && widget != bar->controls[XXWIDGETS_FILE_NAME_BAR_FRAME])
        return 0;
    if (action) *action = result;
    return 1;
}
