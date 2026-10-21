/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_debugger_arch_internal.h"
#include <string.h>

static const char hex_digits[] = "0123456789ABCDEF";

static xxwidgets_status begin_row(xxwidgets_debugger_register_rows *rows, const char *name)
{
    size_t length;
    if (!rows || !name || rows->count >= XXWIDGETS_DEBUGGER_REGISTER_ROW_CAPACITY)
        return XXWIDGETS_INVALID_ARGUMENT;
    length = strlen(name);
    if (length >= XXWIDGETS_DEBUGGER_REGISTER_NAME_CAPACITY) return XXWIDGETS_INVALID_ARGUMENT;
    memcpy(rows->names[rows->count], name, length + 1);
    rows->rows[rows->count].name = rows->names[rows->count];
    rows->rows[rows->count].value = rows->values[rows->count];
    rows->rows[rows->count].flags = 0;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_debugger_add_scalar(xxwidgets_debugger_register_rows *rows, const char *name,
                                              uint64_t value, unsigned digits)
{
    xxwidgets_status status;
    unsigned i;
    if (!digits || digits > 16) return XXWIDGETS_INVALID_ARGUMENT;
    status = begin_row(rows, name);
    if (status != XXWIDGETS_OK) return status;
    for (i = 0; i < digits; ++i) {
        rows->values[rows->count][digits - i - 1] = hex_digits[value & 15u];
        value >>= 4;
    }
    rows->values[rows->count][digits] = 0;
    ++rows->count;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_debugger_add_vector(xxwidgets_debugger_register_rows *rows, const char *name,
                                              const uint8_t value[16])
{
    xxwidgets_status status;
    size_t i;
    if (!value) return XXWIDGETS_INVALID_ARGUMENT;
    status = begin_row(rows, name);
    if (status != XXWIDGETS_OK) return status;
    for (i = 0; i < 16; ++i) {
        rows->values[rows->count][i * 2] = hex_digits[value[15 - i] >> 4];
        rows->values[rows->count][i * 2 + 1] = hex_digits[value[15 - i] & 15u];
    }
    rows->values[rows->count][32] = 0;
    ++rows->count;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_debugger_format_registers(const xxwidgets_debugger_cpu_snapshot *cpu,
                                                     xxwidgets_debugger_register_rows *output)
{
    xxwidgets_debugger_register_rows staged;
    xxwidgets_status status;
    size_t i;
    if (!cpu || !output) return XXWIDGETS_INVALID_ARGUMENT;
    memset(&staged, 0, sizeof(staged));
    switch (cpu->arch) {
        case XXWIDGETS_DEBUGGER_ARCH_X86_16: status = xxwidgets_debugger_format_x86_16(&cpu->regs.x86, &staged); break;
        case XXWIDGETS_DEBUGGER_ARCH_X86_32: status = xxwidgets_debugger_format_x86_32(&cpu->regs.x86, &staged); break;
        case XXWIDGETS_DEBUGGER_ARCH_X86_64: status = xxwidgets_debugger_format_x86_64(&cpu->regs.x86, &staged); break;
        case XXWIDGETS_DEBUGGER_ARCH_ARM32: status = xxwidgets_debugger_format_arm32(&cpu->regs.arm32, &staged); break;
        case XXWIDGETS_DEBUGGER_ARCH_ARM64: status = xxwidgets_debugger_format_arm64(&cpu->regs.arm64, &staged); break;
        default: return XXWIDGETS_INVALID_ARGUMENT;
    }
    if (status != XXWIDGETS_OK) return status;
    *output = staged;
    for (i = 0; i < output->count; ++i) {
        output->rows[i].name = output->names[i];
        output->rows[i].value = output->values[i];
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_debugger_registers_set_cpu(xxwidgets_widget *widget,
                                                      const xxwidgets_debugger_cpu_snapshot *cpu)
{
    xxwidgets_debugger_register_rows rows;
    xxwidgets_status status;
    if (!widget) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_debugger_format_registers(cpu, &rows);
    if (status != XXWIDGETS_OK) return status;
    return xxwidgets_debugger_registers_set_rows(widget, rows.rows, rows.count);
}

xxwidgets_status xxwidgets_debugger_set_cpu_registers(xxwidgets_debugger *view,
                                                      const xxwidgets_debugger_cpu_snapshot *cpu)
{
    xxwidgets_debugger_register_rows rows;
    xxwidgets_status status;
    if (!view) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_debugger_format_registers(cpu, &rows);
    if (status != XXWIDGETS_OK) return status;
    return xxwidgets_debugger_set_registers(view, rows.rows, rows.count);
}

size_t xxwidgets_debugger_arch_word_size(xxwidgets_debugger_arch arch)
{
    switch (arch) {
        case XXWIDGETS_DEBUGGER_ARCH_X86_16: return 2;
        case XXWIDGETS_DEBUGGER_ARCH_X86_32:
        case XXWIDGETS_DEBUGGER_ARCH_ARM32: return 4;
        case XXWIDGETS_DEBUGGER_ARCH_X86_64:
        case XXWIDGETS_DEBUGGER_ARCH_ARM64: return 8;
        default: return 0;
    }
}

xxwidgets_status xxwidgets_debugger_format_stack_word(xxwidgets_debugger_arch arch, int big_endian,
                                                      const unsigned char *bytes, size_t available,
                                                      char value[17])
{
    size_t word_size = xxwidgets_debugger_arch_word_size(arch), i;
    char staged[17];
    if (!value || !word_size || available > word_size || (!bytes && available)) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < word_size; ++i) {
        size_t offset = big_endian ? i : word_size - i - 1;
        if (offset < available) {
            staged[i * 2] = hex_digits[bytes[offset] >> 4];
            staged[i * 2 + 1] = hex_digits[bytes[offset] & 15u];
        } else {
            staged[i * 2] = '?';
            staged[i * 2 + 1] = '?';
        }
    }
    staged[word_size * 2] = 0;
    memcpy(value, staged, word_size * 2 + 1);
    return XXWIDGETS_OK;
}
