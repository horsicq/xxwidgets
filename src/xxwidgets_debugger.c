/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_debugger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct xxwidgets_debugger {
    xxwidgets_widget *owner;
    xxwidgets_widget *controls[XXWIDGETS_DEBUGGER_CONTROL_COUNT];
    xxwidgets_widget *defaults[4];
    xxwidgets_rect bounds, external_rect[4];
    int external_visible[4], panels[4], visible;
    unsigned left, top;
};

static int pane_id(xxwidgets_debugger_control_id pane)
{ return pane >= XXWIDGETS_DEBUGGER_CONTROL_DISASM && pane <= XXWIDGETS_DEBUGGER_CONTROL_STACK; }

static int valid_bounds(xxwidgets_rect b)
{
    return b.x >= 0 && b.y >= 0 && b.width >= 65 && b.height >= 13 &&
        b.width <= 32767 && b.height <= 32767 && b.x <= 32767 - b.width && b.y <= 32767 - b.height;
}

static int clamp(int value, int low, int high)
{ return value < low ? low : value > high ? high : value; }

static void restore_widget(xxwidgets_widget *widget, xxwidgets_rect rect, int visible)
{
    widget->rect = rect;
    widget->visible = visible;
    ++widget->app->syncing;
    (void)widget->app->ops->sync(widget);
    --widget->app->syncing;
}

static void layout_rects(xxwidgets_rect b, unsigned left, unsigned top, const int panes[4],
    int visible, xxwidgets_rect rects[XXWIDGETS_DEBUGGER_CONTROL_COUNT], int shown[XXWIDGETS_DEBUGGER_CONTROL_COUNT])
{
    int upper = panes[0] || panes[1], lower = panes[2] || panes[3];
    int split_x = clamp((int)((unsigned)(b.width - 1) * left / 100), 36, b.width - 29);
    int split_y = upper && lower ? clamp((int)((unsigned)(b.height - 1) * top / 100), 6, b.height - 7) : b.height;
    int i;
    for (i = 0; i < XXWIDGETS_DEBUGGER_CONTROL_COUNT; ++i) {
        rects[i] = (xxwidgets_rect){b.x, b.y, 1, 1};
        shown[i] = 0;
    }
    for (i = 0; i < 4; ++i) {
        int row = i / 2, paired = panes[row * 2] && panes[row * 2 + 1];
        int y = row && upper ? b.y + split_y + 1 : b.y;
        int height = upper && lower ? (row ? b.height - split_y - 1 : split_y) : b.height;
        int x = paired && i % 2 ? b.x + split_x + 1 : b.x;
        int width = paired ? (i % 2 ? b.width - split_x - 1 : split_x) : b.width;
        if (!panes[i]) continue;
        rects[i + 4] = (xxwidgets_rect){x, y, width, 1};
        rects[i] = (xxwidgets_rect){x, y + 1, width, height - 1};
        shown[i] = shown[i + 4] = visible;
    }
    if ((panes[0] && panes[1]) || (panes[2] && panes[3])) {
        int y = b.y, height = b.height;
        if (upper && lower && !(panes[0] && panes[1])) { y += split_y + 1; height -= split_y + 1; }
        else if (upper && lower && !(panes[2] && panes[3])) height = split_y;
        rects[XXWIDGETS_DEBUGGER_SPLIT_VERTICAL] = (xxwidgets_rect){b.x + split_x, y, 1, height};
        shown[XXWIDGETS_DEBUGGER_SPLIT_VERTICAL] = visible;
    }
    if (upper && lower) {
        rects[XXWIDGETS_DEBUGGER_SPLIT_HORIZONTAL] = (xxwidgets_rect){b.x, b.y + split_y, b.width, 1};
        shown[XXWIDGETS_DEBUGGER_SPLIT_HORIZONTAL] = visible;
    }
}

static xxwidgets_status apply_layout(xxwidgets_debugger *view, xxwidgets_rect bounds,
    unsigned left, unsigned top, const int panes[4], int visible)
{
    xxwidgets_rect rects[XXWIDGETS_DEBUGGER_CONTROL_COUNT], previous[XXWIDGETS_DEBUGGER_CONTROL_COUNT];
    int shown[XXWIDGETS_DEBUGGER_CONTROL_COUNT], old_visible[XXWIDGETS_DEBUGGER_CONTROL_COUNT];
    xxwidgets_status status;
    size_t i;
    layout_rects(bounds, left, top, panes, visible, rects, shown);
    for (i = 0; i < XXWIDGETS_DEBUGGER_CONTROL_COUNT; ++i) {
        previous[i] = view->controls[i]->rect;
        old_visible[i] = view->controls[i]->visible;
    }
    for (i = 0; i < XXWIDGETS_DEBUGGER_CONTROL_COUNT; ++i) {
        status = xxwidgets_widget_set_rect(view->controls[i], rects[i]);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_visible(view->controls[i], shown[i]);
        if (status != XXWIDGETS_OK) {
            size_t j;
            for (j = 0; j < XXWIDGETS_DEBUGGER_CONTROL_COUNT; ++j)
                restore_widget(view->controls[j], previous[j], old_visible[j]);
            return status;
        }
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_debugger_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_debugger **out)
{
    static const xxwidgets_kind kinds[XXWIDGETS_DEBUGGER_CONTROL_COUNT] = {
        XXWIDGETS_DEBUGGER_DISASM, XXWIDGETS_DEBUGGER_REGISTERS, XXWIDGETS_HEXVIEW, XXWIDGETS_DEBUGGER_STACK,
        XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_SPLITTER, XXWIDGETS_SPLITTER
    };
    static const char *const titles[XXWIDGETS_DEBUGGER_CONTROL_COUNT] = {
        XXW_N_("Disassembly"), XXW_N_("Registers"), XXW_N_("Hex"), XXW_N_("Stack"), XXW_N_("Disassembly"), XXW_N_("Registers"), XXW_N_("Hex"), XXW_N_("Stack"),
        XXW_N_("Resize left and right panes"), XXW_N_("Resize upper and lower panes")
    };
    xxwidgets_debugger *view;
    xxwidgets_status status;
    size_t i;
    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !valid_bounds(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing) return XXWIDGETS_BUSY;
    view = calloc(1, sizeof(*view));
    if (!view) return XXWIDGETS_OUT_OF_MEMORY;
    view->owner = owner;
    view->bounds = bounds;
    view->left = 65; view->top = 60; view->visible = 1;
    for (i = 0; i < 4; ++i) view->panels[i] = 1;
    for (i = 0; i < XXWIDGETS_DEBUGGER_CONTROL_COUNT; ++i) {
        status = xxwidgets_widget_create(owner->app, owner, kinds[i], XXW_TR(owner->app, titles[i]),
            (xxwidgets_rect){bounds.x, bounds.y, 1, 1}, &view->controls[i]);
        if (status != XXWIDGETS_OK) goto failed;
        if (i < 4) view->defaults[i] = view->controls[i];
    }
    status = apply_layout(view, bounds, view->left, view->top, view->panels, 1);
    if (status != XXWIDGETS_OK) goto failed;
    *out = view;
    return XXWIDGETS_OK;
failed:
    for (i = 0; i < XXWIDGETS_DEBUGGER_CONTROL_COUNT; ++i)
        if (view->controls[i]) (void)xxwidgets_widget_destroy(view->controls[i]);
    free(view);
    return status;
}

xxwidgets_status xxwidgets_debugger_destroy(xxwidgets_debugger *view)
{
    size_t i;
    xxwidgets_status status;
    if (!view) return XXWIDGETS_INVALID_ARGUMENT;
    if (view->owner->app->dispatch_depth || view->owner->app->polling || view->owner->app->syncing) return XXWIDGETS_BUSY;
    for (i = 0; i < 4; ++i) {
        if (view->controls[i] == view->defaults[i]) continue;
        status = xxwidgets_widget_set_rect(view->controls[i], view->external_rect[i]);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_visible(view->controls[i], view->external_visible[i]);
        if (status != XXWIDGETS_OK) return status;
        view->controls[i] = view->defaults[i];
    }
    for (i = 0; i < XXWIDGETS_DEBUGGER_CONTROL_COUNT; ++i) {
        if (!view->controls[i]) continue;
        status = xxwidgets_widget_destroy(view->controls[i]);
        if (status != XXWIDGETS_OK) return status;
        view->controls[i] = NULL;
        if (i < 4) view->defaults[i] = NULL;
    }
    free(view);
    return XXWIDGETS_OK;
}

xxwidgets_widget *xxwidgets_debugger_control(const xxwidgets_debugger *view, xxwidgets_debugger_control_id id)
{ return view && id >= 0 && id < XXWIDGETS_DEBUGGER_CONTROL_COUNT ? view->controls[id] : NULL; }

xxwidgets_status xxwidgets_debugger_set_rect(xxwidgets_debugger *view, xxwidgets_rect bounds)
{
    xxwidgets_status status;
    if (!view || !valid_bounds(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    status = apply_layout(view, bounds, view->left, view->top, view->panels, view->visible);
    if (status == XXWIDGETS_OK) view->bounds = bounds;
    return status;
}

xxwidgets_status xxwidgets_debugger_get_rect(const xxwidgets_debugger *view, xxwidgets_rect *bounds)
{
    if (!view || !bounds) return XXWIDGETS_INVALID_ARGUMENT;
    *bounds = view->bounds;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_debugger_set_split(xxwidgets_debugger *view, unsigned left, unsigned top)
{
    xxwidgets_status status;
    if (!view || left < 1 || left > 99 || top < 1 || top > 99) return XXWIDGETS_INVALID_ARGUMENT;
    status = apply_layout(view, view->bounds, left, top, view->panels, view->visible);
    if (status == XXWIDGETS_OK) { view->left = left; view->top = top; }
    return status;
}

xxwidgets_status xxwidgets_debugger_get_split(const xxwidgets_debugger *view, unsigned *left, unsigned *top)
{
    if (!view || !left || !top) return XXWIDGETS_INVALID_ARGUMENT;
    *left = view->left; *top = view->top;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_debugger_set_visible(xxwidgets_debugger *view, int visible)
{
    xxwidgets_status status;
    if (!view || (visible != 0 && visible != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    status = apply_layout(view, view->bounds, view->left, view->top, view->panels, visible);
    if (status == XXWIDGETS_OK) view->visible = visible;
    return status;
}

xxwidgets_status xxwidgets_debugger_set_panel_visible(xxwidgets_debugger *view, xxwidgets_debugger_control_id pane, int visible)
{
    int panes[4];
    xxwidgets_status status;
    if (!view || !pane_id(pane) || (visible != 0 && visible != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    memcpy(panes, view->panels, sizeof(panes));
    panes[pane] = visible;
    status = apply_layout(view, view->bounds, view->left, view->top, panes, view->visible);
    if (status == XXWIDGETS_OK) view->panels[pane] = visible;
    return status;
}

xxwidgets_status xxwidgets_debugger_get_panel_visible(const xxwidgets_debugger *view, xxwidgets_debugger_control_id pane, int *visible)
{
    if (!view || !pane_id(pane) || !visible) return XXWIDGETS_INVALID_ARGUMENT;
    *visible = view->panels[pane];
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_debugger_set_panel(xxwidgets_debugger *view, xxwidgets_debugger_control_id pane, xxwidgets_widget *replacement)
{
    xxwidgets_widget *previous;
    xxwidgets_rect previous_rect, replacement_rect, saved_rect;
    int previous_visible, replacement_visible, saved_visible;
    xxwidgets_status status;
    size_t i;
    if (!view || !pane_id(pane)) return XXWIDGETS_INVALID_ARGUMENT;
    if (!replacement) replacement = view->defaults[pane];
    if (!replacement || replacement->parent != view->owner || replacement->app != view->owner->app) return XXWIDGETS_INVALID_ARGUMENT;
    previous = view->controls[pane];
    if (replacement == previous) return XXWIDGETS_OK;
    for (i = 0; i < XXWIDGETS_DEBUGGER_CONTROL_COUNT; ++i)
        if (i != (size_t)pane && view->controls[i] == replacement) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < 4; ++i)
        if (i != (size_t)pane && view->defaults[i] == replacement) return XXWIDGETS_INVALID_ARGUMENT;
    previous_rect = previous->rect; previous_visible = previous->visible;
    replacement_rect = replacement->rect; replacement_visible = replacement->visible;
    saved_rect = view->external_rect[pane]; saved_visible = view->external_visible[pane];
    if (previous == view->defaults[pane]) status = xxwidgets_widget_set_visible(previous, 0);
    else {
        status = xxwidgets_widget_set_rect(previous, saved_rect);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_visible(previous, saved_visible);
    }
    if (status != XXWIDGETS_OK) { restore_widget(previous, previous_rect, previous_visible); return status; }
    view->controls[pane] = replacement;
    view->external_rect[pane] = replacement_rect;
    view->external_visible[pane] = replacement_visible;
    status = apply_layout(view, view->bounds, view->left, view->top, view->panels, view->visible);
    if (status != XXWIDGETS_OK) {
        view->controls[pane] = previous;
        view->external_rect[pane] = saved_rect; view->external_visible[pane] = saved_visible;
        restore_widget(replacement, replacement_rect, replacement_visible);
        restore_widget(previous, previous_rect, previous_visible);
    }
    return status;
}

xxwidgets_status xxwidgets_debugger_handle_event(xxwidgets_debugger *view, const xxwidgets_event *event)
{
    if (!view || !event) return XXWIDGETS_INVALID_ARGUMENT;
    if (event->type != XXWIDGETS_EVENT_DRAG) return XXWIDGETS_OK;
    if (event->widget == view->controls[XXWIDGETS_DEBUGGER_SPLIT_VERTICAL]) {
        int x = clamp(event->x, view->bounds.x, view->bounds.x + view->bounds.width - 1) - view->bounds.x;
        return xxwidgets_debugger_set_split(view, (unsigned)clamp((x * 100 + view->bounds.width - 2) / (view->bounds.width - 1), 1, 99), view->top);
    }
    if (event->widget == view->controls[XXWIDGETS_DEBUGGER_SPLIT_HORIZONTAL]) {
        int y = clamp(event->y, view->bounds.y, view->bounds.y + view->bounds.height - 1) - view->bounds.y;
        return xxwidgets_debugger_set_split(view, view->left, (unsigned)clamp((y * 100 + view->bounds.height - 2) / (view->bounds.height - 1), 1, 99));
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_debugger_set_disassembly(xxwidgets_debugger *view, const xxwidgets_debugger_instruction *rows, size_t count)
{
    if (!view) return XXWIDGETS_INVALID_ARGUMENT;
    if (view->controls[0]->kind != XXWIDGETS_DEBUGGER_DISASM) return XXWIDGETS_UNAVAILABLE;
    return xxwidgets_debugger_disasm_set_rows(view->controls[0], rows, count);
}

xxwidgets_status xxwidgets_debugger_set_registers(xxwidgets_debugger *view, const xxwidgets_debugger_register *rows, size_t count)
{
    if (!view) return XXWIDGETS_INVALID_ARGUMENT;
    if (view->controls[1]->kind != XXWIDGETS_DEBUGGER_REGISTERS) return XXWIDGETS_UNAVAILABLE;
    return xxwidgets_debugger_registers_set_rows(view->controls[1], rows, count);
}

xxwidgets_status xxwidgets_debugger_set_stack(xxwidgets_debugger *view, const xxwidgets_debugger_stack_entry *rows, size_t count)
{
    if (!view) return XXWIDGETS_INVALID_ARGUMENT;
    if (view->controls[3]->kind != XXWIDGETS_DEBUGGER_STACK) return XXWIDGETS_UNAVAILABLE;
    return xxwidgets_debugger_stack_set_rows(view->controls[3], rows, count);
}

xxwidgets_status xxwidgets_debugger_set_memory(xxwidgets_debugger *view, uint64_t address, const void *bytes, size_t size)
{
    if (!view) return XXWIDGETS_INVALID_ARGUMENT;
    if (view->controls[2]->kind != XXWIDGETS_HEXVIEW) return XXWIDGETS_UNAVAILABLE;
    return xxwidgets_hexview_replace_snapshot(view->controls[2], address, bytes, size);
}

xxwidgets_status xxwidgets_debugger_set_pane_status(xxwidgets_debugger *view, xxwidgets_debugger_control_id pane, const char *message)
{
    static const char *const names[4] = {XXW_N_("Disassembly"), XXW_N_("Registers"), XXW_N_("Hex"), XXW_N_("Stack")};
    size_t a, b;
    char *caption;
    const char *name;
    xxwidgets_status status;
    if (!view || !pane_id(pane) || (message && !xxwidgets_valid_utf8(message))) return XXWIDGETS_INVALID_ARGUMENT;
    name = XXW_TR(view->owner->app, names[pane]);
    if (!message || !*message) return xxwidgets_widget_set_text(view->controls[pane + 4], name);
    a = strlen(name); b = strlen(message);
    if (b > SIZE_MAX - a - 5) return XXWIDGETS_INVALID_ARGUMENT;
    caption = malloc(a + b + 5);
    if (!caption) return XXWIDGETS_OUT_OF_MEMORY;
    snprintf(caption, a + b + 5, "%s - %s", name, message);
    status = xxwidgets_widget_set_text(view->controls[pane + 4], caption);
    free(caption);
    return status;
}

xxwidgets_status xxwidgets_debugger_clear(xxwidgets_debugger *view)
{
    xxwidgets_status status;
    size_t i;
    if (!view) return XXWIDGETS_INVALID_ARGUMENT;
    if (view->controls[0]->kind == XXWIDGETS_DEBUGGER_DISASM && (status = xxwidgets_debugger_set_disassembly(view, NULL, 0)) != XXWIDGETS_OK) return status;
    if (view->controls[1]->kind == XXWIDGETS_DEBUGGER_REGISTERS && (status = xxwidgets_debugger_set_registers(view, NULL, 0)) != XXWIDGETS_OK) return status;
    if (view->controls[2]->kind == XXWIDGETS_HEXVIEW && (status = xxwidgets_debugger_set_memory(view, 0, NULL, 0)) != XXWIDGETS_OK) return status;
    if (view->controls[3]->kind == XXWIDGETS_DEBUGGER_STACK && (status = xxwidgets_debugger_set_stack(view, NULL, 0)) != XXWIDGETS_OK) return status;
    for (i = 0; i < 4; ++i) {
        status = xxwidgets_debugger_set_pane_status(view, (xxwidgets_debugger_control_id)i, NULL);
        if (status != XXWIDGETS_OK) return status;
    }
    return XXWIDGETS_OK;
}
