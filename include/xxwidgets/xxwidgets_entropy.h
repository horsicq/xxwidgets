/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXWIDGETS_ENTROPY_H
#define XXWIDGETS_ENTROPY_H
#include "xxwidgets/xxwidgets.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_entropy xxwidgets_entropy;
typedef struct xxwidgets_entropy_region {
    uint64_t offset, size; /* Relative to the supplied buffer, not its display base. */
    const char *name;      /* UTF-8, copied. Overlapping regions are allowed. */
} xxwidgets_entropy_region;
typedef struct xxwidgets_entropy_sample {
    uint64_t offset, size; /* Absolute display offset. */
    double entropy;
} xxwidgets_entropy_sample;
/* Precomputed streaming analysis. No source bytes are needed. Offsets in
 * regions are relative; samples are absolute. Histogram sum must equal size;
 * ordered samples cover the complete nonempty range. All arrays are copied. */
typedef struct xxwidgets_entropy_statistics {
    uint64_t size, base;
    const char *file_type;
    double total;
    const uint64_t *counts; /* 256 bins */
    const xxwidgets_entropy_region *regions;
    const double *region_entropy;
    size_t region_count;
    const xxwidgets_entropy_sample *samples;
    size_t sample_count;
} xxwidgets_entropy_statistics;
typedef enum xxwidgets_entropy_control_id {
    XXWIDGETS_ENTROPY_TYPE,
    XXWIDGETS_ENTROPY_MAP,
    XXWIDGETS_ENTROPY_OFFSET,
    XXWIDGETS_ENTROPY_SIZE,
    XXWIDGETS_ENTROPY_COUNT_LABEL,
    XXWIDGETS_ENTROPY_COUNT,
    XXWIDGETS_ENTROPY_PART_SIZE,
    XXWIDGETS_ENTROPY_RELOAD,
    XXWIDGETS_ENTROPY_TOTAL,
    XXWIDGETS_ENTROPY_PROGRESS,
    XXWIDGETS_ENTROPY_STATUS,
    XXWIDGETS_ENTROPY_SAVE,
    XXWIDGETS_ENTROPY_SAVE_DIAGRAM,
    XXWIDGETS_ENTROPY_TAB,
    XXWIDGETS_ENTROPY_BYTES_TAB,
    XXWIDGETS_ENTROPY_FILTER_LABEL,
    XXWIDGETS_ENTROPY_FILTER,
    XXWIDGETS_ENTROPY_REGIONS_TABLE,
    XXWIDGETS_ENTROPY_BYTES_TABLE,
    XXWIDGETS_ENTROPY_GRID,
    XXWIDGETS_ENTROPY_TRACKER,
    XXWIDGETS_ENTROPY_GRAPH,
    XXWIDGETS_ENTROPY_CONTROL_COUNT
} xxwidgets_entropy_control_id;
/* Standalone ENTROPY_PLOT with caller-computed streaming samples. Copies up to
 * 4096 ordered, non-overlapping samples. No file bytes are retained. Empty
 * NULL/0 clears the plot. Invalid input or backend errors preserve its model.
 * Plot SELECT events carry the zero-based sample index. */
XXWIDGETS_API xxwidgets_status xxwidgets_entropy_plot_set_samples(xxwidgets_widget *plot,
    const xxwidgets_entropy_sample *samples, size_t count, uint64_t base, uint64_t size);

/* XEntropyWidget-style composite, native desktop and TUI. Bounds >= 88 x 27
 * cells. Owns its child controls; destroy before the owner window/app. Forward
 * events to handle_event. Save/Save diagram CLICK events are left to the caller
 * to choose a destination and use the export functions outside the callback. */
XXWIDGETS_API xxwidgets_status xxwidgets_entropy_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_entropy **out);
XXWIDGETS_API xxwidgets_status xxwidgets_entropy_destroy(xxwidgets_entropy *view);
XXWIDGETS_API xxwidgets_status xxwidgets_entropy_set_rect(xxwidgets_entropy *view, xxwidgets_rect bounds);
XXWIDGETS_API xxwidgets_widget *xxwidgets_entropy_control(const xxwidgets_entropy *view, xxwidgets_entropy_control_id id);
XXWIDGETS_API xxwidgets_status xxwidgets_entropy_handle_event(xxwidgets_entropy *view, const xxwidgets_event *event);
/* Copies bytes and region names. NULL/0 bytes clears the data. NULL/0 regions
 * supplies a single Whole file region. File type is a display name; the caller
 * supplies format-aware regions from its parser, keeping the widget independent
 * of a particular file-format engine. Auto count = clamp(size/512, 1, 100).
 * Invalid data and allocation failures preserve the preceding model. */
XXWIDGETS_API xxwidgets_status xxwidgets_entropy_set_data(xxwidgets_entropy *view, const void *data, size_t size, uint64_t base,
                                                          const char *file_type, const xxwidgets_entropy_region *regions, size_t count);
/* Installs a snapshot transactionally, up to 1024 regions and 4096 samples.
 * Count/Reload are disabled: recomputing requires new source data. Export,
 * filtering, map selection, byte distribution and plot tracking remain active. */
XXWIDGETS_API xxwidgets_status xxwidgets_entropy_set_statistics(xxwidgets_entropy *view, const xxwidgets_entropy_statistics *statistics);
/* Resizable modal using the same composite. Close/Esc dismisses it. Export
 * buttons are hidden; hosts embedding the composite use the export APIs. */
XXWIDGETS_API xxwidgets_status xxwidgets_entropy_dialog(xxwidgets_widget *owner, const char *title,
    const xxwidgets_entropy_statistics *statistics);
/* Reload the copied data with 1..1000 blocks (clamped to size for small inputs).
 * The final block includes the remainder. Empty inputs have zero samples.
 * Shannon entropy uses [0,8] bits/byte; packed means >= 6.5, as in XBinary. */
XXWIDGETS_API xxwidgets_status xxwidgets_entropy_set_count(xxwidgets_entropy *view, size_t count);
XXWIDGETS_API double xxwidgets_entropy_total(const xxwidgets_entropy *view);
XXWIDGETS_API uint64_t xxwidgets_entropy_byte_count(const xxwidgets_entropy *view, unsigned int byte);
XXWIDGETS_API size_t xxwidgets_entropy_sample_count(const xxwidgets_entropy *view);
XXWIDGETS_API xxwidgets_status xxwidgets_entropy_get_sample(const xxwidgets_entropy *view, size_t index, xxwidgets_entropy_sample *sample);
XXWIDGETS_API xxwidgets_status xxwidgets_entropy_get_region(const xxwidgets_entropy *view, size_t index, xxwidgets_entropy_region *region,
                                                            double *entropy);
XXWIDGETS_API size_t xxwidgets_entropy_region_count(const xxwidgets_entropy *view);
/* Current tab's visible table as CSV, or its diagram as standalone SVG.
 * required includes NUL; NULL/0 queries length, a short buffer is terminated.
 * SVG dimensions must be 160..16384 by 100..16384. No file is written here. */
XXWIDGETS_API xxwidgets_status xxwidgets_entropy_export_csv(const xxwidgets_entropy *view, char *buffer, size_t capacity, size_t *required);
XXWIDGETS_API xxwidgets_status xxwidgets_entropy_export_svg(const xxwidgets_entropy *view, int width, int height, char *buffer, size_t capacity,
                                                            size_t *required);
#ifdef __cplusplus
}
#endif
#endif
