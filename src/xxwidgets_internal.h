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

#ifndef XXWIDGETS_INTERNAL_H
#define XXWIDGETS_INTERNAL_H

#include "xxwidgets/xxwidgets.h"
#include "xxwidgets/xxwidgets_translation.h"
#include "xxfclib/translation/xx_translation.h"

/* xgettext: --keyword=XXW_TR:2 --keyword=XXW_N_ */
#define XXW_TR(app, text) xxwidgets_app_gettext((app), (text))
#define XXW_N_(text) text
#include "xxwidgets/xxwidgets_font_options.h"
#include "xxwidgets/xxwidgets_canvas.h"
#include "die_engine/xxwidgets_die_signature_edit_internal.h"
#include "xxwidgets/xxwidget_systemprocessesviewer.h"

xxwidgets_status xxwidgets_translated_choices(xxwidgets_widget *, const char *const *, const uint64_t *, size_t);

/* Atomic copied-data/base-address replacement for debugger memory panes. */
xxwidgets_status xxwidgets_hexview_replace_snapshot(xxwidgets_widget *, uint64_t, const void *, size_t);

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
    xxwidgets_status (*accept_file_drops)(xxwidgets_widget *window, int accept);
    size_t (*hexview_page_rows)(const xxwidgets_widget *widget);
    xxwidgets_status (*paste_text)(xxwidgets_widget *, char *, size_t, size_t *);
    xxwidgets_status (*hexview_font)(xxwidgets_widget *, const xxwidgets_font *);
    xxwidgets_status (*window_menu)(xxwidgets_widget *);
    /* Optional Detect It Easy style layout for rich About items, filling the
     * window above the Close button row (XXWIDGETS_ABOUT_RICH_* cells). */
    xxwidgets_status (*about_rich_content)(xxwidgets_widget *window, const xxwidgets_about_dialog *about);
} xxwidgets_backend_ops;

#define XXWIDGETS_ABOUT_ITEMS_MAX 64
#define XXWIDGETS_ABOUT_RICH_WIDTH 104
#define XXWIDGETS_ABOUT_RICH_HEIGHT 24

typedef struct xxwidgets_about_item {
    xxwidgets_about_item_kind kind;
    char *text, *value, *link;
} xxwidgets_about_item;

struct xxwidgets_about_dialog {
    char *texts[XXWIDGETS_ABOUT_TEXT_COUNT];
    unsigned char *image;
    unsigned int image_width, image_height;
    xxwidgets_about_item items[XXWIDGETS_ABOUT_ITEMS_MAX];
    size_t item_count;
    int showing;
};

struct xxwidgets_app {
    xx_translation *translation;
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
    int create_hidden;
    xxwidgets_widget *modal_window;
    xxwidgets_widget *modal_default;
    xxwidgets_font_options font_options;
};

struct xxwidgets_hex_pending_action {
    int action;
    uint64_t offset, length;
    unsigned char *data; /* Owned paste bytes, fallback encoding, or cut column/encoding. */
};

typedef struct xxwidgets_hex_position {
    size_t cursor, anchor, anchor_bytes, selection, length;
    unsigned int nibble;
} xxwidgets_hex_position;

struct xxwidgets_widget {
    xxwidgets_app *app;
    xxwidgets_widget *parent;
    xxwidgets_widget *next;
    xxwidgets_kind kind;
    xxwidgets_rect rect;
    char *text;
    char **items;
    size_t item_count;
    xxwidgets_menu_item *menu_items;
    size_t menu_count;
    unsigned char *hex_data;
    size_t hex_size;
    uint64_t hex_base;
    unsigned int hex_columns;
    uint64_t hex_revision;
    xxwidgets_hex_display hex_display;
    size_t hex_widths[3];
    char hex_cell[1024], hex_title[1024]; /* Backend scratch, valid until next cell/title request. */
    unsigned int hex_header, hex_menu_index;
    int hex_menu_open;
    int hex_editable;
    int hex_bold_nonzero;
    xxwidgets_font hex_font;
    int hex_has_font;
    size_t hex_anchor, hex_anchor_bytes, hex_selection, hex_selection_length;
    xxwidgets_hexview_source_read_fn hex_source_read;
    xxwidgets_hexview_source_seek_fn hex_source_seek;
    void *hex_source_user;
    struct xxwidgets_hex_io_state *hex_io; /* Owned adapter; device ownership is explicit. */
    uint64_t hex_source_size, hex_source_anchor, hex_source_anchor_bytes;
    uint64_t hex_source_selection, hex_source_length;
    xxwidgets_hexview_copy_fn hex_copy_handler;
    void *hex_copy_user;
    xxwidgets_hexview_dump_fn hex_dump_handler;
    void *hex_dump_user;
    xxwidgets_hexview_input_fn hex_input_handler;
    void *hex_input_user;
    xxwidgets_hexview_highlight *hex_highlights;
    size_t hex_highlight_count;
    xxwidgets_hexview_highlight *hex_overview_markers;
    size_t hex_overview_marker_count;
    int hex_source_seeking;
    int hex_dragging, hex_pending_dump;
    int hex_pending_edit, hex_edit_busy;
    int hex_cut_busy; /* Confirming clipboard publication and deleting a cut. */
    int hex_replay_input; /* Applying a queued copied-data key in input order. */
    uint64_t hex_pending_edit_offset, hex_pending_edit_length;
    unsigned char *hex_pending_edit_data;
    struct xx_history *hex_history;
    struct xxwidgets_hex_pending_action *hex_edit_queue;
    size_t hex_edit_queue_count, hex_edit_queue_capacity;
    size_t hex_cursor;
    unsigned int hex_nibble, hex_pending_surrogate;
    /* Keep a vertical or row-edge move's byte column and requested row even
     * when the selected scalar starts before that slot or on the preceding row. */
    size_t hex_vertical_column, hex_vertical_row;
    uint64_t hex_source_vertical_row; /* Absolute file row for attached sources. */
    int hex_vertical_active;
    /* Transient hints valid only while the backend syncs a HexView update. */
    int hex_sync_full;
    size_t hex_sync_dirty_count;
    size_t hex_sync_dirty_rows[3];
    xxwidgets_archive_entry *archive_entries;
    uint64_t archive_revision;
    size_t archive_columns; /* Longest formatted row in Unicode scalars. */
    struct xxwidgets_archive_browser_state *browser;
    uint64_t browser_revision;
    struct xxwidgets_combo_state *combo;
    uint64_t combo_revision;
    xxwidgets_scan_result *scan_results;
    xxwidgets_process_info *processes; /* Names/paths borrow scan_results strings. */
    struct xxwidgets_binaryviewer_state *binaryviewer;
    uint64_t binary_revision;
    struct xxwidgets_die_signature_edit_state *die_signature_edit;
    struct xxwidgets_debugger_table_state *debugger_table;
    struct xxwidgets_text_table_state *text_table; /* XXWIDGETS_TABLE; rows use debugger_revision. */
    uint64_t debugger_revision;
    int process_selection; /* Transactional replacement selection hint. */
    char **scan_cells; /* Escaped cells, row * 4 + column. */
    size_t scan_column_width[4];
    uint64_t scan_revision;
    struct xxwidgets_data_struct_field *data_struct_fields;
    char **data_struct_cells; /* Escaped cells, row * 9 + column. */
    size_t data_struct_column_width[9];
    uint64_t data_struct_revision;
    /* Table filters (xxwidgets_table_*): text per column, and the model rows
     * they let through, rebuilt when the rows or the filters change. */
    char **table_filters;
    size_t table_filter_count;
    size_t *table_rows, table_row_count;
    uint64_t table_filter_revision, table_rows_key, table_rows_filter;
    int table_rows_valid;
    struct xxwidgets_tree_state *tree;
    uint64_t tree_revision;
    uint64_t tree_content_revision;
    struct xxwidgets_shortcut_binding *shortcuts;
    size_t shortcut_count;
    xxwidgets_shortcut_filter_fn shortcut_filter; void *shortcut_filter_user;
    /* WINDOW: xxwidgets_window_set_minimum_size; backends without a resize
     * limit ignore it. */
    int min_columns, min_rows, has_minimum;
    int accept_file_drops;
    struct xxwidgets_entropy *entropy; /* Borrowed composite owner. */
    struct xxwidgets_entropy_sample *entropy_plot_samples; /* Owned standalone plot data. */
    size_t entropy_plot_count;
    uint64_t entropy_plot_base, entropy_plot_size;
    char **entropy_cells;
    uint64_t entropy_revision;
    uint32_t *canvas_pixels; unsigned int canvas_width,canvas_height; uint64_t canvas_revision;
    xxwidgets_canvas_label *canvas_labels;size_t canvas_label_count;
    int value;
    int visible;
    int enabled;
    int keep_end; /* LABEL: a text too long for it loses its middle, not its end (a path). */
    void *user_data;
    void *native;
    void *platform; /* Optional backend-specific auxiliary state. */
};

void xxwidgets_canvas_dispose(xxwidgets_widget *);
void xxwidgets_canvas_fit(const xxwidgets_widget *,double,double,double *,double *,double *,double *);
void xxwidgets_canvas_input(xxwidgets_widget *,double,double,double,double);
int xxwidgets_canvas_key(xxwidgets_widget *,int);
void xxwidgets_splitter_input(xxwidgets_widget *,double,double,double,double);
int xxwidgets_splitter_key(xxwidgets_widget *,int);
void xxwidgets_emit_point(xxwidgets_widget *,xxwidgets_event_type,int,int,int);
char *xxwidgets_strdup(const char *text);
int xxwidgets_valid_utf8(const char *text);
/* Backend input helpers update cached state before dispatch. */
xxwidgets_status xxwidgets_store_text(xxwidgets_widget *widget, const char *text);
/* Where an edit's caret goes when the program replaces its text: a byte
 * offset into now, or SIZE_MAX for the end. caret is a byte offset into old. */
size_t xxwidgets_edit_caret(const char *old, size_t caret, const char *now);
void xxwidgets_emit(xxwidgets_widget *widget, xxwidgets_event_type type, int value);
void xxwidgets_menu_dispose(xxwidgets_widget *window);
int xxwidgets_menu_can_activate(const xxwidgets_widget *window, size_t index);
void xxwidgets_menu_activate(xxwidgets_widget *window, size_t index);
int xxwidgets_can_drop_files(const xxwidgets_widget *window);
int xxwidgets_emit_drop_files(xxwidgets_widget *window, const char *const *paths, size_t count);
enum {
    XXWIDGETS_MOD_CTRL = 1,
    XXWIDGETS_MOD_ALT = 2,
    XXWIDGETS_MOD_SHIFT = 4,
    XXWIDGETS_MOD_META = 8,
    XXWIDGETS_KEY_F1 = 0x110000,
    XXWIDGETS_KEY_ESCAPE = 0x110100,
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
void xxwidgets_shortcuts_dispose(xxwidgets_widget *widget);
/* Native context-menu request coordinates are pixels relative to the widget's
 * container. The event exists only for the duration of the callback. */
void xxwidgets_emit_context(xxwidgets_widget *widget, int value, int x, int y);
int xxwidgets_focusable(const xxwidgets_widget *widget);
xxwidgets_font_role xxwidgets_widget_font_role(const xxwidgets_widget *widget);
int xxwidgets_font_valid(const xxwidgets_font *font);
int xxwidgets_font_is_monospace(const xxwidgets_font *font);
static inline int xxwidgets_combo_kind(const xxwidgets_widget *widget)
{
    return widget->kind == XXWIDGETS_COMBOBOX || widget->kind == XXWIDGETS_CHECKCOMBOBOX;
}
void xxwidgets_combobox_dispose(xxwidgets_widget *widget);
const char *xxwidgets_combobox_caption(xxwidgets_widget *widget);
int xxwidgets_checkcombobox_checked(const xxwidgets_widget *widget, size_t index);
xxwidgets_status xxwidgets_checkcombobox_user_toggle(xxwidgets_widget *widget, size_t index);
xxwidgets_status xxwidgets_checkcombobox_set_mask(xxwidgets_widget *widget, unsigned int mask, size_t count);
/* Replace two check-combo models and their enabled state as one logical
 * transaction. This is used by composite widgets whose paired selectors must
 * never expose catalogs from different generations. */
xxwidgets_status xxwidgets_checkcombobox_set_shared_records(xxwidgets_widget *first, xxwidgets_widget *second,
    const xx_meta_string *records, size_t count, int enabled);

static inline int xxwidgets_entropy_table(const xxwidgets_widget *widget)
{ return widget && (widget->kind == XXWIDGETS_ENTROPY_REGIONS || widget->kind == XXWIDGETS_ENTROPY_BYTES); }
static inline int xxwidgets_binaryviewer_kind(const xxwidgets_widget *widget)
{ return widget && widget->kind >= XXWIDGET_RESOURCESVIEWER && widget->kind <= XXWIDGET_SYMBOLSVIEWER; }
/* XXWIDGETS_TABLE is drawn like the debugger tables. */
static inline int xxwidgets_debugger_table_kind(const xxwidgets_widget *widget)
{ return widget && ((widget->kind >= XXWIDGETS_DEBUGGER_DISASM && widget->kind <= XXWIDGETS_DEBUGGER_STACK) || widget->kind == XXWIDGETS_MEMORY_MAP || widget->kind == XXWIDGETS_TABLE); }
static inline int xxwidgets_tabular_kind(const xxwidgets_widget *widget)
{ return widget && (widget->kind == XXWIDGETS_HEXVIEW || widget->kind == XXWIDGETS_SCANRESULTS || widget->kind == XXWIDGET_SYSTEMPROCESSESVIEWER || widget->kind == XXWIDGETS_DATA_STRUCT_TABLE || xxwidgets_entropy_table(widget) || xxwidgets_binaryviewer_kind(widget) || xxwidgets_debugger_table_kind(widget)); }
/* Plot primitives use top-left coordinates, RGB colors, and borrowed text.
 * kind: 0=line, 1=filled rectangle, 2=text. */
typedef void (*xxwidgets_plot_draw_fn)(void *user, int kind, double x, double y, double x2, double y2, uint32_t rgb, const char *text);
void xxwidgets_entropy_draw(const xxwidgets_widget *widget, double width, double height, double cell_x, double cell_y, xxwidgets_plot_draw_fn draw, void *user);
void xxwidgets_entropy_plot_input(xxwidgets_widget *widget, double x, double width, double cell_x);
void xxwidgets_entropy_table_dispose(xxwidgets_widget *widget);
const char *xxwidgets_entropy_cell(const xxwidgets_widget *widget, size_t row, size_t column);
static inline int xxwidgets_formatted_rows(const xxwidgets_widget *widget)
{
    return widget->kind == XXWIDGETS_HEXVIEW || widget->kind == XXWIDGETS_ARCHIVEVIEW || widget->kind == XXWIDGETS_ARCHIVEBROWSER ||
           widget->kind == XXWIDGETS_SCANRESULTS || widget->kind == XXWIDGET_SYSTEMPROCESSESVIEWER || widget->kind == XXWIDGETS_TREEVIEW || widget->kind == XXWIDGETS_DATA_STRUCT_TABLE || xxwidgets_entropy_table(widget) || xxwidgets_binaryviewer_kind(widget) || xxwidgets_debugger_table_kind(widget);
}
static inline int xxwidgets_list_kind(const xxwidgets_widget *widget)
{
    return widget->kind == XXWIDGETS_LISTBOX || xxwidgets_formatted_rows(widget);
}
static inline uint64_t xxwidgets_row_revision(const xxwidgets_widget *widget)
{
    /* A filter change rebuilds the rows too. */
    return widget->table_filter_revision + (xxwidgets_debugger_table_kind(widget) ? widget->debugger_revision
           : xxwidgets_binaryviewer_kind(widget) ? widget->binary_revision
           : xxwidgets_entropy_table(widget) ? widget->entropy_revision
           : widget->kind == XXWIDGETS_TREEVIEW         ? widget->tree_revision
           : (widget->kind == XXWIDGETS_SCANRESULTS || widget->kind == XXWIDGET_SYSTEMPROCESSESVIEWER) ? widget->scan_revision
           : widget->kind == XXWIDGETS_DATA_STRUCT_TABLE ? widget->data_struct_revision
           : widget->kind == XXWIDGETS_ARCHIVEBROWSER ? widget->browser_revision
           : widget->kind == XXWIDGETS_ARCHIVEVIEW    ? widget->archive_revision
                                                      : widget->hex_revision);
}

/* Backend input helpers. selection_input refreshes the cursor marker but does
 * not emit SELECT; the backend emits it after success. key_input accepts ASCII
 * hexadecimal digits plus XXWIDGETS_KEY_* navigation values and emits the
 * corresponding CHANGE/SELECT events after a successful transactional sync. */
typedef struct xxwidgets_hex_menu_item {
    const char *group, *title;
    int checked, disabled;
} xxwidgets_hex_menu_item;
size_t xxwidgets_hexview_menu_count(unsigned int column);
xxwidgets_hex_menu_item xxwidgets_hexview_menu_item(const xxwidgets_widget *, unsigned int column, size_t index);
xxwidgets_status xxwidgets_hexview_menu_apply(xxwidgets_widget *, unsigned int column, size_t index);
const char *xxwidgets_hexview_cell(const xxwidgets_widget *, size_t row, size_t column);
const char *xxwidgets_hexview_title(const xxwidgets_widget *, size_t column);
size_t xxwidgets_hexview_cursor_column(const xxwidgets_widget *);
typedef struct xxwidgets_hex_span {
    size_t start, length, offset, bytes;
} xxwidgets_hex_span;
/* Text ranges are UTF-8 byte indices, shared by hit testing and all renderers. */
int xxwidgets_hexview_highlight_span(const xxwidgets_widget *, size_t offset, size_t length, uint32_t *color);
/* Overview helpers use absolute file offsets and a top-origin pixel row.
 * Empty/invalid offset queries return UINT64_MAX. No drawing-time allocation. */
uint64_t xxwidgets_hexview_overview_size(const xxwidgets_widget *);
int xxwidgets_hexview_overview_color(const xxwidgets_widget *, size_t row, size_t height, uint32_t *color);
uint64_t xxwidgets_hexview_overview_offset(const xxwidgets_widget *, size_t row, size_t height);
xxwidgets_status xxwidgets_hexview_overview_activate(xxwidgets_widget *, size_t row, size_t height);
size_t xxwidgets_hexview_spans(const xxwidgets_widget *, size_t row, size_t column, xxwidgets_hex_span spans[64]);
/* Full non-zero display units, with alignment padding omitted. Numeric column only. */
size_t xxwidgets_hexview_bold_spans(const xxwidgets_widget *, size_t row, size_t column, xxwidgets_hex_span spans[64]);
/* Clip a complete source symbol to the byte slots visible on this row.
 * A continuation-only row span has length zero and retains its source owner. */
void xxwidgets_hexview_symbol_layout(const xxwidgets_widget *, size_t row, const xxwidgets_hex_span *, size_t *cell, size_t *cells);
size_t xxwidgets_hexview_selection_spans(const xxwidgets_widget *, size_t row, size_t column, xxwidgets_hex_span spans[64]);
size_t xxwidgets_hexview_hit(const xxwidgets_widget *, size_t row, size_t column, size_t text_index);
/* Symbols use byte-cell positions; other columns use their text positions.
 * The hit retains the complete source byte range of a decoded symbol. */
int xxwidgets_hexview_hit_span(const xxwidgets_widget *, size_t row, size_t column, size_t cell_index, xxwidgets_hex_span *out);
int xxwidgets_hexview_contains(const xxwidgets_widget *, size_t offset, size_t length);
xxwidgets_status xxwidgets_hexview_pointer(xxwidgets_widget *, size_t offset, int extend);
xxwidgets_status xxwidgets_hexview_pointer_span(xxwidgets_widget *, size_t offset, size_t bytes, int extend);
xxwidgets_status xxwidgets_hexview_body_action(xxwidgets_widget *, size_t index);
void xxwidgets_hexview_deferred(xxwidgets_app *);
xxwidgets_status xxwidgets_hexview_edit_interactive(xxwidgets_widget *, uint64_t offset,
                                                   const void *data, size_t insert_length, uint64_t delete_length);
xxwidgets_status xxwidgets_hexview_history_interactive(xxwidgets_widget *, int redo);
xxwidgets_status xxwidgets_hexview_apply_memory_overwrite(xxwidgets_widget *, size_t offset, const void *, size_t length,
                                                        const xxwidgets_hex_position *);
xxwidgets_status xxwidgets_hexview_overwrite_input(xxwidgets_widget *, size_t offset, unsigned char byte,
                                                 size_t next_cursor, unsigned int next_nibble);
xxwidgets_status xxwidgets_hexview_overwrite_interactive(xxwidgets_widget *, uint64_t offset, const void *, size_t length);
xxwidgets_status xxwidgets_hexview_decode_paste(const xxwidgets_widget *, const char *, unsigned char **, size_t *);
xxwidgets_status xxwidgets_hexview_copy_range_text(xxwidgets_widget *, uint64_t offset, uint64_t length,
                                                  xxwidgets_hex_copy_format, const char *encoding,
                                                  char **text, unsigned char **snapshot);
xxwidgets_status xxwidgets_hexview_cut_range(xxwidgets_widget *, uint64_t offset, uint64_t length,
                                            unsigned int column, const char *encoding, int interactive);
void xxwidgets_hexview_history_dispose(xxwidgets_widget *);
/* Called after polling; also works for a HexView inside the active modal window. */
xxwidgets_status xxwidgets_hexview_dump_dialog(xxwidgets_widget *);
void xxwidgets_hexview_init(xxwidgets_widget *widget);
void xxwidgets_hexview_dispose(xxwidgets_widget *widget);
xxwidgets_status xxwidgets_hexview_selection_input(xxwidgets_widget *widget, int row);
xxwidgets_status xxwidgets_hexview_key_input(xxwidgets_widget *widget, unsigned int key, unsigned int modifiers, int *handled);
void xxwidgets_hexview_activate_column(xxwidgets_widget *widget, unsigned int column);

void xxwidgets_scanresults_dispose(xxwidgets_widget *widget);
void xxwidgets_scanresults_init(xxwidgets_widget *widget);
xxwidgets_status xxwidgets_scanresults_set_table_results(xxwidgets_widget *, const xxwidgets_scan_result *, size_t);
void xxwidgets_treeview_dispose(xxwidgets_widget *widget);
size_t xxwidgets_treeview_node_at_row(const xxwidgets_widget *widget, size_t row);
size_t xxwidgets_treeview_row_of_node(const xxwidgets_widget *widget, size_t index);
size_t xxwidgets_treeview_depth(const xxwidgets_widget *widget, size_t index);
int xxwidgets_treeview_has_children(const xxwidgets_widget *widget, size_t index);
const char *xxwidgets_treeview_display_text(const xxwidgets_widget *widget, size_t index);
/* User input on a tree mark. click reports a pointer hit on mark of node; key
 * a typed character for the selected node, returning 1 when it named a mark
 * there (and the CLICK was emitted). */
void xxwidgets_treeview_mark_click(xxwidgets_widget *widget, size_t index, size_t mark);
int xxwidgets_treeview_mark_key(xxwidgets_widget *widget, unsigned int key);
xxwidgets_status xxwidgets_treeview_select(xxwidgets_widget *widget, int index);
xxwidgets_status xxwidgets_treeview_user_expand(xxwidgets_widget *widget, size_t index, int expanded);
/* Cache native expansion without syncing/emitting. Caller follows the cached
 * selection when collapse hides it, suppressing native selection signals. */
xxwidgets_status xxwidgets_treeview_expansion_input(xxwidgets_widget *widget, size_t index, int expanded);
const char *xxwidgets_scanresults_cell(const xxwidgets_widget *widget, size_t row, size_t column);
size_t xxwidgets_scanresults_column_width(const xxwidgets_widget *widget, size_t column);
void xxwidgets_data_struct_table_dispose(xxwidgets_widget *widget);
void xxwidgets_data_struct_table_init(xxwidgets_widget *widget);
const char *xxwidgets_data_struct_table_cell(const xxwidgets_widget *widget, size_t row, size_t column);
size_t xxwidgets_data_struct_table_column_width(const xxwidgets_widget *widget, size_t column);
/* Tables address rows as shown, after their filters: row text, cells and
 * counts below take shown rows; widget->value and events stay model rows.
 * tabular_value/tabular_row convert (-1 for none or hidden). */
const char *xxwidgets_tabular_cell(const xxwidgets_widget *widget, size_t row, size_t column);
const char *xxwidgets_tabular_model_cell(const xxwidgets_widget *widget, size_t model_row, size_t column);
const char *xxwidgets_tabular_row_text(const xxwidgets_widget *widget, size_t row);
size_t xxwidgets_tabular_rows(const xxwidgets_widget *widget);
size_t xxwidgets_tabular_model_row(const xxwidgets_widget *widget, size_t row);
size_t xxwidgets_tabular_display_row(const xxwidgets_widget *widget, size_t model);
int xxwidgets_tabular_value(const xxwidgets_widget *widget, int row);
int xxwidgets_tabular_row(const xxwidgets_widget *widget, int value);
int xxwidgets_table_filterable(const xxwidgets_widget *widget);
/* User input in a column's filter: applies it, rebuilds the view, reports a
 * selection it hid with SELECT -1, then the change with CHANGE (the column). */
xxwidgets_status xxwidgets_table_filter_input(xxwidgets_widget *widget, size_t column, const char *text);
void xxwidgets_table_filter_dispose(xxwidgets_widget *widget);
size_t xxwidgets_tabular_column_width(const xxwidgets_widget *widget, size_t column);
size_t xxwidgets_tabular_column_count(const xxwidgets_widget *widget);
const char *xxwidgets_tabular_column_title(const xxwidgets_widget *widget, size_t column);
xxwidgets_status xxwidgets_binaryviewer_set_records(xxwidgets_widget *, xxwidgets_kind, const void *, size_t);
xxwidgets_status xxwidgets_binaryviewer_get_record(const xxwidgets_widget *, xxwidgets_kind, size_t, void *);
xxwidgets_status xxwidgets_binaryviewer_get_selection(xxwidgets_widget *, xxwidgets_kind, size_t *, void *);
void xxwidgets_binaryviewer_dispose(xxwidgets_widget *);
void xxwidgets_debugger_table_dispose(xxwidgets_widget *);
/* XXWIDGETS_TABLE: init takes the titles from widget->text. */
xxwidgets_status xxwidgets_text_table_init(xxwidgets_widget *);
/* set_rows selecting the given model row, or the first shown when filtered out. */
xxwidgets_status xxwidgets_text_table_replace(xxwidgets_widget *, const char *const *cells, size_t rows, int selection);
void xxwidgets_text_table_dispose(xxwidgets_widget *);
const char *xxwidgets_text_table_title(const xxwidgets_widget *, size_t);
const char *xxwidgets_text_table_cell(const xxwidgets_widget *, size_t, size_t);
size_t xxwidgets_text_table_column_width(const xxwidgets_widget *, size_t);
size_t xxwidgets_debugger_table_column_count(const xxwidgets_widget *);
const char *xxwidgets_debugger_table_column_title(const xxwidgets_widget *, size_t);
const char *xxwidgets_debugger_table_cell(const xxwidgets_widget *, size_t, size_t);
size_t xxwidgets_debugger_table_column_width(const xxwidgets_widget *, size_t);
size_t xxwidgets_binaryviewer_column_count(const xxwidgets_widget *);
const char *xxwidgets_binaryviewer_column_title(const xxwidgets_widget *, size_t);
const char *xxwidgets_binaryviewer_cell(const xxwidgets_widget *, size_t, size_t);
size_t xxwidgets_binaryviewer_column_width(const xxwidgets_widget *, size_t);

void xxwidgets_archivebrowser_dispose(xxwidgets_widget *widget);
void xxwidgets_archiveview_dispose(xxwidgets_widget *widget);
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
