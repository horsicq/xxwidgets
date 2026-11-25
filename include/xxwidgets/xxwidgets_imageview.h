/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/* Image view: shows one RGBA image in a CANVAS widget, fitted to the widget's
 * geometry.
 *
 * The view keeps its own copy of the source pixels and the view state (zoom
 * mode, zoom factor, pan centre, background, smoothing).  Whenever the widget
 * is painted at a new size, or the state changes, it renders exactly the
 * visible part at the widget's size (box filter when shrinking, nearest
 * neighbour when enlarging, straight alpha composited over a checkerboard or a
 * solid colour) and hands that to the canvas, so every backend draws it 1:1.
 * Native backends draw device pixels; the terminal backend draws text cells
 * and corrects for their 1:2 aspect.
 *
 * Input: arrow keys pan, Page Up / Page Down zoom in / out, Home fits, End
 * shows actual size, Enter emits ACTIVATE.  The mouse wheel zooms around the
 * pointer where the backend reports it.  A click emits SELECT with the image
 * x / y of the pixel under the pointer (value = y * width + x); clicks outside
 * the image are ignored.  CHANGE (value = zoom in percent) follows every zoom
 * or pan change made by input.
 *
 * Create the widget as an ordinary XXWIDGETS_CANVAS and call
 * xxwidgets_imageview_attach once.  Canvas pixel / label setters must not be
 * used on an attached widget.
 */
#ifndef XXWIDGETS_IMAGEVIEW_H
#define XXWIDGETS_IMAGEVIEW_H
#include "xxwidgets.h"
#ifdef __cplusplus
extern "C" {
#endif

/** Straight-alpha RGBA8 pixels, top row first; stride = bytes per row
 *  (0 means width * 4).  Width and height 1..65535. */
typedef struct xxwidgets_image {
    unsigned int width, height;
    size_t stride;
    const uint8_t *rgba;
} xxwidgets_image;

typedef enum xxwidgets_image_zoom_mode {
    XXWIDGETS_IMAGE_FIT_DOWN = 0, /* fit the widget, never enlarge (default) */
    XXWIDGETS_IMAGE_FIT,          /* fit the widget, enlarge small images */
    XXWIDGETS_IMAGE_FILL,         /* cover the widget, cropping the overflow */
    XXWIDGETS_IMAGE_ACTUAL,       /* one image pixel per device pixel */
    XXWIDGETS_IMAGE_ZOOM          /* the factor given to set_zoom */
} xxwidgets_image_zoom_mode;

typedef enum xxwidgets_image_background {
    XXWIDGETS_IMAGE_BACKGROUND_CHECKER = 0, /* light checkerboard under transparency (default) */
    XXWIDGETS_IMAGE_BACKGROUND_COLOR        /* solid colour, see set_background */
} xxwidgets_image_background;

/** Turns a CANVAS widget into an image view (idempotent). */
XXWIDGETS_API xxwidgets_status xxwidgets_imageview_attach(xxwidgets_widget *canvas);
/** True when @p widget is an attached image view. */
XXWIDGETS_API int xxwidgets_imageview_is_attached(const xxwidgets_widget *widget);

/** Copies @p image; NULL clears the view.  The zoom mode is kept; the pan
 *  resets to the image centre. */
XXWIDGETS_API xxwidgets_status xxwidgets_imageview_set_image(xxwidgets_widget *view, const xxwidgets_image *image);
/** The current source size, 0 x 0 when empty. */
XXWIDGETS_API xxwidgets_status xxwidgets_imageview_get_image_size(const xxwidgets_widget *view, unsigned int *width, unsigned int *height);

/** @p factor is used by XXWIDGETS_IMAGE_ZOOM only (0.01 .. 64; 1 = actual size). */
XXWIDGETS_API xxwidgets_status xxwidgets_imageview_set_zoom(xxwidgets_widget *view, xxwidgets_image_zoom_mode mode, double factor);
/** The mode and the factor in effect at the last layout (fit modes report
 *  the computed factor). */
XXWIDGETS_API xxwidgets_status xxwidgets_imageview_get_zoom(const xxwidgets_widget *view, xxwidgets_image_zoom_mode *mode, double *factor);
/** Multiplies the effective factor by 1.25 (steps > 0) or 0.8 (steps < 0)
 *  per step, switching to XXWIDGETS_IMAGE_ZOOM. */
XXWIDGETS_API xxwidgets_status xxwidgets_imageview_zoom_by(xxwidgets_widget *view, int steps);

/** Centre of the view in image coordinates; clamped at layout so the image
 *  stays on screen. */
XXWIDGETS_API xxwidgets_status xxwidgets_imageview_set_center(xxwidgets_widget *view, double x, double y);
XXWIDGETS_API xxwidgets_status xxwidgets_imageview_get_center(const xxwidgets_widget *view, double *x, double *y);

/** Background outside the image and under transparency; rgba is 0xRRGGBBAA
 *  (alpha ignored).  The area outside the image always uses @p outside. */
XXWIDGETS_API xxwidgets_status xxwidgets_imageview_set_background(xxwidgets_widget *view, xxwidgets_image_background mode, uint32_t rgba, uint32_t outside);
/** Smooth (box filter) downscaling, on by default; off uses nearest neighbour. */
XXWIDGETS_API xxwidgets_status xxwidgets_imageview_set_smooth(xxwidgets_widget *view, int smooth);

/** Maps a point in widget units (as given to the canvas) to image pixel
 *  coordinates; UNAVAILABLE when the point is outside the image. */
XXWIDGETS_API xxwidgets_status xxwidgets_imageview_map_point(const xxwidgets_widget *view, double x, double y, unsigned int *image_x, unsigned int *image_y);
/** The last pixel selected by click; UNAVAILABLE when none. */
XXWIDGETS_API xxwidgets_status xxwidgets_imageview_get_selection(const xxwidgets_widget *view, unsigned int *x, unsigned int *y);

#ifdef __cplusplus
}
#endif
#endif
