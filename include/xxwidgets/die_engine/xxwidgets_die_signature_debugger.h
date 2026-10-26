/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_DIE_SIGNATURE_DEBUGGER_H
#define XXWIDGETS_DIE_SIGNATURE_DEBUGGER_H
#include "xxwidgets_die_signature_debug_session.h"
#include "xxwidgets_die_signature_edit.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xxwidgets_die_signature_debugger xxwidgets_die_signature_debugger;
typedef enum xxwidgets_die_signature_debugger_control_id {
    XXWIDGETS_DIE_DEBUGGER_CONTINUE = 0, XXWIDGETS_DIE_DEBUGGER_PAUSE,
    XXWIDGETS_DIE_DEBUGGER_STOP, XXWIDGETS_DIE_DEBUGGER_STEP_INTO,
    XXWIDGETS_DIE_DEBUGGER_STEP_OVER, XXWIDGETS_DIE_DEBUGGER_STEP_OUT,
    XXWIDGETS_DIE_DEBUGGER_TOGGLE_BREAKPOINT, XXWIDGETS_DIE_DEBUGGER_CLEAR_BREAKPOINTS,
    XXWIDGETS_DIE_DEBUGGER_SOURCE_NAME, XXWIDGETS_DIE_DEBUGGER_SOURCE,
    XXWIDGETS_DIE_DEBUGGER_STACK_LABEL, XXWIDGETS_DIE_DEBUGGER_STACK,
    XXWIDGETS_DIE_DEBUGGER_LOCALS_LABEL, XXWIDGETS_DIE_DEBUGGER_LOCALS,
    XXWIDGETS_DIE_DEBUGGER_WATCH_LABEL, XXWIDGETS_DIE_DEBUGGER_WATCHES,
    XXWIDGETS_DIE_DEBUGGER_WATCH_INPUT, XXWIDGETS_DIE_DEBUGGER_ADD_WATCH,
    XXWIDGETS_DIE_DEBUGGER_REMOVE_WATCH, XXWIDGETS_DIE_DEBUGGER_BREAKPOINTS,
    XXWIDGETS_DIE_DEBUGGER_OUTPUT, XXWIDGETS_DIE_DEBUGGER_STATUS,
    XXWIDGETS_DIE_DEBUGGER_CONTROL_COUNT
} xxwidgets_die_signature_debugger_control_id;
/* Return a malloc-allocated UTF-8 source in *source. Called on the UI thread
 * outside dispatch/poll to display included helpers or a selected stack frame.
 * UNAVAILABLE clears the source instead of marking the wrong document. */
typedef xxwidgets_status (*xxwidgets_die_signature_debug_source_fn)(const char *source_name,char **source,void *user_data);
/* Minimum 100x32 text cells. Session is borrowed; destroy view before session
 * and owner. Create/destroy and pending execution actions belong to the UI
 * thread outside input dispatch/poll. Children must not be destroyed separately.
 * The read-only source reflects an immutable execution draft; use the signature
 * editor to change the draft then call set_source when execution has ended.
 * Configure source, breakpoints and watches through the view setters after
 * create; the view does not import preexisting session configuration. While
 * attached, use these setters so displayed and executable settings agree. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debugger_create(xxwidgets_widget *owner,xxwidgets_rect bounds,xxwidgets_die_signature_debug_session *session,xxwidgets_die_signature_debugger **out);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debugger_destroy(xxwidgets_die_signature_debugger *);
XXWIDGETS_API xxwidgets_widget *xxwidgets_die_signature_debugger_control(const xxwidgets_die_signature_debugger *,xxwidgets_die_signature_debugger_control_id);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debugger_set_rect(xxwidgets_die_signature_debugger *,xxwidgets_rect);
/* Replaces the session's immutable primary source when idle/finished. Existing
 * breakpoints are retained by source/line; out-of-range lines remain listed. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debugger_set_source(xxwidgets_die_signature_debugger *,const char *source,const char *source_name);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debugger_set_source_resolver(xxwidgets_die_signature_debugger *,xxwidgets_die_signature_debug_source_fn,void *);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debugger_set_breakpoints(xxwidgets_die_signature_debugger *,const xxwidgets_die_signature_debug_breakpoint *,size_t count);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debugger_toggle_breakpoint(xxwidgets_die_signature_debugger *);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debugger_set_watches(xxwidgets_die_signature_debugger *,const char *const *,size_t count);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debugger_run(xxwidgets_die_signature_debugger *,int pause_on_entry);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debugger_command(xxwidgets_die_signature_debugger *,xxwidgets_die_signature_debug_command);
/* Handle_event queues toolbar/frame/watch actions; call process_pending after
 * app_poll. process_pending also refreshes coherent worker snapshots. It does
 * not wait for execution or pump a nested event loop. F5/F9/F10/F11 shortcuts
 * are installed by the host demo, not globally by this embedded form. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debugger_handle_event(xxwidgets_die_signature_debugger *,const xxwidgets_event *);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debugger_process_pending(xxwidgets_die_signature_debugger *);
#ifdef __cplusplus
}
#endif
#endif
