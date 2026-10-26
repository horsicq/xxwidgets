/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_DIE_SIGNATURE_DEBUG_SESSION_H
#define XXWIDGETS_DIE_SIGNATURE_DEBUG_SESSION_H

#include "../xxwidgets.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct JSCtx JSCtx;
typedef struct xxwidgets_die_signature_debug_session xxwidgets_die_signature_debug_session;

typedef enum xxwidgets_die_signature_debug_state {
    XXWIDGETS_DIE_DEBUG_IDLE = 0,
    XXWIDGETS_DIE_DEBUG_RUNNING,
    XXWIDGETS_DIE_DEBUG_PAUSED,
    XXWIDGETS_DIE_DEBUG_STOPPING,
    XXWIDGETS_DIE_DEBUG_FINISHED,
    XXWIDGETS_DIE_DEBUG_ERROR,
    XXWIDGETS_DIE_DEBUG_STOPPED
} xxwidgets_die_signature_debug_state;

typedef enum xxwidgets_die_signature_debug_command {
    XXWIDGETS_DIE_DEBUG_CONTINUE = 0,
    XXWIDGETS_DIE_DEBUG_PAUSE,
    XXWIDGETS_DIE_DEBUG_STEP_INTO,
    XXWIDGETS_DIE_DEBUG_STEP_OVER,
    XXWIDGETS_DIE_DEBUG_STEP_OUT,
    XXWIDGETS_DIE_DEBUG_STOP
} xxwidgets_die_signature_debug_command;

typedef struct xxwidgets_die_signature_debug_breakpoint {
    const char *source_name; /* NULL/empty means the session's primary source. */
    size_t line;            /* 1-based. */
} xxwidgets_die_signature_debug_breakpoint;

typedef struct xxwidgets_die_signature_debug_frame {
    char *source_name;
    char *function_name;
    size_t line, column;
    int native;
} xxwidgets_die_signature_debug_frame;

typedef struct xxwidgets_die_signature_debug_value {
    char *name;
    char *value;
    int available;
} xxwidgets_die_signature_debug_value;

typedef struct xxwidgets_die_signature_debug_snapshot {
    xxwidgets_die_signature_debug_state state;
    uint64_t revision;
    char *source_name;
    char *function_name;
    size_t line, column;
    size_t selected_frame;
    xxwidgets_die_signature_debug_frame *frames;
    size_t frame_count;
    xxwidgets_die_signature_debug_value *locals;
    size_t local_count;
    xxwidgets_die_signature_debug_value *watches;
    size_t watch_count;
    char *log;
    char *error;
    char *result;
    int exception;
    int truncated;
} xxwidgets_die_signature_debug_snapshot;

/* All callbacks run on the execution worker, with exclusive access to ctx.
 * setup returns nonzero on success; cleanup runs even when setup/eval fails.
 * execute is optional: default evaluates source with js_eval(). A custom
 * execute may install DIE bindings, evaluate helper sources and invoke detect.
 * Return nonzero on success, optionally providing a malloc-allocated UTF-8
 * result_text (the session frees it); on failure this text is the diagnostic. Callbacks may log, but must not pump UI
 * events or call destroy/start/set_source. Stop interrupts JS checkpoints;
 * blocking native code must cooperate before destroy/join can return. */
typedef struct xxwidgets_die_signature_debug_config {
    int (*setup)(JSCtx *ctx, void *user_data);
    int (*execute)(JSCtx *ctx, const char *source, const char *source_name,
        char **result_text, void *user_data);
    void (*cleanup)(JSCtx *ctx, void *user_data);
    void *user_data;
    int pause_on_exception;
    /* Default "detect"; NULL/empty executes the program only. Copied at create.
     * Default execution calls this function with true,true,true after loading. */
    const char *entry_function;
} xxwidgets_die_signature_debug_config;

XXWIDGETS_API void xxwidgets_die_signature_debug_config_init(xxwidgets_die_signature_debug_config *config);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debug_session_create(
    const xxwidgets_die_signature_debug_config *config, xxwidgets_die_signature_debug_session **out_session);
/* Cancels, wakes a paused hook, joins the worker, then frees. */
XXWIDGETS_API void xxwidgets_die_signature_debug_session_destroy(xxwidgets_die_signature_debug_session *session);
/* Source is copied and CRLF/CR normalized to LF. Replacement is BUSY while
 * active. Each start uses a fresh
 * context; the config's user_data must remain valid until destroy. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debug_session_set_source(
    xxwidgets_die_signature_debug_session *session, const char *source, const char *source_name);
/* Copies the primary source for name (NULL/empty accepts its current name).
 * Returns UNAVAILABLE for another source. Caller frees *out_source with free. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debug_session_source(
    xxwidgets_die_signature_debug_session *session, const char *name, char **out_source);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debug_session_set_breakpoints(
    xxwidgets_die_signature_debug_session *session,
    const xxwidgets_die_signature_debug_breakpoint *breakpoints, size_t count);
/* Watches are lexical identifiers (including "this"), never expressions;
 * display formatting never invokes JavaScript getters or toString(). */
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debug_session_set_watches(
    xxwidgets_die_signature_debug_session *session, const char *const *identifiers, size_t count);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debug_session_select_frame(
    xxwidgets_die_signature_debug_session *session, size_t frame_index);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debug_session_start(
    xxwidgets_die_signature_debug_session *session, int pause_on_entry);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debug_session_command(
    xxwidgets_die_signature_debug_session *session, xxwidgets_die_signature_debug_command command);
/* Snapshots are deep copies valid after resume/restart/destroy. Initialize out
 * to zero and free after use. Snapshot does not free an earlier value of out.
 * Mutations belong to one owner thread; snapshot/log/cancel queries may run on
 * other threads. No UI call accesses a running JSCtx. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debug_session_snapshot(
    xxwidgets_die_signature_debug_session *session, xxwidgets_die_signature_debug_snapshot *out_snapshot);
XXWIDGETS_API void xxwidgets_die_signature_debug_snapshot_free(xxwidgets_die_signature_debug_snapshot *snapshot);
XXWIDGETS_API void xxwidgets_die_signature_debug_session_log(
    xxwidgets_die_signature_debug_session *session, const char *text);
XXWIDGETS_API int xxwidgets_die_signature_debug_session_cancelled(
    xxwidgets_die_signature_debug_session *session);

#ifdef __cplusplus
}
#endif
#endif
