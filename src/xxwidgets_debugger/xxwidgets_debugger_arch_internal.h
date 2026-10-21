/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_DEBUGGER_ARCH_INTERNAL_H
#define XXWIDGETS_DEBUGGER_ARCH_INTERNAL_H

#include "xxwidgets/xxwidgets_debugger_arch.h"

/* Append a row with flags zero. Scalars use the low digits * 4 bits, with
 * 1..16 uppercase hexadecimal digits. Vectors contain little-endian bytes. */
xxwidgets_status xxwidgets_debugger_add_scalar(xxwidgets_debugger_register_rows *rows, const char *name,
                                              uint64_t value, unsigned digits);
xxwidgets_status xxwidgets_debugger_add_vector(xxwidgets_debugger_register_rows *rows, const char *name,
                                              const uint8_t value[16]);

xxwidgets_status xxwidgets_debugger_format_x86_16(const xxwidgets_debugger_x86_state *state,
                                                 xxwidgets_debugger_register_rows *rows);
xxwidgets_status xxwidgets_debugger_format_x86_32(const xxwidgets_debugger_x86_state *state,
                                                 xxwidgets_debugger_register_rows *rows);
xxwidgets_status xxwidgets_debugger_format_x86_64(const xxwidgets_debugger_x86_state *state,
                                                 xxwidgets_debugger_register_rows *rows);
xxwidgets_status xxwidgets_debugger_format_arm32(const xxwidgets_debugger_arm32_state *state,
                                                xxwidgets_debugger_register_rows *rows);
xxwidgets_status xxwidgets_debugger_format_arm64(const xxwidgets_debugger_arm64_state *state,
                                                xxwidgets_debugger_register_rows *rows);

#endif
