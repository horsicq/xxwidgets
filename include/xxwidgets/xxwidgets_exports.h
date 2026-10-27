/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_EXPORTS_H
#define XXWIDGETS_EXPORTS_H
#include "xxwidgets/xxwidget_exportsviewer.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XXWIDGETS_EXPORTS_MAX_RECORDS 10000U
#define XXWIDGETS_EXPORTS_MAX_NAME 16384U
#define XXWIDGETS_EXPORTS_MAX_TEXT (8U * 1024U * 1024U)
typedef struct xxwidgets_exports xxwidgets_exports;
typedef struct xxwidgets_exports_data {
    const char *path, *format;
    uint64_t file_size;
    const xx_export_record *records;
    size_t count;
} xxwidgets_exports_data;
typedef enum xxwidgets_exports_control_id {
    XXWIDGETS_EXPORTS_PATH,
    XXWIDGETS_EXPORTS_SUMMARY,
    XXWIDGETS_EXPORTS_FILTER_LABEL,
    XXWIDGETS_EXPORTS_FILTER,
    XXWIDGETS_EXPORTS_RESET,
    XXWIDGETS_EXPORTS_TABLE,
    XXWIDGETS_EXPORTS_DETAILS,
    XXWIDGETS_EXPORTS_COPY,
    XXWIDGETS_EXPORTS_COPY_ALL,
    XXWIDGETS_EXPORTS_STATUS,
    XXWIDGETS_EXPORTS_CONTROL_COUNT
} xxwidgets_exports_control_id;
/* Reuses XXWIDGET_EXPORTSVIEWER. All calls use the UI thread. Bounds >=96x20.
 * Copies metadata and all records; no parser/device is owned or read. Up to
 * 10000 records, 16 KiB per UTF-8 name/forwarder and 8 MiB aggregate NUL-inclusive names.
 * NULL names/forwarders mean empty; UINT64_MAX ordinal means unknown; offsets use -1 and addresses UINT64_MAX for unknown.
 * Replacement is transactional and resets the filter. Destroy before owner. */
XXWIDGETS_API xxwidgets_status xxwidgets_exports_create(xxwidgets_widget *, xxwidgets_rect, xxwidgets_exports **);
XXWIDGETS_API xxwidgets_status xxwidgets_exports_destroy(xxwidgets_exports *);
XXWIDGETS_API xxwidgets_status xxwidgets_exports_set_rect(xxwidgets_exports *, xxwidgets_rect);
XXWIDGETS_API xxwidgets_status xxwidgets_exports_set_data(xxwidgets_exports *, const xxwidgets_exports_data *);
XXWIDGETS_API xxwidgets_widget *xxwidgets_exports_control(const xxwidgets_exports *, xxwidgets_exports_control_id);
XXWIDGETS_API size_t xxwidgets_exports_count(const xxwidgets_exports *);
XXWIDGETS_API size_t xxwidgets_exports_visible_count(const xxwidgets_exports *);
/* Substring in name/forwarder; ASCII case-insensitive, <=256 UTF-8 bytes.
 * Selects first visible row. Invalid input/failures retain previous filter. */
XXWIDGETS_API xxwidgets_status xxwidgets_exports_set_filter(xxwidgets_exports *, const char *);
/* TSV of visible rows. index SIZE_MAX exports all, otherwise visible row index.
 * Cells escape control bytes; includes file offset in addition to table fields.
 * NULL/0 queries NUL-inclusive size; short buffers are terminated. <=32 MiB. */
XXWIDGETS_API xxwidgets_status xxwidgets_exports_export_text(const xxwidgets_exports *, size_t index, char *, size_t, size_t *required);
XXWIDGETS_API xxwidgets_status xxwidgets_exports_handle_event(xxwidgets_exports *, const xxwidgets_event *);
/* Run queued filtering/clipboard work after polling, outside input dispatch. */
XXWIDGETS_API xxwidgets_status xxwidgets_exports_process(xxwidgets_exports *);
/* Snapshot modal with Close/Esc and Ctrl/Meta+F to focus Filter. */
XXWIDGETS_API xxwidgets_status xxwidgets_exports_dialog(xxwidgets_widget *, const char *, const xxwidgets_exports_data *);
#ifdef __cplusplus
}
#endif
#endif
