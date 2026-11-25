/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/* Binary visualization widget: the xxwidgets counterpart of
 * XVisualizationWidget.  Shows a file as a grid of blocks shaded by a
 * statistic (xxfclib/formats/xx_visualization.h), coloured by the format's
 * regions and data highlights, with a schema view that lists their names.
 *
 * Optional component: XXWIDGETS_BUILD_VISUALIZATION (needs xxvis and
 * XXWIDGETS_USE_XXFCLIB_TARGET); link xxwidgets::visualization.
 *
 * Layout inside the bounds (text cells):
 *
 *   row 0  Type: <detected type>  [Map mode v] [Methods v (check)] [Method v]
 *          Block [__] Width [___] Height [___] [x] Schema [Reload] [Save]
 *   row 1  [Regions v (check)] [Highlights v (check)]
 *   rows   image view (fit, zoom, pan)
 *   last   status: file size, block size in bytes, the block under the last click
 *
 * Grid view: width x height blocks of block_px pixels.  Schema view: the grid
 * with the enabled region names (colour swatch + name) in a column on its left
 * and the enabled highlight names on its right, like the Schema tab.
 *
 * A click on a block marks it red and reports its file offset and size
 * (XVisualizationImage::currentLocationChanged) through the location handler
 * below.
 */
#ifndef XXWIDGET_VISUALIZATION_H
#define XXWIDGET_VISUALIZATION_H
#include "xxwidgets.h"
#include "xxfclib/formats/xx_visualization.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidget_visualization xxwidget_visualization;

typedef void (*xxwidget_visualization_location_fn)(xxwidget_visualization *vis, int64_t offset, int64_t size, void *user_data);

typedef enum xxwidget_visualization_control_id {
    XXWIDGET_VISUALIZATION_CONTROL_TYPE = 0, /* LABEL "Type: PE32" */
    XXWIDGET_VISUALIZATION_CONTROL_MAP_MODE, /* COMBOBOX */
    XXWIDGET_VISUALIZATION_CONTROL_METHODS,  /* CHECKCOMBOBOX of methods to compute */
    XXWIDGET_VISUALIZATION_CONTROL_METHOD,   /* COMBOBOX of computed methods shown */
    XXWIDGET_VISUALIZATION_CONTROL_BLOCK_SIZE,
    XXWIDGET_VISUALIZATION_CONTROL_WIDTH,
    XXWIDGET_VISUALIZATION_CONTROL_HEIGHT,
    XXWIDGET_VISUALIZATION_CONTROL_SCHEMA,   /* CHECKBOX */
    XXWIDGET_VISUALIZATION_CONTROL_RELOAD,
    XXWIDGET_VISUALIZATION_CONTROL_SAVE,
    XXWIDGET_VISUALIZATION_CONTROL_REGIONS,    /* CHECKCOMBOBOX */
    XXWIDGET_VISUALIZATION_CONTROL_HIGHLIGHTS, /* CHECKCOMBOBOX */
    XXWIDGET_VISUALIZATION_CONTROL_VIEW,       /* CANVAS with the image view */
    XXWIDGET_VISUALIZATION_CONTROL_STATUS,
    XXWIDGET_VISUALIZATION_CONTROL_COUNT
} xxwidget_visualization_control_id;

XXWIDGETS_API xxwidgets_status xxwidget_visualization_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidget_visualization **out);
XXWIDGETS_API xxwidgets_status xxwidget_visualization_destroy(xxwidget_visualization *vis);
XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_rect(xxwidget_visualization *vis, xxwidgets_rect bounds);
XXWIDGETS_API xxwidgets_widget *xxwidget_visualization_control(const xxwidget_visualization *vis, xxwidget_visualization_control_id id);
/** XXWIDGETS_OK when consumed, XXWIDGETS_UNAVAILABLE for foreign events. */
XXWIDGETS_API xxwidgets_status xxwidget_visualization_handle_event(xxwidget_visualization *vis, const xxwidgets_event *event);
XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_location_handler(xxwidget_visualization *vis, xxwidget_visualization_location_fn fn, void *user_data);

/** Shows @p device (borrowed: it must outlive the widget or the next
 *  set_device / set_file / clear).  @p file_type XX_FILE_TYPE_UNKNOWN detects.
 *  Proposes the grid with xx_visualization_default_size, fills the map modes,
 *  and with @p reload processes at once (like setData(..., bAuto)). */
XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_device(xxwidget_visualization *vis, xx_io_device *device, xx_file_type_t file_type, int reload);
/** Opens @p path (UTF-8) and owns that device. */
XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_file(xxwidget_visualization *vis, const char *path, int reload);
XXWIDGETS_API xxwidgets_status xxwidget_visualization_clear(xxwidget_visualization *vis);

/** Recomputes with the current settings (Reload). */
XXWIDGETS_API xxwidgets_status xxwidget_visualization_reload(xxwidget_visualization *vis);
/** Settings; each updates its control.  Changing methods, grid or map mode
 *  needs reload; method, block size, schema and enabled areas only redraw. */
XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_grid(xxwidget_visualization *vis, uint32_t width, uint32_t height);
XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_methods(xxwidget_visualization *vis, uint32_t method_flags);
XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_method(xxwidget_visualization *vis, xx_visualization_method method);
XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_map_mode(xxwidget_visualization *vis, xx_memory_map_mode_t mode);
/** Pixels per block, 1..32 (default 3). */
XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_block_px(xxwidget_visualization *vis, uint32_t block_px);
XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_schema(xxwidget_visualization *vis, int schema);
/** Enables / disables region (@p highlight 0) or highlight (1) @p index. */
XXWIDGETS_API xxwidgets_status xxwidget_visualization_set_area_enabled(xxwidget_visualization *vis, int highlight, size_t index, int enabled);
/** Marks block @p index as if clicked (SIZE_MAX clears); calls the handler. */
XXWIDGETS_API xxwidgets_status xxwidget_visualization_select_block(xxwidget_visualization *vis, size_t index);

/** The computed data (NULL before the first successful reload). */
XXWIDGETS_API const xx_visualization *xxwidget_visualization_data(const xxwidget_visualization *vis);
/** Renders the current view (grid or schema) as straight RGBA8 into a new
 *  malloc'ed buffer the caller frees. */
XXWIDGETS_API xxwidgets_status xxwidget_visualization_render(xxwidget_visualization *vis, uint8_t **rgba, uint32_t *width, uint32_t *height);
/** Saves the current view; the format comes from the extension (Save). */
XXWIDGETS_API xxwidgets_status xxwidget_visualization_save_image(xxwidget_visualization *vis, const char *path);

#ifdef __cplusplus
}
#endif
#endif
