/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXWIDGETS_DEBUGGER_TABLES_H
#define XXWIDGETS_DEBUGGER_TABLES_H

#include "xxwidgets/xxwidgets.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XXWIDGETS_DEBUGGER_CURRENT 1u
#define XXWIDGETS_DEBUGGER_BREAKPOINT 2u
#define XXWIDGETS_DEBUGGER_CHANGED 4u

typedef struct xxwidgets_debugger_instruction {
    uint64_t address;
    const char *bytes, *instruction, *comment;
    unsigned flags; /* CURRENT | BREAKPOINT. */
} xxwidgets_debugger_instruction;

typedef struct xxwidgets_debugger_register {
    const char *name, *value;
    unsigned flags; /* CHANGED. */
} xxwidgets_debugger_register;

typedef struct xxwidgets_debugger_stack_entry {
    uint64_t address;
    const char *value, *comment;
    unsigned flags; /* CURRENT | CHANGED. */
} xxwidgets_debugger_stack_entry;

/* Read-only tables for native desktops and TUI. Create the matching
 * XXWIDGETS_DEBUGGER_* kind with xxwidgets_widget_create. Setters deep-copy
 * UTF-8 records, escape control characters for display, and emit no input
 * events. NULL strings mean empty cells. Replacement is transactional and
 * selects the first CURRENT row, or the first row; NULL/0 clears the table.
 * Markers remain visible without color: > current, B breakpoint, * changed.
 * Record getters borrow strings until replacement/clear/destruction. Empty
 * selection returns SIZE_MAX and a zeroed record. All calls use the UI thread.
 */
#define XXWIDGETS_DECLARE_DEBUGGER_TABLE(name, type) \
    XXWIDGETS_API xxwidgets_status xxwidgets_debugger_##name##_set_rows(xxwidgets_widget *, const type *, size_t count); \
    XXWIDGETS_API xxwidgets_status xxwidgets_debugger_##name##_clear(xxwidgets_widget *); \
    XXWIDGETS_API size_t xxwidgets_debugger_##name##_count(const xxwidgets_widget *); \
    XXWIDGETS_API xxwidgets_status xxwidgets_debugger_##name##_get_row(const xxwidgets_widget *, size_t index, type *); \
    XXWIDGETS_API xxwidgets_status xxwidgets_debugger_##name##_get_selection(xxwidgets_widget *, size_t *index, type *)
XXWIDGETS_DECLARE_DEBUGGER_TABLE(disasm, xxwidgets_debugger_instruction);
XXWIDGETS_DECLARE_DEBUGGER_TABLE(registers, xxwidgets_debugger_register);
XXWIDGETS_DECLARE_DEBUGGER_TABLE(stack, xxwidgets_debugger_stack_entry);
#undef XXWIDGETS_DECLARE_DEBUGGER_TABLE

#ifdef __cplusplus
}
#endif
#endif
