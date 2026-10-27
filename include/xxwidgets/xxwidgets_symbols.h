/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_SYMBOLS_H
#define XXWIDGETS_SYMBOLS_H
#include "xxwidgets/xxwidget_symbolsviewer.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XXWIDGETS_SYMBOLS_MAX_RECORDS 10000U
#define XXWIDGETS_SYMBOLS_MAX_NAME 16384U
#define XXWIDGETS_SYMBOLS_MAX_TEXT (8U * 1024U * 1024U)
typedef struct xxwidgets_symbols xxwidgets_symbols;
typedef struct xxwidgets_symbols_data {
    const char *path, *format;
    uint64_t file_size;
    const xx_symbol_record *records;
    size_t count;
} xxwidgets_symbols_data;
typedef enum xxwidgets_symbols_control_id {
    XXWIDGETS_SYMBOLS_PATH,
    XXWIDGETS_SYMBOLS_SUMMARY,
    XXWIDGETS_SYMBOLS_FILTER_LABEL,
    XXWIDGETS_SYMBOLS_FILTER,
    XXWIDGETS_SYMBOLS_RESET,
    XXWIDGETS_SYMBOLS_TABLE,
    XXWIDGETS_SYMBOLS_DETAILS,
    XXWIDGETS_SYMBOLS_COPY,
    XXWIDGETS_SYMBOLS_COPY_ALL,
    XXWIDGETS_SYMBOLS_STATUS,
    XXWIDGETS_SYMBOLS_CONTROL_COUNT
} xxwidgets_symbols_control_id;
/* Reuses XXWIDGET_SYMBOLSVIEWER. All calls use the UI thread. Bounds >=96x20.
 * Copies metadata and all records; no parser/device is owned or read. Up to
 * 10000 records, 16 KiB per UTF-8 name/file name and 8 MiB aggregate NUL-inclusive names.
 * NULL names mean empty. Offsets use -1 and address/table index UINT64_MAX for unknown.
 * Known file offsets must lie within the file; a target at EOF is allowed.
 * Replacement is transactional and resets the filter. Destroy before owner. */
XXWIDGETS_API xxwidgets_status xxwidgets_symbols_create(xxwidgets_widget *, xxwidgets_rect, xxwidgets_symbols **);
XXWIDGETS_API xxwidgets_status xxwidgets_symbols_destroy(xxwidgets_symbols *);
XXWIDGETS_API xxwidgets_status xxwidgets_symbols_set_rect(xxwidgets_symbols *, xxwidgets_rect);
XXWIDGETS_API xxwidgets_status xxwidgets_symbols_set_data(xxwidgets_symbols *, const xxwidgets_symbols_data *);
XXWIDGETS_API xxwidgets_widget *xxwidgets_symbols_control(const xxwidgets_symbols *, xxwidgets_symbols_control_id);
XXWIDGETS_API size_t xxwidgets_symbols_count(const xxwidgets_symbols *);
XXWIDGETS_API size_t xxwidgets_symbols_visible_count(const xxwidgets_symbols *);
/* Substring in name/file name; ASCII case-insensitive, <=256 UTF-8 bytes.
 * Selects first visible row. Invalid input/failures retain previous filter. */
XXWIDGETS_API xxwidgets_status xxwidgets_symbols_set_filter(xxwidgets_symbols *, const char *);
/* TSV of visible rows. index SIZE_MAX exports all, otherwise visible row index.
 * Cells escape control bytes; includes native metadata in addition to table fields.
 * NULL/0 queries NUL-inclusive size; short buffers are terminated. <=32 MiB. */
XXWIDGETS_API xxwidgets_status xxwidgets_symbols_export_text(const xxwidgets_symbols *, size_t index, char *, size_t, size_t *required);
XXWIDGETS_API xxwidgets_status xxwidgets_symbols_handle_event(xxwidgets_symbols *, const xxwidgets_event *);
/* Run queued filtering/clipboard work after polling, outside input dispatch. */
XXWIDGETS_API xxwidgets_status xxwidgets_symbols_process(xxwidgets_symbols *);
/* Snapshot modal with Close/Esc and Ctrl/Meta+F to focus Filter. */
XXWIDGETS_API xxwidgets_status xxwidgets_symbols_dialog(xxwidgets_widget *, const char *, const xxwidgets_symbols_data *);
#ifdef __cplusplus
}
#endif
#endif
