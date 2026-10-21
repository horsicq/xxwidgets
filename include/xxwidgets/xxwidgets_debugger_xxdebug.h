/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_DEBUGGER_XXDEBUG_H
#define XXWIDGETS_DEBUGGER_XXDEBUG_H

#include "xxwidgets/xxwidgets_debugger_session.h"
#include "xxdebug/xxdebug.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_debugger_xxdebug xxwidgets_debugger_xxdebug;

/* Optional xxwidgets::debugger provider for a borrowed xxdebug object. The
 * owner launches/attaches the target separately and keeps it alive until this
 * wrapper is destroyed. Creating/destroying the wrapper never changes the
 * target or xxdebug callbacks. Use the common session API for snapshots,
 * memory, execution breakpoints and explicit execution controls. The returned
 * session is borrowed and valid until destroy; destroy(NULL) is harmless.
 * Calls use the session-owning thread, serialized with direct xxdebug calls.
 * Native asynchronous interruption from another thread can still use the
 * underlying xxdebug_break directly; the wrapper itself adds no locking.
 *
 * Polling calls xxdebug_wait_event without invoking callbacks. Timeouts and
 * already stopped/empty/exited targets return OK with EVENT_NONE. Events can
 * leave the process stopped, including OUTPUT/OTHER: consult get_state and
 * explicitly resume as needed. Native event kinds are preserved in
 * backend_kind. Only execution breakpoints are exposed by the common API;
 * set creates a software breakpoint and remove rejects watchpoint IDs.
 * Native errors are available through the session diagnostic callbacks.
 */
xxwidgets_status xxwidgets_debugger_xxdebug_create(xxdebug *debugger, xxwidgets_debugger_xxdebug **output);
const xxwidgets_debugger_session *xxwidgets_debugger_xxdebug_session(xxwidgets_debugger_xxdebug *adapter);
void xxwidgets_debugger_xxdebug_destroy(xxwidgets_debugger_xxdebug *adapter);

/* Compatibility wrapper over xxwidgets_debugger_refresh_session; a temporary
 * provider is borrowed only for this call. NULL options use all defaults.
 * Call on the UI/session-owning thread after xxdebug_wait_event returned a
 * stop event, or after xxdebug_run returned with the target stopped. No call
 * may run concurrently with another operation on the same xxdebug object.
 * tid 0 selects the current event thread. The session is borrowed only for
 * this call: it is never retained, launched, resumed, detached or destroyed.
 *
 * Running, uninitialized and exited sessions are rejected before pane changes.
 * Only visible, compatible Disasm/Registers/HexView/Stack controls are read and
 * populated. Hidden panes and replacement controls of other kinds are skipped;
 * the host populates custom controls. Mode/register failures preserve all panes.
 * Target reads and formatted rows are staged before display updates. On a
 * partial/unreadable target read, available data is shown and affected pane
 * headings report the error. Partial stack words contain ?? for unknown bytes.
 * A successful register refresh marks changed values relative to existing
 * rows of the same name; clear the view when changing targets/threads if such
 * comparisons are not wanted. ARM64e register pointers follow xxdebug's API.
 * Registers and stack words use the base library's architecture formatters.
 * Live modes are x86 16/32/64 and ARM64/ARM64e. ARM32 snapshots are available
 * through xxwidgets_debugger_arch.h; xxdebug has no ARM32 register API yet.
 *
 * Each pane update is transactional; the entire refresh is not. A display
 * update failure may leave earlier panes refreshed and later panes unchanged.
 * debug_status, if non-NULL, receives the first target error, or XXDEBUG_OK.
 * The return value reports a display error first, otherwise maps target errors
 * to xxwidgets_status (state -> BUSY, access/unsupported -> UNAVAILABLE).
 * No debugger callbacks are changed. All target addresses are linear.
 */
xxwidgets_status xxwidgets_debugger_refresh(xxwidgets_debugger *view, xxdebug *debugger, uint64_t tid,
    const xxwidgets_debugger_refresh_options *options, xxdebug_status *debug_status);

#ifdef __cplusplus
}
#endif
#endif
