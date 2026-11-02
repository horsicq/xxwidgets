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

#ifndef XXWIDGETS_SIDE_PANEL_H
#define XXWIDGETS_SIDE_PANEL_H

#include "xxwidgets/xxwidgets.h"
#include "xxwidgets/xxwidgets_icons.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A framed column of checkboxes and icon+text buttons (the right column of
 * the Detect It Easy window). Top items are stacked from the top, bottom
 * items from the bottom, with a spacer between; every item is H high with S
 * between them (Qt: pitch H + 6). The column is as wide as its widest item
 * plus 2 * panel_inset. Each item's tooltip is its label. A hidden item
 * leaves no gap. Conventions: see xxwidgets_value_field.h. */
typedef struct xxwidgets_side_panel xxwidgets_side_panel;

typedef struct xxwidgets_side_panel_item {
    int id;                 /* application id, unique within the panel */
    xxwidgets_kind kind;    /* XXWIDGETS_CHECKBOX or XXWIDGETS_BUTTON */
    const char *label;      /* untranslated (XXW_N_), copied; also the tooltip */
    xxwidgets_icon_id icon; /* left of the text; XXWIDGETS_ICON_NONE for none */
    int bottom;             /* 0: top group (from the top); 1: bottom group (from the bottom) */
} xxwidgets_side_panel_item;

/* items are copied; creation (Tab) order is the order given. */
XXWIDGETS_API xxwidgets_status xxwidgets_side_panel_create(xxwidgets_widget *owner, const xxwidgets_side_panel_item *items, size_t count,
                                                         xxwidgets_side_panel **out);
XXWIDGETS_API xxwidgets_status xxwidgets_side_panel_destroy(xxwidgets_side_panel *panel);
XXWIDGETS_API xxwidgets_status xxwidgets_side_panel_set_bounds(xxwidgets_side_panel *panel, xxwidgets_rect units);
/* Widest visible item plus 2 * panel_inset. */
XXWIDGETS_API xxwidgets_status xxwidgets_side_panel_get_preferred_width(xxwidgets_side_panel *panel, int *width);
/* Visible items of both groups, their spacing and the insets. */
XXWIDGETS_API xxwidgets_status xxwidgets_side_panel_get_minimum_height(xxwidgets_side_panel *panel, int *height);
XXWIDGETS_API xxwidgets_status xxwidgets_side_panel_set_item_visible(xxwidgets_side_panel *panel, int id, int visible);
XXWIDGETS_API xxwidgets_status xxwidgets_side_panel_set_item_enabled(xxwidgets_side_panel *panel, int id, int enabled);
/* CHECKBOX items; emits no event. */
XXWIDGETS_API xxwidgets_status xxwidgets_side_panel_set_checked(xxwidgets_side_panel *panel, int id, int checked);
XXWIDGETS_API xxwidgets_status xxwidgets_side_panel_get_checked(xxwidgets_side_panel *panel, int id, int *checked);
XXWIDGETS_API xxwidgets_status xxwidgets_side_panel_set_visible(xxwidgets_side_panel *panel, int visible);
XXWIDGETS_API xxwidgets_status xxwidgets_side_panel_retranslate(xxwidgets_side_panel *panel);
/* Borrowed control of an item id, or NULL. */
XXWIDGETS_API xxwidgets_widget *xxwidgets_side_panel_control(const xxwidgets_side_panel *panel, int id);
/* Borrowed GROUP around the column; NULL on a backend without GROUP. */
XXWIDGETS_API xxwidgets_widget *xxwidgets_side_panel_frame(const xxwidgets_side_panel *panel);
/* 1 when event is a CLICK (button) or CHANGE (checkbox) of an item: *id is
 * the item id, *value the checkbox state (0 for buttons). */
XXWIDGETS_API int xxwidgets_side_panel_handle_event(xxwidgets_side_panel *panel, const xxwidgets_event *event, int *id, int *value);

#ifdef __cplusplus
}
#endif
#endif
