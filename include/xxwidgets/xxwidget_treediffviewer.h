/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGET_TREEDIFFVIEWER_H
#define XXWIDGET_TREEDIFFVIEWER_H

#include "xxwidgets/xxwidgets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A tree of differences between two snapshots (files, registry, ...): a lazy
 * tree of change rows, a path filter, a change-kind filter, a Field | Before
 * | After table for the selected row and a hex view of the before or after
 * value of the selected field. All data comes from callbacks; the viewer
 * keeps only the rows the tree loaded (see xxwidgets_treeview_set_source for
 * paging: a parent with 300000 changed children opens with its first page
 * and a "... N more" row). */

/* How a row differs. CONTEXT: an unchanged parent, shown to reach changes. */
typedef enum xxwidget_treediff_change {
    XXWIDGET_TREEDIFF_CONTEXT = 0,
    XXWIDGET_TREEDIFF_ADDED,
    XXWIDGET_TREEDIFF_REMOVED,
    XXWIDGET_TREEDIFF_MODIFIED,
    XXWIDGET_TREEDIFF_KIND_CHANGED,
    XXWIDGET_TREEDIFF_ACCESS_CHANGED,
    XXWIDGET_TREEDIFF_CHANGE_COUNT
} xxwidget_treediff_change;
/* Filter bit of a change kind; ALL: every kind but CONTEXT. */
#define XXWIDGET_TREEDIFF_MASK(change) (1u << (unsigned int)(change))
#define XXWIDGET_TREEDIFF_ALL_CHANGES 0x3eu

typedef struct xxwidget_treediff_filter {
    unsigned int kinds; /* XXWIDGET_TREEDIFF_MASK bits of the kinds shown. */
    const char *path;   /* UTF-8 path filter text; "" when there is none. */
} xxwidget_treediff_filter;

/* A row is shown as "<sign> <kind>  <text>": sign '+' added, '-' removed,
 * '*' modified, '!' kind changed, '~' access changed, ' ' context; kind is the
 * kind letter (e.g. 'F' file, 'D' directory, 'K' key, 'V' value; 0 shows a
 * space). Colors follow the change: added green, removed red, modified amber,
 * kind changed purple, access changed gray, context the system colors. */
typedef struct xxwidget_treediff_row {
    uint64_t id;                     /* Nonzero, unique in the diff. */
    xxwidget_treediff_change change; /* Other values show as CONTEXT. */
    char kind;
    const char *text; /* UTF-8 name; a flat search result's path. */
    int has_children;
} xxwidget_treediff_row;

/* One line of the details table. NULL strings are empty cells. A blob is the
 * raw value the hex view shows (e.g. a registry value's data); NULL or a zero
 * size means none. */
typedef struct xxwidget_treediff_field {
    const char *field, *before, *after;
    const void *before_blob, *after_blob;
    size_t before_size, after_size;
} xxwidget_treediff_field;

/* Callbacks run on the UI thread, inside viewer calls and input handling;
 * as in an event callback they cannot destroy widgets (the viewer's, its
 * owner) or the app, or poll (BUSY). The strings and blobs they hand out
 * must stay valid until the same callback is called again (copied). Counting callbacks write at most capacity entries, set
 * *total to the full count and return the number written, or SIZE_MAX on
 * failure (the viewer keeps what it showed). */
typedef struct xxwidget_treediff_source {
    /* Rows under parent (0: the roots) that pass filter, from row offset. */
    size_t (*children)(void *user, uint64_t parent, const xxwidget_treediff_filter *filter, size_t offset, size_t capacity, xxwidget_treediff_row *rows, size_t *total);
    /* The details of row id. */
    size_t (*fields)(void *user, uint64_t id, xxwidget_treediff_field *fields, size_t capacity, size_t *total);
    /* Optional flat search: while the path filter is not empty, the roots are
     * the rows matching filter anywhere (their text a path); children still
     * lists below them. NULL: children gets the path filter itself. */
    size_t (*search)(void *user, const xxwidget_treediff_filter *filter, size_t offset, size_t capacity, xxwidget_treediff_row *rows, size_t *total);
    /* Optional: the parent id of row id (0 for a root), so a selected search
     * result is found in the tree again when the path filter is cleared. */
    uint64_t (*parent)(void *user, uint64_t id);
    void *user;
    size_t page_size; /* Rows per request; 0: XXWIDGETS_TREE_PAGE_DEFAULT. */
} xxwidget_treediff_source;

typedef struct xxwidget_treediffviewer xxwidget_treediffviewer;
typedef enum xxwidget_treediffviewer_control_id {
    XXWIDGET_TREEDIFF_PATH_LABEL,
    XXWIDGET_TREEDIFF_PATH,
    XXWIDGET_TREEDIFF_KINDS,
    XXWIDGET_TREEDIFF_TREE,
    XXWIDGET_TREEDIFF_DETAILS,
    XXWIDGET_TREEDIFF_SIDE_LABEL,
    XXWIDGET_TREEDIFF_SIDE,
    XXWIDGET_TREEDIFF_HEX,
    XXWIDGET_TREEDIFF_STATUS,
    XXWIDGET_TREEDIFF_CONTROL_COUNT
} xxwidget_treediffviewer_control_id;

/* Creates the controls in owner (a WINDOW) inside bounds of at least 60 x 16
 * cells: Path edit and Changes check combo (all kinds checked) on top, the
 * tree on the left, details, the Before/After choice and the hex view on the
 * right, a status line at the bottom. Tab moves in that order. source is
 * copied (its callbacks and user data are borrowed until destroy); the roots
 * are fetched now. Destroy before the owner. */
XXWIDGETS_API xxwidgets_status xxwidget_treediffviewer_create(xxwidgets_widget *owner, xxwidgets_rect bounds, const xxwidget_treediff_source *source,
                                                              xxwidget_treediffviewer **out);
XXWIDGETS_API xxwidgets_status xxwidget_treediffviewer_destroy(xxwidget_treediffviewer *viewer);
XXWIDGETS_API xxwidgets_status xxwidget_treediffviewer_set_rect(xxwidget_treediffviewer *viewer, xxwidgets_rect bounds);
XXWIDGETS_API xxwidgets_widget *xxwidget_treediffviewer_control(const xxwidget_treediffviewer *viewer, xxwidget_treediffviewer_control_id id);
/* Forward every event (others are ignored). Selecting a row shows its
 * fields; selecting a field or Before/After shows that blob; Enter in Path or
 * a change of the kinds applies the filter. Call it from the event callback:
 * it only uses setters (no dialogs, no nested polling). */
XXWIDGETS_API xxwidgets_status xxwidget_treediffviewer_handle_event(xxwidget_treediffviewer *viewer, const xxwidgets_event *event);
/* Reloads the rows and the selected row's fields after the data changed,
 * keeping expanded and selected ids (xxwidgets_treeview_source_refresh). */
XXWIDGETS_API xxwidgets_status xxwidget_treediffviewer_refresh(xxwidget_treediffviewer *viewer);
/* Sets the controls and applies the filter, as the user would; path NULL is
 * "". kinds holds XXWIDGET_TREEDIFF_MASK bits (CONTEXT ignored). */
XXWIDGETS_API xxwidgets_status xxwidget_treediffviewer_set_filter(xxwidget_treediffviewer *viewer, unsigned int kinds, const char *path);
/* The filter in effect; path is borrowed until the filter changes. */
XXWIDGETS_API xxwidgets_status xxwidget_treediffviewer_get_filter(const xxwidget_treediffviewer *viewer, unsigned int *kinds, const char **path);
/* id of the selected row, 0 for none (or a "... N more" row). */
XXWIDGETS_API xxwidgets_status xxwidget_treediffviewer_selection(xxwidget_treediffviewer *viewer, uint64_t *id);
/* Selects the row reached by ids from a root down (xxwidgets_treeview_source_select)
 * and shows its fields. */
XXWIDGETS_API xxwidgets_status xxwidget_treediffviewer_select_path(xxwidget_treediffviewer *viewer, const uint64_t *path, size_t count);
/* Presentation helpers: the row colors and the English change names. */
XXWIDGETS_API xxwidgets_tree_color xxwidget_treediff_change_color(xxwidget_treediff_change change);
XXWIDGETS_API const char *xxwidget_treediff_change_name(xxwidget_treediff_change change);

#ifdef __cplusplus
}
#endif
#endif
