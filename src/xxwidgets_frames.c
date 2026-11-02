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

/* Measured placement (frames in layout units, metrics, preferred sizes) and
 * the small presentation additions that go with it: optional kinds, button
 * icons, alignment and bold, batch updates, tree style, fixed check-combo
 * captions. Every backend operation used here is optional; without one the
 * core works in text cells (1 unit = 1 cell) through rect and sync. */

#include "xxwidgets_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_LIMIT 32767
#define ICON_LIMIT 256u

static xxwidgets_status sync_widget(xxwidgets_widget *widget)
{
    xxwidgets_status status;
    ++widget->app->syncing;
    status = widget->app->ops->sync(widget);
    --widget->app->syncing;
    return status;
}

static void cell_metrics(xxwidgets_metrics *metrics)
{
    memset(metrics, 0, sizeof(*metrics));
    metrics->unit_is_cell = 1;
    metrics->cell_width = metrics->cell_height = 1;
    metrics->control_height = metrics->label_height = 1;
    metrics->spacing = metrics->margin = 1;
}

xxwidgets_status xxwidgets_app_get_metrics(const xxwidgets_app *app, xxwidgets_metrics *metrics)
{
    xxwidgets_metrics native;
    xxwidgets_status status;
    if (!app || !metrics) return XXWIDGETS_INVALID_ARGUMENT;
    if (app->ops->metrics && app->ops->place) {
        memset(&native, 0, sizeof(native));
        status = app->ops->metrics((xxwidgets_app *)app, &native);
        if (status != XXWIDGETS_OK) return status;
        /* A backend reporting nonsense is treated as a cell backend rather
         * than dividing by zero in every layout. */
        if (native.cell_width > 0 && native.cell_height > 0 && native.control_height > 0 && native.label_height > 0 &&
            native.spacing >= 0 && native.margin >= 0 && native.small_icon >= 0 && native.frame_inset >= 0 && native.panel_inset >= 0 &&
            native.caption_gap >= 0) {
            native.unit_is_cell = native.unit_is_cell != 0;
            *metrics = native;
            return XXWIDGETS_OK;
        }
    }
    cell_metrics(metrics);
    return XXWIDGETS_OK;
}

int xxwidgets_frames_native(const xxwidgets_app *app)
{
    xxwidgets_metrics metrics;
    return app && app->ops->place && xxwidgets_app_get_metrics(app, &metrics) == XXWIDGETS_OK && !metrics.unit_is_cell;
}

int xxwidgets_app_supports_kind(const xxwidgets_app *app, xxwidgets_kind kind)
{
    if (!app || kind < XXWIDGETS_WINDOW || kind > XXWIDGETS_GROUP) return 0;
    if (kind <= XXWIDGETS_TABLE) return 1;
    return app->ops->supports_kind && app->ops->supports_kind(kind) ? 1 : 0;
}

/* Width in cells of the widest line (one cell per scalar) and the line count. */
static int text_cells(const char *text, int *lines)
{
    int widest = 0, current = 0, count = 1;
    for (; text && *text; ++text) {
        if (*text == '\n') {
            if (current > widest) widest = current;
            current = 0;
            ++count;
        } else if ((*text & 0xc0) != 0x80 && current < FRAME_LIMIT) ++current;
    }
    if (current > widest) widest = current;
    if (lines) *lines = count;
    return widest;
}

static int clamp_units(long long value)
{
    return value < 1 ? 1 : value > FRAME_LIMIT ? FRAME_LIMIT : (int)value;
}

/* Cell sizes the terminal backend draws: "[text]" buttons, edits and values,
 * "[x] text" checkboxes, "[text v]" combo boxes. */
static void cell_measure(const xxwidgets_widget *widget, const char *text, int *width, int *height)
{
    int lines = 1, cells;
    size_t i;
    if (xxwidgets_combo_kind(widget) && !text) {
        cells = text_cells(xxwidgets_combobox_caption((xxwidgets_widget *)widget), NULL);
        for (i = 0; i < widget->item_count; ++i) {
            int item = text_cells(widget->items[i], NULL);
            if (item > cells) cells = item;
        }
    } else cells = text_cells(text ? text : widget->text, &lines);
    *height = 1;
    switch (widget->kind) {
        case XXWIDGETS_LABEL:
            *width = cells;
            *height = lines;
            break;
        case XXWIDGETS_BUTTON:
        case XXWIDGETS_VALUE: *width = cells + 2; break;
        case XXWIDGETS_EDIT: *width = cells + 2 < 3 ? 3 : cells + 2; break;
        case XXWIDGETS_CHECKBOX: *width = cells + 4; break;
        case XXWIDGETS_COMBOBOX:
        case XXWIDGETS_CHECKCOMBOBOX: *width = cells + 4; break;
        case XXWIDGETS_GROUP:
            *width = cells + 4;
            *height = 2;
            break;
        case XXWIDGETS_PROGRESS: *width = 10; break;
        default:
            *width = widget->rect.width;
            *height = widget->rect.height;
            break;
    }
    *width = clamp_units(*width);
    *height = clamp_units(*height);
}

static xxwidgets_status measure(xxwidgets_widget *widget, const char *text, int *width, int *height)
{
    xxwidgets_metrics metrics;
    xxwidgets_status status;
    int w = 0, h = 0;
    if (!widget || !width || !height || widget->kind == XXWIDGETS_WINDOW || (text && !xxwidgets_valid_utf8(text))) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->app->ops->measure) {
        ++widget->app->syncing;
        status = widget->app->ops->measure(widget, text, &w, &h);
        --widget->app->syncing;
        if (status != XXWIDGETS_OK) return status;
        *width = clamp_units(w);
        *height = clamp_units(h);
        return XXWIDGETS_OK;
    }
    status = xxwidgets_app_get_metrics(widget->app, &metrics);
    if (status != XXWIDGETS_OK) return status;
    cell_measure(widget, text, &w, &h);
    if (!metrics.unit_is_cell) {
        /* A backend placing frames without measuring: cells of its font, one
         * control (or caption line) high. */
        int lines = h;
        w = clamp_units((long long)w * metrics.cell_width);
        if (widget->kind == XXWIDGETS_LABEL) h = clamp_units((long long)lines * metrics.label_height);
        else if (h == 1) h = metrics.control_height;
        else h = clamp_units((long long)h * metrics.cell_height);
    }
    *width = w;
    *height = h;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_widget_get_preferred_size(xxwidgets_widget *widget, int *width, int *height)
{
    return measure(widget, NULL, width, height);
}

xxwidgets_status xxwidgets_widget_measure_text(xxwidgets_widget *widget, const char *text, int *width, int *height)
{
    return text ? measure(widget, text, width, height) : XXWIDGETS_INVALID_ARGUMENT;
}

/* Apply widget->frame: place when the backend has it, else the cell rect. */
static xxwidgets_status apply_frame(xxwidgets_widget *widget)
{
    xxwidgets_status status;
    if (!widget->app->ops->place || !widget->has_frame) return sync_widget(widget);
    ++widget->app->syncing;
    status = widget->app->ops->place(widget);
    --widget->app->syncing;
    return status;
}

xxwidgets_status xxwidgets_widget_set_frame(xxwidgets_widget *widget, xxwidgets_rect frame)
{
    xxwidgets_rect previous_frame, previous_rect;
    xxwidgets_status status;
    int previous_has, window;
    if (!widget) return XXWIDGETS_INVALID_ARGUMENT;
    window = widget->kind == XXWIDGETS_WINDOW;
    if (window) frame.x = frame.y = 0;
    if (frame.x < 0 || frame.y < 0 || frame.x > FRAME_LIMIT || frame.y > FRAME_LIMIT || frame.width < 1 || frame.height < 1 ||
        frame.width > FRAME_LIMIT || frame.height > FRAME_LIMIT)
        return XXWIDGETS_INVALID_ARGUMENT;
    previous_frame = widget->frame;
    previous_rect = widget->rect;
    previous_has = widget->has_frame;
    widget->frame = frame;
    widget->has_frame = 1;
    if (!xxwidgets_frames_native(widget->app)) {
        /* Cells: the frame is the rect, so every cell-based path agrees. */
        if (window) {
            widget->rect.width = frame.width;
            widget->rect.height = frame.height;
        } else widget->rect = frame;
    }
    status = apply_frame(widget);
    if (status != XXWIDGETS_OK) {
        widget->frame = previous_frame;
        widget->rect = previous_rect;
        widget->has_frame = previous_has;
        (void)apply_frame(widget);
    }
    return status;
}

int xxwidgets_widget_has_frame(const xxwidgets_widget *widget)
{
    return widget && widget->has_frame ? 1 : 0;
}

/* The cell rect in units (a window: its client area at 0, 0). */
static xxwidgets_status rect_units(const xxwidgets_widget *widget, xxwidgets_rect *frame)
{
    xxwidgets_metrics metrics;
    xxwidgets_status status = xxwidgets_app_get_metrics(widget->app, &metrics);
    xxwidgets_rect rect = widget->rect;
    if (status != XXWIDGETS_OK) return status;
    if (widget->kind == XXWIDGETS_WINDOW) rect.x = rect.y = 0;
    if (!metrics.unit_is_cell) {
        rect.x *= metrics.cell_width;
        rect.width *= metrics.cell_width;
        rect.y *= metrics.cell_height;
        rect.height *= metrics.cell_height;
    }
    *frame = rect;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_widget_get_frame(const xxwidgets_widget *widget, xxwidgets_rect *frame)
{
    if (!widget || !frame) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->has_frame) {
        *frame = widget->frame;
        return XXWIDGETS_OK;
    }
    return rect_units(widget, frame);
}

xxwidgets_status xxwidgets_widget_get_native_frame(const xxwidgets_widget *widget, xxwidgets_rect *frame)
{
    xxwidgets_rect real;
    xxwidgets_status status;
    if (!widget || !frame) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->app->ops->native_frame) {
        memset(&real, 0, sizeof(real));
        status = widget->app->ops->native_frame(widget, &real);
        if (status == XXWIDGETS_OK) *frame = real;
        return status;
    }
    /* Without the platform's own answer: what was asked for. A window's
     * client area follows user resizes through its rect. */
    if (widget->kind != XXWIDGETS_WINDOW && widget->has_frame) {
        *frame = widget->frame;
        return XXWIDGETS_OK;
    }
    return rect_units(widget, frame);
}

xxwidgets_status xxwidgets_window_get_client_size(const xxwidgets_widget *window, int *width, int *height)
{
    xxwidgets_rect frame;
    xxwidgets_status status;
    if (!window || window->kind != XXWIDGETS_WINDOW || !width || !height) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_widget_get_native_frame(window, &frame);
    if (status != XXWIDGETS_OK) return status;
    *width = frame.width;
    *height = frame.height;
    return XXWIDGETS_OK;
}

static int units_to_cells(int units, int cell)
{
    return cell > 1 ? (int)(((long long)units + cell - 1) / cell) : units;
}

xxwidgets_status xxwidgets_window_set_minimum_frame(xxwidgets_widget *window, int width, int height)
{
    xxwidgets_metrics metrics;
    xxwidgets_status status;
    int previous_width, previous_height, columns, rows;
    if (!window || window->kind != XXWIDGETS_WINDOW || width < 0 || height < 0 || width > FRAME_LIMIT || height > FRAME_LIMIT)
        return XXWIDGETS_INVALID_ARGUMENT;
    previous_width = window->min_frame_width;
    previous_height = window->min_frame_height;
    window->min_frame_width = width;
    window->min_frame_height = height;
    if (window->app->ops->minimum_frame) {
        ++window->app->syncing;
        status = window->app->ops->minimum_frame(window);
        --window->app->syncing;
        if (status != XXWIDGETS_OK) {
            window->min_frame_width = previous_width;
            window->min_frame_height = previous_height;
            ++window->app->syncing;
            (void)window->app->ops->minimum_frame(window);
            --window->app->syncing;
        }
        return status;
    }
    /* Cells: the cell minimum, and a window below it grows to it. */
    status = xxwidgets_app_get_metrics(window->app, &metrics);
    if (status != XXWIDGETS_OK) return status;
    columns = units_to_cells(width, metrics.unit_is_cell ? 1 : metrics.cell_width);
    rows = units_to_cells(height, metrics.unit_is_cell ? 1 : metrics.cell_height);
    status = xxwidgets_window_set_minimum_size(window, columns, rows);
    if (status == XXWIDGETS_OK && (window->rect.width < columns || window->rect.height < rows)) {
        xxwidgets_rect grown = window->rect;
        if (grown.width < columns) grown.width = columns;
        if (grown.height < rows) grown.height = rows;
        status = xxwidgets_widget_set_rect(window, grown);
    }
    if (status != XXWIDGETS_OK) {
        window->min_frame_width = previous_width;
        window->min_frame_height = previous_height;
    }
    return status;
}

xxwidgets_status xxwidgets_window_get_minimum_frame(const xxwidgets_widget *window, int *width, int *height)
{
    if (!window || window->kind != XXWIDGETS_WINDOW || !width || !height) return XXWIDGETS_INVALID_ARGUMENT;
    *width = window->min_frame_width;
    *height = window->min_frame_height;
    return XXWIDGETS_OK;
}

static xxwidgets_status apply_icon(xxwidgets_widget *button)
{
    xxwidgets_status status;
    if (!button->app->ops->set_icon) return XXWIDGETS_OK;
    ++button->app->syncing;
    status = button->app->ops->set_icon(button);
    --button->app->syncing;
    return status;
}

xxwidgets_status xxwidgets_button_set_icon(xxwidgets_widget *button, const void *rgba, unsigned int width, unsigned int height, size_t stride,
                                         unsigned int flags)
{
    unsigned char *copy = NULL, *previous;
    unsigned int previous_width, previous_height, previous_flags, row;
    xxwidgets_status status;
    if (!button || button->kind != XXWIDGETS_BUTTON || (flags & ~(unsigned int)XXWIDGETS_ICON_ONLY)) return XXWIDGETS_INVALID_ARGUMENT;
    if (!rgba) {
        if (width || height || stride) return XXWIDGETS_INVALID_ARGUMENT;
        flags = 0;
    } else {
        if (!width || !height || width > ICON_LIMIT || height > ICON_LIMIT || stride < (size_t)width * 4u) return XXWIDGETS_INVALID_ARGUMENT;
        copy = (unsigned char *)malloc((size_t)width * height * 4u);
        if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
        for (row = 0; row < height; ++row)
            memcpy(copy + (size_t)row * width * 4u, (const unsigned char *)rgba + (size_t)row * stride, (size_t)width * 4u);
    }
    previous = button->icon_rgba;
    previous_width = button->icon_width;
    previous_height = button->icon_height;
    previous_flags = button->icon_flags;
    button->icon_rgba = copy;
    button->icon_width = copy ? width : 0;
    button->icon_height = copy ? height : 0;
    button->icon_flags = flags;
    status = apply_icon(button);
    if (status != XXWIDGETS_OK) {
        button->icon_rgba = previous;
        button->icon_width = previous_width;
        button->icon_height = previous_height;
        button->icon_flags = previous_flags;
        (void)apply_icon(button);
        free(copy);
        return status;
    }
    free(previous);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_button_get_icon(const xxwidgets_widget *button, xxwidgets_icon *icon, unsigned int *flags)
{
    if (!button || button->kind != XXWIDGETS_BUTTON || !icon) return XXWIDGETS_INVALID_ARGUMENT;
    icon->rgba = button->icon_rgba;
    icon->width = button->icon_rgba ? button->icon_width : 0;
    icon->height = button->icon_rgba ? button->icon_height : 0;
    icon->stride = button->icon_rgba ? (size_t)button->icon_width * 4u : 0;
    if (flags) *flags = button->icon_flags;
    return XXWIDGETS_OK;
}

static int text_kind(const xxwidgets_widget *widget)
{
    return widget && (widget->kind == XXWIDGETS_LABEL || widget->kind == XXWIDGETS_VALUE);
}

xxwidgets_status xxwidgets_widget_set_alignment(xxwidgets_widget *widget, xxwidgets_alignment alignment)
{
    xxwidgets_alignment previous;
    xxwidgets_status status;
    if (!text_kind(widget) || alignment < XXWIDGETS_ALIGN_LEFT || alignment > XXWIDGETS_ALIGN_RIGHT) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->alignment == alignment) return XXWIDGETS_OK;
    previous = widget->alignment;
    widget->alignment = alignment;
    status = sync_widget(widget);
    if (status != XXWIDGETS_OK) {
        widget->alignment = previous;
        (void)sync_widget(widget);
    }
    return status;
}

xxwidgets_alignment xxwidgets_widget_alignment(const xxwidgets_widget *widget)
{
    return text_kind(widget) ? widget->alignment : XXWIDGETS_ALIGN_LEFT;
}

xxwidgets_status xxwidgets_widget_set_bold(xxwidgets_widget *widget, int bold)
{
    xxwidgets_status status;
    if (!text_kind(widget) || (bold != 0 && bold != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->bold == bold) return XXWIDGETS_OK;
    widget->bold = bold;
    status = sync_widget(widget);
    if (status != XXWIDGETS_OK) {
        widget->bold = !bold;
        (void)sync_widget(widget);
    }
    return status;
}

int xxwidgets_widget_bold(const xxwidgets_widget *widget)
{
    return text_kind(widget) ? widget->bold : 0;
}

xxwidgets_status xxwidgets_window_begin_update(xxwidgets_widget *window)
{
    xxwidgets_status status = XXWIDGETS_OK;
    if (!window || window->kind != XXWIDGETS_WINDOW || window->update_depth == INT_MAX) return XXWIDGETS_INVALID_ARGUMENT;
    if (window->update_depth == 0 && window->app->ops->begin_update) {
        ++window->app->syncing;
        status = window->app->ops->begin_update(window);
        --window->app->syncing;
    }
    if (status == XXWIDGETS_OK) ++window->update_depth;
    return status;
}

xxwidgets_status xxwidgets_window_end_update(xxwidgets_widget *window)
{
    xxwidgets_status status = XXWIDGETS_OK;
    if (!window || window->kind != XXWIDGETS_WINDOW || window->update_depth <= 0) return XXWIDGETS_INVALID_ARGUMENT;
    /* The depth drops even when the repaint fails: the window must not stay
     * frozen because of one failed call. */
    if (--window->update_depth == 0 && window->app->ops->end_update) {
        ++window->app->syncing;
        status = window->app->ops->end_update(window);
        --window->app->syncing;
    }
    return status;
}

void xxwidgets_focus_after_hide(xxwidgets_widget *widget)
{
    xxwidgets_widget *candidate;
    /* Creation order is the tab order: the next focusable sibling, wrapping. */
    for (candidate = widget->next; candidate; candidate = candidate->next)
        if (candidate->parent == widget->parent && xxwidgets_focusable(candidate)) break;
    if (!candidate)
        for (candidate = widget->app->widgets; candidate && candidate != widget; candidate = candidate->next)
            if (candidate->parent == widget->parent && xxwidgets_focusable(candidate)) break;
    if (candidate && candidate != widget) (void)widget->app->ops->focus(candidate);
}

xxwidgets_status xxwidgets_treeview_set_style(xxwidgets_widget *tree, unsigned int style)
{
    unsigned int previous;
    xxwidgets_status status;
    if (!tree || tree->kind != XXWIDGETS_TREEVIEW || (style & ~(unsigned int)XXWIDGETS_TREE_ARROWS)) return XXWIDGETS_INVALID_ARGUMENT;
    if (tree->tree_style == style) return XXWIDGETS_OK;
    previous = tree->tree_style;
    tree->tree_style = style;
    status = sync_widget(tree);
    if (status != XXWIDGETS_OK) {
        tree->tree_style = previous;
        (void)sync_widget(tree);
    }
    return status;
}

unsigned int xxwidgets_treeview_style(const xxwidgets_widget *tree)
{
    return tree && tree->kind == XXWIDGETS_TREEVIEW ? tree->tree_style : 0;
}

xxwidgets_status xxwidgets_checkcombobox_set_fixed_caption(xxwidgets_widget *widget, const char *caption)
{
    char *copy = NULL, *previous;
    xxwidgets_status status;
    if (!widget || widget->kind != XXWIDGETS_CHECKCOMBOBOX || (caption && !xxwidgets_valid_utf8(caption))) return XXWIDGETS_INVALID_ARGUMENT;
    if (caption && *caption) {
        if (widget->fixed_caption && !strcmp(widget->fixed_caption, caption)) return XXWIDGETS_OK;
        copy = xxwidgets_strdup(caption);
        if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    } else if (!widget->fixed_caption) return XXWIDGETS_OK;
    previous = widget->fixed_caption;
    widget->fixed_caption = copy;
    status = sync_widget(widget);
    if (status != XXWIDGETS_OK) {
        widget->fixed_caption = previous;
        (void)sync_widget(widget);
        free(copy);
        return status;
    }
    free(previous);
    return XXWIDGETS_OK;
}

void xxwidgets_frames_dispose(xxwidgets_widget *widget)
{
    free(widget->icon_rgba);
    widget->icon_rgba = NULL;
    free(widget->fixed_caption);
    widget->fixed_caption = NULL;
}
