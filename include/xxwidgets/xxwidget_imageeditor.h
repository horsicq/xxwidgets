/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/* Image editor: a composite widget that edits an xx_image (xxfclib image
 * model) with the xxvis operations, shows it in an image view
 * (xxwidgets_imageview.h) and keeps an undo / redo history.
 *
 * Optional component: build with XXWIDGETS_BUILD_IMAGE_EDITOR (needs xxvis
 * and XXWIDGETS_USE_XXFCLIB_TARGET) and link xxwidgets::imageeditor.
 *
 * Layout inside the bounds given to create / set_rect (text cells):
 *
 *   row 0  [Operation v] [p1: ___] [p2: ___] [p3: ___] [p4: ___] [Text: ______] [Apply] [Undo] [Redo] [Reset]
 *   row 1+ image view (fit to the area)
 *   last   status: size, frames, zoom, the last operation and its result
 *
 * Choosing an operation shows its parameter fields (with their names and
 * default values) and hides the unused ones; Apply (or Enter in a field)
 * runs it on the current image.  Every successful operation can be undone.
 *
 * The host forwards every event to xxwidget_imageeditor_handle_event.
 */
#ifndef XXWIDGET_IMAGEEDITOR_H
#define XXWIDGET_IMAGEEDITOR_H
#include "xxwidgets.h"
#include "xxfclib/formats/image/xx_image.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum xxwidget_imageeditor_op {
    XXWIDGET_IMAGEEDITOR_ROTATE = 0,         /* turns (+right / -left) */
    XXWIDGET_IMAGEEDITOR_FLIP,               /* horizontal (1) or vertical (0) */
    XXWIDGET_IMAGEEDITOR_CROP,               /* x, y, width, height */
    XXWIDGET_IMAGEEDITOR_RESIZE,             /* width, height (0 = keep aspect), method (xxvis_resample) */
    XXWIDGET_IMAGEEDITOR_CONTAIN,            /* width, height, method, background (0xRRGGBB, -1 = transparent) */
    XXWIDGET_IMAGEEDITOR_COVER,              /* width, height, method */
    XXWIDGET_IMAGEEDITOR_BLUR,               /* sigma */
    XXWIDGET_IMAGEEDITOR_SHARPEN,            /* sigma, amount %, threshold */
    XXWIDGET_IMAGEEDITOR_DITHER,             /* method (xxvis_dither_method), colours (2 = black and white, 3..256 = palette quantised from the image) */
    XXWIDGET_IMAGEEDITOR_INVERT,             /* - */
    XXWIDGET_IMAGEEDITOR_BRIGHTNESS_CONTRAST,/* brightness, contrast */
    XXWIDGET_IMAGEEDITOR_OPACITY,            /* percent */
    XXWIDGET_IMAGEEDITOR_FILTER,             /* filter (xxvis_filter) */
    XXWIDGET_IMAGEEDITOR_HSL,                /* hue, saturation, lightness */
    XXWIDGET_IMAGEEDITOR_NORMALISE,          /* - */
    XXWIDGET_IMAGEEDITOR_AUTO_ORIENT,        /* - : applies the EXIF / TIFF Orientation property, if any */
    XXWIDGET_IMAGEEDITOR_TEXT,               /* x, y, scale, colour (0xRRGGBB) + text */
    XXWIDGET_IMAGEEDITOR_CHART_HISTOGRAM,    /* width, height: series chart of the R, G, B and luma histograms */
    XXWIDGET_IMAGEEDITOR_CHART_SCATTER,      /* width, height: scatter of red against green, at most 4096 sampled pixels */
    XXWIDGET_IMAGEEDITOR_CHART_HEATMAP,      /* width, height, columns, rows: luma averaged over a columns x rows grid */
    XXWIDGET_IMAGEEDITOR_CHART_HEXBIN,       /* width, height: hue (x, 0..360) against lightness (y, 0..100) density */
    XXWIDGET_IMAGEEDITOR_OP_COUNT
} xxwidget_imageeditor_op;

/** One numeric parameter of an operation. */
typedef struct xxwidget_imageeditor_param {
    const char *name;  /* short label, e.g. "sigma" */
    double minimum;
    double maximum;
    double initial;    /* value shown when the operation is chosen */
} xxwidget_imageeditor_param;

/** Static description of an operation. */
typedef struct xxwidget_imageeditor_op_info {
    const char *id;     /* "rotate", "blur", ... (stable, lower case) */
    const char *title;  /* "Rotate", "Brightness / contrast", ... */
    size_t param_count; /* 0..4 */
    xxwidget_imageeditor_param params[4];
    int uses_text;      /* 1 for TEXT */
} xxwidget_imageeditor_op_info;

/** NULL outside 0..XXWIDGET_IMAGEEDITOR_OP_COUNT-1. */
XXWIDGETS_API const xxwidget_imageeditor_op_info *xxwidget_imageeditor_op_info_get(xxwidget_imageeditor_op op);
/** The op whose id equals @p id, or XXWIDGET_IMAGEEDITOR_OP_COUNT. */
XXWIDGETS_API xxwidget_imageeditor_op xxwidget_imageeditor_op_find(const char *id);

typedef struct xxwidget_imageeditor xxwidget_imageeditor;

typedef enum xxwidget_imageeditor_control_id {
    XXWIDGET_IMAGEEDITOR_CONTROL_OPERATION = 0,
    XXWIDGET_IMAGEEDITOR_CONTROL_PARAM0,
    XXWIDGET_IMAGEEDITOR_CONTROL_PARAM1,
    XXWIDGET_IMAGEEDITOR_CONTROL_PARAM2,
    XXWIDGET_IMAGEEDITOR_CONTROL_PARAM3,
    XXWIDGET_IMAGEEDITOR_CONTROL_TEXT,
    XXWIDGET_IMAGEEDITOR_CONTROL_APPLY,
    XXWIDGET_IMAGEEDITOR_CONTROL_UNDO,
    XXWIDGET_IMAGEEDITOR_CONTROL_REDO,
    XXWIDGET_IMAGEEDITOR_CONTROL_RESET,
    XXWIDGET_IMAGEEDITOR_CONTROL_VIEW,
    XXWIDGET_IMAGEEDITOR_CONTROL_STATUS,
    XXWIDGET_IMAGEEDITOR_CONTROL_COUNT
} xxwidget_imageeditor_control_id;

/** Creates the controls as children of @p owner (a WINDOW) inside @p bounds. */
XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidget_imageeditor **out);
/** Destroys the controls and frees the image and the history. */
XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_destroy(xxwidget_imageeditor *editor);
XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_set_rect(xxwidget_imageeditor *editor, xxwidgets_rect bounds);
XXWIDGETS_API xxwidgets_widget *xxwidget_imageeditor_control(const xxwidget_imageeditor *editor, xxwidget_imageeditor_control_id id);
/** Handles events of its own controls; returns XXWIDGETS_OK when the event was
 *  consumed and XXWIDGETS_UNAVAILABLE when it belongs to someone else. */
XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_handle_event(xxwidget_imageeditor *editor, const xxwidgets_event *event);

/** Replaces the image with a copy of @p image (NULL clears) and clears the
 *  history; the original becomes what Reset returns to. */
XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_set_image(xxwidget_imageeditor *editor, const xx_image *image);
/** The current image (owned by the editor; valid until the next change). */
XXWIDGETS_API const xx_image *xxwidget_imageeditor_image(const xxwidget_imageeditor *editor);
/** Loads a file (any xxfclib codec) like set_image. */
XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_load_file(xxwidget_imageeditor *editor, const char *path);
/** Saves the current image; @p codec NULL picks the codec from the extension
 *  (this is also "convert format").  All frames are written when the codec can. */
XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_save_file(xxwidget_imageeditor *editor, const char *path, const xx_image_codec *codec);

/** Runs @p op with @p params (count must equal the op's param_count; NULL with
 *  count 0) and, for TEXT, @p text.  Parameters are checked against the op's
 *  ranges.  On success the previous image goes onto the undo stack, the redo
 *  stack is cleared and the view shows the result. */
XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_apply(xxwidget_imageeditor *editor, xxwidget_imageeditor_op op, const double *params, size_t count, const char *text);
/** Runs the operation chosen in the controls with the values typed into them
 *  (what Apply does).  Fields accept decimal numbers and #RRGGBB / 0xRRGGBB. */
XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_apply_controls(xxwidget_imageeditor *editor);
/** Chooses @p op in the controls and fills its default values. */
XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_select_op(xxwidget_imageeditor *editor, xxwidget_imageeditor_op op);
XXWIDGETS_API xxwidget_imageeditor_op xxwidget_imageeditor_selected_op(const xxwidget_imageeditor *editor);

XXWIDGETS_API int xxwidget_imageeditor_can_undo(const xxwidget_imageeditor *editor);
XXWIDGETS_API int xxwidget_imageeditor_can_redo(const xxwidget_imageeditor *editor);
XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_undo(xxwidget_imageeditor *editor);
XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_redo(xxwidget_imageeditor *editor);
/** Back to the image given to set_image / load_file (undoable). */
XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_reset(xxwidget_imageeditor *editor);
/** Undo depth limit in steps (default 32, 1..1024); the oldest steps are
 *  dropped first.  The history also never holds more than 1 GiB of pixels. */
XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_set_history_limit(xxwidget_imageeditor *editor, size_t steps);

/** Frame shown for animations / multi-page images (operations apply to all). */
XXWIDGETS_API xxwidgets_status xxwidget_imageeditor_set_frame(xxwidget_imageeditor *editor, size_t frame);
/** Increments on every change of the image (apply, undo, redo, reset, set). */
XXWIDGETS_API uint64_t xxwidget_imageeditor_revision(const xxwidget_imageeditor *editor);
/** The message of the last operation ("Blur: ok", "Crop: invalid argument"). */
XXWIDGETS_API const char *xxwidget_imageeditor_last_message(const xxwidget_imageeditor *editor);

#ifdef __cplusplus
}
#endif
#endif
