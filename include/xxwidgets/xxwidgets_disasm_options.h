/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_DISASM_OPTIONS_H
#define XXWIDGETS_DISASM_OPTIONS_H
#include "xxwidgets/xxwidgets_font_options.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum xxwidgets_disasm_syntax {
    XXWIDGETS_DISASM_SYNTAX_INTEL = 0,
    XXWIDGETS_DISASM_SYNTAX_ATT = 1
} xxwidgets_disasm_syntax;

typedef enum xxwidgets_disasm_color_id {
    XXWIDGETS_DISASM_COLOR_ARROWS,
    XXWIDGETS_DISASM_COLOR_ARROWS_SELECTED,
    XXWIDGETS_DISASM_COLOR_REGS,
    XXWIDGETS_DISASM_COLOR_NUMBERS,
    XXWIDGETS_DISASM_COLOR_OPCODE,
    XXWIDGETS_DISASM_COLOR_REFS,
    XXWIDGETS_DISASM_COLOR_CURRENTIP,
    XXWIDGETS_DISASM_COLOR_X86_REGS_GENERAL,
    XXWIDGETS_DISASM_COLOR_X86_REGS_STACK,
    XXWIDGETS_DISASM_COLOR_X86_REGS_SEGMENT,
    XXWIDGETS_DISASM_COLOR_X86_REGS_DEBUG,
    XXWIDGETS_DISASM_COLOR_X86_REGS_IP,
    XXWIDGETS_DISASM_COLOR_X86_REGS_FLAGS,
    XXWIDGETS_DISASM_COLOR_X86_REGS_FPU,
    XXWIDGETS_DISASM_COLOR_X86_REGS_XMM,
    XXWIDGETS_DISASM_COLOR_X86_REGS_YMM,
    XXWIDGETS_DISASM_COLOR_X86_REGS_ZMM,
    XXWIDGETS_DISASM_COLOR_X86_OPCODE_CALL,
    XXWIDGETS_DISASM_COLOR_X86_OPCODE_RET,
    XXWIDGETS_DISASM_COLOR_X86_OPCODE_NOP,
    XXWIDGETS_DISASM_COLOR_X86_OPCODE_PUSH,
    XXWIDGETS_DISASM_COLOR_X86_OPCODE_POP,
    XXWIDGETS_DISASM_COLOR_X86_OPCODE_COND_JMP,
    XXWIDGETS_DISASM_COLOR_X86_OPCODE_JMP,
    XXWIDGETS_DISASM_COLOR_X86_OPCODE_INT3,
    XXWIDGETS_DISASM_COLOR_X86_OPCODE_SYSCALL,
    XXWIDGETS_DISASM_COLOR_ARM_REGS_GENERAL,
    XXWIDGETS_DISASM_COLOR_ARM_REGS_STACK,
    XXWIDGETS_DISASM_COLOR_ARM_OPCODE_BRANCH,
    XXWIDGETS_DISASM_COLOR_ARM_OPCODE_BRANCHLINK,
    XXWIDGETS_DISASM_COLOR_ARM_OPCODE_RET,
    XXWIDGETS_DISASM_COLOR_ARM_OPCODE_PUSH,
    XXWIDGETS_DISASM_COLOR_ARM_OPCODE_POP,
    XXWIDGETS_DISASM_COLOR_ARM_OPCODE_NOP,
    XXWIDGETS_DISASM_COLOR_COUNT
} xxwidgets_disasm_color_id;

/* RGB 0xRRGGBB; UINT32_MAX selects the control's normal foreground or leaves
 * the background transparent. Values outside these ranges are invalid. */
typedef struct xxwidgets_disasm_color { uint32_t foreground, background; } xxwidgets_disasm_color;
typedef struct xxwidgets_disasm_options {
    int syntax; /* xxwidgets_disasm_syntax; ARM retains its native syntax. */
    int uppercase, location_colon, highlight; /* Each 0 or 1. */
    xxwidgets_font font;
    xxwidgets_disasm_color colors[XXWIDGETS_DISASM_COLOR_COUNT];
} xxwidgets_disasm_options;

XXWIDGETS_API xxwidgets_status xxwidgets_disasm_options_init(xxwidgets_disasm_options *options);
XXWIDGETS_API xxwidgets_status xxwidgets_disasm_options_validate(const xxwidgets_disasm_options *options);
/* Stable ASCII key and user-visible label; NULL for an invalid color id. */
XXWIDGETS_API const char *xxwidgets_disasm_color_key(xxwidgets_disasm_color_id color);
XXWIDGETS_API const char *xxwidgets_disasm_color_name(xxwidgets_disasm_color_id color);
/* UI thread only. Validation is transactional; programmatic updates emit no
 * input events. Existing and future disassembly controls use these options. */
XXWIDGETS_API xxwidgets_status xxwidgets_app_set_disasm_options(xxwidgets_app *app, const xxwidgets_disasm_options *options);
XXWIDGETS_API xxwidgets_status xxwidgets_app_get_disasm_options(const xxwidgets_app *app, xxwidgets_disasm_options *options);
#ifdef __cplusplus
}
#endif
#endif
