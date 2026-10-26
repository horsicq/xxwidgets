/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_DEBUGGER_PROJECT_DIALOG_H
#define XXWIDGETS_DEBUGGER_PROJECT_DIALOG_H

#include "xxwidgets/xxwidgets.h"
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xxwidgets_debugger_initial_breakpoint {
    XXWIDGETS_DEBUGGER_BREAK_ENTRY_POINT = 0,
    XXWIDGETS_DEBUGGER_BREAK_NONE,
    XXWIDGETS_DEBUGGER_BREAK_PROCESS_CREATION
} xxwidgets_debugger_initial_breakpoint;

typedef enum xxwidgets_debugger_project_format {
    XXWIDGETS_DEBUGGER_FORMAT_AUTO = 0,
    XXWIDGETS_DEBUGGER_FORMAT_COM,
    XXWIDGETS_DEBUGGER_FORMAT_MZ,
    XXWIDGETS_DEBUGGER_FORMAT_PE32,
    XXWIDGETS_DEBUGGER_FORMAT_PE64,
    XXWIDGETS_DEBUGGER_FORMAT_ELF32,
    XXWIDGETS_DEBUGGER_FORMAT_ELF64
} xxwidgets_debugger_project_format;

typedef struct xxwidgets_debugger_project_settings {
    char program[4096];
    char arguments[8192];
    char working_directory[4096];
    xxwidgets_debugger_initial_breakpoint breakpoint;
    int emulate; /* 0: native debugger; 1: emulator. */
    xxwidgets_debugger_project_format format;
} xxwidgets_debugger_project_settings;

#define XXWIDGETS_DEBUGGER_PROJECT_NATIVE 1u
#define XXWIDGETS_DEBUGGER_PROJECT_EMULATOR 2u

typedef struct xxwidgets_debugger_project_info {
    unsigned backends; /* Bitwise OR of the available PROJECT_* backends. */
    xxwidgets_debugger_project_format format; /* Detected emulator format. */
    wchar_t native_label[64];
    wchar_t emulator_label[64];
    char target[160];
    char message[192];
} xxwidgets_debugger_project_info;

/* Called outside input callbacks on initial display, after path changes, and
 * before acceptance. Do not launch targets or show another modal dialog here.
 * Return OK with backends == 0 and an explanatory message for missing files
 * or unsupported targets. All strings must be terminated; target/message are
 * UTF-8. A known format fixes the emulator format; AUTO allows manual choice.
 * This is a compatibility probe, not a guarantee that launching will succeed. */
typedef xxwidgets_status (*xxwidgets_debugger_project_probe_fn)(
    const char *program, xxwidgets_debugger_project_info *info, void *user_data);

typedef struct xxwidgets_debugger_project_options {
    xxwidgets_debugger_project_probe_fn probe;
    void *user_data;
} xxwidgets_debugger_project_options;

/* Zero initialization selects native debugging, auto format, and a breakpoint
 * at the program entry point. Strings are UTF-8 and must be NUL-terminated.
 * Arguments remain text: the caller parses them into argv without a shell.
 * On Start, validated settings replace the caller's object and accepted is 1.
 * Cancellation/close leaves settings unchanged and reports accepted == 0.
 * Call on the UI thread outside an input callback, as other modal APIs do. */
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_project_dialog(
    xxwidgets_widget *owner, xxwidgets_debugger_project_settings *settings, int *accepted);

/* Optional file-aware chooser. The callback supplies available backend labels
 * and target details; xxwidgets itself has no required debugger dependency.
 * A still-available backend selection is preserved when the path changes.
 * No available backend disables Start. If rechecking on Start changes the
 * selected backend or format, the user must accept the updated settings again.
 * NULL options/probe has the same behavior as project_dialog(). */
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_project_dialog_ex(
    xxwidgets_widget *owner, xxwidgets_debugger_project_settings *settings,
    const xxwidgets_debugger_project_options *options, int *accepted);

/* Displays running processes plus an editable decimal PID. Refresh never
 * attaches to, suspends, or modifies any process. Only accepting changes pid.
 * The caller performs the attachment after this dialog returns. */
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_attach_dialog(
    xxwidgets_widget *owner, uint64_t *pid, int *accepted);

#ifdef __cplusplus
}
#endif
#endif
