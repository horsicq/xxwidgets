/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXWIDGET_SYSTEMPROCESSESVIEWER_H
#define XXWIDGET_SYSTEMPROCESSESVIEWER_H

#include "xxwidgets/xxwidgets.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_process_info {
    const char *name; /* UTF-8 process display name. */
    uint64_t pid;
    const char *path; /* UTF-8 executable path; empty when inaccessible. */
    uint64_t start_time; /* OS identity token; zero when unavailable. */
} xxwidgets_process_info;

typedef enum xxwidgets_process_dump_type {
    XXWIDGETS_PROCESS_DUMP_MEMORY = 0, /* Windows minidump or Unix debugger core. */
    XXWIDGETS_PROCESS_DUMP_IMAGE       /* Raw main executable bytes at mapped offsets. */
} xxwidgets_process_dump_type;

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

/* Synchronous OS dump adapter; no widget or UI access. Rechecks the recorded
 * start identity and refuses PID 0/1 and the current process. MEMORY writes a
 * full-memory minidump on Windows, an ELF core using GNU gcore on Linux, or a
 * Mach-O core using /usr/bin/gcore on macOS. Missing tools, protected processes
 * and incompatible architectures return UNAVAILABLE. IMAGE reads the main
 * executable from process memory into a raw mapped image (.bin), including
 * zero-filled unmapped gaps; it does not reconstruct the on-disk executable.
 * The destination's parent must exist. Output is published only on success;
 * overwrite == 0 preserves any existing destination. OS access rights apply.
 * Core capture may briefly suspend the target. Large dumps can take time;
 * applications can call this standalone adapter from their worker thread. */
XXWIDGETS_API xxwidgets_status xxwidget_systemprocessesviewer_dump_process(const xxwidgets_process_info *, const char *destination,
    xxwidgets_process_dump_type type, int overwrite);
/* Worker-friendly variant. Progress is worker-owned; Cancel stops image
 * copying or native core capture and prevents publication of the staging
 * output. The original destination is preserved on cancellation/failure. */
XXWIDGETS_API xxwidgets_status xxwidget_systemprocessesviewer_dump_process_ex(const xxwidgets_process_info *, const char *destination,
    xxwidgets_process_dump_type type, int overwrite, xx_pd_struct *progress);

/* Call outside an input callback, after a CONTEXT_MENU/ACTIVATE request or an
 * Actions button. Shows Open path, Copy PID, Refresh, Kill process, Dump process
 * memory, Dump executable image and Hex view in both backends. Kill requires explicit
 * confirmation. Dump uses a native Save dialog or a TUI output-path form.
 * Native right-click selects the target row before CONTEXT_MENU; TUI F10
 * requests the same menu.
 * The caller defers the request until after app_poll, as other modal APIs do. */
XXWIDGETS_API xxwidgets_status xxwidget_systemprocessesviewer_show_actions(xxwidgets_widget *);

/* Modal read-only process HexView, called outside an input callback. Opens an
 * identity-checked memory device, starts at a readable region, and supports
 * address navigation and Refresh (F5). Windows, Linux and macOS permissions
 * apply. Allows the current process; refuses PID 0/1 or missing start identity.
 * Inaccessible/changed windows keep their last successfully read bytes.
 * The view closes its device and native handle before returning. */
XXWIDGETS_API xxwidgets_status xxwidget_systemprocessesviewer_show_hexview(xxwidgets_widget *);

#ifdef __cplusplus
}
#endif
#endif
