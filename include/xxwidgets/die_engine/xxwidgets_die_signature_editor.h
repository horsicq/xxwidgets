/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_DIE_SIGNATURE_EDITOR_H
#define XXWIDGETS_DIE_SIGNATURE_EDITOR_H

#include "xxwidgets_die_signature_edit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_die_signature_editor xxwidgets_die_signature_editor;

typedef enum xxwidgets_die_signature_editor_control_id {
    XXWIDGETS_DIE_EDITOR_OPEN = 0,
    XXWIDGETS_DIE_EDITOR_SAVE,
    XXWIDGETS_DIE_EDITOR_SAVE_AS,
    XXWIDGETS_DIE_EDITOR_READ_ONLY,
    XXWIDGETS_DIE_EDITOR_PATH,
    XXWIDGETS_DIE_EDITOR_SOURCE,
    XXWIDGETS_DIE_EDITOR_FIND_TEXT,
    XXWIDGETS_DIE_EDITOR_FIND_NEXT,
    XXWIDGETS_DIE_EDITOR_RUN,
    XXWIDGETS_DIE_EDITOR_CLEAR_RESULT,
    XXWIDGETS_DIE_EDITOR_RESULT,
    XXWIDGETS_DIE_EDITOR_STATUS,
    XXWIDGETS_DIE_EDITOR_CONTROL_COUNT
} xxwidgets_die_signature_editor_control_id;

/* Optional synchronous application adapter. source/path are borrowed for this
 * call; path is empty for an untitled document. On any outcome, result may be
 * NULL or a malloc-allocated UTF-8 string, which the editor consumes and frees.
 * Runs outside input dispatch; do not pump events or destroy the editor here.
 * Engine/database ownership, target input and cancellation belong to the app. */
typedef xxwidgets_status (*xxwidgets_die_signature_editor_run_fn)(
    const char *source, const char *path, char **result, void *user_data);

/* Embedded form, minimum 72 x 22 text cells. All calls belong to the owner's
 * UI thread. Children belong to a WINDOW owner: destroy this form before the
 * owner/app; borrowed controls must not be destroyed separately. Creation,
 * destruction, file operations and execution during dispatch/poll return BUSY.
 * Source is read-only initially, matching the Qt signature editor. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_editor_create(xxwidgets_widget *owner, xxwidgets_rect bounds,
    xxwidgets_die_signature_editor **out_editor);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_editor_destroy(xxwidgets_die_signature_editor *editor);
XXWIDGETS_API xxwidgets_widget *xxwidgets_die_signature_editor_control(const xxwidgets_die_signature_editor *editor,
    xxwidgets_die_signature_editor_control_id control);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_editor_set_rect(xxwidgets_die_signature_editor *editor, xxwidgets_rect bounds);

/* Replace a document with validated UTF-8 source, normalized to LF, <= 8 MiB.
 * path is copied (NULL/empty means untitled). Successful replacement starts a
 * clean undo history and marks the draft saved. Replacing a modified draft
 * returns BUSY unless discard_modified is nonzero. No automatic save occurs. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_editor_set_source(xxwidgets_die_signature_editor *editor,
    const char *source, const char *path, int discard_modified);
XXWIDGETS_API const char *xxwidgets_die_signature_editor_source(const xxwidgets_die_signature_editor *editor);
XXWIDGETS_API const char *xxwidgets_die_signature_editor_path(const xxwidgets_die_signature_editor *editor);
XXWIDGETS_API int xxwidgets_die_signature_editor_modified(const xxwidgets_die_signature_editor *editor);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_editor_set_read_only(xxwidgets_die_signature_editor *editor, int read_only);
XXWIDGETS_API int xxwidgets_die_signature_editor_read_only(const xxwidgets_die_signature_editor *editor);

/* Open rejects binary/NUL-containing, invalid UTF-8 and oversized files without
 * replacing the draft. Save writes LF UTF-8 through an exclusive sibling temp
 * file, then atomically replaces path. NULL path saves to the last opened/saved
 * path; untitled requires an explicit path. Save failures leave the draft dirty.
 * Explicit-path save may replace an existing file: applications obtain approval
 * themselves, or use the Save As chooser, which confirms replacement. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_editor_open(xxwidgets_die_signature_editor *editor,
    const char *path, int discard_modified);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_editor_save(xxwidgets_die_signature_editor *editor, const char *path);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_editor_find(xxwidgets_die_signature_editor *editor,
    const char *text, int match_case, int backwards);

XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_editor_set_result(xxwidgets_die_signature_editor *editor, const char *text);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_editor_clear_result(xxwidgets_die_signature_editor *editor);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_editor_set_run_callback(xxwidgets_die_signature_editor *editor,
    xxwidgets_die_signature_editor_run_fn callback, void *user_data);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_editor_run(xxwidgets_die_signature_editor *editor);

/* Host calls handle_event from its event callback; it updates nonmodal state
 * and queues toolbar actions. Call process_pending after app_poll returns.
 * Native Open/Save As use the file chooser. TUI uses the editable path field.
 * Dirty Open requires an explicit checked discard confirmation; cancellation
 * leaves the draft intact. Run is disabled when no adapter is installed.
 * A pending action's failure is also shown in the status label. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_editor_handle_event(xxwidgets_die_signature_editor *editor,
    const xxwidgets_event *event);
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_editor_process_pending(xxwidgets_die_signature_editor *editor);

#ifdef __cplusplus
}
#endif
#endif
