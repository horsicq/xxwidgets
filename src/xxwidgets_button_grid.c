/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_components_internal.h"
#include "xxwidgets/xxwidgets_button_grid.h"

#include <stdlib.h>
#include <string.h>

typedef struct button_grid_entry {
    int id;
    char *label, *tooltip, *override_tooltip;
    xxwidgets_widget *button;
    xxwidgets_grid_item_state state;
} button_grid_entry;

struct xxwidgets_button_grid {
    xxwidgets_widget *owner, *frame;
    button_grid_entry *entries;
    size_t count;
    unsigned int columns, flags;
    int visible, enabled_all;
    xxwidgets_rect bounds;
    int has_bounds;
};

static button_grid_entry *find_entry(const xxwidgets_button_grid *grid, int id)
{
    size_t i;
    if (!grid) return NULL;
    for (i = 0; i < grid->count; ++i)
        if (grid->entries[i].id == id) return &grid->entries[i];
    return NULL;
}

static xxwidgets_status apply_entry(xxwidgets_button_grid *grid, button_grid_entry *entry)
{
    const char *tip = entry->override_tooltip ? entry->override_tooltip : (entry->tooltip ? entry->tooltip : entry->label);
    xxwidgets_status status = xxwidgets_widget_set_text(entry->button, XXW_TR(grid->owner->app, entry->label));
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_tooltip(entry->button, XXW_TR(grid->owner->app, tip));
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_set_visible(entry->button, grid->visible && entry->state != XXWIDGETS_GRID_ITEM_HIDDEN);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_set_enabled(entry->button, grid->enabled_all && entry->state == XXWIDGETS_GRID_ITEM_ENABLED);
    return status;
}

xxwidgets_status xxwidgets_button_grid_create(xxwidgets_widget *owner, const xxwidgets_button_grid_item *items, size_t count,
                                              unsigned int columns, unsigned int flags, xxwidgets_button_grid **out)
{
    static const xxwidgets_rect start = {0, 0, 1, 1};
    xxwidgets_button_grid *grid;
    xxwidgets_status status = XXWIDGETS_OK;
    size_t i, j;
    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || (!items && count) || !count || !columns || columns > count ||
        (flags & ~(unsigned int)XXWIDGETS_BUTTON_GRID_FRAMED) || xxwidgets_component_busy(owner->app))
        return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < count; ++i) {
        if (!items[i].label || !xxwidgets_valid_utf8(items[i].label) ||
            (items[i].tooltip && !xxwidgets_valid_utf8(items[i].tooltip)) ||
            items[i].icon < XXWIDGETS_ICON_NONE || items[i].icon >= XXWIDGETS_ICON_COUNT)
            return XXWIDGETS_INVALID_ARGUMENT;
        for (j = 0; j < i; ++j) if (items[j].id == items[i].id) return XXWIDGETS_INVALID_ARGUMENT;
    }
    grid = (xxwidgets_button_grid *)calloc(1, sizeof(*grid));
    if (!grid) return XXWIDGETS_OUT_OF_MEMORY;
    grid->entries = (button_grid_entry *)calloc(count, sizeof(*grid->entries));
    if (!grid->entries) { free(grid); return XXWIDGETS_OUT_OF_MEMORY; }
    grid->owner = owner; grid->count = count; grid->columns = columns; grid->flags = flags;
    grid->visible = grid->enabled_all = 1;
    if ((flags & XXWIDGETS_BUTTON_GRID_FRAMED) && xxwidgets_app_supports_kind(owner->app, XXWIDGETS_GROUP))
        status = xxwidgets_widget_create(owner->app, owner, XXWIDGETS_GROUP, "", start, &grid->frame);
    for (i = 0; status == XXWIDGETS_OK && i < count; ++i) {
        button_grid_entry *entry = &grid->entries[i];
        entry->id = items[i].id; entry->state = XXWIDGETS_GRID_ITEM_ENABLED;
        entry->label = xxwidgets_strdup(items[i].label);
        entry->tooltip = items[i].tooltip ? xxwidgets_strdup(items[i].tooltip) : NULL;
        if (!entry->label || (items[i].tooltip && !entry->tooltip)) { status = XXWIDGETS_OUT_OF_MEMORY; break; }
        status = xxwidgets_widget_create(owner->app, owner, XXWIDGETS_BUTTON, XXW_TR(owner->app, items[i].label), start, &entry->button);
        if (status == XXWIDGETS_OK && items[i].icon != XXWIDGETS_ICON_NONE)
            status = xxwidgets_button_set_icon_id(entry->button, items[i].icon, 0);
        if (status == XXWIDGETS_OK) status = apply_entry(grid, entry);
    }
    if (status != XXWIDGETS_OK) { (void)xxwidgets_button_grid_destroy(grid); return status; }
    *out = grid;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_button_grid_destroy(xxwidgets_button_grid *grid)
{
    size_t i;
    if (!grid) return XXWIDGETS_INVALID_ARGUMENT;
    if (xxwidgets_component_busy(grid->owner->app)) return XXWIDGETS_BUSY;
    for (i = grid->count; i-- > 0;) {
        if (grid->entries[i].button) (void)xxwidgets_widget_destroy(grid->entries[i].button);
        free(grid->entries[i].label); free(grid->entries[i].tooltip); free(grid->entries[i].override_tooltip);
    }
    if (grid->frame) (void)xxwidgets_widget_destroy(grid->frame);
    free(grid->entries); free(grid);
    return XXWIDGETS_OK;
}

static xxwidgets_status grid_size(xxwidgets_button_grid *grid, int minimum, int *width, int *height, int *column_widths)
{
    xxwidgets_metrics m;
    xxwidgets_status status;
    size_t i;
    unsigned int rows = (unsigned int)((grid->count + grid->columns - 1) / grid->columns);
    int total = 0;
    if (!grid || !width || !height) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_app_get_metrics(grid->owner->app, &m);
    if (status != XXWIDGETS_OK) return status;
    if (column_widths) memset(column_widths, 0, grid->columns * sizeof(*column_widths));
    for (i = 0; i < grid->count; ++i) {
        int w, h, column = (int)(i % grid->columns);
        status = xxwidgets_widget_get_preferred_size(grid->entries[i].button, &w, &h);
        if (status != XXWIDGETS_OK) return status;
        if (column_widths && w > column_widths[column]) column_widths[column] = w;
    }
    if (minimum && column_widths)
        for (i = 0; i < grid->columns; ++i) column_widths[i] = 3 * m.control_height;
    if (column_widths) for (i = 0; i < grid->columns; ++i) total += column_widths[i];
    if (grid->columns > 1) total += ((int)grid->columns - 1) * m.spacing;
    if (grid->flags & XXWIDGETS_BUTTON_GRID_FRAMED) total += 2 * m.panel_inset;
    *width = minimum ? total : total;
    *height = (int)rows * m.control_height + (rows > 1 ? ((int)rows - 1) * m.spacing : 0) +
              ((grid->flags & XXWIDGETS_BUTTON_GRID_FRAMED) ? 2 * m.panel_inset : 0);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_button_grid_get_preferred_size(xxwidgets_button_grid *grid, int *width, int *height)
{
    int *columns;
    xxwidgets_status status;
    if (!grid) return XXWIDGETS_INVALID_ARGUMENT;
    columns = (int *)calloc(grid->columns, sizeof(*columns));
    if (!columns) return XXWIDGETS_OUT_OF_MEMORY;
    status = grid_size(grid, 0, width, height, columns);
    free(columns);
    return status;
}

xxwidgets_status xxwidgets_button_grid_get_minimum_size(xxwidgets_button_grid *grid, int *width, int *height)
{
    int *columns;
    xxwidgets_status status;
    if (!grid) return XXWIDGETS_INVALID_ARGUMENT;
    columns = (int *)calloc(grid->columns, sizeof(*columns));
    if (!columns) return XXWIDGETS_OUT_OF_MEMORY;
    status = grid_size(grid, 1, width, height, columns);
    free(columns);
    return status;
}

xxwidgets_status xxwidgets_button_grid_set_bounds(xxwidgets_button_grid *grid, xxwidgets_rect units)
{
    xxwidgets_metrics m;
    xxwidgets_status status;
    xxwidgets_rect inner;
    int *widths, preferred = 0, unused, extra, x, available, total = 0;
    size_t i;
    if (!grid || units.x < 0 || units.y < 0 || units.width < 1 || units.height < 1) return XXWIDGETS_INVALID_ARGUMENT;
    widths = (int *)calloc(grid->columns, sizeof(*widths));
    if (!widths) return XXWIDGETS_OUT_OF_MEMORY;
    status = xxwidgets_app_get_metrics(grid->owner->app, &m);
    if (status == XXWIDGETS_OK) status = grid_size(grid, 0, &preferred, &unused, widths);
    inner = units;
    if (status == XXWIDGETS_OK && grid->frame) status = xxwidgets_layout_place(grid->frame, units);
    if (grid->flags & XXWIDGETS_BUTTON_GRID_FRAMED) {
        inner.x += m.panel_inset; inner.y += m.panel_inset;
        inner.width -= 2 * m.panel_inset; inner.height -= 2 * m.panel_inset;
    }
    for (i = 0; i < grid->columns; ++i) total += widths[i];
    available = inner.width - ((int)grid->columns - 1) * m.spacing;
    if (available < (int)grid->columns) available = (int)grid->columns;
    if (total > available) {
        int used = 0;
        for (i = 0; i < grid->columns; ++i) {
            widths[i] = xxwidgets_component_max(1, (int)((long long)widths[i] * available / total));
            used += widths[i];
        }
        for (i = 0; used < available; ++i, ++used) ++widths[i % grid->columns];
    }
    extra = status == XXWIDGETS_OK ? units.width - preferred : 0;
    if (extra > 0) for (i = 0; i < grid->columns; ++i) widths[i] += extra / (int)grid->columns + ((int)i < extra % (int)grid->columns);
    x = inner.x;
    for (i = 0; status == XXWIDGETS_OK && i < grid->count; ++i) {
        unsigned int column = (unsigned int)(i % grid->columns), row = (unsigned int)(i / grid->columns);
        if (!column) x = inner.x;
        status = xxwidgets_layout_place(grid->entries[i].button,
            xxwidgets_layout_rect(x, inner.y + (int)row * (m.control_height + m.spacing), widths[column], m.control_height));
        x += widths[column] + m.spacing;
    }
    free(widths);
    if (status == XXWIDGETS_OK) { grid->bounds = units; grid->has_bounds = 1; }
    return status;
}

xxwidgets_status xxwidgets_button_grid_set_item_state(xxwidgets_button_grid *grid, int id, xxwidgets_grid_item_state state,
                                                      const char *tooltip_override)
{
    button_grid_entry *entry = find_entry(grid, id);
    char *copy = NULL;
    if (!entry || state < XXWIDGETS_GRID_ITEM_ENABLED || state > XXWIDGETS_GRID_ITEM_HIDDEN ||
        (tooltip_override && !xxwidgets_valid_utf8(tooltip_override))) return XXWIDGETS_INVALID_ARGUMENT;
    if (tooltip_override) { copy = xxwidgets_strdup(tooltip_override); if (!copy) return XXWIDGETS_OUT_OF_MEMORY; }
    free(entry->override_tooltip); entry->override_tooltip = copy; entry->state = state;
    return apply_entry(grid, entry);
}

xxwidgets_widget *xxwidgets_button_grid_button(const xxwidgets_button_grid *grid, int id)
{
    button_grid_entry *entry = find_entry(grid, id);
    return entry ? entry->button : NULL;
}

xxwidgets_widget *xxwidgets_button_grid_frame(const xxwidgets_button_grid *grid) { return grid ? grid->frame : NULL; }

xxwidgets_status xxwidgets_button_grid_set_visible(xxwidgets_button_grid *grid, int visible)
{
    xxwidgets_status status = XXWIDGETS_OK;
    size_t i;
    if (!grid || (visible != 0 && visible != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    grid->visible = visible;
    if (grid->frame) status = xxwidgets_widget_set_visible(grid->frame, visible);
    for (i = 0; status == XXWIDGETS_OK && i < grid->count; ++i) status = apply_entry(grid, &grid->entries[i]);
    return status;
}

xxwidgets_status xxwidgets_button_grid_set_enabled_all(xxwidgets_button_grid *grid, int enabled)
{
    xxwidgets_status status = XXWIDGETS_OK;
    size_t i;
    if (!grid || (enabled != 0 && enabled != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    grid->enabled_all = enabled;
    for (i = 0; status == XXWIDGETS_OK && i < grid->count; ++i) status = apply_entry(grid, &grid->entries[i]);
    return status;
}

xxwidgets_status xxwidgets_button_grid_retranslate(xxwidgets_button_grid *grid)
{
    xxwidgets_status status = XXWIDGETS_OK;
    size_t i;
    if (!grid) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; status == XXWIDGETS_OK && i < grid->count; ++i) status = apply_entry(grid, &grid->entries[i]);
    if (status == XXWIDGETS_OK && grid->has_bounds) {
        int width, height;
        status = xxwidgets_button_grid_get_minimum_size(grid, &width, &height);
        if (status == XXWIDGETS_OK && grid->bounds.width >= width && grid->bounds.height >= height)
            status = xxwidgets_button_grid_set_bounds(grid, grid->bounds);
    }
    return status;
}

int xxwidgets_button_grid_handle_event(xxwidgets_button_grid *grid, const xxwidgets_event *event, int *id)
{
    size_t i;
    if (!grid || !event || !event->widget) return 0;
    for (i = 0; i < grid->count; ++i) if (grid->entries[i].button == event->widget) {
        if (id) *id = grid->entries[i].id;
        return 1;
    }
    if (grid->frame == event->widget) { if (id) *id = -1; return 1; }
    return 0;
}
