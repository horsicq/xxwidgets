/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_FILE_INFORMATION_H
#define XXWIDGETS_FILE_INFORMATION_H

#include "xxwidgets/xxwidgets_data_struct_view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_file_information xxwidgets_file_information;
typedef enum xxwidgets_file_information_control_id {
    XXWIDGETS_FILE_INFORMATION_PATH,
    XXWIDGETS_FILE_INFORMATION_SUMMARY,
    XXWIDGETS_FILE_INFORMATION_CONTROL_COUNT
} xxwidgets_file_information_control_id;

/* Read-only structure browser with file metadata. The host supplies native
 * DataStructs; no parser or signature engine is linked by this component.
 * Text-cell bounds must be at least 72 x 12. All calls belong to the UI thread.
 * Destroy before the owner window; borrowed controls are not destroyed by hosts. */
XXWIDGETS_API xxwidgets_status xxwidgets_file_information_create(xxwidgets_widget *owner,
    xxwidgets_rect bounds, xxwidgets_file_information **out);
XXWIDGETS_API xxwidgets_status xxwidgets_file_information_destroy(xxwidgets_file_information *information);
XXWIDGETS_API xxwidgets_status xxwidgets_file_information_set_rect(xxwidgets_file_information *information, xxwidgets_rect bounds);
XXWIDGETS_API xxwidgets_widget *xxwidgets_file_information_control(const xxwidgets_file_information *information,
    xxwidgets_file_information_control_id control);
XXWIDGETS_API xxwidgets_data_struct_view *xxwidgets_file_information_view(const xxwidgets_file_information *information);
XXWIDGETS_API xxwidgets_status xxwidgets_file_information_handle_event(xxwidgets_file_information *information,
    const xxwidgets_event *event);
/* Deep-copy metadata and all structures/fields. On failure the old data remains
 * installed. size >= 0; an empty items array is valid. Repeated names and raw
 * descriptors are retained. Selection and field navigation use the view API. */
XXWIDGETS_API xxwidgets_status xxwidgets_file_information_set_data(xxwidgets_file_information *information,
    const char *path, const char *file_type, int64_t size, const xxwidgets_data_struct_item *items, size_t count);

/* Resizable modal browser, Close/Escape only. Copies the supplied data and
 * restores the owner's enabled state/event handler on every exit. Call outside
 * input dispatch/polling. A nested modal returns BUSY. No source file is edited. */
XXWIDGETS_API xxwidgets_status xxwidgets_file_information_dialog(xxwidgets_widget *owner, const char *title,
    const char *path, const char *file_type, int64_t size, const xxwidgets_data_struct_item *items, size_t count);

#ifdef __cplusplus
}
#endif
#endif
