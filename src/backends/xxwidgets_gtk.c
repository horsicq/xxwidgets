#include "xxwidgets_internal.h"

#include <gtk/gtk.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct gtk_app_state {
    int cell_width;
    int cell_height;
} gtk_app_state;

typedef struct gtk_widget_state {
    GtkWidget *root; /* Scrolled container for a list, otherwise the control. */
    GtkWidget *content; /* A window's GtkFixed. */
    GtkWidget *browser_scroll, *browser_address, *browser_up;
    size_t rendered_items;
    uint64_t rendered_hex_revision;
    int scroll_selection;
    xxwidgets_rect applied_rect;
    int has_rect;
    GtkWidget *combo_box, *combo_scroll;
    uint64_t combo_revision;
    int combo_initialized;
    GtkTreeIter *tree_iters;
    size_t tree_count;
    uint64_t tree_content_revision;
    int tree_collapsing, tree_before_collapse;
} gtk_widget_state;

static int rect_equal(xxwidgets_rect a, xxwidgets_rect b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

static int pixel_rect(xxwidgets_widget *widget, int *x, int *y, int *width, int *height)
{
    gtk_app_state *state = widget->app->platform;
    int64_t px = (int64_t)widget->rect.x * state->cell_width;
    int64_t py = (int64_t)widget->rect.y * state->cell_height;
    int64_t pw = (int64_t)widget->rect.width * state->cell_width;
    int64_t ph = (int64_t)widget->rect.height * state->cell_height;
    if (px < INT_MIN || px > INT_MAX || py < INT_MIN || py > INT_MAX ||
        pw <= 0 || pw > INT_MAX || ph <= 0 || ph > INT_MAX) return 0;
    *x = (int)px; *y = (int)py; *width = (int)pw; *height = (int)ph;
    return 1;
}

static gboolean window_close(GtkWidget *native, GdkEvent *event, gpointer data)
{
    xxwidgets_widget *widget = data;
    (void)native; (void)event;
    if (!widget->app->syncing) xxwidgets_emit(widget, XXWIDGETS_EVENT_CLOSE, 0);
    /* The handler may hide the window or quit. Destruction belongs to the core. */
    return TRUE;
}

static gboolean window_configure(GtkWidget *native, GdkEventConfigure *event, gpointer data)
{
    xxwidgets_widget *widget = data;
    gtk_app_state *app_state = widget->app->platform;
    gtk_widget_state *state = widget->platform;
    int width = (int)(((int64_t)event->width + app_state->cell_width / 2) /
                      app_state->cell_width);
    int height = (int)(((int64_t)event->height + app_state->cell_height / 2) /
                       app_state->cell_height);
    (void)native;
    if (!widget->app->syncing && width > 0 && height > 0 &&
        (width != widget->rect.width || height != widget->rect.height)) {
        widget->rect.width = width;
        widget->rect.height = height;
        state->applied_rect = widget->rect;
        xxwidgets_emit(widget, XXWIDGETS_EVENT_RESIZE, 0);
    }
    return FALSE;
}

static void button_clicked(GtkButton *native, gpointer data)
{
    xxwidgets_widget *widget = data;
    (void)native;
    if (!widget->app->syncing) xxwidgets_emit(widget, XXWIDGETS_EVENT_CLICK, 0);
}

static void edit_changed(GtkEditable *native, gpointer data)
{
    xxwidgets_widget *widget = data;
    if (widget->app->syncing) return;
    if (xxwidgets_store_text(widget, gtk_entry_get_text(GTK_ENTRY(native))) == XXWIDGETS_OK)
        xxwidgets_emit(widget, XXWIDGETS_EVENT_CHANGE, 0);
}

static void combo_changed(GtkComboBox *native, gpointer data)
{
    xxwidgets_widget *widget = data;
    if (widget->app->syncing) return;
    widget->value = gtk_combo_box_get_active(native);
    xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, widget->value);
}

static void combo_checked(GtkToggleButton *native, gpointer data)
{
    xxwidgets_widget *widget = data;
    size_t index = (size_t)GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(native), "xxwidgets-index"));
    if (widget->app->syncing || !xxwidgets_focusable(widget) || index >= widget->item_count) return;
    widget->value = (int)index;
    if (!!gtk_toggle_button_get_active(native) != xxwidgets_checkcombobox_checked(widget, index))
        xxwidgets_checkcombobox_user_toggle(widget, index);
}

static void checkbox_changed(GtkToggleButton *native, gpointer data)
{
    xxwidgets_widget *widget = data;
    if (widget->app->syncing) return;
    widget->value = gtk_toggle_button_get_active(native) ? 1 : 0;
    xxwidgets_emit(widget, XXWIDGETS_EVENT_CHANGE, widget->value);
}

static void list_selected(GtkListBox *native, GtkListBoxRow *row, gpointer data)
{
    xxwidgets_widget *widget = data;
    (void)native;
    if (widget->app->syncing) return;
    widget->value = row ? gtk_list_box_row_get_index(row) : -1;
    xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, widget->value);
}

static void scanresults_selection_read(xxwidgets_widget *widget)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget->native));
    GtkTreeModel *model;
    GtkTreeIter iter;
    widget->value = -1;
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        GtkTreePath *path = gtk_tree_model_get_path(model, &iter);
        if (path) {
            widget->value = gtk_tree_path_get_indices(path)[0];
            gtk_tree_path_free(path);
        }
    }
}

static void scanresults_selected(GtkTreeSelection *selection, gpointer data)
{
    xxwidgets_widget *widget = data;
    (void)selection;
    if (widget->app->syncing || !xxwidgets_focusable(widget)) return;
    scanresults_selection_read(widget);
    xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, widget->value);
}

static void scanresults_sync(xxwidgets_widget *widget)
{
    gtk_widget_state *state = widget->platform;
    GtkTreeView *tree = GTK_TREE_VIEW(widget->native);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree);
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(tree));
    int refreshed = state->rendered_hex_revision != xxwidgets_row_revision(widget);
    int previous = widget->value;
    if (refreshed) {
        size_t row;
        gtk_list_store_clear(store);
        for (row = 0; row < widget->item_count; ++row) {
            GtkTreeIter iter;
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter,
                0, xxwidgets_scanresults_cell(widget, row, 0),
                1, xxwidgets_scanresults_cell(widget, row, 1),
                2, xxwidgets_scanresults_cell(widget, row, 2),
                3, xxwidgets_scanresults_cell(widget, row, 3), -1);
        }
        state->rendered_items = widget->item_count;
        state->rendered_hex_revision = xxwidgets_row_revision(widget);
    }
    scanresults_selection_read(widget);
    if (refreshed || widget->value != previous) {
        gtk_tree_selection_unselect_all(selection);
        if (previous >= 0) {
            GtkTreePath *path = gtk_tree_path_new_from_indices(previous, -1);
            gtk_tree_selection_select_path(selection, path);
            gtk_tree_view_set_cursor(tree, path, NULL, FALSE);
            gtk_tree_view_scroll_to_cell(tree, path, NULL, FALSE, 0, 0);
            gtk_tree_path_free(path);
        }
    }
    widget->value = previous;
    atk_object_set_name(gtk_widget_get_accessible(GTK_WIDGET(tree)), widget->text);
}

static void treeview_selection_read(xxwidgets_widget *widget)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    widget->value = -1;
    if (gtk_tree_selection_get_selected(
        gtk_tree_view_get_selection(GTK_TREE_VIEW(widget->native)), &model, &iter))
        gtk_tree_model_get(model, &iter, 1, &widget->value, -1);
}

static void treeview_selection_apply(xxwidgets_widget *widget)
{
    gtk_widget_state *state = widget->platform;
    GtkTreeView *tree = GTK_TREE_VIEW(widget->native);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree);
    int previous = widget->value;
    treeview_selection_read(widget);
    if (widget->value != previous) {
        ++widget->app->syncing;
        gtk_tree_selection_unselect_all(selection);
        if (previous >= 0 && (size_t)previous < state->tree_count) {
            GtkTreeModel *model = gtk_tree_view_get_model(tree);
            GtkTreePath *path = gtk_tree_model_get_path(model, &state->tree_iters[previous]);
            if (path) {
                gtk_tree_selection_select_path(selection, path);
                gtk_tree_view_set_cursor(tree, path, NULL, FALSE);
                gtk_tree_view_scroll_to_cell(tree, path, NULL, FALSE, 0, 0);
                gtk_tree_path_free(path);
            }
        }
        --widget->app->syncing;
    }
    widget->value = previous;
}

static void treeview_selected(GtkTreeSelection *selection, gpointer data)
{
    xxwidgets_widget *widget = data;
    (void)selection;
    if (widget->app->syncing || !xxwidgets_focusable(widget) ||
        ((gtk_widget_state *)widget->platform)->tree_collapsing) return;
    treeview_selection_read(widget);
    xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, widget->value);
}

static gboolean treeview_before_collapse(GtkTreeView *native, GtkTreeIter *iter,
    GtkTreePath *path, gpointer data)
{
    xxwidgets_widget *widget = data;
    gtk_widget_state *state = widget->platform;
    (void)native; (void)iter; (void)path;
    if (!widget->app->syncing) {
        state->tree_collapsing = 1;
        state->tree_before_collapse = widget->value;
    }
    return FALSE;
}

static void treeview_expand_visible(xxwidgets_widget *widget)
{
    gtk_widget_state *state = widget->platform;
    GtkTreeView *tree = GTK_TREE_VIEW(widget->native);
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    size_t node;
    ++widget->app->syncing;
    for (node = 0; node < state->tree_count; ++node) {
        int expanded;
        if (xxwidgets_treeview_row_of_node(widget, node) != SIZE_MAX &&
            xxwidgets_treeview_get_expanded(widget, node, &expanded) == XXWIDGETS_OK && expanded) {
            GtkTreePath *path = gtk_tree_model_get_path(model, &state->tree_iters[node]);
            if (path) { gtk_tree_view_expand_row(tree, path, FALSE); gtk_tree_path_free(path); }
        }
    }
    --widget->app->syncing;
}

static void treeview_expansion(GtkTreeView *native, GtkTreeIter *iter,
    GtkTreePath *path, xxwidgets_widget *widget, int expanded)
{
    gtk_widget_state *state = widget->platform;
    int node = -1;
    if (widget->app->syncing || !xxwidgets_focusable(widget)) return;
    gtk_tree_model_get(gtk_tree_view_get_model(native), iter, 1, &node, -1);
    if (node < 0) return;
    if (!expanded && state->tree_collapsing) widget->value = state->tree_before_collapse;
    state->tree_collapsing = 0;
    if (xxwidgets_treeview_expansion_input(widget, (size_t)node, expanded) != XXWIDGETS_OK) {
        ++widget->app->syncing;
        if (expanded) gtk_tree_view_collapse_row(native, path);
        else gtk_tree_view_expand_row(native, path, FALSE);
        --widget->app->syncing;
        if (!expanded) treeview_expand_visible(widget);
        treeview_selection_apply(widget);
        return;
    }
    if (expanded) treeview_expand_visible(widget);
    state->rendered_hex_revision = widget->tree_revision;
    treeview_selection_apply(widget);
    xxwidgets_emit(widget, XXWIDGETS_EVENT_CHANGE, node);
}

static void treeview_expanded(GtkTreeView *native, GtkTreeIter *iter, GtkTreePath *path, gpointer data)
{
    treeview_expansion(native, iter, path, data, 1);
}

static void treeview_collapsed(GtkTreeView *native, GtkTreeIter *iter, GtkTreePath *path, gpointer data)
{
    treeview_expansion(native, iter, path, data, 0);
}

static gboolean treeview_key(GtkWidget *native, GdkEventKey *key, gpointer data)
{
    xxwidgets_widget *widget = data;
    int expanded;
    (void)native;
    if (widget->app->syncing || !xxwidgets_focusable(widget) || widget->value < 0) return FALSE;
    if (key->keyval == GDK_KEY_Return || key->keyval == GDK_KEY_KP_Enter) {
        xxwidgets_emit(widget, XXWIDGETS_EVENT_ACTIVATE, widget->value);
        return TRUE;
    }
    if (key->keyval == GDK_KEY_space) {
        if (xxwidgets_treeview_has_children(widget, (size_t)widget->value) &&
            xxwidgets_treeview_get_expanded(widget, (size_t)widget->value, &expanded) == XXWIDGETS_OK)
            xxwidgets_treeview_user_expand(widget, (size_t)widget->value, !expanded);
        else xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, widget->value);
        return TRUE;
    }
    return FALSE;
}

static void treeview_activated(GtkTreeView *native, GtkTreePath *path,
    GtkTreeViewColumn *column, gpointer data)
{
    xxwidgets_widget *widget = data;
    GtkTreeIter iter;
    int node, expanded;
    (void)column;
    if (widget->app->syncing || !xxwidgets_focusable(widget)) return;
    if (!gtk_tree_model_get_iter(gtk_tree_view_get_model(native), &iter, path)) return;
    gtk_tree_model_get(gtk_tree_view_get_model(native), &iter, 1, &node, -1);
    if (node < 0) return;
    if (xxwidgets_treeview_has_children(widget, (size_t)node) &&
        xxwidgets_treeview_get_expanded(widget, (size_t)node, &expanded) == XXWIDGETS_OK)
        xxwidgets_treeview_user_expand(widget, (size_t)node, !expanded);
    else xxwidgets_emit(widget, XXWIDGETS_EVENT_ACTIVATE, node);
}

static xxwidgets_status treeview_sync(xxwidgets_widget *widget)
{
    gtk_widget_state *state = widget->platform;
    GtkTreeView *tree = GTK_TREE_VIEW(widget->native);
    GtkTreeStore *store = GTK_TREE_STORE(gtk_tree_view_get_model(tree));
    int refreshed = state->tree_content_revision != widget->tree_content_revision;
    size_t count = xxwidgets_treeview_count(widget), node;
    if (refreshed) {
        if (count > SIZE_MAX / sizeof(GtkTreeIter)) return XXWIDGETS_OUT_OF_MEMORY;
        GtkTreeIter *iters = count ? malloc(count * sizeof(*iters)) : NULL;
        if (count && !iters) return XXWIDGETS_OUT_OF_MEMORY;
        gtk_tree_store_clear(store);
        for (node = 0; node < count; ++node) {
            xxwidgets_tree_node entry;
            xxwidgets_treeview_get_node(widget, node, &entry);
            gtk_tree_store_append(store, &iters[node], entry.parent == SIZE_MAX ? NULL : &iters[entry.parent]);
            gtk_tree_store_set(store, &iters[node], 0, xxwidgets_treeview_display_text(widget, node),
                1, (int)node, -1);
        }
        free(state->tree_iters);
        state->tree_iters = iters;
        state->tree_count = count;
        state->tree_content_revision = widget->tree_content_revision;
    }
    if (refreshed || state->rendered_hex_revision != widget->tree_revision) {
        gtk_tree_view_collapse_all(tree);
        treeview_expand_visible(widget);
        state->rendered_hex_revision = widget->tree_revision;
    }
    treeview_selection_apply(widget);
    atk_object_set_name(gtk_widget_get_accessible(GTK_WIDGET(tree)), widget->text);
    return XXWIDGETS_OK;
}

static void browser_selection_read(xxwidgets_widget *widget)
{
    GtkTreeView *tree = GTK_TREE_VIEW(widget->native);
    GList *paths = gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(tree), NULL), *item;
    GtkTreePath *cursor = NULL;
    widget->value = -1;
    xxwidgets_archivebrowser_selection_clear(widget);
    for (item = paths; item; item = item->next) {
        int row = gtk_tree_path_get_indices(item->data)[0];
        xxwidgets_archivebrowser_selection_input(widget, (size_t)row, 1);
        if (widget->value < 0) widget->value = row;
    }
    g_list_free_full(paths, (GDestroyNotify)gtk_tree_path_free);
    gtk_tree_view_get_cursor(tree, &cursor, NULL);
    if (cursor) {
        int row = gtk_tree_path_get_indices(cursor)[0];
        if (xxwidgets_archivebrowser_row_selected(widget, (size_t)row)) widget->value = row;
        gtk_tree_path_free(cursor);
    }
}

static void browser_selected(GtkTreeSelection *selection, gpointer data)
{
    xxwidgets_widget *widget = data;
    (void)selection;
    if (widget->app->syncing) return;
    browser_selection_read(widget);
    xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, widget->value);
}

static void browser_activated(GtkTreeView *native, GtkTreePath *path,
                              GtkTreeViewColumn *column, gpointer data)
{
    xxwidgets_widget *widget = data;
    (void)native; (void)column;
    if (!widget->app->syncing)
        xxwidgets_archivebrowser_user_activate(widget, gtk_tree_path_get_indices(path)[0]);
}

static void browser_up_clicked(GtkButton *native, gpointer data)
{
    xxwidgets_widget *widget = data;
    (void)native;
    if (!widget->app->syncing) xxwidgets_archivebrowser_user_up(widget);
}

static void browser_context_at(xxwidgets_widget *widget, int x, int y)
{
    gtk_widget_state *state = widget->platform;
    int local_x = x, local_y = y;
    if (!xxwidgets_focusable(widget) || widget->app->syncing) return;
    gtk_widget_translate_coordinates(GTK_WIDGET(widget->native), state->root,
        x, y, &local_x, &local_y);
    xxwidgets_emit_context(widget, widget->value, local_x, local_y);
}

static gboolean browser_popup_menu(GtkWidget *native, gpointer data)
{
    xxwidgets_widget *widget = data;
    GtkTreeView *tree = GTK_TREE_VIEW(native);
    int x = 8, y = 8;
    if (!xxwidgets_focusable(widget) || widget->app->syncing) return TRUE;
    if (widget->value >= 0 && (size_t)widget->value < widget->item_count) {
        GtkTreePath *path = gtk_tree_path_new_from_indices(widget->value, -1);
        GdkRectangle area;
        gtk_tree_view_scroll_to_cell(tree, path, NULL, FALSE, 0, 0);
        gtk_tree_view_get_cell_area(tree, path, gtk_tree_view_get_column(tree, 0), &area);
        x = area.x + 16; y = area.y + area.height / 2;
        gtk_tree_path_free(path);
    }
    gtk_tree_view_convert_bin_window_to_widget_coords(tree, x, y, &x, &y);
    browser_context_at(widget, x, y);
    return TRUE;
}

static gboolean browser_button_press(GtkWidget *native, GdkEventButton *event, gpointer data)
{
    xxwidgets_widget *widget = data;
    GtkTreeView *tree = GTK_TREE_VIEW(native);
    GtkTreeSelection *selection;
    GtkTreePath *path = NULL;
    int x = (int)event->x, y = (int)event->y;
    int bin_x = x, bin_y = y;
    if (event->button != 3) return FALSE;
    if (!xxwidgets_focusable(widget) || widget->app->syncing) return TRUE;
    if (event->window == gtk_tree_view_get_bin_window(tree))
        gtk_tree_view_convert_bin_window_to_widget_coords(tree, x, y, &x, &y);
    else gtk_tree_view_convert_widget_to_bin_window_coords(tree, x, y, &bin_x, &bin_y);
    selection = gtk_tree_view_get_selection(tree);
    /* Select before dispatch without exposing an intermediate callback that
     * could replace the browser model while its hit-test path is borrowed. */
    ++widget->app->syncing;
    if (gtk_tree_view_get_path_at_pos(tree, bin_x, bin_y, &path, NULL, NULL, NULL)) {
        if (!gtk_tree_selection_path_is_selected(selection, path)) gtk_tree_selection_unselect_all(selection);
        gtk_tree_selection_select_path(selection, path);
        widget->value = gtk_tree_path_get_indices(path)[0];
        gtk_tree_path_free(path);
    } else {
        gtk_tree_selection_unselect_all(selection);
        widget->value = -1;
    }
    --widget->app->syncing;
    {
        int clicked = widget->value;
        browser_selection_read(widget);
        widget->value = clicked;
    }
    xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, widget->value);
    gtk_widget_grab_focus(native);
    browser_context_at(widget, x, y);
    return TRUE;
}

static gboolean window_shortcut(GtkWidget *native, GdkEventKey *event, gpointer data)
{
    unsigned int key = gdk_keyval_to_upper(event->keyval), modifiers = 0;
    (void)native;
    if (key >= GDK_KEY_F1 && key <= GDK_KEY_F24) key = XXWIDGETS_KEY_F1 + key - GDK_KEY_F1;
    else switch (key) {
    case GDK_KEY_Escape: key = XXWIDGETS_KEY_ESCAPE; break;
    case GDK_KEY_Return: case GDK_KEY_KP_Enter: key = XXWIDGETS_KEY_ENTER; break;
    case GDK_KEY_Tab: case GDK_KEY_ISO_Left_Tab: key = XXWIDGETS_KEY_TAB; break;
    case GDK_KEY_BackSpace: key = XXWIDGETS_KEY_BACKSPACE; break;
    case GDK_KEY_Delete: key = XXWIDGETS_KEY_DELETE; break;
    case GDK_KEY_Insert: key = XXWIDGETS_KEY_INSERT; break;
    case GDK_KEY_Home: key = XXWIDGETS_KEY_HOME; break;
    case GDK_KEY_End: key = XXWIDGETS_KEY_END; break;
    case GDK_KEY_Page_Up: key = XXWIDGETS_KEY_PAGEUP; break;
    case GDK_KEY_Page_Down: key = XXWIDGETS_KEY_PAGEDOWN; break;
    case GDK_KEY_Up: key = XXWIDGETS_KEY_UP; break;
    case GDK_KEY_Down: key = XXWIDGETS_KEY_DOWN; break;
    case GDK_KEY_Left: key = XXWIDGETS_KEY_LEFT; break;
    case GDK_KEY_Right: key = XXWIDGETS_KEY_RIGHT; break;
    default: key = gdk_keyval_to_unicode(key); break;
    }
    if (event->state & GDK_CONTROL_MASK) modifiers |= XXWIDGETS_MOD_CTRL;
    if (event->state & GDK_MOD1_MASK) modifiers |= XXWIDGETS_MOD_ALT;
    if (event->state & GDK_SHIFT_MASK) modifiers |= XXWIDGETS_MOD_SHIFT;
    if (event->state & (GDK_SUPER_MASK | GDK_META_MASK)) modifiers |= XXWIDGETS_MOD_META;
    return xxwidgets_shortcut_dispatch(data, key, modifiers);
}

static gboolean browser_key(GtkWidget *native, GdkEventKey *event, gpointer data)
{
    xxwidgets_widget *widget = data;
    if (event->keyval == GDK_KEY_Menu ||
        (event->keyval == GDK_KEY_F10 && (event->state & GDK_SHIFT_MASK)))
        return browser_popup_menu(native, data);
    if (event->keyval == GDK_KEY_BackSpace && !widget->app->syncing) {
        xxwidgets_archivebrowser_user_up(widget);
        return TRUE;
    }
    return FALSE;
}

static void browser_sort_clicked(GtkTreeViewColumn *column, gpointer data)
{
    xxwidgets_widget *widget = data;
    if (!widget->app->syncing)
        xxwidgets_archivebrowser_user_sort(widget,
            (xxwidgets_archive_column)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column), "xxwidgets-column")));
}

static void browser_sync(xxwidgets_widget *widget)
{
    gtk_widget_state *state = widget->platform;
    GtkTreeView *tree = GTK_TREE_VIEW(widget->native);
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(tree));
    const char *directory = xxwidgets_archivebrowser_directory(widget);
    char *address = g_strdup_printf("%s:/%s", xxwidgets_archivebrowser_archive(widget), directory);
    size_t row;
    int column;
    int columns = (int)xxwidgets_archivebrowser_column_count(widget);
    if (address) { gtk_entry_set_text(GTK_ENTRY(state->browser_address), address); g_free(address); }
    gtk_widget_set_sensitive(state->browser_up, directory[0] != '\0');
    if (state->rendered_hex_revision != widget->browser_revision) {
        GType *types = g_new(GType, columns + 1);
        for (column = 0; column <= columns; ++column) types[column] = G_TYPE_STRING;
        store = gtk_list_store_newv(columns + 1, types);
        g_free(types);
        gtk_tree_view_set_model(tree, GTK_TREE_MODEL(store));
        g_object_unref(store);
        while (gtk_tree_view_get_n_columns(tree) > 5)
            gtk_tree_view_remove_column(tree, gtk_tree_view_get_column(tree, 5));
        for (column = 5; column < columns; ++column) {
            GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
            GtkTreeViewColumn *view_column = gtk_tree_view_column_new_with_attributes(
                xxwidgets_archivebrowser_column_title(widget, (size_t)column), renderer, "text", column + 1, NULL);
            gtk_tree_view_column_set_resizable(view_column, TRUE);
            gtk_tree_view_column_set_clickable(view_column, TRUE);
            gtk_tree_view_column_set_min_width(view_column, 100);
            g_object_set_data(G_OBJECT(view_column), "xxwidgets-column", GINT_TO_POINTER(column));
            g_signal_connect(view_column, "clicked", G_CALLBACK(browser_sort_clicked), widget);
            gtk_tree_view_append_column(tree, view_column);
        }
        gtk_list_store_clear(store);
        for (row = 0; row < widget->item_count; ++row) {
            GtkTreeIter iter;
            xxwidgets_archive_browser_entry entry;
            size_t source;
            if (xxwidgets_archivebrowser_get_entry(widget, row, &source, &entry) != XXWIDGETS_OK) continue;
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, entry.is_directory ? "folder" : "text-x-generic",
                1, xxwidgets_archivebrowser_cell(widget, row, XXWIDGETS_ARCHIVE_COLUMN_NAME),
                2, xxwidgets_archivebrowser_cell(widget, row, XXWIDGETS_ARCHIVE_COLUMN_SIZE),
                3, xxwidgets_archivebrowser_cell(widget, row, XXWIDGETS_ARCHIVE_COLUMN_PACKED_SIZE),
                4, xxwidgets_archivebrowser_cell(widget, row, XXWIDGETS_ARCHIVE_COLUMN_MODIFIED),
                5, xxwidgets_archivebrowser_cell(widget, row, XXWIDGETS_ARCHIVE_COLUMN_ATTRIBUTES), -1);
            for (column = 5; column < columns; ++column)
                gtk_list_store_set(store, &iter, column + 1,
                    xxwidgets_archivebrowser_cell(widget, row, (xxwidgets_archive_column)column), -1);
        }
        state->rendered_hex_revision = widget->browser_revision;
    }
    for (column = 0; column < columns; ++column) {
        GtkTreeViewColumn *native_column = gtk_tree_view_get_column(tree, column);
        gtk_tree_view_column_set_sort_indicator(native_column,
            column == xxwidgets_archivebrowser_sort_column(widget));
        gtk_tree_view_column_set_sort_order(native_column,
            xxwidgets_archivebrowser_sort_descending(widget) ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
    }
    {
        GtkTreeSelection *selection = gtk_tree_view_get_selection(tree);
        size_t i;
        gtk_tree_selection_unselect_all(selection);
        for (i = 0; i < widget->item_count; ++i) if (xxwidgets_archivebrowser_row_selected(widget, i)) {
            GtkTreePath *path = gtk_tree_path_new_from_indices((int)i, -1);
            gtk_tree_selection_select_path(selection, path);
            gtk_tree_path_free(path);
        }
        if (widget->value >= 0) {
            GtkTreePath *path = gtk_tree_path_new_from_indices(widget->value, -1);
            gtk_tree_selection_select_path(selection, path);
            gtk_tree_view_scroll_to_cell(tree, path, NULL, FALSE, 0, 0);
            gtk_tree_path_free(path);
        }
    }
    atk_object_set_name(gtk_widget_get_accessible(GTK_WIDGET(tree)), widget->text);
}

static void hex_allocated(GtkWidget *native, GtkAllocation *allocation, gpointer data)
{
    xxwidgets_widget *widget = data;
    gtk_widget_state *state = widget->platform;
    GtkListBoxRow *row;
    GtkAllocation row_rect;
    GtkAdjustment *adjustment;
    double top, page, bottom;
    (void)allocation;
    if (!state || !state->scroll_selection || widget->value < 0) return;
    row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(native), widget->value);
    if (!row || !gtk_widget_get_mapped(GTK_WIDGET(row))) return;
    gtk_widget_get_allocation(GTK_WIDGET(row), &row_rect);
    if (row_rect.height <= 1) return;
    adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(state->root));
    top = gtk_adjustment_get_value(adjustment);
    page = gtk_adjustment_get_page_size(adjustment);
    bottom = row_rect.y + row_rect.height;
    if (row_rect.y < top) top = row_rect.y;
    else if (bottom > top + page) top = bottom - page;
    gtk_adjustment_set_value(adjustment, top);
    state->scroll_selection = 0;
}

static xxwidgets_status gtk_init_backend(xxwidgets_app *app)
{
    gtk_app_state *state;
    GtkWidget *probe;
    PangoContext *context;
    PangoFontMetrics *metrics;
    if (!gtk_init_check(NULL, NULL)) return XXWIDGETS_UNAVAILABLE;
    state = calloc(1, sizeof(*state));
    if (!state) return XXWIDGETS_OUT_OF_MEMORY;
    state->cell_width = 8;
    state->cell_height = 20;
    probe = gtk_label_new("M");
    g_object_ref_sink(probe);
    context = gtk_widget_get_pango_context(probe);
    metrics = pango_context_get_metrics(context, pango_context_get_font_description(context),
                                        pango_context_get_language(context));
    if (metrics) {
        int width = PANGO_PIXELS_CEIL(pango_font_metrics_get_approximate_char_width(metrics));
        int height = PANGO_PIXELS_CEIL(pango_font_metrics_get_ascent(metrics) +
                                      pango_font_metrics_get_descent(metrics)) + 4;
        if (width > 0) state->cell_width = width;
        if (height > 0) state->cell_height = height;
        pango_font_metrics_unref(metrics);
    }
    gtk_widget_destroy(probe);
    g_object_unref(probe);
    app->platform = state;
    return XXWIDGETS_OK;
}

static void gtk_shutdown_backend(xxwidgets_app *app)
{
    free(app->platform);
    app->platform = NULL;
}

static gboolean poll_timeout(gpointer data)
{
    *(int *)data = 1;
    return G_SOURCE_REMOVE;
}

static xxwidgets_status gtk_poll_backend(xxwidgets_app *app, int timeout_ms)
{
    GSource *timer = NULL;
    gint64 deadline = g_get_monotonic_time() + (gint64)timeout_ms * 1000;
    int expired = 0;
    unsigned processed = 0;
    if (timeout_ms > 0) {
        timer = g_timeout_source_new((guint)timeout_ms);
        g_source_set_callback(timer, poll_timeout, &expired, NULL);
        g_source_attach(timer, NULL);
    }
    /* Keep continuously busy native event queues from monopolizing the caller. */
    while (!app->quit && processed < 256) {
        if (g_main_context_iteration(NULL, FALSE)) {
            ++processed;
            if (timeout_ms > 0 && g_get_monotonic_time() >= deadline) break;
            continue;
        }
        if (processed || timeout_ms == 0 || expired ||
            g_get_monotonic_time() >= deadline) break;
        if (g_main_context_iteration(NULL, TRUE)) ++processed;
    }
    if (timer) {
        g_source_destroy(timer);
        g_source_unref(timer);
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status gtk_sync_backend(xxwidgets_widget *widget);
static void gtk_destroy_backend(xxwidgets_widget *widget);

static xxwidgets_status gtk_create_backend(xxwidgets_widget *widget)
{
    GtkWidget *native = NULL;
    gtk_widget_state *state;
    xxwidgets_status status;
    if (!g_utf8_validate(widget->text, -1, NULL)) return XXWIDGETS_INVALID_ARGUMENT;
    state = calloc(1, sizeof(*state));
    if (!state) return XXWIDGETS_OUT_OF_MEMORY;
    widget->platform = state;
    switch (widget->kind) {
    case XXWIDGETS_WINDOW:
        native = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        state->content = gtk_fixed_new();
        gtk_container_add(GTK_CONTAINER(native), state->content);
        gtk_widget_show(state->content);
        g_signal_connect(native, "delete-event", G_CALLBACK(window_close), widget);
        g_signal_connect(native, "key-press-event", G_CALLBACK(window_shortcut), widget);
        g_signal_connect(native, "configure-event", G_CALLBACK(window_configure), widget);
        break;
    case XXWIDGETS_LABEL:
        native = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(native), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(native), PANGO_ELLIPSIZE_END);
        break;
    case XXWIDGETS_BUTTON:
        native = gtk_button_new();
        g_signal_connect(native, "clicked", G_CALLBACK(button_clicked), widget);
        break;
    case XXWIDGETS_EDIT:
        native = gtk_entry_new();
        g_signal_connect(native, "changed", G_CALLBACK(edit_changed), widget);
        break;
    case XXWIDGETS_CHECKBOX:
        native = gtk_check_button_new();
        g_signal_connect(native, "toggled", G_CALLBACK(checkbox_changed), widget);
        break;
    case XXWIDGETS_COMBOBOX:
        native = gtk_combo_box_text_new();
        g_signal_connect(native, "changed", G_CALLBACK(combo_changed), widget);
        break;
    case XXWIDGETS_CHECKCOMBOBOX: {
        GtkWidget *popover, *scroll;
        native = gtk_menu_button_new();
        popover = gtk_popover_new(native);
        scroll = gtk_scrolled_window_new(NULL, NULL);
        state->combo_scroll = scroll;
        state->combo_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_container_add(GTK_CONTAINER(scroll), state->combo_box);
        gtk_container_add(GTK_CONTAINER(popover), scroll);
        gtk_menu_button_set_popover(GTK_MENU_BUTTON(native), popover);
        gtk_widget_show_all(scroll);
        break;
    }
    case XXWIDGETS_LISTBOX:
    case XXWIDGETS_ARCHIVEVIEW:
    case XXWIDGETS_HEXVIEW:
        native = gtk_list_box_new();
        gtk_list_box_set_selection_mode(GTK_LIST_BOX(native), GTK_SELECTION_SINGLE);
        state->root = gtk_scrolled_window_new(NULL, NULL);
        g_object_ref_sink(state->root);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(state->root),
                                       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(state->root), GTK_SHADOW_IN);
        gtk_container_add(GTK_CONTAINER(state->root), native);
        gtk_widget_show(native);
        g_signal_connect(native, "row-selected", G_CALLBACK(list_selected), widget);
        if (xxwidgets_formatted_rows(widget))
            g_signal_connect(native, "size-allocate", G_CALLBACK(hex_allocated), widget);
        break;
    case XXWIDGETS_PROGRESS:
        native = gtk_progress_bar_new();
        break;
    case XXWIDGETS_TREEVIEW: {
        GtkTreeStore *store = gtk_tree_store_new(2, G_TYPE_STRING, G_TYPE_INT);
        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("", renderer, "text", 0, NULL);
        native = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
        g_object_unref(store);
        gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(native), FALSE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(native), column);
        g_object_set(renderer, "single-paragraph-mode", TRUE, NULL);
        gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(native)), GTK_SELECTION_SINGLE);
        state->root = gtk_scrolled_window_new(NULL, NULL);
        g_object_ref_sink(state->root);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(state->root), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(state->root), GTK_SHADOW_IN);
        gtk_container_add(GTK_CONTAINER(state->root), native);
        g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(native)), "changed", G_CALLBACK(treeview_selected), widget);
        g_signal_connect(native, "row-expanded", G_CALLBACK(treeview_expanded), widget);
        g_signal_connect(native, "row-collapsed", G_CALLBACK(treeview_collapsed), widget);
        g_signal_connect(native, "test-collapse-row", G_CALLBACK(treeview_before_collapse), widget);
        g_signal_connect(native, "key-press-event", G_CALLBACK(treeview_key), widget);
        g_signal_connect(native, "row-activated", G_CALLBACK(treeview_activated), widget);
        gtk_widget_show(native);
        break;
    }
    case XXWIDGETS_SCANRESULTS: {
        static const char *titles[] = {"Type", "Name", "Version", "Info"};
        static const int widths[] = {100, 220, 100, 260};
        GtkListStore *store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING,
            G_TYPE_STRING, G_TYPE_STRING);
        int column;
        native = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
        g_object_unref(store);
        gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(native)), GTK_SELECTION_SINGLE);
        state->root = gtk_scrolled_window_new(NULL, NULL);
        g_object_ref_sink(state->root);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(state->root),
            GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(state->root), GTK_SHADOW_IN);
        gtk_container_add(GTK_CONTAINER(state->root), native);
        for (column = 0; column < 4; ++column) {
            GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
            GtkTreeViewColumn *view_column = gtk_tree_view_column_new_with_attributes(
                titles[column], renderer, "text", column, NULL);
            gtk_tree_view_column_set_resizable(view_column, TRUE);
            gtk_tree_view_column_set_sizing(view_column, GTK_TREE_VIEW_COLUMN_FIXED);
            gtk_tree_view_column_set_fixed_width(view_column, widths[column]);
            gtk_tree_view_column_set_min_width(view_column, 60);
            g_object_set(renderer, "single-paragraph-mode", TRUE, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
            gtk_tree_view_append_column(GTK_TREE_VIEW(native), view_column);
        }
        g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(native)), "changed",
            G_CALLBACK(scanresults_selected), widget);
        gtk_widget_show(native);
        break;
    }
    case XXWIDGETS_ARCHIVEBROWSER: {
        static const char *titles[] = {"Name", "Size", "Packed Size", "Modified", "Attributes"};
        GtkListStore *store = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_STRING,
            G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
        GtkWidget *address_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        int column;
        native = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
        g_object_unref(store);
        gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(native)), GTK_SELECTION_MULTIPLE);
        state->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        g_object_ref_sink(state->root);
        state->browser_up = gtk_button_new_from_icon_name("go-up", GTK_ICON_SIZE_BUTTON);
        gtk_widget_set_tooltip_text(state->browser_up, "Up one folder (Backspace)");
        state->browser_address = gtk_entry_new();
        gtk_editable_set_editable(GTK_EDITABLE(state->browser_address), FALSE);
        gtk_box_pack_start(GTK_BOX(address_bar), state->browser_up, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(address_bar), state->browser_address, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(state->root), address_bar, FALSE, FALSE, 0);
        state->browser_scroll = gtk_scrolled_window_new(NULL, NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(state->browser_scroll),
            GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(state->browser_scroll), GTK_SHADOW_IN);
        gtk_container_add(GTK_CONTAINER(state->browser_scroll), native);
        gtk_box_pack_start(GTK_BOX(state->root), state->browser_scroll, TRUE, TRUE, 0);
        for (column = 0; column < 5; ++column) {
            GtkTreeViewColumn *view_column = gtk_tree_view_column_new();
            GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
            gtk_tree_view_column_set_title(view_column, titles[column]);
            if (!column) {
                GtkCellRenderer *icon = gtk_cell_renderer_pixbuf_new();
                gtk_tree_view_column_pack_start(view_column, icon, FALSE);
                gtk_tree_view_column_add_attribute(view_column, icon, "icon-name", 0);
            } else if (column == 1 || column == 2) g_object_set(renderer, "xalign", 1.0f, NULL);
            gtk_tree_view_column_pack_start(view_column, renderer, TRUE);
            gtk_tree_view_column_add_attribute(view_column, renderer, "text", column + 1);
            gtk_tree_view_column_set_resizable(view_column, TRUE);
            gtk_tree_view_column_set_clickable(view_column, TRUE);
            gtk_tree_view_column_set_min_width(view_column, column ? 80 : 180);
            g_object_set_data(G_OBJECT(view_column), "xxwidgets-column", GINT_TO_POINTER(column));
            g_signal_connect(view_column, "clicked", G_CALLBACK(browser_sort_clicked), widget);
            gtk_tree_view_append_column(GTK_TREE_VIEW(native), view_column);
        }
        g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(native)), "changed", G_CALLBACK(browser_selected), widget);
        g_signal_connect(native, "row-activated", G_CALLBACK(browser_activated), widget);
        g_signal_connect(native, "key-press-event", G_CALLBACK(browser_key), widget);
        g_signal_connect(native, "button-press-event", G_CALLBACK(browser_button_press), widget);
        g_signal_connect(native, "popup-menu", G_CALLBACK(browser_popup_menu), widget);
        g_signal_connect(state->browser_up, "clicked", G_CALLBACK(browser_up_clicked), widget);
        gtk_widget_show_all(state->root);
        break;
    }
    default:
        free(state);
        widget->platform = NULL;
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    g_object_ref_sink(native);
    widget->native = native;
    if (!state->root) state->root = native;
    if (widget->parent) {
        gtk_widget_state *parent_state = widget->parent->platform;
        gtk_fixed_put(GTK_FIXED(parent_state->content), state->root, 0, 0);
    }
    status = gtk_sync_backend(widget);
    if (status != XXWIDGETS_OK) gtk_destroy_backend(widget);
    return status;
}

static void gtk_destroy_backend(xxwidgets_widget *widget)
{
    GtkWidget *native = widget->native;
    gtk_widget_state *state = widget->platform;
    if (!state) return;
    if (native) g_signal_handlers_disconnect_by_data(native, widget);
    if ((widget->kind == XXWIDGETS_SCANRESULTS || widget->kind == XXWIDGETS_TREEVIEW) && native)
        g_signal_handlers_disconnect_by_data(gtk_tree_view_get_selection(GTK_TREE_VIEW(native)), widget);
    if (widget->kind == XXWIDGETS_ARCHIVEBROWSER && native) {
        g_signal_handlers_disconnect_by_data(gtk_tree_view_get_selection(GTK_TREE_VIEW(native)), widget);
        g_signal_handlers_disconnect_by_data(state->browser_up, widget);
    }
    if (state->root) {
        gtk_widget_destroy(state->root);
        if (state->root != native) g_object_unref(state->root);
    }
    if (native) g_object_unref(native);
    free(state->tree_iters);
    free(state);
    widget->platform = NULL;
    widget->native = NULL;
}

static xxwidgets_status gtk_sync_backend(xxwidgets_widget *widget)
{
    GtkWidget *native = widget->native;
    gtk_widget_state *state = widget->platform;
    int x, y, width, height;
    size_t i;
    if (!g_utf8_validate(widget->text, -1, NULL) ||
        !pixel_rect(widget, &x, &y, &width, &height)) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < widget->item_count; ++i)
        if (!g_utf8_validate(widget->items[i], -1, NULL)) return XXWIDGETS_INVALID_ARGUMENT;
    if (!state->has_rect || !rect_equal(state->applied_rect, widget->rect)) {
        if (widget->kind == XXWIDGETS_WINDOW) {
            gtk_window_resize(GTK_WINDOW(native), width, height);
            gtk_window_move(GTK_WINDOW(native), x, y);
        } else {
            gtk_widget_state *parent_state = widget->parent->platform;
            gtk_fixed_move(GTK_FIXED(parent_state->content), state->root, x, y);
            gtk_widget_set_size_request(state->root, width, height);
        }
        state->applied_rect = widget->rect;
        state->has_rect = 1;
    }
    switch (widget->kind) {
    case XXWIDGETS_WINDOW:
        gtk_window_set_title(GTK_WINDOW(native), widget->text);
        break;
    case XXWIDGETS_LABEL:
        gtk_label_set_text(GTK_LABEL(native), widget->text);
        break;
    case XXWIDGETS_COMBOBOX:
        if (!state->combo_initialized || state->combo_revision != widget->combo_revision) {
            size_t index;
            gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(native));
            for (index = 0; index < widget->item_count; ++index)
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(native), widget->items[index]);
            state->combo_initialized = 1; state->combo_revision = widget->combo_revision;
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(native), widget->value);
        break;
    case XXWIDGETS_CHECKCOMBOBOX: {
        GList *rows, *row;
        size_t index = 0;
        char *caption = g_strconcat(xxwidgets_combobox_caption(widget), " \xe2\x96\xbe", NULL);
        gtk_button_set_label(GTK_BUTTON(native), caption); g_free(caption);
        if (!state->combo_initialized || state->combo_revision != widget->combo_revision) {
            rows = gtk_container_get_children(GTK_CONTAINER(state->combo_box));
            for (row = rows; row; row = row->next) gtk_widget_destroy(GTK_WIDGET(row->data));
            g_list_free(rows);
            for (index = 0; index < widget->item_count; ++index) {
                GtkWidget *check = gtk_check_button_new_with_label(widget->items[index]);
                g_object_set_data(G_OBJECT(check), "xxwidgets-index", GUINT_TO_POINTER((guint)index));
                g_signal_connect(check, "toggled", G_CALLBACK(combo_checked), widget);
                gtk_box_pack_start(GTK_BOX(state->combo_box), check, FALSE, FALSE, 0);
                gtk_widget_show(check);
            }
            state->combo_initialized = 1; state->combo_revision = widget->combo_revision;
        }
        rows = gtk_container_get_children(GTK_CONTAINER(state->combo_box)); index = 0;
        for (row = rows; row; row = row->next, ++index)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(row->data), xxwidgets_checkcombobox_checked(widget, index));
        g_list_free(rows);
        gtk_widget_set_size_request(state->combo_scroll,
            24 * ((gtk_app_state *)widget->app->platform)->cell_width,
            (int)(widget->item_count < 10 ? widget->item_count : 10) * ((gtk_app_state *)widget->app->platform)->cell_height + 8);
        if (!widget->visible || !widget->enabled || !widget->parent->enabled || !widget->item_count)
            gtk_widget_hide(GTK_WIDGET(gtk_menu_button_get_popover(GTK_MENU_BUTTON(native))));
        break;
    }
    case XXWIDGETS_BUTTON:
        gtk_button_set_label(GTK_BUTTON(native), widget->text);
        break;
    case XXWIDGETS_EDIT:
        /* Setting identical text still moves the caret in many native controls. */
        if (strcmp(gtk_entry_get_text(GTK_ENTRY(native)), widget->text))
            gtk_entry_set_text(GTK_ENTRY(native), widget->text);
        break;
    case XXWIDGETS_CHECKBOX:
        gtk_button_set_label(GTK_BUTTON(native), widget->text);
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(native)) != (widget->value != 0))
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(native), widget->value != 0);
        break;
    case XXWIDGETS_LISTBOX:
    case XXWIDGETS_ARCHIVEVIEW:
    case XXWIDGETS_HEXVIEW:
        if (state->rendered_items > widget->item_count ||
            (xxwidgets_formatted_rows(widget) &&
             state->rendered_hex_revision != xxwidgets_row_revision(widget))) {
            GList *rows = gtk_container_get_children(GTK_CONTAINER(native));
            GList *row;
            for (row = rows; row; row = row->next) gtk_widget_destroy(GTK_WIDGET(row->data));
            g_list_free(rows);
            state->rendered_items = 0;
            state->rendered_hex_revision = xxwidgets_row_revision(widget);
            state->scroll_selection = widget->value >= 0;
        }
        while (state->rendered_items < widget->item_count) {
            GtkWidget *label = gtk_label_new(widget->items[state->rendered_items]);
            gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
            if (xxwidgets_formatted_rows(widget)) {
                PangoAttrList *attributes = pango_attr_list_new();
                pango_attr_list_insert(attributes, pango_attr_family_new("monospace"));
                gtk_label_set_attributes(GTK_LABEL(label), attributes);
                gtk_label_set_single_line_mode(GTK_LABEL(label), TRUE);
                pango_attr_list_unref(attributes);
            } else gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
            gtk_widget_set_margin_start(label, 4);
            gtk_widget_set_margin_end(label, 4);
            gtk_container_add(GTK_CONTAINER(native), label);
            gtk_widget_show_all(label);
            /* The GtkListBox wraps the child in a row, which must also be shown. */
            gtk_widget_show(gtk_widget_get_parent(label));
            ++state->rendered_items;
        }
        {
            GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(native));
            int selected = row ? gtk_list_box_row_get_index(row) : -1;
            if (selected != widget->value) {
                gtk_list_box_select_row(GTK_LIST_BOX(native), widget->value < 0 ? NULL :
                    gtk_list_box_get_row_at_index(GTK_LIST_BOX(native), widget->value));
                if (xxwidgets_formatted_rows(widget)) {
                    state->scroll_selection = widget->value >= 0;
                    gtk_widget_queue_resize(native);
                }
            }
        }
        atk_object_set_name(gtk_widget_get_accessible(native), widget->text);
        break;
    case XXWIDGETS_PROGRESS:
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(native), widget->value / 100.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(native), widget->text);
        gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(native), widget->text[0] != '\0');
        break;
    case XXWIDGETS_ARCHIVEBROWSER:
        browser_sync(widget);
        break;
    case XXWIDGETS_SCANRESULTS:
        scanresults_sync(widget);
        break;
    case XXWIDGETS_TREEVIEW: {
        xxwidgets_status status = treeview_sync(widget);
        if (status != XXWIDGETS_OK) return status;
        break;
    }
    }
    gtk_widget_set_sensitive(state->root, widget->enabled != 0);
    if (widget->kind == XXWIDGETS_WINDOW && (!widget->enabled || !widget->visible)) {
        xxwidgets_widget *child;
        for (child = widget->app->widgets; child; child = child->next)
            if (child->parent == widget && child->kind == XXWIDGETS_CHECKCOMBOBOX)
                gtk_widget_hide(GTK_WIDGET(gtk_menu_button_get_popover(GTK_MENU_BUTTON(child->native))));
    }
    gtk_widget_set_visible(state->root, widget->visible != 0);
    return XXWIDGETS_OK;
}

static xxwidgets_status gtk_read_text_backend(xxwidgets_widget *widget)
{
    GtkWidget *native = widget->native;
    const char *text;
    switch (widget->kind) {
    case XXWIDGETS_WINDOW: text = gtk_window_get_title(GTK_WINDOW(native)); break;
    case XXWIDGETS_LABEL: text = gtk_label_get_text(GTK_LABEL(native)); break;
    case XXWIDGETS_BUTTON:
    case XXWIDGETS_CHECKBOX: text = gtk_button_get_label(GTK_BUTTON(native)); break;
    case XXWIDGETS_EDIT: text = gtk_entry_get_text(GTK_ENTRY(native)); break;
    case XXWIDGETS_PROGRESS: text = gtk_progress_bar_get_text(GTK_PROGRESS_BAR(native)); break;
    default: return XXWIDGETS_OK;
    }
    return xxwidgets_store_text(widget, text ? text : "");
}

static xxwidgets_status gtk_read_value_backend(xxwidgets_widget *widget)
{
    GtkWidget *native = widget->native;
    if (widget->kind == XXWIDGETS_COMBOBOX)
        widget->value = gtk_combo_box_get_active(GTK_COMBO_BOX(native));
    else if (widget->kind == XXWIDGETS_CHECKBOX)
        widget->value = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(native)) ? 1 : 0;
    else if (widget->kind == XXWIDGETS_ARCHIVEBROWSER) {
        browser_selection_read(widget);
    } else if (widget->kind == XXWIDGETS_SCANRESULTS) {
        scanresults_selection_read(widget);
    } else if (widget->kind == XXWIDGETS_TREEVIEW) {
        treeview_selection_read(widget);
    } else if (xxwidgets_list_kind(widget)) {
        GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(native));
        widget->value = row ? gtk_list_box_row_get_index(row) : -1;
    } else if (widget->kind == XXWIDGETS_PROGRESS)
        widget->value = (int)(gtk_progress_bar_get_fraction(GTK_PROGRESS_BAR(native)) * 100.0 + 0.5);
    return XXWIDGETS_OK;
}

static xxwidgets_status gtk_focus_backend(xxwidgets_widget *widget)
{
    if (widget->kind == XXWIDGETS_WINDOW) gtk_window_present(GTK_WINDOW(widget->native));
    else gtk_widget_grab_focus(GTK_WIDGET(widget->native));
    return XXWIDGETS_OK;
}

static gboolean options_key(GtkWidget *native, GdkEventKey *key, gpointer user)
{
    xxwidgets_widget *dialog = user;
    (void)native;
    if (key->keyval == GDK_KEY_Escape) { xxwidgets_emit(dialog, XXWIDGETS_EVENT_CLOSE, 0); return TRUE; }
    if ((key->keyval == GDK_KEY_Return || key->keyval == GDK_KEY_KP_Enter) && dialog->app->modal_default) {
        xxwidgets_emit(dialog->app->modal_default, XXWIDGETS_EVENT_CLICK, 0); return TRUE;
    }
    return FALSE;
}

static xxwidgets_status gtk_modal_owner(xxwidgets_widget *dialog, xxwidgets_widget *owner, int active)
{
    GtkWindow *window = GTK_WINDOW(dialog->native);
    xxwidgets_widget *widget;
    gtk_window_set_transient_for(window, active ? GTK_WINDOW(owner->native) : NULL);
    gtk_window_set_modal(window, active);
    if (active) {
        gtk_window_set_position(window, GTK_WIN_POS_CENTER_ON_PARENT);
        g_signal_connect(window, "key-press-event", G_CALLBACK(options_key), dialog);
        for (widget = dialog->app->widgets; widget; widget = widget->next)
            if (widget->parent == dialog && widget->kind == XXWIDGETS_BUTTON &&
                (widget == dialog->app->modal_default || !strcmp(widget->text, "OK"))) {
                gtk_widget_set_can_default(GTK_WIDGET(widget->native), TRUE);
                gtk_widget_grab_default(GTK_WIDGET(widget->native));
            }
        gtk_window_present(window);
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status gtk_about_content(xxwidgets_widget *window,
    const xxwidgets_about_dialog *about, const char *body)
{
    gtk_app_state *app = window->app->platform;
    gtk_widget_state *state = window->platform;
    int padding = 2 * app->cell_width, image_box = 12 * app->cell_width;
    int text_x = about->image ? image_box + 2 * padding : padding;
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL), *text = gtk_text_view_new();
    gtk_window_set_resizable(GTK_WINDOW(window->native), FALSE);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD_CHAR);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text)), body, -1);
    gtk_widget_set_size_request(scroll, window->rect.width * app->cell_width - text_x - padding,
                               17 * app->cell_height);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), text);
    gtk_fixed_put(GTK_FIXED(state->content), scroll, text_x, app->cell_height);
    gtk_widget_show_all(scroll);
    if (about->image) {
        GdkPixbuf *pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8,
                                         (int)about->image_width, (int)about->image_height);
        GdkPixbuf *scaled;
        GtkWidget *image;
        unsigned int row;
        int width = image_box, height = image_box;
        if (!pixels) return XXWIDGETS_OUT_OF_MEMORY;
        for (row = 0; row < about->image_height; ++row)
            memcpy(gdk_pixbuf_get_pixels(pixels) + (size_t)row * gdk_pixbuf_get_rowstride(pixels),
                   about->image + (size_t)row * about->image_width * 4, (size_t)about->image_width * 4);
        if (about->image_width > about->image_height)
            height = (int)((uint64_t)image_box * about->image_height / about->image_width);
        else width = (int)((uint64_t)image_box * about->image_width / about->image_height);
        scaled = gdk_pixbuf_scale_simple(pixels, width > 0 ? width : 1, height > 0 ? height : 1,
                                        GDK_INTERP_BILINEAR);
        g_object_unref(pixels);
        if (!scaled) return XXWIDGETS_OUT_OF_MEMORY;
        image = gtk_image_new_from_pixbuf(scaled);
        g_object_unref(scaled);
        gtk_widget_set_size_request(image, image_box, image_box);
        gtk_fixed_put(GTK_FIXED(state->content), image, padding, app->cell_height);
        gtk_widget_show(image);
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status gtk_copy_text(xxwidgets_widget *window, const char *text)
{
    GtkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(window->native), GDK_SELECTION_CLIPBOARD);
    if (!clipboard) return XXWIDGETS_PLATFORM_ERROR;
    gtk_clipboard_set_text(clipboard, text, -1);
    gtk_clipboard_store(clipboard);
    return XXWIDGETS_OK;
}

const xxwidgets_backend_ops xxwidgets_native_ops = {
    "GTK3", gtk_init_backend, gtk_shutdown_backend, gtk_poll_backend,
    gtk_create_backend, gtk_destroy_backend, gtk_sync_backend,
    gtk_read_text_backend, gtk_read_value_backend, gtk_focus_backend, gtk_modal_owner, gtk_about_content, gtk_copy_text
};
