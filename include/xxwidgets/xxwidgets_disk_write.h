/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_DISK_WRITE_H
#define XXWIDGETS_DISK_WRITE_H
#include "xxwidgets/xxwidgets.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XXWIDGETS_DISK_WRITE_PATH_CAPACITY 4096
typedef struct xxwidgets_disk_write_reporter xxwidgets_disk_write_reporter;
typedef struct xxwidgets_disk_write_progress {
    char current_file[XXWIDGETS_DISK_WRITE_PATH_CAPACITY];
    uint64_t bytes_done, bytes_total, files_done, files_total;
    int bytes_total_known, files_total_known;
} xxwidgets_disk_write_progress;
typedef struct xxwidgets_disk_write_result {
    xxwidgets_status status;    /* worker status, or setup failure before worker */
    xxwidgets_status ui_status; /* window/poll/cleanup status, separately */
    int cancelled;
    uint64_t elapsed_ms;
    xxwidgets_disk_write_progress progress;
    char error[256]; /* copied xx_pd error or a readable status fallback */
} xxwidgets_disk_write_result;
typedef xxwidgets_status (*xxwidgets_disk_write_worker_fn)(void *user, xx_pd_struct *progress, xxwidgets_disk_write_reporter *reporter);
/* Call only on the worker thread. Snapshots are copied under a mutex; no UI
 * calls are made here. Counts are cumulative for the complete operation.
 * known=1,total=0 is a known empty operation; known=0 means undetermined.
 * Caller metadata is not retained. Invalid values preserve the last snapshot. */
XXWIDGETS_API xxwidgets_status xxwidgets_disk_write_report(xxwidgets_disk_write_reporter *, const xxwidgets_disk_write_progress *);
XXWIDGETS_API xxwidgets_status xxwidgets_disk_write_set_file(xxwidgets_disk_write_reporter *, const char *path, uint64_t completed, uint64_t total, int known);
XXWIDGETS_API xxwidgets_status xxwidgets_disk_write_set_bytes(xxwidgets_disk_write_reporter *, uint64_t completed, uint64_t total, int known);
/* Optional mapping of xx_pd record indices to cumulative file/byte counts.
 * -1 disables that mapping. The observer retains the last published counters
 * when a nested level becomes idle. Map only records whose units match.
 * Explicit report/set helpers supersede mapped values until a later pd update. */
XXWIDGETS_API xxwidgets_status xxwidgets_disk_write_bind_levels(xxwidgets_disk_write_reporter *, int files_level, int bytes_level);
/* Optional bounded current-file label from one xx_pd record's status. The
 * provider should publish a safe filename/path there; -1 disables mapping. */
XXWIDGETS_API xxwidgets_status xxwidgets_disk_write_bind_current_file_level(xxwidgets_disk_write_reporter *, int level);
/* Managed worker + dedicated disk dialog: Destination, Current file, Files,
 * Bytes, two bars, elapsed time, speed and ETA. Long jobs appear after 350 ms;
 * short ones complete without flashing a window. Call outside event dispatch.
 * An existing modal is accepted only when it is owner; disabled or publicly
 * blocked owners return BUSY before a worker starts. Parent modality, focus,
 * enabled state, event handler and modal default are restored on every exit.
 * The worker must use xx_pd helpers to observe cancellation and must never
 * call widget APIs. It must check stop before committing outputs. Returning OK
 * means committed success, authoritative even if a final report observed a late
 * stop/UI failure. Actual cancellation must return UNAVAILABLE with xx_pd stop
 * set. Adapt legacy OK-plus-stop callbacks before returning from this worker.
 * Every exit waits for worker completion. Callback failures return copied error
 * text for the containing browser's inline status; no extra error modal opens.
 * cancelled returns OK with result.cancelled=1; failed writes return status.
 * NULL result is allowed. This helper does not choose paths or write files. */
XXWIDGETS_API xxwidgets_status xxwidgets_disk_write_run(xxwidgets_widget *owner, const char *title, const char *destination, xxwidgets_disk_write_worker_fn worker,
                                                        void *user, xxwidgets_disk_write_result *result);
#ifdef __cplusplus
}
#endif
#endif
