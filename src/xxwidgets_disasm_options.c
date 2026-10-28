/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_debugger_tables.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const char *const color_keys[XXWIDGETS_DISASM_COLOR_COUNT] = {
    "arrows",
    "arrows_selected",
    "regs",
    "numbers",
    "opcode",
    "refs",
    "currentip",
    "x86_regs_general",
    "x86_regs_stack",
    "x86_regs_segment",
    "x86_regs_debug",
    "x86_regs_ip",
    "x86_regs_flags",
    "x86_regs_fpu",
    "x86_regs_xmm",
    "x86_regs_ymm",
    "x86_regs_zmm",
    "x86_opcode_call",
    "x86_opcode_ret",
    "x86_opcode_nop",
    "x86_opcode_push",
    "x86_opcode_pop",
    "x86_opcode_cond_jmp",
    "x86_opcode_jmp",
    "x86_opcode_int3",
    "x86_opcode_syscall",
    "arm_regs_general",
    "arm_regs_stack",
    "arm_opcode_branch",
    "arm_opcode_branchlink",
    "arm_opcode_ret",
    "arm_opcode_push",
    "arm_opcode_pop",
    "arm_opcode_nop",
};
static const char *const color_names[XXWIDGETS_DISASM_COLOR_COUNT] = {
    "Arrows",
    "Arrows Selected",
    "registers",
    "Numbers",
    "instruction",
    "Refs",
    "Currentip",
    "x86 registers General",
    "x86 registers Stack",
    "x86 registers Segment",
    "x86 registers Debug",
    "x86 registers Ip",
    "x86 registers Flags",
    "x86 registers Fpu",
    "x86 registers Xmm",
    "x86 registers Ymm",
    "x86 registers Zmm",
    "x86 instruction Call",
    "x86 instruction Ret",
    "x86 instruction Nop",
    "x86 instruction Push",
    "x86 instruction Pop",
    "x86 instruction Cond Jmp",
    "x86 instruction Jmp",
    "x86 instruction Int3",
    "x86 instruction Syscall",
    "ARM registers General",
    "ARM registers Stack",
    "ARM instruction Branch",
    "ARM instruction Branchlink",
    "ARM instruction Ret",
    "ARM instruction Push",
    "ARM instruction Pop",
    "ARM instruction Nop",
};
static const uint32_t default_foreground[XXWIDGETS_DISASM_COLOR_COUNT] = {
    UINT32_MAX,
    0xFF0000,
    0x960000,
    0x007800,
    0x0000A0,
    0x006E8C,
    UINT32_MAX,
    0x960000,
    0xAA5500,
    0x008080,
    0x808000,
    0xC71585,
    0x800080,
    0x556B2F,
    0x2E8B57,
    0x2E8B57,
    0x2E8B57,
    0x8C00B0,
    0xB22222,
    0x969696,
    0x2864B4,
    0x2864B4,
    0x1E8C1E,
    0x0000CD,
    0x787878,
    0x8C00B0,
    0x960000,
    0xAA5500,
    0x0000CD,
    0x8C00B0,
    0xB22222,
    0x2864B4,
    0x2864B4,
    0x969696,
};

const char *xxwidgets_disasm_color_key(xxwidgets_disasm_color_id color)
{ return color >= 0 && color < XXWIDGETS_DISASM_COLOR_COUNT ? color_keys[color] : NULL; }
const char *xxwidgets_disasm_color_name(xxwidgets_disasm_color_id color)
{ return color >= 0 && color < XXWIDGETS_DISASM_COLOR_COUNT ? color_names[color] : NULL; }

xxwidgets_status xxwidgets_disasm_options_init(xxwidgets_disasm_options *options)
{
    if (!options) return XXWIDGETS_INVALID_ARGUMENT;
    memset(options, 0, sizeof(*options));
    options->location_colon = options->highlight = 1;
    strcpy(options->font.family, "monospace");
    for (size_t i = 0; i < XXWIDGETS_DISASM_COLOR_COUNT; ++i) {
        options->colors[i].foreground = default_foreground[i];
        options->colors[i].background = UINT32_MAX;
    }
    options->colors[XXWIDGETS_DISASM_COLOR_CURRENTIP].background = 0xA5DDA5;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_disasm_options_validate(const xxwidgets_disasm_options *options)
{
    if (!options || options->syntax < XXWIDGETS_DISASM_SYNTAX_INTEL || options->syntax > XXWIDGETS_DISASM_SYNTAX_ATT ||
        (options->uppercase != 0 && options->uppercase != 1) || (options->location_colon != 0 && options->location_colon != 1) ||
        (options->highlight != 0 && options->highlight != 1) || !xxwidgets_font_valid(&options->font)) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < XXWIDGETS_DISASM_COLOR_COUNT; ++i) {
        const xxwidgets_disasm_color *c = options->colors + i;
        if ((c->foreground > 0xFFFFFF && c->foreground != UINT32_MAX) ||
            (c->background > 0xFFFFFF && c->background != UINT32_MAX)) return XXWIDGETS_INVALID_ARGUMENT;
    }
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_app_get_disasm_options(const xxwidgets_app *app, xxwidgets_disasm_options *options)
{
    if (!app || !options) return XXWIDGETS_INVALID_ARGUMENT;
    *options = app->disasm_options;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_app_set_disasm_options(xxwidgets_app *app, const xxwidgets_disasm_options *options)
{
    xxwidgets_disasm_options previous;
    xxwidgets_font_options fonts, previous_fonts;
    xxwidgets_status status;
    if (!app || xxwidgets_disasm_options_validate(options) != XXWIDGETS_OK) return XXWIDGETS_INVALID_ARGUMENT;
    if (app->dispatch_depth || app->polling || app->syncing) return XXWIDGETS_BUSY;
    previous = app->disasm_options;
    previous_fonts = fonts = app->font_options;
    fonts.fonts[XXWIDGETS_FONT_DISASM_VIEWS] = options->font;
    status = xxwidgets_app_set_font_options(app, &fonts);
    if (status != XXWIDGETS_OK) return status;
    app->disasm_options = *options;
    for (xxwidgets_widget *p = app->widgets; p; p = p->next) {
        if (p->kind != XXWIDGETS_DEBUGGER_DISASM) continue;
        status = xxwidgets_debugger_disasm_reformat(p);
        if (status != XXWIDGETS_OK) break;
    }
    if (status != XXWIDGETS_OK) {
        app->disasm_options = previous;
        (void)xxwidgets_app_set_font_options(app, &previous_fonts);
        for (xxwidgets_widget *p = app->widgets; p; p = p->next)
            if (p->kind == XXWIDGETS_DEBUGGER_DISASM) (void)xxwidgets_debugger_disasm_reformat(p);
    }
    return status;
}

static int one_of(const char *word, const char *list)
{
    size_t n = strlen(word);
    const char *p = list;
    while (*p) {
        const char *end = strchr(p, ' ');
        size_t length = end ? (size_t)(end - p) : strlen(p);
        if (length == n && !memcmp(p, word, n)) return 1;
        if (!end) break;
        p = end + 1;
    }
    return 0;
}
static int register_class(const char *word, int arm)
{
    if (arm) {
        if (one_of(word, "sp wsp r13")) return XXWIDGETS_DISASM_COLOR_ARM_REGS_STACK;
        if (one_of(word, "lr pc ip fp xzr wzr cpsr spsr nzcv fpsr fpcr")) return XXWIDGETS_DISASM_COLOR_REGS;
        if ((word[0] == 'r' || word[0] == 'x' || word[0] == 'w' || word[0] == 'v' || word[0] == 'q' || word[0] == 'd' || word[0] == 's') &&
            word[1] >= '0' && word[1] <= '9') return XXWIDGETS_DISASM_COLOR_ARM_REGS_GENERAL;
        return -1;
    }
    if (one_of(word, "sp esp rsp bp ebp rbp spl bpl")) return XXWIDGETS_DISASM_COLOR_X86_REGS_STACK;
    if (one_of(word, "cs ds es fs gs ss")) return XXWIDGETS_DISASM_COLOR_X86_REGS_SEGMENT;
    if (one_of(word, "ip eip rip")) return XXWIDGETS_DISASM_COLOR_X86_REGS_IP;
    if (one_of(word, "flags eflags rflags")) return XXWIDGETS_DISASM_COLOR_X86_REGS_FLAGS;
    if ((!strncmp(word, "dr", 2) || !strncmp(word, "cr", 2)) && isdigit((unsigned char)word[2])) return XXWIDGETS_DISASM_COLOR_X86_REGS_DEBUG;
    if (!strncmp(word, "st", 2) && (!word[2] || isdigit((unsigned char)word[2]))) return XXWIDGETS_DISASM_COLOR_X86_REGS_FPU;
    if (!strncmp(word, "xmm", 3)) return XXWIDGETS_DISASM_COLOR_X86_REGS_XMM;
    if (!strncmp(word, "ymm", 3)) return XXWIDGETS_DISASM_COLOR_X86_REGS_YMM;
    if (!strncmp(word, "zmm", 3)) return XXWIDGETS_DISASM_COLOR_X86_REGS_ZMM;
    if (one_of(word, "al ah ax eax rax bl bh bx ebx rbx cl ch cx ecx rcx dl dh dx edx rdx si esi rsi sil di edi rdi dil") ||
        (word[0] == 'r' && isdigit((unsigned char)word[1]))) return XXWIDGETS_DISASM_COLOR_X86_REGS_GENERAL;
    return -1;
}
static int mnemonic_class(const char *word, int arm)
{
    if (arm) {
        if (one_of(word, "bl blx blr")) return XXWIDGETS_DISASM_COLOR_ARM_OPCODE_BRANCHLINK;
        if (one_of(word, "ret eret")) return XXWIDGETS_DISASM_COLOR_ARM_OPCODE_RET;
        if (!strcmp(word, "push")) return XXWIDGETS_DISASM_COLOR_ARM_OPCODE_PUSH;
        if (!strcmp(word, "pop")) return XXWIDGETS_DISASM_COLOR_ARM_OPCODE_POP;
        if (!strcmp(word, "nop")) return XXWIDGETS_DISASM_COLOR_ARM_OPCODE_NOP;
        if (!strncmp(word, "b.", 2) || one_of(word, "b br bx beq bne bcs bcc bhs blo bmi bpl bvs bvc bhi bls bge blt bgt ble bal bnv cbz cbnz tbz tbnz")) return XXWIDGETS_DISASM_COLOR_ARM_OPCODE_BRANCH;
    } else {
        if (!strncmp(word, "call", 4)) return XXWIDGETS_DISASM_COLOR_X86_OPCODE_CALL;
        if (!strncmp(word, "ret", 3) || !strncmp(word, "iret", 4)) return XXWIDGETS_DISASM_COLOR_X86_OPCODE_RET;
        if (!strncmp(word, "nop", 3) || !strcmp(word, "pause")) return XXWIDGETS_DISASM_COLOR_X86_OPCODE_NOP;
        if (!strncmp(word, "push", 4)) return XXWIDGETS_DISASM_COLOR_X86_OPCODE_PUSH;
        if (!strncmp(word, "pop", 3)) return XXWIDGETS_DISASM_COLOR_X86_OPCODE_POP;
        if (!strncmp(word, "jmp", 3)) return XXWIDGETS_DISASM_COLOR_X86_OPCODE_JMP;
        if (word[0] == 'j' || !strncmp(word, "loop", 4)) return XXWIDGETS_DISASM_COLOR_X86_OPCODE_COND_JMP;
        if (!strcmp(word, "int3")) return XXWIDGETS_DISASM_COLOR_X86_OPCODE_INT3;
        if (one_of(word, "syscall sysenter sysexit sysret")) return XXWIDGETS_DISASM_COLOR_X86_OPCODE_SYSCALL;
    }
    return XXWIDGETS_DISASM_COLOR_OPCODE;
}
size_t xxwidgets_disasm_spans(const xxwidgets_widget *widget, size_t row, size_t column, int selected,
                             xxwidgets_disasm_span *spans, size_t capacity)
{
    const xxwidgets_disasm_options *options;
    const char *text;
    xxwidgets_debugger_instruction instruction;
    size_t count = 0, at = 0, model;
    int mnemonic = 1;
    uint32_t row_background = UINT32_MAX;
    if (!widget || widget->kind != XXWIDGETS_DEBUGGER_DISASM || !spans || !capacity || column > 4) return 0;
    options = &widget->app->disasm_options;
    if (!options->highlight) return 0;
    model = xxwidgets_tabular_model_row(widget, row);
    if (model == SIZE_MAX || xxwidgets_debugger_disasm_get_row(widget, model, &instruction) != XXWIDGETS_OK) return 0;
    text = xxwidgets_debugger_table_cell(widget, model, column);
    if (instruction.flags & XXWIDGETS_DEBUGGER_CURRENT) row_background = options->colors[XXWIDGETS_DISASM_COLOR_CURRENTIP].background;
    if (selected && column != 0) return 0;
    if (column != 3) {
        int color = column == 0 ? (selected ? XXWIDGETS_DISASM_COLOR_ARROWS_SELECTED : XXWIDGETS_DISASM_COLOR_ARROWS)
                    : column == 2 ? XXWIDGETS_DISASM_COLOR_NUMBERS : column == 4 ? XXWIDGETS_DISASM_COLOR_REFS
                    : (instruction.flags & XXWIDGETS_DEBUGGER_CURRENT) ? XXWIDGETS_DISASM_COLOR_CURRENTIP : -1;
        xxwidgets_disasm_color c = color >= 0 ? options->colors[color] : (xxwidgets_disasm_color){UINT32_MAX, UINT32_MAX};
        spans[0] = (xxwidgets_disasm_span){0, strlen(text), c.foreground, c.background == UINT32_MAX ? row_background : c.background};
        return 1;
    }
    while (text[at] && count < capacity) {
        size_t start = at;
        int color;
        char word[128];
        if (isalnum((unsigned char)text[at]) || text[at] == '_' || text[at] == '%' || text[at] == '$' || text[at] == '#') {
            size_t n = 0;
            while (isalnum((unsigned char)text[at]) || text[at] == '_' || text[at] == '%' || text[at] == '$' || text[at] == '#' || text[at] == '.') {
                if (n + 1 < sizeof(word)) word[n++] = (char)tolower((unsigned char)text[at]);
                at++;
            }
            word[n] = 0;
            const char *plain = word + (word[0] == '%' || word[0] == '$' || word[0] == '#');
            if (mnemonic) { color = mnemonic_class(plain, widget->disasm_arm); mnemonic = one_of(plain, "lock rep repe repz repne repnz"); }
            else if ((color = register_class(plain, widget->disasm_arm)) >= 0) {}
            else color = isdigit((unsigned char)*plain) || word[0] == '$' || word[0] == '#' ? XXWIDGETS_DISASM_COLOR_NUMBERS : XXWIDGETS_DISASM_COLOR_REFS;
        } else {
            at++;
            color = XXWIDGETS_DISASM_COLOR_REFS;
            if (text[start] == ' ' || text[start] == '\t' || text[start] == ',') color = -1;
        }
        xxwidgets_disasm_color c = color >= 0 ? options->colors[color] : (xxwidgets_disasm_color){UINT32_MAX,UINT32_MAX};
        spans[count++] = (xxwidgets_disasm_span){start, at - start, c.foreground, c.background == UINT32_MAX ? row_background : c.background};
    }
    return count;
}
