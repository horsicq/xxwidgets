/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_SCAN_DIRECTORY_H
#define XXWIDGETS_SCAN_DIRECTORY_H
#include "xxwidgets/xxwidgets_scan_options.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_scan_directory xxwidgets_scan_directory;
typedef struct xxwidgets_scan_directory_task xxwidgets_scan_directory_task;
typedef enum xxwidgets_scan_directory_control_id {
    XXWIDGETS_SCAN_DIRECTORY_LABEL = 0,
    XXWIDGETS_SCAN_DIRECTORY_PATH,
    XXWIDGETS_SCAN_DIRECTORY_RECURSIVE,
    XXWIDGETS_SCAN_DIRECTORY_OPTIONS,
    XXWIDGETS_SCAN_DIRECTORY_SCAN,
    XXWIDGETS_SCAN_DIRECTORY_STOP,
    XXWIDGETS_SCAN_DIRECTORY_STATUS,
    XXWIDGETS_SCAN_DIRECTORY_CURRENT_FILE,
    XXWIDGETS_SCAN_DIRECTORY_RESULTS,
    XXWIDGETS_SCAN_DIRECTORY_CONTROL_COUNT
} xxwidgets_scan_directory_control_id;
typedef enum xxwidgets_scan_directory_state {
    XXWIDGETS_SCAN_DIRECTORY_IDLE = 0,
    XXWIDGETS_SCAN_DIRECTORY_RUNNING,
    XXWIDGETS_SCAN_DIRECTORY_FINISHED,
    XXWIDGETS_SCAN_DIRECTORY_CANCELLED,
    XXWIDGETS_SCAN_DIRECTORY_FAILED
} xxwidgets_scan_directory_state;
typedef struct xxwidgets_scan_directory_progress {
    xxwidgets_scan_directory_state state;
    size_t files_scanned, files_failed, directories_failed, entries_skipped, detections;
    xxwidgets_status status; /* Fatal error only; individual errors stay in results. */
} xxwidgets_scan_directory_progress;
typedef struct xxwidgets_scan_directory_result {
    const char *path;
    int is_directory; /* Directory enumeration error; no file callback ran. */
    xxwidgets_status status;
    const xxwidgets_scan_result *detections;
    size_t detection_count;
} xxwidgets_scan_directory_result;

/* Called serially on a background thread for each regular file. Options are a
 * snapshot. Use emit to copy zero or more detections, and check cancelled during
 * long scans. Return a per-file error to record it and continue with other files.
 * OUT_OF_MEMORY stops the entire job. Do not call widget APIs from this callback.
 * task, path and options are borrowed only for the callback; user must outlive
 * the job. This follows ScanPanel's application-supplied engine model.
 */
typedef xxwidgets_status (*xxwidgets_scan_directory_file_fn)(
    xxwidgets_scan_directory_task *task, const char *path, const xxwidgets_scan_options *options, void *user);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_directory_emit(xxwidgets_scan_directory_task *task, const xxwidgets_scan_result *detection);
XXWIDGETS_API int xxwidgets_scan_directory_cancelled(const xxwidgets_scan_directory_task *task);

/* Native/TUI composite: path edit, recursive option (on by default), Options,
 * Scan/Stop buttons, counters/current file and a file/detection tree. Bounds
 * require 64 x 15 cells. Set the scanner before starting. Children belong to the
 * owner WINDOW. Destroy this object before its owner/app, never its children.
 * All functions except emit/cancelled are UI-thread-only.
 */
XXWIDGETS_API xxwidgets_status xxwidgets_scan_directory_create(
    xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_scan_directory **out);
/* Returns BUSY while the worker runs. Cancel then poll until no longer RUNNING
 * before destruction. Cancellation is cooperative; no worker is forcibly killed. */
XXWIDGETS_API xxwidgets_status xxwidgets_scan_directory_destroy(xxwidgets_scan_directory *view);
XXWIDGETS_API xxwidgets_widget *xxwidgets_scan_directory_control(
    const xxwidgets_scan_directory *view, xxwidgets_scan_directory_control_id id);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_directory_set_rect(xxwidgets_scan_directory *view, xxwidgets_rect bounds);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_directory_set_scanner(
    xxwidgets_scan_directory *view, xxwidgets_scan_directory_file_fn scanner, void *user);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_directory_set_path(xxwidgets_scan_directory *view, const char *path);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_directory_set_options(xxwidgets_scan_directory *view, const xxwidgets_scan_options *options);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_directory_get_options(const xxwidgets_scan_directory *view, xxwidgets_scan_options *options);
/* Successful start replaces previous results and snapshots path/options. Hidden
 * entries are included. Descendant symlinks/reparse points and special files
 * are skipped, preventing normal link cycles. Order is filesystem enumeration
 * order. The explicitly selected root may itself be a directory link.
 */
XXWIDGETS_API xxwidgets_status xxwidgets_scan_directory_start(xxwidgets_scan_directory *view);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_directory_cancel(xxwidgets_scan_directory *view);
/* Forward app events to handle_event. Call poll after xxwidgets_app_poll, even
 * after cancellation, to drain the bounded queue and finish/reap the worker.
 * poll performs queued Scan/Options actions outside event dispatch. */
XXWIDGETS_API xxwidgets_status xxwidgets_scan_directory_handle_event(xxwidgets_scan_directory *view, const xxwidgets_event *event);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_directory_poll(xxwidgets_scan_directory *view);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_directory_get_progress(
    const xxwidgets_scan_directory *view, xxwidgets_scan_directory_progress *progress);
XXWIDGETS_API size_t xxwidgets_scan_directory_result_count(const xxwidgets_scan_directory *view);
/* Borrowed strings/detections survive polling; invalidated by start or destroy.
 * Rows include files with no detections and file/directory errors. */
XXWIDGETS_API xxwidgets_status xxwidgets_scan_directory_get_result(
    const xxwidgets_scan_directory *view, size_t index, xxwidgets_scan_directory_result *result);
#ifdef __cplusplus
}
#endif
#endif
