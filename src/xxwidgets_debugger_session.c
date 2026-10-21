/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets/xxwidgets_debugger_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUGGER_BREAKPOINT_LIMIT 4096u
#define DEBUGGER_INSTRUCTION_TEXT 256u

static int session_valid(const xxwidgets_debugger_session *session)
{
    return session && session->ops && session->ops->abi_version == XXWIDGETS_DEBUGGER_SESSION_ABI;
}

static int range_valid(uint64_t address, size_t size)
{
    return !size || (uint64_t)(size - 1u) <= UINT64_MAX - address;
}

static xxwidgets_status session_state_impl(const xxwidgets_debugger_session *session,
    xxwidgets_debugger_session_state *state, int *provider_failure)
{
    xxwidgets_debugger_session_state staged = XXWIDGETS_DEBUGGER_SESSION_EMPTY;
    xxwidgets_status status;
    if (provider_failure) *provider_failure = 0;
    if (state) *state = XXWIDGETS_DEBUGGER_SESSION_EMPTY;
    if (!session_valid(session) || !state) return XXWIDGETS_INVALID_ARGUMENT;
    if (!session->ops->get_state) return XXWIDGETS_UNAVAILABLE;
    status = session->ops->get_state(session->context, &staged);
    if (provider_failure) *provider_failure = status != XXWIDGETS_OK;
    if (status != XXWIDGETS_OK) return status;
    if (staged < XXWIDGETS_DEBUGGER_SESSION_EMPTY || staged > XXWIDGETS_DEBUGGER_SESSION_HALTED)
        return XXWIDGETS_PLATFORM_ERROR;
    *state = staged;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_debugger_session_get_state(const xxwidgets_debugger_session *session,
    xxwidgets_debugger_session_state *state)
{
    return session_state_impl(session, state, NULL);
}

static xxwidgets_status session_target_impl(const xxwidgets_debugger_session *session,
    uint64_t tid, xxwidgets_debugger_target *target, int *provider_failure)
{
    xxwidgets_debugger_target staged = {0};
    xxwidgets_status status;
    if (provider_failure) *provider_failure = 0;
    if (target) memset(target, 0, sizeof(*target));
    if (!session_valid(session) || !target) return XXWIDGETS_INVALID_ARGUMENT;
    if (!session->ops->get_target) return XXWIDGETS_UNAVAILABLE;
    status = session->ops->get_target(session->context, tid, &staged);
    if (provider_failure) *provider_failure = status != XXWIDGETS_OK;
    if (status != XXWIDGETS_OK) return status;
    if (!xxwidgets_debugger_arch_word_size(staged.cpu.arch)) return XXWIDGETS_UNAVAILABLE;
    staged.big_endian = !!staged.big_endian;
    *target = staged;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_debugger_session_get_target(const xxwidgets_debugger_session *session,
    uint64_t tid, xxwidgets_debugger_target *target)
{
    return session_target_impl(session, tid, target, NULL);
}

static xxwidgets_status session_read_impl(const xxwidgets_debugger_session *session,
    uint64_t address, void *data, size_t size, size_t *done, int *provider_failure)
{
    xxwidgets_status status;
    if (provider_failure) *provider_failure = 0;
    if (done) *done = 0;
    if (!session_valid(session) || !done || (!data && size) || !range_valid(address, size))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (!session->ops->read_memory) return XXWIDGETS_UNAVAILABLE;
    status = session->ops->read_memory(session->context, address, data, size, done);
    if (provider_failure) *provider_failure = status != XXWIDGETS_OK;
    if (*done > size) { *done = 0; return XXWIDGETS_PLATFORM_ERROR; }
    return status;
}

xxwidgets_status xxwidgets_debugger_session_read_memory(const xxwidgets_debugger_session *session,
    uint64_t address, void *data, size_t size, size_t *done)
{
    return session_read_impl(session, address, data, size, done, NULL);
}

xxwidgets_status xxwidgets_debugger_session_write_memory(const xxwidgets_debugger_session *session,
    uint64_t address, const void *data, size_t size, size_t *done)
{
    xxwidgets_status status;
    if (done) *done = 0;
    if (!session_valid(session) || !done || (!data && size) || !range_valid(address, size))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (!session->ops->write_memory) return XXWIDGETS_UNAVAILABLE;
    status = session->ops->write_memory(session->context, address, data, size, done);
    if (*done > size) { *done = 0; return XXWIDGETS_PLATFORM_ERROR; }
    return status;
}

static xxwidgets_status session_decode_impl(const xxwidgets_debugger_session *session,
    uint64_t tid, uint64_t address, char *text, size_t capacity, uint32_t *length, int *provider_failure)
{
    xxwidgets_status status;
    uint32_t staged = 0;
    if (provider_failure) *provider_failure = 0;
    if (text && capacity) text[0] = 0;
    if (length) *length = 0;
    if (!session_valid(session) || !text || !capacity || !length) return XXWIDGETS_INVALID_ARGUMENT;
    if (!session->ops->disassemble) return XXWIDGETS_UNAVAILABLE;
    status = session->ops->disassemble(session->context, tid, address, text, capacity, &staged);
    if (provider_failure) *provider_failure = status != XXWIDGETS_OK;
    if (status == XXWIDGETS_OK && (!staged || staged > 16u || !range_valid(address, staged) || !memchr(text, 0, capacity)))
        status = XXWIDGETS_PLATFORM_ERROR;
    if (status != XXWIDGETS_OK) { text[0] = 0; return status; }
    *length = staged;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_debugger_session_disassemble(const xxwidgets_debugger_session *session,
    uint64_t tid, uint64_t address, char *text, size_t capacity, uint32_t *length)
{
    return session_decode_impl(session, tid, address, text, capacity, length, NULL);
}

xxwidgets_status xxwidgets_debugger_session_list_breakpoints(const xxwidgets_debugger_session *session,
    xxwidgets_debugger_session_breakpoint *breakpoints, size_t capacity, size_t *count)
{
    if (count) *count = 0;
    if (!session_valid(session) || !count || (!breakpoints && capacity) || capacity > SIZE_MAX / sizeof(*breakpoints))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (!session->ops->list_breakpoints) return XXWIDGETS_UNAVAILABLE;
    return session->ops->list_breakpoints(session->context, breakpoints, capacity, count);
}

xxwidgets_status xxwidgets_debugger_session_set_breakpoint(const xxwidgets_debugger_session *session,
    uint64_t address, uint64_t *id)
{
    uint64_t staged = 0;
    xxwidgets_status status;
    if (id) *id = 0;
    if (!session_valid(session) || !id) return XXWIDGETS_INVALID_ARGUMENT;
    if (!session->ops->set_breakpoint) return XXWIDGETS_UNAVAILABLE;
    status = session->ops->set_breakpoint(session->context, address, &staged);
    if (status == XXWIDGETS_OK) *id = staged;
    return status;
}

xxwidgets_status xxwidgets_debugger_session_remove_breakpoint(const xxwidgets_debugger_session *session, uint64_t id)
{
    if (!session_valid(session)) return XXWIDGETS_INVALID_ARGUMENT;
    if (!session->ops->remove_breakpoint) return XXWIDGETS_UNAVAILABLE;
    return session->ops->remove_breakpoint(session->context, id);
}

xxwidgets_status xxwidgets_debugger_session_resume(const xxwidgets_debugger_session *session, int pass_exception)
{
    if (!session_valid(session)) return XXWIDGETS_INVALID_ARGUMENT;
    if (!session->ops->resume) return XXWIDGETS_UNAVAILABLE;
    return session->ops->resume(session->context, !!pass_exception);
}

xxwidgets_status xxwidgets_debugger_session_step(const xxwidgets_debugger_session *session, uint64_t tid)
{
    if (!session_valid(session)) return XXWIDGETS_INVALID_ARGUMENT;
    if (!session->ops->step) return XXWIDGETS_UNAVAILABLE;
    return session->ops->step(session->context, tid);
}

xxwidgets_status xxwidgets_debugger_session_interrupt(const xxwidgets_debugger_session *session)
{
    if (!session_valid(session)) return XXWIDGETS_INVALID_ARGUMENT;
    if (!session->ops->interrupt) return XXWIDGETS_UNAVAILABLE;
    return session->ops->interrupt(session->context);
}

xxwidgets_status xxwidgets_debugger_session_poll(const xxwidgets_debugger_session *session,
    uint32_t timeout_ms, xxwidgets_debugger_session_event *event)
{
    xxwidgets_debugger_session_event staged = {0};
    xxwidgets_status status;
    if (event) memset(event, 0, sizeof(*event));
    if (!session_valid(session) || !event) return XXWIDGETS_INVALID_ARGUMENT;
    if (!session->ops->poll) return XXWIDGETS_UNAVAILABLE;
    status = session->ops->poll(session->context, timeout_ms, &staged);
    if (status != XXWIDGETS_OK) return status;
    if (staged.kind < XXWIDGETS_DEBUGGER_EVENT_NONE || staged.kind > XXWIDGETS_DEBUGGER_EVENT_OTHER ||
        (!staged.data && staged.data_size)) return XXWIDGETS_PLATFORM_ERROR;
    *event = staged;
    return XXWIDGETS_OK;
}

typedef struct debugger_snapshot {
    int enabled[4];
    xxwidgets_debugger_instruction instructions[XXWIDGETS_DEBUGGER_MAX_INSTRUCTIONS];
    char instruction_bytes[XXWIDGETS_DEBUGGER_MAX_INSTRUCTIONS][49];
    char instruction_text[XXWIDGETS_DEBUGGER_MAX_INSTRUCTIONS][DEBUGGER_INSTRUCTION_TEXT];
    size_t instruction_count;
    xxwidgets_debugger_register_rows registers;
    xxwidgets_debugger_stack_entry stack[XXWIDGETS_DEBUGGER_MAX_STACK];
    char stack_values[XXWIDGETS_DEBUGGER_MAX_STACK][17];
    char stack_comments[XXWIDGETS_DEBUGGER_MAX_STACK][48];
    size_t stack_count;
    unsigned char memory[XXWIDGETS_DEBUGGER_MAX_MEMORY];
    size_t memory_size;
    uint64_t memory_address;
    unsigned char stack_bytes[XXWIDGETS_DEBUGGER_MAX_STACK * 8u];
    char pane_status[4][160];
    xxwidgets_debugger_session_error first_error;
} debugger_snapshot;

static void capture_error(xxwidgets_debugger_session_error *error, const xxwidgets_debugger_session *session,
    xxwidgets_status status)
{
    const char *message = NULL;
    memset(error, 0, sizeof(*error));
    error->status = status;
    if (status == XXWIDGETS_OK) return;
    if (session) {
        if (session->ops->error_code) error->backend_code = session->ops->error_code(session->context);
        if (session->ops->error_message) message = session->ops->error_message(session->context);
    }
    if (!message || !*message) message = xxwidgets_status_string(status);
    snprintf(error->message, sizeof(error->message), "%s", message);
}

static void debugger_error(debugger_snapshot *snapshot, unsigned pane, const xxwidgets_debugger_session *session,
    xxwidgets_status status, size_t received, size_t requested)
{
    xxwidgets_debugger_session_error error;
    /* An OK short read is a provider-independent availability error. Avoid
     * copying diagnostics left over from an unrelated provider operation. */
    if (status == XXWIDGETS_OK) { status = XXWIDGETS_UNAVAILABLE; session = NULL; }
    capture_error(&error, session, status);
    if (snapshot->first_error.status == XXWIDGETS_OK) snapshot->first_error = error;
    if (snapshot->pane_status[pane][0]) return;
    if (requested)
        snprintf(snapshot->pane_status[pane], sizeof(snapshot->pane_status[pane]),
            "%.110s (%zu/%zu)", error.message, received, requested);
    else
        snprintf(snapshot->pane_status[pane], sizeof(snapshot->pane_status[pane]), "%s", error.message);
}

static void mark_register_changes(xxwidgets_debugger *view, debugger_snapshot *snapshot)
{
    xxwidgets_widget *table = xxwidgets_debugger_control(view, XXWIDGETS_DEBUGGER_CONTROL_REGISTERS);
    size_t old_count = xxwidgets_debugger_registers_count(table), i, j;
    if (old_count > 4096u) old_count = 4096u;
    for (i = 0; i < snapshot->registers.count; ++i) {
        for (j = 0; j < old_count; ++j) {
            xxwidgets_debugger_register previous;
            if (xxwidgets_debugger_registers_get_row(table, j, &previous) != XXWIDGETS_OK) break;
            if (previous.name && !strcmp(previous.name, snapshot->registers.rows[i].name)) {
                if (!previous.value || strcmp(previous.value, snapshot->registers.rows[i].value))
                    snapshot->registers.rows[i].flags |= XXWIDGETS_DEBUGGER_CHANGED;
                break;
            }
        }
    }
}

static void read_instructions(debugger_snapshot *snapshot, const xxwidgets_debugger_session *session, uint64_t tid,
    uint64_t address, uint64_t pc, size_t count, const xxwidgets_debugger_session_breakpoint *breakpoints, size_t breakpoint_count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        uint32_t length = 0;
        size_t got = 0, j;
        unsigned char bytes[16];
        xxwidgets_status status;
        int provider_failure;
        xxwidgets_debugger_instruction *row = &snapshot->instructions[i];
        status = session_decode_impl(session, tid, address, snapshot->instruction_text[i],
            DEBUGGER_INSTRUCTION_TEXT, &length, &provider_failure);
        if (status != XXWIDGETS_OK) {
            debugger_error(snapshot, XXWIDGETS_DEBUGGER_CONTROL_DISASM, provider_failure ? session : NULL, status, i, count);
            break;
        }
        status = session_read_impl(session, address, bytes, length, &got, &provider_failure);
        if (status != XXWIDGETS_OK || got != length)
            debugger_error(snapshot, XXWIDGETS_DEBUGGER_CONTROL_DISASM, provider_failure ? session : NULL, status, got, length);
        for (j = 0; j < length; ++j) {
            char *at = snapshot->instruction_bytes[i] + j * 3u;
            if (j < got) snprintf(at, 4u, "%02X%s", (unsigned)bytes[j], j + 1u < length ? " " : "");
            else snprintf(at, 4u, "??%s", j + 1u < length ? " " : "");
        }
        row->address = address;
        row->bytes = snapshot->instruction_bytes[i];
        row->instruction = snapshot->instruction_text[i];
        row->comment = "";
        if (address == pc) row->flags |= XXWIDGETS_DEBUGGER_CURRENT;
        for (j = 0; j < breakpoint_count; ++j)
            if (breakpoints[j].enabled && breakpoints[j].address == address)
                row->flags |= XXWIDGETS_DEBUGGER_BREAKPOINT;
        ++snapshot->instruction_count;
        if ((uint64_t)length > UINT64_MAX - address) {
            if (i + 1u < count)
                debugger_error(snapshot, XXWIDGETS_DEBUGGER_CONTROL_DISASM, NULL, XXWIDGETS_UNAVAILABLE, i + 1u, count);
            break;
        }
        address += length;
    }
}

static void read_memory(debugger_snapshot *snapshot, const xxwidgets_debugger_session *session, uint64_t address, size_t size)
{
    xxwidgets_status status;
    int provider_failure;
    snapshot->memory_address = address;
    if (!range_valid(address, size)) {
        debugger_error(snapshot, XXWIDGETS_DEBUGGER_CONTROL_HEX, NULL, XXWIDGETS_INVALID_ARGUMENT, 0, size);
        return;
    }
    status = session_read_impl(session, address, snapshot->memory, size, &snapshot->memory_size, &provider_failure);
    if (status != XXWIDGETS_OK || snapshot->memory_size != size)
        debugger_error(snapshot, XXWIDGETS_DEBUGGER_CONTROL_HEX, provider_failure ? session : NULL,
            status, snapshot->memory_size, size);
}

static void read_stack(debugger_snapshot *snapshot, const xxwidgets_debugger_session *session, uint64_t address,
    const xxwidgets_debugger_target *target, size_t count)
{
    size_t word_size = xxwidgets_debugger_arch_word_size(target->cpu.arch);
    size_t size = count * word_size, got = 0, i;
    xxwidgets_status status;
    int provider_failure;
    if (!range_valid(address, size)) {
        debugger_error(snapshot, XXWIDGETS_DEBUGGER_CONTROL_STACK, NULL, XXWIDGETS_INVALID_ARGUMENT, 0, size);
        return;
    }
    status = session_read_impl(session, address, snapshot->stack_bytes, size, &got, &provider_failure);
    if (status != XXWIDGETS_OK || got != size)
        debugger_error(snapshot, XXWIDGETS_DEBUGGER_CONTROL_STACK, provider_failure ? session : NULL, status, got, size);
    snapshot->stack_count = got ? (got + word_size - 1u) / word_size : 1u;
    for (i = 0; i < snapshot->stack_count; ++i) {
        xxwidgets_debugger_stack_entry *row = &snapshot->stack[i];
        size_t offset = i * word_size, available = got > offset ? got - offset : 0;
        row->address = address + offset;
        row->value = snapshot->stack_values[i];
        row->comment = snapshot->stack_comments[i];
        if (row->address == target->sp) row->flags |= XXWIDGETS_DEBUGGER_CURRENT;
        if (available > word_size) available = word_size;
        (void)xxwidgets_debugger_format_stack_word(target->cpu.arch, target->big_endian,
            snapshot->stack_bytes + offset, available, snapshot->stack_values[i]);
        if (available < word_size)
            snprintf(snapshot->stack_comments[i], sizeof(snapshot->stack_comments[i]), "%s", got ? "partial read" : "unreadable");
        else if (row->address == target->sp)
            snprintf(snapshot->stack_comments[i], sizeof(snapshot->stack_comments[i]), "SP");
    }
}

static xxwidgets_status apply_snapshot(xxwidgets_debugger *view, const debugger_snapshot *snapshot)
{
    xxwidgets_status status = XXWIDGETS_OK;
    unsigned pane;
    for (pane = 0; pane < 4u && status == XXWIDGETS_OK; ++pane) {
        if (!snapshot->enabled[pane]) continue;
        switch (pane) {
        case XXWIDGETS_DEBUGGER_CONTROL_DISASM:
            status = xxwidgets_debugger_set_disassembly(view, snapshot->instructions, snapshot->instruction_count);
            break;
        case XXWIDGETS_DEBUGGER_CONTROL_REGISTERS:
            status = xxwidgets_debugger_set_registers(view, snapshot->registers.rows, snapshot->registers.count);
            break;
        case XXWIDGETS_DEBUGGER_CONTROL_HEX:
            status = xxwidgets_debugger_set_memory(view, snapshot->memory_address, snapshot->memory, snapshot->memory_size);
            break;
        default:
            status = xxwidgets_debugger_set_stack(view, snapshot->stack, snapshot->stack_count);
            break;
        }
        if (status == XXWIDGETS_OK)
            status = xxwidgets_debugger_set_pane_status(view, (xxwidgets_debugger_control_id)pane, snapshot->pane_status[pane]);
    }
    return status;
}

xxwidgets_status xxwidgets_debugger_refresh_session(xxwidgets_debugger *view, const xxwidgets_debugger_session *session,
    uint64_t tid, const xxwidgets_debugger_refresh_options *options, xxwidgets_debugger_session_error *error)
{
    xxwidgets_debugger_refresh_options defaults = {0};
    static const xxwidgets_kind compatible_kinds[] = {
        XXWIDGETS_DEBUGGER_DISASM, XXWIDGETS_DEBUGGER_REGISTERS, XXWIDGETS_HEXVIEW, XXWIDGETS_DEBUGGER_STACK
    };
    xxwidgets_debugger_session_state state;
    xxwidgets_debugger_target target = {0};
    xxwidgets_debugger_session_breakpoint *breakpoints = NULL;
    debugger_snapshot *snapshot;
    xxwidgets_status status, display_status;
    uint64_t disassembly_address, memory_address, stack_address;
    size_t instruction_count, memory_size, stack_count, breakpoint_count = 0, breakpoint_capacity = 0;
    unsigned pane;
    int enabled[4] = {0}, any_enabled = 0, need_target, provider_failure;
    if (error) memset(error, 0, sizeof(*error));
    if (!view || !session_valid(session)) return XXWIDGETS_INVALID_ARGUMENT;
    if (!options) options = &defaults;
    instruction_count = options->instruction_count ? options->instruction_count : 32u;
    memory_size = options->memory_size ? options->memory_size : 256u;
    stack_count = options->stack_count ? options->stack_count : 32u;
    if (instruction_count > XXWIDGETS_DEBUGGER_MAX_INSTRUCTIONS || memory_size > XXWIDGETS_DEBUGGER_MAX_MEMORY ||
        stack_count > XXWIDGETS_DEBUGGER_MAX_STACK) return XXWIDGETS_INVALID_ARGUMENT;
    status = session_state_impl(session, &state, &provider_failure);
    if (status != XXWIDGETS_OK) {
        if (error) capture_error(error, provider_failure ? session : NULL, status);
        return status;
    }
    if (state != XXWIDGETS_DEBUGGER_SESSION_STOPPED && state != XXWIDGETS_DEBUGGER_SESSION_HALTED) {
        if (error) capture_error(error, NULL, XXWIDGETS_BUSY);
        return XXWIDGETS_BUSY;
    }
    for (pane = 0; pane < 4u; ++pane) {
        xxwidgets_widget *control;
        int visible;
        status = xxwidgets_debugger_get_panel_visible(view, (xxwidgets_debugger_control_id)pane, &visible);
        if (status != XXWIDGETS_OK) return status;
        control = xxwidgets_debugger_control(view, (xxwidgets_debugger_control_id)pane);
        enabled[pane] = visible && control && xxwidgets_widget_kind(control) == compatible_kinds[pane];
        any_enabled |= enabled[pane];
    }
    if (!any_enabled) return XXWIDGETS_OK;
    need_target = enabled[XXWIDGETS_DEBUGGER_CONTROL_DISASM] || enabled[XXWIDGETS_DEBUGGER_CONTROL_REGISTERS] ||
        enabled[XXWIDGETS_DEBUGGER_CONTROL_STACK] || (enabled[XXWIDGETS_DEBUGGER_CONTROL_HEX] && !options->has_memory_address);
    if (need_target) {
        status = session_target_impl(session, tid, &target, &provider_failure);
        if (status != XXWIDGETS_OK) {
            if (error) capture_error(error, provider_failure ? session : NULL, status);
            return status;
        }
    }
    snapshot = (debugger_snapshot *)calloc(1, sizeof(*snapshot));
    if (!snapshot) return XXWIDGETS_OUT_OF_MEMORY;
    memcpy(snapshot->enabled, enabled, sizeof(enabled));
    disassembly_address = options->has_disassembly_address ? options->disassembly_address : target.pc;
    memory_address = options->has_memory_address ? options->memory_address : target.pc;
    stack_address = options->has_stack_address ? options->stack_address : target.sp;
    if (enabled[XXWIDGETS_DEBUGGER_CONTROL_REGISTERS]) {
        status = xxwidgets_debugger_format_registers(&target.cpu, &snapshot->registers);
        if (status != XXWIDGETS_OK) { free(snapshot); return status; }
        mark_register_changes(view, snapshot);
    }
    if (enabled[XXWIDGETS_DEBUGGER_CONTROL_DISASM] && session->ops->list_breakpoints) {
        const xxwidgets_debugger_session *diagnostic_session = session;
        status = xxwidgets_debugger_session_list_breakpoints(session, NULL, 0, &breakpoint_count);
        if ((status == XXWIDGETS_OK || status == XXWIDGETS_BUFFER_TOO_SMALL) && breakpoint_count) {
            breakpoint_capacity = breakpoint_count < DEBUGGER_BREAKPOINT_LIMIT ? breakpoint_count : DEBUGGER_BREAKPOINT_LIMIT;
            breakpoints = (xxwidgets_debugger_session_breakpoint *)calloc(breakpoint_capacity, sizeof(*breakpoints));
            if (!breakpoints) { free(snapshot); return XXWIDGETS_OUT_OF_MEMORY; }
            status = xxwidgets_debugger_session_list_breakpoints(session, breakpoints, breakpoint_capacity, &breakpoint_count);
            if (breakpoint_count > breakpoint_capacity) {
                breakpoint_count = breakpoint_capacity;
                if (status == XXWIDGETS_OK) { status = XXWIDGETS_BUFFER_TOO_SMALL; diagnostic_session = NULL; }
            }
        }
        if (status != XXWIDGETS_OK) {
            debugger_error(snapshot, XXWIDGETS_DEBUGGER_CONTROL_DISASM, diagnostic_session, status, 0, 0);
            if (status != XXWIDGETS_BUFFER_TOO_SMALL) breakpoint_count = 0;
        }
    }
    if (enabled[XXWIDGETS_DEBUGGER_CONTROL_DISASM])
        read_instructions(snapshot, session, tid, disassembly_address, target.pc, instruction_count, breakpoints, breakpoint_count);
    if (enabled[XXWIDGETS_DEBUGGER_CONTROL_HEX]) read_memory(snapshot, session, memory_address, memory_size);
    if (enabled[XXWIDGETS_DEBUGGER_CONTROL_STACK]) read_stack(snapshot, session, stack_address, &target, stack_count);
    free(breakpoints);
    if (error) *error = snapshot->first_error;
    display_status = apply_snapshot(view, snapshot);
    if (display_status == XXWIDGETS_OK) display_status = snapshot->first_error.status;
    free(snapshot);
    return display_status;
}
