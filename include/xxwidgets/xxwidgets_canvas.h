/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_CANVAS_H
#define XXWIDGETS_CANVAS_H
#include "xxwidgets.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xxwidgets_canvas_label {unsigned int x,y;const char *text;uint32_t color;} xxwidgets_canvas_label;
typedef struct xxwidgets_canvas_region {unsigned int x,y,width,height;const char *label;} xxwidgets_canvas_region;
/* Pixels are packed 0xRRGGBBAA, top-left origin. Setters copy inputs and preserve
 * previous content on error. At most 4096x4096 pixels and 4096 UTF-8 labels. */
XXWIDGETS_API xxwidgets_status xxwidgets_canvas_set_pixels(xxwidgets_widget *,unsigned int width,unsigned int height,const uint32_t *rgba);
XXWIDGETS_API xxwidgets_status xxwidgets_canvas_set_labels(xxwidgets_widget *,const xxwidgets_canvas_label *,size_t count);
XXWIDGETS_API xxwidgets_status xxwidgets_canvas_get_pixels(const xxwidgets_widget *,unsigned int *width,unsigned int *height,const uint32_t **rgba);
/* value is a pixel index. SELECT reports image x/y and value=y*width+x. */
XXWIDGETS_API xxwidgets_status xxwidgets_canvas_select(xxwidgets_widget *,unsigned int x,unsigned int y);
/* Read the selected source pixel. UNAVAILABLE means no selection (including
 * immediately after set_pixels). Errors preserve both output coordinates. */
XXWIDGETS_API xxwidgets_status xxwidgets_canvas_get_selection(const xxwidgets_widget *,unsigned int *x,unsigned int *y);
/* Native drag splitters emit DRAG with absolute parent cell coordinates x/y;
 * value is x for vertical splitters, y for horizontal. Arrows move one cell. */
#ifdef __cplusplus
}
#endif
#endif
