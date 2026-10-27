/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_IMPORTS_H
#define XXWIDGETS_IMPORTS_H
#include "xxwidgets/xxwidget_importsviewer.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XXWIDGETS_IMPORTS_MAX_RECORDS 10000U
#define XXWIDGETS_IMPORTS_MAX_NAME 16384U
#define XXWIDGETS_IMPORTS_MAX_TEXT (8U * 1024U * 1024U)
typedef struct xxwidgets_imports xxwidgets_imports;
typedef struct xxwidgets_imports_data {
    const char *path, *format;
    uint64_t file_size;
    const xx_import_record *records;
    size_t count;
} xxwidgets_imports_data;
typedef enum xxwidgets_imports_control_id {
    XXWIDGETS_IMPORTS_PATH,
    XXWIDGETS_IMPORTS_SUMMARY,
    XXWIDGETS_IMPORTS_FILTER_LABEL,
    XXWIDGETS_IMPORTS_FILTER,
    XXWIDGETS_IMPORTS_RESET,
    XXWIDGETS_IMPORTS_TABLE,
    XXWIDGETS_IMPORTS_DETAILS,
    XXWIDGETS_IMPORTS_COPY,
    XXWIDGETS_IMPORTS_COPY_ALL,
    XXWIDGETS_IMPORTS_STATUS,
    XXWIDGETS_IMPORTS_CONTROL_COUNT
} xxwidgets_imports_control_id;
/* Reuses XXWIDGET_IMPORTSVIEWER. All calls use the UI thread. Bounds >=96x20.
 * Copies metadata and all records; no parser/device is owned or read. Up to
 * 10000 records, 16 KiB per UTF-8 name and 8 MiB aggregate NUL-inclusive names.
 * NULL names mean empty; offsets use -1 and addresses UINT64_MAX for unknown.
 * Replacement is transactional and resets the filter. Destroy before owner. */
XXWIDGETS_API xxwidgets_status xxwidgets_imports_create(xxwidgets_widget *, xxwidgets_rect, xxwidgets_imports **);
XXWIDGETS_API xxwidgets_status xxwidgets_imports_destroy(xxwidgets_imports *);
XXWIDGETS_API xxwidgets_status xxwidgets_imports_set_rect(xxwidgets_imports *, xxwidgets_rect);
XXWIDGETS_API xxwidgets_status xxwidgets_imports_set_data(xxwidgets_imports *, const xxwidgets_imports_data *);
XXWIDGETS_API xxwidgets_widget *xxwidgets_imports_control(const xxwidgets_imports *, xxwidgets_imports_control_id);
XXWIDGETS_API size_t xxwidgets_imports_count(const xxwidgets_imports *);
XXWIDGETS_API size_t xxwidgets_imports_visible_count(const xxwidgets_imports *);
/* Substring in library/name; ASCII case-insensitive, <=256 UTF-8 bytes.
 * Selects first visible row. Invalid input/failures retain previous filter. */
XXWIDGETS_API xxwidgets_status xxwidgets_imports_set_filter(xxwidgets_imports *, const char *);
/* TSV of visible rows. index SIZE_MAX exports all, otherwise visible row index.
 * Cells escape control bytes; includes offset/hint in addition to table fields.
 * NULL/0 queries NUL-inclusive size; short buffers are terminated. <=32 MiB. */
XXWIDGETS_API xxwidgets_status xxwidgets_imports_export_text(const xxwidgets_imports *, size_t index, char *, size_t, size_t *required);
XXWIDGETS_API xxwidgets_status xxwidgets_imports_handle_event(xxwidgets_imports *, const xxwidgets_event *);
/* Run queued filtering/clipboard work after polling, outside input dispatch. */
XXWIDGETS_API xxwidgets_status xxwidgets_imports_process(xxwidgets_imports *);
/* Snapshot modal with Close/Esc and Ctrl/Meta+F to focus Filter. */
XXWIDGETS_API xxwidgets_status xxwidgets_imports_dialog(xxwidgets_widget *, const char *, const xxwidgets_imports_data *);
#ifdef __cplusplus
}
#endif
#endif
