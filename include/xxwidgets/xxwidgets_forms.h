/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_FORMS_H
#define XXWIDGETS_FORMS_H

#include "xxwidgets/xxwidgets.h"
#include "xxwidgets/xxwidgets_data_struct_table.h"
#include "xxwidgets/xxwidgets_entropy.h"
#include "xxwidgets/xxwidgets_canvas.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XXWIDGETS_FORM_MAX_FIELDS 12
#define XXWIDGETS_FORM_MAX_CAPACITY 1048576

/* Text fields borrow a UTF-8 buffer, whose capacity includes the terminator.
 * Checkbox fields ignore value/capacity and edit checked (zero or one).
 * Labels and buffers remain owned by the caller for the duration of the modal.
 * Text buffers must not overlap. */
typedef struct xxwidgets_form_field {
    const char *label;
    char *value;
    size_t capacity;
    int checkbox;
    int checked;
} xxwidgets_form_field;

/* Up to 12 fields, displayed in the supplied order. Text edits and checkboxes
 * commit together only after OK and successful cleanup; Cancel/Escape/close,
 * app quit, and errors preserve every caller field. Start modal helpers after
 * returning from callbacks/polling. Native and TUI backends share the form. */
XXWIDGETS_API xxwidgets_status xxwidgets_form_dialog(xxwidgets_widget *owner, const char *title,
    xxwidgets_form_field *fields, size_t count, int *accepted);

/* Borrowed UTF-8 item strings. Selection is a zero-based index; SIZE_MAX means
 * no selection. OK, Enter, or item activation accepts a selected item. Cancel,
 * Escape/close, quit, and errors preserve selection. An empty list is valid and
 * offers Close only. Contents are copied into the list for the modal's life. */
XXWIDGETS_API xxwidgets_status xxwidgets_choice_dialog(xxwidgets_widget *owner, const char *title,
    const char *const *items, size_t count, size_t *selection, int *accepted);

/* Reusable searchable action chooser. Labels, details and shortcuts are
 * borrowed UTF-8 strings. Filtering matches whitespace-separated terms against
 * label/detail/shortcut, without case sensitivity for ASCII. The selected
 * action commits only on Run/Enter; Cancel preserves it. */
typedef struct xxwidgets_command_item {
    const char *label, *detail, *shortcut;
    int action;
} xxwidgets_command_item;
XXWIDGETS_API int xxwidgets_command_matches(const xxwidgets_command_item *, const char *filter);
XXWIDGETS_API xxwidgets_status xxwidgets_command_palette(xxwidgets_widget *owner, const char *title,
    const xxwidgets_command_item *, size_t count, int *action, int *accepted);

/* Graph/image preview with a selectable pixel or a region list. Regions and
 * labels are copied for the modal lifetime. A selection with no regions is a
 * pixel index; with regions it is a region index. Cancel preserves selection. */
XXWIDGETS_API xxwidgets_status xxwidgets_canvas_dialog(xxwidgets_widget *owner,const char *title,
    const uint32_t *rgba,unsigned int width,unsigned int height,
    const xxwidgets_canvas_label *labels,size_t label_count,
    const xxwidgets_canvas_region *regions,size_t region_count,size_t *selection,int *accepted);

/* Compact 74x22 read-only analysis dialogs. Inputs are copied. Selection commits
 * only after acceptance and successful cleanup, with the same transaction and
 * focus conventions as choice_dialog. Both support empty data (Close only). */
XXWIDGETS_API xxwidgets_status xxwidgets_structure_dialog(xxwidgets_widget *owner, const char *title,
    const xxwidgets_data_struct_field *fields, size_t count, size_t *selection, int *accepted);
XXWIDGETS_API xxwidgets_status xxwidgets_entropy_samples_dialog(xxwidgets_widget *owner, const char *title,
    const xxwidgets_entropy_sample *samples, size_t count, uint64_t base, uint64_t size,
    size_t *selection, int *accepted);

#ifdef __cplusplus
}
#endif
#endif
