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

#include "xxwidgets_internal.h"
#include "../die_engine/xxwidgets_die_signature_edit_internal.h"

#include <gtk/gtk.h>
#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Style classes: GRID_CLASS marks a window's cell container, CELL_CLASS the
 * one-row controls placed on it, so GRID_CSS reaches nothing else (file
 * choosers, tree headers and popups keep the theme untouched). */
#define GRID_CLASS "xxwidgets-grid"
#define CELL_CLASS "xxwidgets-cell"
/* Pixels left free between vertically adjacent cells (half above, half below
 * each control), so stacked entries and buttons do not touch. */
#define GRID_ROW_GAP 2

/* Themes give buttons, entries and combos a min-height meant for box layouts
 * (30-34 px), taller than a text cell. Only that floor is dropped: each theme
 * keeps its own vertical padding, borders and colors, and the cell height is
 * then measured from the trimmed controls (measure_control_height). The
 * horizontal padding of buttons is narrowed because the grid, not the label,
 * decides a button's width; the label stays centered either way. The combo's
 * cell view already pads its text by 2 px above and below. Selector node
 * names and min-height need GTK 3.20, so older GTK skips this sheet and the
 * measurement simply sees the untrimmed controls. */
static const char GRID_CSS[] = "." GRID_CLASS " > button." CELL_CLASS
                               ","
                               "." GRID_CLASS " > entry." CELL_CLASS
                               ","
                               "." GRID_CLASS " > combobox." CELL_CLASS
                               " > box > button.combo { min-height: 0; }\n"
                               "." GRID_CLASS " > button." CELL_CLASS
                               " { padding-left: 4px; padding-right: 4px; }\n"
                               "." GRID_CLASS " > combobox." CELL_CLASS " > box > button.combo { padding-top: 0; padding-bottom: 0; }\n";

typedef struct gtk_app_state {
    int cell_width;
    int cell_height;
    int base_cell_width; /* From the font; grid_fit widens at most to 1.5x. */
    int fitted;
    guint fit_source;
    PangoFontDescription *fonts[XXWIDGETS_FONT_ROLE_COUNT];
    /* One stylesheet per role, attached to every control of that role. */
    GtkCssProvider *font_css[XXWIDGETS_FONT_ROLE_COUNT];
} gtk_app_state;

typedef struct gtk_widget_state {
    GtkWidget *root;               /* Scrolled container for a list, otherwise the control. */
    GtkWidget *content;            /* A window's GtkLayout. */
    GtkWidget *content_box, *menu_bar;
    int menu_height;
    int frame_width, frame_height; /* WINDOW: last configured client size. */
    int fit_position;              /* WINDOW: the next resize is ours; keep the frame on screen. Counts
                                    * the resizes it held for, until the window reached its minimum. */
    int configuring;               /* WINDOW: inside its configure-event, whose sizes can be stale. */
    int min_width, min_height;     /* WINDOW: content size request last applied. */
    void *monitor;                 /* WINDOW: GdkMonitor the minimum was clamped to (GTK 3.22+). */
    GtkWidget *browser_scroll, *browser_address, *browser_up;
    size_t rendered_items;
    uint64_t rendered_hex_revision;
    int browser_value; /* ARCHIVEBROWSER: widget->value the view last agreed with. */
    int scroll_selection;
    int follow_tail; /* LISTBOX: the view is at its end, so it stays there. */
    xxwidgets_rect applied_rect;
    int has_rect;
    GtkWidget *combo_box, *combo_scroll;
    uint64_t combo_revision;
    int combo_initialized;
    GtkTreeIter *tree_iters;
    size_t tree_count;
    uint64_t tree_content_revision;
    int tree_collapsing, tree_before_collapse;
    GtkWidget *about_text; /* Owned by the window's content container. */
    PangoFontDescription *preview_font;
    GtkCssProvider *preview_css;
    PangoFontDescription *hex_font;
    GtkCssProvider *hex_font_css;
    GtkWidget *hex_scroll, *hex_overview;
    int hex_overview_dragging;
    int has_preview;
    GtkAdjustment *die_vertical, *die_horizontal; /* Owned by native scrollbars. */
    GtkIMContext *die_im;
    int die_adjusting, die_dragging, die_cell_width, die_cell_height, die_gutter;
    size_t die_rows, die_columns;
} gtk_widget_state;

/* Keep the full-file map outside the scrolling content. The adjacent native
 * scrollbar shares the tree's adjustment, so the map itself never scrolls. */
static gboolean gtk_hex_overview_draw(GtkWidget *native, cairo_t *cr, gpointer data)
{
    xxwidgets_widget *widget = data;
    GtkStyleContext *context = gtk_widget_get_style_context(native);
    GtkAllocation allocation;
    GdkRGBA background;
    gtk_widget_get_allocation(native, &allocation);
    gtk_style_context_get_background_color(context, GTK_STATE_FLAG_NORMAL, &background);
    if (background.alpha < 0.1) background = (GdkRGBA){0.88, 0.88, 0.88, 1.0};
    gdk_cairo_set_source_rgba(cr, &background);
    cairo_paint(cr);
    if (allocation.width > 0 && allocation.height > 0) {
        for (int row = 0; row < allocation.height; ++row) {
            uint32_t rgb;
            if (!xxwidgets_hexview_overview_color(widget, (size_t)row, (size_t)allocation.height, &rgb)) continue;
            cairo_set_source_rgb(cr, ((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0);
            cairo_rectangle(cr, 1, row, allocation.width > 2 ? allocation.width - 2 : allocation.width, 1);
            cairo_fill(cr);
        }
        cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.35);
        cairo_rectangle(cr, 0, 0, 1, allocation.height);
        cairo_rectangle(cr, allocation.width - 1, 0, 1, allocation.height);
        cairo_fill(cr);
    }
    return TRUE;
}

static void gtk_hex_overview_activate(xxwidgets_widget *widget, double y)
{
    gtk_widget_state *state = widget->platform;
    int height = gtk_widget_get_allocated_height(state->hex_overview);
    if (height <= 0 || !xxwidgets_focusable(widget) || widget->app->syncing) return;
    size_t row = y <= 0 ? 0 : y >= height ? (size_t)height - 1 : (size_t)y;
    (void)xxwidgets_hexview_overview_activate(widget, row, (size_t)height);
    gtk_widget_grab_focus(GTK_WIDGET(widget->native));
}

static gboolean gtk_hex_overview_pointer(GtkWidget *native, GdkEventButton *event, gpointer data)
{
    xxwidgets_widget *widget = data;
    gtk_widget_state *state = widget->platform;
    if (event->button != 1) return FALSE;
    if (event->type == GDK_BUTTON_RELEASE) {
        if (state->hex_overview_dragging) {
            state->hex_overview_dragging = 0;
            gtk_grab_remove(native);
        }
        return TRUE;
    }
    if (!xxwidgets_focusable(widget) || widget->app->syncing) return FALSE;
    state->hex_overview_dragging = 1;
    gtk_grab_add(native);
    gtk_hex_overview_activate(widget, event->y);
    return TRUE;
}

static gboolean gtk_hex_overview_motion(GtkWidget *native, GdkEventMotion *event, gpointer data)
{
    xxwidgets_widget *widget = data;
    gtk_widget_state *state = widget->platform;
    if (!state->hex_overview_dragging) return FALSE;
    if (!(event->state & GDK_BUTTON1_MASK)) {
        state->hex_overview_dragging = 0;
        gtk_grab_remove(native);
        return FALSE;
    }
    gtk_hex_overview_activate(widget, event->y);
    return TRUE;
}

static gboolean gtk_hex_overview_grab_broken(GtkWidget *native, GdkEventGrabBroken *event, gpointer data)
{
    gtk_widget_state *state = ((xxwidgets_widget *)data)->platform;
    (void)event;
    if (state && state->hex_overview_dragging) {
        state->hex_overview_dragging = 0;
        gtk_grab_remove(native);
    }
    return FALSE;
}

static PangoFontDescription *gtk_font_description(const xxwidgets_font *font)
{
    PangoFontDescription *description;
    if (!font->family[0] && !font->point_size && !font->bold && !font->italic) return NULL;
    description = pango_font_description_new();
    if (!description) return NULL;
    if (font->family[0]) pango_font_description_set_family(description, font->family);
    if (font->point_size) pango_font_description_set_size(description, (int)font->point_size * PANGO_SCALE);
    pango_font_description_set_weight(description, font->bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_font_description_set_style(description, font->italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
    return description;
}

/* CSS for one font, or an empty sheet to keep the theme's font. GTK CSS fonts
 * are inherited, so a provider on a control's own style context also reaches
 * its label, rows and other children: nothing has to visit them, which kept a
 * long list re-styling every row each time a row was added. */
static gchar *font_css_text(const PangoFontDescription *description)
{
    const char *family;
    GString *css;
    gint size;
    if (!description) return g_strdup("");
    css = g_string_new("* {");
    family = pango_font_description_get_family(description);
    if (family && family[0]) {
        const char *p;
        g_string_append(css, " font-family: \"");
        for (p = family; *p; ++p) {
            if (*p == '"' || *p == '\\') g_string_append_c(css, '\\');
            g_string_append_c(css, *p);
        }
        g_string_append(css, "\";");
    }
    size = pango_font_description_get_size(description);
    if (size > 0) g_string_append_printf(css, " font-size: %dpt;", size / PANGO_SCALE);
    g_string_append_printf(css, " font-weight: %s; font-style: %s; }", pango_font_description_get_weight(description) >= PANGO_WEIGHT_BOLD ? "bold" : "normal",
                           pango_font_description_get_style(description) == PANGO_STYLE_ITALIC ? "italic" : "normal");
    return g_string_free(css, FALSE);
}

static void font_css_load(GtkCssProvider *provider, const PangoFontDescription *description)
{
    gchar *css = font_css_text(description);
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    g_free(css);
}

static void font_attach(GtkWidget *native, GtkCssProvider *provider, guint priority)
{
    if (native && provider) gtk_style_context_add_provider(gtk_widget_get_style_context(native), GTK_STYLE_PROVIDER(provider), priority);
}

/* Formatted views default to monospace; a role family replaces that default. */
static void row_font_attributes(xxwidgets_widget *widget, GtkWidget *label)
{
    gtk_app_state *app = widget->app->platform;
    gtk_widget_state *state = widget->platform;
    const PangoFontDescription *description = state->has_preview ? state->preview_font : app->fonts[xxwidgets_widget_font_role(widget)];
    PangoAttrList *attributes = NULL;
    if (!description || !pango_font_description_get_family(description)) {
        attributes = pango_attr_list_new();
        pango_attr_list_insert(attributes, pango_attr_family_new("monospace"));
    }
    gtk_label_set_attributes(GTK_LABEL(label), attributes);
    if (attributes) pango_attr_list_unref(attributes);
}

static void rows_font_attributes(xxwidgets_widget *widget)
{
    GList *rows, *row;
    if (widget->kind != XXWIDGETS_ARCHIVEVIEW) return;
    rows = gtk_container_get_children(GTK_CONTAINER(widget->native));
    for (row = rows; row; row = row->next) row_font_attributes(widget, gtk_bin_get_child(GTK_BIN(row->data)));
    g_list_free(rows);
}

/* Once per control: its role's stylesheet, plus the browser's own parts. */
static void fonts_attach(xxwidgets_widget *widget)
{
    gtk_app_state *app = widget->app->platform;
    gtk_widget_state *state = widget->platform;
    GtkCssProvider *role;
    if (widget->kind == XXWIDGETS_WINDOW) return; /* about_content attaches its text */
    role = app->font_css[xxwidgets_widget_font_role(widget)];
    font_attach(GTK_WIDGET(widget->native), role, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    font_attach(state->combo_box, role, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    font_attach(state->browser_address, app->font_css[XXWIDGETS_FONT_TEXT_EDITS], GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    font_attach(state->browser_up, app->font_css[XXWIDGETS_FONT_CONTROLS], GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static xxwidgets_status gtk_apply_fonts(xxwidgets_app *app, const xxwidgets_font_options *options)
{
    gtk_app_state *state = app->platform;
    PangoFontDescription *fonts[XXWIDGETS_FONT_ROLE_COUNT] = {0};
    size_t role;
    for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role) {
        const xxwidgets_font *font = &options->fonts[role];
        fonts[role] = gtk_font_description(font);
        if (!fonts[role] && (font->family[0] || font->point_size || font->bold || font->italic)) {
            for (size_t i = 0; i < role; ++i)
                if (fonts[i]) pango_font_description_free(fonts[i]);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
    }
    for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role) {
        PangoFontDescription *previous = state->fonts[role];
        state->fonts[role] = fonts[role];
        if (previous) pango_font_description_free(previous);
        if (state->font_css[role]) font_css_load(state->font_css[role], fonts[role]);
    }
    for (xxwidgets_widget *widget = app->widgets; widget; widget = widget->next)
        if (widget->platform && widget->native) {
            rows_font_attributes(widget);
            if (widget->kind == XXWIDGETS_DIE_SIGNATURE_EDIT) gtk_widget_queue_resize(GTK_WIDGET(widget->native));
        }
    return XXWIDGETS_OK;
}

static xxwidgets_status gtk_preview_font(xxwidgets_widget *widget, xxwidgets_font_role role, const xxwidgets_font *font)
{
    gtk_widget_state *state = widget->platform;
    PangoFontDescription *description = gtk_font_description(font);
    (void)role;
    if (!description && (font->family[0] || font->point_size || font->bold || font->italic)) return XXWIDGETS_OUT_OF_MEMORY;
    if (state->preview_font) pango_font_description_free(state->preview_font);
    state->preview_font = description;
    state->has_preview = 1;
    if (!state->preview_css) {
        state->preview_css = gtk_css_provider_new();
        font_attach(GTK_WIDGET(widget->native), state->preview_css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    }
    if (state->preview_css) font_css_load(state->preview_css, description);
    rows_font_attributes(widget);
    if (widget->kind == XXWIDGETS_DIE_SIGNATURE_EDIT) gtk_widget_queue_resize(GTK_WIDGET(widget->native));
    return XXWIDGETS_OK;
}

static xxwidgets_status gtk_choose_font(xxwidgets_widget *owner, xxwidgets_font_role role, xxwidgets_font *font, int *accepted)
{
    static const char *titles[] = {"Controls font", "Table views font", "Tree views font", "Text edits font"};
    GtkWidget *dialog = gtk_font_chooser_dialog_new(titles[role], GTK_WINDOW(owner->native));
    PangoFontDescription *description = gtk_font_description(font);
    xxwidgets_status status = XXWIDGETS_OK;
    *accepted = 0;
    g_object_ref_sink(dialog);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
    if (description) {
        gtk_font_chooser_set_font_desc(GTK_FONT_CHOOSER(dialog), description);
        pango_font_description_free(description);
    }
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        description = gtk_font_chooser_get_font_desc(GTK_FONT_CHOOSER(dialog));
        const char *family = description ? pango_font_description_get_family(description) : NULL;
        int size = description ? pango_font_description_get_size(description) : 0;
        /* GtkFontChooser describes point sizes; the public API uses integers. */
        unsigned int points = size > 0 ? (unsigned int)(size / PANGO_SCALE + (size % PANGO_SCALE >= PANGO_SCALE / 2)) : 0;
        if (!family || strlen(family) >= sizeof(font->family) || points < 4 || points > 96 ||
            pango_font_description_get_size_is_absolute(description)) status = XXWIDGETS_INVALID_ARGUMENT;
        else {
            xxwidgets_font chosen = {0};
            memcpy(chosen.family, family, strlen(family) + 1);
            chosen.point_size = points;
            chosen.bold = pango_font_description_get_weight(description) >= PANGO_WEIGHT_BOLD;
            chosen.italic = pango_font_description_get_style(description) != PANGO_STYLE_NORMAL;
            if (xxwidgets_font_valid(&chosen)) { *font = chosen; *accepted = 1; }
            else status = XXWIDGETS_INVALID_ARGUMENT;
        }
        if (description) pango_font_description_free(description);
    }
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    return status;
}

static int gtk_hex_cell_width(const xxwidgets_widget *widget)
{
    const gtk_widget_state *state = widget->platform;
    PangoLayout *layout;
    int width;
    if (!state || !state->hex_font) return 8;
    layout = gtk_widget_create_pango_layout(GTK_WIDGET(widget->native), "0");
    pango_layout_set_font_description(layout, state->hex_font);
    pango_layout_get_pixel_size(layout, &width, NULL);
    g_object_unref(layout);
    return width > 0 ? width : 1;
}

static xxwidgets_status gtk_hexview_font(xxwidgets_widget *widget, const xxwidgets_font *font)
{
    gtk_widget_state *state = widget->platform;
    xxwidgets_font effective = *font;
    PangoFontDescription *description, *previous;
    GtkCssProvider *provider = state->hex_font_css;
    if (xxwidgets_font_is_monospace(font)) memcpy(effective.family, "Monospace", sizeof("Monospace"));
    description = gtk_font_description(&effective);
    if (!description) return XXWIDGETS_OUT_OF_MEMORY;
    if (!effective.point_size) {
        gchar *name = NULL;
        PangoFontDescription *theme;
        g_object_get(gtk_settings_get_default(), "gtk-font-name", &name, NULL);
        theme = pango_font_description_from_string(name ? name : "Sans 10");
        pango_font_description_set_size(description, theme && pango_font_description_get_size(theme) ?
            pango_font_description_get_size(theme) : 10 * PANGO_SCALE);
        if (theme) pango_font_description_free(theme);
        g_free(name);
    }
    if (!provider) provider = gtk_css_provider_new();
    if (!provider) { pango_font_description_free(description); return XXWIDGETS_OUT_OF_MEMORY; }
    font_css_load(provider, description);
    if (!state->hex_font_css)
        font_attach(GTK_WIDGET(widget->native), provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 2);
    previous = state->hex_font;
    state->hex_font = description;
    state->hex_font_css = provider;
    for (size_t i = 0; i < 3; ++i) {
        GtkTreeViewColumn *column = gtk_tree_view_get_column(GTK_TREE_VIEW(widget->native), (gint)i);
        GList *cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));
        for (GList *cell = cells; cell; cell = cell->next) g_object_set(cell->data, "font-desc", description, NULL);
        g_list_free(cells);
        gtk_tree_view_column_set_fixed_width(column, (int)xxwidgets_hexview_column_width(widget, i) * gtk_hex_cell_width(widget));
    }
    if (previous) pango_font_description_free(previous);
    gtk_widget_queue_resize(GTK_WIDGET(widget->native));
    gtk_widget_queue_draw(GTK_WIDGET(widget->native));
    return XXWIDGETS_OK;
}

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
    if (widget->parent) {
        /* cell_height leaves at least GRID_ROW_GAP above a control's minimum. */
        py += GRID_ROW_GAP / 2;
        ph -= GRID_ROW_GAP;
    }
    if (px < INT_MIN || px > INT_MAX || py < INT_MIN || py > INT_MAX || pw <= 0 || pw > INT_MAX || ph <= 0 || ph > INT_MAX) return 0;
    *x = (int)px;
    *y = (int)py;
    *width = (int)pw;
    *height = (int)ph;
    return 1;
}

static int monitor_workarea(GtkWidget *native, int x, int y, GdkRectangle *area)
{
#if GTK_CHECK_VERSION(3, 22, 0)
    /* A realized window knows its monitor, also where positions are not
     * reported (Wayland); before that, the requested position decides. */
    GdkWindow *window = gtk_widget_get_window(native);
    GdkMonitor *monitor =
        window ? gdk_display_get_monitor_at_window(gtk_widget_get_display(native), window) : gdk_display_get_monitor_at_point(gtk_widget_get_display(native), x, y);
    if (!monitor) return 0;
    gdk_monitor_get_workarea(monitor, area);
#else
    GdkScreen *screen = gtk_widget_get_screen(native);
    gdk_screen_get_monitor_workarea(screen, gdk_screen_get_monitor_at_point(screen, x, y), area);
#endif
    return area->width > 0 && area->height > 0;
}

/* Shrinks and moves a requested window frame into its monitor's work area.
 * The cell rect is left as requested: the configure event that follows
 * reports the fitted size and emits RESIZE, so the application lays out for
 * the size it actually got. */
/* Work area of the monitor at (x, y) and the window manager's frame size;
 * returns 0 when the monitor is unknown. */
static int window_frame_area(xxwidgets_widget *widget, int x, int y, GdkRectangle *area, int *frame_width, int *frame_height)
{
    GtkWidget *native = widget->native;
    GdkWindow *window = gtk_widget_get_window(native);
    /* Room for server-side decorations until the window manager reports them. */
    *frame_width = 16;
    *frame_height = 48;
    if (!monitor_workarea(native, x, y, area)) return 0;
    if (window && gtk_widget_get_mapped(native)) {
        GdkRectangle frame;
        gdk_window_get_frame_extents(window, &frame);
        /* Before the frame exists the extents equal the client: keep the guess. */
        if (frame.width > gdk_window_get_width(window) || frame.height > gdk_window_get_height(window)) {
            *frame_width = frame.width - gdk_window_get_width(window);
            *frame_height = frame.height - gdk_window_get_height(window);
        }
    }
    return 1;
}

/* Moves a frame of the given client size into its monitor's work area. */
static void window_fit_position(xxwidgets_widget *widget, int *x, int *y, int width, int height)
{
    GdkRectangle area;
    int frame_width, frame_height;
    if (!window_frame_area(widget, *x, *y, &area, &frame_width, &frame_height)) return;
    if (*x > area.x + area.width - frame_width - width) *x = area.x + area.width - frame_width - width;
    if (*y > area.y + area.height - frame_height - height) *y = area.y + area.height - frame_height - height;
    if (*x < area.x) *x = area.x;
    if (*y < area.y) *y = area.y;
}

static void window_fit_workarea(xxwidgets_widget *widget, int *x, int *y, int *width, int *height)
{
    gtk_app_state *app = widget->app->platform;
    GdkRectangle area;
    int frame_width, frame_height, max_width, max_height;
    if (!window_frame_area(widget, *x, *y, &area, &frame_width, &frame_height)) return;
    max_width = (area.width - frame_width) / app->cell_width * app->cell_width;
    max_height = (area.height - frame_height) / app->cell_height * app->cell_height;
    if (max_width > 0 && *width > max_width) *width = max_width;
    if (max_height > 0 && *height > max_height) *height = max_height;
    window_fit_position(widget, x, y, *width, *height);
}

/* The content GtkLayout reports no minimum of its own, so this request alone
 * sets how far the user can shrink the window: a fixed-size window's own size,
 * the minimum the application set, or else the cell bounding box of the
 * children, which is what GtkFixed enforced for dialogs that never re-lay
 * out. Native control sizes never feed it, so the window cannot grow itself. */
static void window_update_minimum(xxwidgets_widget *window)
{
    gtk_app_state *app = window->app->platform;
    gtk_widget_state *state = window->platform;
    int64_t columns = 0, rows = 0, width, height;
    if (!state || !state->content) return;
    if (!gtk_window_get_resizable(GTK_WINDOW(window->native))) {
        columns = window->rect.width;
        rows = window->rect.height;
    } else if (window->has_minimum) {
        columns = window->min_columns;
        rows = window->min_rows;
    } else {
        xxwidgets_widget *child;
        for (child = window->app->widgets; child; child = child->next)
            if (child->parent == window && child->visible) {
                if (child->rect.x + child->rect.width > columns) columns = child->rect.x + child->rect.width;
                if (child->rect.y + child->rect.height > rows) rows = child->rect.y + child->rect.height;
            }
    }
    width = columns * app->cell_width;
    height = rows * app->cell_height;
    {
        /* A minimum taller or wider than the work area would push the frame
         * off a small screen; the layout's last rows are cut off instead. */
        GdkRectangle area;
        int x = 0, y = 0, frame_width, frame_height;
        gtk_window_get_position(GTK_WINDOW(window->native), &x, &y);
        if (window_frame_area(window, x, y, &area, &frame_width, &frame_height)) {
            if (width > area.width - frame_width) width = area.width - frame_width;
            if (height > area.height - frame_height - state->menu_height) height = area.height - frame_height - state->menu_height;
            if (width < 0) width = 0;
            if (height < 0) height = 0;
        }
    }
    if (width > INT_MAX) width = INT_MAX;
    if (height > INT_MAX) height = INT_MAX;
    if (width == state->min_width && height == state->min_height) return;
    {
        /* A larger minimum grows the window: that resize is ours to fit. */
        int current_width = 0, current_height = 0;
        gtk_window_get_size(GTK_WINDOW(window->native), &current_width, &current_height);
        if (width > current_width || height > current_height) {
            GdkWindow *gdk_window = gtk_widget_get_window(window->native);
            GdkRectangle frame = {0, 0, 0, 0};
            state->fit_position = 1;
            if (gdk_window && gtk_widget_get_mapped(window->native)) gdk_window_get_frame_extents(gdk_window, &frame);
            /* Fit the grown frame now too: the configure event that checks it
             * can come after another size change has used up the flag. Only
             * once the window manager has placed and framed the window (it
             * was configured since it mapped, and the frame is larger than
             * the client); until then position and frame are guesses. Not
             * from within a configure event: GDK may still report the size
             * of an earlier map there, and the flag fits the next one. */
            if (gdk_window && !state->configuring && state->frame_width > 0 && frame.width > gdk_window_get_width(gdk_window)) {
                int x, y, fitted_x, fitted_y;
                gtk_window_get_position(GTK_WINDOW(window->native), &x, &y);
                fitted_x = x;
                fitted_y = y;
                window_fit_position(window, &fitted_x, &fitted_y, width > current_width ? (int)width : current_width,
                                    height > current_height ? (int)height : current_height);
                if (fitted_x != x || fitted_y != y) gtk_window_move(GTK_WINDOW(window->native), fitted_x, fitted_y);
            }
        }
    }
    state->min_width = (int)width;
    state->min_height = (int)height;
    gtk_widget_set_size_request(state->content, (int)width, (int)height);
}

static GtkLabel *control_label(GtkWidget *native)
{
    GtkWidget *label = GTK_IS_LABEL(native) ? native : GTK_IS_BIN(native) ? gtk_bin_get_child(GTK_BIN(native)) : NULL;
    return label && GTK_IS_LABEL(label) ? GTK_LABEL(label) : NULL;
}

/* A label that its cells cut short shows the whole text as its tooltip. */
static gboolean ellipsis_tooltip(GtkWidget *native, gint x, gint y, gboolean keyboard, GtkTooltip *tooltip, gpointer data)
{
    GtkLabel *label = control_label(native);
    (void)x;
    (void)y;
    (void)keyboard;
    (void)data;
    if (!label || !pango_layout_is_ellipsized(gtk_label_get_layout(label))) return FALSE;
    gtk_tooltip_set_text(tooltip, gtk_label_get_text(label));
    return TRUE;
}

/* Button labels are created lazily by gtk_button_set_label. Ellipsizing caps
 * the control's minimum width, so text that cannot fit its cells is cut with
 * an ellipsis inside the control instead of widening it over its neighbors. */
static void ellipsize_control_label(GtkWidget *native)
{
    GtkLabel *label = control_label(native);
    if (label && GTK_WIDGET(label) != native) gtk_label_set_ellipsize(label, PANGO_ELLIPSIZE_END);
}

/* Prepares a one-row grid control: CSS class, and minimum widths that the
 * text or the toolkit's defaults would otherwise impose beyond its cells. */
static void cell_control(GtkWidget *native)
{
    gtk_style_context_add_class(gtk_widget_get_style_context(native), CELL_CLASS);
    if (GTK_IS_ENTRY(native)) gtk_entry_set_width_chars(GTK_ENTRY(native), 1);
    if (GTK_IS_COMBO_BOX(native)) {
        GList *cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(native));
        for (GList *cell = cells; cell; cell = cell->next)
            if (GTK_IS_CELL_RENDERER_TEXT(cell->data)) g_object_set(cell->data, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
        g_list_free(cells);
    }
    if (GTK_IS_BUTTON(native)) {
        ellipsize_control_label(native);
        gtk_widget_set_has_tooltip(native, TRUE);
        g_signal_connect(native, "query-tooltip", G_CALLBACK(ellipsis_tooltip), NULL);
    }
}

/* Tallest minimum height among the one-row controls as GRID_CSS leaves them,
 * measured on an unshown window so theme rules apply as on a real one. */
static int measure_control_height(void)
{
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL), *grid = gtk_layout_new(NULL, NULL);
    GtkWidget *controls[5];
    int height = 0;
    size_t i;
    gtk_style_context_add_class(gtk_widget_get_style_context(grid), GRID_CLASS);
    gtk_container_add(GTK_CONTAINER(window), grid);
    controls[0] = gtk_button_new_with_label("Mg");
    controls[1] = gtk_entry_new();
    controls[2] = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(controls[2]), "Mg");
    gtk_combo_box_set_active(GTK_COMBO_BOX(controls[2]), 0);
    controls[3] = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(controls[3]), "Mg \xe2\x96\xbe");
    controls[4] = gtk_check_button_new_with_label("Mg");
    for (i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i) {
        int minimum = 0, natural = 0;
        cell_control(controls[i]);
        gtk_layout_put(GTK_LAYOUT(grid), controls[i], 0, 0);
        /* Hidden widgets measure as 0 x 0. */
        gtk_widget_show(controls[i]);
        gtk_widget_get_preferred_height(controls[i], &minimum, &natural);
        if (minimum > height) height = minimum;
    }
    gtk_widget_destroy(window);
    return height;
}

static gboolean window_close(GtkWidget *native, GdkEvent *event, gpointer data)
{
    xxwidgets_widget *widget = data;
    (void)native;
    (void)event;
    if (!widget->app->syncing) xxwidgets_emit(widget, XXWIDGETS_EVENT_CLOSE, 0);
    /* The handler may hide the window or quit. Destruction belongs to the core. */
    return TRUE;
}

static gboolean window_configure(GtkWidget *native, GdkEventConfigure *event, gpointer data)
{
    xxwidgets_widget *widget = data;
    gtk_app_state *app_state = widget->app->platform;
    gtk_widget_state *state = widget->platform;
    /* Round down: a layout for the rounded-up count would overhang the edge. */
    int width = event->width / app_state->cell_width;
    int height = (event->height - state->menu_height) / app_state->cell_height;
    if ((event->width != state->frame_width || event->height != state->frame_height) && gtk_widget_get_mapped(native)) {
        /* A size change the toolkit caused (the first map, its minimum, a wider
         * grid) must not push the window off its monitor. Resizes and moves by
         * the user are theirs and are left alone. */
        if (state->fit_position) {
            int x, y, fitted_x, fitted_y;
            gtk_window_get_position(GTK_WINDOW(native), &x, &y);
            fitted_x = x;
            fitted_y = y;
            window_fit_position(widget, &fitted_x, &fitted_y, event->width, event->height);
            if (fitted_x != x || fitted_y != y) gtk_window_move(GTK_WINDOW(native), fitted_x, fitted_y);
        }
        /* A window still short of the minimum it was given grows again, and
         * that size is ours too (a configure can report the size from before
         * the minimum); a few resizes at most, so later ones stay the user's. */
        if (state->fit_position && state->fit_position < 4 && (event->width < state->min_width || event->height < state->min_height + state->menu_height)) ++state->fit_position;
        else state->fit_position = 0;
    }
    state->frame_width = event->width;
    state->frame_height = event->height;
#if GTK_CHECK_VERSION(3, 22, 0)
    {
        /* The minimum is clamped to one monitor's work area: redo it on another. */
        GdkWindow *window = gtk_widget_get_window(native);
        void *monitor = window ? (void *)gdk_display_get_monitor_at_window(gtk_widget_get_display(native), window) : NULL;
        if (monitor && monitor != state->monitor) {
            state->monitor = monitor;
            state->min_width = state->min_height = -1;
            state->configuring = 1;
            window_update_minimum(widget);
            state->configuring = 0;
        }
    }
#endif
    /* A fixed-size window keeps its own size in rect: a work-area clamp on a
     * small screen must not become the size it returns to on a larger one. */
    if (!widget->app->syncing && width > 0 && height > 0 && gtk_window_get_resizable(GTK_WINDOW(native)) &&
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

static xxwidgets_status gtk_focus_backend(xxwidgets_widget *widget);
static int gtk_has_focus(const xxwidgets_widget *widget);

static int gtk_key_input(GdkEventKey *event, unsigned int *out_key, unsigned int *out_modifiers)
{
    unsigned int key = event->keyval, modifiers = 0;
    if (key >= GDK_KEY_F1 && key <= GDK_KEY_F24) key = XXWIDGETS_KEY_F1 + key - GDK_KEY_F1;
    else switch (key) {
            case GDK_KEY_Escape: key = XXWIDGETS_KEY_ESCAPE; break;
            case GDK_KEY_Return:
            case GDK_KEY_KP_Enter: key = XXWIDGETS_KEY_ENTER; break;
            case GDK_KEY_Tab:
            case GDK_KEY_ISO_Left_Tab:
            case GDK_KEY_KP_Tab: key = XXWIDGETS_KEY_TAB; break;
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
    if (!key) return 0;
    if (event->state & GDK_CONTROL_MASK) modifiers |= XXWIDGETS_MOD_CTRL;
    if (event->state & GDK_MOD1_MASK) modifiers |= XXWIDGETS_MOD_ALT;
    if (event->state & GDK_SHIFT_MASK) modifiers |= XXWIDGETS_MOD_SHIFT;
    if (event->state & (GDK_SUPER_MASK | GDK_META_MASK)) modifiers |= XXWIDGETS_MOD_META;
    *out_key = key;
    *out_modifiers = modifiers;
    return 1;
}

static void edit_activated(GtkEntry *native, gpointer data)
{
    xxwidgets_widget *widget = data;
    (void)native;
    if (!widget->app->syncing) xxwidgets_emit(widget, XXWIDGETS_EVENT_ACTIVATE, 0);
}

/* GtkListBox makes every row a Tab stop; Tab and Shift+Tab leave the list
 * for the next or previous control instead, as in the other backends. */
static void gtk_hex_menu(xxwidgets_widget *widget, unsigned int column, guint button, guint32 time);
static gboolean list_key(GtkWidget *native, GdkEventKey *event, gpointer data)
{
    xxwidgets_widget *widget = data, *candidate;
    GPtrArray *order;
    guint at = 0, count, k;
    int backward;
    (void)native;
    if (widget->kind == XXWIDGETS_HEXVIEW && !widget->app->syncing && xxwidgets_focusable(widget)) {
        unsigned int key, modifiers;
        int handled = 0;
        if (event->keyval == GDK_KEY_Menu) {
            gtk_hex_menu(widget, 3, 0, event->time);
            return TRUE;
        }
        if (gtk_key_input(event, &key, &modifiers)) {
            if (key == XXWIDGETS_KEY_F1 + 5) { xxwidgets_hexview_activate_column(widget, (widget->hex_header + 1) % 3); return TRUE; }
            if (key == XXWIDGETS_KEY_F1 + 9) {
                gtk_hex_menu(widget, (modifiers & XXWIDGETS_MOD_SHIFT) ? 3 : widget->hex_header, 0, event->time);
                return TRUE;
            }
            (void)xxwidgets_hexview_key_input(widget, key, modifiers, &handled);
            if (handled) {
                if (xxwidgets_focusable(widget)) gtk_focus_backend(widget);
                return TRUE;
            }
        }
    }
    if (event->keyval != GDK_KEY_Tab && event->keyval != GDK_KEY_ISO_Left_Tab && event->keyval != GDK_KEY_KP_Tab) return FALSE;
    if (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) return FALSE;
    backward = event->keyval == GDK_KEY_ISO_Left_Tab || (event->state & GDK_SHIFT_MASK);
    /* The other focusable controls of this window, in Tab (creation) order;
     * 'at' is where this list sits among them. */
    order = g_ptr_array_new();
    for (candidate = widget->app->widgets; candidate; candidate = candidate->next) {
        if (candidate == widget) {
            at = order->len;
            continue;
        }
        if (candidate->parent == widget->parent && xxwidgets_focusable(candidate)) g_ptr_array_add(order, candidate);
    }
    count = order->len;
    for (k = 0; k < count; ++k) {
        candidate = (xxwidgets_widget *)g_ptr_array_index(order, backward ? (at + count - 1 - k) % count : (at + k) % count);
        gtk_focus_backend(candidate);
        if (gtk_has_focus(candidate)) break;
    }
    g_ptr_array_free(order, TRUE);
    return TRUE;
}

static void edit_changed(GtkEditable *native, gpointer data)
{
    xxwidgets_widget *widget = data;
    if (widget->app->syncing) return;
    if (xxwidgets_store_text(widget, gtk_entry_get_text(GTK_ENTRY(native))) == XXWIDGETS_OK) xxwidgets_emit(widget, XXWIDGETS_EVENT_CHANGE, 0);
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
    if (!!gtk_toggle_button_get_active(native) != xxwidgets_checkcombobox_checked(widget, index)) xxwidgets_checkcombobox_user_toggle(widget, index);
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
    if (widget->kind == XXWIDGETS_HEXVIEW) {
        int value = row ? gtk_list_box_row_get_index(row) : -1;
        if (xxwidgets_hexview_selection_input(widget, value) != XXWIDGETS_OK) return;
    } else widget->value = row ? gtk_list_box_row_get_index(row) : -1;
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

static void gtk_hex_pick(GtkMenuItem *item, gpointer data)
{
    xxwidgets_widget *widget = data;
    size_t action = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "hex-action"));
    if (xxwidgets_focusable(widget)) (void)xxwidgets_hexview_menu_apply(widget, (unsigned)(action / 1000), action % 1000);
}
static void gtk_hex_menu(xxwidgets_widget *widget, unsigned int column, guint button, guint32 time)
{
    GtkWidget *menu, *group;
    const char *last = "";
    if (!xxwidgets_focusable(widget) || column > 3)
        return;
    if (column < 3)
        xxwidgets_hexview_activate_column(widget, column);
    menu = gtk_menu_new(); group = menu;
    gtk_menu_attach_to_widget(GTK_MENU(menu), GTK_WIDGET(widget->native), NULL);
    g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
    for (size_t i = 0; i < xxwidgets_hexview_menu_count(column); ++i) {
        xxwidgets_hex_menu_item entry = xxwidgets_hexview_menu_item(widget, column, i);
        if (strcmp(last, entry.group)) {
            GtkWidget *parent = gtk_menu_item_new_with_label(entry.group);
            group = gtk_menu_new(); gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent), group);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), parent); last = entry.group;
        }
        GtkWidget *item = column == 3 ? gtk_menu_item_new_with_label(entry.title) : gtk_check_menu_item_new_with_label(entry.title);
        if (column != 3) {
            gtk_check_menu_item_set_draw_as_radio(GTK_CHECK_MENU_ITEM(item), TRUE);
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), entry.checked);
        }
        gtk_widget_set_sensitive(item, !entry.disabled);
        g_object_set_data(G_OBJECT(item), "hex-action", GUINT_TO_POINTER(column * 1000 + (unsigned)i));
        g_signal_connect(item, "activate", G_CALLBACK(gtk_hex_pick), widget);
        gtk_menu_shell_append(GTK_MENU_SHELL(group), item);
    }
    gtk_widget_show_all(menu);
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, button, time);
}
static PangoLayout *gtk_hex_layout(GtkWidget *native, GtkCellRenderer *cell, const char *text)
{
    PangoLayout *layout = gtk_widget_create_pango_layout(native, text);
    PangoFontDescription *font = NULL;
    if (cell)
        g_object_get(cell, "font-desc", &font, NULL);
    if (font) {
        pango_layout_set_font_description(layout, font);
        pango_font_description_free(font);
    }
    pango_layout_set_auto_dir(layout, FALSE);
    return layout;
}
static double gtk_hex_character_width(PangoLayout *layout)
{
    int width;
    pango_layout_set_text(layout, "0", 1);
    pango_layout_get_size(layout, &width, NULL);
    return width > 0 ? (double)width / PANGO_SCALE : 1;
}
typedef struct { GtkCellRendererText parent; } GtkHexSymbolRenderer;
typedef struct { GtkCellRendererTextClass parent; } GtkHexSymbolRendererClass;
G_DEFINE_TYPE(GtkHexSymbolRenderer, gtk_hex_symbol_renderer, GTK_TYPE_CELL_RENDERER_TEXT)
static void gtk_hex_draw_symbol(PangoLayout *layout, cairo_t *cr, const char *text, int length,
                                double left, double width, const GdkRectangle *cell_area)
{
    PangoRectangle logical;
    pango_layout_set_text(layout, text, length);
    pango_layout_get_pixel_extents(layout, NULL, &logical);
    double scale = logical.width > width ? width / logical.width : 1;
    cairo_save(cr);
    cairo_translate(cr, left + (width - logical.width * scale) / 2,
                     cell_area->y + (cell_area->height - logical.height) / 2);
    cairo_scale(cr, scale, 1);
    cairo_move_to(cr, -logical.x, -logical.y);
    pango_cairo_show_layout(cr, layout);
    cairo_restore(cr);
}
static void gtk_hex_symbol_render(GtkCellRenderer *cell, cairo_t *cr, GtkWidget *native,
                                  const GdkRectangle *background_area, const GdkRectangle *cell_area,
                                  GtkCellRendererState flags)
{
    xxwidgets_widget *w = g_object_get_data(G_OBJECT(cell), "hex-widget");
    size_t row = (size_t)GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(cell), "hex-row"));
    if (!w || row >= w->item_count)
        return;
    (void)flags;
    GdkRGBA foreground, background, selected, selected_text, disabled;
    GtkStyleContext *style = gtk_widget_get_style_context(native);
    gtk_style_context_get_color(style, GTK_STATE_FLAG_NORMAL, &foreground);
    gtk_style_context_get_background_color(style, GTK_STATE_FLAG_NORMAL, &background);
    gtk_style_context_get_color(style, GTK_STATE_FLAG_SELECTED, &selected_text);
    gtk_style_context_get_background_color(style, GTK_STATE_FLAG_SELECTED, &selected);
    gtk_style_context_get_color(style, GTK_STATE_FLAG_INSENSITIVE, &disabled);
    PangoLayout *layout = gtk_hex_layout(native, cell, "0");
    double width = gtk_hex_character_width(layout);
    gint xpad, ypad;
    gtk_cell_renderer_get_padding(cell, &xpad, &ypad);
    (void)ypad;
    const char *text = xxwidgets_hexview_cell(w, row, 2);
    xxwidgets_hex_span spans[64];
    size_t count = xxwidgets_hexview_spans(w, row, 2, spans);
    cairo_save(cr);
    cairo_rectangle(cr, background_area->x, background_area->y, background_area->width, background_area->height);
    cairo_clip(cr);
    gdk_cairo_set_source_rgba(cr, &background);
    cairo_paint(cr);
    for (size_t i = 0; i < count; ++i) {
        size_t cell_offset, cells;
        xxwidgets_hexview_symbol_layout(w, row, &spans[i], &cell_offset, &cells);
        double left = cell_area->x + xpad + cell_offset * width;
        double extent = cells * width;
        int highlighted = xxwidgets_hexview_contains(w, spans[i].offset, spans[i].bytes);
        uint32_t tint;
        if (!highlighted && xxwidgets_hexview_highlight_span(w, spans[i].offset, spans[i].bytes, &tint)) {
            cairo_set_source_rgba(cr, ((tint >> 16) & 255) / 255.0, ((tint >> 8) & 255) / 255.0, (tint & 255) / 255.0, 0.45);
            cairo_rectangle(cr, left, background_area->y, extent, background_area->height); cairo_fill(cr);
        }
        if (highlighted) {
            gdk_cairo_set_source_rgba(cr, &selected);
            cairo_rectangle(cr, left, background_area->y, extent, background_area->height);
            cairo_fill(cr);
        }
        if (spans[i].length) {
            gdk_cairo_set_source_rgba(cr, highlighted ? &selected_text : &foreground);
            gtk_hex_draw_symbol(layout, cr, text + spans[i].start, (int)spans[i].length, left, width, cell_area);
        }
        gdk_cairo_set_source_rgba(cr, &disabled);
        for (size_t byte = spans[i].length ? 1 : 0; byte < cells; ++byte)
            gtk_hex_draw_symbol(layout, cr, "\xc2\xb7", 2, left + byte * width, width, cell_area);
    }
    cairo_restore(cr);
    g_object_unref(layout);
}
static void gtk_hex_symbol_renderer_class_init(GtkHexSymbolRendererClass *klass)
{
    GTK_CELL_RENDERER_CLASS(klass)->render = gtk_hex_symbol_render;
}
static void gtk_hex_symbol_renderer_init(GtkHexSymbolRenderer *renderer)
{
    (void)renderer;
}
static size_t gtk_hex_hit(xxwidgets_widget *w, double x, double y, size_t *bytes)
{
    *bytes = 1;
    GtkTreeView *tree = GTK_TREE_VIEW(w->native);
    GtkTreePath *path = NULL;
    GtkTreeViewColumn *column;
    int cell_x, cell_y, index = 0, trailing = 0;
    if (!gtk_tree_view_get_path_at_pos(tree, (int)x, (int)y, &path, &column, &cell_x, &cell_y))
        return SIZE_MAX;
    size_t row = (size_t)gtk_tree_path_get_indices(path)[0];
    unsigned col = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(column), "hex-column"));
    GList *cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));
    GtkCellRenderer *cell = cells ? cells->data : NULL;
    PangoLayout *layout = gtk_hex_layout(GTK_WIDGET(tree), cell, xxwidgets_hexview_cell(w, row, col));
    gint xpad = 0;
    if (cell)
        g_object_get(cell, "xpad", &xpad, NULL);
    size_t offset = SIZE_MAX;
    if (col == 2) {
        double width = gtk_hex_character_width(layout);
        size_t byte = cell_x > xpad ? (size_t)((cell_x - xpad) / width) : 0;
        xxwidgets_hex_span span;
        if (xxwidgets_hexview_hit_span(w, row, col, byte, &span)) {
            offset = span.offset;
            *bytes = span.bytes;
        }
    } else {
        pango_layout_xy_to_index(layout, (cell_x - xpad) * PANGO_SCALE, 0, &index, &trailing);
        offset = xxwidgets_hexview_hit(w, row, col, (size_t)(index < 0 ? 0 : index));
    }
    g_list_free(cells);
    g_object_unref(layout);
    gtk_tree_path_free(path);
    if (offset != SIZE_MAX)
        xxwidgets_hexview_activate_column(w, col);
    return offset;
}
static gboolean gtk_hex_pointer(GtkWidget *native, GdkEventButton *event, gpointer data)
{
    xxwidgets_widget *w = data;
    if (w->app->syncing || !xxwidgets_focusable(w))
        return FALSE;
    if (event->type == GDK_BUTTON_RELEASE) {
        w->hex_dragging = 0;
        return event->button == 1;
    }
    if (event->button != 1 && event->button != 3)
        return FALSE;
    gtk_widget_grab_focus(native);
    size_t bytes, at = gtk_hex_hit(w, event->x, event->y, &bytes);
    if (event->button == 1) {
        w->hex_dragging = at != SIZE_MAX;
        if (at != SIZE_MAX)
            (void)xxwidgets_hexview_pointer_span(w, at, bytes, (event->state & GDK_SHIFT_MASK) != 0);
    } else {
        if (at != SIZE_MAX && !xxwidgets_hexview_contains(w, at, bytes))
            (void)xxwidgets_hexview_pointer_span(w, at, bytes, 0);
        gtk_hex_menu(w, 3, event->button, event->time);
    }
    return TRUE;
}
static gboolean gtk_hex_motion(GtkWidget *native, GdkEventMotion *event, gpointer data)
{
    xxwidgets_widget *w = data;
    if (!w->hex_dragging || !(event->state & GDK_BUTTON1_MASK) || !xxwidgets_focusable(w))
        return FALSE;
    size_t bytes, at = gtk_hex_hit(w, event->x, event->y, &bytes);
    if (at == SIZE_MAX) {
        GtkTreePath *first = NULL, *last = NULL;
        if (gtk_tree_view_get_visible_range(GTK_TREE_VIEW(native), &first, &last)) {
            size_t row = (size_t)gtk_tree_path_get_indices(event->y < 0 ? first : last)[0];
            if (event->y < 0 && row)
                --row;
            else if (event->y >= 0 && row + 1 < w->item_count)
                ++row;
            at = row * w->hex_columns + w->hex_cursor % w->hex_columns;
            if (at >= w->hex_size)
                at = w->hex_size - 1;
            gtk_tree_path_free(first);
            gtk_tree_path_free(last);
        }
    }
    if (at != SIZE_MAX)
        (void)xxwidgets_hexview_pointer_span(w, at, bytes, 1);
    return TRUE;
}
static void gtk_hex_cell(GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    xxwidgets_widget *w = data;
    GtkTreePath *path = gtk_tree_model_get_path(model, iter);
    size_t row = (size_t)gtk_tree_path_get_indices(path)[0];
    size_t col = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(column), "hex-column"));
    if (col == 2) {
        g_object_set_data(G_OBJECT(cell), "hex-widget", w);
        g_object_set_data(G_OBJECT(cell), "hex-row", GUINT_TO_POINTER((guint)row));
    }
    xxwidgets_hex_span spans[64];
    size_t count = xxwidgets_hexview_selection_spans(w, row, col, spans);
    PangoAttrList *attributes = pango_attr_list_new();
    int bold_nonzero = 0;
    if (col == 1 && xxwidgets_hexview_get_bold_nonzero(w, &bold_nonzero) == XXWIDGETS_OK && bold_nonzero) {
        /* Weight is the only overridden font attribute; family, size and style
         * continue to come from the configured table font. */
        pango_attr_list_insert(attributes, pango_attr_weight_new(PANGO_WEIGHT_NORMAL));
        xxwidgets_hex_span bold[64];
        size_t bold_count = xxwidgets_hexview_bold_spans(w, row, col, bold);
        for (size_t i = 0; i < bold_count; ++i) {
            PangoAttribute *weight = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
            weight->start_index = (guint)bold[i].start;
            weight->end_index = (guint)(bold[i].start + bold[i].length);
            pango_attr_list_change(attributes, weight);
        }
    }
    GdkRGBA foreground, background, selected, selected_text;
    GtkStyleContext *style = gtk_widget_get_style_context(GTK_WIDGET(w->native));
    gtk_style_context_get_color(style, GTK_STATE_FLAG_NORMAL, &foreground);
    gtk_style_context_get_background_color(style, GTK_STATE_FLAG_NORMAL, &background);
    gtk_style_context_get_color(style, GTK_STATE_FLAG_SELECTED, &selected_text);
    gtk_style_context_get_background_color(style, GTK_STATE_FLAG_SELECTED, &selected);
    xxwidgets_hex_span all[64]; size_t all_count = xxwidgets_hexview_spans(w, row, col, all);
    for (size_t i = 0; i < all_count; ++i) {
        uint32_t rgb;
        if (!xxwidgets_hexview_highlight_span(w, all[i].offset, all[i].bytes, &rgb)) continue;
        PangoAttribute *tint = pango_attr_background_new((guint16)(((rgb >> 16) & 255) * 257), (guint16)(((rgb >> 8) & 255) * 257), (guint16)((rgb & 255) * 257));
        tint->start_index = (guint)all[i].start; tint->end_index = (guint)(all[i].start + all[i].length); pango_attr_list_change(attributes, tint);
    }
    for (size_t i = 0; i < count; ++i) {
        PangoAttribute *bg =
            pango_attr_background_new((guint16)(selected.red * 65535), (guint16)(selected.green * 65535), (guint16)(selected.blue * 65535));
        PangoAttribute *fg = pango_attr_foreground_new((guint16)(selected_text.red * 65535), (guint16)(selected_text.green * 65535),
                                                       (guint16)(selected_text.blue * 65535));
        bg->start_index = fg->start_index = (guint)spans[i].start;
        bg->end_index = fg->end_index = (guint)(spans[i].start + spans[i].length);
        pango_attr_list_change(attributes, bg);
        pango_attr_list_change(attributes, fg);
    }
    g_object_set(cell, "attributes", attributes, "foreground-rgba", &foreground, "cell-background-rgba", &background, NULL);
    pango_attr_list_unref(attributes);
    gtk_tree_path_free(path);
}
static void gtk_hex_header(GtkTreeViewColumn *column, gpointer data)
{
    gtk_hex_menu(data, GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(column), "hex-column")), 0, gtk_get_current_event_time());
}
static gboolean gtk_hex_header_right(GtkWidget *button, GdkEventButton *event, gpointer data)
{
    if (event->button != 3) return FALSE;
    gtk_hex_menu(data, GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "hex-column")), 3, event->time);
    return TRUE;
}
static void gtk_hex_resized(GObject *column, GParamSpec *property, gpointer data)
{
    xxwidgets_widget *widget = data;
    size_t index = GPOINTER_TO_UINT(g_object_get_data(column, "hex-column"));
    (void)property;
    if (!widget->app->syncing && index < 3) widget->hex_widths[index] = MAX(4, (gtk_tree_view_column_get_width(GTK_TREE_VIEW_COLUMN(column)) + 4) / gtk_hex_cell_width(widget));
}

static void scanresults_selected(GtkTreeSelection *selection, gpointer data)
{
    xxwidgets_widget *widget = data;
    (void)selection;
    if (widget->app->syncing || !xxwidgets_focusable(widget)) return;
    int previous = widget->value;
    scanresults_selection_read(widget);
    if (widget->kind == XXWIDGETS_HEXVIEW) {
        int selected = widget->value; widget->value = previous;
        if (xxwidgets_hexview_selection_input(widget, selected) != XXWIDGETS_OK) return;
    }
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
    if (widget->kind == XXWIDGETS_HEXVIEW) {
        gtk_widget_queue_draw(GTK_WIDGET(tree));
        if (state->hex_overview) gtk_widget_queue_draw(state->hex_overview);
        for (size_t i = 0; i < 3; ++i) {
            GtkTreeViewColumn *column = gtk_tree_view_get_column(tree, (gint)i);
            gtk_tree_view_column_set_title(column, xxwidgets_hexview_title(widget, i));
            gtk_tree_view_column_set_fixed_width(column, (int)xxwidgets_hexview_column_width(widget, i) * gtk_hex_cell_width(widget));
        }
        if (refreshed && !widget->hex_sync_full && state->rendered_items == widget->item_count) {
            for (size_t i = 0; i < widget->hex_sync_dirty_count; ++i) {
                GtkTreeIter iter;
                if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(store), &iter, NULL, (gint)widget->hex_sync_dirty_rows[i]))
                    for (size_t col = 0; col < 3; ++col)
                        gtk_list_store_set(store, &iter, (gint)col, xxwidgets_hexview_cell(widget, widget->hex_sync_dirty_rows[i], col), -1);
            }
            state->rendered_hex_revision = widget->hex_revision; refreshed = 0;
        }
    }
    if (refreshed) {
        size_t row, column, columns = xxwidgets_tabular_column_count(widget);
        if (xxwidgets_debugger_table_kind(widget)) {
            for (column = 0; column < columns; ++column) {
                GtkTreeViewColumn *native_column = gtk_tree_view_get_column(tree, (gint)column);
                GList *cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(native_column));
                PangoLayout *layout = gtk_hex_layout(GTK_WIDGET(tree), cells ? cells->data : NULL, "0");
                int needed = (int)ceil(xxwidgets_tabular_column_width(widget, column) * gtk_hex_character_width(layout)) + 12;
                int current = gtk_tree_view_column_get_width(native_column);
                int fixed = gtk_tree_view_column_get_fixed_width(native_column);
                if (fixed > current) current = fixed;
                /* Grow for a wider model without undoing manual widening. */
                if (needed > current) gtk_tree_view_column_set_fixed_width(native_column, needed);
                g_object_unref(layout);
                g_list_free(cells);
            }
        }
        gtk_list_store_clear(store);
        for (row = 0; row < widget->item_count; ++row) {
            GtkTreeIter iter;
            gtk_list_store_append(store, &iter);
            for (column = 0; column < columns; ++column) {
                GValue value = G_VALUE_INIT;
                g_value_init(&value, G_TYPE_STRING);
                g_value_set_string(&value, xxwidgets_tabular_cell(widget, row, column));
                gtk_list_store_set_value(store, &iter, (gint)column, &value);
                g_value_unset(&value);
            }
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
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget->native)), &model, &iter))
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
    if (widget->app->syncing || !xxwidgets_focusable(widget) || ((gtk_widget_state *)widget->platform)->tree_collapsing) return;
    treeview_selection_read(widget);
    xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, widget->value);
}

static gboolean treeview_before_collapse(GtkTreeView *native, GtkTreeIter *iter, GtkTreePath *path, gpointer data)
{
    xxwidgets_widget *widget = data;
    gtk_widget_state *state = widget->platform;
    (void)native;
    (void)iter;
    (void)path;
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
        if (xxwidgets_treeview_row_of_node(widget, node) != SIZE_MAX && xxwidgets_treeview_get_expanded(widget, node, &expanded) == XXWIDGETS_OK && expanded) {
            GtkTreePath *path = gtk_tree_model_get_path(model, &state->tree_iters[node]);
            if (path) {
                gtk_tree_view_expand_row(tree, path, FALSE);
                gtk_tree_path_free(path);
            }
        }
    }
    --widget->app->syncing;
}

static void treeview_expansion(GtkTreeView *native, GtkTreeIter *iter, GtkTreePath *path, xxwidgets_widget *widget, int expanded)
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
    /* A mark's first character presses it on the selected row (S, ?). */
    if (!(key->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_META_MASK)) &&
        xxwidgets_treeview_mark_key(widget, (unsigned int)gdk_keyval_to_unicode(key->keyval)))
        return TRUE;
    if (key->keyval == GDK_KEY_space) {
        if (xxwidgets_treeview_has_children(widget, (size_t)widget->value) && xxwidgets_treeview_get_expanded(widget, (size_t)widget->value, &expanded) == XXWIDGETS_OK)
            xxwidgets_treeview_user_expand(widget, (size_t)widget->value, !expanded);
        else xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, widget->value);
        return TRUE;
    }
    return FALSE;
}

static void treeview_cell_color(GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    int node = -1;
    uint32_t color, background;
    GdkRGBA foreground, backdrop;
    GtkWidget *view = gtk_tree_view_column_get_tree_view(column);
    GtkStyleContext *context = gtk_widget_get_style_context(view);
    xxwidgets_tree_color colors;
    int dark;
    (void)column;
    gtk_tree_model_get(model, iter, 1, &node, -1);
    colors = xxwidgets_treeview_node_color((xxwidgets_widget *)data, (size_t)node);
    gtk_style_context_get_color(context, GTK_STATE_FLAG_NORMAL, &foreground);
    gtk_style_context_get_background_color(context, GTK_STATE_FLAG_NORMAL, &backdrop);
    dark = foreground.red + foreground.green + foreground.blue > backdrop.red + backdrop.green + backdrop.blue;
    color = dark ? colors.dark_foreground : colors.foreground;
    background = dark ? colors.dark_background : colors.background;
    if (color == UINT32_MAX) g_object_set(cell, "foreground-set", FALSE, NULL);
    else {
        foreground.red = ((color >> 16) & 255) / 255.0;
        foreground.green = ((color >> 8) & 255) / 255.0;
        foreground.blue = (color & 255) / 255.0;
        foreground.alpha = 1;
        g_object_set(cell, "foreground-rgba", &foreground, "foreground-set", TRUE, NULL);
    }
    if (background == UINT32_MAX) g_object_set(cell, "cell-background-set", FALSE, NULL);
    else {
        backdrop.red = ((background >> 16) & 255) / 255.0;
        backdrop.green = ((background >> 8) & 255) / 255.0;
        backdrop.blue = (background & 255) / 255.0;
        backdrop.alpha = 1;
        g_object_set(cell, "cell-background-rgba", &backdrop, "cell-background-set", TRUE, NULL);
    }
}

/* Mark columns follow the text column; each knows its mark index plus one. */
static void treeview_mark_cell(GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    xxwidgets_widget *widget = data;
    size_t mark = (size_t)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column), "xxwidgets-mark")) - 1;
    int node = -1;
    gtk_tree_model_get(model, iter, 1, &node, -1);
    g_object_set(cell, "text", node >= 0 && (xxwidgets_treeview_node_marks(widget, (size_t)node) >> mark & 1u)
        ? xxwidgets_treeview_mark_label(widget, mark) : "", NULL);
}

static gboolean treeview_mark_press(GtkWidget *native, GdkEventButton *button, gpointer data)
{
    xxwidgets_widget *widget = data;
    GtkTreePath *path = NULL;
    GtkTreeViewColumn *column = NULL;
    GtkTreeIter iter;
    int node = -1, mark;
    if (button->type != GDK_BUTTON_PRESS || button->button != 1 || widget->app->syncing || !xxwidgets_focusable(widget) ||
        !xxwidgets_treeview_mark_count(widget) ||
        !gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(native), (gint)button->x, (gint)button->y, &path, &column, NULL, NULL))
        return FALSE;
    mark = column ? GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column), "xxwidgets-mark")) - 1 : -1;
    if (mark >= 0 && gtk_tree_model_get_iter(gtk_tree_view_get_model(GTK_TREE_VIEW(native)), &iter, path))
        gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(native)), &iter, 1, &node, -1);
    if (node < 0 || !(xxwidgets_treeview_node_marks(widget, (size_t)node) >> mark & 1u)) {
        gtk_tree_path_free(path);
        return FALSE;
    }
    {
        /* A button, not the row: select it as feedback, then report the press. */
        uint64_t revision = widget->tree_content_revision;
        gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(native)), path);
        gtk_tree_path_free(path);
        if (revision == widget->tree_content_revision) xxwidgets_treeview_mark_click(widget, (size_t)node, (size_t)mark);
    }
    return TRUE;
}

static void treeview_activated(GtkTreeView *native, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data)
{
    xxwidgets_widget *widget = data;
    GtkTreeIter iter;
    int node, expanded;
    (void)column;
    if (widget->app->syncing || !xxwidgets_focusable(widget)) return;
    if (!gtk_tree_model_get_iter(gtk_tree_view_get_model(native), &iter, path)) return;
    gtk_tree_model_get(gtk_tree_view_get_model(native), &iter, 1, &node, -1);
    if (node < 0) return;
    if (xxwidgets_treeview_has_children(widget, (size_t)node) && xxwidgets_treeview_get_expanded(widget, (size_t)node, &expanded) == XXWIDGETS_OK)
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
            gtk_tree_store_set(store, &iters[node], 0, xxwidgets_treeview_display_text(widget, node), 1, (int)node, -1);
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
    for (node = 0; node < XXWIDGETS_TREE_MARKS_MAX; ++node) {
        GtkTreeViewColumn *column = gtk_tree_view_get_column(tree, (gint)node + 1);
        if (column) gtk_tree_view_column_set_visible(column, node < xxwidgets_treeview_mark_count(widget));
    }
    gtk_widget_queue_draw(GTK_WIDGET(tree));
    atk_object_set_name(gtk_widget_get_accessible(GTK_WIDGET(tree)), widget->text);
    return XXWIDGETS_OK;
}

/* changed: the user changed the selection; otherwise only reading it. */
static void browser_selection_read(xxwidgets_widget *widget, int changed)
{
    GtkTreeView *tree = GTK_TREE_VIEW(widget->native);
    GList *paths = gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(tree), NULL), *item;
    GtkTreePath *cursor = NULL;
    int previous = widget->value, first = -1, at = -1;
    widget->value = -1;
    xxwidgets_archivebrowser_selection_clear(widget);
    for (item = paths; item; item = item->next) {
        int row = gtk_tree_path_get_indices(item->data)[0];
        xxwidgets_archivebrowser_selection_input(widget, (size_t)row, 1);
        if (first < 0) first = row;
    }
    g_list_free_full(paths, (GDestroyNotify)gtk_tree_path_free);
    /* The current row: after a selection change the cursor's, when it is
     * selected; else the one that was current while it stays selected
     * (Ctrl+arrows move only the cursor, and the status line names that one);
     * else the first selected. */
    gtk_tree_view_get_cursor(tree, &cursor, NULL);
    if (cursor) {
        int row = gtk_tree_path_get_indices(cursor)[0];
        if (xxwidgets_archivebrowser_row_selected(widget, (size_t)row)) at = row;
        gtk_tree_path_free(cursor);
    }
    if (previous >= 0 && (size_t)previous < widget->item_count && !xxwidgets_archivebrowser_row_selected(widget, (size_t)previous)) previous = -1;
    widget->value = changed ? (at >= 0 ? at : previous) : (previous >= 0 ? previous : at);
    if (widget->value < 0) widget->value = first;
    ((gtk_widget_state *)widget->platform)->browser_value = widget->value;
}

static void browser_selected(GtkTreeSelection *selection, gpointer data)
{
    xxwidgets_widget *widget = data;
    (void)selection;
    if (widget->app->syncing) return;
    browser_selection_read(widget, 1);
    xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, widget->value);
}

static void browser_activated(GtkTreeView *native, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data)
{
    xxwidgets_widget *widget = data;
    (void)native;
    (void)column;
    if (!widget->app->syncing) xxwidgets_archivebrowser_user_activate(widget, gtk_tree_path_get_indices(path)[0]);
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
    gtk_widget_translate_coordinates(GTK_WIDGET(widget->native), state->root, x, y, &local_x, &local_y);
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
        x = area.x + 16;
        y = area.y + area.height / 2;
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
    if (event->button == 1 && event->type == GDK_BUTTON_PRESS && (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) && !gtk_widget_has_focus(native) &&
        xxwidgets_focusable(widget) && !widget->app->syncing) {
        /* GtkTreeView extends or toggles the selection on a Shift/Ctrl click
         * only when it already has focus: take it first, so that click counts. */
        gtk_widget_grab_focus(native);
        return FALSE;
    }
    if (event->button != 3) return FALSE;
    /* Rows only: a column header's press arrives here too, in the header's
     * own coordinates, and must not clear the selection. */
    if (event->window != gtk_tree_view_get_bin_window(tree)) return FALSE;
    if (!xxwidgets_focusable(widget) || widget->app->syncing) return TRUE;
    gtk_tree_view_convert_bin_window_to_widget_coords(tree, x, y, &x, &y);
    selection = gtk_tree_view_get_selection(tree);
    /* Select before dispatch without exposing an intermediate callback that
     * could replace the browser model while its hit-test path is borrowed. */
    ++widget->app->syncing;
    if (gtk_tree_view_get_path_at_pos(tree, bin_x, bin_y, &path, NULL, NULL, NULL)) {
        /* The clicked row also becomes the keyboard cursor, so Enter and the
         * arrows act on the row the status line names. set_cursor selects
         * only that row: a selection it already belongs to is kept. */
        GList *kept = gtk_tree_selection_path_is_selected(selection, path) ? gtk_tree_selection_get_selected_rows(selection, NULL) : NULL, *item;
        gtk_tree_view_set_cursor(tree, path, NULL, FALSE);
        for (item = kept; item; item = item->next) gtk_tree_selection_select_path(selection, item->data);
        if (kept) {
            /* Each re-select moved the Shift-range anchor; it belongs on the clicked row. */
            gtk_tree_selection_unselect_path(selection, path);
            gtk_tree_selection_select_path(selection, path);
        }
        g_list_free_full(kept, (GDestroyNotify)gtk_tree_path_free);
        widget->value = gtk_tree_path_get_indices(path)[0];
        gtk_tree_path_free(path);
    } else {
        gtk_tree_selection_unselect_all(selection);
        widget->value = -1;
    }
    --widget->app->syncing;
    {
        int clicked = widget->value;
        browser_selection_read(widget, 1);
        widget->value = clicked;
        ((gtk_widget_state *)widget->platform)->browser_value = clicked;
    }
    xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, widget->value);
    gtk_widget_grab_focus(native);
    browser_context_at(widget, x, y);
    return TRUE;
}

static gboolean systemprocessesviewer_button_press(GtkWidget *native, GdkEventButton *event, gpointer data)
{
    xxwidgets_widget *widget = data;
    GtkTreeView *tree = GTK_TREE_VIEW(native);
    GtkTreePath *path = NULL;
    int x = (int)event->x, y = (int)event->y;
    if (event->button != 3 || event->window != gtk_tree_view_get_bin_window(tree)) return FALSE;
    if (!xxwidgets_focusable(widget) || widget->app->syncing) return TRUE;
    ++widget->app->syncing;
    if (gtk_tree_view_get_path_at_pos(tree, x, y, &path, NULL, NULL, NULL)) {
        gtk_tree_view_set_cursor(tree, path, NULL, FALSE);
        widget->value = gtk_tree_path_get_indices(path)[0];
        gtk_tree_path_free(path);
    } else {
        gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(tree));
        widget->value = -1;
    }
    --widget->app->syncing;
    gtk_widget_grab_focus(native);
    xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, widget->value);
    /* The same coordinate translation serves any table inside a scroller. */
    gtk_tree_view_convert_bin_window_to_widget_coords(tree, x, y, &x, &y);
    browser_context_at(widget, x, y);
    return TRUE;
}

static gboolean systemprocessesviewer_key(GtkWidget *native, GdkEventKey *event, gpointer data)
{
    if (event->keyval == GDK_KEY_Menu ||
        (event->keyval == GDK_KEY_F10 && (event->state & GDK_SHIFT_MASK)))
        return browser_popup_menu(native, data);
    return FALSE;
}

static void table_activated(GtkTreeView *native, GtkTreePath *path,
    GtkTreeViewColumn *column, gpointer data)
{
    xxwidgets_widget *widget = data;
    int row = gtk_tree_path_get_indices(path)[0];
    (void)native;
    (void)column;
    if (widget->app->syncing || !xxwidgets_focusable(widget) || row < 0 ||
        (size_t)row >= widget->item_count) return;
    widget->value = row;
    xxwidgets_emit(widget, XXWIDGETS_EVENT_ACTIVATE, row);
}

static gboolean gtk_die_key(GtkWidget *, GdkEventKey *, gpointer);
static gboolean window_shortcut(GtkWidget *native, GdkEventKey *event, gpointer data)
{
    xxwidgets_widget *window = data;
    GtkWidget *focused = gtk_window_get_focus(GTK_WINDOW(native));
    unsigned int key, modifiers;
    for (xxwidgets_widget *candidate = window->app->widgets; candidate; candidate = candidate->next)
        if (candidate->parent == window && candidate->kind == XXWIDGETS_DIE_SIGNATURE_EDIT && candidate->native == focused &&
            gtk_die_key(focused, event, candidate)) return TRUE;
    if (!gtk_key_input(event, &key, &modifiers)) return FALSE;
    return xxwidgets_shortcut_dispatch(data, key, modifiers);
}

static gboolean browser_key(GtkWidget *native, GdkEventKey *event, gpointer data)
{
    xxwidgets_widget *widget = data;
    if (event->keyval == GDK_KEY_Menu || (event->keyval == GDK_KEY_F10 && (event->state & GDK_SHIFT_MASK))) return browser_popup_menu(native, data);
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
        xxwidgets_archivebrowser_user_sort(widget, (xxwidgets_archive_column)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column), "xxwidgets-column")));
}

static void browser_sync(xxwidgets_widget *widget)
{
    gtk_widget_state *state = widget->platform;
    GtkTreeView *tree = GTK_TREE_VIEW(widget->native);
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(tree));
    const char *directory = xxwidgets_archivebrowser_directory(widget);
    const char *archive = xxwidgets_archivebrowser_archive(widget);
    /* No archive yet: an empty path bar rather than a bare ":/". */
    char *address = archive[0] ? g_strdup_printf("%s:/%s", archive, directory) : g_strdup(directory);
    size_t row;
    int column, rebuilt = 0;
    int columns = (int)xxwidgets_archivebrowser_column_count(widget);
    if (address) {
        gtk_entry_set_text(GTK_ENTRY(state->browser_address), address);
        /* The archive name and folder are at the end of a long path. */
        gtk_editable_set_position(GTK_EDITABLE(state->browser_address), -1);
        g_free(address);
    }
    if (!directory[0] && gtk_widget_is_focus(state->browser_up)) gtk_widget_grab_focus(GTK_WIDGET(tree)); /* Up is about to become insensitive */
    gtk_widget_set_sensitive(state->browser_up, directory[0] != '\0');
    if (state->rendered_hex_revision != widget->browser_revision) {
        GType *types = g_new(GType, columns + 1);
        rebuilt = 1;
        for (column = 0; column <= columns; ++column) types[column] = G_TYPE_STRING;
        store = gtk_list_store_newv(columns + 1, types);
        g_free(types);
        gtk_tree_view_set_model(tree, GTK_TREE_MODEL(store));
        g_object_unref(store);
        /* A new model resets the type-ahead column to the first string one. */
        gtk_tree_view_set_search_column(tree, 1);
        /* Columns only grow by default: a long name from an earlier listing
         * would keep pushing the other columns out of view. */
        gtk_tree_view_columns_autosize(tree);
        while (gtk_tree_view_get_n_columns(tree) > 5) gtk_tree_view_remove_column(tree, gtk_tree_view_get_column(tree, 5));
        for (column = 5; column < columns; ++column) {
            GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
            GtkTreeViewColumn *view_column =
                gtk_tree_view_column_new_with_attributes(xxwidgets_archivebrowser_column_title(widget, (size_t)column), renderer, "text", column + 1, NULL);
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
            gtk_list_store_set(store, &iter, 0, entry.is_directory ? "folder" : "text-x-generic", 1,
                               xxwidgets_archivebrowser_cell(widget, row, XXWIDGETS_ARCHIVE_COLUMN_NAME), 2,
                               xxwidgets_archivebrowser_cell(widget, row, XXWIDGETS_ARCHIVE_COLUMN_SIZE), 3,
                               xxwidgets_archivebrowser_cell(widget, row, XXWIDGETS_ARCHIVE_COLUMN_PACKED_SIZE), 4,
                               xxwidgets_archivebrowser_cell(widget, row, XXWIDGETS_ARCHIVE_COLUMN_MODIFIED), 5,
                               xxwidgets_archivebrowser_cell(widget, row, XXWIDGETS_ARCHIVE_COLUMN_ATTRIBUTES), -1);
            for (column = 5; column < columns; ++column)
                gtk_list_store_set(store, &iter, column + 1, xxwidgets_archivebrowser_cell(widget, row, (xxwidgets_archive_column)column), -1);
        }
        state->rendered_hex_revision = widget->browser_revision;
    }
    for (column = 0; column < columns; ++column) {
        GtkTreeViewColumn *native_column = gtk_tree_view_get_column(tree, column);
        gtk_tree_view_column_set_sort_indicator(native_column, column == (int)xxwidgets_archivebrowser_sort_column(widget));
        gtk_tree_view_column_set_sort_order(native_column, xxwidgets_archivebrowser_sort_descending(widget) ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
    }
    if (widget->value >= 0) {
        /* A new listing or folder has no keyboard cursor, and Enter acts on the
         * cursor row only. Set it before the selection, which it would reset.
         * When the program has made another row current since the view last
         * agreed (a folder shown, then a member chosen in it), the cursor
         * follows; a cursor the user moved off the selection stays. */
        GtkTreePath *cursor = NULL;
        gtk_tree_view_get_cursor(tree, &cursor, NULL);
        if (cursor) {
            int depth = 0, *at = gtk_tree_path_get_indices_with_depth(cursor, &depth);
            if (depth < 1 || (at[0] != widget->value && widget->value != state->browser_value)) {
                gtk_tree_path_free(cursor);
                cursor = NULL;
            }
        }
        if (!cursor) {
            GtkTreePath *path = gtk_tree_path_new_from_indices(widget->value, -1);
            gtk_tree_view_set_cursor(tree, path, NULL, FALSE);
            gtk_tree_path_free(path);
        } else gtk_tree_path_free(cursor);
    }
    {
        GtkTreeSelection *selection = gtk_tree_view_get_selection(tree);
        size_t i;
        /* Selecting a row moves GTK's Shift-range anchor, so the selection is
         * applied again only when the view's differs (a new listing, or the
         * program changed it); a layout pass keeps the user's anchor. */
        int same = !rebuilt;
        for (i = 0; same && i < widget->item_count; ++i) {
            GtkTreePath *path = gtk_tree_path_new_from_indices((int)i, -1);
            int wanted = xxwidgets_archivebrowser_row_selected(widget, i) || (int)i == widget->value;
            if (!gtk_tree_selection_path_is_selected(selection, path) != !wanted) same = 0;
            gtk_tree_path_free(path);
        }
        if (!same) {
            gtk_tree_selection_unselect_all(selection);
            for (i = 0; i < widget->item_count; ++i)
                if (xxwidgets_archivebrowser_row_selected(widget, i)) {
                    GtkTreePath *path = gtk_tree_path_new_from_indices((int)i, -1);
                    gtk_tree_selection_select_path(selection, path);
                    gtk_tree_path_free(path);
                }
        }
        if (widget->value >= 0) {
            GtkTreePath *path = gtk_tree_path_new_from_indices(widget->value, -1);
            if (!same) {
                /* The anchor belongs on the current row, as after a click there. */
                gtk_tree_selection_unselect_path(selection, path);
                gtk_tree_selection_select_path(selection, path);
            }
            gtk_tree_view_scroll_to_cell(tree, path, NULL, FALSE, 0, 0);
            gtk_tree_path_free(path);
        }
    }
    state->browser_value = widget->value;
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

/* A plain list (an operation log) remembers whether its view is at the end... */
static void list_scrolled(GtkAdjustment *adjustment, gpointer data)
{
    xxwidgets_widget *widget = data;
    gtk_widget_state *state = widget->platform;
    if (state) state->follow_tail = gtk_adjustment_get_value(adjustment) + gtk_adjustment_get_page_size(adjustment) >= gtk_adjustment_get_upper(adjustment) - 2.0;
}

/* ...and stays there when its height or content changes. A user who scrolled
 * up keeps that position. */
static void list_resized(GtkAdjustment *adjustment, gpointer data)
{
    xxwidgets_widget *widget = data;
    gtk_widget_state *state = widget->platform;
    double end;
    if (!state || !state->follow_tail || widget->value < 0 || (size_t)widget->value + 1 != widget->item_count) return;
    end = gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment);
    if (gtk_adjustment_get_value(adjustment) != end) gtk_adjustment_set_value(adjustment, end);
}

/* GDK keeps one GdkMonitor per output and changes its geometry in place:
 * re-clamp every window's minimum when the screen layout changes. */
static void screen_changed(GdkScreen *screen, gpointer data)
{
    xxwidgets_app *app = data;
    xxwidgets_widget *widget;
    (void)screen;
    for (widget = app->widgets; widget; widget = widget->next)
        if (widget->kind == XXWIDGETS_WINDOW && widget->platform) {
            gtk_widget_state *state = widget->platform;
            state->min_width = state->min_height = -1;
            window_update_minimum(widget);
        }
}

static xxwidgets_status gtk_init_backend(xxwidgets_app *app)
{
    gtk_app_state *state;
    GtkWidget *probe;
    PangoContext *context;
    PangoFontMetrics *metrics;
    int control_height;
    if (!gtk_init_check(NULL, NULL)) return XXWIDGETS_UNAVAILABLE;
    state = calloc(1, sizeof(*state));
    if (!state) return XXWIDGETS_OUT_OF_MEMORY;
    state->cell_width = 8;
    state->cell_height = 20;
    if (!gtk_check_version(3, 20, 0)) {
        static int installed;
        GtkCssProvider *css;
        if (!installed && (css = gtk_css_provider_new()) != NULL) {
            /* Installed once per process: the screen keeps the provider. */
            if (gtk_css_provider_load_from_data(css, GRID_CSS, -1, NULL))
                gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            g_object_unref(css);
            installed = 1;
        }
    }
    probe = gtk_label_new("M");
    g_object_ref_sink(probe);
    context = gtk_widget_get_pango_context(probe);
    metrics = pango_context_get_metrics(context, pango_context_get_font_description(context), pango_context_get_language(context));
    if (metrics) {
        int width = PANGO_PIXELS_CEIL(pango_font_metrics_get_approximate_char_width(metrics));
        int height = PANGO_PIXELS_CEIL(pango_font_metrics_get_ascent(metrics) + pango_font_metrics_get_descent(metrics)) + 4;
        if (width > 0) state->cell_width = width;
        if (height > 0) state->cell_height = height;
        pango_font_metrics_unref(metrics);
    }
    gtk_widget_destroy(probe);
    g_object_unref(probe);
    /* One-row controls get the whole cell minus GRID_ROW_GAP, so the cell
     * must hold the tallest of them; labels alone would need only the text. */
    control_height = measure_control_height();
    if (control_height + GRID_ROW_GAP > state->cell_height) state->cell_height = control_height + GRID_ROW_GAP;
    state->base_cell_width = state->cell_width;
    g_signal_connect(gdk_screen_get_default(), "monitors-changed", G_CALLBACK(screen_changed), app);
    g_signal_connect(gdk_screen_get_default(), "size-changed", G_CALLBACK(screen_changed), app);
    for (size_t role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role) {
        state->font_css[role] = gtk_css_provider_new();
        if (state->font_css[role]) font_css_load(state->font_css[role], NULL);
    }
    app->platform = state;
    return XXWIDGETS_OK;
}

static void gtk_shutdown_backend(xxwidgets_app *app)
{
    gtk_app_state *state = app->platform;
    if (state && state->fit_source) g_source_remove(state->fit_source);
    if (state) g_signal_handlers_disconnect_by_data(gdk_screen_get_default(), app);
    if (state)
        for (size_t role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role) {
            if (state->fonts[role]) pango_font_description_free(state->fonts[role]);
            if (state->font_css[role]) g_object_unref(state->font_css[role]);
        }
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
        if (processed || timeout_ms == 0 || expired || g_get_monotonic_time() >= deadline) break;
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

static GdkAtom file_uri_target(void) { return gdk_atom_intern_static_string("text/uri-list"); }

static gboolean file_drag_motion(GtkWidget *native, GdkDragContext *context, gint x, gint y, guint time, gpointer data)
{
    xxwidgets_widget *widget = data, *window = widget->parent ? widget->parent : widget;
    (void)native; (void)x; (void)y;
    if (!window->accept_file_drops || !g_list_find(gdk_drag_context_list_targets(context), file_uri_target())) return FALSE;
    gdk_drag_status(context, xxwidgets_can_drop_files(window) && (gdk_drag_context_get_actions(context) & GDK_ACTION_COPY)
        ? GDK_ACTION_COPY : 0, time);
    return TRUE;
}

static gboolean file_drag_drop(GtkWidget *native, GdkDragContext *context, gint x, gint y, guint time, gpointer data)
{
    xxwidgets_widget *widget = data, *window = widget->parent ? widget->parent : widget;
    (void)x; (void)y;
    if (!window->accept_file_drops || !g_list_find(gdk_drag_context_list_targets(context), file_uri_target())) return FALSE;
    if (xxwidgets_can_drop_files(window) && (gdk_drag_context_get_actions(context) & GDK_ACTION_COPY))
        gtk_drag_get_data(native, context, file_uri_target(), time);
    else gtk_drag_finish(context, FALSE, FALSE, time);
    return TRUE;
}

static void file_drag_received(GtkWidget *native, GdkDragContext *context, gint x, gint y,
                               GtkSelectionData *selection, guint info, guint time, gpointer data)
{
    xxwidgets_widget *widget = data, *window = widget->parent ? widget->parent : widget;
    gchar **uris = NULL, **paths = NULL;
    size_t count = 0, i;
    int accepted = 0;
    (void)x; (void)y; (void)info;
    if (gtk_selection_data_get_target(selection) != file_uri_target()) return;
    g_signal_stop_emission_by_name(native, "drag-data-received");
    if (!xxwidgets_can_drop_files(window) || !(gdk_drag_context_get_actions(context) & GDK_ACTION_COPY)) goto done;
    uris = gtk_selection_data_get_uris(selection);
    if (!uris || !uris[0]) goto done;
    count = g_strv_length(uris);
    paths = g_try_new0(gchar *, count + 1);
    if (!paths) goto done;
    for (i = 0; i < count; ++i) {
        gchar *host = NULL, *filename = g_filename_from_uri(uris[i], &host, NULL);
        if (!filename || (host && *host && g_ascii_strcasecmp(host, "localhost"))) {
            g_free(host); g_free(filename); goto done;
        }
        g_free(host);
        paths[i] = g_filename_to_utf8(filename, -1, NULL, NULL, NULL);
        g_free(filename);
        if (!paths[i]) goto done;
    }
    accepted = xxwidgets_emit_drop_files(window, (const char *const *)paths, count);
done:
    g_strfreev(paths); g_strfreev(uris);
    gtk_drag_finish(context, accepted, FALSE, time);
}

static xxwidgets_status gtk_accept_file_drops(xxwidgets_widget *window, int accept)
{
    GtkTargetEntry target = {(gchar *)"text/uri-list", 0, 0};
    if (accept) gtk_drag_dest_set(GTK_WIDGET(window->native), 0, &target, 1, GDK_ACTION_COPY);
    else gtk_drag_dest_unset(GTK_WIDGET(window->native));
    return XXWIDGETS_OK;
}

static void gtk_plot_draw(void *user, int kind, double x, double y, double x2, double y2, uint32_t rgb, const char *text)
{
    cairo_t *cr = user;
    cairo_set_source_rgb(cr, ((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0);
    if (kind == 0) { cairo_move_to(cr,x,y); cairo_line_to(cr,x2,y2); cairo_stroke(cr); }
    else if (kind == 1) { cairo_rectangle(cr,x,y,x2-x,y2-y); cairo_fill(cr); }
    else { cairo_select_font_face(cr,"monospace",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_NORMAL); cairo_set_font_size(cr,12); cairo_move_to(cr,x,y+12); cairo_show_text(cr,text); }
}
static gboolean gtk_canvas_paint(GtkWidget *native,cairo_t *cr,gpointer data) {
    xxwidgets_widget *w=data;int width=gtk_widget_get_allocated_width(native),height=gtk_widget_get_allocated_height(native);
    cairo_set_source_rgb(cr,0.92,0.92,0.92);cairo_paint(cr);if(w->kind==XXWIDGETS_SPLITTER){cairo_set_source_rgb(cr,0.55,0.55,0.55);cairo_paint(cr);return FALSE;}
    if(!w->canvas_pixels||!w->canvas_width||!w->canvas_height)return FALSE;size_t n=(size_t)w->canvas_width*w->canvas_height;uint32_t *pixels=malloc(n*4);if(!pixels)return FALSE;
    for(size_t i=0;i<n;++i){uint32_t c=w->canvas_pixels[i];unsigned a=c&255,r=(c>>24),g=(c>>16)&255,b=(c>>8)&255;pixels[i]=(a<<24)|((r*a/255)<<16)|((g*a/255)<<8)|(b*a/255);}
    cairo_surface_t *surface=cairo_image_surface_create_for_data((unsigned char*)pixels,CAIRO_FORMAT_ARGB32,w->canvas_width,w->canvas_height,w->canvas_width*4);double x,y,rw,rh;xxwidgets_canvas_fit(w,width,height,&x,&y,&rw,&rh);
    cairo_save(cr);cairo_translate(cr,x,y);cairo_scale(cr,rw/w->canvas_width,rh/w->canvas_height);cairo_set_source_surface(cr,surface,0,0);cairo_pattern_set_filter(cairo_get_source(cr),CAIRO_FILTER_NEAREST);cairo_paint(cr);cairo_restore(cr);cairo_surface_destroy(surface);free(pixels);
    for(size_t i=0;i<w->canvas_label_count;++i){xxwidgets_canvas_label *l=w->canvas_labels+i;uint32_t c=l->color;cairo_set_source_rgb(cr,(c>>24)/255.0,((c>>16)&255)/255.0,((c>>8)&255)/255.0);cairo_set_font_size(cr,11);cairo_move_to(cr,x+l->x*rw/w->canvas_width,y+l->y*rh/w->canvas_height+11);cairo_show_text(cr,l->text);}
    if(w->value>=0){cairo_set_source_rgb(cr,1,0,0);cairo_rectangle(cr,x+(w->value%w->canvas_width)*rw/w->canvas_width,y+(w->value/w->canvas_width)*rh/w->canvas_height,fmax(2,rw/w->canvas_width),fmax(2,rh/w->canvas_height));cairo_stroke(cr);}return FALSE;
}
static gboolean gtk_canvas_pointer(GtkWidget *native,GdkEventButton *event,gpointer data){if(event->button!=1)return FALSE;xxwidgets_widget *w=data;gtk_widget_grab_focus(native);if(w->kind==XXWIDGETS_SPLITTER)xxwidgets_splitter_input(w,event->x,event->y,gtk_widget_get_allocated_width(native),gtk_widget_get_allocated_height(native));else xxwidgets_canvas_input(w,event->x,event->y,gtk_widget_get_allocated_width(native),gtk_widget_get_allocated_height(native));return TRUE;}
static gboolean gtk_canvas_motion(GtkWidget *native,GdkEventMotion *event,gpointer data){if(!(event->state&GDK_BUTTON1_MASK))return FALSE;xxwidgets_widget *w=data;if(w->kind==XXWIDGETS_SPLITTER)xxwidgets_splitter_input(w,event->x,event->y,gtk_widget_get_allocated_width(native),gtk_widget_get_allocated_height(native));else xxwidgets_canvas_input(w,event->x,event->y,gtk_widget_get_allocated_width(native),gtk_widget_get_allocated_height(native));return TRUE;}
static gboolean gtk_canvas_key(GtkWidget *native,GdkEventKey *event,gpointer data){(void)native;unsigned key=0,mods=0;xxwidgets_widget *w=data;if(!gtk_key_input(event,&key,&mods))return FALSE;return w->kind==XXWIDGETS_SPLITTER?xxwidgets_splitter_key(w,(int)key):xxwidgets_canvas_key(w,(int)key);}

static gboolean gtk_plot_paint(GtkWidget *native, cairo_t *cr, gpointer data)
{
    xxwidgets_entropy_draw(data, gtk_widget_get_allocated_width(native), gtk_widget_get_allocated_height(native), 7, 14, gtk_plot_draw, cr);
    return FALSE;
}
static gboolean gtk_plot_click(GtkWidget *native, GdkEventButton *event, gpointer data)
{
    if (event->button == 1) xxwidgets_entropy_plot_input(data, event->x, gtk_widget_get_allocated_width(native), 7);
    return TRUE;
}

/* Native signature viewport: the shared model owns source, selection and
 * history. Scrolling never creates a second text buffer. */
static PangoLayout *gtk_die_layout(xxwidgets_widget *widget)
{
    gtk_widget_state *state = widget->platform;
    gtk_app_state *app = widget->app->platform;
    PangoFontDescription *font;
    const PangoFontDescription *configured = state->has_preview ? state->preview_font : app->fonts[xxwidgets_widget_font_role(widget)];
    PangoLayout *layout = gtk_widget_create_pango_layout(GTK_WIDGET(widget->native), "0");
    font = configured ? pango_font_description_copy(configured) : pango_font_description_new();
    if (!pango_font_description_get_family(font)) pango_font_description_set_family(font, "monospace");
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    pango_layout_get_pixel_size(layout, &state->die_cell_width, &state->die_cell_height);
    if (state->die_cell_width < 1) state->die_cell_width = 1;
    if (state->die_cell_height < 1) state->die_cell_height = 1;
    return layout;
}

static void gtk_die_sync(xxwidgets_widget *widget)
{
    gtk_widget_state *state = widget->platform;
    xxwidgets_die_edit_view view;
    int width = gtk_widget_get_allocated_width(GTK_WIDGET(widget->native));
    int height = gtk_widget_get_allocated_height(GTK_WIDGET(widget->native));
    PangoLayout *layout = gtk_die_layout(widget);
    size_t rows, columns;
    g_object_unref(layout);
    xxwidgets_die_edit_view_get(widget, &view);
    state->die_gutter = (int)(view.gutter_columns + 2) * state->die_cell_width;
    rows = height > 0 ? (size_t)(height / state->die_cell_height) : 1;
    columns = width > state->die_gutter ? (size_t)((width - state->die_gutter) / state->die_cell_width) : 1;
    if (!rows) rows = 1;
    if (!columns) columns = 1;
    if (rows != state->die_rows || columns != state->die_columns) {
        state->die_rows = rows; state->die_columns = columns;
        xxwidgets_die_edit_page(widget, rows, columns);
        xxwidgets_die_edit_view_get(widget, &view);
    }
    state->die_adjusting = 1;
    gtk_adjustment_configure(state->die_vertical, (double)view.first_line, 0,
        (double)(view.line_count > rows ? view.line_count : rows), 1, (double)rows, (double)rows);
    gtk_adjustment_configure(state->die_horizontal, (double)view.first_column, 0,
        (double)(view.max_columns + 1 > columns ? view.max_columns + 1 : columns), 1, (double)columns, (double)columns);
    state->die_adjusting = 0;
    gtk_widget_queue_draw(GTK_WIDGET(widget->native));
}

static void gtk_die_adjusted(GtkAdjustment *adjustment, gpointer data)
{
    xxwidgets_widget *widget = data;
    gtk_widget_state *state = widget->platform;
    xxwidgets_die_edit_view view;
    int delta;
    if (state->die_adjusting || widget->app->syncing) return;
    xxwidgets_die_edit_view_get(widget, &view);
    delta = (int)gtk_adjustment_get_value(adjustment) - (int)(adjustment == state->die_vertical ? view.first_line : view.first_column);
    xxwidgets_die_edit_scroll(widget, adjustment == state->die_vertical ? delta : 0, adjustment == state->die_horizontal ? delta : 0);
    gtk_die_sync(widget);
}

static void gtk_die_rgb(cairo_t *cr, uint32_t rgb)
{
    cairo_set_source_rgb(cr, ((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0);
}

static gboolean gtk_die_draw(GtkWidget *native, cairo_t *cr, gpointer data)
{
    xxwidgets_widget *widget = data;
    gtk_widget_state *state = widget->platform;
    GtkStyleContext *context = gtk_widget_get_style_context(native);
    xxwidgets_die_edit_view view;
    PangoLayout *layout = gtk_die_layout(widget);
    GdkRGBA text, selection, selected_text;
    int width = gtk_widget_get_allocated_width(native), height = gtk_widget_get_allocated_height(native);
    size_t current = xxwidgets_die_edit_caret_line(widget), count;
    xxwidgets_die_edit_cell *cells;
    text = (GdkRGBA){0, 0, 0, 1}; /* The source palette has a light background. */
    gtk_style_context_get_background_color(context, GTK_STATE_FLAG_SELECTED, &selection);
    gtk_style_context_get_color(context, GTK_STATE_FLAG_SELECTED, &selected_text);
    if (selection.alpha < 0.1) selection = (GdkRGBA){0.20, 0.40, 0.70, 1};
    if (selected_text.alpha < 0.1) selected_text = (GdkRGBA){1, 1, 1, 1};
    cairo_set_source_rgb(cr, 1, 1, 1); cairo_paint(cr);
    xxwidgets_die_edit_view_get(widget, &view);
    count = width > state->die_gutter ? (size_t)((width - state->die_gutter) / state->die_cell_width + 1) : 1;
    cells = calloc(count, sizeof(*cells));
    if (!cells) { g_object_unref(layout); return TRUE; }
    for (size_t row = 0; row < (size_t)(height / state->die_cell_height + 1); ++row) {
        size_t line = view.first_line + row;
        int y = (int)row * state->die_cell_height;
        char number[32];
        int breakpoint, execution;
        if (line >= view.line_count) break;
        xxwidgets_die_edit_line_marker(widget, line, &breakpoint, &execution);
        if (execution || (!view.read_only && line == current)) {
            if (execution) cairo_set_source_rgb(cr, 1, 0.91, 0.64);
            else cairo_set_source_rgb(cr, 1, 1, 0.88);
            cairo_rectangle(cr, state->die_gutter, y, width - state->die_gutter, state->die_cell_height); cairo_fill(cr);
        }
        cairo_set_source_rgb(cr, 0.93, 0.93, 0.93);
        cairo_rectangle(cr, 0, y, state->die_gutter - state->die_cell_width / 2, state->die_cell_height); cairo_fill(cr);
        if (breakpoint) {
            double radius = MIN(state->die_cell_width, state->die_cell_height) * 0.325;
            cairo_set_source_rgb(cr, 0.82, 0.12, 0.12);
            cairo_arc(cr, state->die_cell_width * 0.5, y + state->die_cell_height * 0.5, radius, 0, 6.283185307179586); cairo_fill(cr);
        }
        if (execution) {
            double left = state->die_cell_width + 1, right = state->die_cell_width * 2 - 1;
            double middle = y + state->die_cell_height * 0.5, head = right - state->die_cell_width * 0.35;
            cairo_set_source_rgb(cr, 0.82, 0.43, 0.02);
            cairo_move_to(cr, left, middle - state->die_cell_height * 0.2);
            cairo_line_to(cr, head, middle - state->die_cell_height * 0.2);
            cairo_line_to(cr, head, middle - state->die_cell_height * 0.36);
            cairo_line_to(cr, right, middle);
            cairo_line_to(cr, head, middle + state->die_cell_height * 0.36);
            cairo_line_to(cr, head, middle + state->die_cell_height * 0.2);
            cairo_line_to(cr, left, middle + state->die_cell_height * 0.2);
            cairo_close_path(cr); cairo_fill(cr);
        }
        g_snprintf(number, sizeof(number), "%zu", line + 1);
        pango_layout_set_text(layout, number, -1);
        { int nwidth; pango_layout_get_pixel_size(layout, &nwidth, NULL);
          cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
          cairo_move_to(cr, state->die_gutter - state->die_cell_width - nwidth, y); pango_cairo_show_layout(cr, layout); }
        xxwidgets_die_edit_render_line(widget, line, view.first_column, cells, count);
        for (size_t column = 0; column < count; ++column) {
            int x = state->die_gutter + (int)column * state->die_cell_width;
            char utf8[5];
            if (cells[column].selected) {
                gdk_cairo_set_source_rgba(cr, &selection);
                cairo_rectangle(cr, x, y, state->die_cell_width, state->die_cell_height); cairo_fill(cr);
            }
            if (cells[column].scalar >= 32) {
                xxwidgets_die_edit_encode(cells[column].scalar, utf8);
                pango_layout_set_text(layout, utf8, -1);
                if (cells[column].selected) gdk_cairo_set_source_rgba(cr, &selected_text);
                else if (cells[column].style == XXWIDGETS_DIE_STYLE_TEXT) gdk_cairo_set_source_rgba(cr, &text);
                else gtk_die_rgb(cr, xxwidgets_die_edit_color(cells[column].style));
                cairo_save(cr); cairo_rectangle(cr, x, y, state->die_cell_width, state->die_cell_height); cairo_clip(cr);
                cairo_move_to(cr, x, y); pango_cairo_show_layout(cr, layout); cairo_restore(cr);
            }
            if (cells[column].caret && gtk_widget_has_focus(native)) {
                gdk_cairo_set_source_rgba(cr, &text);
                cairo_rectangle(cr, x, y, 1, state->die_cell_height); cairo_fill(cr);
            }
        }
    }
    free(cells); g_object_unref(layout);
    return TRUE;
}

static void gtk_die_commit(GtkIMContext *context, const gchar *text, gpointer data)
{
    xxwidgets_widget *widget = data;
    (void)context;
    if (xxwidgets_focusable(widget) && !widget->app->syncing) (void)xxwidgets_die_edit_input(widget, text);
}

static gboolean gtk_die_key(GtkWidget *native, GdkEventKey *event, gpointer data)
{
    xxwidgets_widget *widget = data;
    gtk_widget_state *state = widget->platform;
    unsigned int key, modifiers;
    if (widget->app->syncing || !xxwidgets_focusable(widget)) return FALSE;
    if (event->type == GDK_KEY_RELEASE) return gtk_im_context_filter_keypress(state->die_im, event);
    if (gtk_key_input(event, &key, &modifiers)) {
        if (key == XXWIDGETS_KEY_TAB && (modifiers & (XXWIDGETS_MOD_CTRL | XXWIDGETS_MOD_META))) {
            GdkEventKey navigation = *event;
            navigation.state &= ~(GDK_CONTROL_MASK | GDK_SUPER_MASK | GDK_META_MASK);
            return list_key(native, &navigation, data);
        }
        /* Let the input method translate printable keys, including dead-key
         * composition and AltGr. Only commands/navigation bypass the IM. */
        if ((key >= 0x110000 || ((modifiers & (XXWIDGETS_MOD_CTRL | XXWIDGETS_MOD_META)) && !(modifiers & XXWIDGETS_MOD_ALT))) &&
            xxwidgets_die_edit_key(widget, (int)key, modifiers)) {
            gtk_im_context_reset(state->die_im);
            return TRUE;
        }
        if ((modifiers & (XXWIDGETS_MOD_CTRL | XXWIDGETS_MOD_META)) && !(modifiers & XXWIDGETS_MOD_ALT)) return FALSE;
    }
    return gtk_im_context_filter_keypress(state->die_im, event);
}

static void gtk_die_hit(xxwidgets_widget *widget, double x, double y, int extend)
{
    gtk_widget_state *state = widget->platform;
    xxwidgets_die_edit_view view;
    int row = (int)floor(y / state->die_cell_height);
    int column = (int)floor((x - state->die_gutter + state->die_cell_width / 2) / state->die_cell_width);
    xxwidgets_die_edit_view_get(widget, &view);
    if (row < 0) { xxwidgets_die_edit_scroll(widget, -1, 0); row = 0; xxwidgets_die_edit_view_get(widget, &view); }
    else if ((size_t)row >= state->die_rows) { xxwidgets_die_edit_scroll(widget, 1, 0); row = (int)state->die_rows - 1; xxwidgets_die_edit_view_get(widget, &view); }
    if (column < 0) column = 0;
    xxwidgets_die_edit_point(widget, view.first_line + (size_t)row, view.first_column + (size_t)column, extend);
}

static gboolean gtk_die_pointer(GtkWidget *native, GdkEventButton *event, gpointer data)
{
    xxwidgets_widget *widget = data;
    gtk_widget_state *state = widget->platform;
    if (event->button != 1) return FALSE;
    if (event->type == GDK_BUTTON_RELEASE) {
        if (state->die_dragging) { state->die_dragging = 0; gtk_grab_remove(native); }
        return TRUE;
    }
    if (!xxwidgets_focusable(widget) || widget->app->syncing) return FALSE;
    gtk_widget_grab_focus(native);
    state->die_dragging = 1; gtk_grab_add(native);
    gtk_die_hit(widget, event->x, event->y, !!(event->state & GDK_SHIFT_MASK));
    return TRUE;
}

static gboolean gtk_die_motion(GtkWidget *native, GdkEventMotion *event, gpointer data)
{
    gtk_widget_state *state = ((xxwidgets_widget *)data)->platform;
    if (!state->die_dragging) return FALSE;
    if (!(event->state & GDK_BUTTON1_MASK)) { state->die_dragging = 0; gtk_grab_remove(native); return FALSE; }
    gtk_die_hit(data, event->x, event->y, 1);
    return TRUE;
}

static gboolean gtk_die_scroll(GtkWidget *native, GdkEventScroll *event, gpointer data)
{
    xxwidgets_widget *widget = data;
    double dx = 0, dy = 0;
    (void)native;
    if (!xxwidgets_focusable(widget) || widget->app->syncing) return FALSE;
    if (!gdk_event_get_scroll_deltas((GdkEvent *)event, &dx, &dy)) {
        if (event->direction == GDK_SCROLL_UP) dy = -1;
        else if (event->direction == GDK_SCROLL_DOWN) dy = 1;
        else if (event->direction == GDK_SCROLL_LEFT) dx = -1;
        else if (event->direction == GDK_SCROLL_RIGHT) dx = 1;
    }
    if (event->state & GDK_SHIFT_MASK) { dx += dy; dy = 0; }
    xxwidgets_die_edit_scroll(widget, (int)(dy < 0 ? floor(dy * 3) : ceil(dy * 3)), (int)(dx < 0 ? floor(dx * 3) : ceil(dx * 3)));
    gtk_die_sync(widget);
    return TRUE;
}

static gboolean gtk_die_focus(GtkWidget *native, GdkEventFocus *event, gpointer data)
{
    gtk_widget_state *state = ((xxwidgets_widget *)data)->platform;
    if (event->in) { gtk_im_context_set_client_window(state->die_im, gtk_widget_get_window(native)); gtk_im_context_focus_in(state->die_im); }
    else { gtk_im_context_focus_out(state->die_im); gtk_im_context_reset(state->die_im); }
    gtk_widget_queue_draw(native);
    return FALSE;
}

static void gtk_die_allocate(GtkWidget *native, GtkAllocation *allocation, gpointer data)
{
    (void)native; (void)allocation;
    gtk_die_sync(data);
}

static gboolean gtk_die_grab_broken(GtkWidget *native, GdkEventGrabBroken *event, gpointer data)
{
    gtk_widget_state *state = ((xxwidgets_widget *)data)->platform;
    (void)event;
    if (state->die_dragging) { state->die_dragging = 0; gtk_grab_remove(native); }
    return FALSE;
}

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
            /* A GtkLayout, unlike GtkFixed, requests no size from its children
             * and clips them to the client area: overflowing controls can neither
             * grow the window nor paint outside it, and the window can shrink.
             * Its minimum comes from window_update_minimum alone. */
            state->content = gtk_layout_new(NULL, NULL);
            /* Cleared if another client destroys the window, which frees the layout. */
            g_object_add_weak_pointer(G_OBJECT(state->content), (gpointer *)&state->content);
            gtk_style_context_add_class(gtk_widget_get_style_context(state->content), GRID_CLASS);
            state->content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
            gtk_container_add(GTK_CONTAINER(native), state->content_box);
            gtk_box_pack_end(GTK_BOX(state->content_box), state->content, TRUE, TRUE, 0);
            gtk_widget_show(state->content_box);
            gtk_widget_show(state->content);
            g_signal_connect(native, "delete-event", G_CALLBACK(window_close), widget);
            g_signal_connect(native, "key-press-event", G_CALLBACK(window_shortcut), widget);
            g_signal_connect(native, "configure-event", G_CALLBACK(window_configure), widget);
            state->fit_position = 1;
            state->min_width = state->min_height = -1;
            break;
        case XXWIDGETS_LABEL:
            native = gtk_label_new("");
            gtk_label_set_xalign(GTK_LABEL(native), 0.0f);
            gtk_label_set_ellipsize(GTK_LABEL(native), PANGO_ELLIPSIZE_END);
            gtk_widget_set_has_tooltip(native, TRUE);
            g_signal_connect(native, "query-tooltip", G_CALLBACK(ellipsis_tooltip), NULL);
            break;
        case XXWIDGETS_BUTTON:
            native = gtk_button_new_with_label(widget->text);
            g_signal_connect(native, "clicked", G_CALLBACK(button_clicked), widget);
            break;
        case XXWIDGETS_DIE_SIGNATURE_EDIT: {
            GtkWidget *vertical, *horizontal;
            native = gtk_drawing_area_new();
            state->root = gtk_grid_new();
            g_object_ref_sink(state->root);
            state->die_vertical = GTK_ADJUSTMENT(gtk_adjustment_new(0, 0, 1, 1, 1, 1));
            state->die_horizontal = GTK_ADJUSTMENT(gtk_adjustment_new(0, 0, 1, 1, 1, 1));
            vertical = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, state->die_vertical);
            horizontal = gtk_scrollbar_new(GTK_ORIENTATION_HORIZONTAL, state->die_horizontal);
            gtk_widget_set_hexpand(native, TRUE); gtk_widget_set_vexpand(native, TRUE);
            gtk_grid_attach(GTK_GRID(state->root), native, 0, 0, 1, 1);
            gtk_grid_attach(GTK_GRID(state->root), vertical, 1, 0, 1, 1);
            gtk_grid_attach(GTK_GRID(state->root), horizontal, 0, 1, 1, 1);
            gtk_widget_show(native); gtk_widget_show(vertical); gtk_widget_show(horizontal);
            gtk_widget_set_can_focus(native, TRUE);
            gtk_widget_add_events(native, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK |
                GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK | GDK_FOCUS_CHANGE_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
            state->die_im = gtk_im_multicontext_new();
            g_signal_connect(state->die_im, "commit", G_CALLBACK(gtk_die_commit), widget);
            g_signal_connect(state->die_vertical, "value-changed", G_CALLBACK(gtk_die_adjusted), widget);
            g_signal_connect(state->die_horizontal, "value-changed", G_CALLBACK(gtk_die_adjusted), widget);
            g_signal_connect(native, "draw", G_CALLBACK(gtk_die_draw), widget);
            g_signal_connect(native, "key-press-event", G_CALLBACK(gtk_die_key), widget);
            g_signal_connect(native, "key-release-event", G_CALLBACK(gtk_die_key), widget);
            g_signal_connect(native, "button-press-event", G_CALLBACK(gtk_die_pointer), widget);
            g_signal_connect(native, "button-release-event", G_CALLBACK(gtk_die_pointer), widget);
            g_signal_connect(native, "motion-notify-event", G_CALLBACK(gtk_die_motion), widget);
            g_signal_connect(native, "scroll-event", G_CALLBACK(gtk_die_scroll), widget);
            g_signal_connect(native, "focus-in-event", G_CALLBACK(gtk_die_focus), widget);
            g_signal_connect(native, "focus-out-event", G_CALLBACK(gtk_die_focus), widget);
            g_signal_connect(native, "size-allocate", G_CALLBACK(gtk_die_allocate), widget);
            g_signal_connect(native, "grab-broken-event", G_CALLBACK(gtk_die_grab_broken), widget);
            break;
        }
        case XXWIDGETS_EDIT:
            native = gtk_entry_new();
            g_signal_connect(native, "changed", G_CALLBACK(edit_changed), widget);
            g_signal_connect(native, "activate", G_CALLBACK(edit_activated), widget);
            break;
        case XXWIDGETS_CHECKBOX:
            native = gtk_check_button_new_with_label(widget->text);
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
            native = gtk_list_box_new();
            gtk_list_box_set_selection_mode(GTK_LIST_BOX(native), GTK_SELECTION_SINGLE);
            state->root = gtk_scrolled_window_new(NULL, NULL);
            g_object_ref_sink(state->root);
            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(state->root), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
            gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(state->root), GTK_SHADOW_IN);
            /* The rows take focus. A focusable scrolled window would take it itself
             * when the list is empty: a Tab stop with nothing drawn for it. */
            gtk_widget_set_can_focus(state->root, FALSE);
            gtk_container_add(GTK_CONTAINER(state->root), native);
            gtk_widget_show(native);
            g_signal_connect(native, "row-selected", G_CALLBACK(list_selected), widget);
            g_signal_connect(native, "key-press-event", G_CALLBACK(list_key), widget);
            if (widget->kind == XXWIDGETS_LISTBOX) {
                GtkAdjustment *adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(state->root));
                state->follow_tail = 1;
                g_signal_connect(adjustment, "value-changed", G_CALLBACK(list_scrolled), widget);
                g_signal_connect(adjustment, "changed", G_CALLBACK(list_resized), widget);
            }
            /* Keeps the selected row in view, e.g. the newest log line. */
            g_signal_connect(native, "size-allocate", G_CALLBACK(hex_allocated), widget);
            break;
        case XXWIDGETS_CANVAS:
        case XXWIDGETS_SPLITTER:
            native=gtk_drawing_area_new();gtk_widget_set_can_focus(native,TRUE);gtk_widget_add_events(native,GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_POINTER_MOTION_MASK|GDK_KEY_PRESS_MASK);
            g_signal_connect(native,"draw",G_CALLBACK(gtk_canvas_paint),widget);g_signal_connect(native,"button-press-event",G_CALLBACK(gtk_canvas_pointer),widget);g_signal_connect(native,"motion-notify-event",G_CALLBACK(gtk_canvas_motion),widget);g_signal_connect(native,"key-press-event",G_CALLBACK(gtk_canvas_key),widget);break;
        case XXWIDGETS_ENTROPY_PLOT:
            native = gtk_drawing_area_new();
            gtk_widget_add_events(native, GDK_BUTTON_PRESS_MASK);
            g_signal_connect(native, "draw", G_CALLBACK(gtk_plot_paint), widget);
            g_signal_connect(native, "button-press-event", G_CALLBACK(gtk_plot_click), widget);
            break;
        case XXWIDGETS_PROGRESS: native = gtk_progress_bar_new(); break;
        case XXWIDGETS_TREEVIEW: {
            GtkTreeStore *store = gtk_tree_store_new(2, G_TYPE_STRING, G_TYPE_INT);
            GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
            GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("", renderer, "text", 0, NULL);
            gtk_tree_view_column_set_cell_data_func(column, renderer, treeview_cell_color, widget, NULL);
            native = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
            g_object_unref(store);
            gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(native), FALSE);
            gtk_tree_view_append_column(GTK_TREE_VIEW(native), column);
            gtk_tree_view_column_set_expand(column, TRUE);
            g_object_set(renderer, "single-paragraph-mode", TRUE, NULL);
            for (int mark = 0; mark < XXWIDGETS_TREE_MARKS_MAX; ++mark) {
                GtkCellRenderer *label = gtk_cell_renderer_text_new();
                GtkTreeViewColumn *marks = gtk_tree_view_column_new();
                g_object_set(label, "xalign", 0.5, "xpad", 6, NULL);
                gtk_tree_view_column_pack_start(marks, label, TRUE);
                gtk_tree_view_column_set_cell_data_func(marks, label, treeview_mark_cell, widget, NULL);
                g_object_set_data(G_OBJECT(marks), "xxwidgets-mark", GINT_TO_POINTER(mark + 1));
                gtk_tree_view_column_set_visible(marks, FALSE);
                gtk_tree_view_append_column(GTK_TREE_VIEW(native), marks);
            }
            g_signal_connect(native, "button-press-event", G_CALLBACK(treeview_mark_press), widget);
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
        case XXWIDGETS_SCANRESULTS:
        case XXWIDGET_RESOURCESVIEWER:
        case XXWIDGET_IMPORTSVIEWER:
        case XXWIDGET_EXPORTSVIEWER:
        case XXWIDGET_SYMBOLSVIEWER:
        case XXWIDGETS_DEBUGGER_DISASM:
        case XXWIDGETS_DEBUGGER_REGISTERS:
        case XXWIDGETS_MEMORY_MAP:
        case XXWIDGETS_DEBUGGER_STACK:
        case XXWIDGET_SYSTEMPROCESSESVIEWER:
        case XXWIDGETS_ENTROPY_REGIONS:
        case XXWIDGETS_ENTROPY_BYTES:
        case XXWIDGETS_HEXVIEW:
        case XXWIDGETS_DATA_STRUCT_TABLE: {
            static const int scan_widths[] = {100, 220, 100, 260};
            static const int data_widths[] = {160, 110, 130, 120, 80, 110, 180, 180, 260};
            GType types[9];
            size_t column, columns = xxwidgets_tabular_column_count(widget);
            GtkListStore *store;
            for (column = 0; column < columns; ++column) types[column] = G_TYPE_STRING;
            store = gtk_list_store_newv((gint)columns, types);
            native = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
            g_object_unref(store);
            gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(native)), GTK_SELECTION_SINGLE);
            state->root = gtk_scrolled_window_new(NULL, NULL);
            g_object_ref_sink(state->root);
            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(state->root), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
            gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(state->root), GTK_SHADOW_IN);
            gtk_container_add(GTK_CONTAINER(state->root), native);
            for (column = 0; column < columns; ++column) {
                GtkCellRenderer *renderer = widget->kind == XXWIDGETS_HEXVIEW && column == 2
                                                ? g_object_new(gtk_hex_symbol_renderer_get_type(), NULL)
                                                : gtk_cell_renderer_text_new();
                GtkTreeViewColumn *view_column =
                    gtk_tree_view_column_new_with_attributes(xxwidgets_tabular_column_title(widget, column), renderer, "text", (gint)column, NULL);
                gtk_tree_view_column_set_resizable(view_column, TRUE);
                gtk_tree_view_column_set_sizing(view_column, GTK_TREE_VIEW_COLUMN_FIXED);
                gtk_tree_view_column_set_fixed_width(view_column,
                                                     (xxwidgets_entropy_table(widget) || widget->kind == XXWIDGETS_HEXVIEW || widget->kind == XXWIDGET_SYSTEMPROCESSESVIEWER || xxwidgets_binaryviewer_kind(widget) || xxwidgets_debugger_table_kind(widget)) ? (int)xxwidgets_tabular_column_width(widget, column) * 8 : widget->kind == XXWIDGETS_SCANRESULTS ? scan_widths[column] : data_widths[column]);
                gtk_tree_view_column_set_min_width(view_column, xxwidgets_debugger_table_kind(widget) && column == 0 ? 16 : 60);
                /* GTK otherwise gives spare space to the last (PID) column,
                 * leaving the process name at its narrow initial width. */
                if (widget->kind == XXWIDGET_SYSTEMPROCESSESVIEWER)
                    gtk_tree_view_column_set_expand(view_column, column == 0);
                g_object_set(renderer, "single-paragraph-mode", TRUE, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
                gtk_tree_view_append_column(GTK_TREE_VIEW(native), view_column);
                if (xxwidgets_debugger_table_kind(widget)) g_object_set(renderer, "family", "monospace", NULL);
                if (widget->kind == XXWIDGETS_HEXVIEW) {
                    GtkWidget *button = gtk_tree_view_column_get_button(view_column);
                    g_object_set(renderer, "family", "monospace", "ellipsize", PANGO_ELLIPSIZE_NONE, NULL);
                    gtk_tree_view_column_set_cell_data_func(view_column, renderer, gtk_hex_cell, widget, NULL);
                    gtk_tree_view_column_set_clickable(view_column, TRUE);
                    g_object_set_data(G_OBJECT(view_column), "hex-column", GUINT_TO_POINTER((unsigned)column));
                    g_object_set_data(G_OBJECT(button), "hex-column", GUINT_TO_POINTER((unsigned)column));
                    g_signal_connect(view_column, "clicked", G_CALLBACK(gtk_hex_header), widget);
                    g_signal_connect(view_column, "notify::width", G_CALLBACK(gtk_hex_resized), widget);
                    g_signal_connect(button, "button-press-event", G_CALLBACK(gtk_hex_header_right), widget);
                }
            }
            g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(native)), "changed", G_CALLBACK(scanresults_selected), widget);
            if (widget->kind == XXWIDGET_SYSTEMPROCESSESVIEWER) {
                gtk_widget_add_events(native, GDK_BUTTON_PRESS_MASK);
                g_signal_connect(native, "button-press-event", G_CALLBACK(systemprocessesviewer_button_press), widget);
                g_signal_connect(native, "popup-menu", G_CALLBACK(browser_popup_menu), widget);
                g_signal_connect(native, "key-press-event", G_CALLBACK(systemprocessesviewer_key), widget);
                g_signal_connect(native, "row-activated", G_CALLBACK(table_activated), widget);
            }
            if (xxwidgets_binaryviewer_kind(widget) || xxwidgets_debugger_table_kind(widget))
                g_signal_connect(native, "row-activated", G_CALLBACK(table_activated), widget);
            if (widget->kind == XXWIDGETS_HEXVIEW) {
                GtkWidget *bar;
                GtkPolicyType vertical_policy = GTK_POLICY_NEVER;
                state->hex_scroll = state->root;
                state->root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
                g_object_ref_sink(state->root);
                /* The content keeps its horizontal scrollbar. Only the vertical
                 * scrollbar moves outside, with the overview on its left. */
#if GTK_CHECK_VERSION(3, 16, 0)
                vertical_policy = GTK_POLICY_EXTERNAL;
#endif
                gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(state->hex_scroll), GTK_POLICY_AUTOMATIC, vertical_policy);
                state->hex_overview = gtk_drawing_area_new();
                gtk_widget_set_size_request(state->hex_overview, 12, -1);
                gtk_widget_set_tooltip_text(state->hex_overview, "File overview: click or drag to go to a file offset");
                atk_object_set_name(gtk_widget_get_accessible(state->hex_overview), "File overview");
                gtk_widget_add_events(state->hex_overview, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
                g_signal_connect(state->hex_overview, "draw", G_CALLBACK(gtk_hex_overview_draw), widget);
                g_signal_connect(state->hex_overview, "button-press-event", G_CALLBACK(gtk_hex_overview_pointer), widget);
                g_signal_connect(state->hex_overview, "button-release-event", G_CALLBACK(gtk_hex_overview_pointer), widget);
                g_signal_connect(state->hex_overview, "motion-notify-event", G_CALLBACK(gtk_hex_overview_motion), widget);
                g_signal_connect(state->hex_overview, "grab-broken-event", G_CALLBACK(gtk_hex_overview_grab_broken), widget);
                bar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(state->hex_scroll)));
                gtk_box_pack_start(GTK_BOX(state->root), state->hex_scroll, TRUE, TRUE, 0);
                gtk_box_pack_start(GTK_BOX(state->root), state->hex_overview, FALSE, FALSE, 0);
                gtk_box_pack_start(GTK_BOX(state->root), bar, FALSE, FALSE, 0);
                /* The box now owns the scroller; its original root reference
                 * must not survive the box's destruction. */
                g_object_unref(state->hex_scroll);
                gtk_widget_show(state->hex_scroll);
                gtk_widget_show(state->hex_overview);
                gtk_widget_show(bar);
                g_signal_connect(native, "key-press-event", G_CALLBACK(list_key), widget);
                gtk_widget_add_events(native, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
                g_signal_connect(native, "button-press-event", G_CALLBACK(gtk_hex_pointer), widget);
                g_signal_connect(native, "button-release-event", G_CALLBACK(gtk_hex_pointer), widget);
                g_signal_connect(native, "motion-notify-event", G_CALLBACK(gtk_hex_motion), widget);
            }
            gtk_widget_show(native);
            break;
        }
        case XXWIDGETS_ARCHIVEBROWSER: {
            static const char *titles[] = {"Name", "Size", "Packed Size", "Modified", "Attributes"};
            GtkListStore *store = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
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
            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(state->browser_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
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
    switch (widget->kind) {
        case XXWIDGETS_BUTTON:
        case XXWIDGETS_EDIT:
        case XXWIDGETS_CHECKBOX:
        case XXWIDGETS_COMBOBOX:
        case XXWIDGETS_CHECKCOMBOBOX: cell_control(native); break;
        default: break;
    }
    g_object_ref_sink(native);
    widget->native = native;
    if (widget->kind == XXWIDGETS_WINDOW || widget->kind == XXWIDGETS_EDIT) {
        /* An entry normally consumes text offered alongside a URI. Prefer the
         * file drop there, while leaving ordinary text dragging to GtkEntry. */
        g_signal_connect(native, "drag-motion", G_CALLBACK(file_drag_motion), widget);
        g_signal_connect(native, "drag-drop", G_CALLBACK(file_drag_drop), widget);
        g_signal_connect(native, "drag-data-received", G_CALLBACK(file_drag_received), widget);
    }
    if (!state->root) state->root = native;
    if (widget->parent) {
        gtk_widget_state *parent_state = widget->parent->platform;
        gtk_layout_put(GTK_LAYOUT(parent_state->content), state->root, 0, 0);
    }
    status = gtk_sync_backend(widget);
    if (status == XXWIDGETS_OK) fonts_attach(widget);
    else gtk_destroy_backend(widget);
    return status;
}

static void gtk_destroy_backend(xxwidgets_widget *widget)
{
    GtkWidget *native = widget->native;
    gtk_widget_state *state = widget->platform;
    if (!state) return;
    if (native) g_signal_handlers_disconnect_by_data(native, widget);
    if (state->die_im) {
        g_signal_handlers_disconnect_by_data(state->die_im, widget);
        gtk_im_context_set_client_window(state->die_im, NULL);
        g_object_unref(state->die_im);
    }
    if (state->die_dragging && native) gtk_grab_remove(native);
    if (state->die_vertical) g_signal_handlers_disconnect_by_data(state->die_vertical, widget);
    if (state->die_horizontal) g_signal_handlers_disconnect_by_data(state->die_horizontal, widget);
    if (state->hex_overview) {
        if (state->hex_overview_dragging) gtk_grab_remove(state->hex_overview);
        g_signal_handlers_disconnect_by_data(state->hex_overview, widget);
    }
    if ((xxwidgets_tabular_kind(widget) || widget->kind == XXWIDGETS_TREEVIEW) && native)
        g_signal_handlers_disconnect_by_data(gtk_tree_view_get_selection(GTK_TREE_VIEW(native)), widget);
    if (widget->kind == XXWIDGETS_ARCHIVEBROWSER && native) {
        g_signal_handlers_disconnect_by_data(gtk_tree_view_get_selection(GTK_TREE_VIEW(native)), widget);
        g_signal_handlers_disconnect_by_data(state->browser_up, widget);
    }
    if (widget->kind == XXWIDGETS_LISTBOX && state->root && GTK_IS_SCROLLED_WINDOW(state->root))
        g_signal_handlers_disconnect_by_data(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(state->root)), widget);
    if (state->content) g_object_remove_weak_pointer(G_OBJECT(state->content), (gpointer *)&state->content);
    if (state->root) {
        gtk_widget_destroy(state->root);
        if (state->root != native) g_object_unref(state->root);
    }
    if (native) g_object_unref(native);
    if (state->preview_font) pango_font_description_free(state->preview_font);
    if (state->preview_css) g_object_unref(state->preview_css);
    if (state->hex_font) pango_font_description_free(state->hex_font);
    if (state->hex_font_css) g_object_unref(state->hex_font_css);
    free(state->tree_iters);
    free(state);
    widget->platform = NULL;
    widget->native = NULL;
}

/* Places a control, or sizes a window, from its cell rect. */
static void apply_geometry(xxwidgets_widget *widget, int x, int y, int width, int height)
{
    gtk_widget_state *state = widget->platform;
    if (widget->kind == XXWIDGETS_WINDOW) {
        int current_width = 0, current_height = 0;
        if (height > INT_MAX - state->menu_height) return;
        height += state->menu_height;
        window_fit_workarea(widget, &x, &y, &width, &height);
        gtk_window_get_size(GTK_WINDOW(widget->native), &current_width, &current_height);
        if (width != current_width || height != current_height) state->fit_position = 1;
        gtk_window_resize(GTK_WINDOW(widget->native), width, height);
        /* Only a position the program changed: a new size keeps the place the
         * user or the window manager gave the window (configure-event never
         * writes the position back into rect). */
        if (!state->has_rect || widget->rect.x != state->applied_rect.x || widget->rect.y != state->applied_rect.y) gtk_window_move(GTK_WINDOW(widget->native), x, y);
    } else {
        gtk_widget_state *parent_state = widget->parent->platform;
        int one_row = widget->kind == XXWIDGETS_BUTTON || widget->kind == XXWIDGETS_EDIT || widget->kind == XXWIDGETS_CHECKBOX || widget->kind == XXWIDGETS_COMBOBOX ||
                      widget->kind == XXWIDGETS_CHECKCOMBOBOX;
        if (one_row && widget->rect.height > 1) {
            /* A button given two rows stays button-sized, centred in them. */
            int minimum = 0, shown = gtk_widget_get_visible(state->root);
            /* A newly created control is still hidden, and GTK measures a
             * hidden widget as 0 x 0; sync shows it right after this. */
            if (!shown) gtk_widget_show(state->root);
            gtk_widget_set_size_request(state->root, width, -1);
            gtk_widget_get_preferred_height(state->root, &minimum, NULL);
            if (!shown) gtk_widget_hide(state->root);
            if (minimum > 0 && minimum < height) {
                y += (height - minimum) / 2;
                height = minimum;
            }
        }
        gtk_layout_move(GTK_LAYOUT(parent_state->content), state->root, x, y);
        gtk_widget_set_size_request(state->root, width, height);
        if (!widget->parent->has_minimum) window_update_minimum(widget->parent);
    }
    state->applied_rect = widget->rect;
    state->has_rect = 1;
}

/* A cell is as wide as the font's average character, but a button also needs
 * its theme's padding and border: with a wide font such as Cantarell,
 * "Browse..." no longer fits the ten cells a layout gives it. Before the first
 * window is drawn, widen the cells just enough for every button and check box
 * to show its whole label (at most 1.5x the font's cell) and re-place every
 * control. Windows created later keep that grid; a label that still cannot
 * fit is ellipsized, with the whole text as its tooltip. */
static gboolean grid_fit(gpointer data)
{
    xxwidgets_app *app = data;
    gtk_app_state *state = app->platform;
    xxwidgets_widget *widget;
    int needed = state->cell_width, cap = state->base_cell_width * 3 / 2;
    state->fit_source = 0;
    state->fitted = 1;
    for (widget = app->widgets; widget; widget = widget->next) {
        gtk_widget_state *control = widget->platform;
        int natural = 0, cells = widget->rect.width, per_cell;
        if (!widget->parent || !control || !widget->visible || cells <= 0 || (widget->kind != XXWIDGETS_BUTTON && widget->kind != XXWIDGETS_CHECKBOX)) continue;
        gtk_widget_get_preferred_width(control->root, NULL, &natural);
        per_cell = (natural + cells - 1) / cells;
        if (per_cell > needed) needed = per_cell;
    }
    if (needed > cap) needed = cap;
    if (needed > state->cell_width) {
        state->cell_width = needed;
        for (widget = app->widgets; widget; widget = widget->next) {
            int x, y, width, height;
            if (widget->platform && pixel_rect(widget, &x, &y, &width, &height)) apply_geometry(widget, x, y, width, height);
        }
        for (widget = app->widgets; widget; widget = widget->next)
            if (widget->kind == XXWIDGETS_WINDOW && widget->platform) window_update_minimum(widget);
    }
    return G_SOURCE_REMOVE;
}

static xxwidgets_status gtk_sync_backend(xxwidgets_widget *widget)
{
    GtkWidget *native = widget->native;
    gtk_widget_state *state = widget->platform;
    gtk_app_state *app_state = widget->app->platform;
    int x, y, width, height;
    size_t i;
    if (!g_utf8_validate(widget->text, -1, NULL) || !pixel_rect(widget, &x, &y, &width, &height)) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < widget->item_count; ++i)
        if (!g_utf8_validate(widget->items[i], -1, NULL)) return XXWIDGETS_INVALID_ARGUMENT;
    if (!state->has_rect || !rect_equal(state->applied_rect, widget->rect)) apply_geometry(widget, x, y, width, height);
    switch (widget->kind) {
        case XXWIDGETS_DIE_SIGNATURE_EDIT: gtk_die_sync(widget); break;
        case XXWIDGETS_WINDOW:
            if (g_strcmp0(gtk_window_get_title(GTK_WINDOW(native)), widget->text)) gtk_window_set_title(GTK_WINDOW(native), widget->text);
            window_update_minimum(widget);
            /* Runs before GTK's resize and redraw idles, once the application
             * has created and placed the window's controls. */
            if (!app_state->fitted && !app_state->fit_source && widget->visible)
                app_state->fit_source = g_idle_add_full(G_PRIORITY_HIGH_IDLE, grid_fit, widget->app, NULL);
            break;
        case XXWIDGETS_LABEL: {
            PangoEllipsizeMode mode = widget->keep_end ? PANGO_ELLIPSIZE_MIDDLE : PANGO_ELLIPSIZE_END;
            if (gtk_label_get_ellipsize(GTK_LABEL(native)) != mode) gtk_label_set_ellipsize(GTK_LABEL(native), mode);
            if (strcmp(gtk_label_get_text(GTK_LABEL(native)), widget->text)) gtk_label_set_text(GTK_LABEL(native), widget->text);
            break;
        }
        case XXWIDGETS_COMBOBOX:
            if (!state->combo_initialized || state->combo_revision != widget->combo_revision) {
                size_t index;
                gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(native));
                for (index = 0; index < widget->item_count; ++index) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(native), widget->items[index]);
                state->combo_initialized = 1;
                state->combo_revision = widget->combo_revision;
            }
            gtk_combo_box_set_active(GTK_COMBO_BOX(native), widget->value);
            break;
        case XXWIDGETS_CHECKCOMBOBOX: {
            GList *rows, *row;
            size_t index = 0;
            char *caption = g_strconcat(xxwidgets_combobox_caption(widget), " \xe2\x96\xbe", NULL);
            gtk_button_set_label(GTK_BUTTON(native), caption);
            g_free(caption);
            ellipsize_control_label(native);
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
                state->combo_initialized = 1;
                state->combo_revision = widget->combo_revision;
            }
            rows = gtk_container_get_children(GTK_CONTAINER(state->combo_box));
            index = 0;
            for (row = rows; row; row = row->next, ++index) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(row->data), xxwidgets_checkcombobox_checked(widget, index));
            g_list_free(rows);
            gtk_widget_set_size_request(state->combo_scroll, 24 * ((gtk_app_state *)widget->app->platform)->cell_width,
                                        (int)(widget->item_count < 10 ? widget->item_count : 10) * ((gtk_app_state *)widget->app->platform)->cell_height + 8);
            if (!widget->visible || !widget->enabled || !widget->parent->enabled || !widget->item_count)
                gtk_widget_hide(GTK_WIDGET(gtk_menu_button_get_popover(GTK_MENU_BUTTON(native))));
            break;
        }
        case XXWIDGETS_BUTTON:
            if (g_strcmp0(gtk_button_get_label(GTK_BUTTON(native)), widget->text)) {
                gtk_button_set_label(GTK_BUTTON(native), widget->text);
                ellipsize_control_label(native);
            }
            break;
        case XXWIDGETS_EDIT:
            /* Setting identical text still moves the caret in many native controls. */
            if (strcmp(gtk_entry_get_text(GTK_ENTRY(native)), widget->text)) {
                /* Show a long path's end, where the file name is. In the focused
                 * field the caret keeps its place around a small change, as when
                 * a '~' is expanded with the caret anywhere in the path. */
                int position = -1;
                if (gtk_widget_is_focus(native)) {
                    const char *old = gtk_entry_get_text(GTK_ENTRY(native));
                    const char *at = g_utf8_offset_to_pointer(old, gtk_editable_get_position(GTK_EDITABLE(native)));
                    size_t caret = xxwidgets_edit_caret(old, (size_t)(at - old), widget->text);
                    if (caret != SIZE_MAX) position = (int)g_utf8_pointer_to_offset(widget->text, widget->text + caret);
                }
                gtk_entry_set_text(GTK_ENTRY(native), widget->text);
                gtk_editable_set_position(GTK_EDITABLE(native), position);
            }
            break;
        case XXWIDGETS_CHECKBOX:
            if (g_strcmp0(gtk_button_get_label(GTK_BUTTON(native)), widget->text)) {
                gtk_button_set_label(GTK_BUTTON(native), widget->text);
                ellipsize_control_label(native);
            }
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(native)) != (widget->value != 0))
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(native), widget->value != 0);
            break;
        case XXWIDGETS_LISTBOX:
        case XXWIDGETS_ARCHIVEVIEW: {
            int rebuild_rows = state->rendered_items > widget->item_count ||
                (xxwidgets_formatted_rows(widget) && state->rendered_hex_revision != xxwidgets_row_revision(widget));
            if (rebuild_rows) {
                GList *rows = gtk_container_get_children(GTK_CONTAINER(native));
                GList *row;
                for (row = rows; row; row = row->next) gtk_widget_destroy(GTK_WIDGET(row->data));
                g_list_free(rows);
                state->rendered_items = 0;
                state->scroll_selection = widget->value >= 0;
            }
            while (state->rendered_items < widget->item_count) {
                GtkWidget *label = gtk_label_new(widget->items[state->rendered_items]);
                gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
                if (xxwidgets_formatted_rows(widget)) {
                    row_font_attributes(widget, label);
                    gtk_label_set_single_line_mode(GTK_LABEL(label), TRUE);
                } else {
                    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
                    gtk_widget_set_has_tooltip(label, TRUE);
                    g_signal_connect(label, "query-tooltip", G_CALLBACK(ellipsis_tooltip), NULL);
                }
                gtk_widget_set_margin_start(label, 4);
                gtk_widget_set_margin_end(label, 4);
                gtk_container_add(GTK_CONTAINER(native), label);
                gtk_widget_show_all(label);
                /* The GtkListBox wraps the child in a row, which must also be shown. */
                gtk_widget_show(gtk_widget_get_parent(label));
                ++state->rendered_items;
            }
            if (rebuild_rows && xxwidgets_formatted_rows(widget)) {
                state->rendered_hex_revision = xxwidgets_row_revision(widget);
            }
            {
                GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(native));
                int selected = row ? gtk_list_box_row_get_index(row) : -1;
                if (selected != widget->value) {
                    gtk_list_box_select_row(GTK_LIST_BOX(native), widget->value < 0 ? NULL : gtk_list_box_get_row_at_index(GTK_LIST_BOX(native), widget->value));
                    state->scroll_selection = widget->value >= 0;
                    gtk_widget_queue_resize(native);
                }
            }
            atk_object_set_name(gtk_widget_get_accessible(native), widget->text);
            /* An empty list is no Tab stop either. */
            gtk_widget_set_can_focus(native, widget->item_count > 0);
            break;
        }
        case XXWIDGETS_CANVAS:
        case XXWIDGETS_SPLITTER:
        case XXWIDGETS_ENTROPY_PLOT: gtk_widget_queue_draw(native); break;
        case XXWIDGETS_PROGRESS: {
            /* An unchanged fraction still notifies, and each notify reaches
             * assistive technology as a value change: the progress dialog
             * re-syncs its bars on every pass of its loop. */
            GtkProgressBar *bar = GTK_PROGRESS_BAR(native);
            double fraction = CLAMP(widget->value / 100.0, 0.0, 1.0);
            gboolean show = widget->text[0] != '\0';
            if (gtk_progress_bar_get_fraction(bar) != fraction) gtk_progress_bar_set_fraction(bar, fraction);
            if (g_strcmp0(gtk_progress_bar_get_text(bar), widget->text)) gtk_progress_bar_set_text(bar, widget->text);
            if (gtk_progress_bar_get_show_text(bar) != show) gtk_progress_bar_set_show_text(bar, show);
            break;
        }
        case XXWIDGETS_ARCHIVEBROWSER: browser_sync(widget); break;
        case XXWIDGETS_SCANRESULTS:
        case XXWIDGET_RESOURCESVIEWER:
        case XXWIDGET_IMPORTSVIEWER:
        case XXWIDGET_EXPORTSVIEWER:
        case XXWIDGET_SYMBOLSVIEWER:
        case XXWIDGETS_DEBUGGER_DISASM:
        case XXWIDGETS_DEBUGGER_REGISTERS:
        case XXWIDGETS_MEMORY_MAP:
        case XXWIDGETS_DEBUGGER_STACK:
        case XXWIDGET_SYSTEMPROCESSESVIEWER:
        case XXWIDGETS_ENTROPY_REGIONS:
        case XXWIDGETS_ENTROPY_BYTES:
        case XXWIDGETS_HEXVIEW:
        case XXWIDGETS_DATA_STRUCT_TABLE: scanresults_sync(widget); break;
        case XXWIDGETS_TREEVIEW: {
            xxwidgets_status status = treeview_sync(widget);
            if (status != XXWIDGETS_OK) return status;
            break;
        }
    }
    if (state->die_dragging && (!widget->enabled || !widget->visible)) {
        state->die_dragging = 0; gtk_grab_remove(native);
    }
    if (state->hex_overview_dragging && (!widget->enabled || !widget->visible)) {
        state->hex_overview_dragging = 0;
        gtk_grab_remove(state->hex_overview);
    }
    gtk_widget_set_sensitive(state->root, widget->enabled != 0);
    if (widget->kind == XXWIDGETS_WINDOW && (!widget->enabled || !widget->visible)) {
        xxwidgets_widget *child;
        for (child = widget->app->widgets; child; child = child->next)
            if (child->parent == widget && child->kind == XXWIDGETS_CHECKCOMBOBOX)
                gtk_widget_hide(GTK_WIDGET(gtk_menu_button_get_popover(GTK_MENU_BUTTON(child->native))));
    }
    if (gtk_widget_get_visible(state->root) != (widget->visible != 0)) {
        gtk_widget_set_visible(state->root, widget->visible != 0);
        /* A window without an explicit minimum is held to its visible controls. */
        if (widget->parent && !widget->parent->has_minimum) window_update_minimum(widget->parent);
    }
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
        case XXWIDGETS_DIE_SIGNATURE_EDIT: return XXWIDGETS_OK; /* Shared source is authoritative. */
        case XXWIDGETS_EDIT: text = gtk_entry_get_text(GTK_ENTRY(native)); break;
        case XXWIDGETS_PROGRESS: text = gtk_progress_bar_get_text(GTK_PROGRESS_BAR(native)); break;
        default: return XXWIDGETS_OK;
    }
    return xxwidgets_store_text(widget, text ? text : "");
}

static xxwidgets_status gtk_read_value_backend(xxwidgets_widget *widget)
{
    GtkWidget *native = widget->native;
    if (widget->kind == XXWIDGETS_COMBOBOX) widget->value = gtk_combo_box_get_active(GTK_COMBO_BOX(native));
    else if (widget->kind == XXWIDGETS_CHECKBOX) widget->value = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(native)) ? 1 : 0;
    else if (widget->kind == XXWIDGETS_ARCHIVEBROWSER) {
        browser_selection_read(widget, 0);
    } else if (xxwidgets_tabular_kind(widget)) {
        scanresults_selection_read(widget);
    } else if (widget->kind == XXWIDGETS_TREEVIEW) {
        treeview_selection_read(widget);
    } else if (xxwidgets_list_kind(widget)) {
        GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(native));
        widget->value = row ? gtk_list_box_row_get_index(row) : -1;
    } else if (widget->kind == XXWIDGETS_PROGRESS) widget->value = (int)(gtk_progress_bar_get_fraction(GTK_PROGRESS_BAR(native)) * 100.0 + 0.5);
    return XXWIDGETS_OK;
}

static xxwidgets_status gtk_focus_backend(xxwidgets_widget *widget)
{
    GtkWidget *native = GTK_WIDGET(widget->native);
    if (widget->kind == XXWIDGETS_WINDOW) gtk_window_present(GTK_WINDOW(native));
    else if (GTK_IS_ENTRY(native)) {
        /* Selecting all would let the next keystroke replace the text. */
        gtk_entry_grab_focus_without_selecting(GTK_ENTRY(native));
    } else if (GTK_IS_LIST_BOX(native)) {
        /* A list box itself cannot hold focus; its selected (or first) row can. */
        GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(native));
        if (!row) row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(native), 0);
        if (row) gtk_widget_grab_focus(GTK_WIDGET(row));
    } else gtk_widget_grab_focus(native);
    return XXWIDGETS_OK;
}

static int gtk_has_focus(const xxwidgets_widget *widget)
{
    gtk_widget_state *state = widget->platform;
    GtkWidget *toplevel, *focus;
    if (!state || !state->root) return 0;
    toplevel = gtk_widget_get_toplevel(state->root);
    focus = GTK_IS_WINDOW(toplevel) ? gtk_window_get_focus(GTK_WINDOW(toplevel)) : NULL;
    /* The window's focus widget, whether or not the window is active now. */
    return focus && (focus == state->root || gtk_widget_is_ancestor(focus, state->root));
}

static gboolean options_key(GtkWidget *native, GdkEventKey *key, gpointer user)
{
    xxwidgets_widget *dialog = user;
    (void)native;
    if (key->keyval == GDK_KEY_Escape) {
        xxwidgets_emit(dialog, XXWIDGETS_EVENT_CLOSE, 0);
        return TRUE;
    }
    if ((key->keyval == GDK_KEY_Return || key->keyval == GDK_KEY_KP_Enter) && dialog->app->modal_default) {
        xxwidgets_emit(dialog->app->modal_default, XXWIDGETS_EVENT_CLICK, 0);
        return TRUE;
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
        gtk_widget_state *state = dialog->platform;
        gtk_window_set_position(window, GTK_WIN_POS_CENTER_ON_PARENT);
        /* Centring places only the client area: check the frame against the
         * screen again when it maps (an earlier hidden map used up the check). */
        state->fit_position = 1;
        state->frame_width = state->frame_height = 0;
        g_signal_connect(window, "key-press-event", G_CALLBACK(options_key), dialog);
        for (widget = dialog->app->widgets; widget; widget = widget->next)
            if (widget->parent == dialog && widget->kind == XXWIDGETS_BUTTON && (widget == dialog->app->modal_default || !strcmp(widget->text, "OK"))) {
                gtk_widget_set_can_default(GTK_WIDGET(widget->native), TRUE);
                gtk_widget_grab_default(GTK_WIDGET(widget->native));
            }
        gtk_window_present(window);
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status gtk_about_content(xxwidgets_widget *window, const xxwidgets_about_dialog *about, const char *body)
{
    gtk_app_state *app = window->app->platform;
    gtk_widget_state *state = window->platform;
    int padding = 2 * app->cell_width, image_box = 12 * app->cell_width;
    int text_x = about->image ? image_box + 2 * padding : padding;
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL), *text = gtk_text_view_new();
    gtk_window_set_resizable(GTK_WINDOW(window->native), FALSE);
    /* A fixed-size window takes its size from the content's request. */
    window_update_minimum(window);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD_CHAR);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text)), body, -1);
    gtk_widget_set_size_request(scroll, window->rect.width * app->cell_width - text_x - padding, 17 * app->cell_height);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), text);
    gtk_layout_put(GTK_LAYOUT(state->content), scroll, text_x, app->cell_height);
    state->about_text = text;
    font_attach(text, app->font_css[XXWIDGETS_FONT_TEXT_EDITS], GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_widget_show_all(scroll);
    if (about->image) {
        GdkPixbuf *pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, (int)about->image_width, (int)about->image_height);
        GdkPixbuf *scaled;
        GtkWidget *image;
        unsigned int row;
        int width = image_box, height = image_box;
        if (!pixels) return XXWIDGETS_OUT_OF_MEMORY;
        for (row = 0; row < about->image_height; ++row)
            memcpy(gdk_pixbuf_get_pixels(pixels) + (size_t)row * gdk_pixbuf_get_rowstride(pixels), about->image + (size_t)row * about->image_width * 4,
                   (size_t)about->image_width * 4);
        if (about->image_width > about->image_height) height = (int)((uint64_t)image_box * about->image_height / about->image_width);
        else width = (int)((uint64_t)image_box * about->image_width / about->image_height);
        scaled = gdk_pixbuf_scale_simple(pixels, width > 0 ? width : 1, height > 0 ? height : 1, GDK_INTERP_BILINEAR);
        g_object_unref(pixels);
        if (!scaled) return XXWIDGETS_OUT_OF_MEMORY;
        image = gtk_image_new_from_pixbuf(scaled);
        g_object_unref(scaled);
        gtk_widget_set_size_request(image, image_box, image_box);
        gtk_layout_put(GTK_LAYOUT(state->content), image, padding, app->cell_height);
        gtk_widget_show(image);
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status gtk_paste_text(xxwidgets_widget *owner, char *text, size_t capacity, size_t *required)
{
    GtkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(owner->native), GDK_SELECTION_CLIPBOARD);
    gchar *value = clipboard ? gtk_clipboard_wait_for_text(clipboard) : NULL;
    const char *bytes = value ? value : "";
    *required = strlen(bytes) + 1;
    xxwidgets_status status = XXWIDGETS_OK;
    if (text || capacity) { if (capacity < *required) status = XXWIDGETS_BUFFER_TOO_SMALL; else memcpy(text, bytes, *required); }
    g_free(value); return status;
}

static xxwidgets_status gtk_copy_text(xxwidgets_widget *window, const char *text)
{
    GtkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(window->native), GDK_SELECTION_CLIPBOARD);
    if (!clipboard) return XXWIDGETS_PLATFORM_ERROR;
    gtk_clipboard_set_text(clipboard, text, -1);
    gtk_clipboard_store(clipboard);
    return XXWIDGETS_OK;
}

/* initial may be relative (a path typed into an entry); GtkFileChooser wants
 * absolute paths. Returns a g_malloc'd absolute path, or NULL for none. */
static gchar *absolute_path(const char *path)
{
    gchar *current, *joined;
    if (!path || !path[0]) return NULL;
    if (g_path_is_absolute(path)) return g_strdup(path);
    current = g_get_current_dir();
    joined = g_build_filename(current, path, NULL);
    g_free(current);
    return joined;
}

static void choose_file_start(GtkFileChooser *chooser, const char *initial, int save)
{
    gchar *path = absolute_path(initial), *folder, *name;
    if (!path) {
        /* Nothing typed yet: start where relative paths resolve, not in Recent. */
        folder = g_get_current_dir();
        gtk_file_chooser_set_current_folder(chooser, folder);
        g_free(folder);
        return;
    }
    if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
        gtk_file_chooser_set_current_folder(chooser, path);
    } else if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        gtk_file_chooser_set_filename(chooser, path);
    } else {
        /* A path that does not exist yet still names the folder to start in,
         * and for Save the proposed file name. */
        folder = g_path_get_dirname(path);
        name = g_path_get_basename(path);
        if (!g_file_test(folder, G_FILE_TEST_IS_DIR)) {
            /* Neither the file nor its folder exists: the working directory,
             * not GTK's Recent view. */
            g_free(folder);
            folder = g_get_current_dir();
        }
        gtk_file_chooser_set_current_folder(chooser, folder);
        if (save && name[0] && strcmp(name, ".") && strcmp(name, G_DIR_SEPARATOR_S)) gtk_file_chooser_set_current_name(chooser, name);
        g_free(folder);
        g_free(name);
    }
    g_free(path);
}

/* GtkFileChooserDialog sizes itself from saved settings, or from its font when
 * there are none (about 60 x 45 characters), whatever the screen. Keep it, and
 * so its Open and Cancel buttons, inside the owner's work area. */
static void chooser_mapped(GtkWidget *dialog, gpointer data)
{
    xxwidgets_widget *owner = data;
    GdkRectangle area;
    int frame_width, frame_height, x = 0, y = 0, width = 0, height = 0, max_width, max_height;
    gtk_window_get_position(GTK_WINDOW(owner->native), &x, &y);
    if (!window_frame_area(owner, x, y, &area, &frame_width, &frame_height)) return;
    gtk_window_get_size(GTK_WINDOW(dialog), &width, &height);
    max_width = area.width - frame_width;
    max_height = area.height - frame_height;
    if (max_width <= 0 || max_height <= 0 || (width <= max_width && height <= max_height)) return;
    if (width > max_width) width = max_width;
    if (height > max_height) height = max_height;
    gtk_window_resize(GTK_WINDOW(dialog), width, height);
    gtk_window_move(GTK_WINDOW(dialog), area.x + (area.width - frame_width - width) / 2, area.y + (area.height - frame_height - height) / 2);
}

static xxwidgets_status gtk_choose_file(xxwidgets_widget *owner, xxwidgets_file_dialog_mode mode, const char *title, const char *initial, char **path, int *accepted)
{
    int save = mode == XXWIDGETS_FILE_DIALOG_SAVE;
    GtkWidget *dialog;
    GtkFileChooser *chooser;
    xxwidgets_status status = XXWIDGETS_OK;
    dialog = gtk_file_chooser_dialog_new(title && title[0] ? title : (save ? "Save File" : "Open File"), GTK_WINDOW(owner->native),
                                         save ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, save ? "_Save" : "_Open",
                                         GTK_RESPONSE_ACCEPT, NULL);
    if (!dialog) return XXWIDGETS_PLATFORM_ERROR;
    /* Destroying the window from outside during gtk_dialog_run must not free
     * the dialog under us. */
    g_object_ref(dialog);
    chooser = GTK_FILE_CHOOSER(dialog);
    g_signal_connect(dialog, "map", G_CALLBACK(chooser_mapped), owner);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    gtk_file_chooser_set_local_only(chooser, TRUE);
    gtk_file_chooser_set_do_overwrite_confirmation(chooser, save);
    choose_file_start(chooser, initial, save);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *selected = gtk_file_chooser_get_filename(chooser);
        if (selected) {
            size_t length = strlen(selected);
            *path = (char *)malloc(length + 1);
            if (*path) {
                memcpy(*path, selected, length + 1);
                *accepted = 1;
            } else status = XXWIDGETS_OUT_OF_MEMORY;
            g_free(selected);
        }
    }
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    return status;
}

static size_t gtk_hexview_page_rows(const xxwidgets_widget *widget)
{
    size_t fallback = (size_t)(widget->rect.height > 1 ? widget->rect.height - 1 : 1);
    if (!widget->native || !widget->item_count)
        return fallback;
    GtkTreeView *tree = GTK_TREE_VIEW(widget->native);
    GdkRectangle visible, row;
    GtkTreePath *path = gtk_tree_path_new_from_indices(0, -1);
    gtk_tree_view_get_visible_rect(tree, &visible);
    gtk_tree_view_get_background_area(tree, path, NULL, &row);
    gtk_tree_path_free(path);
    if (visible.height <= 0 || row.height <= 0)
        return fallback;
    size_t rows = (size_t)(visible.height / row.height);
    return rows ? rows : 1;
}

static void gtk_menu_activate(GtkMenuItem *native, gpointer data)
{
    size_t index = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(native), "xxwidgets-menu-index"));
    xxwidgets_menu_activate(data, index);
}

static xxwidgets_status gtk_window_menu(xxwidgets_widget *window)
{
    gtk_widget_state *state = window->platform;
    gtk_app_state *app = window->app->platform;
    GtkWidget *bar = NULL, *group = NULL;
    const char *last = NULL;
    int menu_height = 0;
    if (window->menu_count) {
        bar = gtk_menu_bar_new();
        for (size_t i = 0; i < window->menu_count; ++i) {
            const xxwidgets_menu_item *entry = &window->menu_items[i];
            if (!last || strcmp(last, entry->menu)) {
                GtkWidget *heading = gtk_menu_item_new_with_label(entry->menu);
                group = gtk_menu_new();
                gtk_menu_item_set_submenu(GTK_MENU_ITEM(heading), group);
                gtk_menu_shell_append(GTK_MENU_SHELL(bar), heading);
                last = entry->menu;
            }
            GtkWidget *item = *entry->label ? gtk_menu_item_new_with_label(entry->label) : gtk_separator_menu_item_new();
            if (*entry->label) {
                gtk_widget_set_sensitive(item, entry->enabled);
                g_object_set_data(G_OBJECT(item), "xxwidgets-menu-index", GSIZE_TO_POINTER(i));
                g_signal_connect(item, "activate", G_CALLBACK(gtk_menu_activate), window);
            }
            gtk_menu_shell_append(GTK_MENU_SHELL(group), item);
        }
        gtk_widget_show_all(bar);
        gtk_widget_get_preferred_height(bar, &menu_height, NULL);
    }
    if (state->menu_bar) gtk_widget_destroy(state->menu_bar);
    state->menu_bar = bar;
    int old_height = state->menu_height;
    state->menu_height = menu_height;
    if (bar) gtk_box_pack_start(GTK_BOX(state->content_box), bar, FALSE, FALSE, 0);
    if (old_height != menu_height) {
        state->min_width = state->min_height = -1;
        window_update_minimum(window);
        apply_geometry(window, window->rect.x * app->cell_width, window->rect.y * app->cell_height,
                       window->rect.width * app->cell_width, window->rect.height * app->cell_height);
    }
    return XXWIDGETS_OK;
}

const xxwidgets_backend_ops xxwidgets_native_ops = {"GTK3",
                                                    gtk_init_backend,
                                                    gtk_shutdown_backend,
                                                    gtk_poll_backend,
                                                    gtk_create_backend,
                                                    gtk_destroy_backend,
                                                    gtk_sync_backend,
                                                    gtk_read_text_backend,
                                                    gtk_read_value_backend,
                                                    gtk_focus_backend,
                                                    gtk_modal_owner,
                                                    gtk_about_content,
                                                    gtk_copy_text,
                                                    gtk_apply_fonts,
                                                    gtk_choose_font,
                                                    gtk_preview_font,
                                                    NULL,
                                                    NULL,
                                                    gtk_choose_file,
                                                    gtk_has_focus,
                                                    gtk_accept_file_drops,
                                                    gtk_hexview_page_rows, gtk_paste_text, gtk_hexview_font, gtk_window_menu, NULL};
