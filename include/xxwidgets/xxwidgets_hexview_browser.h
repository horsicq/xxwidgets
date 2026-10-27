/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_HEXVIEW_BROWSER_H
#define XXWIDGETS_HEXVIEW_BROWSER_H
#include "xxwidgets/xxwidgets.h"
#include "xxwidgets/xxwidgets_hex_options.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XXWIDGETS_HEXVIEW_BROWSER_WINDOW 65536U
#define XXWIDGETS_HEXVIEW_BROWSER_COPY_LIMIT 1048576U
typedef struct xxwidgets_hexview_browser xxwidgets_hexview_browser;
typedef enum xxwidgets_hexview_browser_control_id {
    XXWIDGETS_HEX_BROWSER_PATH, XXWIDGETS_HEX_BROWSER_SUMMARY,
    XXWIDGETS_HEX_BROWSER_OFFSET_LABEL, XXWIDGETS_HEX_BROWSER_OFFSET,
    XXWIDGETS_HEX_BROWSER_GO, XXWIDGETS_HEX_BROWSER_PREVIOUS, XXWIDGETS_HEX_BROWSER_NEXT,
    XXWIDGETS_HEX_BROWSER_REFRESH, XXWIDGETS_HEX_BROWSER_ENCODING_LABEL, XXWIDGETS_HEX_BROWSER_ENCODING,
    XXWIDGETS_HEX_BROWSER_ROWS_LABEL, XXWIDGETS_HEX_BROWSER_ROWS,
    XXWIDGETS_HEX_BROWSER_COPY_HEX, XXWIDGETS_HEX_BROWSER_COPY_TEXT, XXWIDGETS_HEX_BROWSER_DUMP,
    XXWIDGETS_HEX_BROWSER_HEX, XXWIDGETS_HEX_BROWSER_STATUS, XXWIDGETS_HEX_BROWSER_CONTROL_COUNT
} xxwidgets_hexview_browser_control_id;
/* Read-only browser composed around the existing windowed HEXVIEW. Bounds
 * >=96x20 cells. Destroy before owner, and before closing a borrowed device.
 * File parsing/recognition is not part of this component. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_browser_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_hexview_browser **out);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_browser_destroy(xxwidgets_hexview_browser *browser);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_browser_set_rect(xxwidgets_hexview_browser *browser, xxwidgets_rect bounds);
XXWIDGETS_API xxwidgets_widget *xxwidgets_hexview_browser_control(const xxwidgets_hexview_browser *browser, xxwidgets_hexview_browser_control_id id);
/* Copies path; borrows a seekable, known-size device. Opens no file and never
 * closes the device. The host should open it read-only. Failure preserves the
 * old device, bytes and metadata. Only a bounded 64 KiB window is retained. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_browser_set_device(xxwidgets_hexview_browser *browser, const char *path, xx_io_device *device);
/* Decimal or 0x-prefixed hex offset, with surrounding ASCII whitespace. Signs,
 * overflow, trailing junk and positions beyond EOF fail without changing data.
 * EOF is valid and displays an empty window. No input events are emitted. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_browser_go_to(xxwidgets_hexview_browser *browser, const char *offset);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_browser_refresh(xxwidgets_hexview_browser *browser);
/* Forward events, then call process after app_poll/input dispatch. File reads,
 * clipboard copies and native export dialogs are deferred until process.
 * Input read/validation failures are shown in Status and keep the prior bytes.
 * Copies are limited to 1 MiB of selected bytes; byte export remains streamed. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_browser_handle_event(xxwidgets_hexview_browser *browser, const xxwidgets_event *event);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_browser_process(xxwidgets_hexview_browser *browser);
/* Borrowed device stays open on every exit. Resizable modal with Go, paging,
 * Refresh/F5, Copy, encoding/row controls, byte export and Close/Esc. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_dialog(xxwidgets_widget *owner, const char *title, const char *path, xx_io_device *device);
/* Applies HexView preferences: bytes per row, bold non-zero units and the
 * font (backends without per-view fonts keep theirs). editable is ignored;
 * the browser stays read-only. Emits no input events. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_browser_set_options(xxwidgets_hexview_browser *browser, const xxwidgets_hex_options *options);
/* As xxwidgets_hexview_dialog, opening with options; NULL uses the defaults. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_dialog_with_options(xxwidgets_widget *owner, const char *title, const char *path,
                                                                     xx_io_device *device, const xxwidgets_hex_options *options);
#ifdef __cplusplus
}
#endif
#endif
