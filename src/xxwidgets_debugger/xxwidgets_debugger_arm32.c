/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxwidgets_debugger_arch_internal.h"

#include <stdio.h>

xxwidgets_status xxwidgets_debugger_format_arm32(
    const xxwidgets_debugger_arm32_state *state,
    xxwidgets_debugger_register_rows *rows)
{
    static const char *const aliases[] = {"SP", "LR", "PC"};
    xxwidgets_status status;
    unsigned i;
    char name[12];

    if (!state || !rows) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < 13u; ++i) {
        snprintf(name, sizeof(name), "R%u", i);
        status = xxwidgets_debugger_add_scalar(rows, name, state->r[i], 8u);
        if (status != XXWIDGETS_OK) return status;
    }
    for (i = 0; i < 3u; ++i) {
        status = xxwidgets_debugger_add_scalar(rows, aliases[i], state->r[13u + i], 8u);
        if (status != XXWIDGETS_OK) return status;
    }
    status = xxwidgets_debugger_add_scalar(rows, "CPSR", state->cpsr, 8u);
    if (status != XXWIDGETS_OK || !state->has_vfp) return status;

    status = xxwidgets_debugger_add_scalar(rows, "FPSCR", state->fpscr, 8u);
    if (status != XXWIDGETS_OK) return status;
    for (i = 0; i < 32u; ++i) {
        snprintf(name, sizeof(name), "D%u", i);
        status = xxwidgets_debugger_add_scalar(rows, name, state->d[i], 16u);
        if (status != XXWIDGETS_OK) return status;
    }
    return XXWIDGETS_OK;
}
