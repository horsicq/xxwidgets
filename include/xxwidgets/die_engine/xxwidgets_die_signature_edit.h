/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_DIE_SIGNATURE_EDIT_H
#define XXWIDGETS_DIE_SIGNATURE_EDIT_H
#include "../xxwidgets.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Create with XXWIDGETS_DIE_SIGNATURE_EDIT. UTF-8 source uses LF newlines;
 * CRLF/CR inputs are normalized. Byte offsets must be Unicode scalar boundaries.
 * A document is limited to 8 MiB. Setters do not emit user input events.
 * Keyboard/mouse edits emit CHANGE or SELECT after updating the shared model.
 * Tab inserts spaces to the next four-column stop; Enter preserves indentation.
 * No wrapping: vertical and horizontal scrolling retain source line numbers. */
typedef enum xxwidgets_die_signature_style {
    XXWIDGETS_DIE_STYLE_TEXT = 0, XXWIDGETS_DIE_STYLE_KEYWORD,
    XXWIDGETS_DIE_STYLE_BUILTIN, XXWIDGETS_DIE_STYLE_STRING,
    XXWIDGETS_DIE_STYLE_COMMENT, XXWIDGETS_DIE_STYLE_NUMBER,
    XXWIDGETS_DIE_STYLE_FUNCTION, XXWIDGETS_DIE_STYLE_REGEX
} xxwidgets_die_signature_style;
/* Loading source resets the savepoint, caret and bounded undo/redo history. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_edit_set_source(xxwidgets_widget *, const char *source);
/* Borrowed until the next source mutation or widget destruction. */
XXWIDGETS_API const char *xxwidgets_die_signature_edit_source(const xxwidgets_widget *);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_edit_set_read_only(xxwidgets_widget *, int);
XXWIDGETS_API int xxwidgets_die_signature_edit_read_only(const xxwidgets_widget *);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_edit_set_selection(xxwidgets_widget *, size_t offset, size_t length);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_edit_get_selection(const xxwidgets_widget *, size_t *offset, size_t *length);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_edit_set_cursor(xxwidgets_widget *, size_t byte_offset);
XXWIDGETS_API size_t xxwidgets_die_signature_edit_cursor(const xxwidgets_widget *);
/* One-based line and expanded display column. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_edit_position(const xxwidgets_widget *, size_t *line, size_t *column);
XXWIDGETS_API size_t xxwidgets_die_signature_edit_line_count(const xxwidgets_widget *);
/* ASCII case-insensitive search is optional; UTF-8 matching otherwise exact.
 * Search begins after the selection, wraps once, and selects a found match.
 * UNAVAILABLE means no match; source/cursor/selection remain unchanged. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_edit_find(xxwidgets_widget *, const char *text, int match_case, int backwards);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_edit_insert(xxwidgets_widget *, const char *text);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_edit_undo(xxwidgets_widget *);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_edit_redo(xxwidgets_widget *);
XXWIDGETS_API int xxwidgets_die_signature_edit_modified(const xxwidgets_widget *);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_edit_mark_saved(xxwidgets_widget *);
/* Debug overlays use one-based source lines. execution_line=0 clears the
 * execution marker. Breakpoint lines are copied, unique and within the source;
 * up to 4096 are accepted. Source mutations invalidate all markers. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_edit_set_debug_markers(xxwidgets_widget *, const size_t *breakpoint_lines, size_t count, size_t execution_line);
XXWIDGETS_API xxwidgets_die_signature_style xxwidgets_die_signature_edit_style_at(const xxwidgets_widget *, size_t byte_offset);
#ifdef __cplusplus
}
#endif
#endif
