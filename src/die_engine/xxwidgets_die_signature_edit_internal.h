/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_DIE_SIGNATURE_EDIT_INTERNAL_H
#define XXWIDGETS_DIE_SIGNATURE_EDIT_INTERNAL_H
#include "xxwidgets/die_engine/xxwidgets_die_signature_edit.h"
typedef struct xxwidgets_die_edit_view {
    size_t caret, anchor, first_line, first_column, line_count, max_columns, execution_line;
    unsigned int gutter_columns;
    int read_only, modified;
} xxwidgets_die_edit_view;
typedef struct xxwidgets_die_edit_cell {
    uint32_t scalar;
    xxwidgets_die_signature_style style;
    int selected, caret;
} xxwidgets_die_edit_cell;
xxwidgets_status xxwidgets_die_edit_init(xxwidgets_widget *);
void xxwidgets_die_edit_dispose(xxwidgets_widget *);
/* Model uses one cell per Unicode scalar, tabs expand to four-column stops.
 * Render each line in a single pass. Background current-line highlighting is
 * suppressed in read-only mode. The gutter contains one-based line numbers. */
void xxwidgets_die_edit_view_get(const xxwidgets_widget *, xxwidgets_die_edit_view *);
void xxwidgets_die_edit_page(xxwidgets_widget *, size_t rows, size_t columns);
void xxwidgets_die_edit_render_line(const xxwidgets_widget *, size_t line, size_t first_column, xxwidgets_die_edit_cell *, size_t count);
size_t xxwidgets_die_edit_caret_line(const xxwidgets_widget *);
/* Input consumes shared XXWIDGETS_KEY_* / XXWIDGETS_MOD_* values. Returns 1 if
 * handled, 0 for application shortcut/focus navigation. Ctrl/Meta+A/C/X/V/Z/Y
 * use model selection/history and the backend's text clipboard API. Escape
 * and Ctrl+Tab are left to the application. */
int xxwidgets_die_edit_key(xxwidgets_widget *, int key, unsigned int modifiers);
xxwidgets_status xxwidgets_die_edit_input(xxwidgets_widget *, const char *utf8);
void xxwidgets_die_edit_point(xxwidgets_widget *, size_t line, size_t column, int extend);
void xxwidgets_die_edit_scroll(xxwidgets_widget *, int lines, int columns);
/* Colors are 0xRRGGBB; native renderers may use standard theme colors for text,
 * selection, background and caret. */
void xxwidgets_die_edit_line_marker(const xxwidgets_widget *, size_t zero_based_line, int *breakpoint, int *execution);
uint32_t xxwidgets_die_edit_color(xxwidgets_die_signature_style);
size_t xxwidgets_die_edit_encode(uint32_t scalar, char out[5]);
#endif
