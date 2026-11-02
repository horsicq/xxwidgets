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

/* Private row/column layout engine for components placed in layout units
 * (xxwidgets_widget_set_frame). Sizes come from xxwidgets_app_get_metrics and
 * xxwidgets_widget_get_preferred_size; nothing here is a fixed pixel. Units
 * are cells on the terminal and the test mock, so the same code is tested in
 * the core suite. Not part of the public API. */

#ifndef XXWIDGETS_LAYOUT_H
#define XXWIDGETS_LAYOUT_H

#include "xxwidgets/xxwidgets.h"

/* One box along a line: the widths of a row or the heights of a column. */
typedef struct xxwidgets_layout_item {
    int minimum;   /* smallest size the item accepts (>= 0) */
    int preferred; /* natural size; raised to minimum when smaller */
    int maximum;   /* largest size when stretched; 0: no limit */
    int stretch;   /* weight in the extra space; 0 keeps the preferred size */
    int hidden;    /* takes neither size nor spacing (a hidden control) */
    int spacer;    /* an expanding gap: no spacing follows it, so "left items,
                    * spacer, right items" right-aligns the right items */
    int position;  /* out: start of the item */
    int size;      /* out: its size (0 when hidden) */
} xxwidgets_layout_item;

/* Smallest / natural length of a line: the visible items' minimums
 * (preferred sizes) plus spacing between them. */
int xxwidgets_layout_line_minimum(const xxwidgets_layout_item *items, size_t count, int spacing);
int xxwidgets_layout_line_preferred(const xxwidgets_layout_item *items, size_t count, int spacing);

/* Lays the items out from start within length units:
 *   - enough room: every item gets its preferred size, then the extra space
 *     is shared by stretch weight (an item stops at its maximum, the rest
 *     goes to the others); room nobody takes stays after the last item;
 *   - too little room: items shrink from preferred towards minimum in
 *     proportion to how much each can give; below the line minimum every
 *     item keeps its minimum and the line overflows.
 * Hidden items get position = the next position and size 0. Returns the end
 * of the last visible item (start when none is visible). */
int xxwidgets_layout_line(xxwidgets_layout_item *items, size_t count, int start, int length, int spacing);

/* The widest of count values (0 for none): column widths of a grid, or the
 * height of a stack whose pages must all be equally tall. */
int xxwidgets_layout_max(const int *values, size_t count);

/* A captioned box (Qt group box look): a caption label_height high on top,
 * caption_gap, then a frame holding one control with frame_inset on every
 * side. */
typedef struct xxwidgets_layout_caption_box {
    xxwidgets_rect caption; /* the LABEL */
    xxwidgets_rect frame;   /* the GROUP */
    xxwidgets_rect control; /* the value, combo box or button row */
} xxwidgets_layout_caption_box;
int xxwidgets_layout_caption_box_height(const xxwidgets_metrics *metrics, int control_height);
int xxwidgets_layout_caption_box_width(const xxwidgets_metrics *metrics, int control_width);
/* Splits box (as wide and tall as the caller decided) into the three parts.
 * The control is control_height high at the top of the frame interior. */
void xxwidgets_layout_caption_box_split(const xxwidgets_metrics *metrics, xxwidgets_rect box, int control_height,
                                        xxwidgets_layout_caption_box *out);

/* xxwidgets_widget_set_frame after clamping rect to a valid frame: x and y
 * at least 0, width and height at least 1. A NULL widget is skipped (OK). */
xxwidgets_status xxwidgets_layout_place(xxwidgets_widget *widget, xxwidgets_rect rect);

static inline xxwidgets_rect xxwidgets_layout_rect(int x, int y, int width, int height)
{
    xxwidgets_rect rect;
    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;
    return rect;
}

#endif
