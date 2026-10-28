/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_EXTRACTOR_H
#define XXWIDGETS_EXTRACTOR_H
#include "xxwidgets/xxwidgets.h"
#include "xxwidgets/xxwidgets_disk_write.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XXWIDGETS_EXTRACTOR_PAGE_SIZE 128
#define XXWIDGETS_EXTRACTOR_COLUMN_COUNT 9
typedef struct xxwidgets_extractor xxwidgets_extractor;
typedef enum xxwidgets_extractor_mode {
    XXWIDGETS_EXTRACTOR_HEURISTIC,
    XXWIDGETS_EXTRACTOR_FORMAT,
    XXWIDGETS_EXTRACTOR_RAW
} xxwidgets_extractor_mode;
typedef struct xxwidgets_extractor_options {
    size_t format, map_mode;
    xxwidgets_extractor_mode mode;
    uint64_t type_mask;
    int all_types, deep_scan;
} xxwidgets_extractor_options;
typedef struct xxwidgets_extractor_record {
    uint64_t offset, size, address;
    int has_address;
    const char *region, *method, *type, *info, *name;
    uint64_t number; /* original zero-based scan ordinal, stable under filters */
} xxwidgets_extractor_record;
typedef struct xxwidgets_extractor_source {
    uint64_t count;
    /* read returns exactly capacity records from the committed filtered index.
     * capacity is at most PAGE_SIZE. Strings are borrowed until the next read;
     * the browser copies the visible page. Called on the UI thread. */
    xxwidgets_status (*read)(void *user, uint64_t first, size_t capacity, xxwidgets_extractor_record *records, size_t *received);
    /* Worker callbacks must never call widget APIs. scan applies all column
     * filters to the complete result set, and commits its index only after a
     * final cancellation check. Errors/stop preserve the preceding index.
     * Options are a private staged copy; count/options publish on success. */
    xxwidgets_status (*scan)(void *user, xxwidgets_extractor_options *options, const char *const columns[XXWIDGETS_EXTRACTOR_COLUMN_COUNT], xx_pd_struct *progress,
                             uint64_t *count);
    /* directory=1 and indices=NULL/count=0 means every committed filtered row.
     * Otherwise destination is one file and indices contains one source index.
     * Host owns safe filenames, decoding and atomic writes. Never trust member
     * names as destination paths. Cancellation may leave completed outputs. */
    xxwidgets_status (*dump)(void *user, const char *destination, int directory, const uint64_t *indices, size_t count, xx_pd_struct *progress);
    xxwidgets_status (*save)(void *user, const char *path, xx_pd_struct *progress);
    const char *(*error)(void *user);
    void *user;
    const char *const *formats, *const *map_modes, *const *types;
    size_t format_count, map_mode_count, type_count; /* at most 64 types */
    xxwidgets_extractor_options options;
    xx_io_device *device; /* optional borrowed read-only source for linked Hex */
    uint64_t size;
    /* Optional per-format masks, one byte per format label. Bits are 1u<<mode;
     * NULL offers all three modes. A format switch retains the staged mode
     * when supported, otherwise selects Heuristic (or the first offered mode).
     * Providers keep these borrowed masks valid for the browser's lifetime. */
    const unsigned char *format_mode_masks;
    /* Optional disk-aware callbacks supersede the legacy callbacks above.
     * Run on the disk dialog worker; report actual bytes and current filename
     * through reporter. Atomic publication and cancellation remain host-owned. */
    xxwidgets_status (*save_ex)(void *user, const char *path, xx_pd_struct *progress, xxwidgets_disk_write_reporter *reporter);
    xxwidgets_status (*dump_ex)(void *user, const char *destination, int directory, const uint64_t *indices, size_t count,
        xx_pd_struct *progress, xxwidgets_disk_write_reporter *reporter);
} xxwidgets_extractor_source;
typedef enum xxwidgets_extractor_control_id {
    XXWIDGETS_EXTRACTOR_FORMAT_SELECTOR,
    XXWIDGETS_EXTRACTOR_MAP_SELECTOR,
    XXWIDGETS_EXTRACTOR_SUMMARY,
    XXWIDGETS_EXTRACTOR_DUMP_ALL,
    XXWIDGETS_EXTRACTOR_SAVE,
    XXWIDGETS_EXTRACTOR_SCAN,
    XXWIDGETS_EXTRACTOR_MODE_SELECTOR,
    XXWIDGETS_EXTRACTOR_ALL,
    XXWIDGETS_EXTRACTOR_TYPES,
    XXWIDGETS_EXTRACTOR_DEEP,
    XXWIDGETS_EXTRACTOR_TABLE,
    XXWIDGETS_EXTRACTOR_HEX_LABEL,
    XXWIDGETS_EXTRACTOR_HEX,
    XXWIDGETS_EXTRACTOR_STATUS,
    XXWIDGETS_EXTRACTOR_PREVIOUS,
    XXWIDGETS_EXTRACTOR_PAGE,
    XXWIDGETS_EXTRACTOR_GO,
    XXWIDGETS_EXTRACTOR_NEXT,
    XXWIDGETS_EXTRACTOR_DUMP,
    XXWIDGETS_EXTRACTOR_CONTROL_COUNT
} xxwidgets_extractor_control_id;
XXWIDGETS_API void xxwidgets_extractor_options_init(xxwidgets_extractor_options *);
/* Reusable native composite, minimum 118x30 cells. Borrow source/user/device
 * until destroy; destroy before owner. Forward events then call process after
 * polling. No global Filter: each of the nine columns has its own filter.
 * Changing a filter schedules a worker scan; controls stage until Scan. */
XXWIDGETS_API xxwidgets_status xxwidgets_extractor_create(xxwidgets_widget *owner, xxwidgets_rect, xxwidgets_extractor_source *, xxwidgets_extractor **);
XXWIDGETS_API xxwidgets_status xxwidgets_extractor_destroy(xxwidgets_extractor *);
XXWIDGETS_API xxwidgets_status xxwidgets_extractor_set_rect(xxwidgets_extractor *, xxwidgets_rect);
XXWIDGETS_API xxwidgets_widget *xxwidgets_extractor_control(const xxwidgets_extractor *, xxwidgets_extractor_control_id);
XXWIDGETS_API xxwidgets_status xxwidgets_extractor_handle_event(xxwidgets_extractor *, const xxwidgets_event *);
XXWIDGETS_API xxwidgets_status xxwidgets_extractor_process(xxwidgets_extractor *);
/* Programmatic page load, preserving preceding rows on provider errors. */
XXWIDGETS_API xxwidgets_status xxwidgets_extractor_set_page(xxwidgets_extractor *, uint64_t first);
/* Resizable, owned native modal with Close/Esc; source is initially displayed
 * as supplied. Set count=0 and call scan before opening, or use auto_scan=1. */
XXWIDGETS_API xxwidgets_status xxwidgets_extractor_dialog(xxwidgets_widget *owner, const char *title, xxwidgets_extractor_source *, int auto_scan);
/* Cancellable worker entry point, callable outside dispatch. */
XXWIDGETS_API xxwidgets_status xxwidgets_extractor_source_scan(xxwidgets_widget *owner, xxwidgets_extractor_source *, const xxwidgets_extractor_options *,
                                                               const char *const columns[XXWIDGETS_EXTRACTOR_COLUMN_COUNT], int *cancelled);
#ifdef __cplusplus
}
#endif
#endif
