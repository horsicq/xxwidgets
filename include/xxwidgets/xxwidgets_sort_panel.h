/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/* Sort panel: the form of Detect It Easy's DIE-sort (XScanSortWidget) as
 * plain xxwidgets controls, for native and terminal backends. It is a view
 * only: it edits an xxwidgets_sort_settings, opens its own pickers and
 * folder choosers, and reports a Scan request; the caller runs the sort.
 *
 * Rows: directory + browse; Subdirectories, Log, Scan; scan flags; buffer
 * sizes; main / custom database; filter (all file types / types + pickers,
 * Unknown); result directory; Catalog + format + variables help; Copy +
 * format + Remove source + Move to first + variables help.
 *
 * Use: create it in a window, forward every event to
 * xxwidgets_sort_panel_handle_event() and call xxwidgets_sort_panel_process()
 * after each poll, outside input callbacks (it may open modal dialogs).
 * process() returns XXWIDGETS_OK and sets *scan to 1 once Scan was pressed. */
#ifndef XXWIDGETS_SORT_PANEL_H
#define XXWIDGETS_SORT_PANEL_H
#include "xxwidgets/xxwidgets.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Flag bits: XScanEngine's SF_* values. */
#define XXWIDGETS_SORT_SF_DEEP 0x00000001u
#define XXWIDGETS_SORT_SF_HEURISTIC 0x00000002u
#define XXWIDGETS_SORT_SF_ALLTYPES 0x00000004u
#define XXWIDGETS_SORT_SF_RECURSIVE 0x00000008u
#define XXWIDGETS_SORT_SF_VERBOSE 0x00000010u
#define XXWIDGETS_SORT_SF_OVERLAY 0x00000040u
#define XXWIDGETS_SORT_SF_RESOURCES 0x00000080u
#define XXWIDGETS_SORT_SF_ARCHIVES 0x00000100u

/* All strings are UTF-8 and owned (malloc'd); NULL reads as "". */
typedef struct xxwidgets_sort_settings {
    char *directory;
    int subdirectories;
    int log;
    unsigned int flags;
    char *read_buffer_size; /* bytes, "" = default */
    char *file_buffer_size; /* bytes, "" = default */
    char *main_database;
    char *custom_database;
    int use_custom_database;
    int all_file_types;
    char *file_types; /* '|'-separated names */
    int all_types;
    char *types; /* '|'-separated names */
    int unknown;
    char *result_directory;
    int catalog;
    char *catalog_format;
    int copy;
    char *copy_format;
    int remove_source;
    int move_to_first;
} xxwidgets_sort_settings;

/* DIE-sort defaults (formats, "collection", "$data/db", subdirectories on...). */
XXWIDGETS_API xxwidgets_status xxwidgets_sort_settings_init(xxwidgets_sort_settings *settings);
XXWIDGETS_API void xxwidgets_sort_settings_free(xxwidgets_sort_settings *settings);
/* Replace one owned string field. */
XXWIDGETS_API xxwidgets_status xxwidgets_sort_settings_set(char **field, const char *value);

typedef struct xxwidgets_sort_panel_config {
    const char *const *file_types; /* names offered by the file type picker */
    size_t file_type_count;
    const char *const *types; /* names offered by the type picker */
    size_t type_count;
    const char *variables_help; /* shown by the "?" buttons; NULL: a built-in list */
} xxwidgets_sort_panel_config;

typedef struct xxwidgets_sort_panel xxwidgets_sort_panel;

typedef enum xxwidgets_sort_panel_control_id {
    XXWIDGETS_SORT_DIRECTORY_LABEL = 0,
    XXWIDGETS_SORT_DIRECTORY,
    XXWIDGETS_SORT_DIRECTORY_BROWSE,
    XXWIDGETS_SORT_SUBDIRECTORIES,
    XXWIDGETS_SORT_LOG,
    XXWIDGETS_SORT_SCAN,
    XXWIDGETS_SORT_FLAGS_LABEL,
    XXWIDGETS_SORT_FLAG_RECURSIVE,
    XXWIDGETS_SORT_FLAG_OVERLAY,
    XXWIDGETS_SORT_FLAG_RESOURCES,
    XXWIDGETS_SORT_FLAG_ARCHIVES,
    XXWIDGETS_SORT_FLAG_DEEP,
    XXWIDGETS_SORT_FLAG_HEURISTIC,
    XXWIDGETS_SORT_FLAG_VERBOSE,
    XXWIDGETS_SORT_FLAG_ALLTYPES,
    XXWIDGETS_SORT_READ_BUFFER_LABEL,
    XXWIDGETS_SORT_READ_BUFFER,
    XXWIDGETS_SORT_FILE_BUFFER_LABEL,
    XXWIDGETS_SORT_FILE_BUFFER,
    XXWIDGETS_SORT_MAIN_DATABASE_LABEL,
    XXWIDGETS_SORT_MAIN_DATABASE,
    XXWIDGETS_SORT_MAIN_DATABASE_BROWSE,
    XXWIDGETS_SORT_CUSTOM_DATABASE_ENABLED,
    XXWIDGETS_SORT_CUSTOM_DATABASE,
    XXWIDGETS_SORT_CUSTOM_DATABASE_BROWSE,
    XXWIDGETS_SORT_ALL_FILE_TYPES,
    XXWIDGETS_SORT_FILE_TYPES_CHOOSE,
    XXWIDGETS_SORT_FILE_TYPES_SUMMARY,
    XXWIDGETS_SORT_ALL_TYPES,
    XXWIDGETS_SORT_TYPES_CHOOSE,
    XXWIDGETS_SORT_TYPES_SUMMARY,
    XXWIDGETS_SORT_UNKNOWN,
    XXWIDGETS_SORT_RESULT_LABEL,
    XXWIDGETS_SORT_RESULT,
    XXWIDGETS_SORT_RESULT_BROWSE,
    XXWIDGETS_SORT_CATALOG,
    XXWIDGETS_SORT_CATALOG_FORMAT,
    XXWIDGETS_SORT_CATALOG_HELP,
    XXWIDGETS_SORT_COPY,
    XXWIDGETS_SORT_COPY_FORMAT,
    XXWIDGETS_SORT_COPY_HELP,
    XXWIDGETS_SORT_REMOVE_SOURCE,
    XXWIDGETS_SORT_MOVE_TO_FIRST,
    XXWIDGETS_SORT_CONTROL_COUNT
} xxwidgets_sort_panel_control_id;

/* The panel's controls are children of owner (a WINDOW). config is copied. */
XXWIDGETS_API xxwidgets_status xxwidgets_sort_panel_create(xxwidgets_widget *owner, xxwidgets_rect bounds, const xxwidgets_sort_panel_config *config,
                                                           xxwidgets_sort_panel **out);
XXWIDGETS_API xxwidgets_status xxwidgets_sort_panel_destroy(xxwidgets_sort_panel *panel);
XXWIDGETS_API xxwidgets_status xxwidgets_sort_panel_set_rect(xxwidgets_sort_panel *panel, xxwidgets_rect bounds);
/* The smallest bounds the form fits in (text cells). */
XXWIDGETS_API xxwidgets_status xxwidgets_sort_panel_minimum_size(int *width, int *height);
XXWIDGETS_API xxwidgets_widget *xxwidgets_sort_panel_control(const xxwidgets_sort_panel *panel, xxwidgets_sort_panel_control_id id);
/* Show settings in the controls (strings are copied). */
XXWIDGETS_API xxwidgets_status xxwidgets_sort_panel_set(xxwidgets_sort_panel *panel, const xxwidgets_sort_settings *settings);
/* Read the controls into settings (freed and refilled). */
XXWIDGETS_API xxwidgets_status xxwidgets_sort_panel_get(const xxwidgets_sort_panel *panel, xxwidgets_sort_settings *settings);
/* 1 when the event belonged to the panel (it is then fully handled). */
XXWIDGETS_API int xxwidgets_sort_panel_handle_event(xxwidgets_sort_panel *panel, const xxwidgets_event *event);
/* Run a pending action (browse, picker, help). *scan becomes 1 when Scan was
 * pressed (the caller then gets the settings and runs the sort). */
XXWIDGETS_API xxwidgets_status xxwidgets_sort_panel_process(xxwidgets_sort_panel *panel, int *scan);
/* As if Scan were pressed (shortcut F5). */
XXWIDGETS_API void xxwidgets_sort_panel_request_scan(xxwidgets_sort_panel *panel);
/* Enable or disable the whole form, e.g. while a sort runs. */
XXWIDGETS_API xxwidgets_status xxwidgets_sort_panel_set_enabled(xxwidgets_sort_panel *panel, int enabled);

#ifdef __cplusplus
}
#endif
#endif
