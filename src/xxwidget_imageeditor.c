/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets/xxwidget_imageeditor.h"
#include "xxwidgets/xxwidgets_combobox.h"
#include "xxwidgets/xxwidgets_imageview.h"
#include "xxvis/xxvis.h"
#include "xxfclib/formats/image/xx_image_codec.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EDITOR_HISTORY_DEFAULT 32u
#define EDITOR_HISTORY_MIN 1u
#define EDITOR_HISTORY_MAX 1024u
#define EDITOR_PIXEL_BUDGET_BYTES (1024ULL * 1024ULL * 1024ULL)
#define EDITOR_SCATTER_MAX_POINTS 4096u
#define EDITOR_HEXBIN_MAX_POINTS 20000u
#define EDITOR_MESSAGE_CAP 256

struct xxwidget_imageeditor {
    xxwidgets_widget *owner;
    xxwidgets_widget *controls[XXWIDGET_IMAGEEDITOR_CONTROL_COUNT];
    xxwidgets_widget *param_labels[4];
    xxwidgets_widget *text_label;
    xxwidgets_rect bounds;
    xxwidget_imageeditor_op selected_op;
    xx_image image;
    xx_image original;
    int has_image;
    size_t frame;
    xx_image *undo_stack;
    size_t undo_count;
    size_t undo_capacity;
    xx_image *redo_stack;
    size_t redo_count;
    size_t redo_capacity;
    size_t history_limit;
    uint64_t revision;
    char last_message[EDITOR_MESSAGE_CAP];
};

#define C(ed, id) ((ed)->controls[XXWIDGET_IMAGEEDITOR_CONTROL_##id])

static const xxwidget_imageeditor_op_info g_op_table[XXWIDGET_IMAGEEDITOR_OP_COUNT] = {
    {
        "rotate",
        "Rotate",
        1,
        {{"turns", -3.0, 3.0, 1.0}},
        0
    },
    {
        "flip",
        "Flip",
        1,
        {{"horiz", 0.0, 1.0, 1.0}},
        0
    },
    {
        "crop",
        "Crop",
        4,
        {{"x", 0.0, 65535.0, 0.0}, {"y", 0.0, 65535.0, 0.0}, {"width", 1.0, 65535.0, 32.0}, {"height", 1.0, 65535.0, 20.0}},
        0
    },
    {
        "resize",
        "Resize",
        3,
        {{"width", 1.0, 65535.0, 640.0}, {"height", 0.0, 65535.0, 0.0}, {"method", 0.0, (double)XXVIS_RESAMPLE_BOX, (double)XXVIS_RESAMPLE_BILINEAR}},
        0
    },
    {
        "contain",
        "Contain",
        4,
        {{"width", 1.0, 65535.0, 640.0}, {"height", 1.0, 65535.0, 480.0}, {"method", 0.0, (double)XXVIS_RESAMPLE_BOX, (double)XXVIS_RESAMPLE_BILINEAR}, {"bg", -1.0, 16777215.0, -1.0}},
        0
    },
    {
        "cover",
        "Cover",
        3,
        {{"width", 1.0, 65535.0, 640.0}, {"height", 1.0, 65535.0, 480.0}, {"method", 0.0, (double)XXVIS_RESAMPLE_BOX, (double)XXVIS_RESAMPLE_BILINEAR}},
        0
    },
    {
        "blur",
        "Blur",
        1,
        {{"sigma", 0.0, 100.0, 2.0}},
        0
    },
    {
        "sharpen",
        "Sharpen",
        3,
        {{"sigma", 0.0, 100.0, 1.0}, {"amount", 0.0, 500.0, 100.0}, {"thresh", 0.0, 255.0, 0.0}},
        0
    },
    {
        "dither",
        "Dither",
        2,
        {{"method", 0.0, (double)XXVIS_DITHER_BAYER8, (double)XXVIS_DITHER_FLOYD_STEINBERG}, {"colors", 2.0, 256.0, 16.0}},
        0
    },
    {
        "invert",
        "Invert",
        0,
        {{0}},
        0
    },
    {
        "brightness_contrast",
        "Brightness / contrast",
        2,
        {{"bright", -100.0, 100.0, 0.0}, {"contr", -100.0, 100.0, 0.0}},
        0
    },
    {
        "opacity",
        "Opacity",
        1,
        {{"pct", 0.0, 100.0, 50.0}},
        0
    },
    {
        "filter",
        "Filter",
        1,
        {{"filter", 0.0, (double)(XXVIS_FILTER_COUNT - 1), 0.0}},
        0
    },
    {
        "hsl",
        "HSL",
        3,
        {{"hue", -180.0, 180.0, 0.0}, {"sat", -100.0, 100.0, 0.0}, {"light", -100.0, 100.0, 0.0}},
        0
    },
    {
        "normalise",
        "Normalise",
        0,
        {{0}},
        0
    },
    {
        "auto_orient",
        "Auto orient",
        0,
        {{0}},
        0
    },
    {
        "text",
        "Text",
        4,
        {{"x", -65535.0, 65535.0, 4.0}, {"y", -65535.0, 65535.0, 4.0}, {"scale", 1.0, 16.0, 1.0}, {"color", 0.0, 16777215.0, 16777215.0}},
        1
    },
    {
        "chart_histogram",
        "Histogram",
        2,
        {{"width", 64.0, 4096.0, 640.0}, {"height", 64.0, 4096.0, 480.0}},
        0
    },
    {
        "chart_scatter",
        "Scatter",
        2,
        {{"width", 64.0, 4096.0, 640.0}, {"height", 64.0, 4096.0, 480.0}},
        0
    },
    {
        "chart_heatmap",
        "Heatmap",
        4,
        {{"width", 64.0, 4096.0, 640.0}, {"height", 64.0, 4096.0, 480.0}, {"cols", 1.0, 512.0, 32.0}, {"rows", 1.0, 512.0, 24.0}},
        0
    },
    {
        "chart_hexbin",
        "Hexbin",
        2,
        {{"width", 64.0, 4096.0, 640.0}, {"height", 64.0, 4096.0, 480.0}},
        0
    }
};

XXWIDGETS_API const xxwidget_imageeditor_op_info *xxwidget_imageeditor_op_info_get(xxwidget_imageeditor_op op)
{
    if ((int)op < 0 || (int)op >= XXWIDGET_IMAGEEDITOR_OP_COUNT) return NULL;
    return &g_op_table[(int)op];
}

XXWIDGETS_API xxwidget_imageeditor_op xxwidget_imageeditor_op_find(const char *id)
{
    if (!id) return XXWIDGET_IMAGEEDITOR_OP_COUNT;
    for (int i = 0; i < XXWIDGET_IMAGEEDITOR_OP_COUNT; ++i) {
        if (strcmp(g_op_table[i].id, id) == 0) return (xxwidget_imageeditor_op)i;
    }
    return XXWIDGET_IMAGEEDITOR_OP_COUNT;
}

static int bounds_valid(xxwidgets_rect b)
{
    return b.x >= 0 && b.y >= 0 && b.width >= 20 && b.height >= 6 && b.x <= 32767 - b.width && b.y <= 32767 - b.height;
}

static xxwidgets_status map_image_status(xx_image_status st)
{
    switch (st) {
    case XX_IMAGE_OK: return XXWIDGETS_OK;
    case XX_IMAGE_ERR_ARGUMENT: return XXWIDGETS_INVALID_ARGUMENT;
    case XX_IMAGE_ERR_MEMORY: return XXWIDGETS_OUT_OF_MEMORY;
    default: return XXWIDGETS_PLATFORM_ERROR;
    }
}

static void set_message(xxwidget_imageeditor *ed, const char *fmt, const char *a, const char *b)
{
    if (!ed) return;
    if (b) snprintf(ed->last_message, sizeof(ed->last_message), fmt, a, b);
    else if (a) snprintf(ed->last_message, sizeof(ed->last_message), fmt, a);
    else snprintf(ed->last_message, sizeof(ed->last_message), "%s", fmt);
}

static uint64_t image_pixel_bytes(const xx_image *img)
{
    uint64_t total = 0;
    if (!img || !img->frames) return 0;
    for (size_t i = 0; i < img->frame_count; ++i) {
        uint64_t fbytes = (uint64_t)img->frames[i].width * (uint64_t)img->frames[i].height * 4ULL;
        if (fbytes > UINT64_MAX - total) return UINT64_MAX;
        total += fbytes;
    }
    return total;
}

static uint64_t stack_pixel_bytes(const xx_image *stack, size_t count)
{
    uint64_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        uint64_t b = image_pixel_bytes(&stack[i]);
        if (b > UINT64_MAX - total) return UINT64_MAX;
        total += b;
    }
    return total;
}

static uint64_t total_history_bytes(const xxwidget_imageeditor *ed)
{
    uint64_t u = stack_pixel_bytes(ed->undo_stack, ed->undo_count);
    uint64_t r = stack_pixel_bytes(ed->redo_stack, ed->redo_count);
    return (r > UINT64_MAX - u) ? UINT64_MAX : u + r;
}

static void clear_stack(xx_image *stack, size_t *count)
{
    for (size_t i = 0; i < *count; ++i) xx_image_free(&stack[i]);
    *count = 0;
}

static void drop_oldest_undo(xxwidget_imageeditor *ed, size_t n)
{
    if (!n || n > ed->undo_count) n = ed->undo_count;
    for (size_t i = 0; i < n; ++i) xx_image_free(&ed->undo_stack[i]);
    if (n < ed->undo_count) {
        memmove(ed->undo_stack, ed->undo_stack + n, (ed->undo_count - n) * sizeof(*ed->undo_stack));
    }
    ed->undo_count -= n;
}

static void enforce_history_limits(xxwidget_imageeditor *ed)
{
    while (ed->undo_count > ed->history_limit) drop_oldest_undo(ed, 1);
    while (ed->undo_count > 0 && total_history_bytes(ed) > EDITOR_PIXEL_BUDGET_BYTES) {
        drop_oldest_undo(ed, 1);
    }
    while (ed->redo_count > 0 && total_history_bytes(ed) > EDITOR_PIXEL_BUDGET_BYTES) {
        xx_image_free(&ed->redo_stack[0]);
        if (ed->redo_count > 1) {
            memmove(ed->redo_stack, ed->redo_stack + 1, (ed->redo_count - 1) * sizeof(*ed->redo_stack));
        }
        --ed->redo_count;
    }
}

static int stack_push(xx_image **stack, size_t *count, size_t *capacity, const xx_image *img)
{
    if (*count == *capacity) {
        size_t new_cap = *capacity ? *capacity * 2u : 16u;
        xx_image *grown;
        if (new_cap > SIZE_MAX / sizeof(*grown)) return 0;
        grown = (xx_image *)realloc(*stack, new_cap * sizeof(*grown));
        if (!grown) return 0;
        *stack = grown;
        *capacity = new_cap;
    }
    (*stack)[*count] = *img;
    ++(*count);
    return 1;
}

static int push_undo(xxwidget_imageeditor *ed, const xx_image *img)
{
    if (!stack_push(&ed->undo_stack, &ed->undo_count, &ed->undo_capacity, img)) return 0;
    enforce_history_limits(ed);
    return 1;
}

static void refresh_buttons(xxwidget_imageeditor *ed)
{
    (void)xxwidgets_widget_set_enabled(C(ed, UNDO), xxwidget_imageeditor_can_undo(ed));
    (void)xxwidgets_widget_set_enabled(C(ed, REDO), xxwidget_imageeditor_can_redo(ed));
    (void)xxwidgets_widget_set_enabled(C(ed, RESET), ed->has_image ? 1 : 0);
}

static void refresh_status(xxwidget_imageeditor *ed)
{
    char text[512];
    if (!ed->has_image) {
        if (ed->last_message[0]) snprintf(text, sizeof(text), "No image | %s", ed->last_message);
        else snprintf(text, sizeof(text), "No image");
    } else {
        double zoom = 1.0;
        unsigned zoom_pct;
        const char *unit = ed->image.frame_count == 1 ? "frame" : "frames";
        if (xxwidgets_imageview_get_zoom(C(ed, VIEW), NULL, &zoom) != XXWIDGETS_OK || !(zoom > 0.0)) zoom = 1.0;
        zoom_pct = (unsigned)llround(zoom * 100.0);
        if (ed->last_message[0]) {
            snprintf(text, sizeof(text), "%u x %u, %zu %s, zoom %u%% | %s",
                     ed->image.width, ed->image.height, ed->image.frame_count, unit, zoom_pct, ed->last_message);
        } else {
            snprintf(text, sizeof(text), "%u x %u, %zu %s, zoom %u%%",
                     ed->image.width, ed->image.height, ed->image.frame_count, unit, zoom_pct);
        }
    }
    (void)xxwidgets_widget_set_text(C(ed, STATUS), text);
}

static void refresh_view(xxwidget_imageeditor *ed, int reset_zoom)
{
    if (reset_zoom) {
        (void)xxwidgets_imageview_set_zoom(C(ed, VIEW), XXWIDGETS_IMAGE_FIT_DOWN, 1.0);
    }
    if (!ed->has_image || !ed->image.frame_count) {
        (void)xxwidgets_imageview_set_image(C(ed, VIEW), NULL);
        return;
    }
    if (ed->frame >= ed->image.frame_count) ed->frame = 0;
    if (ed->image.width > 0 && ed->image.height > 0 && ed->image.width <= 65535u && ed->image.height <= 65535u) {
        uint8_t *rgba = xxvis_compose_frame(&ed->image, ed->frame);
        if (rgba) {
            xxwidgets_image view_img;
            view_img.width = ed->image.width;
            view_img.height = ed->image.height;
            view_img.stride = 0;
            view_img.rgba = rgba;
            (void)xxwidgets_imageview_set_image(C(ed, VIEW), &view_img);
            free(rgba);
        }
    } else {
        (void)xxwidgets_imageview_set_image(C(ed, VIEW), NULL);
    }
}

static xxwidgets_status populate_operations(xxwidgets_widget *combo)
{
    xx_meta_string records[XXWIDGET_IMAGEEDITOR_OP_COUNT];
    xx_str_w_s labels[XXWIDGET_IMAGEEDITOR_OP_COUNT];
    wchar_t wide[XXWIDGET_IMAGEEDITOR_OP_COUNT][64];
    memset(records, 0, sizeof(records));
    memset(labels, 0, sizeof(labels));
    memset(wide, 0, sizeof(wide));
    for (size_t i = 0; i < XXWIDGET_IMAGEEDITOR_OP_COUNT; ++i) {
        const xxwidget_imageeditor_op_info *info = xxwidget_imageeditor_op_info_get((xxwidget_imageeditor_op)i);
        size_t len = strlen(info->title);
        if (len >= 64) len = 63;
        for (size_t j = 0; j < len; ++j) wide[i][j] = (wchar_t)(unsigned char)info->title[j];
        labels[i].data = wide[i];
        labels[i].length = len;
        labels[i].capacity = 64;
        labels[i].is_view = true;
        records[i].meta_string = &labels[i];
        records[i].var.type = XX_VAR_TYPE_UINT64;
        records[i].var.val.u64 = (uint64_t)i;
    }
    return xxwidgets_combobox_set_records(combo, records, XXWIDGET_IMAGEEDITOR_OP_COUNT);
}

static int valid_image_input(const xx_image *img)
{
    if (!img || !img->frame_count || !img->frames || !img->width || !img->height) return 0;
    for (size_t i = 0; i < img->frame_count; ++i) {
        const xx_image_frame *f = &img->frames[i];
        if (!f->width || !f->height || !f->rgba) return 0;
    }
    return 1;
}

static size_t sample_opaque_pixels(const uint8_t *rgba, uint32_t w, uint32_t h, size_t max_samples,
                                   double *xs, double *ys, int mode_hexbin)
{
    size_t total = (size_t)w * (size_t)h;
    size_t opaque = 0;
    size_t target_count;
    size_t seen = 0;
    size_t taken = 0;
    size_t next_idx = 0;

    for (size_t i = 0; i < total; ++i) {
        if (rgba[i * 4u + 3u] > 0) ++opaque;
    }
    if (opaque == 0) return 0;
    target_count = opaque < max_samples ? opaque : max_samples;
    for (size_t i = 0; i < total && taken < target_count; ++i) {
        const uint8_t *p = rgba + i * 4u;
        if (p[3] == 0) continue;
        if (seen == next_idx) {
            if (mode_hexbin) {
                double r = (double)p[0] / 255.0;
                double g = (double)p[1] / 255.0;
                double b = (double)p[2] / 255.0;
                double mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
                double mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
                double d = mx - mn;
                double l = 0.5 * (mx + mn);
                double h_deg = 0.0;
                if (d > 0.0) {
                    if (mx == r) h_deg = 60.0 * ((g - b) / d + (g < b ? 6.0 : 0.0));
                    else if (mx == g) h_deg = 60.0 * ((b - r) / d + 2.0);
                    else h_deg = 60.0 * ((r - g) / d + 4.0);
                }
                xs[taken] = h_deg;
                ys[taken] = l * 100.0;
            } else {
                xs[taken] = (double)p[0];
                ys[taken] = (double)p[1];
            }
            ++taken;
            next_idx = (size_t)(((uint64_t)taken * (uint64_t)opaque) / (uint64_t)target_count);
        }
        ++seen;
    }
    return taken;
}

static xxwidgets_status run_chart_op(const xx_image *src, xxwidget_imageeditor_op op, const double *params, xx_image *out)
{
    uint32_t cw = (uint32_t)llround(params[0]);
    uint32_t ch = (uint32_t)llround(params[1]);
    uint8_t *rgba;
    xxvis_chart_style style;
    xx_image_status st;

    if (!src->frame_count || !src->width || !src->height) return XXWIDGETS_INVALID_ARGUMENT;
    rgba = xxvis_compose_frame(src, 0);
    if (!rgba) return XXWIDGETS_OUT_OF_MEMORY;

    xxvis_chart_style_init(&style);
    style.width = cw;
    style.height = ch;
    xx_image_init(out);

    if (op == XXWIDGET_IMAGEEDITOR_CHART_HISTOGRAM) {
        static const uint32_t hist_colors[4] = {0xD62728FFu, 0x2CA02CFFu, 0x1F77B4FFu, 0x222222FFu};
        static const char *const names[4] = {"red", "green", "blue", "luma"};
        double red[256] = {0}, green[256] = {0}, blue[256] = {0}, luma[256] = {0};
        const double *ys[4];
        size_t total = (size_t)src->width * (size_t)src->height;
        for (size_t i = 0; i < total; ++i) {
            const uint8_t *p = rgba + i * 4u;
            int y;
            if (p[3] == 0) continue;
            red[p[0]] += 1.0;
            green[p[1]] += 1.0;
            blue[p[2]] += 1.0;
            y = (int)llround(0.299 * (double)p[0] + 0.587 * (double)p[1] + 0.114 * (double)p[2]);
            if (y < 0) y = 0;
            if (y > 255) y = 255;
            luma[y] += 1.0;
        }
        ys[0] = red;
        ys[1] = green;
        ys[2] = blue;
        ys[3] = luma;
        style.title = "Histogram";
        style.x_label = "Value";
        style.y_label = "Count";
        style.colors = hist_colors;
        style.color_count = 4;
        st = xxvis_chart_series(out, &style, NULL, ys, 4, 256, names);
    } else if (op == XXWIDGET_IMAGEEDITOR_CHART_SCATTER) {
        double *xs = (double *)malloc(EDITOR_SCATTER_MAX_POINTS * sizeof(double));
        double *ys = (double *)malloc(EDITOR_SCATTER_MAX_POINTS * sizeof(double));
        if (!xs || !ys) {
            free(xs);
            free(ys);
            free(rgba);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        size_t count = sample_opaque_pixels(rgba, src->width, src->height, EDITOR_SCATTER_MAX_POINTS, xs, ys, 0);
        style.title = "Red vs Green";
        style.x_label = "Red";
        style.y_label = "Green";
        st = xxvis_chart_scatter(out, &style, count ? xs : NULL, count ? ys : NULL, count, 0);
        free(xs);
        free(ys);
    } else if (op == XXWIDGET_IMAGEEDITOR_CHART_HEATMAP) {
        size_t cols = (size_t)llround(params[2]);
        size_t rows = (size_t)llround(params[3]);
        size_t cells = cols * rows;
        double *sums = (double *)calloc(cells, sizeof(double));
        size_t *counts = (size_t *)calloc(cells, sizeof(size_t));
        double *values = (double *)malloc(cells * sizeof(double));
        if (!sums || !counts || !values) {
            free(sums);
            free(counts);
            free(values);
            free(rgba);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        for (uint32_t py = 0; py < src->height; ++py) {
            size_t r = (size_t)(((uint64_t)py * (uint64_t)rows) / (uint64_t)src->height);
            if (r >= rows) r = rows - 1u;
            for (uint32_t px = 0; px < src->width; ++px) {
                const uint8_t *p = rgba + ((size_t)py * src->width + px) * 4u;
                size_t c;
                if (p[3] == 0) continue;
                c = (size_t)(((uint64_t)px * (uint64_t)cols) / (uint64_t)src->width);
                if (c >= cols) c = cols - 1u;
                sums[r * cols + c] += 0.299 * (double)p[0] + 0.587 * (double)p[1] + 0.114 * (double)p[2];
                ++counts[r * cols + c];
            }
        }
        for (size_t i = 0; i < cells; ++i) {
            values[i] = counts[i] ? (sums[i] / (double)counts[i]) : (double)NAN;
        }
        style.title = "Luma Heatmap";
        style.x_label = "Column";
        style.y_label = "Row";
        st = xxvis_chart_heatmap(out, &style, values, cols, rows);
        free(sums);
        free(counts);
        free(values);
    } else {
        double *xs = (double *)malloc(EDITOR_HEXBIN_MAX_POINTS * sizeof(double));
        double *ys = (double *)malloc(EDITOR_HEXBIN_MAX_POINTS * sizeof(double));
        if (!xs || !ys) {
            free(xs);
            free(ys);
            free(rgba);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        size_t count = sample_opaque_pixels(rgba, src->width, src->height, EDITOR_HEXBIN_MAX_POINTS, xs, ys, 1);
        style.title = "Hue vs Lightness";
        style.x_label = "Hue";
        style.y_label = "Lightness";
        st = xxvis_chart_hexbin(out, &style, count ? xs : NULL, count ? ys : NULL, count, 0);
        free(xs);
        free(ys);
    }
    free(rgba);
    if (st != XX_IMAGE_OK) {
        xx_image_free(out);
        return map_image_status(st);
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status run_dither_op(xx_image *copy, xxvis_dither_method method, uint32_t colors)
{
    uint32_t palette[256];
    uint32_t qcount;
    uint8_t *indices;
    size_t px;
    const xx_image_frame *f0;
    xx_image_status st;

    if (colors == 2u || !copy->frame_count) {
        return map_image_status(xxvis_dither(copy, NULL, colors, method));
    }
    f0 = &copy->frames[0];
    if ((uint64_t)f0->width * (uint64_t)f0->height > SIZE_MAX) return XXWIDGETS_OUT_OF_MEMORY;
    px = (size_t)f0->width * (size_t)f0->height;
    indices = (uint8_t *)malloc(px ? px : 1u);
    if (!indices) return XXWIDGETS_OUT_OF_MEMORY;
    qcount = xx_image_quantize(f0, colors, indices, palette);
    free(indices);
    if (qcount == 0) return XXWIDGETS_OUT_OF_MEMORY;
    if (qcount == 1) {
        palette[1] = palette[0];
        qcount = 2;
    }
    st = xxvis_dither(copy, palette, qcount, method);
    return map_image_status(st);
}

static xxwidgets_status run_auto_orient_op(xx_image *copy, int *no_orientation)
{
    size_t found_idx = SIZE_MAX;
    int orient = 0;
    xx_image_status st;

    *no_orientation = 0;
    for (size_t i = 0; i < copy->prop_count; ++i) {
        if (copy->props[i].key && strcmp(copy->props[i].key, "Orientation") == 0) {
            found_idx = i;
            break;
        }
    }
    if (found_idx == SIZE_MAX) {
        *no_orientation = 1;
        return XXWIDGETS_OK;
    }
    {
        xx_image_property *p = &copy->props[found_idx];
        if (p->type == XX_IMAGE_PROP_UINT) {
            if (p->v.u < 1 || p->v.u > 8) return XXWIDGETS_INVALID_ARGUMENT;
            orient = (int)p->v.u;
        } else if (p->type == XX_IMAGE_PROP_INT) {
            if (p->v.i < 1 || p->v.i > 8) return XXWIDGETS_INVALID_ARGUMENT;
            orient = (int)p->v.i;
        } else if (p->type == XX_IMAGE_PROP_TEXT) {
            const char *s = p->text ? p->text : "";
            char *end = NULL;
            long v;
            while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') ++s;
            if (!*s) return XXWIDGETS_INVALID_ARGUMENT;
            errno = 0;
            v = strtol(s, &end, 10);
            if (errno == ERANGE || end == s) return XXWIDGETS_INVALID_ARGUMENT;
            while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
            if (*end != '\0' || v < 1 || v > 8) return XXWIDGETS_INVALID_ARGUMENT;
            orient = (int)v;
        } else {
            *no_orientation = 1;
            return XXWIDGETS_OK;
        }
        st = xxvis_apply_orientation(copy, orient);
        if (st != XX_IMAGE_OK) return map_image_status(st);
        if (p->type == XX_IMAGE_PROP_UINT) p->v.u = 1;
        else if (p->type == XX_IMAGE_PROP_INT) p->v.i = 1;
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status read_widget_text_alloc(xxwidgets_widget *w, char **out)
{
    size_t need = 0;
    char *buf;
    xxwidgets_status st = xxwidgets_widget_get_text(w, NULL, 0, &need);
    if (st != XXWIDGETS_OK) return st;
    buf = (char *)malloc(need ? need : 1u);
    if (!buf) return XXWIDGETS_OUT_OF_MEMORY;
    buf[0] = '\0';
    if (need > 0) {
        st = xxwidgets_widget_get_text(w, buf, need, NULL);
        if (st != XXWIDGETS_OK) {
            free(buf);
            return st;
        }
    }
    *out = buf;
    return XXWIDGETS_OK;
}

static int parse_param_text(const char *str, double *out)
{
    const char *p = str;
    char *end = NULL;
    double val;

    if (!p) return 0;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (!*p) return 0;

    if (p[0] == '#') {
        unsigned long long v;
        if (!isxdigit((unsigned char)p[1])) return 0;
        errno = 0;
        v = strtoull(p + 1, &end, 16);
        if (errno != 0 || (end - (p + 1)) != 6) return 0;
        while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
        if (*end != '\0') return 0;
        *out = (double)v;
        return 1;
    }

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        unsigned long long v;
        if (!isxdigit((unsigned char)p[2])) return 0;
        errno = 0;
        v = strtoull(p + 2, &end, 16);
        if (errno != 0 || end == p + 2) return 0;
        while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
        if (*end != '\0') return 0;
        *out = (double)v;
        return 1;
    }

    errno = 0;
    val = strtod(p, &end);
    if (end == p || !isfinite(val) || errno == ERANGE) return 0;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    if (*end != '\0') return 0;
    *out = val;
    return 1;
}

typedef struct editor_layout_group {
    int width;
    int split_label;
} editor_layout_group;

XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_set_rect(xxwidget_imageeditor *ed, xxwidgets_rect b)
{
    /* Order matches control creation / Tab order. */
    editor_layout_group groups[10];
    xxwidgets_rect r_controls[XXWIDGET_IMAGEEDITOR_CONTROL_COUNT] = {0};
    xxwidgets_rect r_plabels[4] = {0};
    xxwidgets_rect r_tlabel = {0, 0, 1, 1};
    xxwidgets_rect old_controls[XXWIDGET_IMAGEEDITOR_CONTROL_COUNT];
    xxwidgets_rect old_plabels[4];
    xxwidgets_rect old_tlabel;
    int cur_x = 0, row = 0, toolbar_rows, toolbar_bottom, view_y, view_h;
    int text_group_w;
    xxwidgets_status st = XXWIDGETS_OK;

    if (!ed || !bounds_valid(b)) return XXWIDGETS_INVALID_ARGUMENT;

    text_group_w = b.width >= 134 ? 17 : 15;
    if (b.width > 134) {
        int extra = b.width - 134;
        if (extra > 8) extra = 8;
        text_group_w += extra;
    }

    groups[0] = (editor_layout_group){22, 0}; /* Operation */
    groups[1] = (editor_layout_group){14, 6}; /* param0 */
    groups[2] = (editor_layout_group){14, 6}; /* param1 */
    groups[3] = (editor_layout_group){14, 6}; /* param2 */
    groups[4] = (editor_layout_group){14, 6}; /* param3 */
    groups[5] = (editor_layout_group){text_group_w, 5}; /* Text */
    groups[6] = (editor_layout_group){7, 0};  /* Apply */
    groups[7] = (editor_layout_group){6, 0};  /* Undo */
    groups[8] = (editor_layout_group){6, 0};  /* Redo */
    groups[9] = (editor_layout_group){7, 0};  /* Reset */

    /* First pass: determine how many toolbar rows are needed. */
    for (int g = 0; g < 10; ++g) {
        int gw = groups[g].width;
        if (gw > b.width) gw = b.width;
        if (cur_x > 0 && cur_x + gw > b.width) {
            ++row;
            cur_x = 0;
        }
        cur_x += gw + 1;
    }
    toolbar_rows = row + 1;
    if (toolbar_rows > b.height - 3) toolbar_rows = b.height - 3;
    if (toolbar_rows < 1) toolbar_rows = 1;

    /* Second pass: assign rects inside bounds. */
    cur_x = 0;
    row = 0;
    for (int g = 0; g < 10; ++g) {
        int gw = groups[g].width;
        int ry, lw, ew;
        if (gw > b.width) gw = b.width;
        if (cur_x > 0 && cur_x + gw > b.width) {
            ++row;
            cur_x = 0;
        }
        ry = b.y + (row < toolbar_rows ? row : toolbar_rows - 1);
        if (g == 0) {
            r_controls[XXWIDGET_IMAGEEDITOR_CONTROL_OPERATION] = (xxwidgets_rect){b.x + cur_x, ry, gw, 1};
        } else if (g >= 1 && g <= 4) {
            int p = g - 1;
            lw = groups[g].split_label;
            if (lw >= gw) lw = gw > 1 ? gw / 2 : 1;
            ew = gw - lw;
            if (ew < 1) ew = 1;
            r_plabels[p] = (xxwidgets_rect){b.x + cur_x, ry, lw, 1};
            r_controls[XXWIDGET_IMAGEEDITOR_CONTROL_PARAM0 + p] = (xxwidgets_rect){b.x + cur_x + lw, ry, ew, 1};
        } else if (g == 5) {
            lw = groups[g].split_label;
            if (lw >= gw) lw = gw > 1 ? gw / 2 : 1;
            ew = gw - lw;
            if (ew < 1) ew = 1;
            r_tlabel = (xxwidgets_rect){b.x + cur_x, ry, lw, 1};
            r_controls[XXWIDGET_IMAGEEDITOR_CONTROL_TEXT] = (xxwidgets_rect){b.x + cur_x + lw, ry, ew, 1};
        } else {
            int cid = XXWIDGET_IMAGEEDITOR_CONTROL_APPLY + (g - 6);
            r_controls[cid] = (xxwidgets_rect){b.x + cur_x, ry, gw, 1};
        }
        cur_x += gw + 1;
    }

    toolbar_bottom = b.y + toolbar_rows - 1;
    view_y = (b.height >= toolbar_rows + 4) ? toolbar_bottom + 2 : toolbar_bottom + 1;
    view_h = (b.y + b.height - 1) - view_y;
    if (view_h < 1) {
        view_y = b.y + b.height - 2;
        view_h = 1;
    }
    r_controls[XXWIDGET_IMAGEEDITOR_CONTROL_VIEW] = (xxwidgets_rect){b.x, view_y, b.width, view_h};
    r_controls[XXWIDGET_IMAGEEDITOR_CONTROL_STATUS] = (xxwidgets_rect){b.x, b.y + b.height - 1, b.width, 1};

    for (int i = 0; i < XXWIDGET_IMAGEEDITOR_CONTROL_COUNT; ++i) {
        (void)xxwidgets_widget_get_rect(ed->controls[i], &old_controls[i]);
    }
    for (int i = 0; i < 4; ++i) {
        (void)xxwidgets_widget_get_rect(ed->param_labels[i], &old_plabels[i]);
    }
    (void)xxwidgets_widget_get_rect(ed->text_label, &old_tlabel);

    (void)xxwidgets_window_begin_update(ed->owner);
    for (int i = 0; i < XXWIDGET_IMAGEEDITOR_CONTROL_COUNT && st == XXWIDGETS_OK; ++i) {
        st = xxwidgets_widget_set_rect(ed->controls[i], r_controls[i]);
    }
    for (int i = 0; i < 4 && st == XXWIDGETS_OK; ++i) {
        st = xxwidgets_widget_set_rect(ed->param_labels[i], r_plabels[i]);
    }
    if (st == XXWIDGETS_OK) {
        st = xxwidgets_widget_set_rect(ed->text_label, r_tlabel);
    }
    if (st != XXWIDGETS_OK) {
        for (int i = 0; i < XXWIDGET_IMAGEEDITOR_CONTROL_COUNT; ++i) {
            (void)xxwidgets_widget_set_rect(ed->controls[i], old_controls[i]);
        }
        for (int i = 0; i < 4; ++i) {
            (void)xxwidgets_widget_set_rect(ed->param_labels[i], old_plabels[i]);
        }
        (void)xxwidgets_widget_set_rect(ed->text_label, old_tlabel);
        (void)xxwidgets_window_end_update(ed->owner);
        return st;
    }
    (void)xxwidgets_window_end_update(ed->owner);
    ed->bounds = b;
    return XXWIDGETS_OK;
}

XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidget_imageeditor **out)
{
    static const xxwidgets_kind kinds[XXWIDGET_IMAGEEDITOR_CONTROL_COUNT] = {
        XXWIDGETS_COMBOBOX, /* OPERATION */
        XXWIDGETS_EDIT,     /* PARAM0 */
        XXWIDGETS_EDIT,     /* PARAM1 */
        XXWIDGETS_EDIT,     /* PARAM2 */
        XXWIDGETS_EDIT,     /* PARAM3 */
        XXWIDGETS_EDIT,     /* TEXT */
        XXWIDGETS_BUTTON,   /* APPLY */
        XXWIDGETS_BUTTON,   /* UNDO */
        XXWIDGETS_BUTTON,   /* REDO */
        XXWIDGETS_BUTTON,   /* RESET */
        XXWIDGETS_CANVAS,   /* VIEW */
        XXWIDGETS_LABEL     /* STATUS */
    };
    static const char *const init_texts[XXWIDGET_IMAGEEDITOR_CONTROL_COUNT] = {
        "", "", "", "", "", "", "Apply", "Undo", "Redo", "Reset", "", "No image"
    };
    xxwidgets_app *app;
    xxwidget_imageeditor *ed;
    xxwidgets_status st = XXWIDGETS_OK;
    xxwidgets_rect dummy;

    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || xxwidgets_widget_kind(owner) != XXWIDGETS_WINDOW || !bounds_valid(bounds)) {
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    app = xxwidgets_widget_app(owner);
    if (!app) return XXWIDGETS_INVALID_ARGUMENT;

    ed = (xxwidget_imageeditor *)calloc(1, sizeof(*ed));
    if (!ed) return XXWIDGETS_OUT_OF_MEMORY;
    ed->owner = owner;
    ed->bounds = bounds;
    ed->history_limit = EDITOR_HISTORY_DEFAULT;
    ed->selected_op = XXWIDGET_IMAGEEDITOR_ROTATE;
    xx_image_init(&ed->image);
    xx_image_init(&ed->original);

    dummy = (xxwidgets_rect){bounds.x, bounds.y, 1, 1};

    /* Create in visual / Tab order. */
    st = xxwidgets_widget_create(app, owner, kinds[XXWIDGET_IMAGEEDITOR_CONTROL_OPERATION], init_texts[XXWIDGET_IMAGEEDITOR_CONTROL_OPERATION], dummy, &C(ed, OPERATION));
    for (int i = 0; i < 4 && st == XXWIDGETS_OK; ++i) {
        st = xxwidgets_widget_create(app, owner, XXWIDGETS_LABEL, "", dummy, &ed->param_labels[i]);
        if (st == XXWIDGETS_OK) {
            st = xxwidgets_widget_create(app, owner, kinds[XXWIDGET_IMAGEEDITOR_CONTROL_PARAM0 + i], init_texts[XXWIDGET_IMAGEEDITOR_CONTROL_PARAM0 + i], dummy, &ed->controls[XXWIDGET_IMAGEEDITOR_CONTROL_PARAM0 + i]);
        }
    }
    if (st == XXWIDGETS_OK) {
        st = xxwidgets_widget_create(app, owner, XXWIDGETS_LABEL, "Text", dummy, &ed->text_label);
    }
    if (st == XXWIDGETS_OK) {
        st = xxwidgets_widget_create(app, owner, kinds[XXWIDGET_IMAGEEDITOR_CONTROL_TEXT], init_texts[XXWIDGET_IMAGEEDITOR_CONTROL_TEXT], dummy, &C(ed, TEXT));
    }
    for (int id = XXWIDGET_IMAGEEDITOR_CONTROL_APPLY; id < XXWIDGET_IMAGEEDITOR_CONTROL_COUNT && st == XXWIDGETS_OK; ++id) {
        st = xxwidgets_widget_create(app, owner, kinds[id], init_texts[id], dummy, &ed->controls[id]);
    }
    if (st == XXWIDGETS_OK) st = xxwidgets_imageview_attach(C(ed, VIEW));
    if (st == XXWIDGETS_OK) st = populate_operations(C(ed, OPERATION));
    if (st == XXWIDGETS_OK) st = xxwidget_imageeditor_set_rect(ed, bounds);
    if (st == XXWIDGETS_OK) st = xxwidget_imageeditor_select_op(ed, XXWIDGET_IMAGEEDITOR_ROTATE);
    if (st == XXWIDGETS_OK) {
        refresh_buttons(ed);
        refresh_status(ed);
    }
    if (st != XXWIDGETS_OK) {
        (void)xxwidget_imageeditor_destroy(ed);
        return st;
    }
    *out = ed;
    return XXWIDGETS_OK;
}

XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_destroy(xxwidget_imageeditor *ed)
{
    if (!ed) return XXWIDGETS_INVALID_ARGUMENT;
    for (int i = 0; i < XXWIDGET_IMAGEEDITOR_CONTROL_COUNT; ++i) {
        if (ed->controls[i]) {
            xxwidgets_status st = xxwidgets_widget_destroy(ed->controls[i]);
            if (st != XXWIDGETS_OK) return st;
            ed->controls[i] = NULL;
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (ed->param_labels[i]) {
            xxwidgets_status st = xxwidgets_widget_destroy(ed->param_labels[i]);
            if (st != XXWIDGETS_OK) return st;
            ed->param_labels[i] = NULL;
        }
    }
    if (ed->text_label) {
        xxwidgets_status st = xxwidgets_widget_destroy(ed->text_label);
        if (st != XXWIDGETS_OK) return st;
        ed->text_label = NULL;
    }
    clear_stack(ed->undo_stack, &ed->undo_count);
    clear_stack(ed->redo_stack, &ed->redo_count);
    free(ed->undo_stack);
    free(ed->redo_stack);
    xx_image_free(&ed->image);
    xx_image_free(&ed->original);
    free(ed);
    return XXWIDGETS_OK;
}

XXWIDGETS_API xxwidgets_widget *xxwidget_imageeditor_control(const xxwidget_imageeditor *ed, xxwidget_imageeditor_control_id id)
{
    if (!ed || (int)id < 0 || id >= XXWIDGET_IMAGEEDITOR_CONTROL_COUNT) return NULL;
    return ed->controls[id];
}

static int is_own_widget(const xxwidget_imageeditor *ed, const xxwidgets_widget *w)
{
    if (!ed || !w) return 0;
    for (int i = 0; i < XXWIDGET_IMAGEEDITOR_CONTROL_COUNT; ++i) {
        if (ed->controls[i] == w) return 1;
    }
    for (int i = 0; i < 4; ++i) {
        if (ed->param_labels[i] == w) return 1;
    }
    if (ed->text_label == w) return 1;
    return 0;
}

XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_handle_event(xxwidget_imageeditor *ed, const xxwidgets_event *event)
{
    if (!ed || !event) return XXWIDGETS_INVALID_ARGUMENT;
    if (!is_own_widget(ed, event->widget)) return XXWIDGETS_UNAVAILABLE;

    if (event->widget == C(ed, OPERATION) && (event->type == XXWIDGETS_EVENT_SELECT || event->type == XXWIDGETS_EVENT_CHANGE)) {
        int idx = -1, combo_val = -1;
        if (xxwidgets_widget_get_value(C(ed, OPERATION), &combo_val) == XXWIDGETS_OK &&
            combo_val >= 0 && combo_val < XXWIDGET_IMAGEEDITOR_OP_COUNT &&
            combo_val != (int)ed->selected_op) {
            idx = combo_val;
        } else if (event->value >= 0 && event->value < XXWIDGET_IMAGEEDITOR_OP_COUNT) {
            idx = event->value;
        } else {
            idx = combo_val;
        }
        if (idx >= 0 && idx < XXWIDGET_IMAGEEDITOR_OP_COUNT) {
            return xxwidget_imageeditor_select_op(ed, (xxwidget_imageeditor_op)idx);
        }
        return XXWIDGETS_OK;
    }
    if (event->widget == C(ed, APPLY) && event->type == XXWIDGETS_EVENT_CLICK) {
        (void)xxwidget_imageeditor_apply_controls(ed);
        return XXWIDGETS_OK;
    }
    if (event->type == XXWIDGETS_EVENT_ACTIVATE &&
        (event->widget == C(ed, PARAM0) || event->widget == C(ed, PARAM1) ||
         event->widget == C(ed, PARAM2) || event->widget == C(ed, PARAM3) ||
         event->widget == C(ed, TEXT))) {
        (void)xxwidget_imageeditor_apply_controls(ed);
        return XXWIDGETS_OK;
    }
    if (event->widget == C(ed, UNDO) && event->type == XXWIDGETS_EVENT_CLICK) {
        (void)xxwidget_imageeditor_undo(ed);
        return XXWIDGETS_OK;
    }
    if (event->widget == C(ed, REDO) && event->type == XXWIDGETS_EVENT_CLICK) {
        (void)xxwidget_imageeditor_redo(ed);
        return XXWIDGETS_OK;
    }
    if (event->widget == C(ed, RESET) && event->type == XXWIDGETS_EVENT_CLICK) {
        (void)xxwidget_imageeditor_reset(ed);
        return XXWIDGETS_OK;
    }
    if (event->widget == C(ed, VIEW) && event->type == XXWIDGETS_EVENT_CHANGE) {
        refresh_status(ed);
        return XXWIDGETS_OK;
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status install_new_image(xxwidget_imageeditor *ed, const xx_image *image, const char *msg)
{
    xx_image cur_copy, orig_copy;
    xx_image_status st;

    xx_image_init(&cur_copy);
    xx_image_init(&orig_copy);
    if (image) {
        if (!valid_image_input(image)) return XXWIDGETS_INVALID_ARGUMENT;
        st = xx_image_copy(&cur_copy, image);
        if (st != XX_IMAGE_OK) return map_image_status(st);
        st = xx_image_copy(&orig_copy, image);
        if (st != XX_IMAGE_OK) {
            xx_image_free(&cur_copy);
            return map_image_status(st);
        }
    }
    clear_stack(ed->undo_stack, &ed->undo_count);
    clear_stack(ed->redo_stack, &ed->redo_count);
    xx_image_free(&ed->image);
    xx_image_free(&ed->original);
    if (image) {
        ed->image = cur_copy;
        ed->original = orig_copy;
        ed->has_image = 1;
    } else {
        ed->has_image = 0;
    }
    ed->frame = 0;
    ++ed->revision;
    set_message(ed, "%s", msg ? msg : "", NULL);
    refresh_view(ed, 1);
    refresh_buttons(ed);
    refresh_status(ed);
    return XXWIDGETS_OK;
}

XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_set_image(xxwidget_imageeditor *ed, const xx_image *image)
{
    if (!ed) return XXWIDGETS_INVALID_ARGUMENT;
    return install_new_image(ed, image, "");
}

XXWIDGETS_API const xx_image *xxwidget_imageeditor_image(const xxwidget_imageeditor *ed)
{
    if (!ed || !ed->has_image) return NULL;
    return &ed->image;
}

XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_load_file(xxwidget_imageeditor *ed, const char *path)
{
    xx_image loaded;
    xx_image_status st;
    xxwidgets_status res;

    if (!ed || !path || !path[0]) return XXWIDGETS_INVALID_ARGUMENT;
    xx_image_init(&loaded);
    st = xx_image_load_file(path, NULL, NULL, &loaded);
    if (st != XX_IMAGE_OK) {
        xx_image_free(&loaded);
        set_message(ed, "Load: %s", xx_image_status_string(st), NULL);
        refresh_status(ed);
        return map_image_status(st);
    }
    if (!valid_image_input(&loaded)) {
        xx_image_free(&loaded);
        set_message(ed, "Load: %s", "no pixel frames", NULL);
        refresh_status(ed);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    res = install_new_image(ed, &loaded, "Load: ok");
    xx_image_free(&loaded);
    return res;
}

XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_save_file(xxwidget_imageeditor *ed, const char *path, const xx_image_codec *codec)
{
    xx_image_save_options so;
    xx_image_status st;

    if (!ed || !path || !path[0]) return XXWIDGETS_INVALID_ARGUMENT;
    if (!ed->has_image) {
        set_message(ed, "Save: no image", NULL, NULL);
        refresh_status(ed);
        return XXWIDGETS_UNAVAILABLE;
    }
    xx_image_save_options_init(&so);
    so.all_frames = ed->image.frame_count > 1;
    so.frame = ed->frame < ed->image.frame_count ? ed->frame : 0;
    st = xx_image_save_file(&ed->image, path, codec, &so);
    if (st != XX_IMAGE_OK) {
        set_message(ed, "Save: %s", xx_image_status_string(st), NULL);
        refresh_status(ed);
        return map_image_status(st);
    }
    set_message(ed, "Save: ok", NULL, NULL);
    refresh_status(ed);
    return XXWIDGETS_OK;
}

XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_apply(xxwidget_imageeditor *ed, xxwidget_imageeditor_op op, const double *params, size_t count, const char *text)
{
    const xxwidget_imageeditor_op_info *info;
    xx_image next;
    xx_image_status ist = XX_IMAGE_OK;
    xxwidgets_status wst = XXWIDGETS_OK;
    int no_orient = 0;

    if (!ed) return XXWIDGETS_INVALID_ARGUMENT;
    info = xxwidget_imageeditor_op_info_get(op);
    if (!info) {
        set_message(ed, "Invalid operation", NULL, NULL);
        refresh_status(ed);
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    if (count != info->param_count || (count > 0 && !params)) {
        set_message(ed, "%s: invalid argument", info->title, NULL);
        refresh_status(ed);
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < count; ++i) {
        double v = params[i];
        if (!isfinite(v) || v < info->params[i].minimum || v > info->params[i].maximum) {
            set_message(ed, "%s: %s out of range", info->title, info->params[i].name);
            refresh_status(ed);
            return XXWIDGETS_INVALID_ARGUMENT;
        }
    }
    if (info->uses_text && !text) {
        set_message(ed, "%s: invalid argument", info->title, NULL);
        refresh_status(ed);
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    if (!ed->has_image) {
        set_message(ed, "%s: no image", info->title, NULL);
        refresh_status(ed);
        return XXWIDGETS_UNAVAILABLE;
    }

    xx_image_init(&next);
    if (op >= XXWIDGET_IMAGEEDITOR_CHART_HISTOGRAM && op <= XXWIDGET_IMAGEEDITOR_CHART_HEXBIN) {
        wst = run_chart_op(&ed->image, op, params, &next);
        if (wst != XXWIDGETS_OK) {
            set_message(ed, "%s: failed", info->title, NULL);
            refresh_status(ed);
            return wst;
        }
    } else {
        ist = xx_image_copy(&next, &ed->image);
        if (ist != XX_IMAGE_OK) {
            set_message(ed, "%s: %s", info->title, xx_image_status_string(ist));
            refresh_status(ed);
            return map_image_status(ist);
        }
        switch (op) {
        case XXWIDGET_IMAGEEDITOR_ROTATE:
            ist = xxvis_rotate(&next, (int)llround(params[0]));
            wst = map_image_status(ist);
            break;
        case XXWIDGET_IMAGEEDITOR_FLIP:
            ist = xxvis_flip(&next, llround(params[0]) != 0);
            wst = map_image_status(ist);
            break;
        case XXWIDGET_IMAGEEDITOR_CROP:
            ist = xxvis_crop(&next, (uint32_t)llround(params[0]), (uint32_t)llround(params[1]),
                             (uint32_t)llround(params[2]), (uint32_t)llround(params[3]));
            wst = map_image_status(ist);
            break;
        case XXWIDGET_IMAGEEDITOR_RESIZE:
            ist = xxvis_resize(&next, (uint32_t)llround(params[0]), (uint32_t)llround(params[1]),
                               (xxvis_resample)(int)llround(params[2]));
            wst = map_image_status(ist);
            break;
        case XXWIDGET_IMAGEEDITOR_CONTAIN: {
            long long bg_val = llround(params[3]);
            uint32_t bg_rgba = bg_val < 0 ? 0u : (((uint32_t)bg_val << 8) | 0xFFu);
            ist = xxvis_contain(&next, (uint32_t)llround(params[0]), (uint32_t)llround(params[1]),
                                bg_rgba, (xxvis_resample)(int)llround(params[2]));
            wst = map_image_status(ist);
            break;
        }
        case XXWIDGET_IMAGEEDITOR_COVER:
            ist = xxvis_cover(&next, (uint32_t)llround(params[0]), (uint32_t)llround(params[1]),
                              (xxvis_resample)(int)llround(params[2]));
            wst = map_image_status(ist);
            break;
        case XXWIDGET_IMAGEEDITOR_BLUR:
            ist = xxvis_blur(&next, params[0]);
            wst = map_image_status(ist);
            break;
        case XXWIDGET_IMAGEEDITOR_SHARPEN:
            ist = xxvis_sharpen(&next, params[0], (int)llround(params[1]), (int)llround(params[2]));
            wst = map_image_status(ist);
            break;
        case XXWIDGET_IMAGEEDITOR_DITHER:
            wst = run_dither_op(&next, (xxvis_dither_method)(int)llround(params[0]), (uint32_t)llround(params[1]));
            break;
        case XXWIDGET_IMAGEEDITOR_INVERT:
            ist = xxvis_invert(&next);
            wst = map_image_status(ist);
            break;
        case XXWIDGET_IMAGEEDITOR_BRIGHTNESS_CONTRAST:
            ist = xxvis_brightness_contrast(&next, (int)llround(params[0]), (int)llround(params[1]));
            wst = map_image_status(ist);
            break;
        case XXWIDGET_IMAGEEDITOR_OPACITY:
            ist = xxvis_opacity(&next, (int)llround(params[0]));
            wst = map_image_status(ist);
            break;
        case XXWIDGET_IMAGEEDITOR_FILTER:
            ist = xxvis_apply_filter(&next, (xxvis_filter)(int)llround(params[0]));
            wst = map_image_status(ist);
            break;
        case XXWIDGET_IMAGEEDITOR_HSL:
            ist = xxvis_hsl(&next, (int)llround(params[0]), (int)llround(params[1]), (int)llround(params[2]));
            wst = map_image_status(ist);
            break;
        case XXWIDGET_IMAGEEDITOR_NORMALISE:
            ist = xxvis_normalise(&next);
            wst = map_image_status(ist);
            break;
        case XXWIDGET_IMAGEEDITOR_AUTO_ORIENT:
            wst = run_auto_orient_op(&next, &no_orient);
            break;
        case XXWIDGET_IMAGEEDITOR_TEXT: {
            uint32_t rgba = (((uint32_t)llround(params[3])) << 8) | 0xFFu;
            ist = xxvis_draw_text(&next, (int32_t)llround(params[0]), (int32_t)llround(params[1]),
                                  text, rgba, (uint32_t)llround(params[2]));
            wst = map_image_status(ist);
            break;
        }
        default:
            wst = XXWIDGETS_INVALID_ARGUMENT;
            break;
        }
        if (wst != XXWIDGETS_OK) {
            xx_image_free(&next);
            if (ist != XX_IMAGE_OK) set_message(ed, "%s: %s", info->title, xx_image_status_string(ist));
            else set_message(ed, "%s: invalid argument", info->title, NULL);
            refresh_status(ed);
            return wst;
        }
    }

    if (!push_undo(ed, &ed->image)) {
        xx_image_free(&next);
        set_message(ed, "%s: out of memory", info->title, NULL);
        refresh_status(ed);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    clear_stack(ed->redo_stack, &ed->redo_count);
    ed->image = next;
    if (ed->frame >= ed->image.frame_count) ed->frame = 0;
    ++ed->revision;
    if (op == XXWIDGET_IMAGEEDITOR_AUTO_ORIENT && no_orient) {
        set_message(ed, "Auto orient: no orientation", NULL, NULL);
    } else {
        set_message(ed, "%s: ok", info->title, NULL);
    }
    refresh_view(ed, 0);
    refresh_buttons(ed);
    refresh_status(ed);
    return XXWIDGETS_OK;
}

XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_apply_controls(xxwidget_imageeditor *ed)
{
    const xxwidget_imageeditor_op_info *info;
    double values[4] = {0, 0, 0, 0};
    char *text_buf = NULL;
    xxwidgets_status st;

    if (!ed) return XXWIDGETS_INVALID_ARGUMENT;
    info = xxwidget_imageeditor_op_info_get(ed->selected_op);
    if (!info) return XXWIDGETS_INVALID_ARGUMENT;

    for (size_t i = 0; i < info->param_count; ++i) {
        char *field_str = NULL;
        st = read_widget_text_alloc(ed->controls[XXWIDGET_IMAGEEDITOR_CONTROL_PARAM0 + i], &field_str);
        if (st != XXWIDGETS_OK) return st;
        if (!parse_param_text(field_str, &values[i])) {
            free(field_str);
            set_message(ed, "%s: invalid %s", info->title, info->params[i].name);
            refresh_status(ed);
            return XXWIDGETS_INVALID_ARGUMENT;
        }
        free(field_str);
    }
    if (info->uses_text) {
        st = read_widget_text_alloc(C(ed, TEXT), &text_buf);
        if (st != XXWIDGETS_OK) return st;
    }
    st = xxwidget_imageeditor_apply(ed, ed->selected_op, info->param_count ? values : NULL, info->param_count, text_buf);
    free(text_buf);
    return st;
}

XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_select_op(xxwidget_imageeditor *ed, xxwidget_imageeditor_op op)
{
    const xxwidget_imageeditor_op_info *info;
    xxwidgets_status st;

    if (!ed) return XXWIDGETS_INVALID_ARGUMENT;
    info = xxwidget_imageeditor_op_info_get(op);
    if (!info) return XXWIDGETS_INVALID_ARGUMENT;

    st = xxwidgets_widget_set_value(C(ed, OPERATION), (int)op);
    if (st != XXWIDGETS_OK) return st;
    ed->selected_op = op;

    (void)xxwidgets_window_begin_update(ed->owner);
    for (size_t i = 0; i < 4; ++i) {
        xxwidgets_widget *lbl = ed->param_labels[i];
        xxwidgets_widget *edit = ed->controls[XXWIDGET_IMAGEEDITOR_CONTROL_PARAM0 + i];
        if (i < info->param_count) {
            char buf[64];
            double init_val = info->params[i].initial;
            if (init_val == 0.0) init_val = 0.0;
            snprintf(buf, sizeof(buf), "%.15g", init_val);
            if (strcmp(buf, "-0") == 0) snprintf(buf, sizeof(buf), "0");
            (void)xxwidgets_widget_set_text(lbl, info->params[i].name);
            (void)xxwidgets_widget_set_text(edit, buf);
            (void)xxwidgets_widget_set_visible(lbl, 1);
            (void)xxwidgets_widget_set_visible(edit, 1);
        } else {
            (void)xxwidgets_widget_set_text(lbl, "");
            (void)xxwidgets_widget_set_text(edit, "");
            (void)xxwidgets_widget_set_visible(lbl, 0);
            (void)xxwidgets_widget_set_visible(edit, 0);
        }
    }
    if (!info->uses_text) {
        (void)xxwidgets_widget_set_text(C(ed, TEXT), "");
    }
    (void)xxwidgets_widget_set_visible(ed->text_label, info->uses_text ? 1 : 0);
    (void)xxwidgets_widget_set_visible(C(ed, TEXT), info->uses_text ? 1 : 0);
    (void)xxwidgets_window_end_update(ed->owner);
    return XXWIDGETS_OK;
}

XXWIDGETS_API xxwidget_imageeditor_op xxwidget_imageeditor_selected_op(const xxwidget_imageeditor *ed)
{
    if (!ed) return XXWIDGET_IMAGEEDITOR_OP_COUNT;
    return ed->selected_op;
}

XXWIDGETS_API int xxwidget_imageeditor_can_undo(const xxwidget_imageeditor *ed)
{
    return ed && ed->undo_count > 0 ? 1 : 0;
}

XXWIDGETS_API int xxwidget_imageeditor_can_redo(const xxwidget_imageeditor *ed)
{
    return ed && ed->redo_count > 0 ? 1 : 0;
}

XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_undo(xxwidget_imageeditor *ed)
{
    xx_image restored;
    if (!ed) return XXWIDGETS_INVALID_ARGUMENT;
    if (!ed->undo_count) return XXWIDGETS_UNAVAILABLE;
    if (!stack_push(&ed->redo_stack, &ed->redo_count, &ed->redo_capacity, &ed->image)) {
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    restored = ed->undo_stack[ed->undo_count - 1u];
    --ed->undo_count;
    ed->image = restored;
    enforce_history_limits(ed);
    if (ed->frame >= ed->image.frame_count) ed->frame = 0;
    ++ed->revision;
    set_message(ed, "Undo: ok", NULL, NULL);
    refresh_view(ed, 0);
    refresh_buttons(ed);
    refresh_status(ed);
    return XXWIDGETS_OK;
}

XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_redo(xxwidget_imageeditor *ed)
{
    xx_image restored;
    if (!ed) return XXWIDGETS_INVALID_ARGUMENT;
    if (!ed->redo_count) return XXWIDGETS_UNAVAILABLE;
    if (!stack_push(&ed->undo_stack, &ed->undo_count, &ed->undo_capacity, &ed->image)) {
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    restored = ed->redo_stack[ed->redo_count - 1u];
    --ed->redo_count;
    ed->image = restored;
    enforce_history_limits(ed);
    if (ed->frame >= ed->image.frame_count) ed->frame = 0;
    ++ed->revision;
    set_message(ed, "Redo: ok", NULL, NULL);
    refresh_view(ed, 0);
    refresh_buttons(ed);
    refresh_status(ed);
    return XXWIDGETS_OK;
}

XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_reset(xxwidget_imageeditor *ed)
{
    xx_image copy;
    xx_image_status st;
    if (!ed) return XXWIDGETS_INVALID_ARGUMENT;
    if (!ed->has_image) return XXWIDGETS_UNAVAILABLE;
    xx_image_init(&copy);
    st = xx_image_copy(&copy, &ed->original);
    if (st != XX_IMAGE_OK) return map_image_status(st);
    if (!push_undo(ed, &ed->image)) {
        xx_image_free(&copy);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    clear_stack(ed->redo_stack, &ed->redo_count);
    ed->image = copy;
    if (ed->frame >= ed->image.frame_count) ed->frame = 0;
    ++ed->revision;
    set_message(ed, "Reset: ok", NULL, NULL);
    refresh_view(ed, 0);
    refresh_buttons(ed);
    refresh_status(ed);
    return XXWIDGETS_OK;
}

XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_set_history_limit(xxwidget_imageeditor *ed, size_t steps)
{
    if (!ed || steps < EDITOR_HISTORY_MIN || steps > EDITOR_HISTORY_MAX) {
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    ed->history_limit = steps;
    enforce_history_limits(ed);
    refresh_buttons(ed);
    return XXWIDGETS_OK;
}

XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_set_frame(xxwidget_imageeditor *ed, size_t frame)
{
    if (!ed) return XXWIDGETS_INVALID_ARGUMENT;
    if (!ed->has_image) return XXWIDGETS_UNAVAILABLE;
    if (frame >= ed->image.frame_count) return XXWIDGETS_INVALID_ARGUMENT;
    ed->frame = frame;
    refresh_view(ed, 0);
    refresh_status(ed);
    return XXWIDGETS_OK;
}

XXWIDGETS_API uint64_t xxwidget_imageeditor_revision(const xxwidget_imageeditor *ed)
{
    return ed ? ed->revision : 0;
}

XXWIDGETS_API const char *xxwidget_imageeditor_last_message(const xxwidget_imageeditor *ed)
{
    return ed ? ed->last_message : "";
}
