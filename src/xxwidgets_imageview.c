/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/* Image view on top of the CANVAS widget; see xxwidgets_imageview.h.
 *
 * Layout works in widget units (device pixels on native backends, text cells
 * on the terminal).  An image pixel (ix, iy) lands at
 *     x = off_x + ix * scale,   y = off_y + iy * scale / aspect
 * where aspect is the height / width ratio of one widget unit (1 on native
 * backends, 2 for terminal cells).  The render buffer covers the whole widget
 * (at most 4096 x 4096; larger widgets get a buffer the backend stretches). */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_canvas.h"
#include "xxwidgets/xxwidgets_imageview.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define IV_MAX_BUFFER 4096U
#define IV_MIN_ZOOM 0.01
#define IV_MAX_ZOOM 64.0

struct xxwidgets_imageview {
    uint8_t *rgba; /* owned copy, width * height * 4 */
    unsigned width, height;
    xxwidgets_image_zoom_mode mode;
    double factor;    /* XXWIDGETS_IMAGE_ZOOM */
    double effective; /* factor at the last layout */
    double cx, cy;    /* view centre in image coordinates */
    int centered;     /* cx / cy not set yet: use the image centre */
    double aspect;    /* widget unit height / width */
    xxwidgets_image_background background;
    uint32_t back_rgba, outside_rgba;
    int smooth;
    /* last layout */
    double lw, lh, off_x, off_y;
    int dirty;
    int have_layout;
    int selected;
    unsigned sel_x, sel_y;
};

static xxwidgets_imageview *iv_get(const xxwidgets_widget *w)
{
    return (w && w->kind == XXWIDGETS_CANVAS) ? w->imageview : NULL;
}

static void iv_sync(xxwidgets_widget *w)
{
    ++w->app->syncing;
    (void)w->app->ops->sync(w);
    --w->app->syncing;
}

void xxwidgets_imageview_dispose(xxwidgets_widget *w)
{
    if (!w || !w->imageview) return;
    free(w->imageview->rgba);
    free(w->imageview);
    w->imageview = NULL;
}

xxwidgets_status xxwidgets_imageview_attach(xxwidgets_widget *w)
{
    xxwidgets_imageview *v;
    if (!w || w->kind != XXWIDGETS_CANVAS) return XXWIDGETS_INVALID_ARGUMENT;
    if (w->imageview) return XXWIDGETS_OK;
    v = calloc(1, sizeof(*v));
    if (!v) return XXWIDGETS_OUT_OF_MEMORY;
    v->mode = XXWIDGETS_IMAGE_FIT_DOWN;
    v->factor = 1.0;
    v->effective = 1.0;
    v->aspect = 1.0;
    v->centered = 1;
    v->background = XXWIDGETS_IMAGE_BACKGROUND_CHECKER;
    v->back_rgba = 0xFFFFFFFFU;
    v->outside_rgba = 0x505050FFU;
    v->smooth = 1;
    v->dirty = 1;
    w->imageview = v;
    w->value = -1;
    return XXWIDGETS_OK;
}

int xxwidgets_imageview_is_attached(const xxwidgets_widget *w)
{
    return iv_get(w) != NULL;
}

xxwidgets_status xxwidgets_imageview_set_image(xxwidgets_widget *w, const xxwidgets_image *img)
{
    xxwidgets_imageview *v = iv_get(w);
    uint8_t *copy = NULL;
    if (!v) return XXWIDGETS_INVALID_ARGUMENT;
    if (img) {
        size_t stride = img->stride ? img->stride : (size_t)img->width * 4;
        if (!img->rgba || !img->width || !img->height || img->width > 65535 || img->height > 65535 || stride < (size_t)img->width * 4)
            return XXWIDGETS_INVALID_ARGUMENT;
        copy = malloc((size_t)img->width * img->height * 4);
        if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
        for (unsigned y = 0; y < img->height; ++y) memcpy(copy + (size_t)y * img->width * 4, img->rgba + (size_t)y * stride, (size_t)img->width * 4);
    }
    free(v->rgba);
    v->rgba = copy;
    v->width = img ? img->width : 0;
    v->height = img ? img->height : 0;
    v->centered = 1;
    v->selected = 0;
    v->dirty = 1;
    iv_sync(w);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_imageview_get_image_size(const xxwidgets_widget *w, unsigned *width, unsigned *height)
{
    const xxwidgets_imageview *v = iv_get(w);
    if (!v || !width || !height) return XXWIDGETS_INVALID_ARGUMENT;
    *width = v->width;
    *height = v->height;
    return XXWIDGETS_OK;
}

static double iv_clamp_zoom(double f)
{
    if (!(f == f)) return 1.0; /* NaN */
    return f < IV_MIN_ZOOM ? IV_MIN_ZOOM : f > IV_MAX_ZOOM ? IV_MAX_ZOOM : f;
}

xxwidgets_status xxwidgets_imageview_set_zoom(xxwidgets_widget *w, xxwidgets_image_zoom_mode mode, double factor)
{
    xxwidgets_imageview *v = iv_get(w);
    if (!v || (unsigned)mode > XXWIDGETS_IMAGE_ZOOM) return XXWIDGETS_INVALID_ARGUMENT;
    v->mode = mode;
    if (mode == XXWIDGETS_IMAGE_ZOOM) v->factor = iv_clamp_zoom(factor);
    if (mode != XXWIDGETS_IMAGE_ZOOM && mode != XXWIDGETS_IMAGE_ACTUAL) v->centered = 1;
    v->dirty = 1;
    iv_sync(w);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_imageview_get_zoom(const xxwidgets_widget *w, xxwidgets_image_zoom_mode *mode, double *factor)
{
    const xxwidgets_imageview *v = iv_get(w);
    if (!v) return XXWIDGETS_INVALID_ARGUMENT;
    if (mode) *mode = v->mode;
    if (factor) *factor = v->mode == XXWIDGETS_IMAGE_ZOOM ? v->factor : v->mode == XXWIDGETS_IMAGE_ACTUAL ? 1.0 : v->effective;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_imageview_zoom_by(xxwidgets_widget *w, int steps)
{
    xxwidgets_imageview *v = iv_get(w);
    double f;
    if (!v) return XXWIDGETS_INVALID_ARGUMENT;
    f = v->mode == XXWIDGETS_IMAGE_ZOOM ? v->factor : v->mode == XXWIDGETS_IMAGE_ACTUAL ? 1.0 : v->effective;
    f *= pow(steps > 0 ? 1.25 : 0.8, abs(steps));
    /* snap to 100 % when passing it */
    if (fabs(f - 1.0) < 0.06) f = 1.0;
    return xxwidgets_imageview_set_zoom(w, XXWIDGETS_IMAGE_ZOOM, f);
}

xxwidgets_status xxwidgets_imageview_set_center(xxwidgets_widget *w, double x, double y)
{
    xxwidgets_imageview *v = iv_get(w);
    if (!v || !(x == x) || !(y == y)) return XXWIDGETS_INVALID_ARGUMENT;
    v->cx = x;
    v->cy = y;
    v->centered = 0;
    v->dirty = 1;
    iv_sync(w);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_imageview_get_center(const xxwidgets_widget *w, double *x, double *y)
{
    const xxwidgets_imageview *v = iv_get(w);
    if (!v || !x || !y) return XXWIDGETS_INVALID_ARGUMENT;
    *x = v->centered ? v->width / 2.0 : v->cx;
    *y = v->centered ? v->height / 2.0 : v->cy;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_imageview_set_background(xxwidgets_widget *w, xxwidgets_image_background mode, uint32_t rgba, uint32_t outside)
{
    xxwidgets_imageview *v = iv_get(w);
    if (!v || (unsigned)mode > XXWIDGETS_IMAGE_BACKGROUND_COLOR) return XXWIDGETS_INVALID_ARGUMENT;
    v->background = mode;
    v->back_rgba = rgba | 0xFFU;
    v->outside_rgba = outside | 0xFFU;
    v->dirty = 1;
    iv_sync(w);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_imageview_set_smooth(xxwidgets_widget *w, int smooth)
{
    xxwidgets_imageview *v = iv_get(w);
    if (!v) return XXWIDGETS_INVALID_ARGUMENT;
    v->smooth = smooth != 0;
    v->dirty = 1;
    iv_sync(w);
    return XXWIDGETS_OK;
}

/* ---- layout ------------------------------------------------------------- */

static void iv_layout(xxwidgets_imageview *v, double W, double H)
{
    double s, dw, dh;
    double a = v->aspect > 0 ? v->aspect : 1.0;
    if (!v->width || !v->height || W <= 0 || H <= 0) {
        v->effective = 1.0;
        v->off_x = v->off_y = 0;
        return;
    }
    switch (v->mode) {
        case XXWIDGETS_IMAGE_FIT: s = fmin(W / v->width, H * a / v->height); break;
        case XXWIDGETS_IMAGE_FILL: s = fmax(W / v->width, H * a / v->height); break;
        case XXWIDGETS_IMAGE_ACTUAL: s = 1.0; break;
        case XXWIDGETS_IMAGE_ZOOM: s = v->factor; break;
        default: s = fmin(1.0, fmin(W / v->width, H * a / v->height)); break;
    }
    if (!(s > 0)) s = 1.0;
    v->effective = s;
    dw = v->width * s;
    dh = v->height * s / a;
    if (v->centered) {
        v->cx = v->width / 2.0;
        v->cy = v->height / 2.0;
    }
    /* horizontal */
    if (dw <= W) v->off_x = (W - dw) / 2;
    else {
        v->off_x = W / 2 - v->cx * s;
        if (v->off_x > 0) v->off_x = 0;
        if (v->off_x < W - dw) v->off_x = W - dw;
    }
    if (dh <= H) v->off_y = (H - dh) / 2;
    else {
        v->off_y = H / 2 - v->cy * s / a;
        if (v->off_y > 0) v->off_y = 0;
        if (v->off_y < H - dh) v->off_y = H - dh;
    }
    if (!v->centered) {
        v->cx = (W / 2 - v->off_x) / s;
        v->cy = (H / 2 - v->off_y) * a / s;
    }
}

/* Image coordinates of a widget point; false outside the image. */
static int iv_map(const xxwidgets_imageview *v, double x, double y, unsigned *ix, unsigned *iy)
{
    double s = v->effective, a = v->aspect > 0 ? v->aspect : 1.0, fx, fy;
    if (!v->rgba || !v->have_layout || s <= 0) return 0;
    fx = (x - v->off_x) / s;
    fy = (y - v->off_y) * a / s;
    if (fx < 0 || fy < 0 || fx >= v->width || fy >= v->height) return 0;
    *ix = (unsigned)fx;
    *iy = (unsigned)fy;
    return 1;
}

static uint32_t iv_back(const xxwidgets_imageview *v, unsigned bx, unsigned by)
{
    if (v->background == XXWIDGETS_IMAGE_BACKGROUND_COLOR) return v->back_rgba;
    return (((bx >> 3) + (by >> 3)) & 1) ? 0xCCCCCCFFU : 0xFFFFFFFFU;
}

/* Renders the visible region into the canvas buffer. */
static void iv_render(xxwidgets_widget *w, xxwidgets_imageview *v, double W, double H)
{
    unsigned bw = (unsigned)ceil(W), bh = (unsigned)ceil(H);
    double a = v->aspect > 0 ? v->aspect : 1.0, s = v->effective;
    uint32_t *buf;
    unsigned *col0 = NULL, *col1 = NULL;
    if (bw < 1) bw = 1;
    if (bh < 1) bh = 1;
    if (bw > IV_MAX_BUFFER) bw = IV_MAX_BUFFER;
    if (bh > IV_MAX_BUFFER) bh = IV_MAX_BUFFER;
    if (w->canvas_pixels && w->canvas_width == bw && w->canvas_height == bh) buf = w->canvas_pixels;
    else {
        buf = malloc((size_t)bw * bh * sizeof(*buf));
        if (!buf) return;
        free(w->canvas_pixels);
        w->canvas_pixels = buf;
        w->canvas_width = bw;
        w->canvas_height = bh;
    }
    if (v->rgba) {
        col0 = malloc((size_t)bw * sizeof(*col0));
        col1 = malloc((size_t)bw * sizeof(*col1));
        if (!col0 || !col1) {
            free(col0);
            free(col1);
            col0 = col1 = NULL;
        }
    }
    /* source column span of every buffer column; col0 > col1 marks outside */
    if (col0) {
        for (unsigned bx = 0; bx < bw; ++bx) {
            double u0 = (double)bx * W / bw, u1 = (double)(bx + 1) * W / bw;
            double x0 = (u0 - v->off_x) / s, x1 = (u1 - v->off_x) / s, xc = ((u0 + u1) / 2 - v->off_x) / s;
            if (xc < 0 || xc >= v->width) {
                col0[bx] = 1;
                col1[bx] = 0;
                continue;
            }
            if (v->smooth && x1 - x0 > 1.0) {
                long i0 = (long)floor(x0), i1 = (long)ceil(x1) - 1;
                if (i0 < 0) i0 = 0;
                if (i1 >= (long)v->width) i1 = (long)v->width - 1;
                if (i1 < i0) i1 = i0;
                col0[bx] = (unsigned)i0;
                col1[bx] = (unsigned)i1;
            } else
                col0[bx] = col1[bx] = (unsigned)xc;
        }
    }
    for (unsigned by = 0; by < bh; ++by) {
        double t0 = (double)by * H / bh, t1 = (double)(by + 1) * H / bh;
        double y0 = (t0 - v->off_y) * a / s, y1 = (t1 - v->off_y) * a / s, yc = ((t0 + t1) / 2 - v->off_y) * a / s;
        long r0 = -1, r1 = -2;
        uint32_t *out = buf + (size_t)by * bw;
        if (col0 && yc >= 0 && yc < v->height) {
            if (v->smooth && y1 - y0 > 1.0) {
                r0 = (long)floor(y0);
                r1 = (long)ceil(y1) - 1;
                if (r0 < 0) r0 = 0;
                if (r1 >= (long)v->height) r1 = (long)v->height - 1;
                if (r1 < r0) r1 = r0;
            } else
                r0 = r1 = (long)yc;
        }
        for (unsigned bx = 0; bx < bw; ++bx) {
            uint32_t back;
            uint64_t sr = 0, sg = 0, sb = 0, sa = 0, n = 0;
            if (r0 < 0 || !col0 || col0[bx] > col1[bx]) {
                out[bx] = v->outside_rgba;
                continue;
            }
            for (long r = r0; r <= r1; ++r) {
                const uint8_t *p = v->rgba + ((size_t)r * v->width + col0[bx]) * 4;
                for (unsigned c = col0[bx]; c <= col1[bx]; ++c, p += 4) {
                    sr += (uint64_t)p[0] * p[3];
                    sg += (uint64_t)p[1] * p[3];
                    sb += (uint64_t)p[2] * p[3];
                    sa += p[3];
                    ++n;
                }
            }
            back = iv_back(v, bx, by);
            {
                uint64_t full = 255 * n, rest = full - sa;
                uint32_t R = (uint32_t)((sr + (uint64_t)(back >> 24) * rest + full / 2) / full);
                uint32_t G = (uint32_t)((sg + (uint64_t)((back >> 16) & 255) * rest + full / 2) / full);
                uint32_t B = (uint32_t)((sb + (uint64_t)((back >> 8) & 255) * rest + full / 2) / full);
                out[bx] = (R << 24) | (G << 16) | (B << 8) | 0xFFU;
            }
        }
    }
    free(col0);
    free(col1);
    w->value = -1;
    ++w->canvas_revision;
}

/* Called by backends before painting (and by input) with the widget size in
 * its units.  Re-renders only when the size or the view state changed. */
void xxwidgets_imageview_prepare(xxwidgets_widget *w, double width, double height)
{
    xxwidgets_imageview *v = iv_get(w);
    if (!v || width <= 0 || height <= 0) return;
    if (!v->dirty && v->have_layout && v->lw == width && v->lh == height && w->canvas_pixels) return;
    iv_layout(v, width, height);
    v->lw = width;
    v->lh = height;
    v->have_layout = 1;
    v->dirty = 0;
    iv_render(w, v, width, height);
}

void xxwidgets_imageview_set_cell_aspect(xxwidgets_widget *w, double aspect)
{
    xxwidgets_imageview *v = iv_get(w);
    if (!v || !(aspect > 0) || v->aspect == aspect) return;
    v->aspect = aspect;
    v->dirty = 1;
}

xxwidgets_status xxwidgets_imageview_map_point(const xxwidgets_widget *w, double x, double y, unsigned *ix, unsigned *iy)
{
    const xxwidgets_imageview *v = iv_get(w);
    if (!v || !ix || !iy) return XXWIDGETS_INVALID_ARGUMENT;
    return iv_map(v, x, y, ix, iy) ? XXWIDGETS_OK : XXWIDGETS_UNAVAILABLE;
}

xxwidgets_status xxwidgets_imageview_get_selection(const xxwidgets_widget *w, unsigned *x, unsigned *y)
{
    const xxwidgets_imageview *v = iv_get(w);
    if (!v || !x || !y) return XXWIDGETS_INVALID_ARGUMENT;
    if (!v->selected) return XXWIDGETS_UNAVAILABLE;
    *x = v->sel_x;
    *y = v->sel_y;
    return XXWIDGETS_OK;
}

static int iv_percent(const xxwidgets_imageview *v)
{
    double f = v->mode == XXWIDGETS_IMAGE_ZOOM ? v->factor : v->mode == XXWIDGETS_IMAGE_ACTUAL ? 1.0 : v->effective;
    double p = f * 100.0 + 0.5;
    return p > 1e6 ? 1000000 : (int)p;
}

/* Pointer press / drag at (x, y) in a widget of width x height units. */
int xxwidgets_imageview_input(xxwidgets_widget *w, double x, double y, double width, double height)
{
    xxwidgets_imageview *v = iv_get(w);
    unsigned ix, iy;
    if (!v) return 0;
    xxwidgets_imageview_prepare(w, width, height);
    if (!iv_map(v, x, y, &ix, &iy)) return 1;
    v->selected = 1;
    v->sel_x = ix;
    v->sel_y = iy;
    xxwidgets_emit_point(w, XXWIDGETS_EVENT_SELECT, (int)((size_t)iy * v->width + ix > 0x7FFFFFFF ? 0x7FFFFFFF : (size_t)iy * v->width + ix), (int)ix, (int)iy);
    return 1;
}

/* Wheel steps (positive = zoom in) around the widget point (x, y). */
int xxwidgets_imageview_wheel(xxwidgets_widget *w, int steps, double x, double y, double width, double height)
{
    xxwidgets_imageview *v = iv_get(w);
    double a, fx, fy, s;
    if (!v || !steps) return v != NULL;
    if (!v->rgba) return 1;
    xxwidgets_imageview_prepare(w, width, height);
    a = v->aspect > 0 ? v->aspect : 1.0;
    s = v->effective;
    fx = (x - v->off_x) / s;
    fy = (y - v->off_y) * a / s;
    xxwidgets_imageview_zoom_by(w, steps);
    /* keep the image point under the pointer: new centre so that fx lands at x */
    s = v->factor;
    v->cx = fx - (x - width / 2) / s;
    v->cy = fy - (y - height / 2) * a / s;
    v->centered = 0;
    v->dirty = 1;
    iv_sync(w);
    xxwidgets_emit(w, XXWIDGETS_EVENT_CHANGE, (int)(s * 100 + 0.5));
    return 1;
}

int xxwidgets_imageview_key(xxwidgets_widget *w, int key)
{
    xxwidgets_imageview *v = iv_get(w);
    double step_x, step_y;
    if (!v) return 0;
    if (key == 13 || key == XXWIDGETS_KEY_ENTER) {
        xxwidgets_emit(w, XXWIDGETS_EVENT_ACTIVATE, v->selected ? (int)(v->sel_y * v->width + v->sel_x) : -1);
        return 1;
    }
    if (!v->rgba) return 0;
    if (key == XXWIDGETS_KEY_PAGEUP) xxwidgets_imageview_zoom_by(w, 1);
    else if (key == XXWIDGETS_KEY_PAGEDOWN) xxwidgets_imageview_zoom_by(w, -1);
    else if (key == XXWIDGETS_KEY_HOME) xxwidgets_imageview_set_zoom(w, XXWIDGETS_IMAGE_FIT_DOWN, 1.0);
    else if (key == XXWIDGETS_KEY_END) xxwidgets_imageview_set_zoom(w, XXWIDGETS_IMAGE_ACTUAL, 1.0);
    else if (key == XXWIDGETS_KEY_LEFT || key == XXWIDGETS_KEY_RIGHT || key == XXWIDGETS_KEY_UP || key == XXWIDGETS_KEY_DOWN) {
        double a = v->aspect > 0 ? v->aspect : 1.0;
        if (!v->have_layout || v->effective <= 0) return 1;
        /* a tenth of the visible area per press */
        step_x = v->lw / 10 / v->effective;
        step_y = v->lh / 10 * a / v->effective;
        if (v->centered) {
            v->cx = v->width / 2.0;
            v->cy = v->height / 2.0;
        }
        if (v->mode != XXWIDGETS_IMAGE_ZOOM && v->mode != XXWIDGETS_IMAGE_ACTUAL) {
            /* panning a fitted image: freeze the current factor first */
            v->factor = v->effective;
            v->mode = XXWIDGETS_IMAGE_ZOOM;
        }
        if (key == XXWIDGETS_KEY_LEFT) v->cx -= step_x;
        else if (key == XXWIDGETS_KEY_RIGHT) v->cx += step_x;
        else if (key == XXWIDGETS_KEY_UP) v->cy -= step_y;
        else v->cy += step_y;
        v->centered = 0;
        v->dirty = 1;
        iv_sync(w);
    } else
        return 0;
    xxwidgets_emit(w, XXWIDGETS_EVENT_CHANGE, iv_percent(v));
    return 1;
}
