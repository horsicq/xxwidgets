/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets/xxwidgets_debugger_xxemul.h"

#include <cdisasm/cdisasm.h>
#include <cdisasm/cdisasm_format.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct xxwidgets_debugger_xxemul {
    xxwidgets_debugger_session session;
    xxemul *emulator;
    uint64_t quantum, next_breakpoint_id;
    xxwidgets_debugger_session_breakpoint *breakpoints;
    size_t breakpoint_count, breakpoint_capacity;
    int running, in_step, pending;
    int stopped_at_breakpoint, skip_breakpoint;
    uint64_t stopped_address, skip_address;
    xxwidgets_debugger_session_event event;
    xxemul_status last_code;
    char last_message[256];
};

static xxwidgets_status set_result(xxwidgets_debugger_xxemul *adapter, xxwidgets_status result,
    xxemul_status code, const char *message)
{
    adapter->last_code = code;
    if (message != NULL) {
        (void)snprintf(adapter->last_message, sizeof(adapter->last_message), "%s", message);
    } else if (code == XXEMUL_STATUS_OK) {
        adapter->last_message[0] = '\0';
    } else {
        const char *detail = xxemul_process_error(adapter->emulator);
        const char *name = xxemul_status_string(code);
        (void)snprintf(adapter->last_message, sizeof(adapter->last_message), "%s%s%s",
            name != NULL ? name : "Emulator error", detail != NULL && *detail != '\0' ? ": " : "",
            detail != NULL ? detail : "");
    }
    return result;
}

static xxwidgets_status emul_result(xxwidgets_debugger_xxemul *adapter, xxemul_status code)
{
    xxwidgets_status result;
    switch (code) {
    case XXEMUL_STATUS_OK: result = XXWIDGETS_OK; break;
    case XXEMUL_STATUS_INVALID_ARGUMENT: result = XXWIDGETS_INVALID_ARGUMENT; break;
    case XXEMUL_STATUS_OUT_OF_MEMORY: result = XXWIDGETS_OUT_OF_MEMORY; break;
    case XXEMUL_STATUS_IO_ERROR: result = XXWIDGETS_PLATFORM_ERROR; break;
    default: result = XXWIDGETS_UNAVAILABLE; break;
    }
    return set_result(adapter, result, code, NULL);
}

static xxwidgets_status stopped(xxwidgets_debugger_xxemul *adapter)
{
    if (adapter->running || adapter->in_step)
        return set_result(adapter, XXWIDGETS_BUSY, XXEMUL_STATUS_INVALID_ARGUMENT,
            "Stop the emulator before reading or changing its state");
    return XXWIDGETS_OK;
}

static int valid_tid(uint64_t tid)
{
    return tid <= UINT64_C(1);
}

static xxwidgets_status get_state(void *context, xxwidgets_debugger_session_state *state)
{
    xxwidgets_debugger_xxemul *adapter = context;
    if (state == NULL) return emul_result(adapter, XXEMUL_STATUS_INVALID_ARGUMENT);
    *state = xxemul_is_halted(adapter->emulator) ? XXWIDGETS_DEBUGGER_SESSION_HALTED
        : adapter->running ? XXWIDGETS_DEBUGGER_SESSION_RUNNING : XXWIDGETS_DEBUGGER_SESSION_STOPPED;
    return emul_result(adapter, XXEMUL_STATUS_OK);
}

static xxwidgets_status get_target(void *context, uint64_t tid, xxwidgets_debugger_target *target)
{
    xxwidgets_debugger_xxemul *adapter = context;
    xxwidgets_debugger_target value;
    xxemul_status code;
    xxemul_mode mode = xxemul_get_mode(adapter->emulator);
    size_t index;
    if (target == NULL || !valid_tid(tid)) return emul_result(adapter, XXEMUL_STATUS_INVALID_ARGUMENT);
    if (stopped(adapter) != XXWIDGETS_OK) return XXWIDGETS_BUSY;
    memset(&value, 0, sizeof(value));
    code = xxemul_get_current_address(adapter->emulator, &value.pc);
    if (code != XXEMUL_STATUS_OK) return emul_result(adapter, code);
    if (xxemul_get_arch(adapter->emulator) == XXEMUL_ARCH_X86) {
        xxemul_x86_state native;
        xxwidgets_debugger_x86_state *regs = &value.cpu.regs.x86;
        uint64_t mask;
        if (mode == XXEMUL_MODE_X86_16) {
            value.cpu.arch = XXWIDGETS_DEBUGGER_ARCH_X86_16;
            mask = UINT16_MAX;
        } else if (mode == XXEMUL_MODE_X86_32) {
            value.cpu.arch = XXWIDGETS_DEBUGGER_ARCH_X86_32;
            mask = UINT32_MAX;
        } else if (mode == XXEMUL_MODE_X86_64) {
            value.cpu.arch = XXWIDGETS_DEBUGGER_ARCH_X86_64;
            mask = UINT64_MAX;
        } else {
            return emul_result(adapter, XXEMUL_STATUS_INVALID_ARGUMENT);
        }
        code = xxemul_get_x86_state(adapter->emulator, &native);
        if (code != XXEMUL_STATUS_OK) return emul_result(adapter, code);
        for (index = 0; index < 16u; ++index) regs->gpr[index] = native.gpr[index] & mask;
        regs->ip = native.ip & mask;
        regs->pc = value.pc;
        regs->flags = native.flags;
        regs->unavailable = XXWIDGETS_DEBUGGER_X86_NO_DEBUG_REGISTERS;
        for (index = 0; index < 6u; ++index) {
            regs->seg[index] = native.segment[index];
            code = xxemul_x86_get_segment_base(adapter->emulator, (unsigned)index, &regs->seg_base[index]);
            if (code != XXEMUL_STATUS_OK) return emul_result(adapter, code);
        }
        code = xxemul_get_stack_address(adapter->emulator, &value.sp);
        if (code != XXEMUL_STATUS_OK) return emul_result(adapter, code);
    } else if (xxemul_get_arch(adapter->emulator) == XXEMUL_ARCH_ARM) {
        xxemul_arm_state native;
        code = xxemul_get_arm_state(adapter->emulator, &native);
        if (code != XXEMUL_STATUS_OK) return emul_result(adapter, code);
        if (mode == XXEMUL_MODE_ARM_A64) {
            xxwidgets_debugger_arm64_state *regs = &value.cpu.regs.arm64;
            value.cpu.arch = XXWIDGETS_DEBUGGER_ARCH_ARM64;
            for (index = 0; index < 31u; ++index) regs->x[index] = native.gpr[index];
            regs->sp = native.sp;
            regs->pc = native.pc;
            regs->cpsr = native.pstate;
            regs->fpsr = native.fpsr;
            regs->fpcr = native.fpcr;
            regs->has_simd = 1;
            memcpy(regs->v, native.v, sizeof(regs->v));
            value.sp = native.sp;
        } else if (mode == XXEMUL_MODE_ARM_A32 || mode == XXEMUL_MODE_ARM_T32) {
            xxwidgets_debugger_arm32_state *regs = &value.cpu.regs.arm32;
            value.cpu.arch = XXWIDGETS_DEBUGGER_ARCH_ARM32;
            for (index = 0; index < 13u; ++index) regs->r[index] = (uint32_t)native.gpr[index];
            regs->r[13] = (uint32_t)native.sp;
            regs->r[14] = (uint32_t)native.gpr[14];
            regs->r[15] = (uint32_t)native.pc;
            regs->cpsr = (native.pstate & ~UINT32_C(0x20))
                | (mode == XXEMUL_MODE_ARM_T32 ? UINT32_C(0x20) : 0u);
            value.sp = regs->r[13];
        } else {
            return emul_result(adapter, XXEMUL_STATUS_INVALID_ARGUMENT);
        }
    } else {
        return emul_result(adapter, XXEMUL_STATUS_INVALID_ARGUMENT);
    }
    *target = value;
    return emul_result(adapter, XXEMUL_STATUS_OK);
}

/* Public xxemul reads are all-or-error, so shorten failed chunks until the
 * readable prefix is known. This also supports an instruction at a region's
 * end without assuming the initial flat region is the only mapped memory. */
static xxemul_status read_prefix(xxwidgets_debugger_xxemul *adapter, uint64_t address,
    void *buffer, size_t size, size_t *done)
{
    unsigned char *bytes = buffer;
    *done = 0;
    while (*done < size) {
        size_t chunk = size - *done;
        xxemul_status code;
        if (chunk > 4096u) chunk = 4096u;
        for (;;) {
            code = xxemul_read_memory(adapter->emulator, address + *done, bytes + *done, chunk);
            if (code == XXEMUL_STATUS_OK) break;
            if (chunk == 1u || code != XXEMUL_STATUS_ADDRESS_FAULT) return code;
            chunk /= 2u;
        }
        *done += chunk;
    }
    return XXEMUL_STATUS_OK;
}

static int valid_range(uint64_t address, size_t size)
{
    return size == 0 || (uint64_t)(size - 1u) <= UINT64_MAX - address;
}

static xxwidgets_status read_memory(void *context, uint64_t address, void *buffer, size_t size, size_t *done)
{
    xxwidgets_debugger_xxemul *adapter = context;
    if (done != NULL) *done = 0;
    if (done == NULL || (size != 0 && buffer == NULL) || !valid_range(address, size))
        return emul_result(adapter, XXEMUL_STATUS_INVALID_ARGUMENT);
    if (stopped(adapter) != XXWIDGETS_OK) return XXWIDGETS_BUSY;
    return emul_result(adapter, read_prefix(adapter, address, buffer, size, done));
}

static xxwidgets_status write_memory(void *context, uint64_t address, const void *buffer, size_t size, size_t *done)
{
    xxwidgets_debugger_xxemul *adapter = context;
    const unsigned char *bytes = buffer;
    if (done != NULL) *done = 0;
    if (done == NULL || (size != 0 && buffer == NULL) || !valid_range(address, size))
        return emul_result(adapter, XXEMUL_STATUS_INVALID_ARGUMENT);
    if (stopped(adapter) != XXWIDGETS_OK) return XXWIDGETS_BUSY;
    /* A bulk write crossing Linux mappings can modify a prefix before the
     * public API returns ADDRESS_FAULT without a transfer count. Write bytes
     * individually so done remains exact even at a mapping boundary. */
    while (*done < size) {
        xxemul_status code = xxemul_write_memory(adapter->emulator, address + *done, bytes + *done, 1u);
        if (code != XXEMUL_STATUS_OK) return emul_result(adapter, code);
        ++*done;
    }
    return emul_result(adapter, XXEMUL_STATUS_OK);
}

static xxwidgets_status disassemble(void *context, uint64_t tid, uint64_t address,
    char *text, size_t capacity, uint32_t *length)
{
    xxwidgets_debugger_xxemul *adapter = context;
    uint8_t bytes[15];
    xxemul_mode mode = xxemul_get_mode(adapter->emulator);
    xxemul_arch arch = xxemul_get_arch(adapter->emulator);
    size_t wanted = arch == XXEMUL_ARCH_X86 ? 15u : 4u, available, formatted;
    uint32_t decoded;
    xxemul_status code;
    if (text != NULL && capacity != 0) text[0] = '\0';
    if (length != NULL) *length = 0;
    if (!valid_tid(tid) || text == NULL || capacity == 0 || length == NULL)
        return emul_result(adapter, XXEMUL_STATUS_INVALID_ARGUMENT);
    if (stopped(adapter) != XXWIDGETS_OK) return XXWIDGETS_BUSY;
    if ((uint64_t)(wanted - 1u) > UINT64_MAX - address) wanted = (size_t)(UINT64_MAX - address) + 1u;
    code = read_prefix(adapter, address, bytes, wanted, &available);
    if (available == 0) return emul_result(adapter, code);
    if (arch == XXEMUL_ARCH_X86) {
        cdisasm_x86_instruction instruction;
        cdisasm_x86_decode_flags flags;
        cdisasm_x86_mode decode_mode;
        if (mode == XXEMUL_MODE_X86_16) decode_mode = CDISASM_X86_MODE_16;
        else if (mode == XXEMUL_MODE_X86_32) decode_mode = CDISASM_X86_MODE_32;
        else if (mode == XXEMUL_MODE_X86_64) decode_mode = CDISASM_X86_MODE_64;
        else return emul_result(adapter, XXEMUL_STATUS_INVALID_ARGUMENT);
        if (cdisasm_x86_cpu_decode_flag_mask(CDISASM_CPU_X86, decode_mode, &flags) != CDISASM_STATUS_OK)
            return emul_result(adapter, XXEMUL_STATUS_DECODE_ERROR);
        decoded = cdisasm_x86_decode(CDISASM_CPU_X86, decode_mode, bytes, available, address, &flags, &instruction);
        if (decoded == 0) return emul_result(adapter, XXEMUL_STATUS_DECODE_ERROR);
        formatted = cdisasm_x86_format_mode(&instruction, decode_mode, CDISASM_FORMAT_SYNTAX_X86_INTEL, text, capacity);
    } else if (arch == XXEMUL_ARCH_ARM) {
        cdisasm_arm_instruction instruction;
        cdisasm_arm_mode decode_mode;
        if (mode == XXEMUL_MODE_ARM_A32) decode_mode = CDISASM_ARM_MODE_A32;
        else if (mode == XXEMUL_MODE_ARM_T32) decode_mode = CDISASM_ARM_MODE_T32;
        else if (mode == XXEMUL_MODE_ARM_A64) decode_mode = CDISASM_ARM_MODE_A64;
        else return emul_result(adapter, XXEMUL_STATUS_INVALID_ARGUMENT);
        decoded = cdisasm_arm_decode(CDISASM_ARM_CPU_ANY, decode_mode, bytes, available, address, NULL, &instruction);
        if (decoded == 0) return emul_result(adapter, XXEMUL_STATUS_DECODE_ERROR);
        formatted = cdisasm_arm_format(&instruction, CDISASM_FORMAT_SYNTAX_ARM_CANONICAL, text, capacity);
    } else {
        return emul_result(adapter, XXEMUL_STATUS_INVALID_ARGUMENT);
    }
    if (formatted == 0 || decoded > available || decoded > 16u)
        return emul_result(adapter, XXEMUL_STATUS_DECODE_ERROR);
    if (formatted >= capacity)
        return set_result(adapter, XXWIDGETS_BUFFER_TOO_SMALL, XXEMUL_STATUS_INVALID_ARGUMENT,
            "Disassembly text buffer is too small");
    *length = decoded;
    return emul_result(adapter, XXEMUL_STATUS_OK);
}

static xxwidgets_status list_breakpoints(void *context, xxwidgets_debugger_session_breakpoint *items,
    size_t capacity, size_t *count)
{
    xxwidgets_debugger_xxemul *adapter = context;
    size_t copied = capacity < adapter->breakpoint_count ? capacity : adapter->breakpoint_count;
    if (count == NULL || (capacity != 0 && items == NULL)) return emul_result(adapter, XXEMUL_STATUS_INVALID_ARGUMENT);
    if (copied != 0) memcpy(items, adapter->breakpoints, copied * sizeof(*items));
    *count = adapter->breakpoint_count;
    return emul_result(adapter, XXEMUL_STATUS_OK);
}

static xxwidgets_status set_breakpoint(void *context, uint64_t address, uint64_t *id)
{
    xxwidgets_debugger_xxemul *adapter = context;
    size_t index;
    if (id == NULL) return emul_result(adapter, XXEMUL_STATUS_INVALID_ARGUMENT);
    for (index = 0; index < adapter->breakpoint_count; ++index) {
        if (adapter->breakpoints[index].address == address) {
            *id = adapter->breakpoints[index].id;
            return emul_result(adapter, XXEMUL_STATUS_OK);
        }
    }
    if (adapter->next_breakpoint_id == 0)
        return set_result(adapter, XXWIDGETS_UNAVAILABLE, XXEMUL_STATUS_INVALID_ARGUMENT, "Breakpoint ID space exhausted");
    if (adapter->breakpoint_count == adapter->breakpoint_capacity) {
        size_t capacity = adapter->breakpoint_capacity == 0 ? 8u : adapter->breakpoint_capacity * 2u;
        xxwidgets_debugger_session_breakpoint *items;
        if (capacity < adapter->breakpoint_capacity || capacity > SIZE_MAX / sizeof(*items))
            return emul_result(adapter, XXEMUL_STATUS_OUT_OF_MEMORY);
        items = realloc(adapter->breakpoints, capacity * sizeof(*items));
        if (items == NULL) return emul_result(adapter, XXEMUL_STATUS_OUT_OF_MEMORY);
        adapter->breakpoints = items;
        adapter->breakpoint_capacity = capacity;
    }
    adapter->breakpoints[adapter->breakpoint_count].id = adapter->next_breakpoint_id++;
    adapter->breakpoints[adapter->breakpoint_count].address = address;
    adapter->breakpoints[adapter->breakpoint_count].enabled = 1;
    *id = adapter->breakpoints[adapter->breakpoint_count++].id;
    return emul_result(adapter, XXEMUL_STATUS_OK);
}

static xxwidgets_status remove_breakpoint(void *context, uint64_t id)
{
    xxwidgets_debugger_xxemul *adapter = context;
    size_t index;
    for (index = 0; index < adapter->breakpoint_count; ++index) {
        if (adapter->breakpoints[index].id == id) {
            memmove(adapter->breakpoints + index, adapter->breakpoints + index + 1u,
                (adapter->breakpoint_count - index - 1u) * sizeof(*adapter->breakpoints));
            --adapter->breakpoint_count;
            return emul_result(adapter, XXEMUL_STATUS_OK);
        }
    }
    return emul_result(adapter, XXEMUL_STATUS_INVALID_ARGUMENT);
}

static xxwidgets_status ready_to_execute(xxwidgets_debugger_xxemul *adapter)
{
    if (adapter->running || adapter->in_step || adapter->pending)
        return set_result(adapter, XXWIDGETS_BUSY, XXEMUL_STATUS_INVALID_ARGUMENT,
            "Stop execution and consume its pending event before starting again");
    if (xxemul_is_halted(adapter->emulator)) return emul_result(adapter, XXEMUL_STATUS_HALTED);
    return XXWIDGETS_OK;
}

static void queue_event(xxwidgets_debugger_xxemul *adapter, xxemul_status code, uint64_t address, uint64_t id)
{
    memset(&adapter->event, 0, sizeof(adapter->event));
    adapter->event.kind = code == XXEMUL_STATUS_HALTED ? XXWIDGETS_DEBUGGER_EVENT_EXITED : XXWIDGETS_DEBUGGER_EVENT_STOPPED;
    adapter->event.tid = 1;
    adapter->event.address = address;
    adapter->event.breakpoint_id = id;
    adapter->event.backend_kind = code;
    if (code == XXEMUL_STATUS_HALTED) adapter->event.exit_code = xxemul_process_exit_code(adapter->emulator);
    adapter->running = 0;
    adapter->pending = 1;
    adapter->stopped_at_breakpoint = id != 0;
    adapter->stopped_address = address;
    (void)emul_result(adapter, code);
}

static xxwidgets_status resume(void *context, int pass_exception)
{
    xxwidgets_debugger_xxemul *adapter = context;
    xxwidgets_status result;
    if (pass_exception != 0)
        return set_result(adapter, XXWIDGETS_UNAVAILABLE, XXEMUL_STATUS_INVALID_ARGUMENT,
            "Passing native exceptions is unsupported by the emulator");
    result = ready_to_execute(adapter);
    if (result != XXWIDGETS_OK) return result;
    adapter->skip_breakpoint = adapter->stopped_at_breakpoint;
    adapter->skip_address = adapter->stopped_address;
    adapter->stopped_at_breakpoint = 0;
    adapter->running = 1;
    return emul_result(adapter, XXEMUL_STATUS_OK);
}

static xxwidgets_status step(void *context, uint64_t tid)
{
    xxwidgets_debugger_xxemul *adapter = context;
    xxemul_status code;
    xxemul_step_info info;
    uint64_t address = 0;
    xxwidgets_status result;
    if (!valid_tid(tid)) return emul_result(adapter, XXEMUL_STATUS_INVALID_ARGUMENT);
    result = ready_to_execute(adapter);
    if (result != XXWIDGETS_OK) return result;
    code = xxemul_get_current_address(adapter->emulator, &address);
    if (code != XXEMUL_STATUS_OK) return emul_result(adapter, code);
    adapter->in_step = 1;
    code = xxemul_step(adapter->emulator, &info);
    adapter->in_step = 0;
    (void)xxemul_get_current_address(adapter->emulator, &address);
    adapter->skip_breakpoint = 0;
    queue_event(adapter, code, address, 0);
    /* A stop/exit is the normal result of the action, while execution faults
     * are both returned now and retained in the pending event. */
    if (code == XXEMUL_STATUS_OK || code == XXEMUL_STATUS_HALTED || code == XXEMUL_STATUS_BREAKPOINT)
        return XXWIDGETS_OK;
    return emul_result(adapter, code);
}

static xxwidgets_status interrupt(void *context)
{
    xxwidgets_debugger_xxemul *adapter = context;
    uint64_t address;
    xxemul_status code;
    if (adapter->in_step)
        return set_result(adapter, XXWIDGETS_BUSY, XXEMUL_STATUS_INVALID_ARGUMENT,
            "Interrupt must be called between emulator poll operations");
    if (!adapter->running) return emul_result(adapter, XXEMUL_STATUS_OK);
    code = xxemul_get_current_address(adapter->emulator, &address);
    if (code != XXEMUL_STATUS_OK) return emul_result(adapter, code);
    adapter->skip_breakpoint = 0;
    queue_event(adapter, XXEMUL_STATUS_OK, address, 0);
    return XXWIDGETS_OK;
}

static xxwidgets_status poll(void *context, uint32_t timeout_ms, xxwidgets_debugger_session_event *event)
{
    xxwidgets_debugger_xxemul *adapter = context;
    uint64_t index;
    (void)timeout_ms;
    if (event == NULL) return emul_result(adapter, XXEMUL_STATUS_INVALID_ARGUMENT);
    memset(event, 0, sizeof(*event));
    if (adapter->in_step)
        return set_result(adapter, XXWIDGETS_BUSY, XXEMUL_STATUS_INVALID_ARGUMENT, "Emulator poll cannot be reentered");
    if (adapter->pending) {
        *event = adapter->event;
        adapter->pending = 0;
        (void)emul_result(adapter, (xxemul_status)event->backend_kind);
        return XXWIDGETS_OK;
    }
    if (!adapter->running) return emul_result(adapter, XXEMUL_STATUS_OK);
    for (index = 0; index < adapter->quantum; ++index) {
        uint64_t address = 0;
        xxemul_step_info info;
        xxemul_status code;
        size_t breakpoint;
        int skip;
        code = xxemul_get_current_address(adapter->emulator, &address);
        if (code != XXEMUL_STATUS_OK) {
            queue_event(adapter, code, address, 0);
            break;
        }
        if (xxemul_is_halted(adapter->emulator)) {
            queue_event(adapter, XXEMUL_STATUS_HALTED, address, 0);
            break;
        }
        skip = adapter->skip_breakpoint && adapter->skip_address == address;
        adapter->skip_breakpoint = 0;
        if (!skip) {
            for (breakpoint = 0; breakpoint < adapter->breakpoint_count; ++breakpoint) {
                if (adapter->breakpoints[breakpoint].enabled && adapter->breakpoints[breakpoint].address == address) {
                    queue_event(adapter, XXEMUL_STATUS_BREAKPOINT, address, adapter->breakpoints[breakpoint].id);
                    break;
                }
            }
            if (adapter->pending) break;
        }
        adapter->in_step = 1;
        code = xxemul_step(adapter->emulator, &info);
        adapter->in_step = 0;
        if (code != XXEMUL_STATUS_OK) {
            (void)xxemul_get_current_address(adapter->emulator, &address);
            queue_event(adapter, code, address, 0);
            break;
        }
    }
    if (adapter->pending) {
        *event = adapter->event;
        adapter->pending = 0;
        return XXWIDGETS_OK;
    }
    return emul_result(adapter, XXEMUL_STATUS_OK);
}

static int64_t error_code(void *context)
{
    return ((xxwidgets_debugger_xxemul *)context)->last_code;
}

static const char *error_message(void *context)
{
    return ((xxwidgets_debugger_xxemul *)context)->last_message;
}

static const xxwidgets_debugger_session_ops emul_ops = {
    XXWIDGETS_DEBUGGER_SESSION_ABI,
    get_state, get_target, read_memory, write_memory, disassemble,
    list_breakpoints, set_breakpoint, remove_breakpoint,
    resume, step, interrupt, poll, error_code, error_message
};

xxwidgets_status xxwidgets_debugger_xxemul_create(xxemul *emulator, uint64_t instruction_quantum,
    xxwidgets_debugger_xxemul **adapter)
{
    xxwidgets_debugger_xxemul *result;
    xxemul_arch arch;
    xxemul_mode mode;
    if (adapter != NULL) *adapter = NULL;
    if (emulator == NULL || adapter == NULL || instruction_quantum > UINT64_C(100000)) return XXWIDGETS_INVALID_ARGUMENT;
    arch = xxemul_get_arch(emulator);
    mode = xxemul_get_mode(emulator);
    if (!((arch == XXEMUL_ARCH_X86 && (mode == XXEMUL_MODE_X86_16 || mode == XXEMUL_MODE_X86_32 || mode == XXEMUL_MODE_X86_64))
        || (arch == XXEMUL_ARCH_ARM && (mode == XXEMUL_MODE_ARM_A32 || mode == XXEMUL_MODE_ARM_T32 || mode == XXEMUL_MODE_ARM_A64))))
        return XXWIDGETS_UNAVAILABLE;
    result = calloc(1u, sizeof(*result));
    if (result == NULL) return XXWIDGETS_OUT_OF_MEMORY;
    result->emulator = emulator;
    result->quantum = instruction_quantum == 0 ? UINT64_C(1000) : instruction_quantum;
    result->next_breakpoint_id = 1;
    result->session.ops = &emul_ops;
    result->session.context = result;
    *adapter = result;
    return XXWIDGETS_OK;
}

const xxwidgets_debugger_session *xxwidgets_debugger_xxemul_session(xxwidgets_debugger_xxemul *adapter)
{
    return adapter != NULL ? &adapter->session : NULL;
}

void xxwidgets_debugger_xxemul_destroy(xxwidgets_debugger_xxemul *adapter)
{
    if (adapter == NULL) return;
    free(adapter->breakpoints);
    free(adapter);
}
