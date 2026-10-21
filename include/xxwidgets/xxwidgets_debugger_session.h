/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_DEBUGGER_SESSION_H
#define XXWIDGETS_DEBUGGER_SESSION_H

#include "xxwidgets/xxwidgets_debugger_arch.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XXWIDGETS_DEBUGGER_SESSION_ABI 1u
#define XXWIDGETS_DEBUGGER_MAX_INSTRUCTIONS 256u
#define XXWIDGETS_DEBUGGER_MAX_MEMORY 65536u
#define XXWIDGETS_DEBUGGER_MAX_STACK 256u

typedef enum xxwidgets_debugger_session_state {
    XXWIDGETS_DEBUGGER_SESSION_EMPTY = 0,
    XXWIDGETS_DEBUGGER_SESSION_STOPPED,
    XXWIDGETS_DEBUGGER_SESSION_RUNNING,
    XXWIDGETS_DEBUGGER_SESSION_EXITED,
    /* Emulator execution ended; registers and memory remain readable. */
    XXWIDGETS_DEBUGGER_SESSION_HALTED
} xxwidgets_debugger_session_state;

typedef struct xxwidgets_debugger_target {
    xxwidgets_debugger_cpu_snapshot cpu;
    uint64_t pc, sp; /* Linear addresses, including segment translation. */
    int big_endian;  /* Target memory byte order, independent of host order. */
} xxwidgets_debugger_target;

typedef struct xxwidgets_debugger_session_breakpoint {
    uint64_t id, address;
    int enabled; /* This interface lists execution breakpoints only. */
} xxwidgets_debugger_session_breakpoint;

typedef enum xxwidgets_debugger_event_kind {
    XXWIDGETS_DEBUGGER_EVENT_NONE = 0,
    XXWIDGETS_DEBUGGER_EVENT_STOPPED,
    XXWIDGETS_DEBUGGER_EVENT_EXITED,
    XXWIDGETS_DEBUGGER_EVENT_OUTPUT,
    XXWIDGETS_DEBUGGER_EVENT_OTHER
} xxwidgets_debugger_event_kind;

typedef struct xxwidgets_debugger_session_event {
    xxwidgets_debugger_event_kind kind;
    uint64_t tid, address, breakpoint_id;
    int exit_code;
    int64_t backend_kind; /* Native event/status code, for host-specific UI. */
    const void *data;     /* Borrowed until the next provider operation. */
    size_t data_size;
} xxwidgets_debugger_session_event;

/* Provider callbacks are ordinary C function pointers. Missing callbacks
 * report UNAVAILABLE through the wrappers. All calls run on the session's
 * owning thread, serialized with direct backend calls; no locking is added.
 * get_target returns a coherent CPU/PC/SP snapshot. read_memory always sets
 * done, including on partial failure. disassemble must not execute or change
 * the target; it writes NUL-terminated text and a length from 1 through 16.
 * list_breakpoints writes up to capacity and returns the total execute count.
 * resume/step can start asynchronous execution; call poll and get_state
 * before refreshing. poll returning OK/NONE means no event is available.
 * Native poll uses timeout_ms; an emulator may instead execute a bounded
 * instruction slice without sleeping. error_code/error_message describe the
 * most recent operation; messages are borrowed. Optional control callbacks
 * let read-only providers leave unsupported actions disabled in the host UI.
 */
typedef struct xxwidgets_debugger_session_ops {
    uint32_t abi_version;
    xxwidgets_status (*get_state)(void *, xxwidgets_debugger_session_state *);
    xxwidgets_status (*get_target)(void *, uint64_t tid, xxwidgets_debugger_target *);
    xxwidgets_status (*read_memory)(void *, uint64_t address, void *, size_t, size_t *done);
    xxwidgets_status (*write_memory)(void *, uint64_t address, const void *, size_t, size_t *done);
    xxwidgets_status (*disassemble)(void *, uint64_t tid, uint64_t address, char *, size_t, uint32_t *length);
    xxwidgets_status (*list_breakpoints)(void *, xxwidgets_debugger_session_breakpoint *, size_t capacity, size_t *count);
    xxwidgets_status (*set_breakpoint)(void *, uint64_t address, uint64_t *id);
    xxwidgets_status (*remove_breakpoint)(void *, uint64_t id);
    xxwidgets_status (*resume)(void *, int pass_exception);
    xxwidgets_status (*step)(void *, uint64_t tid);
    xxwidgets_status (*interrupt)(void *);
    xxwidgets_status (*poll)(void *, uint32_t timeout_ms, xxwidgets_debugger_session_event *);
    int64_t (*error_code)(void *);
    const char *(*error_message)(void *);
} xxwidgets_debugger_session_ops;

/* Borrowed interface: neither this struct nor the widget owns context or ops.
 * Keep them alive for every call. Binding/refreshing never starts execution,
 * replaces target callbacks, closes the target, or destroys an emulator.
 * A custom provider can initialize this struct directly with ABI 1 ops. */
typedef struct xxwidgets_debugger_session {
    const xxwidgets_debugger_session_ops *ops;
    void *context;
} xxwidgets_debugger_session;

typedef struct xxwidgets_debugger_session_error {
    xxwidgets_status status;
    int64_t backend_code;
    char message[160];
} xxwidgets_debugger_session_error;

typedef struct xxwidgets_debugger_refresh_options {
    int has_disassembly_address, has_memory_address, has_stack_address;
    uint64_t disassembly_address, memory_address, stack_address;
    /* Zero selects defaults: 32 instructions, 256 memory bytes, 32 words. */
    size_t instruction_count, memory_size, stack_count;
} xxwidgets_debugger_refresh_options;

XXWIDGETS_API xxwidgets_status xxwidgets_debugger_session_get_state(const xxwidgets_debugger_session *, xxwidgets_debugger_session_state *);
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_session_get_target(const xxwidgets_debugger_session *, uint64_t tid, xxwidgets_debugger_target *);
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_session_read_memory(const xxwidgets_debugger_session *, uint64_t address, void *, size_t, size_t *done);
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_session_write_memory(const xxwidgets_debugger_session *, uint64_t address, const void *, size_t, size_t *done);
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_session_disassemble(const xxwidgets_debugger_session *, uint64_t tid, uint64_t address, char *, size_t, uint32_t *length);
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_session_list_breakpoints(const xxwidgets_debugger_session *, xxwidgets_debugger_session_breakpoint *, size_t capacity, size_t *count);
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_session_set_breakpoint(const xxwidgets_debugger_session *, uint64_t address, uint64_t *id);
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_session_remove_breakpoint(const xxwidgets_debugger_session *, uint64_t id);
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_session_resume(const xxwidgets_debugger_session *, int pass_exception);
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_session_step(const xxwidgets_debugger_session *, uint64_t tid);
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_session_interrupt(const xxwidgets_debugger_session *);
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_session_poll(const xxwidgets_debugger_session *, uint32_t timeout_ms, xxwidgets_debugger_session_event *);

/* Refresh compatible, visible panes from a STOPPED or HALTED session. Hidden
 * and custom pane kinds are skipped. Explicit-address Hex alone needs no CPU
 * callback. Reads are staged; each pane commit is transactional, not the
 * entire refresh. Partial reads display known bytes and ?? in stack words.
 * error receives the first provider error (OK/zero/empty on success), captured
 * before another provider call can overwrite its diagnostics. The return
 * value gives display errors precedence. All widget calls use the UI thread.
 */
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_refresh_session(xxwidgets_debugger *, const xxwidgets_debugger_session *, uint64_t tid,
    const xxwidgets_debugger_refresh_options *, xxwidgets_debugger_session_error *error);

#ifdef __cplusplus
}
#endif
#endif
