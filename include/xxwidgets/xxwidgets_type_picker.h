/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/* A modal picker for a long list of named, checkable types (file formats,
 * signatures, ...): a filterable table with one row per item, toggled with
 * Enter / double click or the Toggle button, plus "Check shown", "Uncheck
 * shown" (the rows the column filters let through) and "Defaults". Any
 * number of items; the extractor panel uses it when its source has more
 * types than its 64-bit mask holds. */
#ifndef XXWIDGETS_TYPE_PICKER_H
#define XXWIDGETS_TYPE_PICKER_H
#include "xxwidgets/xxwidgets.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_type_picker_item {
    const char *name;     /* required, UTF-8 */
    const char *category; /* NULL = "" */
    const char *note;     /* NULL = "" (e.g. the presets an item is in) */
} xxwidgets_type_picker_item;

/* checked: count bytes, 0/1, read as the initial state and written on OK.
 * defaults: count bytes or NULL (the Defaults button is then disabled).
 * accepted: 1 after OK, 0 after Cancel / Esc / close. */
XXWIDGETS_API xxwidgets_status xxwidgets_type_picker_dialog(xxwidgets_widget *owner, const char *title, const xxwidgets_type_picker_item *items,
                                                            size_t count, unsigned char *checked, const unsigned char *defaults, int *accepted);

/* The rows of a picker table for tests and custom layouts: count * 4 cells
 * (mark, name, category, note), mark "[x]" or "[ ]". The caller frees the
 * array with free(); the strings point into items and a static mark. */
XXWIDGETS_API const char **xxwidgets_type_picker_cells(const xxwidgets_type_picker_item *items, size_t count, const unsigned char *checked);

#ifdef __cplusplus
}
#endif
#endif
