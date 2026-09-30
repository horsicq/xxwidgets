#ifndef XXWIDGETS_XXWIDGETS_H
#define XXWIDGETS_XXWIDGETS_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && !defined(XXWIDGETS_STATIC)
# if defined(XXWIDGETS_BUILDING_LIBRARY)
#  define XXWIDGETS_API __declspec(dllexport)
# else
#  define XXWIDGETS_API __declspec(dllimport)
# endif
#else
# define XXWIDGETS_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_app xxwidgets_app;
typedef struct xxwidgets_widget xxwidgets_widget;
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
    XXWIDGETS_TREEVIEW
} xxwidgets_kind;

typedef enum xxwidgets_event_type {
    XXWIDGETS_EVENT_CLOSE = 0,
    XXWIDGETS_EVENT_CLICK,
    XXWIDGETS_EVENT_CHANGE,
    XXWIDGETS_EVENT_SELECT,
    XXWIDGETS_EVENT_RESIZE,
    XXWIDGETS_EVENT_ACTIVATE,
    XXWIDGETS_EVENT_CONTEXT_MENU,
    XXWIDGETS_EVENT_SHORTCUT
} xxwidgets_event_type;

/* Logical text-cell units: native controls scale these to font-sized pixels.
 * A window's width/height describe its client area. Child coordinates are
 * relative to that area; windows cannot be nested. */
typedef struct xxwidgets_rect { int x, y, width, height; } xxwidgets_rect;

typedef struct xxwidgets_event {
    xxwidgets_event_type type;
    xxwidgets_widget *widget;
    int value; /* Control value/row, or the installed action ID for SHORTCUT. */
    /* CONTEXT_MENU only: popup anchor relative to the widget. Native backends
     * use container pixels; TUI uses text cells. Other types use 0, 0.
     * ArchiveBrowser context value is the selected visible row, or -1. */
    int x, y;
} xxwidgets_event;

typedef void (*xxwidgets_event_fn)(xxwidgets_app *app,
                                  const xxwidgets_event *event, void *user_data);

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
XXWIDGETS_API xxwidgets_status xxwidgets_window_set_shortcuts(xxwidgets_widget *window,
    const xxwidgets_shortcut *shortcuts, size_t count);
XXWIDGETS_API size_t xxwidgets_window_shortcut_count(const xxwidgets_widget *window);

/* UTF-8 text is copied. A control must have a WINDOW parent in the same app.
 * Widgets start visible and enabled. Destroying a window destroys its children. */
XXWIDGETS_API xxwidgets_status xxwidgets_widget_create(xxwidgets_app *app, xxwidgets_widget *parent,
    xxwidgets_kind kind, const char *text, xxwidgets_rect rect, xxwidgets_widget **out_widget);
XXWIDGETS_API xxwidgets_status xxwidgets_widget_destroy(xxwidgets_widget *widget);
XXWIDGETS_API xxwidgets_kind xxwidgets_widget_kind(const xxwidgets_widget *widget);
XXWIDGETS_API void *xxwidgets_widget_native_handle(const xxwidgets_widget *widget);
XXWIDGETS_API void xxwidgets_widget_set_user_data(xxwidgets_widget *widget, void *user_data);
XXWIDGETS_API void *xxwidgets_widget_user_data(const xxwidgets_widget *widget);
XXWIDGETS_API xxwidgets_status xxwidgets_widget_set_text(xxwidgets_widget *widget, const char *text);
/* required includes the NUL byte. A NULL buffer with capacity 0 queries size.
 * Too-small buffers are NUL-terminated when capacity > 0. */
XXWIDGETS_API xxwidgets_status xxwidgets_widget_get_text(xxwidgets_widget *widget,
    char *buffer, size_t capacity, size_t *required);
XXWIDGETS_API xxwidgets_status xxwidgets_widget_set_rect(xxwidgets_widget *widget, xxwidgets_rect rect);
XXWIDGETS_API xxwidgets_status xxwidgets_widget_set_visible(xxwidgets_widget *widget, int visible);
XXWIDGETS_API xxwidgets_status xxwidgets_widget_set_enabled(xxwidgets_widget *widget, int enabled);
XXWIDGETS_API xxwidgets_status xxwidgets_widget_focus(xxwidgets_widget *widget);
/* CHECKBOX: 0/1; PROGRESS: 0..100; list controls: -1 or visible row;
 * comboboxes: -1 or record index (checkbox cursor is separate from checks).
 * TREEVIEW: -1 or original node index; selecting a hidden node opens ancestors.
 * Programmatic changes do not emit user-input events. */
XXWIDGETS_API xxwidgets_status xxwidgets_widget_set_value(xxwidgets_widget *widget, int value);
XXWIDGETS_API xxwidgets_status xxwidgets_widget_get_value(xxwidgets_widget *widget, int *value);
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
XXWIDGETS_API xxwidgets_status xxwidgets_treeview_set_nodes(xxwidgets_widget *widget,
    const xxwidgets_tree_node *nodes, size_t count);
XXWIDGETS_API xxwidgets_status xxwidgets_treeview_clear(xxwidgets_widget *widget);
XXWIDGETS_API size_t xxwidgets_treeview_count(const xxwidgets_widget *widget);
XXWIDGETS_API size_t xxwidgets_treeview_visible_count(const xxwidgets_widget *widget);
XXWIDGETS_API xxwidgets_status xxwidgets_treeview_get_node(const xxwidgets_widget *widget,
    size_t index, xxwidgets_tree_node *node);
/* No selection: SIZE_MAX and a zeroed node. Both outputs required. */
XXWIDGETS_API xxwidgets_status xxwidgets_treeview_get_selection(xxwidgets_widget *widget,
    size_t *index, xxwidgets_tree_node *node);
XXWIDGETS_API xxwidgets_status xxwidgets_treeview_set_expanded(xxwidgets_widget *widget,
    size_t index, int expanded);
XXWIDGETS_API xxwidgets_status xxwidgets_treeview_get_expanded(const xxwidgets_widget *widget,
    size_t index, int *expanded);

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
XXWIDGETS_API xxwidgets_status xxwidgets_scanresults_set_results(xxwidgets_widget *widget,
    const xxwidgets_scan_result *results, size_t count);
XXWIDGETS_API xxwidgets_status xxwidgets_scanresults_clear(xxwidgets_widget *widget);
XXWIDGETS_API size_t xxwidgets_scanresults_count(const xxwidgets_widget *widget);
/* Returned strings are borrowed until replacement/clear/destruction.
 * SELECT event.value and widget set/get_value use the original result index. */
XXWIDGETS_API xxwidgets_status xxwidgets_scanresults_get_result(const xxwidgets_widget *widget,
    size_t index, xxwidgets_scan_result *result);
/* Both outputs required. No selection: SIZE_MAX and a zeroed result. */
XXWIDGETS_API xxwidgets_status xxwidgets_scanresults_get_selection(xxwidgets_widget *widget,
    size_t *index, xxwidgets_scan_result *result);
/* Tab-separated report with a header; controls are escaped. required includes
 * NUL. NULL/capacity 0 queries size; short output remains valid UTF-8. */
XXWIDGETS_API xxwidgets_status xxwidgets_scanresults_get_report(const xxwidgets_widget *widget,
    char *buffer, size_t capacity, size_t *required);
/* Copies the complete report to the native clipboard. TUI returns UNAVAILABLE. */
XXWIDGETS_API xxwidgets_status xxwidgets_scanresults_copy(xxwidgets_widget *widget);

/* Include xxwidgets/xxwidgets_combobox.h for the complete xxfclib types.
 * Both combo kinds accept an array of label/value records. Label length is in
 * wchar_t units; counted views need no trailing NUL. Labels and string/
 * wide-string/byte values are copied; opaque PTR values remain borrowed.
 * Replacement is transactional: normal combo selects the first record;
 * checkbox combo starts with no checks. NULL/count 0 clears either control. */
XXWIDGETS_API xxwidgets_status xxwidgets_combobox_set_records(xxwidgets_widget *widget,
    const xx_meta_string *records, size_t count);
XXWIDGETS_API size_t xxwidgets_combobox_count(const xxwidgets_widget *widget);
/* Returned record and dynamic values are read-only borrowed views, valid until
 * replacement/destruction. Do not free them. Scalar values are copied. */
XXWIDGETS_API xxwidgets_status xxwidgets_combobox_get_record(const xxwidgets_widget *widget,
    size_t index, const xx_meta_string **record);
/* Normal combo only. Returns a borrowed xx_var value (is_allocated == false),
 * or NONE for an empty/unselected control. Initialize/clean up an existing
 * output variant before calling: this method does not release its old value.
 * Native selection emits SELECT with the record index, after updating state. */
XXWIDGETS_API xxwidgets_status xxwidgets_combobox_get_current(xxwidgets_widget *widget, xx_var *value);
XXWIDGETS_API xxwidgets_status xxwidgets_checkcombobox_set_checked(xxwidgets_widget *widget,
    size_t index, int checked);
XXWIDGETS_API xxwidgets_status xxwidgets_checkcombobox_is_checked(const xxwidgets_widget *widget,
    size_t index, int *checked);
/* Checkbox combo only. Returns pointers to checked records in input order.
 * NULL/capacity 0 queries count; too-small buffers return BUFFER_TOO_SMALL
 * without partial output. User toggles emit CHANGE with the affected index;
 * programmatic changes do not emit events. Returned records are borrowed. */
XXWIDGETS_API xxwidgets_status xxwidgets_checkcombobox_get_checked(const xxwidgets_widget *widget,
    const xx_meta_string **records, size_t capacity, size_t *count);

typedef struct xxwidgets_option {
    const char *label; /* UTF-8 checkbox label. */
    int value; /* 0 or 1. */
} xxwidgets_option;

/* Common modal options form with checkboxes, OK and Cancel. The owner is
 * disabled while open. Values change only on OK; Cancel/close leaves them
 * unchanged and reports accepted == 0. Call on the UI thread outside input
 * callbacks/polling; reentrant calls return BUSY. Supports 0..16 options. */
XXWIDGETS_API xxwidgets_status xxwidgets_options_dialog(xxwidgets_widget *owner,
    const char *title, xxwidgets_option *options, size_t count, int *accepted);

/* Include xxwidgets/xxwidgets_process.h for xxfclib's xx_pd_struct definition.
 * Called immediately, then between event polls (30 ms timeout). Keep it
 * short: advance bounded work, or copy a synchronized worker snapshot into
 * progress and report its explicit completion through finished (initially 0).
 * stop_requested is sticky; propagate it to the operation. This callback runs
 * on the UI thread, with the same reentrancy rules as an input callback. */
typedef xxwidgets_status (*xxwidgets_process_update_fn)(void *user_data,
    int stop_requested, xx_pd_struct *progress, int *finished);

/* Modal progress for all five xx_pd_record slots. A bar and its status/count
 * label are visible exactly while is_busy is true. Percentages are clamped;
 * total == 0 displays zero percent with an unknown-total count.
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
XXWIDGETS_API xxwidgets_status xxwidgets_process_dialog(xxwidgets_widget *owner,
    const char *title, xx_pd_struct *progress, xxwidgets_process_update_fn update, void *user_data);

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
    XXWIDGETS_ABOUT_TEXT_COUNT
} xxwidgets_about_text;

/* Reusable About content, independent of an app/window. Text is copied UTF-8;
 * empty strings hide optional fields. Defaults: About / Application / Close.
 * The Close label must be nonempty. Setters are transactional.
 * Destroy/set while showing returns BUSY. */
XXWIDGETS_API xxwidgets_status xxwidgets_about_dialog_create(xxwidgets_about_dialog **out_dialog);
XXWIDGETS_API xxwidgets_status xxwidgets_about_dialog_destroy(xxwidgets_about_dialog *dialog);
XXWIDGETS_API xxwidgets_status xxwidgets_about_dialog_set_text(xxwidgets_about_dialog *dialog,
    xxwidgets_about_text field, const char *text);
/* Same buffer/required conventions as widget_get_text. */
XXWIDGETS_API xxwidgets_status xxwidgets_about_dialog_get_text(const xxwidgets_about_dialog *dialog,
    xxwidgets_about_text field, char *buffer, size_t capacity, size_t *required);
/* Copies top-down, straight-alpha RGBA8 pixels; stride is bytes per input row.
 * Dimensions: 1..32767. NULL/0/0/0 clears the image. Native backends display it
 * with its aspect ratio; the terminal dialog presents text only. No OS image
 * handles, file paths, or decoder dependencies cross this API. */
XXWIDGETS_API xxwidgets_status xxwidgets_about_dialog_set_image(xxwidgets_about_dialog *dialog,
    const void *rgba, unsigned int width, unsigned int height, size_t stride);
/* Modal, on the owner's UI thread, outside input callbacks/polling. The owner
 * is disabled while open, then restored. Content may be shown repeatedly with
 * different owners. Close/Enter/Escape dismiss; nested shows return BUSY. */
XXWIDGETS_API xxwidgets_status xxwidgets_about_dialog_show(xxwidgets_about_dialog *dialog,
    xxwidgets_widget *owner);

/* Scrollable, selectable UTF-8 text with Close and a Copy all button on native
 * desktops. Copy uses the system clipboard and reports success on the button.
 * Terminal backends show a scrolling text list with Close. Modal/reentrancy
 * rules are the same as about_dialog_show. Text is borrowed for this call. */
XXWIDGETS_API xxwidgets_status xxwidgets_text_dialog(xxwidgets_widget *owner,
    const char *title, const char *text);

/* Read-only HexView: 64-bit addresses, hex bytes and printable ASCII. Data is
 * copied and rendered as selectable rows. Defaults: base 0, 16 bytes per row.
 * set_data resets selection to the first row, or -1 for empty data. Data and
 * layout changes are transactional and do not emit input events. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_data(xxwidgets_widget *widget, const void *data, size_t size);
/* bytes_per_row must be 8, 16 or 32. The final displayed address must fit uint64_t. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_layout(xxwidgets_widget *widget,
    uint64_t base_address, unsigned int bytes_per_row);
XXWIDGETS_API size_t xxwidgets_hexview_size(const xxwidgets_widget *widget);
/* Reports selected row's byte offset and length. No selection: SIZE_MAX, 0.
 * SELECT event.value is a row index; this API returns its full byte range. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_get_selection(xxwidgets_widget *widget,
    size_t *offset, size_t *length);

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
XXWIDGETS_API xxwidgets_status xxwidgets_archiveview_set_entries(xxwidgets_widget *widget,
    const xxwidgets_archive_entry *entries, size_t count);
XXWIDGETS_API xxwidgets_status xxwidgets_archiveview_clear(xxwidgets_widget *widget);
XXWIDGETS_API size_t xxwidgets_archiveview_count(const xxwidgets_widget *widget);
/* Returned paths belong to the widget until replacement/clear/destruction.
 * SELECT event.value and widget set/get_value use the original entry index. */
XXWIDGETS_API xxwidgets_status xxwidgets_archiveview_get_entry(const xxwidgets_widget *widget,
    size_t index, xxwidgets_archive_entry *entry);
/* No selection: index SIZE_MAX and entry {NULL, 0, 0}. Both outputs required. */
XXWIDGETS_API xxwidgets_status xxwidgets_archiveview_get_selection(xxwidgets_widget *widget,
    size_t *index, xxwidgets_archive_entry *entry);

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
XXWIDGETS_API xxwidgets_status xxwidgets_archivebrowser_set_entries(xxwidgets_widget *widget,
    const xxwidgets_archive_browser_entry *entries, size_t count);
/* Advanced mode adds one column per distinct property name, in supplied order.
 * Properties are copied with entries; absent values/inferred folders stay blank.
 * Toggling preserves the directory and selection. Default is off. */
/* Archive is an optional UTF-8 display label/path; no file is opened. */
XXWIDGETS_API xxwidgets_status xxwidgets_archivebrowser_set_archive(xxwidgets_widget *widget,
    const char *archive);
XXWIDGETS_API const char *xxwidgets_archivebrowser_archive(const xxwidgets_widget *widget);
/* directory is archive-relative. Empty means root. Only existing explicit or
 * inferred directories can be selected. The getter returns a normalized path
 * with '/' separators and a trailing '/' for non-root directories. */
XXWIDGETS_API xxwidgets_status xxwidgets_archivebrowser_set_directory(xxwidgets_widget *widget,
    const char *directory);
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
XXWIDGETS_API xxwidgets_status xxwidgets_archivebrowser_get_entry(const xxwidgets_widget *widget,
    size_t row, size_t *source_index, xxwidgets_archive_browser_entry *entry);
/* No selection: source_index SIZE_MAX and a zero-initialized entry. */
XXWIDGETS_API xxwidgets_status xxwidgets_archivebrowser_get_selection(xxwidgets_widget *widget,
    size_t *source_index, xxwidgets_archive_browser_entry *entry);
/* Multiple selection uses current visible row indexes. set_value retains its
 * single-selection behavior. These setters do not emit input events.
 * Selection survives sorting, column changes and archive-label changes;
 * navigating or replacing entries selects the first row in the new view. */
XXWIDGETS_API xxwidgets_status xxwidgets_archivebrowser_set_selection(xxwidgets_widget *widget,
    const size_t *rows, size_t count);
XXWIDGETS_API size_t xxwidgets_archivebrowser_selection_count(const xxwidgets_widget *widget);
/* Returns original source indexes in ascending order, once each. Selected
 * folders include all descendants, including inferred folders. No selection
 * returns zero indexes. NULL/capacity 0 queries the required count. */
XXWIDGETS_API xxwidgets_status xxwidgets_archivebrowser_selected_sources(const xxwidgets_widget *widget,
    size_t *indexes, size_t capacity, size_t *required);
/* Directories always precede files; unknown sizes sort after known sizes.
 * Name comparison is deterministic, ASCII case-insensitive UTF-8 byte order. */
XXWIDGETS_API xxwidgets_status xxwidgets_archivebrowser_sort(xxwidgets_widget *widget,
    xxwidgets_archive_column column, int descending);

/* Advanced mode adds a column for each distinct property name in the supplied
 * entries. Properties are copied, unknown values stay blank, and inferred
 * folders have no record properties. Extra columns sort by their display text.
 * Toggling retains the current directory and selected member. */
XXWIDGETS_API xxwidgets_status xxwidgets_archivebrowser_set_advanced(xxwidgets_widget *widget, int advanced);
XXWIDGETS_API size_t xxwidgets_archivebrowser_column_count(const xxwidgets_widget *widget);
XXWIDGETS_API const char *xxwidgets_archivebrowser_column_title(const xxwidgets_widget *widget, size_t column);
XXWIDGETS_API const char *xxwidgets_archivebrowser_cell(const xxwidgets_widget *widget, size_t row,
    xxwidgets_archive_column column);

#ifdef __cplusplus
}
#endif
#endif
