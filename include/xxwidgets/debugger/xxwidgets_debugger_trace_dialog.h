/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_DEBUGGER_TRACE_DIALOG_H
#define XXWIDGETS_DEBUGGER_TRACE_DIALOG_H

#include "xxwidgets/xxwidgets.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_debugger_trace_settings {
    uint64_t instruction_limit;
    int write_file;
    char file_path[4096];
    char format[1024];
} xxwidgets_debugger_trace_settings;

/* Shows tracing options without executing instructions or opening an output
 * file. Initialize instruction_limit to a positive count (for example 1000),
 * write_file to 0 or 1, and the UTF-8 strings to NUL-terminated text. Suggested
 * defaults are "trace.txt" and "Address: 0x%PC%". The format has one record per
 * line; %% escapes a literal percent sign and %NAME% denotes a register.
 * The caller validates register names for the target and creates the file
 * without replacing an existing file. Values describe each instruction before
 * execution; tracing stops at the limit or when the target exits.
 * On Start, validated settings replace the caller's object and accepted is 1.
 * Cancellation/close leaves settings unchanged and reports accepted == 0.
 * Call on the UI thread outside an input callback, as other modal APIs do. */
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_trace_dialog(
    xxwidgets_widget *owner, xxwidgets_debugger_trace_settings *settings, int *accepted);

#ifdef __cplusplus
}
#endif
#endif
