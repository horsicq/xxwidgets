/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXWIDGET_SYSTEMPROCESSESVIEWER_H
#define XXWIDGET_SYSTEMPROCESSESVIEWER_H

#include "xxwidgets/xxwidgets.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_process_info {
    const char *name; /* UTF-8 process display name. */
    uint64_t pid;
    const char *path; /* UTF-8 executable path; empty when inaccessible. */
    uint64_t start_time; /* OS identity token; zero when unavailable. */
} xxwidgets_process_info;

/* Create XXWIDGET_SYSTEMPROCESSESVIEWER with widget_create for native Name/PID
 * columns or the same selectable table in TUI. Replacement deep-copies all
 * strings, keeps a selected PID/start_time identity when it still exists,
 * and otherwise selects the first row. Programmatic updates emit no events.
 * Returned records are borrowed until replacement/refresh/destruction. */
XXWIDGETS_API xxwidgets_status xxwidget_systemprocessesviewer_set_processes(xxwidgets_widget *, const xxwidgets_process_info *, size_t count);
XXWIDGETS_API xxwidgets_status xxwidget_systemprocessesviewer_clear(xxwidgets_widget *);
XXWIDGETS_API size_t xxwidget_systemprocessesviewer_count(const xxwidgets_widget *);
XXWIDGETS_API xxwidgets_status xxwidget_systemprocessesviewer_get_process(const xxwidgets_widget *, size_t index, xxwidgets_process_info *);
XXWIDGETS_API xxwidgets_status xxwidget_systemprocessesviewer_get_selection(xxwidgets_widget *, size_t *index, xxwidgets_process_info *);
XXWIDGETS_API xxwidgets_status xxwidget_systemprocessesviewer_refresh(xxwidgets_widget *);

/* Standalone OS adapter. Enumeration owns its strings and array; free using
 * free_processes. Protected/vanished processes may have empty paths/identity.
 * kill_process requires the recorded identity to still match, refuses the
 * current process and PID 0/1, and sends SIGTERM (SIGKILL when force != 0) on
 * POSIX or TerminateProcess on Windows. Opening uses OS arguments, no shell. */
XXWIDGETS_API xxwidgets_status xxwidget_systemprocessesviewer_enumerate(xxwidgets_process_info **out_processes, size_t *out_count);
XXWIDGETS_API void xxwidget_systemprocessesviewer_free_processes(xxwidgets_process_info *, size_t count);
XXWIDGETS_API xxwidgets_status xxwidget_systemprocessesviewer_kill_process(const xxwidgets_process_info *, int force);
XXWIDGETS_API xxwidgets_status xxwidget_systemprocessesviewer_open_process_path(const xxwidgets_process_info *);

/* Call outside an input callback, after a CONTEXT_MENU/ACTIVATE request or an
 * Actions button. Shows Open path, Copy PID, Refresh, and Kill process actions
 * in both backends. Kill requires explicit confirmation. Native right-click
 * selects the target row before CONTEXT_MENU; TUI F10 requests the same menu.
 * The caller defers the request until after app_poll, as other modal APIs do. */
XXWIDGETS_API xxwidgets_status xxwidget_systemprocessesviewer_show_actions(xxwidgets_widget *);

#ifdef __cplusplus
}
#endif
#endif
