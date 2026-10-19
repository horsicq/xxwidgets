/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_SEARCH_H
#define XXWIDGETS_SEARCH_H

#include "xxwidgets/xxwidgets.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XXWIDGETS_SEARCH_PATTERN_CAPACITY 4096
#define XXWIDGETS_SEARCH_ENCODING_CAPACITY 64

typedef enum xxwidgets_search_mode {
    XXWIDGETS_SEARCH_TEXT = 0,
    XXWIDGETS_SEARCH_HEX,
    XXWIDGETS_SEARCH_SIGNATURE,
    XXWIDGETS_SEARCH_NUMBER,
    XXWIDGETS_SEARCH_REGEX
} xxwidgets_search_mode;

typedef enum xxwidgets_search_action {
    XXWIDGETS_SEARCH_CANCEL = 0,
    XXWIDGETS_SEARCH_FIND,
    XXWIDGETS_SEARCH_FIND_ALL
} xxwidgets_search_action;

/* Value object: callers retain ownership. Text is UTF-8; encoding names are
 * interpreted by the client's search engine. start/length are absolute source
 * byte offsets. UINT64_MAX length means the remainder of the source. Alignment
 * is relative to source offset zero and must be nonzero. */
typedef struct xxwidgets_search_options {
    char pattern[XXWIDGETS_SEARCH_PATTERN_CAPACITY];
    char encoding[XXWIDGETS_SEARCH_ENCODING_CAPACITY];
    xxwidgets_search_mode mode;
    int backward, wrap, selection_only, case_sensitive;
    uint64_t start, length, alignment;
} xxwidgets_search_options;

typedef struct xxwidgets_search_widget xxwidgets_search_widget;

typedef enum xxwidgets_search_control_id {
    XXWIDGETS_SEARCH_PATTERN = 0,
    XXWIDGETS_SEARCH_MODE,
    XXWIDGETS_SEARCH_ENCODING,
    XXWIDGETS_SEARCH_BACKWARD,
    XXWIDGETS_SEARCH_WRAP,
    XXWIDGETS_SEARCH_SELECTION_ONLY,
    XXWIDGETS_SEARCH_CASE_SENSITIVE,
    XXWIDGETS_SEARCH_START,
    XXWIDGETS_SEARCH_LENGTH,
    XXWIDGETS_SEARCH_ALIGNMENT,
    XXWIDGETS_SEARCH_PATTERN_LABEL,
    XXWIDGETS_SEARCH_MODE_LABEL,
    XXWIDGETS_SEARCH_ENCODING_LABEL,
    XXWIDGETS_SEARCH_START_LABEL,
    XXWIDGETS_SEARCH_LENGTH_LABEL,
    XXWIDGETS_SEARCH_ALIGNMENT_LABEL,
    XXWIDGETS_SEARCH_HELP,
    XXWIDGETS_SEARCH_CONTROL_COUNT
} xxwidgets_search_control_id;

/* Empty patterns are valid drafts. The dialog requires a nonempty pattern on
 * Find/Find All. Syntax and encoding support belong to the client, allowing the
 * same form to serve hex views, disassembly, and other source views. */
XXWIDGETS_API xxwidgets_status xxwidgets_search_options_init(xxwidgets_search_options *options);
XXWIDGETS_API xxwidgets_status xxwidgets_search_options_validate(const xxwidgets_search_options *options);

/* Embedded form, minimum 70 x 15 text cells. Children belong to owner; destroy
 * the form before owner/app, and do not destroy borrowed controls separately.
 * Set/get copy values, emit no input events, and leave their destination intact
 * on errors. Numeric edits accept decimal or 0x hexadecimal; blank length
 * means the remainder of the source. */
XXWIDGETS_API xxwidgets_status xxwidgets_search_widget_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_search_widget **out_widget);
XXWIDGETS_API xxwidgets_status xxwidgets_search_widget_destroy(xxwidgets_search_widget *widget);
XXWIDGETS_API xxwidgets_widget *xxwidgets_search_widget_control(const xxwidgets_search_widget *widget, xxwidgets_search_control_id control);
XXWIDGETS_API xxwidgets_status xxwidgets_search_widget_get(const xxwidgets_search_widget *widget, xxwidgets_search_options *options);
XXWIDGETS_API xxwidgets_status xxwidgets_search_widget_set(xxwidgets_search_widget *widget, const xxwidgets_search_options *options);
XXWIDGETS_API xxwidgets_status xxwidgets_search_widget_set_rect(xxwidgets_search_widget *widget, xxwidgets_rect bounds);

/* Native/TUI modal form. Start after returning from callbacks/polling. On Find
 * or Find All, copies accepted settings and reports the action; Cancel, Escape,
 * close, app quit, and every failure preserve options and report CANCEL. */
XXWIDGETS_API xxwidgets_status xxwidgets_search_dialog(xxwidgets_widget *owner, const char *title,
    xxwidgets_search_options *options, xxwidgets_search_action *action);

#ifdef __cplusplus
}
#endif
#endif
