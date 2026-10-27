/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_DISASM_H
#define XXWIDGETS_DISASM_H
#include "xxwidgets/xxwidgets_debugger_tables.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XXWIDGETS_DISASM_MAX_ROWS 512U
typedef struct xxwidgets_disasm_record {
    uint64_t offset, address;
    unsigned size;
    const char *bytes, *instruction, *comment;
} xxwidgets_disasm_record;
typedef struct xxwidgets_disasm_page {
    uint64_t file_size, offset, next_offset;
    const xxwidgets_disasm_record *records;
    size_t count;
} xxwidgets_disasm_page;
/* Called outside input dispatch. Output is borrowed until the next load;
 * the browser deep-copies strings before then. UNAVAILABLE requests manual
 * mode selection. Other failures retain the installed page/history/mode. */
typedef xxwidgets_status (*xxwidgets_disasm_load_fn)(void *user, uint64_t offset, size_t mode, xxwidgets_disasm_page *page);
typedef struct xxwidgets_disasm_source {
    const char *path, *description;
    uint64_t file_size, initial_offset;
    const char *const *mode_names; /* 1..16 printable ASCII labels, <=31 bytes */
    size_t mode_count, initial_mode;
    xxwidgets_disasm_load_fn load;
    void *user; /* Borrowed until browser/dialog destruction. */
} xxwidgets_disasm_source;
typedef struct xxwidgets_disasm_browser xxwidgets_disasm_browser;
typedef enum xxwidgets_disasm_control_id {
    XXWIDGETS_DISASM_PATH,
    XXWIDGETS_DISASM_SUMMARY,
    XXWIDGETS_DISASM_OFFSET_LABEL,
    XXWIDGETS_DISASM_OFFSET,
    XXWIDGETS_DISASM_GO,
    XXWIDGETS_DISASM_BACK,
    XXWIDGETS_DISASM_NEXT,
    XXWIDGETS_DISASM_REFRESH,
    XXWIDGETS_DISASM_MODE_LABEL,
    XXWIDGETS_DISASM_MODE,
    XXWIDGETS_DISASM_COPY,
    XXWIDGETS_DISASM_TABLE,
    XXWIDGETS_DISASM_STATUS,
    XXWIDGETS_DISASM_CONTROL_COUNT
} xxwidgets_disasm_control_id;
/* Engine-independent browser using the existing DEBUGGER_DISASM table. No
 * decoder, device or process is owned. Bounds >=96x20 cells. Initial
 * UNAVAILABLE is displayed as a mode prompt; initial I/O/allocation errors fail. */
XXWIDGETS_API xxwidgets_status xxwidgets_disasm_browser_create(xxwidgets_widget *owner, xxwidgets_rect bounds,
                                                               const xxwidgets_disasm_source *source, xxwidgets_disasm_browser **out);
XXWIDGETS_API xxwidgets_status xxwidgets_disasm_browser_destroy(xxwidgets_disasm_browser *browser);
XXWIDGETS_API xxwidgets_status xxwidgets_disasm_browser_set_rect(xxwidgets_disasm_browser *browser, xxwidgets_rect bounds);
XXWIDGETS_API xxwidgets_widget *xxwidgets_disasm_browser_control(const xxwidgets_disasm_browser *browser, xxwidgets_disasm_control_id id);
XXWIDGETS_API size_t xxwidgets_disasm_browser_count(const xxwidgets_disasm_browser *browser);
XXWIDGETS_API xxwidgets_status xxwidgets_disasm_browser_get_record(const xxwidgets_disasm_browser *browser, size_t index,
                                                                   xxwidgets_disasm_record *record);
/* Decimal / 0x physical offset. Go/mode changes clear bounded Back history;
 * Next preserves instruction boundaries supplied by the provider. */
XXWIDGETS_API xxwidgets_status xxwidgets_disasm_browser_go_to(xxwidgets_disasm_browser *browser, const char *offset);
XXWIDGETS_API xxwidgets_status xxwidgets_disasm_browser_handle_event(xxwidgets_disasm_browser *browser, const xxwidgets_event *event);
XXWIDGETS_API xxwidgets_status xxwidgets_disasm_browser_process(xxwidgets_disasm_browser *browser);
XXWIDGETS_API xxwidgets_status xxwidgets_disasm_dialog(xxwidgets_widget *owner, const char *title, const xxwidgets_disasm_source *source);
#ifdef __cplusplus
}
#endif
#endif
