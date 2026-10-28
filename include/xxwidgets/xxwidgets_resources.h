/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_RESOURCES_H
#define XXWIDGETS_RESOURCES_H
#include "xxwidgets/xxwidget_resourcesviewer.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XXWIDGETS_RESOURCES_MAX_RECORDS 10000U
#define XXWIDGETS_RESOURCES_MAX_NAME 16384U
#define XXWIDGETS_RESOURCES_MAX_TEXT (8U * 1024U * 1024U)
typedef struct xxwidgets_resources xxwidgets_resources;
typedef struct xxwidgets_resources_data {
    const char *path, *format;
    uint64_t file_size;
    const xx_resource_record *records;
    size_t count;
} xxwidgets_resources_data;
typedef enum xxwidgets_resources_control_id {
    XXWIDGETS_RESOURCES_PATH,
    XXWIDGETS_RESOURCES_SUMMARY,
    /* Legacy global filter controls stay hidden; use the table column filters. */
    XXWIDGETS_RESOURCES_FILTER_LABEL,
    XXWIDGETS_RESOURCES_FILTER,
    XXWIDGETS_RESOURCES_RESET,
    XXWIDGETS_RESOURCES_TABLE,
    XXWIDGETS_RESOURCES_DETAILS,
    XXWIDGETS_RESOURCES_COPY,
    XXWIDGETS_RESOURCES_COPY_ALL,
    XXWIDGETS_RESOURCES_STATUS,
    XXWIDGETS_RESOURCES_CONTROL_COUNT
} xxwidgets_resources_control_id;
/* Reuses XXWIDGET_RESOURCESVIEWER. All calls use the UI thread. Bounds >=96x20.
 * Copies metadata and all records; no parser/device is owned or read. Up to
 * 10000 records, 16 KiB per UTF-8 type/name/language and 8 MiB aggregate NUL-inclusive names.
 * NULL names mean empty; UINT32_MAX IDs are unknown. Offset/size use -1 for unknown.
 * Known physical ranges must fit inside file_size.
 * Replacement is transactional and resets the programmatic global filter.
 * The visible table provides per-column filters. Destroy before owner. */
XXWIDGETS_API xxwidgets_status xxwidgets_resources_create(xxwidgets_widget *, xxwidgets_rect, xxwidgets_resources **);
XXWIDGETS_API xxwidgets_status xxwidgets_resources_destroy(xxwidgets_resources *);
XXWIDGETS_API xxwidgets_status xxwidgets_resources_set_rect(xxwidgets_resources *, xxwidgets_rect);
XXWIDGETS_API xxwidgets_status xxwidgets_resources_set_data(xxwidgets_resources *, const xxwidgets_resources_data *);
XXWIDGETS_API xxwidgets_widget *xxwidgets_resources_control(const xxwidgets_resources *, xxwidgets_resources_control_id);
XXWIDGETS_API size_t xxwidgets_resources_count(const xxwidgets_resources *);
XXWIDGETS_API size_t xxwidgets_resources_visible_count(const xxwidgets_resources *);
/* Substring in type/name/language (names or decimal IDs); ASCII case-insensitive, <=256 UTF-8 bytes.
 * Selects first visible row. Invalid input/failures retain previous filter. */
XXWIDGETS_API xxwidgets_status xxwidgets_resources_set_filter(xxwidgets_resources *, const char *);
/* TSV of rows passing the column and programmatic global filters. index SIZE_MAX
 * exports all visible rows; otherwise use a visible model row index from TABLE.
 * Cells escape controls; includes numeric IDs and code page as well as table fields.
 * NULL/0 queries NUL-inclusive size; short buffers are terminated. <=32 MiB. */
XXWIDGETS_API xxwidgets_status xxwidgets_resources_export_text(const xxwidgets_resources *, size_t index, char *, size_t, size_t *required);
XXWIDGETS_API xxwidgets_status xxwidgets_resources_handle_event(xxwidgets_resources *, const xxwidgets_event *);
/* Run queued filtering/clipboard work after polling, outside input dispatch. */
XXWIDGETS_API xxwidgets_status xxwidgets_resources_process(xxwidgets_resources *);
/* Snapshot modal with Close/Esc and Ctrl/Meta+F to focus the table. */
XXWIDGETS_API xxwidgets_status xxwidgets_resources_dialog(xxwidgets_widget *, const char *, const xxwidgets_resources_data *);
#ifdef __cplusplus
}
#endif
#endif
