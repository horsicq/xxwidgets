#ifndef XXWIDGETS_INTERNAL_H
#define XXWIDGETS_INTERNAL_H

#include "xxwidgets/xxwidgets.h"
#include "xxwidgets/xxwidgets_font_options.h"

typedef struct xxwidgets_backend_ops {
    const char *name;
    xxwidgets_status (*init)(xxwidgets_app *app);
    void (*shutdown)(xxwidgets_app *app);
    xxwidgets_status (*poll)(xxwidgets_app *app, int timeout_ms);
    xxwidgets_status (*create)(xxwidgets_widget *widget);
    void (*destroy)(xxwidgets_widget *widget);
    xxwidgets_status (*sync)(xxwidgets_widget *widget);
    xxwidgets_status (*read_text)(xxwidgets_widget *widget);
    xxwidgets_status (*read_value)(xxwidgets_widget *widget);
    xxwidgets_status (*focus)(xxwidgets_widget *widget);
    xxwidgets_status (*modal_owner)(xxwidgets_widget *dialog, xxwidgets_widget *owner, int active);
    xxwidgets_status (*about_content)(xxwidgets_widget *window, const xxwidgets_about_dialog *about, const char *body);
    xxwidgets_status (*copy_text)(xxwidgets_widget *window, const char *text);
    xxwidgets_status (*apply_fonts)(xxwidgets_app *app, const xxwidgets_font_options *options);
    xxwidgets_status (*choose_font)(xxwidgets_widget *owner, xxwidgets_font_role role, xxwidgets_font *font, int *accepted);
    xxwidgets_status (*preview_font)(xxwidgets_widget *widget, xxwidgets_font_role role, const xxwidgets_font *font);
    xxwidgets_status (*font_options_layout)(xxwidgets_widget *dialog, int refresh);
    xxwidgets_status (*optimization_options_layout)(xxwidgets_widget *dialog, int refresh);
    xxwidgets_status (*choose_file)(xxwidgets_widget *owner, xxwidgets_file_dialog_mode mode, const char *title, const char *initial, char **path, int *accepted);
    int (*has_focus)(const xxwidgets_widget *widget);
} xxwidgets_backend_ops;

struct xxwidgets_about_dialog {
    char *texts[XXWIDGETS_ABOUT_TEXT_COUNT];
    unsigned char *image;
    unsigned int image_width, image_height;
    int showing;
};

struct xxwidgets_app {
    const xxwidgets_backend_ops *ops;
    xxwidgets_backend backend;
    xxwidgets_event_fn on_event;
    void *user_data;
    void *platform;
    xxwidgets_widget *widgets; /* Creation order, singly linked. */
    int quit;
    int exit_code;
    int dispatch_depth;
    int syncing; /* Backend signal handlers suppress programmatic notifications. */
    int polling;
    xxwidgets_widget *modal_window;
    xxwidgets_widget *modal_default;
    xxwidgets_font_options font_options;
};

struct xxwidgets_widget {
    xxwidgets_app *app;
    xxwidgets_widget *parent;
    xxwidgets_widget *next;
    xxwidgets_kind kind;
    xxwidgets_rect rect;
    char *text;
    char **items;
    size_t item_count;
    unsigned char *hex_data;
    size_t hex_size;
    uint64_t hex_base;
    unsigned int hex_columns;
    uint64_t hex_revision;
    xxwidgets_archive_entry *archive_entries;
    uint64_t archive_revision;
    size_t archive_columns; /* Longest formatted row in Unicode scalars. */
    struct xxwidgets_archive_browser_state *browser;
    uint64_t browser_revision;
    struct xxwidgets_combo_state *combo;
    uint64_t combo_revision;
    xxwidgets_scan_result *scan_results;
    char **scan_cells; /* Escaped cells, row * 4 + column. */
    size_t scan_column_width[4];
    uint64_t scan_revision;
    struct xxwidgets_tree_state *tree;
    uint64_t tree_revision;
    uint64_t tree_content_revision;
    struct xxwidgets_shortcut_binding *shortcuts;
    size_t shortcut_count;
    /* WINDOW: xxwidgets_window_set_minimum_size; backends without a resize
     * limit ignore it. */
    int min_columns, min_rows, has_minimum;
    int value;
    int visible;
    int enabled;
    void *user_data;
    void *native;
    void *platform; /* Optional backend-specific auxiliary state. */
};

char *xxwidgets_strdup(const char *text);
int xxwidgets_valid_utf8(const char *text);
/* Backend input helpers update cached state before dispatch. */
xxwidgets_status xxwidgets_store_text(xxwidgets_widget *widget, const char *text);
/* Where an edit's caret goes when the program replaces its text: a byte
 * offset into now, or SIZE_MAX for the end. caret is a byte offset into old. */
size_t xxwidgets_edit_caret(const char *old, size_t caret, const char *now);
void xxwidgets_emit(xxwidgets_widget *widget, xxwidgets_event_type type, int value);
enum {
    XXWIDGETS_MOD_CTRL = 1,
    XXWIDGETS_MOD_ALT = 2,
    XXWIDGETS_MOD_SHIFT = 4,
    XXWIDGETS_MOD_META = 8,
    XXWIDGETS_KEY_F1 = 0x100,
    XXWIDGETS_KEY_ESCAPE = 0x200,
    XXWIDGETS_KEY_ENTER,
    XXWIDGETS_KEY_TAB,
    XXWIDGETS_KEY_BACKSPACE,
    XXWIDGETS_KEY_DELETE,
    XXWIDGETS_KEY_INSERT,
    XXWIDGETS_KEY_HOME,
    XXWIDGETS_KEY_END,
    XXWIDGETS_KEY_PAGEUP,
    XXWIDGETS_KEY_PAGEDOWN,
    XXWIDGETS_KEY_UP,
    XXWIDGETS_KEY_DOWN,
    XXWIDGETS_KEY_LEFT,
    XXWIDGETS_KEY_RIGHT
};
/* Return 1 when consumed. No references into the list survive the callback. */
int xxwidgets_shortcut_dispatch(xxwidgets_widget *window, unsigned int key, unsigned int modifiers);
/* Native context-menu request coordinates are pixels relative to the widget's
 * container. The event exists only for the duration of the callback. */
void xxwidgets_emit_context(xxwidgets_widget *widget, int value, int x, int y);
int xxwidgets_focusable(const xxwidgets_widget *widget);
xxwidgets_font_role xxwidgets_widget_font_role(const xxwidgets_widget *widget);
int xxwidgets_font_valid(const xxwidgets_font *font);
static inline int xxwidgets_combo_kind(const xxwidgets_widget *widget)
{
    return widget->kind == XXWIDGETS_COMBOBOX || widget->kind == XXWIDGETS_CHECKCOMBOBOX;
}
void xxwidgets_combobox_dispose(xxwidgets_widget *widget);
const char *xxwidgets_combobox_caption(xxwidgets_widget *widget);
int xxwidgets_checkcombobox_checked(const xxwidgets_widget *widget, size_t index);
xxwidgets_status xxwidgets_checkcombobox_user_toggle(xxwidgets_widget *widget, size_t index);
xxwidgets_status xxwidgets_checkcombobox_set_mask(xxwidgets_widget *widget, unsigned int mask, size_t count);

static inline int xxwidgets_formatted_rows(const xxwidgets_widget *widget)
{
    return widget->kind == XXWIDGETS_HEXVIEW || widget->kind == XXWIDGETS_ARCHIVEVIEW || widget->kind == XXWIDGETS_ARCHIVEBROWSER ||
           widget->kind == XXWIDGETS_SCANRESULTS || widget->kind == XXWIDGETS_TREEVIEW;
}
static inline int xxwidgets_list_kind(const xxwidgets_widget *widget)
{
    return widget->kind == XXWIDGETS_LISTBOX || xxwidgets_formatted_rows(widget);
}
static inline uint64_t xxwidgets_row_revision(const xxwidgets_widget *widget)
{
    return widget->kind == XXWIDGETS_TREEVIEW         ? widget->tree_revision
           : widget->kind == XXWIDGETS_SCANRESULTS    ? widget->scan_revision
           : widget->kind == XXWIDGETS_ARCHIVEBROWSER ? widget->browser_revision
           : widget->kind == XXWIDGETS_ARCHIVEVIEW    ? widget->archive_revision
                                                      : widget->hex_revision;
}

void xxwidgets_scanresults_dispose(xxwidgets_widget *widget);
void xxwidgets_treeview_dispose(xxwidgets_widget *widget);
size_t xxwidgets_treeview_node_at_row(const xxwidgets_widget *widget, size_t row);
size_t xxwidgets_treeview_row_of_node(const xxwidgets_widget *widget, size_t index);
size_t xxwidgets_treeview_depth(const xxwidgets_widget *widget, size_t index);
int xxwidgets_treeview_has_children(const xxwidgets_widget *widget, size_t index);
const char *xxwidgets_treeview_display_text(const xxwidgets_widget *widget, size_t index);
xxwidgets_status xxwidgets_treeview_select(xxwidgets_widget *widget, int index);
xxwidgets_status xxwidgets_treeview_user_expand(xxwidgets_widget *widget, size_t index, int expanded);
/* Cache native expansion without syncing/emitting. Caller follows the cached
 * selection when collapse hides it, suppressing native selection signals. */
xxwidgets_status xxwidgets_treeview_expansion_input(xxwidgets_widget *widget, size_t index, int expanded);
const char *xxwidgets_scanresults_cell(const xxwidgets_widget *widget, size_t row, size_t column);
size_t xxwidgets_scanresults_column_width(const xxwidgets_widget *widget, size_t column);

void xxwidgets_archivebrowser_dispose(xxwidgets_widget *widget);
const char *xxwidgets_archivebrowser_name(const xxwidgets_widget *widget, size_t row);
const char *xxwidgets_archivebrowser_cell(const xxwidgets_widget *widget, size_t row, xxwidgets_archive_column column);
size_t xxwidgets_archivebrowser_name_columns(const xxwidgets_widget *widget);
xxwidgets_archive_column xxwidgets_archivebrowser_sort_column(const xxwidgets_widget *widget);
int xxwidgets_archivebrowser_sort_descending(const xxwidgets_widget *widget);
int xxwidgets_archivebrowser_row_selected(const xxwidgets_widget *widget, size_t row);
/* Update cached selection from a native input notification, without syncing
 * or emitting events. Native backends emit one final SELECT themselves. */
void xxwidgets_archivebrowser_selection_input(xxwidgets_widget *widget, size_t row, int selected);
void xxwidgets_archivebrowser_selection_clear(xxwidgets_widget *widget);
xxwidgets_status xxwidgets_archivebrowser_select_all(xxwidgets_widget *widget);
/* Backend user actions. Navigation emits CHANGE only after successful sync;
 * file activation emits ACTIVATE. Programmatic setters never emit events. */
xxwidgets_status xxwidgets_archivebrowser_user_activate(xxwidgets_widget *widget, size_t row);
xxwidgets_status xxwidgets_archivebrowser_user_up(xxwidgets_widget *widget);
xxwidgets_status xxwidgets_archivebrowser_user_sort(xxwidgets_widget *widget, xxwidgets_archive_column column);

extern const xxwidgets_backend_ops xxwidgets_tui_ops;
#if defined(XXWIDGETS_HAS_NATIVE)
extern const xxwidgets_backend_ops xxwidgets_native_ops;
#endif
#endif
