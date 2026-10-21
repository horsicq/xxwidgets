/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_DEBUGGER_ARCH_H
#define XXWIDGETS_DEBUGGER_ARCH_H

#include "xxwidgets/xxwidgets_debugger.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xxwidgets_debugger_arch {
    XXWIDGETS_DEBUGGER_ARCH_UNKNOWN = 0,
    XXWIDGETS_DEBUGGER_ARCH_X86_16,
    XXWIDGETS_DEBUGGER_ARCH_X86_32,
    XXWIDGETS_DEBUGGER_ARCH_X86_64,
    XXWIDGETS_DEBUGGER_ARCH_ARM32,
    XXWIDGETS_DEBUGGER_ARCH_ARM64
} xxwidgets_debugger_arch;

#define XXWIDGETS_DEBUGGER_X86_NO_DEBUG_REGISTERS 1u

/* gpr[] follows x86 encoding order, matching xxdebug: AX/CX/DX/BX/SP/BP/SI/DI,
 * then R8..R15. Narrow modes use the low 16 or 32 bits. seg[] and seg_base[]
 * are ES/CS/SS/DS/FS/GS. ip is the instruction offset; pc and seg_base[] are
 * linear addresses. dr[] contains DR0..DR7 (DR4/DR5 are reserved). No xxdebug
 * dependency is needed to populate these snapshots. */
typedef struct xxwidgets_debugger_x86_state {
    uint64_t gpr[16];
    uint64_t ip, pc, flags;
    uint16_t seg[6];
    uint64_t seg_base[6], dr[8];
    /* Set NO_DEBUG_REGISTERS when a provider cannot read DR0..DR7. Such
     * rows are omitted, rather than presenting fabricated zero values. */
    unsigned unavailable;
} xxwidgets_debugger_x86_state;

/* r[13] is SP, r[14] is LR, r[15] is PC. cpsr also carries the Thumb state.
 * Set has_vfp nonzero when D0..D31 and FPSCR are available. d[] contains raw
 * 64-bit register bits; VFP values are presented as hexadecimal bits. */
typedef struct xxwidgets_debugger_arm32_state {
    uint32_t r[16], cpsr;
    int has_vfp;
    uint64_t d[32];
    uint32_t fpscr;
} xxwidgets_debugger_arm32_state;

/* x[29] is FP and x[30] is LR. Set has_simd nonzero when V0..V31 and FPSR/FPCR
 * are available. v[] contains little-endian bytes of each 128-bit register,
 * independent of the host byte order. ARM64e uses this same presentation. */
typedef struct xxwidgets_debugger_arm64_state {
    uint64_t x[31], sp, pc;
    uint32_t cpsr, fpsr, fpcr;
    int has_simd;
    uint8_t v[32][16];
} xxwidgets_debugger_arm64_state;

typedef struct xxwidgets_debugger_cpu_snapshot {
    xxwidgets_debugger_arch arch;
    union {
        xxwidgets_debugger_x86_state x86;
        xxwidgets_debugger_arm32_state arm32;
        xxwidgets_debugger_arm64_state arm64;
    } regs;
} xxwidgets_debugger_cpu_snapshot;

#define XXWIDGETS_DEBUGGER_REGISTER_ROW_CAPACITY 72u
#define XXWIDGETS_DEBUGGER_REGISTER_NAME_CAPACITY 12u
#define XXWIDGETS_DEBUGGER_REGISTER_VALUE_CAPACITY 33u

/* Storage for formatted rows: every name/value pointer references this same
 * object's buffers. Do not shallow-copy or move a populated result; format
 * into the destination again instead. The table setters deep-copy its rows,
 * so this storage may be released immediately after a successful setter. */
typedef struct xxwidgets_debugger_register_rows {
    size_t count;
    xxwidgets_debugger_register rows[XXWIDGETS_DEBUGGER_REGISTER_ROW_CAPACITY];
    char names[XXWIDGETS_DEBUGGER_REGISTER_ROW_CAPACITY][XXWIDGETS_DEBUGGER_REGISTER_NAME_CAPACITY];
    char values[XXWIDGETS_DEBUGGER_REGISTER_ROW_CAPACITY][XXWIDGETS_DEBUGGER_REGISTER_VALUE_CAPACITY];
} xxwidgets_debugger_register_rows;

/* Pure formatting, with no UI/session/backend access. Values are uppercase,
 * fixed-width hexadecimal without a prefix; row flags start at zero. Unknown
 * architectures and NULL arguments return INVALID_ARGUMENT. Failure leaves
 * output unchanged. Hosts may add CHANGED markers before setting the rows. */
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_format_registers(const xxwidgets_debugger_cpu_snapshot *cpu,
                                                                  xxwidgets_debugger_register_rows *output);

/* Convenience setters for a standalone Registers widget or the currently
 * installed Registers pane. They format and deep-copy a snapshot on the UI
 * thread, following the normal table/composite setter rules. Other panes,
 * including the architecture-independent Hex view, are unaffected. */
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_registers_set_cpu(xxwidgets_widget *widget,
                                                                 const xxwidgets_debugger_cpu_snapshot *cpu);
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_set_cpu_registers(xxwidgets_debugger *view,
                                                                 const xxwidgets_debugger_cpu_snapshot *cpu);

/* Returns 2, 4 or 8 bytes, or zero for an unknown architecture. */
XXWIDGETS_API size_t xxwidgets_debugger_arch_word_size(xxwidgets_debugger_arch arch);

/* Format one stack word in target byte order. bytes holds available bytes
 * starting at the word's lowest address; missing bytes display as "??".
 * available must not exceed word_size. bytes may be NULL only when available
 * is zero. big_endian is zero for little-endian, nonzero for big-endian.
 * value needs 17 bytes (including NUL), even for narrower words. Invalid
 * arguments leave value unchanged. No UI/backend access is performed. */
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_format_stack_word(xxwidgets_debugger_arch arch, int big_endian,
                                                                  const unsigned char *bytes, size_t available,
                                                                  char value[17]);

#ifdef __cplusplus
}
#endif
#endif
