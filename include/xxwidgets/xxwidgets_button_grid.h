/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_BUTTON_GRID_H
#define XXWIDGETS_BUTTON_GRID_H

#include "xxwidgets/xxwidgets.h"
#include "xxwidgets/xxwidgets_icons.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_button_grid xxwidgets_button_grid;

typedef struct xxwidgets_button_grid_item {
    int id;
    const char *label;   /* untranslated */
    xxwidgets_icon_id icon;
    const char *tooltip; /* untranslated; NULL follows label */
} xxwidgets_button_grid_item;

typedef enum xxwidgets_grid_item_state {
    XXWIDGETS_GRID_ITEM_ENABLED = 0,
    XXWIDGETS_GRID_ITEM_DISABLED,
    XXWIDGETS_GRID_ITEM_HIDDEN
} xxwidgets_grid_item_state;

enum { XXWIDGETS_BUTTON_GRID_FRAMED = 1u << 0 };

XXWIDGETS_API xxwidgets_status xxwidgets_button_grid_create(xxwidgets_widget *owner, const xxwidgets_button_grid_item *items,
                                                            size_t count, unsigned int columns, unsigned int flags,
                                                            xxwidgets_button_grid **out);
XXWIDGETS_API xxwidgets_status xxwidgets_button_grid_destroy(xxwidgets_button_grid *grid);
XXWIDGETS_API xxwidgets_status xxwidgets_button_grid_set_bounds(xxwidgets_button_grid *grid, xxwidgets_rect units);
XXWIDGETS_API xxwidgets_status xxwidgets_button_grid_get_preferred_size(xxwidgets_button_grid *grid, int *width, int *height);
XXWIDGETS_API xxwidgets_status xxwidgets_button_grid_get_minimum_size(xxwidgets_button_grid *grid, int *width, int *height);
XXWIDGETS_API xxwidgets_status xxwidgets_button_grid_set_item_state(xxwidgets_button_grid *grid, int id,
                                                                   xxwidgets_grid_item_state state,
                                                                   const char *tooltip_override);
XXWIDGETS_API xxwidgets_widget *xxwidgets_button_grid_button(const xxwidgets_button_grid *grid, int id);
XXWIDGETS_API xxwidgets_widget *xxwidgets_button_grid_frame(const xxwidgets_button_grid *grid);
XXWIDGETS_API xxwidgets_status xxwidgets_button_grid_set_visible(xxwidgets_button_grid *grid, int visible);
XXWIDGETS_API xxwidgets_status xxwidgets_button_grid_set_enabled_all(xxwidgets_button_grid *grid, int enabled);
XXWIDGETS_API xxwidgets_status xxwidgets_button_grid_retranslate(xxwidgets_button_grid *grid);
XXWIDGETS_API int xxwidgets_button_grid_handle_event(xxwidgets_button_grid *grid, const xxwidgets_event *event, int *id);

#ifdef __cplusplus
}
#endif
#endif
