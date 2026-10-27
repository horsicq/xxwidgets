/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_HASH_H
#define XXWIDGETS_HASH_H
#include "xxwidgets/xxwidgets.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xxwidgets_hash xxwidgets_hash;
typedef struct xxwidgets_hash_entry { const char *algorithm, *value; } xxwidgets_hash_entry;
typedef enum xxwidgets_hash_control_id {
    XXWIDGETS_HASH_PATH, XXWIDGETS_HASH_SUMMARY, XXWIDGETS_HASH_LIST,
    XXWIDGETS_HASH_VALUE, XXWIDGETS_HASH_COPY, XXWIDGETS_HASH_COPY_ALL,
    XXWIDGETS_HASH_STATUS, XXWIDGETS_HASH_CONTROL_COUNT
} xxwidgets_hash_control_id;
/* Engine-independent read-only digest browser built from shared controls.
 * Minimum 72 x 12 cells. All calls belong to the UI thread. Destroy before
 * the owner. set_data copies at most 64 entries transactionally: printable
 * ASCII algorithm names (1..64 bytes), even-length hex values (2..512 bytes).
 * No file bytes, hash calculation or DataStructs are part of this widget. */
XXWIDGETS_API xxwidgets_status xxwidgets_hash_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_hash **out);
XXWIDGETS_API xxwidgets_status xxwidgets_hash_destroy(xxwidgets_hash *view);
XXWIDGETS_API xxwidgets_status xxwidgets_hash_set_rect(xxwidgets_hash *view, xxwidgets_rect bounds);
XXWIDGETS_API xxwidgets_status xxwidgets_hash_set_data(xxwidgets_hash *view, const char *path, uint64_t size,
    const xxwidgets_hash_entry *entries, size_t count);
XXWIDGETS_API size_t xxwidgets_hash_count(const xxwidgets_hash *view);
/* Borrowed data, valid until replacement/destruction. */
XXWIDGETS_API xxwidgets_status xxwidgets_hash_get_entry(const xxwidgets_hash *view, size_t index, xxwidgets_hash_entry *entry);
XXWIDGETS_API xxwidgets_widget *xxwidgets_hash_control(const xxwidgets_hash *view, xxwidgets_hash_control_id id);
/* Forward events to support selection, Copy selected (digest only), and Copy
 * all (algorithm: digest lines). Clipboard unavailability is shown in Status. */
XXWIDGETS_API xxwidgets_status xxwidgets_hash_handle_event(xxwidgets_hash *view, const xxwidgets_event *event);
/* required includes NUL. NULL/0 queries length; short buffers are terminated. */
XXWIDGETS_API xxwidgets_status xxwidgets_hash_export_text(const xxwidgets_hash *view, char *buffer, size_t capacity, size_t *required);
/* Resizable Close/Esc modal; copies data, restores owner state on every exit.
 * Call outside event dispatch. Nested modal requests return BUSY. */
XXWIDGETS_API xxwidgets_status xxwidgets_hash_dialog(xxwidgets_widget *owner, const char *title, const char *path,
    uint64_t size, const xxwidgets_hash_entry *entries, size_t count);
#ifdef __cplusplus
}
#endif
#endif
