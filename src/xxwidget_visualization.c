/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets/xxwidget_visualization.h"
#include "xxwidgets/xxwidgets_combobox.h"
#include "xxwidgets/xxwidgets_imageview.h"
#include "xxvis/xxvis.h"
#include "xxfclib/formats/image/xx_image.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define VIS_BASE_RGB 0xF0F0F0u
#define VIS_DEFAULT_BLOCK_PX 3u
#define VIS_MAX_BLOCK_PX 32u
#define VIS_MAX_GRID 4096u
#define VIS_SCHEMA_MIN_COLUMN 150u
#define VIS_SCHEMA_MARGIN 8u
#define VIS_SCHEMA_ROW 14u
#define VIS_SCHEMA_SWATCH 10u
#define VIS_SCHEMA_TEXT_GAP 4u
#define VIS_MAX_IMAGE_BYTES (1024ULL * 1024ULL * 1024ULL)
#define VIS_VIEW_MAX_SIDE 65535u
#define VIS_LABEL_COUNT 3
#define VIS_MAP_MODE_MAX 16

struct xxwidget_visualization {
    xxwidgets_widget *owner;
    xxwidgets_widget *controls[XXWIDGET_VISUALIZATION_CONTROL_COUNT];
    xxwidgets_widget *labels[VIS_LABEL_COUNT]; /* "Block", "Width", "Height" */
    xxwidgets_rect bounds;

    xx_io_device *device;
    int owns_device;
    xx_file_type_t file_type;  /* as given (UNKNOWN = detect) */
    xx_file_type_t shown_type; /* detected / given type shown in the Type label */
    int64_t file_size;

    xx_visualization data;
    int has_data;

    /* settings */
    uint32_t width;
    uint32_t height;
    uint32_t methods;
    xx_memory_map_mode_t map_mode;
    xx_visualization_method method;
    int method_chosen;
    uint32_t block_px;
    int schema;
    size_t marked;

    xxwidget_visualization_location_fn location_fn;
    void *location_user;
};

#define C(vis, id) ((vis)->controls[XXWIDGET_VISUALIZATION_CONTROL_##id])

/* ---- small helpers ------------------------------------------------------ */

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

/* UTF-8 to wchar_t (UTF-16 with surrogates where wchar_t is 16 bits).
 * Invalid sequences become '?'.  @p out must hold strlen(s) + 1 units. */
static size_t utf8_to_wide(const char *s, wchar_t *out)
{
    const unsigned char *p = (const unsigned char *)s;
    size_t n = 0;
    while (*p) {
        uint32_t cp;
        int extra;
        if (*p < 0x80) {
            cp = *p++;
            extra = 0;
        } else if ((*p & 0xE0) == 0xC0) {
            cp = *p++ & 0x1Fu;
            extra = 1;
        } else if ((*p & 0xF0) == 0xE0) {
            cp = *p++ & 0x0Fu;
            extra = 2;
        } else if ((*p & 0xF8) == 0xF0) {
            cp = *p++ & 0x07u;
            extra = 3;
        } else {
            ++p;
            out[n++] = L'?';
            continue;
        }
        {
            int ok = 1;
            for (int i = 0; i < extra; ++i) {
                if ((*p & 0xC0) != 0x80) {
                    ok = 0;
                    break;
                }
                cp = (cp << 6) | (uint32_t)(*p++ & 0x3Fu);
            }
            if (!ok || cp == 0 || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu) || (extra == 1 && cp < 0x80u) ||
                (extra == 2 && cp < 0x800u) || (extra == 3 && cp < 0x10000u)) {
                out[n++] = L'?';
                continue;
            }
        }
        if (sizeof(wchar_t) == 2 && cp >= 0x10000u) {
            cp -= 0x10000u;
            out[n++] = (wchar_t)(0xD800u + (cp >> 10));
            out[n++] = (wchar_t)(0xDC00u + (cp & 0x3FFu));
        } else {
            out[n++] = (wchar_t)cp;
        }
    }
    out[n] = 0;
    return n;
}

/* Replaces the records of a COMBOBOX / CHECKCOMBOBOX with @p names and the
 * values @p values (u64). */
static xxwidgets_status set_combo_records(xxwidgets_widget *combo, const char *const *names, const uint64_t *values, size_t count)
{
    xx_meta_string *records;
    xx_str_w_s *labels;
    wchar_t **wide;
    xxwidgets_status st = XXWIDGETS_OK;

    if (!count) return xxwidgets_combobox_set_records(combo, NULL, 0);
    if (count > SIZE_MAX / sizeof(*records) || count > SIZE_MAX / sizeof(*labels) || count > SIZE_MAX / sizeof(*wide)) {
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    records = (xx_meta_string *)calloc(count, sizeof(*records));
    labels = (xx_str_w_s *)calloc(count, sizeof(*labels));
    wide = (wchar_t **)calloc(count, sizeof(*wide));
    if (!records || !labels || !wide) {
        st = XXWIDGETS_OUT_OF_MEMORY;
        goto done;
    }
    for (size_t i = 0; i < count; ++i) {
        const char *name = (names[i] && names[i][0]) ? names[i] : "-";
        size_t len = strlen(name);
        size_t wlen;
        wide[i] = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
        if (!wide[i]) {
            st = XXWIDGETS_OUT_OF_MEMORY;
            goto done;
        }
        wlen = utf8_to_wide(name, wide[i]);
        labels[i].data = wide[i];
        labels[i].length = wlen;
        labels[i].capacity = len + 1;
        labels[i].is_view = true;
        records[i].meta_string = &labels[i];
        records[i].var.type = XX_VAR_TYPE_UINT64;
        records[i].var.val.u64 = values ? values[i] : (uint64_t)i;
    }
    st = xxwidgets_combobox_set_records(combo, records, count);
done:
    if (wide) {
        for (size_t i = 0; i < count; ++i) free(wide[i]);
    }
    free(wide);
    free(labels);
    free(records);
    return st;
}

static int combo_current_u64(xxwidgets_widget *combo, uint64_t *value)
{
    int idx = -1;
    const xx_meta_string *rec = NULL;
    if (xxwidgets_widget_get_value(combo, &idx) != XXWIDGETS_OK || idx < 0) return 0;
    if (xxwidgets_combobox_get_record(combo, (size_t)idx, &rec) != XXWIDGETS_OK || !rec) return 0;
    if (rec->var.type != XX_VAR_TYPE_UINT64) return 0;
    *value = rec->var.val.u64;
    return 1;
}

static int combo_find_u64(const xxwidgets_widget *combo, uint64_t value)
{
    size_t n = xxwidgets_combobox_count(combo);
    for (size_t i = 0; i < n && i <= (size_t)INT_MAX; ++i) {
        const xx_meta_string *rec = NULL;
        if (xxwidgets_combobox_get_record(combo, i, &rec) == XXWIDGETS_OK && rec && rec->var.type == XX_VAR_TYPE_UINT64 &&
            rec->var.val.u64 == value) {
            return (int)i;
        }
    }
    return -1;
}

static void set_edit_u32(xxwidgets_widget *edit, uint32_t value)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%u", (unsigned)value);
    (void)xxwidgets_widget_set_text(edit, buf);
}

static int read_edit_u32(xxwidgets_widget *edit, uint32_t *value)
{
    char buf[32];
    size_t need = 0;
    const char *p;
    uint64_t v = 0;
    int digits = 0;
    if (xxwidgets_widget_get_text(edit, buf, sizeof(buf), &need) != XXWIDGETS_OK || need > sizeof(buf)) return 0;
    p = buf;
    while (*p == ' ' || *p == '\t') ++p;
    while (*p >= '0' && *p <= '9') {
        v = v * 10u + (uint64_t)(*p - '0');
        if (v > UINT32_MAX) return 0;
        ++p;
        ++digits;
    }
    while (*p == ' ' || *p == '\t') ++p;
    if (!digits || *p) return 0;
    *value = (uint32_t)v;
    return 1;
}

/* ---- device / data lifetime ---------------------------------------------- */

static void release_device(xxwidget_visualization *vis)
{
    if (vis->device && vis->owns_device) (void)xx_io_close(vis->device);
    vis->device = NULL;
    vis->owns_device = 0;
    vis->file_size = 0;
    vis->file_type = XX_FILE_TYPE_UNKNOWN;
    vis->shown_type = XX_FILE_TYPE_UNKNOWN;
}

static void free_data(xxwidget_visualization *vis)
{
    xx_visualization_free(&vis->data);
    vis->has_data = 0;
    vis->marked = SIZE_MAX;
}

/* ---- schema layout and rendering ---------------------------------------- */

typedef struct vis_layout {
    uint32_t grid_w, grid_h; /* grid in pixels */
    uint32_t left_w, right_w; /* schema columns (0 without schema) */
    uint32_t width, height;   /* whole image */
} vis_layout;

static uint32_t column_width(const xx_visualization_area *areas, size_t count, size_t *enabled)
{
    uint32_t w = VIS_SCHEMA_MIN_COLUMN;
    size_t n = 0;
    for (size_t i = 0; i < count; ++i) {
        uint32_t tw = 0, th = 0, need;
        if (!areas[i].enabled) continue;
        ++n;
        xxvis_text_size(areas[i].name[0] ? areas[i].name : "-", 1, &tw, &th);
        if (tw > 65535u) tw = 65535u;
        need = VIS_SCHEMA_MARGIN + VIS_SCHEMA_SWATCH + VIS_SCHEMA_TEXT_GAP + tw + VIS_SCHEMA_MARGIN;
        if (need > w) w = need;
    }
    *enabled = n;
    return w;
}

static xxwidgets_status compute_layout(const xxwidget_visualization *vis, vis_layout *lay)
{
    uint64_t gw, gh, w, h, bytes;
    memset(lay, 0, sizeof(*lay));
    if (!vis->has_data || !vis->data.width || !vis->data.height) return XXWIDGETS_UNAVAILABLE;
    gw = (uint64_t)vis->data.width * vis->block_px;
    gh = (uint64_t)vis->data.height * vis->block_px;
    w = gw;
    h = gh;
    if (vis->schema) {
        size_t nr = 0, nh = 0, rows;
        uint64_t rows_h;
        lay->left_w = column_width(vis->data.regions, vis->data.region_count, &nr);
        lay->right_w = column_width(vis->data.highlights, vis->data.highlight_count, &nh);
        rows = nr > nh ? nr : nh;
        rows_h = rows ? 2ull * VIS_SCHEMA_MARGIN + (uint64_t)rows * VIS_SCHEMA_ROW : 0;
        w = (uint64_t)lay->left_w + gw + lay->right_w;
        if (rows_h > h) h = rows_h;
    }
    if (w > UINT32_MAX || h > UINT32_MAX) return XXWIDGETS_OUT_OF_MEMORY;
    bytes = w * h * 4ull;
    if (bytes > VIS_MAX_IMAGE_BYTES || bytes > (uint64_t)SIZE_MAX) return XXWIDGETS_OUT_OF_MEMORY;
    lay->grid_w = (uint32_t)gw;
    lay->grid_h = (uint32_t)gh;
    lay->width = (uint32_t)w;
    lay->height = (uint32_t)h;
    return XXWIDGETS_OK;
}

static void fill_rect(uint8_t *rgba, uint32_t stride_px, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb)
{
    for (uint32_t yy = y; yy < y + h; ++yy) {
        uint8_t *row = rgba + ((size_t)yy * stride_px + x) * 4u;
        for (uint32_t xx = 0; xx < w; ++xx) {
            row[xx * 4u + 0] = (uint8_t)((rgb >> 16) & 0xFFu);
            row[xx * 4u + 1] = (uint8_t)((rgb >> 8) & 0xFFu);
            row[xx * 4u + 2] = (uint8_t)(rgb & 0xFFu);
            row[xx * 4u + 3] = 0xFFu;
        }
    }
}

static xx_image_status draw_legend(xx_image *img, uint32_t x0, const xx_visualization_area *areas, size_t count)
{
    uint32_t row = 0;
    for (size_t i = 0; i < count; ++i) {
        uint32_t y;
        xx_image_status st;
        if (!areas[i].enabled) continue;
        y = VIS_SCHEMA_MARGIN + row * VIS_SCHEMA_ROW;
        if (y + VIS_SCHEMA_SWATCH > img->height) break;
        fill_rect(img->frames[0].rgba, img->width, x0 + VIS_SCHEMA_MARGIN, y, VIS_SCHEMA_SWATCH, VIS_SCHEMA_SWATCH, areas[i].color & 0xFFFFFFu);
        st = xxvis_draw_text(img, (int32_t)(x0 + VIS_SCHEMA_MARGIN + VIS_SCHEMA_SWATCH + VIS_SCHEMA_TEXT_GAP), (int32_t)(y + 1u),
                             areas[i].name[0] ? areas[i].name : "-", 0x000000FFu, 1);
        if (st != XX_IMAGE_OK) return st;
        ++row;
    }
    return XX_IMAGE_OK;
}

/* Renders the current view into a new malloc'ed RGBA buffer. */
static xxwidgets_status render_current(const xxwidget_visualization *vis, uint8_t **out, uint32_t *out_w, uint32_t *out_h)
{
    vis_layout lay;
    uint8_t *grid;
    xxwidgets_status st = compute_layout(vis, &lay);
    if (st != XXWIDGETS_OK) return st;

    grid = (uint8_t *)malloc((size_t)lay.grid_w * lay.grid_h * 4u);
    if (!grid) return XXWIDGETS_OUT_OF_MEMORY;
    if (!xx_visualization_render(&vis->data, vis->method, VIS_BASE_RGB, vis->block_px, vis->marked, grid)) {
        free(grid);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    if (!vis->schema) {
        *out = grid;
        *out_w = lay.grid_w;
        *out_h = lay.grid_h;
        return XXWIDGETS_OK;
    }
    {
        xx_image img;
        xx_image_status ist;
        uint8_t *buf;
        size_t bytes = (size_t)lay.width * lay.height * 4u;
        xx_image_init(&img);
        ist = xx_image_create(&img, lay.width, lay.height);
        if (ist != XX_IMAGE_OK || !img.frame_count || !img.frames || !img.frames[0].rgba) {
            free(grid);
            xx_image_free(&img);
            return ist != XX_IMAGE_OK ? map_image_status(ist) : XXWIDGETS_OUT_OF_MEMORY;
        }
        fill_rect(img.frames[0].rgba, lay.width, 0, 0, lay.width, lay.height, VIS_BASE_RGB);
        for (uint32_t y = 0; y < lay.grid_h; ++y) {
            memcpy(img.frames[0].rgba + ((size_t)y * lay.width + lay.left_w) * 4u, grid + (size_t)y * lay.grid_w * 4u, (size_t)lay.grid_w * 4u);
        }
        free(grid);
        ist = draw_legend(&img, 0, vis->data.regions, vis->data.region_count);
        if (ist == XX_IMAGE_OK) ist = draw_legend(&img, lay.left_w + lay.grid_w, vis->data.highlights, vis->data.highlight_count);
        if (ist != XX_IMAGE_OK) {
            xx_image_free(&img);
            return map_image_status(ist);
        }
        buf = (uint8_t *)malloc(bytes);
        if (!buf) {
            xx_image_free(&img);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        memcpy(buf, img.frames[0].rgba, bytes);
        xx_image_free(&img);
        *out = buf;
        *out_w = lay.width;
        *out_h = lay.height;
    }
    return XXWIDGETS_OK;
}

/* ---- refresh ------------------------------------------------------------ */

static void refresh_type(xxwidget_visualization *vis)
{
    char text[96];
    if (!vis->device) snprintf(text, sizeof(text), "Type:");
    else snprintf(text, sizeof(text), "Type: %s", xx_format_file_type_to_string(vis->has_data ? vis->data.detected_type : vis->shown_type));
    (void)xxwidgets_widget_set_text(C(vis, TYPE), text);
}

static void refresh_status(xxwidget_visualization *vis)
{
    char text[256];
    char bsize[48];
    int n;
    if (!vis->device) {
        (void)xxwidgets_widget_set_text(C(vis, STATUS), "No data");
        return;
    }
    if (!vis->has_data) {
        snprintf(text, sizeof(text), "%lld bytes | not processed", (long long)vis->file_size);
        (void)xxwidgets_widget_set_text(C(vis, STATUS), text);
        return;
    }
    if (vis->data.block_size == (double)(long long)vis->data.block_size) snprintf(bsize, sizeof(bsize), "%lld", (long long)vis->data.block_size);
    else snprintf(bsize, sizeof(bsize), "%.2f", vis->data.block_size);
    n = snprintf(text, sizeof(text), "%lld bytes, %u x %u blocks of %s bytes", (long long)vis->data.file_size, (unsigned)vis->data.width,
                 (unsigned)vis->data.height, bsize);
    if (n > 0 && (size_t)n < sizeof(text) && vis->marked < vis->data.block_count && vis->data.blocks) {
        snprintf(text + n, sizeof(text) - (size_t)n, " | block %zu at 0x%llX", vis->marked,
                 (unsigned long long)vis->data.blocks[vis->marked].offset);
    }
    (void)xxwidgets_widget_set_text(C(vis, STATUS), text);
}

static xxwidgets_status redraw(xxwidget_visualization *vis)
{
    uint8_t *rgba = NULL;
    uint32_t w = 0, h = 0;
    xxwidgets_status st;
    xxwidgets_image image;
    unsigned int old_w = 0, old_h = 0;
    double cx = 0.0, cy = 0.0;
    int keep_center;

    if (!vis->has_data) {
        (void)xxwidgets_imageview_set_image(C(vis, VIEW), NULL);
        return XXWIDGETS_OK;
    }
    st = render_current(vis, &rgba, &w, &h);
    if (st != XXWIDGETS_OK) {
        (void)xxwidgets_imageview_set_image(C(vis, VIEW), NULL);
        return st;
    }
    if (w > VIS_VIEW_MAX_SIDE || h > VIS_VIEW_MAX_SIDE) {
        /* Too large for the view; render() / save_image() still work. */
        free(rgba);
        (void)xxwidgets_imageview_set_image(C(vis, VIEW), NULL);
        return XXWIDGETS_OK;
    }
    keep_center = xxwidgets_imageview_get_image_size(C(vis, VIEW), &old_w, &old_h) == XXWIDGETS_OK && old_w == w && old_h == h &&
                  xxwidgets_imageview_get_center(C(vis, VIEW), &cx, &cy) == XXWIDGETS_OK;
    image.width = w;
    image.height = h;
    image.stride = 0;
    image.rgba = rgba;
    st = xxwidgets_imageview_set_image(C(vis, VIEW), &image);
    free(rgba);
    if (st == XXWIDGETS_OK && keep_center) (void)xxwidgets_imageview_set_center(C(vis, VIEW), cx, cy);
    return st;
}

static xxwidgets_status fill_area_combo(xxwidgets_widget *combo, const xx_visualization_area *areas, size_t count)
{
    const char **names = NULL;
    xxwidgets_status st;
    if (count) {
        names = (const char **)calloc(count, sizeof(*names));
        if (!names) return XXWIDGETS_OUT_OF_MEMORY;
        for (size_t i = 0; i < count; ++i) names[i] = areas[i].name;
    }
    st = set_combo_records(combo, names, NULL, count);
    free(names);
    for (size_t i = 0; i < count && st == XXWIDGETS_OK; ++i) {
        st = xxwidgets_checkcombobox_set_checked(combo, i, areas[i].enabled ? 1 : 0);
    }
    (void)xxwidgets_widget_set_enabled(combo, count ? 1 : 0);
    return st;
}

/* Method combo: the computed methods in enum order; index 1 when there are
 * more than one (the first real method), as XVisualizationWidget::reload. */
static xxwidgets_status fill_method_combo(xxwidget_visualization *vis, uint32_t flags)
{
    const char *names[XX_VISUALIZATION_METHOD_COUNT];
    uint64_t values[XX_VISUALIZATION_METHOD_COUNT];
    size_t n = 0;
    int idx;
    xxwidgets_status st;
    for (int m = 0; m < XX_VISUALIZATION_METHOD_COUNT; ++m) {
        if (flags & XX_VISUALIZATION_FLAG(m)) {
            names[n] = xx_visualization_method_name((xx_visualization_method)m);
            values[n] = (uint64_t)m;
            ++n;
        }
    }
    st = set_combo_records(C(vis, METHOD), names, values, n);
    if (st != XXWIDGETS_OK) return st;
    if (!n) return XXWIDGETS_OK;
    idx = vis->method_chosen ? combo_find_u64(C(vis, METHOD), (uint64_t)vis->method) : -1;
    if (idx < 0) idx = n > 1 ? 1 : 0;
    st = xxwidgets_widget_set_value(C(vis, METHOD), idx);
    vis->method = (xx_visualization_method)values[idx];
    return st;
}

static xxwidgets_status fill_methods_checks(xxwidget_visualization *vis)
{
    xxwidgets_status st = XXWIDGETS_OK;
    for (int m = 0; m < XX_VISUALIZATION_METHOD_COUNT && st == XXWIDGETS_OK; ++m) {
        st = xxwidgets_checkcombobox_set_checked(C(vis, METHODS), (size_t)m, (vis->methods & XX_VISUALIZATION_FLAG(m)) ? 1 : 0);
    }
    return st;
}

static xxwidgets_status fill_map_modes(xxwidget_visualization *vis)
{
    xx_memory_map_mode_t modes[VIS_MAP_MODE_MAX];
    const char *names[VIS_MAP_MODE_MAX];
    uint64_t values[VIS_MAP_MODE_MAX];
    size_t n = 0;
    if (vis->device) n = xx_visualization_map_modes(vis->device, vis->shown_type, modes, VIS_MAP_MODE_MAX);
    if (n > VIS_MAP_MODE_MAX) n = VIS_MAP_MODE_MAX;
    if (!n && vis->device) {
        modes[0] = XX_MEMORY_MAP_MODE_UNKNOWN;
        n = 1;
    }
    for (size_t i = 0; i < n; ++i) {
        names[i] = xx_visualization_map_mode_name(modes[i]);
        values[i] = (uint64_t)modes[i];
    }
    return set_combo_records(C(vis, MAP_MODE), names, values, n);
}

/* ---- layout ------------------------------------------------------------- */

typedef struct vis_layout_item {
    int control;  /* control id, or -1 */
    int label;    /* label index, or -1 */
    int width;    /* total cells */
    int label_w;  /* cells of the label part */
    int new_row;  /* starts a new row */
} vis_layout_item;

static const vis_layout_item g_items[] = {
    {XXWIDGET_VISUALIZATION_CONTROL_TYPE, -1, 18, 0, 0},
    {XXWIDGET_VISUALIZATION_CONTROL_MAP_MODE, -1, 14, 0, 0},
    {XXWIDGET_VISUALIZATION_CONTROL_METHODS, -1, 12, 0, 0},
    {XXWIDGET_VISUALIZATION_CONTROL_METHOD, -1, 18, 0, 0},
    {XXWIDGET_VISUALIZATION_CONTROL_BLOCK_SIZE, 0, 10, 6, 0},
    {XXWIDGET_VISUALIZATION_CONTROL_WIDTH, 1, 12, 6, 0},
    {XXWIDGET_VISUALIZATION_CONTROL_HEIGHT, 2, 13, 7, 0},
    {XXWIDGET_VISUALIZATION_CONTROL_SCHEMA, -1, 10, 0, 0},
    {XXWIDGET_VISUALIZATION_CONTROL_RELOAD, -1, 8, 0, 0},
    {XXWIDGET_VISUALIZATION_CONTROL_SAVE, -1, 6, 0, 0},
    {XXWIDGET_VISUALIZATION_CONTROL_REGIONS, -1, 22, 0, 1},
    {XXWIDGET_VISUALIZATION_CONTROL_HIGHLIGHTS, -1, 22, 0, 0},
};
#define VIS_ITEM_COUNT ((int)(sizeof(g_items) / sizeof(g_items[0])))

XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_rect(xxwidget_visualization *vis, xxwidgets_rect b)
{
    xxwidgets_rect r_controls[XXWIDGET_VISUALIZATION_CONTROL_COUNT];
    xxwidgets_rect r_labels[VIS_LABEL_COUNT];
    xxwidgets_rect old_controls[XXWIDGET_VISUALIZATION_CONTROL_COUNT];
    xxwidgets_rect old_labels[VIS_LABEL_COUNT];
    int cur_x = 0, row = 0, rows, max_rows, view_y, view_h;
    xxwidgets_status st = XXWIDGETS_OK;

    if (!vis || !bounds_valid(b)) return XXWIDGETS_INVALID_ARGUMENT;
    memset(r_controls, 0, sizeof(r_controls));
    memset(r_labels, 0, sizeof(r_labels));

    /* First pass: rows needed. */
    for (int i = 0; i < VIS_ITEM_COUNT; ++i) {
        int w = g_items[i].width > b.width ? b.width : g_items[i].width;
        if (cur_x > 0 && (g_items[i].new_row || cur_x + w > b.width)) {
            ++row;
            cur_x = 0;
        }
        cur_x += w + 1;
    }
    rows = row + 1;
    max_rows = b.height - 2; /* keep a view row and the status row */
    if (rows > max_rows) rows = max_rows;
    if (rows < 1) rows = 1;

    /* Second pass: rects. */
    cur_x = 0;
    row = 0;
    for (int i = 0; i < VIS_ITEM_COUNT; ++i) {
        const vis_layout_item *it = &g_items[i];
        int w = it->width > b.width ? b.width : it->width;
        int ry;
        if (cur_x > 0 && (it->new_row || cur_x + w > b.width)) {
            ++row;
            cur_x = 0;
        }
        ry = b.y + (row < rows ? row : rows - 1);
        if (it->label >= 0) {
            int lw = it->label_w;
            int ew;
            if (lw >= w) lw = w > 1 ? w / 2 : 1;
            ew = w - lw;
            if (ew < 1) ew = 1;
            r_labels[it->label] = (xxwidgets_rect){b.x + cur_x, ry, lw, 1};
            r_controls[it->control] = (xxwidgets_rect){b.x + cur_x + lw, ry, ew, 1};
        } else {
            r_controls[it->control] = (xxwidgets_rect){b.x + cur_x, ry, w, 1};
        }
        cur_x += w + 1;
    }
    view_y = b.y + rows;
    view_h = b.height - rows - 1;
    if (view_h < 1) {
        view_y = b.y + b.height - 2;
        view_h = 1;
    }
    r_controls[XXWIDGET_VISUALIZATION_CONTROL_VIEW] = (xxwidgets_rect){b.x, view_y, b.width, view_h};
    r_controls[XXWIDGET_VISUALIZATION_CONTROL_STATUS] = (xxwidgets_rect){b.x, b.y + b.height - 1, b.width, 1};

    for (int i = 0; i < XXWIDGET_VISUALIZATION_CONTROL_COUNT; ++i) (void)xxwidgets_widget_get_rect(vis->controls[i], &old_controls[i]);
    for (int i = 0; i < VIS_LABEL_COUNT; ++i) (void)xxwidgets_widget_get_rect(vis->labels[i], &old_labels[i]);

    (void)xxwidgets_window_begin_update(vis->owner);
    for (int i = 0; i < XXWIDGET_VISUALIZATION_CONTROL_COUNT && st == XXWIDGETS_OK; ++i) st = xxwidgets_widget_set_rect(vis->controls[i], r_controls[i]);
    for (int i = 0; i < VIS_LABEL_COUNT && st == XXWIDGETS_OK; ++i) st = xxwidgets_widget_set_rect(vis->labels[i], r_labels[i]);
    if (st != XXWIDGETS_OK) {
        for (int i = 0; i < XXWIDGET_VISUALIZATION_CONTROL_COUNT; ++i) (void)xxwidgets_widget_set_rect(vis->controls[i], old_controls[i]);
        for (int i = 0; i < VIS_LABEL_COUNT; ++i) (void)xxwidgets_widget_set_rect(vis->labels[i], old_labels[i]);
        (void)xxwidgets_window_end_update(vis->owner);
        return st;
    }
    (void)xxwidgets_window_end_update(vis->owner);
    vis->bounds = b;
    return XXWIDGETS_OK;
}

/* ---- create / destroy --------------------------------------------------- */

XXWIDGETS_API xxwidgets_status xxwidget_visualization_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidget_visualization **out)
{
    static const xxwidgets_kind kinds[XXWIDGET_VISUALIZATION_CONTROL_COUNT] = {
        XXWIDGETS_LABEL,         /* TYPE */
        XXWIDGETS_COMBOBOX,      /* MAP_MODE */
        XXWIDGETS_CHECKCOMBOBOX, /* METHODS */
        XXWIDGETS_COMBOBOX,      /* METHOD */
        XXWIDGETS_EDIT,          /* BLOCK_SIZE */
        XXWIDGETS_EDIT,          /* WIDTH */
        XXWIDGETS_EDIT,          /* HEIGHT */
        XXWIDGETS_CHECKBOX,      /* SCHEMA */
        XXWIDGETS_BUTTON,        /* RELOAD */
        XXWIDGETS_BUTTON,        /* SAVE */
        XXWIDGETS_CHECKCOMBOBOX, /* REGIONS */
        XXWIDGETS_CHECKCOMBOBOX, /* HIGHLIGHTS */
        XXWIDGETS_CANVAS,        /* VIEW */
        XXWIDGETS_LABEL          /* STATUS */
    };
    static const char *const init_texts[XXWIDGET_VISUALIZATION_CONTROL_COUNT] = {
        "Type:", "", "Methods", "", "3", "1", "1", "Schema", "Reload", "Save", "Regions", "Highlights", "", "No data"
    };
    static const char *const label_texts[VIS_LABEL_COUNT] = {"Block", "Width", "Height"};
    static const int label_before[VIS_LABEL_COUNT] = {
        XXWIDGET_VISUALIZATION_CONTROL_BLOCK_SIZE, XXWIDGET_VISUALIZATION_CONTROL_WIDTH, XXWIDGET_VISUALIZATION_CONTROL_HEIGHT
    };
    const char *method_names[XX_VISUALIZATION_METHOD_COUNT];
    xxwidgets_app *app;
    xxwidget_visualization *vis;
    xxwidgets_status st = XXWIDGETS_OK;
    xxwidgets_rect dummy;

    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || xxwidgets_widget_kind(owner) != XXWIDGETS_WINDOW || !bounds_valid(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    app = xxwidgets_widget_app(owner);
    if (!app) return XXWIDGETS_INVALID_ARGUMENT;

    vis = (xxwidget_visualization *)calloc(1, sizeof(*vis));
    if (!vis) return XXWIDGETS_OUT_OF_MEMORY;
    vis->owner = owner;
    vis->bounds = bounds;
    xx_visualization_init(&vis->data);
    vis->width = 1;
    vis->height = 1;
    vis->methods = XX_VISUALIZATION_DEFAULT_METHODS;
    vis->map_mode = XX_MEMORY_MAP_MODE_UNKNOWN;
    vis->method = XX_VISUALIZATION_ENTROPY;
    vis->block_px = VIS_DEFAULT_BLOCK_PX;
    vis->marked = SIZE_MAX;

    dummy = (xxwidgets_rect){bounds.x, bounds.y, 1, 1};
    /* Create in visual / Tab order; each number edit after its label. */
    for (int id = 0; id < XXWIDGET_VISUALIZATION_CONTROL_COUNT && st == XXWIDGETS_OK; ++id) {
        for (int l = 0; l < VIS_LABEL_COUNT && st == XXWIDGETS_OK; ++l) {
            if (label_before[l] == id) st = xxwidgets_widget_create(app, owner, XXWIDGETS_LABEL, label_texts[l], dummy, &vis->labels[l]);
        }
        if (st == XXWIDGETS_OK) st = xxwidgets_widget_create(app, owner, kinds[id], init_texts[id], dummy, &vis->controls[id]);
    }
    if (st == XXWIDGETS_OK) st = xxwidgets_imageview_attach(C(vis, VIEW));
    if (st == XXWIDGETS_OK) {
        (void)xxwidgets_imageview_set_zoom(C(vis, VIEW), XXWIDGETS_IMAGE_FIT, 1.0);
        (void)xxwidgets_imageview_set_smooth(C(vis, VIEW), 0);
        (void)xxwidgets_imageview_set_background(C(vis, VIEW), XXWIDGETS_IMAGE_BACKGROUND_COLOR, (VIS_BASE_RGB << 8) | 0xFFu, (VIS_BASE_RGB << 8) | 0xFFu);
        for (int m = 0; m < XX_VISUALIZATION_METHOD_COUNT; ++m) method_names[m] = xx_visualization_method_name((xx_visualization_method)m);
        st = set_combo_records(C(vis, METHODS), method_names, NULL, XX_VISUALIZATION_METHOD_COUNT);
    }
    if (st == XXWIDGETS_OK) st = fill_methods_checks(vis);
    if (st == XXWIDGETS_OK) {
        (void)xxwidgets_checkcombobox_set_fixed_caption(C(vis, METHODS), "Methods");
        (void)xxwidgets_checkcombobox_set_fixed_caption(C(vis, REGIONS), "Regions");
        (void)xxwidgets_checkcombobox_set_fixed_caption(C(vis, HIGHLIGHTS), "Highlights");
        (void)xxwidgets_widget_set_tooltip(C(vis, MAP_MODE), "Mode");
        (void)xxwidgets_widget_set_tooltip(C(vis, METHODS), "Methods");
        (void)xxwidgets_widget_set_tooltip(C(vis, METHOD), "Method");
        (void)xxwidgets_widget_set_tooltip(C(vis, BLOCK_SIZE), "Block size");
        (void)xxwidgets_widget_set_tooltip(C(vis, REGIONS), "Regions");
        (void)xxwidgets_widget_set_tooltip(C(vis, HIGHLIGHTS), "Highlights");
        (void)xxwidgets_widget_set_enabled(C(vis, REGIONS), 0);
        (void)xxwidgets_widget_set_enabled(C(vis, HIGHLIGHTS), 0);
        set_edit_u32(C(vis, BLOCK_SIZE), vis->block_px);
        set_edit_u32(C(vis, WIDTH), vis->width);
        set_edit_u32(C(vis, HEIGHT), vis->height);
        st = xxwidget_visualization_set_rect(vis, bounds);
    }
    if (st != XXWIDGETS_OK) {
        (void)xxwidget_visualization_destroy(vis);
        return st;
    }
    *out = vis;
    return XXWIDGETS_OK;
}

XXWIDGETS_API xxwidgets_status xxwidget_visualization_destroy(xxwidget_visualization *vis)
{
    if (!vis) return XXWIDGETS_INVALID_ARGUMENT;
    for (int i = 0; i < XXWIDGET_VISUALIZATION_CONTROL_COUNT; ++i) {
        if (vis->controls[i]) {
            xxwidgets_status st = xxwidgets_widget_destroy(vis->controls[i]);
            if (st != XXWIDGETS_OK) return st;
            vis->controls[i] = NULL;
        }
    }
    for (int i = 0; i < VIS_LABEL_COUNT; ++i) {
        if (vis->labels[i]) {
            xxwidgets_status st = xxwidgets_widget_destroy(vis->labels[i]);
            if (st != XXWIDGETS_OK) return st;
            vis->labels[i] = NULL;
        }
    }
    free_data(vis);
    release_device(vis);
    free(vis);
    return XXWIDGETS_OK;
}

XXWIDGETS_API xxwidgets_widget *xxwidget_visualization_control(const xxwidget_visualization *vis, xxwidget_visualization_control_id id)
{
    if (!vis || (int)id < 0 || id >= XXWIDGET_VISUALIZATION_CONTROL_COUNT) return NULL;
    return vis->controls[id];
}

XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_location_handler(xxwidget_visualization *vis, xxwidget_visualization_location_fn fn, void *user_data)
{
    if (!vis) return XXWIDGETS_INVALID_ARGUMENT;
    vis->location_fn = fn;
    vis->location_user = user_data;
    return XXWIDGETS_OK;
}

/* ---- data --------------------------------------------------------------- */

static xxwidgets_status install_device(xxwidget_visualization *vis, xx_io_device *device, int owns, xx_file_type_t file_type, int reload)
{
    uint32_t w = 1, h = 1;
    xxwidgets_status st;

    if (vis->device != device) release_device(vis);
    free_data(vis);
    vis->device = device;
    vis->owns_device = owns;
    vis->file_type = file_type;
    vis->file_size = xx_io_size(device);
    if (vis->file_size < 0) vis->file_size = 0;
    vis->shown_type = file_type != XX_FILE_TYPE_UNKNOWN ? file_type : xx_format_get_file_type_device(device);
    vis->map_mode = XX_MEMORY_MAP_MODE_UNKNOWN;

    xx_visualization_default_size(vis->file_size, &w, &h);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > VIS_MAX_GRID) w = VIS_MAX_GRID;
    if (h > VIS_MAX_GRID) h = VIS_MAX_GRID;
    vis->width = w;
    vis->height = h;
    set_edit_u32(C(vis, WIDTH), w);
    set_edit_u32(C(vis, HEIGHT), h);

    (void)xxwidgets_window_begin_update(vis->owner);
    st = fill_map_modes(vis);
    if (st == XXWIDGETS_OK) st = fill_area_combo(C(vis, REGIONS), NULL, 0);
    if (st == XXWIDGETS_OK) st = fill_area_combo(C(vis, HIGHLIGHTS), NULL, 0);
    if (st == XXWIDGETS_OK) st = set_combo_records(C(vis, METHOD), NULL, NULL, 0);
    (void)xxwidgets_imageview_set_image(C(vis, VIEW), NULL);
    refresh_type(vis);
    refresh_status(vis);
    (void)xxwidgets_window_end_update(vis->owner);
    if (st != XXWIDGETS_OK) return st;
    if (reload) return xxwidget_visualization_reload(vis);
    return XXWIDGETS_OK;
}

XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_device(xxwidget_visualization *vis, xx_io_device *device, xx_file_type_t file_type, int reload)
{
    if (!vis || !device) return XXWIDGETS_INVALID_ARGUMENT;
    return install_device(vis, device, 0, file_type, reload);
}

XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_file(xxwidget_visualization *vis, const char *path, int reload)
{
    xx_io_device *device;
    if (!vis || !path || !path[0]) return XXWIDGETS_INVALID_ARGUMENT;
    device = xx_io_file_open(path, "rb");
    if (!device) return XXWIDGETS_PLATFORM_ERROR;
    return install_device(vis, device, 1, XX_FILE_TYPE_UNKNOWN, reload);
}

XXWIDGETS_API xxwidgets_status xxwidget_visualization_clear(xxwidget_visualization *vis)
{
    if (!vis) return XXWIDGETS_INVALID_ARGUMENT;
    free_data(vis);
    release_device(vis);
    vis->map_mode = XX_MEMORY_MAP_MODE_UNKNOWN;
    (void)xxwidgets_window_begin_update(vis->owner);
    (void)set_combo_records(C(vis, MAP_MODE), NULL, NULL, 0);
    (void)set_combo_records(C(vis, METHOD), NULL, NULL, 0);
    (void)fill_area_combo(C(vis, REGIONS), NULL, 0);
    (void)fill_area_combo(C(vis, HIGHLIGHTS), NULL, 0);
    (void)xxwidgets_imageview_set_image(C(vis, VIEW), NULL);
    refresh_type(vis);
    refresh_status(vis);
    (void)xxwidgets_window_end_update(vis->owner);
    return XXWIDGETS_OK;
}

XXWIDGETS_API xxwidgets_status xxwidget_visualization_reload(xxwidget_visualization *vis)
{
    xxwidgets_status st;
    int ok;
    if (!vis) return XXWIDGETS_INVALID_ARGUMENT;
    if (!vis->device) return XXWIDGETS_UNAVAILABLE;

    free_data(vis);
    vis->data.width = vis->width;
    vis->data.height = vis->height;
    vis->data.methods = vis->methods;
    vis->data.file_type = vis->shown_type;
    vis->data.map_mode = vis->map_mode;
    ok = xx_visualization_process(&vis->data, vis->device, NULL) ? 1 : 0;
    vis->has_data = ok && vis->data.blocks && vis->data.block_count;
    if (ok && !vis->has_data) xx_visualization_free(&vis->data);

    (void)xxwidgets_window_begin_update(vis->owner);
    st = fill_area_combo(C(vis, REGIONS), vis->has_data ? vis->data.regions : NULL, vis->has_data ? vis->data.region_count : 0);
    if (st == XXWIDGETS_OK) {
        st = fill_area_combo(C(vis, HIGHLIGHTS), vis->has_data ? vis->data.highlights : NULL, vis->has_data ? vis->data.highlight_count : 0);
    }
    if (st == XXWIDGETS_OK) st = fill_method_combo(vis, vis->methods);
    if (st == XXWIDGETS_OK) st = redraw(vis);
    refresh_type(vis);
    refresh_status(vis);
    (void)xxwidgets_window_end_update(vis->owner);
    if (st != XXWIDGETS_OK) return st;
    return vis->has_data ? XXWIDGETS_OK : XXWIDGETS_PLATFORM_ERROR;
}

/* ---- settings ----------------------------------------------------------- */

XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_grid(xxwidget_visualization *vis, uint32_t width, uint32_t height)
{
    if (!vis || width < 1 || height < 1 || width > VIS_MAX_GRID || height > VIS_MAX_GRID ||
        (uint64_t)width * height > (uint64_t)XX_VISUALIZATION_MAX_BLOCKS) {
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    vis->width = width;
    vis->height = height;
    set_edit_u32(C(vis, WIDTH), width);
    set_edit_u32(C(vis, HEIGHT), height);
    return XXWIDGETS_OK;
}

XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_methods(xxwidget_visualization *vis, uint32_t method_flags)
{
    if (!vis || !method_flags || (method_flags & ~XX_VISUALIZATION_ALL_METHODS)) return XXWIDGETS_INVALID_ARGUMENT;
    vis->methods = method_flags;
    return fill_methods_checks(vis);
}

XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_method(xxwidget_visualization *vis, xx_visualization_method method)
{
    int idx;
    if (!vis || (int)method < 0 || (int)method >= XX_VISUALIZATION_METHOD_COUNT) return XXWIDGETS_INVALID_ARGUMENT;
    vis->method = method;
    vis->method_chosen = 1;
    idx = combo_find_u64(C(vis, METHOD), (uint64_t)method);
    if (idx >= 0) (void)xxwidgets_widget_set_value(C(vis, METHOD), idx);
    return redraw(vis);
}

XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_map_mode(xxwidget_visualization *vis, xx_memory_map_mode_t mode)
{
    int idx;
    if (!vis || (int)mode < (int)XX_MEMORY_MAP_MODE_UNKNOWN || (int)mode > (int)XX_MEMORY_MAP_MODE_DATA) return XXWIDGETS_INVALID_ARGUMENT;
    vis->map_mode = mode;
    idx = combo_find_u64(C(vis, MAP_MODE), (uint64_t)mode);
    if (idx >= 0) (void)xxwidgets_widget_set_value(C(vis, MAP_MODE), idx);
    return XXWIDGETS_OK;
}

XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_block_px(xxwidget_visualization *vis, uint32_t block_px)
{
    if (!vis || block_px < 1 || block_px > VIS_MAX_BLOCK_PX) return XXWIDGETS_INVALID_ARGUMENT;
    vis->block_px = block_px;
    set_edit_u32(C(vis, BLOCK_SIZE), block_px);
    return redraw(vis);
}

XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_schema(xxwidget_visualization *vis, int schema)
{
    if (!vis) return XXWIDGETS_INVALID_ARGUMENT;
    vis->schema = schema ? 1 : 0;
    (void)xxwidgets_widget_set_value(C(vis, SCHEMA), vis->schema);
    return redraw(vis);
}

XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_area_enabled(xxwidget_visualization *vis, int highlight, size_t index, int enabled)
{
    xx_visualization_area *areas;
    size_t count;
    xxwidgets_widget *combo;
    if (!vis || !vis->has_data) return XXWIDGETS_INVALID_ARGUMENT;
    areas = highlight ? vis->data.highlights : vis->data.regions;
    count = highlight ? vis->data.highlight_count : vis->data.region_count;
    combo = highlight ? C(vis, HIGHLIGHTS) : C(vis, REGIONS);
    if (!areas || index >= count) return XXWIDGETS_INVALID_ARGUMENT;
    areas[index].enabled = enabled ? true : false;
    (void)xxwidgets_checkcombobox_set_checked(combo, index, enabled ? 1 : 0);
    return redraw(vis);
}

XXWIDGETS_API xxwidgets_status xxwidget_visualization_select_block(xxwidget_visualization *vis, size_t index)
{
    xxwidgets_status st;
    if (!vis) return XXWIDGETS_INVALID_ARGUMENT;
    if (index == SIZE_MAX) {
        vis->marked = SIZE_MAX;
        st = redraw(vis);
        refresh_status(vis);
        return st;
    }
    if (!vis->has_data) return XXWIDGETS_UNAVAILABLE;
    if (index >= vis->data.block_count) return XXWIDGETS_INVALID_ARGUMENT;
    vis->marked = index;
    st = redraw(vis);
    refresh_status(vis);
    if (vis->location_fn) vis->location_fn(vis, vis->data.blocks[index].offset, vis->data.blocks[index].size, vis->location_user);
    return st;
}

XXWIDGETS_API const xx_visualization *xxwidget_visualization_data(const xxwidget_visualization *vis)
{
    return (vis && vis->has_data) ? &vis->data : NULL;
}

XXWIDGETS_API xxwidgets_status xxwidget_visualization_render(xxwidget_visualization *vis, uint8_t **rgba, uint32_t *width, uint32_t *height)
{
    uint8_t *buf = NULL;
    uint32_t w = 0, h = 0;
    xxwidgets_status st;
    if (!vis || !rgba || !width || !height) return XXWIDGETS_INVALID_ARGUMENT;
    *rgba = NULL;
    *width = 0;
    *height = 0;
    st = render_current(vis, &buf, &w, &h);
    if (st != XXWIDGETS_OK) return st;
    *rgba = buf;
    *width = w;
    *height = h;
    return XXWIDGETS_OK;
}

XXWIDGETS_API xxwidgets_status xxwidget_visualization_save_image(xxwidget_visualization *vis, const char *path)
{
    uint8_t *buf = NULL;
    uint32_t w = 0, h = 0;
    xx_image img;
    xx_image_save_options so;
    xx_image_status ist;
    xxwidgets_status st;

    if (!vis || !path || !path[0]) return XXWIDGETS_INVALID_ARGUMENT;
    st = render_current(vis, &buf, &w, &h);
    if (st != XXWIDGETS_OK) return st;
    xx_image_init(&img);
    ist = xx_image_create(&img, w, h);
    if (ist != XX_IMAGE_OK || !img.frame_count || !img.frames || !img.frames[0].rgba) {
        free(buf);
        xx_image_free(&img);
        return ist != XX_IMAGE_OK ? map_image_status(ist) : XXWIDGETS_OUT_OF_MEMORY;
    }
    memcpy(img.frames[0].rgba, buf, (size_t)w * h * 4u);
    free(buf);
    xx_image_save_options_init(&so);
    ist = xx_image_save_file(&img, path, NULL, &so);
    xx_image_free(&img);
    return map_image_status(ist);
}

/* ---- events ------------------------------------------------------------- */

static int is_own_widget(const xxwidget_visualization *vis, const xxwidgets_widget *w)
{
    if (!w) return 0;
    for (int i = 0; i < XXWIDGET_VISUALIZATION_CONTROL_COUNT; ++i) {
        if (vis->controls[i] == w) return 1;
    }
    for (int i = 0; i < VIS_LABEL_COUNT; ++i) {
        if (vis->labels[i] == w) return 1;
    }
    return 0;
}

static void handle_view_click(xxwidget_visualization *vis)
{
    unsigned int x = 0, y = 0;
    vis_layout lay;
    uint32_t gx, bx, by;
    size_t index;
    if (!vis->has_data) return;
    if (xxwidgets_imageview_get_selection(C(vis, VIEW), &x, &y) != XXWIDGETS_OK) return;
    if (compute_layout(vis, &lay) != XXWIDGETS_OK) return;
    if (x < lay.left_w) return;
    gx = (uint32_t)x - lay.left_w;
    if (gx >= lay.grid_w || y >= lay.grid_h) return;
    bx = gx / vis->block_px;
    by = (uint32_t)y / vis->block_px;
    if (bx >= vis->data.width || by >= vis->data.height) return;
    index = (size_t)by * vis->data.width + bx;
    if (index >= vis->data.block_count) return;
    (void)xxwidget_visualization_select_block(vis, index);
}

static void handle_save(xxwidget_visualization *vis)
{
    char *path = NULL;
    int accepted = 0;
    if (!vis->has_data) return;
    if (!xxwidgets_file_dialog_available(xxwidgets_widget_app(vis->owner))) return; /* the host saves through save_image */
    if (xxwidgets_file_dialog(vis->owner, XXWIDGETS_FILE_DIALOG_SAVE, "Save", "visualization.png", &path, &accepted) == XXWIDGETS_OK && accepted && path) {
        if (xxwidget_visualization_save_image(vis, path) != XXWIDGETS_OK) {
            (void)xxwidgets_widget_set_text(C(vis, STATUS), "Save failed");
        }
    }
    free(path);
}

XXWIDGETS_API xxwidgets_status xxwidget_visualization_handle_event(xxwidget_visualization *vis, const xxwidgets_event *event)
{
    xxwidgets_widget *w;
    if (!vis || !event) return XXWIDGETS_INVALID_ARGUMENT;
    if (!is_own_widget(vis, event->widget)) return XXWIDGETS_UNAVAILABLE;
    w = event->widget;

    if (w == C(vis, MAP_MODE) && (event->type == XXWIDGETS_EVENT_SELECT || event->type == XXWIDGETS_EVENT_CHANGE)) {
        uint64_t v;
        if (combo_current_u64(w, &v) && v <= (uint64_t)XX_MEMORY_MAP_MODE_DATA && (xx_memory_map_mode_t)v != vis->map_mode) {
            vis->map_mode = (xx_memory_map_mode_t)v;
            if (vis->device) (void)xxwidget_visualization_reload(vis); /* like on_comboBoxMapMode_currentIndexChanged */
        }
        return XXWIDGETS_OK;
    }
    if (w == C(vis, METHOD) && (event->type == XXWIDGETS_EVENT_SELECT || event->type == XXWIDGETS_EVENT_CHANGE)) {
        uint64_t v;
        if (combo_current_u64(w, &v) && v < (uint64_t)XX_VISUALIZATION_METHOD_COUNT) {
            vis->method = (xx_visualization_method)v;
            vis->method_chosen = 1;
            (void)redraw(vis);
        }
        return XXWIDGETS_OK;
    }
    if (w == C(vis, METHODS) && event->type == XXWIDGETS_EVENT_CHANGE) {
        uint32_t flags = 0;
        for (int m = 0; m < XX_VISUALIZATION_METHOD_COUNT; ++m) {
            int checked = 0;
            if (xxwidgets_checkcombobox_is_checked(w, (size_t)m, &checked) == XXWIDGETS_OK && checked) flags |= XX_VISUALIZATION_FLAG(m);
        }
        if (!flags) {
            (void)fill_methods_checks(vis); /* keep at least one method */
        } else {
            vis->methods = flags;
        }
        return XXWIDGETS_OK;
    }
    if ((w == C(vis, REGIONS) || w == C(vis, HIGHLIGHTS)) && event->type == XXWIDGETS_EVENT_CHANGE) {
        int highlight = w == C(vis, HIGHLIGHTS);
        int checked = 0;
        if (event->value >= 0 && xxwidgets_checkcombobox_is_checked(w, (size_t)event->value, &checked) == XXWIDGETS_OK) {
            (void)xxwidget_visualization_set_area_enabled(vis, highlight, (size_t)event->value, checked);
        }
        return XXWIDGETS_OK;
    }
    if (w == C(vis, BLOCK_SIZE) && event->type == XXWIDGETS_EVENT_ACTIVATE) {
        uint32_t v = 0;
        if (!read_edit_u32(w, &v) || xxwidget_visualization_set_block_px(vis, v) != XXWIDGETS_OK) set_edit_u32(w, vis->block_px);
        return XXWIDGETS_OK;
    }
    if ((w == C(vis, WIDTH) || w == C(vis, HEIGHT)) && event->type == XXWIDGETS_EVENT_ACTIVATE) {
        uint32_t v = 0;
        int ok = read_edit_u32(w, &v);
        if (ok) ok = xxwidget_visualization_set_grid(vis, w == C(vis, WIDTH) ? v : vis->width, w == C(vis, HEIGHT) ? v : vis->height) == XXWIDGETS_OK;
        if (!ok) set_edit_u32(w, w == C(vis, WIDTH) ? vis->width : vis->height);
        return XXWIDGETS_OK;
    }
    if (w == C(vis, SCHEMA) && (event->type == XXWIDGETS_EVENT_CLICK || event->type == XXWIDGETS_EVENT_CHANGE)) {
        int v = 0;
        if (xxwidgets_widget_get_value(w, &v) == XXWIDGETS_OK && (v ? 1 : 0) != vis->schema) (void)xxwidget_visualization_set_schema(vis, v);
        return XXWIDGETS_OK;
    }
    if (w == C(vis, RELOAD) && event->type == XXWIDGETS_EVENT_CLICK) {
        /* Pending edits count, as the Qt spin boxes do. */
        uint32_t bw = 0, bh = 0, bp = 0;
        if (read_edit_u32(C(vis, WIDTH), &bw) && read_edit_u32(C(vis, HEIGHT), &bh)) (void)xxwidget_visualization_set_grid(vis, bw, bh);
        if (read_edit_u32(C(vis, BLOCK_SIZE), &bp) && bp >= 1 && bp <= VIS_MAX_BLOCK_PX) vis->block_px = bp;
        if (vis->device) (void)xxwidget_visualization_reload(vis);
        return XXWIDGETS_OK;
    }
    if (w == C(vis, SAVE) && event->type == XXWIDGETS_EVENT_CLICK) {
        handle_save(vis);
        return XXWIDGETS_OK;
    }
    if (w == C(vis, VIEW) && event->type == XXWIDGETS_EVENT_SELECT) {
        handle_view_click(vis);
        return XXWIDGETS_OK;
    }
    return XXWIDGETS_OK;
}
