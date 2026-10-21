/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_debugger_arch_internal.h"

xxwidgets_status xxwidgets_debugger_format_x86_16(
    const xxwidgets_debugger_x86_state *state, xxwidgets_debugger_register_rows *rows)
{
    static const char *const names[] = {"AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI"};
    static const char *const segments[] = {"ES", "CS", "SS", "DS", "FS", "GS"};
    static const char *const bases[] = {"ES.base", "CS.base", "SS.base", "DS.base", "FS.base", "GS.base"};
    static const char *const debug_names[] = {"DR0", "DR1", "DR2", "DR3", "DR6", "DR7"};
    static const unsigned debug_indices[] = {0u, 1u, 2u, 3u, 6u, 7u};
    xxwidgets_status status;
    size_t i;

    if (!state || !rows) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < 8u; ++i) {
        status = xxwidgets_debugger_add_scalar(rows, names[i], state->gpr[i] & UINT64_C(0xffff), 4u);
        if (status != XXWIDGETS_OK) return status;
    }
    status = xxwidgets_debugger_add_scalar(rows, "IP", state->ip & UINT64_C(0xffff), 4u);
    if (status != XXWIDGETS_OK) return status;
    /* PC and segment bases are linear addresses, independent of the operand width. */
    status = xxwidgets_debugger_add_scalar(rows, "PC", state->pc, 16u);
    if (status != XXWIDGETS_OK) return status;
    status = xxwidgets_debugger_add_scalar(rows, "FLAGS", state->flags & UINT64_C(0xffff), 4u);
    if (status != XXWIDGETS_OK) return status;
    for (i = 0; i < 6u; ++i) {
        status = xxwidgets_debugger_add_scalar(rows, segments[i], state->seg[i], 4u);
        if (status != XXWIDGETS_OK) return status;
        status = xxwidgets_debugger_add_scalar(rows, bases[i], state->seg_base[i], 16u);
        if (status != XXWIDGETS_OK) return status;
    }
    for (i = 0; !(state->unavailable & XXWIDGETS_DEBUGGER_X86_NO_DEBUG_REGISTERS) && i < 6u; ++i) {
        status = xxwidgets_debugger_add_scalar(rows, debug_names[i], state->dr[debug_indices[i]], 16u);
        if (status != XXWIDGETS_OK) return status;
    }
    return XXWIDGETS_OK;
}
