/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXWIDGETS_STRINGS_H
#define XXWIDGETS_STRINGS_H
#include "xxwidgets/xxwidgets.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_strings xxwidgets_strings;
/* Display metadata supplied by a scanner. Strings are valid UTF-8 and copied;
 * text is decoded text, encoding is a nonempty display name. Offsets and byte
 * sizes describe the original bytes, character_length counts decoded symbols.
 * The widget neither scans files nor retains their bytes. */
typedef struct xxwidgets_string_record {
    uint64_t offset, byte_size, character_length;
    const char *encoding, *text;
} xxwidgets_string_record;

typedef enum xxwidgets_strings_control_id {
    XXWIDGETS_STRINGS_FILTER_LABEL,
    XXWIDGETS_STRINGS_FILTER,
    XXWIDGETS_STRINGS_HEADER,
    XXWIDGETS_STRINGS_RESULTS,
    XXWIDGETS_STRINGS_DETAILS,
    XXWIDGETS_STRINGS_COUNT,
    XXWIDGETS_STRINGS_COPY,
    XXWIDGETS_STRINGS_VIEW,
    XXWIDGETS_STRINGS_SAVE,
    XXWIDGETS_STRINGS_CONTROL_COUNT
} xxwidgets_strings_control_id;

/* Native/TUI composite using portable controls, bounds >=72x14 cells. Owns
 * its child controls. Destroy before its WINDOW owner/application. Forward
 * events to handle_event: filtering, selection and Copy are handled here.
 * VIEW and SAVE clicks are left to the caller; defer modal dialogs and file
 * writes until after the event callback. RESULTS is an ARCHIVEVIEW row renderer;
 * use this composite's getters for string records. ACTIVATE may also open View.
 * No setters emit user-input events. */
XXWIDGETS_API xxwidgets_status xxwidgets_strings_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_strings **out);
XXWIDGETS_API xxwidgets_status xxwidgets_strings_destroy(xxwidgets_strings *view);
XXWIDGETS_API xxwidgets_status xxwidgets_strings_set_rect(xxwidgets_strings *view, xxwidgets_rect bounds);
XXWIDGETS_API xxwidgets_widget *xxwidgets_strings_control(const xxwidgets_strings *view, xxwidgets_strings_control_id id);
XXWIDGETS_API xxwidgets_status xxwidgets_strings_handle_event(xxwidgets_strings *view, const xxwidgets_event *event);
/* Replacement is transactional, selects the first matching record, and keeps
 * the current filter. NULL/0 clears. Invalid input/allocation/backend failures
 * preserve the preceding records and displayed rows. */
XXWIDGETS_API xxwidgets_status xxwidgets_strings_set_records(xxwidgets_strings *view, const xxwidgets_string_record *records, size_t count);
/* Substring search over text/encoding, ASCII case-insensitive; other UTF-8
 * bytes compare exactly. Keeps selection if that record remains visible. */
XXWIDGETS_API xxwidgets_status xxwidgets_strings_set_filter(xxwidgets_strings *view, const char *filter);
XXWIDGETS_API size_t xxwidgets_strings_count(const xxwidgets_strings *view);
XXWIDGETS_API size_t xxwidgets_strings_visible_count(const xxwidgets_strings *view);
/* Strings returned below are borrowed until record replacement/destruction.
 * Original indexes always refer to set_records input order. An empty selection
 * succeeds with index SIZE_MAX and a zeroed record. */
XXWIDGETS_API xxwidgets_status xxwidgets_strings_get_record(const xxwidgets_strings *view, size_t index, xxwidgets_string_record *record);
XXWIDGETS_API xxwidgets_status xxwidgets_strings_get_visible_record(const xxwidgets_strings *view, size_t row, size_t *index,
                                                                 xxwidgets_string_record *record);
XXWIDGETS_API xxwidgets_status xxwidgets_strings_get_selection(xxwidgets_strings *view, size_t *index, xxwidgets_string_record *record);
/* Copies complete decoded text. POSIX terminals return UNAVAILABLE. */
XXWIDGETS_API xxwidgets_status xxwidgets_strings_copy_selection(xxwidgets_strings *view);
/* Exports visible records in order, preserving full UTF-8 text and escaping
 * CSV quotes/newlines. required includes NUL; NULL/0 queries size, short
 * buffers are terminated. No file is written. */
XXWIDGETS_API xxwidgets_status xxwidgets_strings_export_csv(const xxwidgets_strings *view, char *buffer, size_t capacity, size_t *required);

#ifdef __cplusplus
}
#endif
#endif
