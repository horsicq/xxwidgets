/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef XXWIDGETS_XXWIDGETS_H
#define XXWIDGETS_XXWIDGETS_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && !defined(XXWIDGETS_STATIC)
#if defined(XXWIDGETS_BUILDING_LIBRARY)
#define XXWIDGETS_API __declspec(dllexport)
#else
#define XXWIDGETS_API __declspec(dllimport)
#endif
#else
#define XXWIDGETS_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_app xxwidgets_app;
typedef struct xxwidgets_widget xxwidgets_widget;
typedef struct xx_io_device xx_io_device;
typedef struct xxwidgets_about_dialog xxwidgets_about_dialog;
typedef struct xx_meta_string xx_meta_string;
typedef struct xx_var xx_var;
typedef struct xx_pd_struct xx_pd_struct;

typedef enum xxwidgets_status {
    XXWIDGETS_OK = 0,
    XXWIDGETS_INVALID_ARGUMENT,
    XXWIDGETS_OUT_OF_MEMORY,
    XXWIDGETS_UNAVAILABLE,
    XXWIDGETS_PLATFORM_ERROR,
    XXWIDGETS_BUFFER_TOO_SMALL,
    XXWIDGETS_BUSY
} xxwidgets_status;

typedef enum xxwidgets_backend {
    XXWIDGETS_BACKEND_AUTO = 0,
    XXWIDGETS_BACKEND_NATIVE,
    XXWIDGETS_BACKEND_TUI
} xxwidgets_backend;

typedef enum xxwidgets_kind {
    XXWIDGETS_WINDOW = 0,
    XXWIDGETS_LABEL,
    XXWIDGETS_BUTTON,
    XXWIDGETS_EDIT,
    XXWIDGETS_CHECKBOX,
    XXWIDGETS_LISTBOX,
    XXWIDGETS_PROGRESS,
    XXWIDGETS_HEXVIEW,
    XXWIDGETS_ARCHIVEVIEW,
    XXWIDGETS_ARCHIVEBROWSER,
    XXWIDGETS_COMBOBOX,
    XXWIDGETS_CHECKCOMBOBOX,
    XXWIDGETS_SCANRESULTS,
    XXWIDGETS_TREEVIEW,
    XXWIDGETS_DATA_STRUCT_TABLE,
    /* Children created by xxwidgets_entropy_create. */
    XXWIDGETS_ENTROPY_REGIONS,
    XXWIDGETS_ENTROPY_BYTES,
    XXWIDGETS_ENTROPY_PLOT,
    XXWIDGETS_CANVAS,
    XXWIDGETS_SPLITTER,
    XXWIDGET_SYSTEMPROCESSESVIEWER,
    XXWIDGET_RESOURCESVIEWER,
    XXWIDGET_IMPORTSVIEWER,
    XXWIDGET_EXPORTSVIEWER,
    XXWIDGET_SYMBOLSVIEWER,
    XXWIDGETS_DEBUGGER_DISASM,
    XXWIDGETS_DEBUGGER_REGISTERS,
    XXWIDGETS_DEBUGGER_STACK,
    XXWIDGETS_DIE_SIGNATURE_EDIT,
    XXWIDGETS_MEMORY_MAP
} xxwidgets_kind;

typedef enum xxwidgets_event_type {
    XXWIDGETS_EVENT_CLOSE = 0,
    XXWIDGETS_EVENT_CLICK,
    XXWIDGETS_EVENT_CHANGE,
    XXWIDGETS_EVENT_SELECT,
    XXWIDGETS_EVENT_RESIZE,
    XXWIDGETS_EVENT_ACTIVATE,
    XXWIDGETS_EVENT_CONTEXT_MENU,
    XXWIDGETS_EVENT_SHORTCUT,
    XXWIDGETS_EVENT_DROP_FILES,
    XXWIDGETS_EVENT_VIEW_CHANGE, /* Hex header menu applied; value is column 0..2. */
    XXWIDGETS_EVENT_DRAG, /* Splitter: absolute parent text-cell coordinates. */
    XXWIDGETS_EVENT_READ_ERROR /* HexView device input read failed; value is xxwidgets_status. View is retained. */
} xxwidgets_event_type;

/* Logical text-cell units: native controls scale these to font-sized pixels.
 * A window's width/height describe its client area. Child coordinates are
 * relative to that area; windows cannot be nested. */
typedef struct xxwidgets_rect {
    int x, y, width, height;
} xxwidgets_rect;

typedef struct xxwidgets_event {
    xxwidgets_event_type type;
    xxwidgets_widget *widget;
    int value; /* Control value/row, installed action ID for SHORTCUT, or xxwidgets_status for READ_ERROR. */
    /* CONTEXT_MENU only: popup anchor relative to the widget. Native backends
     * use container pixels; TUI uses text cells. Other types use 0, 0.
     * ArchiveBrowser context value is the selected visible row, or -1. */
    int x, y;
    /* DROP_FILES only: local UTF-8 paths in source order, borrowed until the
     * callback returns. Copy paths that must be kept. Other events use NULL/0. */
    const char *const *paths;
    size_t path_count;
} xxwidgets_event;

typedef void (*xxwidgets_event_fn)(xxwidgets_app *app, const xxwidgets_event *event, void *user_data);

typedef struct xxwidgets_config {
    xxwidgets_backend backend;
    xxwidgets_event_fn on_event;
    void *user_data;
} xxwidgets_config;

/* Zero-initialized configs select AUTO. All calls belong to one UI thread.
 * AUTO selects the compiled native backend, or TUI in a TUI-only build.
 * A failed native initialization is reported, never silently replaced. */
XXWIDGETS_API xxwidgets_status xxwidgets_app_create(const xxwidgets_config *config, xxwidgets_app **out_app);
/* Creation/destruction and nested event loops during callbacks return BUSY;
 * quit and destroy after run. Setters and focus are allowed in callbacks. */
XXWIDGETS_API xxwidgets_status xxwidgets_app_destroy(xxwidgets_app *app);
XXWIDGETS_API xxwidgets_backend xxwidgets_app_backend(const xxwidgets_app *app);
XXWIDGETS_API const char *xxwidgets_app_backend_name(const xxwidgets_app *app);
/* TUI applications start with colors enabled. Pass 0 for monochrome output,
 * retaining noncolor emphasis (selection/reverse video and dimmed text), or 1
 * to enable colors. Applies to all windows on the next redraw. Native backends
 * return UNAVAILABLE; values other than 0/1 return INVALID_ARGUMENT. */
XXWIDGETS_API xxwidgets_status xxwidgets_app_set_tui_color(xxwidgets_app *app, int enabled);
/* timeout_ms >= 0; 0 processes currently available events without waiting. */
XXWIDGETS_API xxwidgets_status xxwidgets_app_poll(xxwidgets_app *app, int timeout_ms);
XXWIDGETS_API int xxwidgets_app_run(xxwidgets_app *app);
XXWIDGETS_API void xxwidgets_app_quit(xxwidgets_app *app, int exit_code);
XXWIDGETS_API const char *xxwidgets_status_string(xxwidgets_status status);

typedef struct xxwidgets_shortcut {
    const char *sequence; /* Ctrl+O, Alt+Enter, Ctrl+Shift+E, F5, etc. */
    int action;
} xxwidgets_shortcut;

/* Replace a window's shortcuts from a list. Sequences are parsed; empty
 * sequences disable an entry, NULL/count 0 clears the list. Invalid/duplicate
 * combinations leave the previous list intact. Input emits SHORTCUT on the
 * window, with value=action, before handling the focused control's key.
 * Hidden/disabled windows and owners blocked by a modal dialog do not fire.
 * Supports letters/digits, F1..F24, navigation keys and named punctuation;
 * Ctrl/Control, Alt/Option, Shift, Meta/Cmd/Command/Win modifiers, any case. */
XXWIDGETS_API xxwidgets_status xxwidgets_window_set_shortcuts(xxwidgets_widget *window, const xxwidgets_shortcut *shortcuts, size_t count);
XXWIDGETS_API size_t xxwidgets_window_shortcut_count(const xxwidgets_widget *window);
/* Optional contextual filter for a matching binding. Returning zero lets the
 * focused control handle the key normally, without emitting SHORTCUT. Called
 * during input dispatch; do not destroy the window or pump events here.
 * Handler/user data are borrowed. NULL removes the filter. */
typedef int (*xxwidgets_shortcut_filter_fn)(xxwidgets_widget *window, int action, void *user_data);
XXWIDGETS_API xxwidgets_status xxwidgets_window_set_shortcut_filter(xxwidgets_widget *, xxwidgets_shortcut_filter_fn, void *);

typedef struct xxwidgets_menu_item {
    const char *menu;  /* Top-level group, e.g. "File". Adjacent entries share a group. */
    const char *label; /* UTF-8 label; NULL or empty inserts a separator. */
    int action;       /* Unique nonzero action for ordinary items. */
    int enabled;      /* 0 or 1; ignored for separators. */
} xxwidgets_menu_item;

/* Replace the native window menu with a copied list (NULL/count 0 clears it).
 * Activation emits SHORTCUT on the window with value=action. Install keyboard
 * bindings separately with window_set_shortcuts. Disabled/hidden windows and
 * owners blocked by a modal dialog do not activate menu actions. The menu is
 * outside the content-cell rectangle; child coordinates remain unchanged.
 * TUI returns UNAVAILABLE. Do not replace a menu from an input callback. */
XXWIDGETS_API xxwidgets_status xxwidgets_window_set_menu(xxwidgets_widget *window, const xxwidgets_menu_item *items, size_t count);
XXWIDGETS_API xxwidgets_status xxwidgets_window_set_menu_item_enabled(xxwidgets_widget *window, int action, int enabled);


/* Opt in to file drops anywhere in the window, including over its controls.
 * Emits DROP_FILES on the window. Disabled by default; hidden/disabled windows
 * and owners blocked by a modal dialog do not receive drops. Native backends
 * support local file/directory paths; enabling on TUI returns UNAVAILABLE.
 * This copies no files and does not delete/move the source. */
XXWIDGETS_API xxwidgets_status xxwidgets_window_set_accept_file_drops(xxwidgets_widget *window, int accept);

/* UTF-8 text is copied. A control must have a WINDOW parent in the same app.
 * Widgets start visible and enabled. Destroying a window destroys its children. */
XXWIDGETS_API xxwidgets_status xxwidgets_widget_create(xxwidgets_app *app, xxwidgets_widget *parent, xxwidgets_kind kind, const char *text, xxwidgets_rect rect,
                                                       xxwidgets_widget **out_widget);
XXWIDGETS_API xxwidgets_status xxwidgets_widget_destroy(xxwidgets_widget *widget);
XXWIDGETS_API xxwidgets_kind xxwidgets_widget_kind(const xxwidgets_widget *widget);
XXWIDGETS_API void *xxwidgets_widget_native_handle(const xxwidgets_widget *widget);
XXWIDGETS_API void xxwidgets_widget_set_user_data(xxwidgets_widget *widget, void *user_data);
XXWIDGETS_API void *xxwidgets_widget_user_data(const xxwidgets_widget *widget);
/* The app that created the widget, e.g. to add controls to a dialog window. */
XXWIDGETS_API xxwidgets_app *xxwidgets_widget_app(const xxwidgets_widget *widget);
XXWIDGETS_API xxwidgets_status xxwidgets_widget_set_text(xxwidgets_widget *widget, const char *text);
/* required includes the NUL byte. A NULL buffer with capacity 0 queries size.
 * Too-small buffers are NUL-terminated when capacity > 0. */
XXWIDGETS_API xxwidgets_status xxwidgets_widget_get_text(xxwidgets_widget *widget, char *buffer, size_t capacity, size_t *required);
XXWIDGETS_API xxwidgets_status xxwidgets_widget_set_rect(xxwidgets_widget *widget, xxwidgets_rect rect);
/* Current rect in cells. For a window this follows user resizes, which are
 * reported as XXWIDGETS_EVENT_RESIZE. */
XXWIDGETS_API xxwidgets_status xxwidgets_widget_get_rect(const xxwidgets_widget *widget, xxwidgets_rect *rect);
/* Smallest client area, in cells, the user can resize a window to; for a
 * window that re-lays out its controls on RESIZE, pass its layout's minimum.
 * Without a call, GTK keeps a window at least as large as its controls'
 * rects. Backends whose windows have no such limit accept and ignore it. */
XXWIDGETS_API xxwidgets_status xxwidgets_window_set_minimum_size(xxwidgets_widget *window, int columns, int rows);
XXWIDGETS_API xxwidgets_status xxwidgets_widget_set_visible(xxwidgets_widget *widget, int visible);
XXWIDGETS_API xxwidgets_status xxwidgets_widget_set_enabled(xxwidgets_widget *widget, int enabled);
XXWIDGETS_API xxwidgets_status xxwidgets_widget_focus(xxwidgets_widget *widget);
/* 1 when the widget (or a part of it, such as a list row) has keyboard focus,
 * 0 when it has not, -1 when the backend cannot tell. */
XXWIDGETS_API int xxwidgets_widget_has_focus(const xxwidgets_widget *widget);
/* CHECKBOX: 0/1; PROGRESS: 0..100; list controls: -1 or visible row;
 * comboboxes: -1 or record index (checkbox cursor is separate from checks).
 * TREEVIEW: -1 or original node index; selecting a hidden node opens ancestors.
 * Programmatic changes do not emit user-input events. */
XXWIDGETS_API xxwidgets_status xxwidgets_widget_set_value(xxwidgets_widget *widget, int value);
XXWIDGETS_API xxwidgets_status xxwidgets_widget_get_value(xxwidgets_widget *widget, int *value);
/* Publish already prepared UTF-8 text on the UI thread. owner must be a WINDOW.
 * No input events are emitted. Invalid text is rejected before touching the
 * clipboard; UNAVAILABLE means the backend has no clipboard support. */
XXWIDGETS_API xxwidgets_status xxwidgets_clipboard_set_text(xxwidgets_widget *owner, const char *text);
/* UTF-8 clipboard text, including terminating NUL in required. Query with
 * NULL/0; short buffers are untouched. TUI uses the local Windows clipboard,
 * macOS pbpaste, or available Wayland/X11 clipboard helpers. Terminals without
 * an accessible desktop clipboard return UNAVAILABLE. */
XXWIDGETS_API xxwidgets_status xxwidgets_clipboard_get_text(xxwidgets_widget *owner, char *text, size_t capacity, size_t *required);
XXWIDGETS_API xxwidgets_status xxwidgets_listbox_add(xxwidgets_widget *widget, const char *text);
XXWIDGETS_API xxwidgets_status xxwidgets_listbox_clear(xxwidgets_widget *widget);
XXWIDGETS_API size_t xxwidgets_listbox_count(const xxwidgets_widget *widget);

/* Generic hierarchy, preserving sibling input order. parent is SIZE_MAX for a
 * root, otherwise an earlier node index. Text is copied valid UTF-8; expanded
 * must be 0 or 1. Display escapes control characters, metadata retains them. */
typedef struct xxwidgets_tree_node {
    size_t parent;
    const char *text;
    int expanded;
} xxwidgets_tree_node;

/* Transactional replacement selects the first root, or -1 when empty. All
 * setters emit no input events. NULL/count 0 clears. Returned strings remain
 * borrowed until replacement/clear/destruction. Expansion retains selection;
 * collapsing its ancestor selects that ancestor. Values and SELECT, CHANGE,
 * ACTIVATE event values refer to original input indexes, never visible rows.
 * User expansion emits CHANGE; Enter activates the selected node. */
XXWIDGETS_API xxwidgets_status xxwidgets_treeview_set_nodes(xxwidgets_widget *widget, const xxwidgets_tree_node *nodes, size_t count);
/* Per-node light/dark RGB pairs. UINT32_MAX retains the system color.
 * Native backends select the current appearance; TUI uses the dark palette. */
typedef struct xxwidgets_tree_color {
    uint32_t foreground, background, dark_foreground, dark_background;
} xxwidgets_tree_color;
/* NULL colors preserves the ordinary tree behavior. Copied transactionally. */
XXWIDGETS_API xxwidgets_status xxwidgets_treeview_set_colored_nodes(xxwidgets_widget *widget, const xxwidgets_tree_node *nodes,
                                                                  const xxwidgets_tree_color *colors, size_t count);
XXWIDGETS_API xxwidgets_tree_color xxwidgets_treeview_node_color(const xxwidgets_widget *widget, size_t index);
/* Small buttons at the right end of rows, like the S and ? columns of a DIE
 * result tree. labels holds 1..XXWIDGETS_TREE_MARKS_MAX short UTF-8 labels
 * without control characters, one column each; masks holds one bit set per
 * current node (count is the node count), bit k showing labels[k] there.
 * Copied. Replacing or clearing the nodes removes them, as do NULL labels.
 * Clicking one emits CLICK with value = node index and x = mark index; in a
 * focused tree, typing a label's first character does the same for the
 * selected node. TUI has no mouse, so there only the key works. */
#define XXWIDGETS_TREE_MARKS_MAX 4
XXWIDGETS_API xxwidgets_status xxwidgets_treeview_set_marks(xxwidgets_widget *widget, const char *const *labels, size_t label_count,
                                                          const unsigned int *masks, size_t count);
/* Marks shown on node index as a bit set; 0 when there are none. */
XXWIDGETS_API unsigned int xxwidgets_treeview_node_marks(const xxwidgets_widget *widget, size_t index);
/* Number of mark columns, and their labels (NULL past the end). */
XXWIDGETS_API size_t xxwidgets_treeview_mark_count(const xxwidgets_widget *widget);
XXWIDGETS_API const char *xxwidgets_treeview_mark_label(const xxwidgets_widget *widget, size_t mark);
XXWIDGETS_API xxwidgets_status xxwidgets_treeview_clear(xxwidgets_widget *widget);
XXWIDGETS_API size_t xxwidgets_treeview_count(const xxwidgets_widget *widget);
XXWIDGETS_API size_t xxwidgets_treeview_visible_count(const xxwidgets_widget *widget);
XXWIDGETS_API xxwidgets_status xxwidgets_treeview_get_node(const xxwidgets_widget *widget, size_t index, xxwidgets_tree_node *node);
/* No selection: SIZE_MAX and a zeroed node. Both outputs required. */
XXWIDGETS_API xxwidgets_status xxwidgets_treeview_get_selection(xxwidgets_widget *widget, size_t *index, xxwidgets_tree_node *node);
XXWIDGETS_API xxwidgets_status xxwidgets_treeview_set_expanded(xxwidgets_widget *widget, size_t index, int expanded);
XXWIDGETS_API xxwidgets_status xxwidgets_treeview_get_expanded(const xxwidgets_widget *widget, size_t index, int *expanded);

/* Read-only scan results in input order, with Type/Name/Version/Info columns.
 * No scan engine dependency is required. NULL fields mean empty strings. */
typedef struct xxwidgets_scan_result {
    const char *type;
    const char *name;
    const char *version;
    const char *info;
} xxwidgets_scan_result;

/* Copies valid UTF-8 strings. Replacement is transactional, selects the first
 * result (or -1 when empty), and emits no events. NULL/count 0 clears.
 * Display escapes control characters; returned metadata retains original text. */
XXWIDGETS_API xxwidgets_status xxwidgets_scanresults_set_results(xxwidgets_widget *widget, const xxwidgets_scan_result *results, size_t count);
XXWIDGETS_API xxwidgets_status xxwidgets_scanresults_clear(xxwidgets_widget *widget);
XXWIDGETS_API size_t xxwidgets_scanresults_count(const xxwidgets_widget *widget);
/* Returned strings are borrowed until replacement/clear/destruction.
 * SELECT event.value and widget set/get_value use the original result index. */
XXWIDGETS_API xxwidgets_status xxwidgets_scanresults_get_result(const xxwidgets_widget *widget, size_t index, xxwidgets_scan_result *result);
/* Both outputs required. No selection: SIZE_MAX and a zeroed result. */
XXWIDGETS_API xxwidgets_status xxwidgets_scanresults_get_selection(xxwidgets_widget *widget, size_t *index, xxwidgets_scan_result *result);
/* Tab-separated report with a header; controls are escaped. required includes
 * NUL. NULL/capacity 0 queries size; short output remains valid UTF-8. */
XXWIDGETS_API xxwidgets_status xxwidgets_scanresults_get_report(const xxwidgets_widget *widget, char *buffer, size_t capacity, size_t *required);
/* Copies the complete report to the native clipboard. TUI returns UNAVAILABLE. */
XXWIDGETS_API xxwidgets_status xxwidgets_scanresults_copy(xxwidgets_widget *widget);

/* Include xxwidgets/xxwidgets_combobox.h for the complete xxfclib types.
 * Both combo kinds accept an array of label/value records. Label length is in
 * wchar_t units; counted views need no trailing NUL. Labels and string/
 * wide-string/byte values are copied; opaque PTR values remain borrowed.
 * Replacement is transactional: normal combo selects the first record;
 * checkbox combo starts with no checks. NULL/count 0 clears either control. */
XXWIDGETS_API xxwidgets_status xxwidgets_combobox_set_records(xxwidgets_widget *widget, const xx_meta_string *records, size_t count);
XXWIDGETS_API size_t xxwidgets_combobox_count(const xxwidgets_widget *widget);
/* Returned record and dynamic values are read-only borrowed views, valid until
 * replacement/destruction. Do not free them. Scalar values are copied. */
XXWIDGETS_API xxwidgets_status xxwidgets_combobox_get_record(const xxwidgets_widget *widget, size_t index, const xx_meta_string **record);
/* Normal combo only. Returns a borrowed xx_var value (is_allocated == false),
 * or NONE for an empty/unselected control. Initialize/clean up an existing
 * output variant before calling: this method does not release its old value.
 * Native selection emits SELECT with the record index, after updating state. */
XXWIDGETS_API xxwidgets_status xxwidgets_combobox_get_current(xxwidgets_widget *widget, xx_var *value);
XXWIDGETS_API xxwidgets_status xxwidgets_checkcombobox_set_checked(xxwidgets_widget *widget, size_t index, int checked);
XXWIDGETS_API xxwidgets_status xxwidgets_checkcombobox_is_checked(const xxwidgets_widget *widget, size_t index, int *checked);
/* Checkbox combo only. Returns pointers to checked records in input order.
 * NULL/capacity 0 queries count; too-small buffers return BUFFER_TOO_SMALL
 * without partial output. User toggles emit CHANGE with the affected index;
 * programmatic changes do not emit events. Returned records are borrowed. */
XXWIDGETS_API xxwidgets_status xxwidgets_checkcombobox_get_checked(const xxwidgets_widget *widget, const xx_meta_string **records, size_t capacity, size_t *count);

typedef struct xxwidgets_option {
    const char *label; /* UTF-8 checkbox label. */
    int value;         /* 0 or 1. */
} xxwidgets_option;

/* Common modal options form with checkboxes, OK and Cancel. The owner is
 * disabled while open. Values change only on OK; Cancel/close leaves them
 * unchanged and reports accepted == 0. Call on the UI thread outside input
 * callbacks/polling; reentrant calls return BUSY. Supports 0..16 options. */
XXWIDGETS_API xxwidgets_status xxwidgets_options_dialog(xxwidgets_widget *owner, const char *title, xxwidgets_option *options, size_t count, int *accepted);

/* Include xxwidgets/xxwidgets_process.h for xxfclib's xx_pd_struct definition.
 * Called immediately, then between event polls (30 ms timeout). Keep it
 * short: advance bounded work, or copy a synchronized worker snapshot into
 * progress and report its explicit completion through finished (initially 0).
 * stop_requested is sticky; propagate it to the operation. This callback runs
 * on the UI thread, with the same reentrancy rules as an input callback. */
typedef xxwidgets_status (*xxwidgets_process_update_fn)(void *user_data, int stop_requested, xx_pd_struct *progress, int *finished);

/* Modal progress for all five xx_pd_record slots. A bar and its status/count
 * label follow is_busy: a change shows once it has lasted 200 ms (the first
 * view shows the busy slots at once), so a slot that flashes by does not
 * resize the dialog; an idle slot still shown keeps its last text.
 * Percentages are clamped; total == 0 displays zero percent with an
 * unknown-total count.
 * The dialog is created only if unfinished after MORE than 1,000 ms, measured
 * from entry with a monotonic clock. The owner is disabled throughout the wait
 * and restored on return. Cancel/Escape/close request is_stop, then keep waiting
 * until update reports finished; idle records alone never imply completion.
 * progress is a caller-owned UI snapshot, NOT a concurrently written worker
 * struct. The callback handles any worker synchronization; is_stop is retained
 * across snapshots. Start workers before calling; join them after returning.
 * UI/callback failures or app quit request stop and invoke update once more
 * before returning; the caller must still stop/join unfinished work. OK means
 * the UI succeeded: inspect progress.is_stop / last_error for the job outcome.
 * Call outside input callbacks/polling. No xxfclib runtime linkage is needed. */
XXWIDGETS_API xxwidgets_status xxwidgets_process_dialog(xxwidgets_widget *owner, const char *title, xx_pd_struct *progress, xxwidgets_process_update_fn update,
                                                        void *user_data);

/* Copies text into a status buffer of size bytes (an xx_pd_record status).
 * A longer text keeps its end, behind a leading '...' (U+2026), cut where a
 * whole character starts: not before a combining mark, not inside a '\xNN'
 * escape. The dialog fits its labels the same way, so the end of a long
 * path, which names the member, stays visible. A NULL text is empty; a
 * text too long for a buffer of fewer than 4 bytes (no room for the
 * ellipsis) gives ""; a NULL status or a size of 0 writes nothing. Safe on
 * any thread. */
XXWIDGETS_API void xxwidgets_process_status_tail(char *status, size_t size, const char *text);

typedef enum xxwidgets_about_text {
    XXWIDGETS_ABOUT_TITLE = 0,
    XXWIDGETS_ABOUT_PROGRAM_NAME,
    XXWIDGETS_ABOUT_VERSION,
    XXWIDGETS_ABOUT_DESCRIPTION,
    XXWIDGETS_ABOUT_COPYRIGHT,
    XXWIDGETS_ABOUT_WEBSITE,
    XXWIDGETS_ABOUT_LICENSE,
    XXWIDGETS_ABOUT_CREDITS,
    XXWIDGETS_ABOUT_CLOSE_LABEL,
    XXWIDGETS_ABOUT_BUILD_DATE,  /* Shown between Thanks and Check updates. */
    XXWIDGETS_ABOUT_THANKS_URL,  /* Opened by Thanks; empty hides the button. */
    XXWIDGETS_ABOUT_UPDATES_URL, /* Opened by Check updates; empty hides it. */
    XXWIDGETS_ABOUT_TEXT_COUNT
} xxwidgets_about_text;

/* Rich About content, in the Detect It Easy layout: the image on the left,
 * Info and Libraries tabs on the right. Native backends that support it show
 * that layout once any item is added; others append the items to the text.
 * text is a bold caption; value follows it and becomes a link when link is
 * set. Links starting with http://, https:// or mailto: open in the default
 * handler; any other link is copied to the clipboard. All strings are copied
 * UTF-8; value and link may be NULL. Up to 64 items. */
typedef enum xxwidgets_about_item_kind {
    XXWIDGETS_ABOUT_ITEM_SOCIAL = 0, /* "Follow me" button: text is its label, link its URL. */
    XXWIDGETS_ABOUT_ITEM_INFO,       /* Centered line on the Info tab. */
    XXWIDGETS_ABOUT_ITEM_LIBRARY     /* Centered line on the Libraries tab. */
} xxwidgets_about_item_kind;

/* Reusable About content, independent of an app/window. Text is copied UTF-8;
 * empty strings hide optional fields. Defaults: About / Application / Close.
 * The Close label must be nonempty. Setters are transactional.
 * Destroy/set while showing returns BUSY. */
XXWIDGETS_API xxwidgets_status xxwidgets_about_dialog_create(xxwidgets_about_dialog **out_dialog);
XXWIDGETS_API xxwidgets_status xxwidgets_about_dialog_destroy(xxwidgets_about_dialog *dialog);
XXWIDGETS_API xxwidgets_status xxwidgets_about_dialog_set_text(xxwidgets_about_dialog *dialog, xxwidgets_about_text field, const char *text);
/* Same buffer/required conventions as widget_get_text. */
XXWIDGETS_API xxwidgets_status xxwidgets_about_dialog_get_text(const xxwidgets_about_dialog *dialog, xxwidgets_about_text field, char *buffer, size_t capacity,
                                                               size_t *required);
/* Copies top-down, straight-alpha RGBA8 pixels; stride is bytes per input row.
 * Dimensions: 1..32767. NULL/0/0/0 clears the image. Native backends display it
 * with its aspect ratio; the terminal dialog presents text only. No OS image
 * handles, file paths, or decoder dependencies cross this API. */
XXWIDGETS_API xxwidgets_status xxwidgets_about_dialog_set_image(xxwidgets_about_dialog *dialog, const void *rgba, unsigned int width, unsigned int height, size_t stride);
/* Appends a rich item (see xxwidgets_about_item_kind). BUSY while showing. */
XXWIDGETS_API xxwidgets_status xxwidgets_about_dialog_add_item(xxwidgets_about_dialog *dialog, xxwidgets_about_item_kind kind, const char *text,
                                                               const char *value, const char *link);
XXWIDGETS_API xxwidgets_status xxwidgets_about_dialog_clear_items(xxwidgets_about_dialog *dialog);
XXWIDGETS_API size_t xxwidgets_about_dialog_item_count(const xxwidgets_about_dialog *dialog);
/* Modal, on the owner's UI thread, outside input callbacks/polling. The owner
 * is disabled while open, then restored. Content may be shown repeatedly with
 * different owners. Close/Enter/Escape dismiss; nested shows return BUSY. */
XXWIDGETS_API xxwidgets_status xxwidgets_about_dialog_show(xxwidgets_about_dialog *dialog, xxwidgets_widget *owner);

/* Scrollable, selectable UTF-8 text with Close and a Copy all button on native
 * desktops. Copy uses the system clipboard and reports success on the button.
 * Terminal backends show a scrolling text list with Close. Modal/reentrancy
 * rules are the same as about_dialog_show. Text is borrowed for this call. */
/* May be owned by the current modal window; restores that modal on close. */
XXWIDGETS_API xxwidgets_status xxwidgets_text_dialog(xxwidgets_widget *owner, const char *title, const char *text);

/* Opens an http(s) or mailto URL, already percent-encoded, with the desktop's
 * default handler and returns without waiting for it. owner is any widget of
 * the app. UNAVAILABLE when there is no handler, as in a terminal without a
 * desktop session; other schemes and unencoded characters are rejected. */
XXWIDGETS_API xxwidgets_status xxwidgets_open_url(xxwidgets_widget *owner, const char *url);

typedef enum xxwidgets_file_dialog_mode {
    XXWIDGETS_FILE_DIALOG_OPEN = 0, /* choose an existing file */
    XXWIDGETS_FILE_DIALOG_SAVE      /* choose a file name; replacing a file is confirmed */
} xxwidgets_file_dialog_mode;

/* Native file chooser. Modal/reentrancy rules are the same as
 * about_dialog_show. title is UTF-8 and may be NULL. initial may be NULL or
 * empty; a file preselects it, a folder (or a missing file's folder) is where
 * the chooser starts. On acceptance *path receives a malloc'd file path the
 * caller frees and *accepted is 1. Cancellation is successful with
 * accepted == 0 and *path NULL. UNAVAILABLE when the backend has no native
 * chooser; xxwidgets_file_dialog_available reports that in advance. */
XXWIDGETS_API xxwidgets_status xxwidgets_file_dialog(xxwidgets_widget *owner, xxwidgets_file_dialog_mode mode, const char *title, const char *initial, char **path,
                                                     int *accepted);
XXWIDGETS_API int xxwidgets_file_dialog_available(const xxwidgets_app *app);

/* HexView: 64-bit addresses, hex bytes and printable ASCII. Data is copied and
 * rendered as selectable rows. It is read-only by default. Defaults: base 0,
 * 16 bytes per row. set_data resets selection and the byte cursor to the first
 * byte/high nibble, or -1 and SIZE_MAX/0 for empty data. Data, layout, editing
 * mode and cursor changes are transactional and do not emit input events.
 * Replacing copied data detaches an attached IO device only on success;
 * paging callbacks retain their existing source. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_data(xxwidgets_widget *widget, const void *data, size_t size);
/* Attach a live window on a seekable xx_io_device. initial_offset is
 * an absolute device address and window_size must be 64..1048576 bytes. The
 * initial window begins at initial_offset; only that bounded window is copied.
 * A known total size supplies source boundaries (an empty device is valid);
 * otherwise UINT64_MAX is the logical exclusive upper bound. Process devices
 * use uint64_t direct reads; ordinary devices must support read/seek/tell and
 * signed xx_io_read_at offsets. Reads preserve the device's cursor and respect
 * its buffering. Unreadable or partial windows fail without manufacturing
 * bytes or changing the previous view. Attaching to an editable view is invalid.
 *
 * On successful attachment take_ownership transfers closing responsibility to
 * the widget. Failure leaves ownership with the caller and the old attachment
 * intact. Borrowed devices must outlive their attachment. Reattaching the same
 * device changes its ownership according to the new flag without closing it.
 * Replacement, detachment and destruction close owned devices. Detach using
 * NULL/0/0; copied bytes remain as a snapshot. set_source also replaces an IO
 * attachment. These synchronous calls belong to the UI thread and emit no
 * input events. Failed device reads during user navigation/context actions
 * retain the view and emit READ_ERROR instead of ending the application's
 * input loop. Backend synchronization and allocation errors still propagate.
 * The live source supports source-wide selection/copy/dump and paging.
 * Nibble editing remains unavailable while attached. Explicit byte insertion
 * and deletion below are available for resizable read/write devices. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_device(xxwidgets_widget *, xx_io_device *, uint64_t initial_offset,
                                                          size_t window_size, int take_ownership);
/* Seek loads a bounded window starting at offset and selects its first byte.
 * Refresh reloads the displayed window, retaining selection when still valid.
 * Offset at a known EOF loads an empty window. Failures preserve the old view. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_seek_device(xxwidgets_widget *, uint64_t offset);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_refresh_device(xxwidgets_widget *);
/* Borrowed device pointer, or NULL for another kind or an unattached view. */
XXWIDGETS_API xx_io_device *xxwidgets_hexview_get_device(const xxwidgets_widget *);
/* Structural editing uses absolute device offsets. Available only for an
 * attached, readable/writable, seekable device with a resize callback and a
 * known size. Append-only files, fixed memory and callback-only sources are
 * unavailable. Capability is checked again for each operation.
 *
 * Insert accepts [0,size], including an empty file or EOF; delete requires
 * [offset,offset+length) within the file. Zero lengths are no-ops. The device
 * cursor is preserved; the view reloads and selects inserted bytes, or the
 * nearest remaining byte/whole symbol after deletion. These synchronous APIs
 * belong to the UI thread and emit no events. The affected suffix is backed
 * up to a temporary device and transferred in bounded chunks. Failures attempt
 * to restore the original bytes and view; persistent device failures can also
 * prevent recovery. Keep exclusive access to the device during an operation.
 * User context actions run the storage work on a worker with cancellable
 * progress, then emit CHANGE and SELECT only after success. Delete/Backspace
 * delete the selection; application-owned input handlers retain precedence. */
XXWIDGETS_API int xxwidgets_hexview_can_resize(const xxwidgets_widget *);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_insert_bytes(xxwidgets_widget *, uint64_t offset, const void *data, size_t length);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_delete_bytes(xxwidgets_widget *, uint64_t offset, uint64_t length);
/* Cut a nonempty selection to the clipboard and delete exactly those bytes.
 * Available only in Elements/Symbols for attached devices supporting resizing;
 * copied data, fixed-size/read-only devices and callback-only sources cannot cut.
 * Elements copies complete hexadecimal byte pairs; Symbols copies UTF-8 text
 * decoded with the selected codepage, with the same control/invalid-character
 * substitution as ordinary copy. Clipboard failure or a source/view change
 * during copying leaves the bytes and history unchanged. Cut calls the backend
 * clipboard directly: the application's possibly asynchronous copy handler is
 * reserved for ordinary copy. One cut is one existing deletion history entry;
 * undo/redo never change the clipboard. This UI-thread API is synchronous,
 * preserves the device cursor and emits no events. Ctrl+X/context actions use
 * cancellable deletion workers in GUI and TUI, then emit CHANGE/SELECT. */
XXWIDGETS_API int xxwidgets_hexview_can_cut(const xxwidgets_widget *);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_cut_selection(xxwidgets_widget *);
/* Replace existing bytes without changing size. Offsets are absolute device
 * addresses, or layout base plus the byte index for copied data. Supported for
 * editable copied data and known-size, seekable read/write devices; no resize
 * callback is required. Read-only devices and source callbacks are unavailable.
 * The whole range must already exist. Zero-length and identical-byte writes
 * do not add history or discard redo. Calls are synchronous, emit no events,
 * preserve the device cursor and select the overwritten byte range. */
XXWIDGETS_API int xxwidgets_hexview_can_overwrite(const xxwidgets_widget *);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_overwrite_bytes(xxwidgets_widget *, uint64_t offset, const void *data, size_t length);
/* Paste UTF-8 clipboard text at the selection start, or the cursor when no
 * bytes are selected. Elements accepts complete hexadecimal byte pairs,
 * adjacent or separated by ASCII whitespace (AA BB00); Symbols strictly
 * encodes the text using the selected codepage, including UTF-8/16/32.
 * Address column input is unavailable. Invalid, unrepresentable, or greater
 * than 16 MiB text is rejected before editing. The complete byte range must
 * already exist; paste overwrites without resizing or truncating. One changed
 * paste is one shared undo/redo command. Empty/identical input retains redo.
 * Supported targets and other guarantees match overwrite_bytes. This API
 * does not read the clipboard, emits no events, and runs synchronously.
 * Ctrl+V and the context menu read the clipboard in both GUI and TUI. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_paste_text(xxwidgets_widget *, const char *text);
/* Insert clipboard text before the selection start, or the cursor when no
 * bytes are selected, without removing the selection's existing bytes. An
 * empty device or a window at EOF accepts insertion at EOF. Input decoding
 * and the 16 MiB text limit match paste_text. Available only in Elements or
 * Symbols for an attached device supporting resizing; copied data, fixed-size
 * or read-only devices and callback-only sources are unavailable. Invalid or
 * empty text does not change bytes or history. One insertion is one existing
 * xxfclib history command, restoring the selection on undo/redo. This API
 * does not read the clipboard, emits no events and runs synchronously.
 * Ctrl+Shift+V/context actions use cancellable insertion workers in GUI and
 * TUI. Ctrl+V continues to overwrite existing bytes without resizing. */
XXWIDGETS_API int xxwidgets_hexview_can_paste_insert(const xxwidgets_widget *);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_paste_insert_text(xxwidgets_widget *, const char *text);
/* Insertion/deletion/overwrite history using xxfclib's command history. The
 * last 128 successful byte changes are retained. Device payloads use temporary
 * streams; copied nibble changes keep only the old/new byte. Undo/redo restore the
 * corresponding byte selection and preserve the device cursor. New successful
 * edits discard the redo branch; failed/cancelled edits and replay retain it.
 * Capabilities follow the next command: insert/delete require resizing,
 * overwrite requires writing, and copied edits can replay after editing is
 * disabled. EMPTY/unsupported replay
 * returns UNAVAILABLE; programmatic calls are synchronous and emit no events.
 * Ctrl+Z/Ctrl+Y and the context menu use cancellable workers for device edits
 * in GUI and TUI, then emit CHANGE/SELECT on success. Copied edits replay
 * synchronously. Paging/display changes retain history;
 * successful attachment, detachment, set_data/set_source, or explicit device
 * refresh clears it. External writers must clear history or refresh before
 * further editing; replay checks device identity, size and removed bytes but
 * cannot detect every external change. clear_history changes no file bytes. */
XXWIDGETS_API int xxwidgets_hexview_can_undo(const xxwidgets_widget *);
XXWIDGETS_API int xxwidgets_hexview_can_redo(const xxwidgets_widget *);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_undo(xxwidgets_widget *);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_redo(xxwidgets_widget *);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_clear_history(xxwidgets_widget *);
/* Copied absolute byte ranges. Highest priority wins; equal priority uses the
 * last supplied range. Selection colors take precedence; symbol continuation
 * markers retain disabled foreground colors. Zero lengths are ignored. */
typedef struct xxwidgets_hexview_highlight {
    uint64_t offset, length;
    uint32_t color; /* 0xRRGGBB background */
    int priority;
} xxwidgets_hexview_highlight;
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_highlights(xxwidgets_widget *, const xxwidgets_hexview_highlight *, size_t count);
/* Returns 1 when a range covers the absolute byte, zero otherwise. */
XXWIDGETS_API int xxwidgets_hexview_highlight_at(const xxwidgets_widget *, uint64_t absolute_offset, uint32_t *color);
/* Copied absolute ranges for the native full-file overview strip, independent
 * of inline byte highlights. Zero length marks a single file position. Bands
 * remain at least two pixels high when space permits; highest priority wins,
 * with the last supplied marker breaking ties. Clicking the strip selects the
 * corresponding byte (or complete symbol in Symbols), paging the source when
 * needed and emitting SELECT. Empty sources have an inactive strip. Copied
 * views map their base through their final byte. TUI retains markers without
 * reserving terminal space. Invalid colors/overflow and backend failures leave
 * the previous markers intact. Passing NULL/0 clears the strip. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_overview_markers(xxwidgets_widget *, const xxwidgets_hexview_highlight *, size_t count);
/* Optional read-only, windowed source. Source offsets are absolute and the
 * displayed window starts at the layout base. read must fill exactly length
 * bytes; seek must load a window containing the requested byte and its whole
 * encoded symbol using the ordinary data/layout setters. Callbacks and user
 * data are borrowed. Passing NULL callbacks and size 0 detaches the source.
 * Attaching to an editable view or enabling editing while attached is invalid.
 * Control/Meta+Home/End in Symbols use source boundaries. Selection can span
 * windows; navigation and saving use bounded windows/chunks. Copying allocates
 * the selected bytes and their clipboard representation, with overflow checks. */
typedef xxwidgets_status (*xxwidgets_hexview_source_read_fn)(void *user_data, uint64_t offset, void *data, size_t length);
typedef xxwidgets_status (*xxwidgets_hexview_source_seek_fn)(xxwidgets_widget *widget, uint64_t offset, void *user_data);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_source(xxwidgets_widget *, uint64_t size, xxwidgets_hexview_source_read_fn,
                                                         xxwidgets_hexview_source_seek_fn, void *user_data);
/* Absolute source range, or base plus the ordinary selection without a source.
 * Empty selection returns UINT64_MAX/0. get_selection remains window-relative
 * and returns only the visible intersection of a source selection. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_get_source_selection(const xxwidgets_widget *, uint64_t *offset, uint64_t *length);
/* bytes_per_row must be 8, 16, 24, 32, 48 or 64. The final displayed address must fit uint64_t. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_layout(xxwidgets_widget *widget, uint64_t base_address, unsigned int bytes_per_row);
/* Three independently resizable columns: location (0), elements (1), symbols (2).
 * Symbols start in the first cell for their encoded bytes; continuation cells
 * show disabled-color dots. UTF-8 scalars occupy one to four cells, UTF-16 code
 * units two (surrogate pairs four), and UTF-32 scalars four. Unicode symbols
 * crossing rows retain the same full byte range on their continuation row.
 * Clicking or dragging either a symbol or a continuation cell selects
 * their complete encoded byte ranges. Hex column input remains byte-based.
 * In the active Symbols column, Left/Right move by decoded symbol and Shift
 * extends by whole symbols. Up/Down preserve a preferred byte column across
 * rows and land on complete symbols; Shift also extends to complete symbols.
 * PageUp/PageDown do the same across one visible page of rows.
 * Home/End select the complete first/last symbol in the row; Shift extends
 * through the complete endpoint. Control/Meta use the buffer boundaries, or
 * the complete file boundaries when a windowed source is attached.
 * Control/Meta+A selects all symbols and bytes in the buffer or attached file.
 * Control/Meta+C copies decoded text in Symbols and hex bytes in Elements.
 * Clicking a column or pressing F6 activates it.
 * Widths are character cells; zero restores automatic sizing. */
typedef enum xxwidgets_hex_mode {
    XXWIDGETS_HEX_HEX, XXWIDGETS_HEX_BYTE, XXWIDGETS_HEX_WORD, XXWIDGETS_HEX_DWORD, XXWIDGETS_HEX_QWORD,
    XXWIDGETS_HEX_UINT8, XXWIDGETS_HEX_INT8, XXWIDGETS_HEX_UINT16, XXWIDGETS_HEX_INT16,
    XXWIDGETS_HEX_UINT32, XXWIDGETS_HEX_INT32, XXWIDGETS_HEX_UINT64, XXWIDGETS_HEX_INT64
} xxwidgets_hex_mode;
typedef struct xxwidgets_hex_display {
    unsigned int radix; /* 10 or 16. */
    int offsets; /* 1: file offsets, 0: base address + offset. */
    xxwidgets_hex_mode mode; /* Multi-byte numbers use little-endian byte order. */
    const char *encoding; /* Name from xxfclib codepage registry; copied by setter. */
} xxwidgets_hex_display;
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_display(xxwidgets_widget *, const xxwidgets_hex_display *);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_get_display(const xxwidgets_widget *, xxwidgets_hex_display *);
/* Emphasize non-zero display units in the numeric column. enabled is 0 or 1.
 * This independent visual option leaves encoding, selection and editing intact. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_bold_nonzero(xxwidgets_widget *, int enabled);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_get_bold_nonzero(const xxwidgets_widget *, int *enabled);
/* Active keyboard column: Address (0), Elements (1), or Symbols (2). */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_get_active_column(const xxwidgets_widget *, unsigned int *column);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_active_column(xxwidgets_widget *, unsigned int column);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_column_width(xxwidgets_widget *, size_t column, size_t cells);
XXWIDGETS_API size_t xxwidgets_hexview_column_width(const xxwidgets_widget *, size_t column);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_get_layout(const xxwidgets_widget *, uint64_t *base_address, unsigned int *bytes_per_row);
XXWIDGETS_API size_t xxwidgets_hexview_size(const xxwidgets_widget *widget);
/* Copies the current bytes. required is mandatory and receives the raw byte
 * count (there is no terminator). NULL/capacity 0 queries the count. A short
 * buffer returns BUFFER_TOO_SMALL without copying a partial result. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_get_data(const xxwidgets_widget *widget, void *data, size_t capacity, size_t *required);
/* Editable input overwrites fixed-size copied data one hexadecimal nibble at a time.
 * It never inserts or removes bytes. User edits emit CHANGE with the edited
 * row in event.value; the copied data can then be obtained with get_data.
 * Actual byte changes join the same xxfclib history as explicit overwrites;
 * unchanged nibble input only advances the caret. Programmatic changes do not
 * emit events. Any nonzero value enables editing. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_editable(xxwidgets_widget *widget, int editable);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_get_editable(const xxwidgets_widget *widget, int *editable);
/* Application-owned editing for a windowed source. The view stays read-only;
 * printable scalar/nibble input, Delete/Backspace and Ctrl/Meta+V without Shift are delivered
 * with absolute cursor/selection coordinates. Queue work in this handler and
 * refresh source bytes after polling. No bytes/cursor change automatically.
 * Ctrl/Meta+Shift+V uses built-in insertion only for resizable devices.
 * A NULL handler disables application-owned input. */
typedef enum xxwidgets_hex_input_kind { XXWIDGETS_HEX_INPUT_NIBBLE,
    XXWIDGETS_HEX_INPUT_SCALAR, XXWIDGETS_HEX_INPUT_PASTE,
    XXWIDGETS_HEX_INPUT_DELETE, XXWIDGETS_HEX_INPUT_BACKSPACE } xxwidgets_hex_input_kind;
typedef struct xxwidgets_hex_input {
    xxwidgets_hex_input_kind kind;
    uint64_t offset, selection_offset, selection_length;
    unsigned int column, nibble;
    uint32_t value; /* 0..15 nibble or Unicode scalar. */
} xxwidgets_hex_input;
typedef xxwidgets_status (*xxwidgets_hexview_input_fn)(xxwidgets_widget *, const xxwidgets_hex_input *, void *);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_input_handler(xxwidgets_widget *, xxwidgets_hexview_input_fn, void *);

/* The byte cursor is a zero-based offset and nibble is 0 (high) or 1 (low).
 * SIZE_MAX/0 clears it and is also returned for an empty view/no selection.
 * Setting a byte cursor selects that byte. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_cursor(xxwidgets_widget *widget, size_t offset, unsigned int nibble);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_get_cursor(const xxwidgets_widget *widget, size_t *offset, unsigned int *nibble);
/* Selects the complete symbol containing a zero-based byte offset in the
 * current buffer, using the selected encoding. The cursor lands on its first
 * byte with nibble 0. Invalid/truncated bytes select their single-byte dot.
 * An offset outside the buffer (including SIZE_MAX) is invalid. A windowed
 * source must load the target and its symbol context before calling this
 * setter. The absolute source selection is updated; no events are emitted. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_symbol_cursor(xxwidgets_widget *widget, size_t offset);
/* Byte range [offset, offset + length). A zero length clears the selection;
 * get_selection returns SIZE_MAX/0 when empty. Setters do not emit events.
 * Mouse/keyboard selection emits SELECT, including moves within the same row. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_selection(xxwidgets_widget *, size_t offset, size_t length);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_get_selection(xxwidgets_widget *, size_t *offset, size_t *length);
/* Select an absolute byte range in an attached file source. The source seeks
 * to its first byte if necessary; only the visible intersection is rendered.
 * Whole-symbol callers must normalize their endpoints before calling. Zero
 * length clears the selection. No input events are emitted. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_source_selection(xxwidgets_widget *, uint64_t offset, uint64_t length);
/* As above, with an independent absolute cursor for restoring a saved view. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_source_range(xxwidgets_widget *, uint64_t cursor, uint64_t offset, uint64_t length);
typedef enum xxwidgets_hex_copy_format {
    XXWIDGETS_HEX_COPY_HEX, XXWIDGETS_HEX_COPY_TEXT, XXWIDGETS_HEX_COPY_C_ARRAY, XXWIDGETS_HEX_COPY_OFFSET
} xxwidgets_hex_copy_format;
/* Optional application override for a nonempty selection. Called synchronously
 * before source reads or clipboard allocation, including for COPY_OFFSET.
 * Input-triggered handlers should queue work and return; start modal dialogs
 * after returning from callbacks/polling. Handler/user_data are borrowed.
 * Use clipboard_set_text to publish prepared text without invoking the handler
 * recursively. A handler error leaves the clipboard untouched by xxwidgets. */
typedef xxwidgets_status (*xxwidgets_hexview_copy_fn)(xxwidgets_widget *widget, xxwidgets_hex_copy_format format, void *user_data);
/* NULL removes the override and clears its borrowed user_data. Default copying
 * remains synchronous for controls without a handler. Emits no input events. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_copy_handler(xxwidgets_widget *widget, xxwidgets_hexview_copy_fn handler, void *user_data);
/* Optional override for the nonempty-selection Dump action, including Ctrl+D
 * and its context menu. Called before a chooser, source read or file write.
 * Queue work and return from input callbacks; start dialogs/workers after the
 * input poll. The application can snapshot absolute selection with
 * get_source_selection. Handler/user_data are borrowed. Errors do not fall
 * back to the synchronous built-in dump. NULL restores that default. Setting
 * a handler clears an already deferred built-in dump and emits no events. */
typedef xxwidgets_status (*xxwidgets_hexview_dump_fn)(xxwidgets_widget *widget, void *user_data);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_dump_handler(xxwidgets_widget *widget,
    xxwidgets_hexview_dump_fn handler, void *user_data);
/* Copies the selected range. Text uses the selected codepage, replacing invalid
 * sequences/control characters with dots. POSIX TUI uses OSC 52 in supporting
 * terminals; Windows TUI uses the system clipboard. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_copy_selection(xxwidgets_widget *, xxwidgets_hex_copy_format);
/* Writes exactly the selected bytes. path is UTF-8; an existing file is replaced. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_save_selection(xxwidgets_widget *, const char *path);

/* Read-only archive contents with copied UTF-8 paths, 64-bit uncompressed
 * sizes and directory markers. Entry order is preserved; sizes are in bytes.
 * is_directory must be 0 or 1. No archive format dependency is required. */
typedef struct xxwidgets_archive_entry {
    const char *path;
    uint64_t size;
    int is_directory;
} xxwidgets_archive_entry;

/* Transactional bulk replacement; NULL with count 0 clears the view.
 * Selects the first entry, or -1 when empty. Does not emit input events.
 * Paths must be nonempty valid UTF-8. Control characters are escaped for
 * display while the original path is retained in the entry metadata. */
XXWIDGETS_API xxwidgets_status xxwidgets_archiveview_set_entries(xxwidgets_widget *widget, const xxwidgets_archive_entry *entries, size_t count);
XXWIDGETS_API xxwidgets_status xxwidgets_archiveview_clear(xxwidgets_widget *widget);
XXWIDGETS_API size_t xxwidgets_archiveview_count(const xxwidgets_widget *widget);
/* Returned paths belong to the widget until replacement/clear/destruction.
 * SELECT event.value and widget set/get_value use the original entry index. */
XXWIDGETS_API xxwidgets_status xxwidgets_archiveview_get_entry(const xxwidgets_widget *widget, size_t index, xxwidgets_archive_entry *entry);
/* No selection: index SIZE_MAX and entry {NULL, 0, 0}. Both outputs required. */
XXWIDGETS_API xxwidgets_status xxwidgets_archiveview_get_selection(xxwidgets_widget *widget, size_t *index, xxwidgets_archive_entry *entry);

/* ArchiveBrowser is a read-only file-manager view with an address bar,
 * current-folder contents and sortable metadata columns. It has no archive
 * format dependency. Flat member paths are converted into a folder hierarchy;
 * missing directory members are inferred. All strings are copied. */
typedef struct xxwidgets_archive_property {
    const char *name;
    const char *value;
} xxwidgets_archive_property;

typedef struct xxwidgets_archive_browser_entry {
    const char *path;
    uint64_t size;
    uint64_t packed_size;
    int is_directory;
    unsigned int flags;
    const char *modified;
    const char *attributes;
    const xxwidgets_archive_property *properties;
    size_t property_count;
} xxwidgets_archive_browser_entry;

enum xxwidgets_archive_entry_flags {
    XXWIDGETS_ARCHIVE_SIZE_KNOWN = 1u,
    XXWIDGETS_ARCHIVE_PACKED_SIZE_KNOWN = 2u
};

typedef enum xxwidgets_archive_column {
    XXWIDGETS_ARCHIVE_COLUMN_NAME = 0,
    XXWIDGETS_ARCHIVE_COLUMN_SIZE,
    XXWIDGETS_ARCHIVE_COLUMN_PACKED_SIZE,
    XXWIDGETS_ARCHIVE_COLUMN_MODIFIED,
    XXWIDGETS_ARCHIVE_COLUMN_ATTRIBUTES
} xxwidgets_archive_column;

/* Replaces all members and returns to the root directory. NULL/count 0 clears.
 * Paths must be nonempty valid UTF-8. Display treats '/' and '\\' as separators,
 * collapses repeated separators and '.' components, and rejects '..'. Other
 * control characters are escaped in displayed rows. Original member paths are
 * retained in returned metadata. NULL modified/attributes mean unknown.
 * Setters are transactional and do not emit user-input events. */
XXWIDGETS_API xxwidgets_status xxwidgets_archivebrowser_set_entries(xxwidgets_widget *widget, const xxwidgets_archive_browser_entry *entries, size_t count);
/* Advanced mode adds one column per distinct property name, in supplied order.
 * Properties are copied with entries; absent values/inferred folders stay blank.
 * Toggling preserves the directory and selection. Default is off. */
/* Archive is an optional UTF-8 display label/path; no file is opened. */
XXWIDGETS_API xxwidgets_status xxwidgets_archivebrowser_set_archive(xxwidgets_widget *widget, const char *archive);
XXWIDGETS_API const char *xxwidgets_archivebrowser_archive(const xxwidgets_widget *widget);
/* directory is archive-relative. Empty means root. Only existing explicit or
 * inferred directories can be selected. The getter returns a normalized path
 * with '/' separators and a trailing '/' for non-root directories. */
XXWIDGETS_API xxwidgets_status xxwidgets_archivebrowser_set_directory(xxwidgets_widget *widget, const char *directory);
XXWIDGETS_API const char *xxwidgets_archivebrowser_directory(const xxwidgets_widget *widget);
XXWIDGETS_API xxwidgets_status xxwidgets_archivebrowser_up(xxwidgets_widget *widget);
/* Total supplied members and current visible rows, respectively. */
XXWIDGETS_API size_t xxwidgets_archivebrowser_count(const xxwidgets_widget *widget);
XXWIDGETS_API size_t xxwidgets_archivebrowser_visible_count(const xxwidgets_widget *widget);
/* source_index refers to the original supplied array, or SIZE_MAX for an
 * inferred directory. Borrowed strings remain valid until the next successful
 * browser setter/navigation/sort or widget destruction. All outputs required.
 * Widget values and SELECT/ACTIVATE event values use visible row indexes.
 * User activation enters directories (CHANGE) or activates files (ACTIVATE).
 * For CHANGE, event.value is the selected row in the new directory, or -1. */
XXWIDGETS_API xxwidgets_status xxwidgets_archivebrowser_get_entry(const xxwidgets_widget *widget, size_t row, size_t *source_index,
                                                                  xxwidgets_archive_browser_entry *entry);
/* No selection: source_index SIZE_MAX and a zero-initialized entry. */
XXWIDGETS_API xxwidgets_status xxwidgets_archivebrowser_get_selection(xxwidgets_widget *widget, size_t *source_index, xxwidgets_archive_browser_entry *entry);
/* Multiple selection uses current visible row indexes. set_value retains its
 * single-selection behavior. These setters do not emit input events.
 * Selection survives sorting, column changes and archive-label changes;
 * navigating or replacing entries selects the first row in the new view. */
XXWIDGETS_API xxwidgets_status xxwidgets_archivebrowser_set_selection(xxwidgets_widget *widget, const size_t *rows, size_t count);
XXWIDGETS_API size_t xxwidgets_archivebrowser_selection_count(const xxwidgets_widget *widget);
/* Returns original source indexes in ascending order, once each. Selected
 * folders include all descendants, including inferred folders. No selection
 * returns zero indexes. NULL/capacity 0 queries the required count. */
XXWIDGETS_API xxwidgets_status xxwidgets_archivebrowser_selected_sources(const xxwidgets_widget *widget, size_t *indexes, size_t capacity, size_t *required);
/* Directories always precede files; unknown sizes sort after known sizes.
 * Name comparison is deterministic, ASCII case-insensitive UTF-8 byte order. */
XXWIDGETS_API xxwidgets_status xxwidgets_archivebrowser_sort(xxwidgets_widget *widget, xxwidgets_archive_column column, int descending);

/* Advanced mode adds a column for each distinct property name in the supplied
 * entries. Properties are copied, unknown values stay blank, and inferred
 * folders have no record properties. Extra columns sort by their display text.
 * Toggling retains the current directory and selected member. */
XXWIDGETS_API xxwidgets_status xxwidgets_archivebrowser_set_advanced(xxwidgets_widget *widget, int advanced);
XXWIDGETS_API size_t xxwidgets_archivebrowser_column_count(const xxwidgets_widget *widget);
XXWIDGETS_API const char *xxwidgets_archivebrowser_column_title(const xxwidgets_widget *widget, size_t column);
XXWIDGETS_API const char *xxwidgets_archivebrowser_cell(const xxwidgets_widget *widget, size_t row, xxwidgets_archive_column column);

XXWIDGETS_API void xxwidgets_app_set_create_hidden(xxwidgets_app* app, int hidden);

#ifdef __cplusplus
}
#endif
#endif
