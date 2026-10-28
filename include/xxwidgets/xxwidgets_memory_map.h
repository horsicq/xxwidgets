/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_MEMORY_MAP_H
#define XXWIDGETS_MEMORY_MAP_H
#include "xxwidgets/xxwidgets.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XXWIDGETS_MEMORY_MAP_MAX_RECORDS 1024U
#define XXWIDGETS_MEMORY_MAP_INVALID_ADDRESS UINT64_MAX
#define XXWIDGETS_MEMORY_MAP_VIRTUAL 1U
#define XXWIDGETS_MEMORY_MAP_INVISIBLE 2U
#define XXWIDGETS_MEMORY_MAP_MODE_DATA 1U
#define XXWIDGETS_MEMORY_MAP_MODE_SECTIONS 2U
#define XXWIDGETS_MEMORY_MAP_MODE_SEGMENTS 4U
typedef struct xxwidgets_memory_map_record {
    int64_t offset;         /* -1 for virtual-only ranges. */
    uint64_t address, size; /* UINT64_MAX address means unmapped. */
    const char *name, *type;
    unsigned flags; /* VIRTUAL | INVISIBLE; these are not access permissions. */
} xxwidgets_memory_map_record;
typedef struct xxwidgets_memory_map_data {
    const char *path, *format;
    uint64_t file_size, module_address, entry_point_address;
    const xxwidgets_memory_map_record *records;
    size_t count;
    /* Optional display metadata; NULL uses Unknown/defaults. */
    const char *bitness, *endianness, *architecture, *mode;
    unsigned int available_modes;
} xxwidgets_memory_map_data;
/* Read-only six-column table reusing the shared tabular backend. All setters
 * deep-copy strings and replace transactionally, selecting the first row.
 * At most 1024 records; names/types are valid UTF-8, <=512/128 bytes.
 * Range overflows are checked; the browser also checks physical file bounds.
 * Overlap/order is retained.
 * Getters borrow strings until replacement/destruction. All calls use UI thread. */
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_table_set_rows(xxwidgets_widget *, const xxwidgets_memory_map_record *, size_t count);
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_table_clear(xxwidgets_widget *);
XXWIDGETS_API size_t xxwidgets_memory_map_table_count(const xxwidgets_widget *);
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_table_get_row(const xxwidgets_widget *, size_t index, xxwidgets_memory_map_record *);
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_table_get_selection(xxwidgets_widget *, size_t *index, xxwidgets_memory_map_record *);
/* Four-column Offset/Address/Size/Name presentation. Raw records are retained. */
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_table_set_compact(xxwidgets_widget *, int compact);
typedef struct xxwidgets_memory_map xxwidgets_memory_map;
typedef enum xxwidgets_memory_map_control_id {
    XXWIDGETS_MEMORY_MAP_PATH,
    XXWIDGETS_MEMORY_MAP_SUMMARY,
    XXWIDGETS_MEMORY_MAP_METADATA,
    XXWIDGETS_MEMORY_MAP_FIND_LABEL,
    XXWIDGETS_MEMORY_MAP_FIND,
    XXWIDGETS_MEMORY_MAP_FIND_OFFSET,
    XXWIDGETS_MEMORY_MAP_FIND_ADDRESS,
    XXWIDGETS_MEMORY_MAP_TABLE,
    XXWIDGETS_MEMORY_MAP_DETAILS,
    XXWIDGETS_MEMORY_MAP_COPY,
    XXWIDGETS_MEMORY_MAP_COPY_ALL,
    XXWIDGETS_MEMORY_MAP_STATUS,
    XXWIDGETS_MEMORY_MAP_FORMAT,
    XXWIDGETS_MEMORY_MAP_MODE,
    XXWIDGETS_MEMORY_MAP_SHOW_ALL,
    XXWIDGETS_MEMORY_MAP_READONLY,
    XXWIDGETS_MEMORY_MAP_RELOAD,
    XXWIDGETS_MEMORY_MAP_DUMP_ALL,
    XXWIDGETS_MEMORY_MAP_SAVE,
    XXWIDGETS_MEMORY_MAP_RADIO_OFFSET,
    XXWIDGETS_MEMORY_MAP_RADIO_ADDRESS,
    XXWIDGETS_MEMORY_MAP_RADIO_RVA,
    XXWIDGETS_MEMORY_MAP_ADDRESS,
    XXWIDGETS_MEMORY_MAP_RVA,
    XXWIDGETS_MEMORY_MAP_FIND_RVA,
    XXWIDGETS_MEMORY_MAP_BITNESS_LABEL,
    XXWIDGETS_MEMORY_MAP_BITNESS,
    XXWIDGETS_MEMORY_MAP_ENDIAN_LABEL,
    XXWIDGETS_MEMORY_MAP_ENDIAN,
    XXWIDGETS_MEMORY_MAP_ARCH_LABEL,
    XXWIDGETS_MEMORY_MAP_ARCH,
    XXWIDGETS_MEMORY_MAP_HEX_LABEL,
    XXWIDGETS_MEMORY_MAP_HEX,
    XXWIDGETS_MEMORY_MAP_NO_DATA,
    XXWIDGETS_MEMORY_MAP_VERTICAL_SPLITTER,
    XXWIDGETS_MEMORY_MAP_HORIZONTAL_SPLITTER,
    XXWIDGETS_MEMORY_MAP_DUMP,
    XXWIDGETS_MEMORY_MAP_CONTROL_COUNT
} xxwidgets_memory_map_control_id;
/* Engine-independent snapshot browser; minimum 96x20 cells. Destroy before owner.
 * The host supplies parsing and an optional borrowed device for a bounded,
 * read-only hex preview. Model replacement copies metadata and rows.
 * NULL/0 records clear the snapshot. */
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_create(xxwidgets_widget *, xxwidgets_rect, xxwidgets_memory_map **);
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_destroy(xxwidgets_memory_map *);
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_set_rect(xxwidgets_memory_map *, xxwidgets_rect);
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_set_data(xxwidgets_memory_map *, const xxwidgets_memory_map_data *);
/* Optional borrowed read-only file device. It must outlive the view. Metadata
 * snapshots never open their path. NULL detaches/clears the preview. */
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_set_device(xxwidgets_memory_map *, xx_io_device *);
/* Replace metadata and borrowed source together, preserving both on failure. */
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_set_data_device(xxwidgets_memory_map *, const xxwidgets_memory_map_data *, xx_io_device *);
typedef xxwidgets_status (*xxwidgets_memory_map_reload_fn)(xxwidgets_memory_map *, const char *format, const char *mode, void *user);
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_set_reload_handler(xxwidgets_memory_map *, xxwidgets_memory_map_reload_fn, void *user);
XXWIDGETS_API xxwidgets_widget *xxwidgets_memory_map_control(const xxwidgets_memory_map *, xxwidgets_memory_map_control_id);
/* Decimal/0x lookup; half-open intervals. File lookup skips virtual-only rows.
 * Address lookup includes them. First matching input record wins on overlap.
 * Invalid/not-found lookups leave selection unchanged. address_lookup is 0 or 1. */
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_find(xxwidgets_memory_map *, const char *, int address_lookup);
/* Lookup kind 0=file offset, 1=VA, 2=RVA; uses decimal/0x notation like find. */
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_navigate(xxwidgets_memory_map *, const char *, int kind);
/* Stream exports without modifying the source or replacing existing files.
 * Save includes filtered rows; dump_all writes physical rows and a new patch
 * manifest in an existing directory. Calls use a joined disk-write worker and
 * modal progress; filtered metadata is snapshotted before the worker starts.
 * Keep the borrowed source exclusively available until the call returns. */
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_save(xxwidgets_memory_map *, const char *path);
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_dump(xxwidgets_memory_map *, xx_io_device *, size_t row, const char *path);
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_dump_all(xxwidgets_memory_map *, xx_io_device *, const char *directory);
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_choose_directory(xxwidgets_widget *, char **path, int *accepted);
/* index SIZE_MAX exports all rows as tab-separated, single-line escaped cells.
 * required includes NUL; NULL/0 queries size. Short buffers are terminated. */
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_export_text(const xxwidgets_memory_map *, size_t index, char *, size_t, size_t *required);
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_handle_event(xxwidgets_memory_map *, const xxwidgets_event *);
/* Call process after polling for navigation, reload, export and bounded search.
 * Location edits display hexadecimal. Their search buttons find the numeric
 * value in the source using the map's bitness and byte order. */
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_process(xxwidgets_memory_map *);
/* Resizable Close/Esc modal, Ctrl/Meta+G focuses lookup. Snapshot is copied. */
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_dialog(xxwidgets_widget *, const char *title, const xxwidgets_memory_map_data *);
XXWIDGETS_API xxwidgets_status xxwidgets_memory_map_dialog_with_device(xxwidgets_widget *, const char *title, const xxwidgets_memory_map_data *, xx_io_device *,
                                                                       xxwidgets_memory_map_reload_fn, void *user);
#ifdef __cplusplus
}
#endif
#endif
