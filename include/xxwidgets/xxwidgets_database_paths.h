/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_DATABASE_PATHS_H
#define XXWIDGETS_DATABASE_PATHS_H
#include "xxwidgets/xxwidgets.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Main, extra and custom UTF-8 folders, in that order. Empty means the
 * application's default. Input strings are borrowed and copied by the form.
 * Each path is limited to 16383 bytes, excluding NUL.
 *
 * Callbacks run after polling, outside input dispatch. browse is optional;
 * it returns a malloc'd path on acceptance, NULL on cancellation. The form
 * frees the returned path. apply is required: return 1 only after the new
 * databases loaded; on failure preserve active state, return 0 and write a
 * NUL-terminated error. The form stays open for retry. Callback path strings
 * are borrowed for the duration of the call; copy any retained values. */
typedef struct xxwidgets_database_paths_callbacks {
    xxwidgets_status (*browse)(xxwidgets_widget *dialog, const char *initial,
        char **path, int *accepted, void *user);
    int (*apply)(const char *const paths[3], char *error, size_t capacity, void *user);
} xxwidgets_database_paths_callbacks;

/* Embedded form: a label, path edit and Browse button for each folder, and a
 * note that empty paths use the defaults. Bounds use text cells and require
 * at least 48 x 10. callbacks may be NULL; only browse is used here, and
 * Browse stays disabled without it. callbacks/user are copied/borrowed for the
 * form's lifetime. Destroy before the owner/app. */
typedef struct xxwidgets_database_paths_widget xxwidgets_database_paths_widget;
typedef enum xxwidgets_database_paths_control_id {
    XXWIDGETS_DATABASE_PATHS_MAIN_LABEL = 0,
    XXWIDGETS_DATABASE_PATHS_MAIN,
    XXWIDGETS_DATABASE_PATHS_MAIN_BROWSE,
    XXWIDGETS_DATABASE_PATHS_EXTRA_LABEL,
    XXWIDGETS_DATABASE_PATHS_EXTRA,
    XXWIDGETS_DATABASE_PATHS_EXTRA_BROWSE,
    XXWIDGETS_DATABASE_PATHS_CUSTOM_LABEL,
    XXWIDGETS_DATABASE_PATHS_CUSTOM,
    XXWIDGETS_DATABASE_PATHS_CUSTOM_BROWSE,
    XXWIDGETS_DATABASE_PATHS_NOTE,
    XXWIDGETS_DATABASE_PATHS_CONTROL_COUNT
} xxwidgets_database_paths_control_id;
XXWIDGETS_API xxwidgets_status xxwidgets_database_paths_widget_create(xxwidgets_widget *owner, xxwidgets_rect bounds,
    const xxwidgets_database_paths_callbacks *callbacks, void *user, xxwidgets_database_paths_widget **out_widget);
XXWIDGETS_API xxwidgets_status xxwidgets_database_paths_widget_destroy(xxwidgets_database_paths_widget *widget);
XXWIDGETS_API xxwidgets_widget *xxwidgets_database_paths_widget_control(const xxwidgets_database_paths_widget *widget,
    xxwidgets_database_paths_control_id control);
/* Replaces all three edits; NULL entries mean empty. No input events. */
XXWIDGETS_API xxwidgets_status xxwidgets_database_paths_widget_set(xxwidgets_database_paths_widget *widget, const char *const paths[3]);
/* index 0..2 = main, extra, custom. Same buffer conventions as widget_get_text;
 * INVALID_ARGUMENT for paths of 16384 bytes or more. */
XXWIDGETS_API xxwidgets_status xxwidgets_database_paths_widget_get(const xxwidgets_database_paths_widget *widget, size_t index,
    char *buffer, size_t capacity, size_t *required);
XXWIDGETS_API xxwidgets_status xxwidgets_database_paths_widget_set_rect(xxwidgets_database_paths_widget *widget, xxwidgets_rect bounds);
/* Forward the owner's input events, then call process after polling; Browse
 * runs callbacks->browse with the owner window there. */
XXWIDGETS_API xxwidgets_status xxwidgets_database_paths_widget_handle_event(xxwidgets_database_paths_widget *widget, const xxwidgets_event *event);
XXWIDGETS_API xxwidgets_status xxwidgets_database_paths_widget_process(xxwidgets_database_paths_widget *widget);

/* Modal window with editable paths, Browse, Defaults, OK and Cancel.
 * Only OK calls apply. Cancel, Esc and window close leave application state
 * untouched. accepted is 1 after a successful apply. Standard modal BUSY
 * rules apply; call outside callbacks/polling. */
XXWIDGETS_API xxwidgets_status xxwidgets_database_paths_dialog(
    xxwidgets_widget *owner, const char *title, const char *const paths[3],
    const xxwidgets_database_paths_callbacks *callbacks, void *user, int *accepted);
#ifdef __cplusplus
}
#endif
#endif
