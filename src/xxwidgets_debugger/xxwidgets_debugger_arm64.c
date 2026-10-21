/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxwidgets_debugger_arch_internal.h"

#include <stdio.h>

xxwidgets_status xxwidgets_debugger_format_arm64(
    const xxwidgets_debugger_arm64_state *state,
    xxwidgets_debugger_register_rows *rows)
{
    xxwidgets_status status;
    unsigned i;
    char name[12];

    if (!state || !rows) return XXWIDGETS_INVALID_ARGUMENT;
    /* Preserve all pointer bits, including ARM64e authentication bits. */
    for (i = 0; i < 31u; ++i) {
        snprintf(name, sizeof(name), "X%u", i);
        status = xxwidgets_debugger_add_scalar(rows, name, state->x[i], 16u);
        if (status != XXWIDGETS_OK) return status;
    }
    status = xxwidgets_debugger_add_scalar(rows, "SP", state->sp, 16u);
    if (status != XXWIDGETS_OK) return status;
    status = xxwidgets_debugger_add_scalar(rows, "PC", state->pc, 16u);
    if (status != XXWIDGETS_OK) return status;
    status = xxwidgets_debugger_add_scalar(rows, "CPSR", state->cpsr, 8u);
    if (status != XXWIDGETS_OK || !state->has_simd) return status;

    status = xxwidgets_debugger_add_scalar(rows, "FPSR", state->fpsr, 8u);
    if (status != XXWIDGETS_OK) return status;
    status = xxwidgets_debugger_add_scalar(rows, "FPCR", state->fpcr, 8u);
    if (status != XXWIDGETS_OK) return status;
    for (i = 0; i < 32u; ++i) {
        snprintf(name, sizeof(name), "V%u", i);
        status = xxwidgets_debugger_add_vector(rows, name, state->v[i]);
        if (status != XXWIDGETS_OK) return status;
    }
    return XXWIDGETS_OK;
}
