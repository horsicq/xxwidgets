/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets/xxwidgets_debugger_xxdebug.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct xxwidgets_debugger_xxdebug {
    xxdebug *debugger;
    xxwidgets_debugger_session session;
    xxdebug_status last_error;
    char error_text[160];
};

static xxwidgets_status native_result(xxwidgets_debugger_xxdebug *adapter, xxdebug_status status)
{
    adapter->last_error = status;
    if (status == XXDEBUG_OK) adapter->error_text[0] = '\0';
    else if (status == XXDEBUG_E_OS)
        snprintf(adapter->error_text, sizeof(adapter->error_text), "%s (OS error %u)",
            xxdebug_status_string(status), (unsigned)xxdebug_os_error(adapter->debugger));
    else snprintf(adapter->error_text, sizeof(adapter->error_text), "%s", xxdebug_status_string(status));
    switch (status) {
    case XXDEBUG_OK: return XXWIDGETS_OK;
    case XXDEBUG_E_INVALID_ARGUMENT: return XXWIDGETS_INVALID_ARGUMENT;
    case XXDEBUG_E_OUT_OF_MEMORY: return XXWIDGETS_OUT_OF_MEMORY;
    case XXDEBUG_E_STATE: case XXDEBUG_E_TIMEOUT: return XXWIDGETS_BUSY;
    case XXDEBUG_E_UNSUPPORTED: case XXDEBUG_E_NOT_FOUND: case XXDEBUG_E_ACCESS:
    case XXDEBUG_E_NO_RESOURCES: return XXWIDGETS_UNAVAILABLE;
    default: return XXWIDGETS_PLATFORM_ERROR;
    }
}

static int native_range_valid(uint64_t address, size_t size)
{
    return size == 0 || (uint64_t)(size - 1u) <= UINT64_MAX - address;
}

static xxwidgets_status native_get_state(void *context, xxwidgets_debugger_session_state *state)
{
    xxwidgets_debugger_xxdebug *adapter = (xxwidgets_debugger_xxdebug *)context;
    if (!state) return native_result(adapter, XXDEBUG_E_INVALID_ARGUMENT);
    if (xxdebug_has_exited(adapter->debugger)) *state = XXWIDGETS_DEBUGGER_SESSION_EXITED;
    else if (xxdebug_is_running(adapter->debugger)) *state = XXWIDGETS_DEBUGGER_SESSION_RUNNING;
    else if (xxdebug_pid(adapter->debugger)) *state = XXWIDGETS_DEBUGGER_SESSION_STOPPED;
    else *state = XXWIDGETS_DEBUGGER_SESSION_EMPTY;
    return native_result(adapter, XXDEBUG_OK);
}

static xxwidgets_status native_get_target(void *context, uint64_t tid, xxwidgets_debugger_target *target)
{
    xxwidgets_debugger_xxdebug *adapter = (xxwidgets_debugger_xxdebug *)context;
    xxwidgets_debugger_target result = {0};
    xxdebug_mode mode = XXDEBUG_MODE_UNKNOWN;
    xxdebug_status status;
    if (!target) return native_result(adapter, XXDEBUG_E_INVALID_ARGUMENT);
    if (xxdebug_is_running(adapter->debugger) || xxdebug_has_exited(adapter->debugger) || !xxdebug_pid(adapter->debugger))
        return native_result(adapter, XXDEBUG_E_STATE);
    status = xxdebug_get_mode(adapter->debugger, tid, &mode);
    if (status != XXDEBUG_OK) return native_result(adapter, status);
    if (mode == XXDEBUG_MODE_ARM64 || mode == XXDEBUG_MODE_ARM64E) {
        xxdebug_arm64_regs regs;
        status = xxdebug_get_arm64_regs(adapter->debugger, tid, &regs);
        if (status != XXDEBUG_OK) return native_result(adapter, status);
        result.cpu.arch = XXWIDGETS_DEBUGGER_ARCH_ARM64;
        memcpy(result.cpu.regs.arm64.x, regs.x, sizeof(regs.x));
        result.cpu.regs.arm64.sp = result.sp = regs.sp;
        result.cpu.regs.arm64.pc = result.pc = regs.pc;
        result.cpu.regs.arm64.cpsr = regs.cpsr;
        result.cpu.regs.arm64.fpsr = regs.fpsr;
        result.cpu.regs.arm64.fpcr = regs.fpcr;
        result.cpu.regs.arm64.has_simd = 1;
        memcpy(result.cpu.regs.arm64.v, regs.v, sizeof(regs.v));
    } else if (mode == XXDEBUG_MODE_X86_16 || mode == XXDEBUG_MODE_X86_32 || mode == XXDEBUG_MODE_X86_64) {
        xxdebug_x86_regs regs;
        const xxdebug_backend *backend = xxdebug_get_backend(adapter->debugger);
        uint64_t mask = mode == XXDEBUG_MODE_X86_16 ? UINT64_C(0xffff) :
            mode == XXDEBUG_MODE_X86_32 ? UINT64_C(0xffffffff) : UINT64_MAX;
        uint64_t stack_offset;
        status = xxdebug_get_regs(adapter->debugger, tid, &regs);
        if (status != XXDEBUG_OK) return native_result(adapter, status);
        stack_offset = regs.gpr[XXDEBUG_X86_RSP] & mask;
        if (stack_offset > UINT64_MAX - regs.seg_base[XXDEBUG_X86_SS])
            return native_result(adapter, XXDEBUG_E_INVALID_ARGUMENT);
        result.sp = regs.seg_base[XXDEBUG_X86_SS] + stack_offset;
        result.pc = regs.pc;
        result.cpu.arch = mode == XXDEBUG_MODE_X86_16 ? XXWIDGETS_DEBUGGER_ARCH_X86_16 :
            mode == XXDEBUG_MODE_X86_32 ? XXWIDGETS_DEBUGGER_ARCH_X86_32 : XXWIDGETS_DEBUGGER_ARCH_X86_64;
        memcpy(result.cpu.regs.x86.gpr, regs.gpr, sizeof(regs.gpr));
        result.cpu.regs.x86.ip = regs.ip;
        result.cpu.regs.x86.pc = regs.pc;
        result.cpu.regs.x86.flags = regs.flags;
        memcpy(result.cpu.regs.x86.seg, regs.seg, sizeof(regs.seg));
        memcpy(result.cpu.regs.x86.seg_base, regs.seg_base, sizeof(regs.seg_base));
        memcpy(result.cpu.regs.x86.dr, regs.dr, sizeof(regs.dr));
        if (!backend || !(backend->caps & XXDEBUG_CAP_DEBUG_REGS))
            result.cpu.regs.x86.unavailable |= XXWIDGETS_DEBUGGER_X86_NO_DEBUG_REGISTERS;
    } else return native_result(adapter, XXDEBUG_E_UNSUPPORTED);
    /* Current xxdebug x86 and macOS ARM64 providers use little-endian memory. */
    result.big_endian = 0;
    *target = result;
    return native_result(adapter, XXDEBUG_OK);
}

static xxwidgets_status native_read_memory(void *context, uint64_t address, void *buffer, size_t size, size_t *done)
{
    xxwidgets_debugger_xxdebug *adapter = (xxwidgets_debugger_xxdebug *)context;
    xxdebug_status status;
    if (done) *done = 0;
    if (!done || (!buffer && size) || !native_range_valid(address, size))
        return native_result(adapter, XXDEBUG_E_INVALID_ARGUMENT);
    status = xxdebug_read_memory(adapter->debugger, address, buffer, size, done);
    if (*done > size) { *done = size; status = XXDEBUG_E_IO; }
    if (status == XXDEBUG_OK && *done != size) status = XXDEBUG_E_ACCESS;
    return native_result(adapter, status);
}

static xxwidgets_status native_write_memory(void *context, uint64_t address, const void *buffer, size_t size, size_t *done)
{
    xxwidgets_debugger_xxdebug *adapter = (xxwidgets_debugger_xxdebug *)context;
    xxdebug_status status;
    if (done) *done = 0;
    if (!done || (!buffer && size) || !native_range_valid(address, size))
        return native_result(adapter, XXDEBUG_E_INVALID_ARGUMENT);
    status = xxdebug_write_memory(adapter->debugger, address, buffer, size, done);
    if (*done > size) { *done = size; status = XXDEBUG_E_IO; }
    if (status == XXDEBUG_OK && *done != size) status = XXDEBUG_E_ACCESS;
    return native_result(adapter, status);
}

static xxwidgets_status native_disassemble(void *context, uint64_t tid, uint64_t address,
    char *text, size_t text_size, uint32_t *length)
{
    xxwidgets_debugger_xxdebug *adapter = (xxwidgets_debugger_xxdebug *)context;
    xxdebug_mode mode = XXDEBUG_MODE_UNKNOWN;
    xxdebug_status status;
    size_t window;
    if (length) *length = 0;
    if (text && text_size) text[0] = '\0';
    if (!length || !text || !text_size) return native_result(adapter, XXDEBUG_E_INVALID_ARGUMENT);
    status = xxdebug_get_mode(adapter->debugger, tid, &mode);
    if (status != XXDEBUG_OK) return native_result(adapter, status);
    window = mode == XXDEBUG_MODE_ARM64 || mode == XXDEBUG_MODE_ARM64E ? 4u : 16u;
    if (!native_range_valid(address, window)) return native_result(adapter, XXDEBUG_E_ACCESS);
    status = xxdebug_disassemble(adapter->debugger, address, mode, text, text_size, length);
    text[text_size - 1u] = '\0';
    if (status == XXDEBUG_OK && (!*length || *length > 16u)) {
        *length = 0;
        status = XXDEBUG_E_IO;
    }
    return native_result(adapter, status);
}

static int native_execute_breakpoint(const xxdebug_breakpoint *bp)
{
    return bp->type == XXDEBUG_BP_SOFTWARE || bp->type == XXDEBUG_BP_HW_EXECUTE;
}

static xxwidgets_status native_list_breakpoints(void *context, xxwidgets_debugger_session_breakpoint *output,
    size_t capacity, size_t *count)
{
    xxwidgets_debugger_xxdebug *adapter = (xxwidgets_debugger_xxdebug *)context;
    xxdebug_breakpoint *breakpoints;
    size_t total = 0, received = 0, i, kept = 0;
    xxdebug_status status;
    if (count) *count = 0;
    if (!count || (!output && capacity)) return native_result(adapter, XXDEBUG_E_INVALID_ARGUMENT);
    status = xxdebug_list_breakpoints(adapter->debugger, NULL, 0, &total);
    if (status != XXDEBUG_OK || !total) return native_result(adapter, status);
    if (total > SIZE_MAX / sizeof(*breakpoints)) return native_result(adapter, XXDEBUG_E_OUT_OF_MEMORY);
    breakpoints = (xxdebug_breakpoint *)malloc(total * sizeof(*breakpoints));
    if (!breakpoints) return native_result(adapter, XXDEBUG_E_OUT_OF_MEMORY);
    status = xxdebug_list_breakpoints(adapter->debugger, breakpoints, total, &received);
    if (status == XXDEBUG_OK && received > total) status = XXDEBUG_E_STATE;
    if (status == XXDEBUG_OK) {
        for (i = 0; i < received; ++i) {
            if (!native_execute_breakpoint(&breakpoints[i])) continue;
            if (kept < capacity) {
                output[kept].id = breakpoints[i].id;
                output[kept].address = breakpoints[i].address;
                output[kept].enabled = breakpoints[i].enabled != 0;
            }
            ++kept;
        }
        *count = kept;
    }
    free(breakpoints);
    return native_result(adapter, status);
}

static xxwidgets_status native_set_breakpoint(void *context, uint64_t address, uint64_t *id)
{
    xxwidgets_debugger_xxdebug *adapter = (xxwidgets_debugger_xxdebug *)context;
    uint32_t native_id = 0;
    xxdebug_status status;
    if (!id) return native_result(adapter, XXDEBUG_E_INVALID_ARGUMENT);
    *id = 0;
    status = xxdebug_set_breakpoint(adapter->debugger, XXDEBUG_BP_SOFTWARE, address, 1u, 0, &native_id);
    if (status == XXDEBUG_OK) *id = native_id;
    return native_result(adapter, status);
}

static xxwidgets_status native_remove_breakpoint(void *context, uint64_t id)
{
    xxwidgets_debugger_xxdebug *adapter = (xxwidgets_debugger_xxdebug *)context;
    xxdebug_breakpoint breakpoint;
    xxdebug_status status;
    if (!id || id > UINT32_MAX) return native_result(adapter, XXDEBUG_E_INVALID_ARGUMENT);
    status = xxdebug_get_breakpoint(adapter->debugger, (uint32_t)id, &breakpoint);
    if (status == XXDEBUG_OK && !native_execute_breakpoint(&breakpoint)) status = XXDEBUG_E_UNSUPPORTED;
    if (status == XXDEBUG_OK) status = xxdebug_remove_breakpoint(adapter->debugger, (uint32_t)id);
    return native_result(adapter, status);
}

static xxwidgets_status native_resume(void *context, int pass_exception)
{
    xxwidgets_debugger_xxdebug *adapter = (xxwidgets_debugger_xxdebug *)context;
    return native_result(adapter, xxdebug_continue(adapter->debugger, pass_exception != 0));
}

static xxwidgets_status native_step(void *context, uint64_t tid)
{
    xxwidgets_debugger_xxdebug *adapter = (xxwidgets_debugger_xxdebug *)context;
    return native_result(adapter, xxdebug_step(adapter->debugger, tid));
}

static xxwidgets_status native_interrupt(void *context)
{
    xxwidgets_debugger_xxdebug *adapter = (xxwidgets_debugger_xxdebug *)context;
    return native_result(adapter, xxdebug_break(adapter->debugger));
}

static xxwidgets_status native_poll(void *context, uint32_t timeout_ms, xxwidgets_debugger_session_event *event)
{
    xxwidgets_debugger_xxdebug *adapter = (xxwidgets_debugger_xxdebug *)context;
    xxdebug_event native_event;
    xxdebug_status status;
    if (!event) return native_result(adapter, XXDEBUG_E_INVALID_ARGUMENT);
    memset(event, 0, sizeof(*event));
    /* Launch/attach mark xxdebug running while PROCESS_CREATED is pending. */
    if (!xxdebug_is_running(adapter->debugger)) return native_result(adapter, XXDEBUG_OK);
    status = xxdebug_wait_event(adapter->debugger, &native_event, timeout_ms);
    if (status == XXDEBUG_E_TIMEOUT) return native_result(adapter, XXDEBUG_OK);
    if (status != XXDEBUG_OK) return native_result(adapter, status);
    event->backend_kind = native_event.kind;
    event->tid = native_event.tid;
    event->address = native_event.address;
    switch (native_event.kind) {
    case XXDEBUG_EVENT_NONE:
        event->kind = XXWIDGETS_DEBUGGER_EVENT_NONE;
        break;
    case XXDEBUG_EVENT_PROCESS_EXITED:
        event->kind = XXWIDGETS_DEBUGGER_EVENT_EXITED;
        event->exit_code = native_event.u.exit.exit_code < INT_MIN ? INT_MIN :
            native_event.u.exit.exit_code > INT_MAX ? INT_MAX : (int)native_event.u.exit.exit_code;
        break;
    case XXDEBUG_EVENT_OUTPUT:
        event->kind = XXWIDGETS_DEBUGGER_EVENT_OUTPUT;
        event->data = native_event.u.output.text;
        event->data_size = native_event.u.output.size;
        break;
    case XXDEBUG_EVENT_BREAKPOINT: case XXDEBUG_EVENT_WATCHPOINT:
        event->breakpoint_id = native_event.u.breakpoint.id;
        event->kind = XXWIDGETS_DEBUGGER_EVENT_STOPPED;
        break;
    case XXDEBUG_EVENT_PROCESS_CREATED: case XXDEBUG_EVENT_ENTRY_POINT:
    case XXDEBUG_EVENT_SINGLE_STEP: case XXDEBUG_EVENT_EXCEPTION:
    case XXDEBUG_EVENT_BREAK_IN: case XXDEBUG_EVENT_INPUT_REQUIRED:
        event->kind = XXWIDGETS_DEBUGGER_EVENT_STOPPED;
        break;
    default:
        event->kind = XXWIDGETS_DEBUGGER_EVENT_OTHER;
        break;
    }
    return native_result(adapter, XXDEBUG_OK);
}

static int64_t native_error_code(void *context)
{
    return ((xxwidgets_debugger_xxdebug *)context)->last_error;
}

static const char *native_error_message(void *context)
{
    return ((xxwidgets_debugger_xxdebug *)context)->error_text;
}

static const xxwidgets_debugger_session_ops native_ops = {
    XXWIDGETS_DEBUGGER_SESSION_ABI,
    native_get_state, native_get_target, native_read_memory, native_write_memory,
    native_disassemble, native_list_breakpoints, native_set_breakpoint, native_remove_breakpoint,
    native_resume, native_step, native_interrupt, native_poll, native_error_code, native_error_message
};

static void native_init(xxwidgets_debugger_xxdebug *adapter, xxdebug *debugger)
{
    memset(adapter, 0, sizeof(*adapter));
    adapter->debugger = debugger;
    adapter->session.ops = &native_ops;
    adapter->session.context = adapter;
}

xxwidgets_status xxwidgets_debugger_xxdebug_create(xxdebug *debugger, xxwidgets_debugger_xxdebug **output)
{
    xxwidgets_debugger_xxdebug *adapter;
    if (!output) return XXWIDGETS_INVALID_ARGUMENT;
    *output = NULL;
    if (!debugger) return XXWIDGETS_INVALID_ARGUMENT;
    adapter = (xxwidgets_debugger_xxdebug *)malloc(sizeof(*adapter));
    if (!adapter) return XXWIDGETS_OUT_OF_MEMORY;
    native_init(adapter, debugger);
    *output = adapter;
    return XXWIDGETS_OK;
}

const xxwidgets_debugger_session *xxwidgets_debugger_xxdebug_session(xxwidgets_debugger_xxdebug *adapter)
{
    return adapter ? &adapter->session : NULL;
}

void xxwidgets_debugger_xxdebug_destroy(xxwidgets_debugger_xxdebug *adapter)
{
    free(adapter);
}

xxwidgets_status xxwidgets_debugger_refresh(xxwidgets_debugger *view, xxdebug *debugger, uint64_t tid,
    const xxwidgets_debugger_refresh_options *options, xxdebug_status *debug_status)
{
    xxwidgets_debugger_xxdebug adapter;
    xxwidgets_debugger_session_error error = {0};
    xxwidgets_status status;
    if (debug_status) *debug_status = XXDEBUG_OK;
    if (!view || !debugger) return XXWIDGETS_INVALID_ARGUMENT;
    native_init(&adapter, debugger);
    status = xxwidgets_debugger_refresh_session(view, &adapter.session, tid, options, &error);
    if (debug_status) {
        if (error.backend_code) *debug_status = (xxdebug_status)error.backend_code;
        else if (status == XXWIDGETS_BUSY &&
            (xxdebug_is_running(debugger) || xxdebug_has_exited(debugger) || !xxdebug_pid(debugger)))
            *debug_status = XXDEBUG_E_STATE;
    }
    return status;
}
