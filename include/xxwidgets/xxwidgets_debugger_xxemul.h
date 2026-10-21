/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_DEBUGGER_XXEMUL_H
#define XXWIDGETS_DEBUGGER_XXEMUL_H

#include "xxwidgets/xxwidgets_debugger_session.h"
#include "xxemul/xxemul.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_debugger_xxemul xxwidgets_debugger_xxemul;

/* Optional direct xxemul provider. The emulator is borrowed and must outlive
 * the adapter. Use all calls on one thread, serialized with direct emulator
 * calls; do not reenter from emulator callbacks. The adapter never changes
 * output/memory hooks, debug-trap settings, or emulator ownership.
 *
 * resume marks the session RUNNING. Each subsequent poll executes at most
 * instruction_quantum instructions, without sleeping; zero selects 1000,
 * and values above 100000 are rejected. This is an instruction budget, not a
 * wall-clock deadline: an emulated OS service can itself block. step executes
 * one instruction synchronously; its stop/error event is delivered by poll.
 * interrupt stops between poll calls. Consume pending events before resuming
 * or stepping again. Nonzero pass_exception is unsupported. TIDs 0 and 1 both
 * select the emulator's single CPU; other IDs are rejected.
 *
 * Execution breakpoints are adapter-owned address checks, never code patches.
 * Continuing after one executes that address once before checking it again.
 * Raw guest traps follow the emulator's existing debug-trap configuration.
 * Poll reports execution faults as STOPPED events with the xxemul status in
 * backend_kind and the diagnostic accessors. A successful step/interrupt has
 * backend_kind XXEMUL_STATUS_OK. HALTED retains readable final CPU/memory.
 *
 * All five architecture views are supported, including ARM A32/T32 switches.
 * x86 debug registers are not exposed by xxemul (marked unavailable), and
 * ARM32 VFP is unavailable. Memory/disassembly use xxemul's public memory view;
 * DOS protected-mode paging translation is not provided by that public API.
 * Disassembly uses cdisasm at the selected address in the current CPU mode,
 * without changing registers or executing the target.
 */
xxwidgets_status xxwidgets_debugger_xxemul_create(xxemul *emulator, uint64_t instruction_quantum,
    xxwidgets_debugger_xxemul **adapter);
const xxwidgets_debugger_session *xxwidgets_debugger_xxemul_session(xxwidgets_debugger_xxemul *adapter);
void xxwidgets_debugger_xxemul_destroy(xxwidgets_debugger_xxemul *adapter);

#ifdef __cplusplus
}
#endif
#endif
