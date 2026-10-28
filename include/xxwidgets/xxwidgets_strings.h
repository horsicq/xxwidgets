/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXWIDGETS_STRINGS_H
#define XXWIDGETS_STRINGS_H
#include "xxwidgets/xxwidgets.h"
#include "xxwidgets/xxwidgets_disk_write.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_strings xxwidgets_strings;
/* Display metadata supplied by a scanner. Strings are valid UTF-8 and copied;
 * text is decoded text, encoding is a nonempty display name. Offsets and byte
 * sizes describe the original bytes, character_length counts decoded symbols.
 * region names the section or segment holding the string (NULL for none) and
 * address is where it is loaded when has_address is nonzero. The widget
 * neither scans files nor retains their bytes. */
typedef struct xxwidgets_string_record {
    uint64_t offset, byte_size, character_length;
    const char *encoding, *text;
    const char *region;
    uint64_t address;
    int has_address;
} xxwidgets_string_record;

typedef enum xxwidgets_strings_control_id {
    /* Retained hidden controls for source/API compatibility. The visible
     * browser provides one filter below each table column heading. */
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
 * writes until after the event callback. RESULTS is an XXWIDGETS_TABLE with the
 * columns #, Offset, Address, Region, Size, Type and String, each with its own
 * filter (xxwidgets_table_set_filter); its rows also respect any programmatic
 * xxwidgets_strings_set_filter and its value indexes them. Use this composite's
 * getters for string records. FILTER_LABEL, FILTER and HEADER remain hidden.
 * ACTIVATE may also open View. No setters emit
 * user-input events. */
XXWIDGETS_API xxwidgets_status xxwidgets_strings_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_strings **out);
XXWIDGETS_API xxwidgets_status xxwidgets_strings_destroy(xxwidgets_strings *view);
XXWIDGETS_API xxwidgets_status xxwidgets_strings_set_rect(xxwidgets_strings *view, xxwidgets_rect bounds);
XXWIDGETS_API xxwidgets_widget *xxwidgets_strings_control(const xxwidgets_strings *view, xxwidgets_strings_control_id id);
XXWIDGETS_API xxwidgets_status xxwidgets_strings_handle_event(xxwidgets_strings *view, const xxwidgets_event *event);
/* Replacement is transactional, selects the first matching record, and keeps
 * the current filter. NULL/0 clears. Invalid input/allocation/backend failures
 * preserve the preceding records and displayed rows. */
XXWIDGETS_API xxwidgets_status xxwidgets_strings_set_records(xxwidgets_strings *view, const xxwidgets_string_record *records, size_t count);
/* As set_records, with copied 64-bit display numbers for the # column.
 * Record getter indexes still refer to this page's input order. */
XXWIDGETS_API xxwidgets_status xxwidgets_strings_set_records_numbered(xxwidgets_strings *view,
    const xxwidgets_string_record *records, const uint64_t *numbers, size_t count);
/* Programmatic substring search over text/encoding, ASCII case-insensitive; other UTF-8
 * bytes compare exactly. Keeps selection if that record remains visible.
 * Visible records pass this filter and the column filters of RESULTS. */
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

/* A browser page has a fixed upper bound even when the backing file contains
 * billions of strings. Counts, original record numbers and page positions are
 * 64-bit; no native table or heap allocation is proportional to count. */
#define XXWIDGETS_STRINGS_PAGE_SIZE 128
#define XXWIDGETS_STRINGS_COLUMN_COUNT 7
typedef xxwidgets_status (*xxwidgets_strings_read_fn)(void *user, uint64_t first, size_t capacity,
    xxwidgets_string_record *records, uint64_t *original_indices, size_t *received);
typedef xxwidgets_status (*xxwidgets_strings_search_fn)(void *user, const char *filter,
    const char *const column_filters[XXWIDGETS_STRINGS_COLUMN_COUNT], xx_pd_struct *progress, uint64_t *matched_count);
typedef xxwidgets_status (*xxwidgets_strings_source_save_fn)(void *user, const char *path, xx_pd_struct *progress);
/* Extraction controls are staged by the browser until Search. format and
 * map_mode are indices into the source's label arrays. minimum_length
 * is at least two decoded characters. expression is UTF-8, including its NUL.
 * When formats is supplied, index zero is the physical Binary view; selecting
 * it uses map_mode zero and disables memory-map selection in the browser. */
typedef struct xxwidgets_strings_search_options {
    unsigned minimum_length;
    int ansi, unicode, null_terminated, links, regexp;
    char expression[1024];
    size_t format, map_mode;
} xxwidgets_strings_search_options;
XXWIDGETS_API void xxwidgets_strings_search_options_init(xxwidgets_strings_search_options *options);
typedef xxwidgets_status (*xxwidgets_strings_search_ex_fn)(void *user,
    xxwidgets_strings_search_options *options, const char *filter,
    const char *const column_filters[XXWIDGETS_STRINGS_COLUMN_COUNT], xx_pd_struct *progress, uint64_t *matched_count);
typedef struct xxwidgets_strings_source {
    uint64_t count, total_count;
    xxwidgets_strings_read_fn read;
    xxwidgets_strings_search_fn search;
    xxwidgets_strings_source_save_fn save;
    void *user;
    const char *const *formats;
    size_t format_count;
    const char *const *map_modes;
    size_t map_mode_count;
    xxwidgets_strings_search_options options;
    xxwidgets_strings_search_ex_fn search_ex;
    /* Optional borrowed UTF-8 explanation of the last rejected operation.
     * Read on the UI thread after the worker has joined. */
    const char *(*error)(void *user);
    /* Nonzero when read returns bounded text previews. View/Copy are labelled
     * accordingly; save still receives the complete provider result set. */
    int records_are_previews;
    /* Optional disk-aware writer; supersedes save and reports actual written
     * bytes/current filename through the dedicated disk progress dialog. */
    xxwidgets_status (*save_ex)(void *user, const char *path, xx_pd_struct *progress, xxwidgets_disk_write_reporter *reporter);
} xxwidgets_strings_source;

/* read runs on the UI thread for at most PAGE_SIZE rows; its UTF-8 record
 * strings are borrowed until the next read. The browser copies the page.
 * search/save run exclusively on a worker, never call widget APIs, and use
 * xx_pd helpers for progress and cancellation. search applies all filters to
 * the complete source (not merely the displayed page). It must transactionally
 * replace its result index only on success without cancellation, preserving
 * the preceding index on errors/stop. NULL search makes filters read-only;
 * NULL save/save_ex hides Save CSV. search_ex supersedes search, enables the extraction
 * toolbar, and may publish a new total_count only on success. It receives a
 * private options copy; on success it may normalize that copy to the accepted
 * settings, which the browser then publishes. Its provider
 * must preserve the preceding result index on error/cancellation. options,
 * count and total_count are published by the browser only after success.
 * A successful search may replace format/map labels and counts with the newly
 * detected source description; the browser reloads both selectors afterward.
 * Arrays/labels and source/user remain owned by the caller for the dialog. */
XXWIDGETS_API xxwidgets_status xxwidgets_strings_source_set_page(xxwidgets_strings *view,
    const xxwidgets_strings_source *source, uint64_t first);
/* Starts the worker and keeps the UI polling with a cancellable progress
 * dialog for long searches. Call outside event dispatch. A cancelled search
 * returns OK with cancelled=1, preserves count and leaves its source intact. */
XXWIDGETS_API xxwidgets_status xxwidgets_strings_source_search(xxwidgets_widget *owner,
    xxwidgets_strings_source *source, const char *filter,
    const char *const column_filters[XXWIDGETS_STRINGS_COLUMN_COUNT], int *cancelled);
/* Search with staged extraction options. Invalid options never start a worker.
 * Errors/cancellation preserve source options, count and total_count. */
XXWIDGETS_API xxwidgets_status xxwidgets_strings_source_search_ex(xxwidgets_widget *owner,
    xxwidgets_strings_source *source, const xxwidgets_strings_search_options *options, const char *filter,
    const char *const column_filters[XXWIDGETS_STRINGS_COLUMN_COUNT], int *cancelled);
XXWIDGETS_API xxwidgets_status xxwidgets_strings_source_dialog(xxwidgets_widget *owner,
    const char *title, const char *path, uint64_t size, xxwidgets_strings_source *source);

/* Shared modal browser: copied metadata, filtering, Copy and full-text View.
 * Save CSV invokes the host callback only after native destination acceptance,
 * outside event dispatch. NULL callback or absent chooser hides Save CSV.
 * The callback borrows UTF-8 path/CSV and writes exactly length bytes. */
typedef xxwidgets_status (*xxwidgets_strings_save_fn)(const char *path, const char *csv, size_t length, void *user);
XXWIDGETS_API xxwidgets_status xxwidgets_strings_dialog(xxwidgets_widget *owner, const char *title,
    const char *path, uint64_t size, const xxwidgets_string_record *records, size_t count,
    xxwidgets_strings_save_fn save, void *user);
#ifdef __cplusplus
}
#endif
#endif
