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

/* The Detect It Easy main window content, including the Advanced tools and
 * per-format rows, laid out in measured units. */

#include "xxwidgets_components_internal.h"
#include "xxwidgets/xxwidgets_combobox.h"
#include "xxwidgets/xxwidgets_die_main_panel.h"

#include <stdlib.h>
#include <string.h>

#define DIE_MAIN_PANEL_FLAGS \
    (XXWIDGETS_DIE_MAIN_PANEL_SHOW_UPDATE | XXWIDGETS_DIE_MAIN_PANEL_SHOW_DEMANGLE | XXWIDGETS_DIE_MAIN_PANEL_TAG_FILTERS)

/* The right column: Advanced and Demangle at the top; Update database (Q2:
 * above Shortcuts), Shortcuts, Options, About and Exit at the bottom. */
static const xxwidgets_side_panel_item side_items[XXWIDGETS_DIE_SIDE_COUNT] = {
    {XXWIDGETS_DIE_SIDE_ADVANCED, XXWIDGETS_CHECKBOX, XXW_N_("Advanced"), XXWIDGETS_ICON_NONE, 0},
    {XXWIDGETS_DIE_SIDE_DEMANGLE, XXWIDGETS_BUTTON, XXW_N_("Demangle"), XXWIDGETS_ICON_DEMANGLE, 0},
    {XXWIDGETS_DIE_SIDE_UPDATE, XXWIDGETS_BUTTON, XXW_N_("Update database"), XXWIDGETS_ICON_DOWNLOAD, 1},
    {XXWIDGETS_DIE_SIDE_SHORTCUTS, XXWIDGETS_BUTTON, XXW_N_("Shortcuts"), XXWIDGETS_ICON_SHORTCUT, 1},
    {XXWIDGETS_DIE_SIDE_OPTIONS, XXWIDGETS_BUTTON, XXW_N_("Options"), XXWIDGETS_ICON_OPTION, 1},
    {XXWIDGETS_DIE_SIDE_ABOUT, XXWIDGETS_BUTTON, XXW_N_("About"), XXWIDGETS_ICON_INFO, 1},
    {XXWIDGETS_DIE_SIDE_EXIT, XXWIDGETS_BUTTON, XXW_N_("Exit"), XXWIDGETS_ICON_EXIT, 1}};

static const xxwidgets_button_grid_item tool_items[XXWIDGETS_DIE_TOOL_COUNT] = {
    {XXWIDGETS_DIE_TOOL_FILE_INFO, XXW_N_("File info"), XXWIDGETS_ICON_INFO, NULL},
    {XXWIDGETS_DIE_TOOL_MEMORY_MAP, XXW_N_("Memory map"), XXWIDGETS_ICON_MEMORY_MAP, NULL},
    {XXWIDGETS_DIE_TOOL_DISASM, XXW_N_("Disasm"), XXWIDGETS_ICON_DISASM, NULL},
    {XXWIDGETS_DIE_TOOL_HEX, XXW_N_("Hex"), XXWIDGETS_ICON_BINARY, NULL},
    {XXWIDGETS_DIE_TOOL_STRINGS, XXW_N_("Strings"), XXWIDGETS_ICON_STRING, NULL},
    {XXWIDGETS_DIE_TOOL_SIGNATURES, XXW_N_("Signatures"), XXWIDGETS_ICON_SIGNATURE, NULL},
    {XXWIDGETS_DIE_TOOL_VIRUSTOTAL, XXW_N_("VirusTotal"), XXWIDGETS_ICON_VIRUSTOTAL, NULL},
    {XXWIDGETS_DIE_TOOL_MIME, XXW_N_("MIME"), XXWIDGETS_ICON_MIME, NULL},
    {XXWIDGETS_DIE_TOOL_VISUALIZATION, XXW_N_("Visualization"), XXWIDGETS_ICON_IMAGE, NULL},
    {XXWIDGETS_DIE_TOOL_SEARCH, XXW_N_("Search"), XXWIDGETS_ICON_SEARCH, NULL},
    {XXWIDGETS_DIE_TOOL_HASH, XXW_N_("Hash"), XXWIDGETS_ICON_HASH, NULL},
    {XXWIDGETS_DIE_TOOL_ENTROPY, XXW_N_("Entropy"), XXWIDGETS_ICON_ENTROPY, NULL},
    {XXWIDGETS_DIE_TOOL_EXTRACTOR, XXW_N_("Extractor"), XXWIDGETS_ICON_EXTRACT, NULL},
    {XXWIDGETS_DIE_TOOL_YARA, XXW_N_("YARA"), XXWIDGETS_ICON_YARA, NULL}};

/* Scan panel controls of the engine row and the scan bar: disabled while no
 * file is loaded. The result tree (row F) stays as it is. */
static const xxwidgets_scan_panel_control_id file_controls[] = {
    XXWIDGETS_SCAN_PANEL_ENGINE_LABEL, XXWIDGETS_SCAN_PANEL_ENGINE,
    XXWIDGETS_SCAN_PANEL_ENDIANNESS_LABEL, XXWIDGETS_SCAN_PANEL_ENDIANNESS,
    XXWIDGETS_SCAN_PANEL_MODE_LABEL, XXWIDGETS_SCAN_PANEL_MODE,
    XXWIDGETS_SCAN_PANEL_ARCHITECTURE_LABEL, XXWIDGETS_SCAN_PANEL_ARCHITECTURE,
    XXWIDGETS_SCAN_PANEL_TYPE_LABEL, XXWIDGETS_SCAN_PANEL_TYPE,
    XXWIDGETS_SCAN_PANEL_SIGNATURES, XXWIDGETS_SCAN_PANEL_FLAGS, XXWIDGETS_SCAN_PANEL_DATABASES,
    XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS, XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS,
    XXWIDGETS_SCAN_PANEL_SCAN, XXWIDGETS_SCAN_PANEL_DIRECTORY, XXWIDGETS_SCAN_PANEL_LOG,
    XXWIDGETS_SCAN_PANEL_REPORT, XXWIDGETS_SCAN_PANEL_ELAPSED};

/* Scan panel texts re-applied on retranslate (the scan panel fixes its texts
 * at creation; these are its own msgids). */
static const struct {
    xxwidgets_scan_panel_control_id id;
    const char *text;
} scan_texts[] = {
    {XXWIDGETS_SCAN_PANEL_ENGINE_LABEL, XXW_N_("Scan")},
    {XXWIDGETS_SCAN_PANEL_ENDIANNESS_LABEL, XXW_N_("Endianness")},
    {XXWIDGETS_SCAN_PANEL_MODE_LABEL, XXW_N_("Mode")},
    {XXWIDGETS_SCAN_PANEL_ARCHITECTURE_LABEL, XXW_N_("Architecture")},
    {XXWIDGETS_SCAN_PANEL_TYPE_LABEL, XXW_N_("Type")},
    {XXWIDGETS_SCAN_PANEL_SIGNATURES, XXW_N_("Signatures")},
    {XXWIDGETS_SCAN_PANEL_SCAN, XXW_N_("Scan")},
    {XXWIDGETS_SCAN_PANEL_DIRECTORY, XXW_N_("Directory")},
    {XXWIDGETS_SCAN_PANEL_LOG, XXW_N_("Log")}};

struct xxwidgets_die_main_panel {
    xxwidgets_widget *window;
    unsigned int flags;
    int advanced;
    int loaded;
    xxwidgets_file_name_bar *bar;
    xxwidgets_file_header *header;
    xxwidgets_button_grid *tools;
    xxwidgets_format_panel *format_panel;
    xxwidgets_scan_panel *scan;
    xxwidgets_side_panel *side;
};

/* The sizes the layout needs for one view. */
typedef struct die_parts {
    xxwidgets_metrics metrics;
    int bar_height, bar_minimum;
    int header_width, header_height;
    int tools_width, tools_height;
    int format_width, format_height;
    int scan_width, scan_height;
    int side_width, side_height;
    int minimum_width, minimum_height;
} die_parts;

static int side_item_shown(const xxwidgets_die_main_panel *panel, int advanced, int id)
{
    if (id == XXWIDGETS_DIE_SIDE_DEMANGLE) return advanced && (panel->flags & XXWIDGETS_DIE_MAIN_PANEL_SHOW_DEMANGLE);
    if (id == XXWIDGETS_DIE_SIDE_UPDATE) return (panel->flags & XXWIDGETS_DIE_MAIN_PANEL_SHOW_UPDATE) != 0;
    return 1;
}

/* The side panel's own rule (widest item + 2 * panel_inset; items H high and
 * S apart), evaluated for either view. */
static xxwidgets_status side_size(const xxwidgets_die_main_panel *panel, const xxwidgets_metrics *m, int advanced, int *width, int *height)
{
    int widest = 0, shown = 0, i;
    for (i = 0; i < XXWIDGETS_DIE_SIDE_COUNT; ++i) {
        int w = 0, h = 0;
        xxwidgets_status status;
        if (!side_item_shown(panel, advanced, i)) continue;
        status = xxwidgets_widget_get_preferred_size(xxwidgets_side_panel_control(panel->side, i), &w, &h);
        if (status != XXWIDGETS_OK) return status;
        widest = xxwidgets_component_max(widest, w);
        ++shown;
    }
    *width = widest + 2 * m->panel_inset;
    *height = 2 * m->panel_inset + shown * m->control_height + (shown > 0 ? (shown - 1) * m->spacing : 0);
    return XXWIDGETS_OK;
}

static xxwidgets_status compute_parts(const xxwidgets_die_main_panel *panel, int advanced, die_parts *parts)
{
    xxwidgets_status status;
    int unused, left_width, left_height;
    memset(parts, 0, sizeof(*parts));
    status = xxwidgets_app_get_metrics(panel->window->app, &parts->metrics);
    if (status == XXWIDGETS_OK) status = xxwidgets_file_name_bar_get_preferred_height(panel->bar, &parts->bar_height);
    if (status == XXWIDGETS_OK) status = xxwidgets_file_name_bar_get_minimum_width(panel->bar, &parts->bar_minimum);
    if (status == XXWIDGETS_OK) status = xxwidgets_file_header_view_size(panel->header, advanced, 1, &parts->header_width, &unused);
    if (status == XXWIDGETS_OK) status = xxwidgets_file_header_view_size(panel->header, advanced, 0, &unused, &parts->header_height);
    if (status == XXWIDGETS_OK && advanced)
        status = xxwidgets_button_grid_get_minimum_size(panel->tools, &parts->tools_width, &parts->tools_height);
    if (status == XXWIDGETS_OK && advanced)
        status = xxwidgets_format_panel_get_minimum_size(panel->format_panel, &parts->format_width, &parts->format_height);
    if (status == XXWIDGETS_OK) status = xxwidgets_scan_panel_get_minimum_size(panel->scan, &parts->scan_width, &parts->scan_height);
    if (status == XXWIDGETS_OK) status = side_size(panel, &parts->metrics, advanced, &parts->side_width, &parts->side_height);
    if (status != XXWIDGETS_OK) return status;
    left_width = xxwidgets_component_max(parts->header_width, parts->scan_width);
    left_width = xxwidgets_component_max(left_width, xxwidgets_component_max(parts->tools_width, parts->format_width));
    left_height = parts->header_height + parts->metrics.spacing + parts->scan_height;
    if (parts->tools_height) left_height += parts->tools_height + parts->metrics.spacing;
    if (parts->format_height) left_height += parts->format_height + parts->metrics.spacing;
    parts->minimum_width = 2 * parts->metrics.margin +
                           xxwidgets_component_max(parts->bar_minimum, left_width + parts->metrics.spacing + parts->side_width);
    parts->minimum_height = 2 * parts->metrics.margin + parts->bar_height + parts->metrics.spacing +
                            xxwidgets_component_max(left_height, parts->side_height);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_die_main_panel_get_minimum_size(const xxwidgets_die_main_panel *panel, int advanced, int *width, int *height)
{
    die_parts parts;
    xxwidgets_status status;
    if (!panel || (advanced != 0 && advanced != 1) || !width || !height) return XXWIDGETS_INVALID_ARGUMENT;
    status = compute_parts(panel, advanced, &parts);
    if (status != XXWIDGETS_OK) return status;
    *width = parts.minimum_width;
    *height = parts.minimum_height;
    return XXWIDGETS_OK;
}

/* Qt's pixels at 96 DPI in units: scaled like the margin (9 at 96 DPI on
 * Win32, logical pixels on GTK, points on Cocoa), so a taller theme control
 * does not blow the window up; about one cell per 8 pixels in cells. */
static int qt_window_units(const xxwidgets_metrics *m, int qt_pixels)
{
    if (m->unit_is_cell || m->margin <= 0) return xxwidgets_component_qt(m, qt_pixels);
    return (int)(((long long)qt_pixels * m->margin + 4) / 9);
}

xxwidgets_status xxwidgets_die_main_panel_get_default_size(const xxwidgets_die_main_panel *panel, int advanced, int *width, int *height)
{
    die_parts parts;
    xxwidgets_status status;
    int scan_width = 0, header_width = 0, unused = 0, preferred;
    if (!panel || (advanced != 0 && advanced != 1) || !width || !height) return XXWIDGETS_INVALID_ARGUMENT;
    status = compute_parts(panel, advanced, &parts);
    if (status == XXWIDGETS_OK) status = xxwidgets_scan_panel_get_preferred_size(panel->scan, &scan_width, &unused);
    if (status == XXWIDGETS_OK) status = xxwidgets_file_header_view_size(panel->header, advanced, 0, &header_width, &unused);
    if (status != XXWIDGETS_OK) return status;
    preferred = 2 * parts.metrics.margin + xxwidgets_component_max(header_width, scan_width) + parts.metrics.spacing + parts.side_width;
    *width = xxwidgets_component_max(xxwidgets_component_max(parts.minimum_width, preferred), qt_window_units(&parts.metrics, 720));
    *height = xxwidgets_component_max(parts.minimum_height, qt_window_units(&parts.metrics, 500));
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_die_main_panel_layout(xxwidgets_die_main_panel *panel)
{
    die_parts parts;
    xxwidgets_status status;
    int client_width = 0, client_height = 0, width, height, margin, spacing, top, side_x, left_width, y;
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    if (panel->window->app->syncing) return XXWIDGETS_BUSY;
    status = compute_parts(panel, panel->advanced, &parts);
    if (status == XXWIDGETS_OK) status = xxwidgets_window_get_client_size(panel->window, &client_width, &client_height);
    if (status != XXWIDGETS_OK) return status;
    /* Below the minimum the parts keep their minimum (the window grows to it
     * through set_minimum_frame). */
    width = xxwidgets_component_max(client_width, parts.minimum_width);
    height = xxwidgets_component_max(client_height, parts.minimum_height);
    margin = parts.metrics.margin;
    spacing = parts.metrics.spacing;
    /* Row A over the full width. */
    status = xxwidgets_file_name_bar_set_bounds(panel->bar, xxwidgets_layout_rect(margin, margin, width - 2 * margin, parts.bar_height));
    if (status != XXWIDGETS_OK) return status;
    /* The right column from row B to the bottom; the left area beside it. */
    top = margin + parts.bar_height + spacing;
    side_x = width - margin - parts.side_width;
    left_width = side_x - spacing - margin;
    status = xxwidgets_side_panel_set_bounds(panel->side, xxwidgets_layout_rect(side_x, top, parts.side_width, height - margin - top));
    if (status != XXWIDGETS_OK) return status;
    status = xxwidgets_file_header_set_bounds(panel->header, xxwidgets_layout_rect(margin, top, left_width, parts.header_height));
    if (status != XXWIDGETS_OK) return status;
    y = top + parts.header_height + spacing;
    if (parts.tools_height) {
        status = xxwidgets_button_grid_set_bounds(panel->tools, xxwidgets_layout_rect(margin, y, left_width, parts.tools_height));
        if (status != XXWIDGETS_OK) return status;
        y += parts.tools_height + spacing;
    }
    if (parts.format_height) {
        status = xxwidgets_format_panel_set_bounds(panel->format_panel, xxwidgets_layout_rect(margin, y, left_width, parts.format_height));
        if (status != XXWIDGETS_OK) return status;
        y += parts.format_height + spacing;
    }
    /* Rows E, F and G: the tree takes every extra unit of height. */
    return xxwidgets_scan_panel_set_bounds(panel->scan, xxwidgets_layout_rect(margin, y, left_width, height - margin - y));
}

static xxwidgets_status apply_view(xxwidgets_die_main_panel *panel, int advanced)
{
    xxwidgets_status status, end;
    int width = 0, height = 0;
    status = xxwidgets_window_begin_update(panel->window);
    if (status != XXWIDGETS_OK) return status;
    /* Show and hide only: every control exists from creation (Tab order and
     * handles stay stable); the core hands the focus on from a hidden one. */
    status = xxwidgets_file_header_set_advanced(panel->header, advanced);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_side_panel_set_item_visible(panel->side, XXWIDGETS_DIE_SIDE_DEMANGLE,
                                                        side_item_shown(panel, advanced, XXWIDGETS_DIE_SIDE_DEMANGLE));
    if (status == XXWIDGETS_OK) status = xxwidgets_button_grid_set_visible(panel->tools, advanced);
    if (status == XXWIDGETS_OK) status = xxwidgets_format_panel_set_visible(panel->format_panel, advanced);
    if (status == XXWIDGETS_OK) status = xxwidgets_side_panel_set_checked(panel->side, XXWIDGETS_DIE_SIDE_ADVANCED, advanced);
    if (status == XXWIDGETS_OK) {
        panel->advanced = advanced;
        /* The window minimum for this view: a smaller window grows, none
         * shrinks; then the parts are laid out over the client area. */
        status = xxwidgets_die_main_panel_get_minimum_size(panel, advanced, &width, &height);
        if (status == XXWIDGETS_OK) status = xxwidgets_window_set_minimum_frame(panel->window, width, height);
        if (status == XXWIDGETS_OK) status = xxwidgets_die_main_panel_layout(panel);
    }
    end = xxwidgets_window_end_update(panel->window);
    return status != XXWIDGETS_OK ? status : end;
}

xxwidgets_status xxwidgets_die_main_panel_set_advanced(xxwidgets_die_main_panel *panel, int advanced)
{
    if (!panel || (advanced != 0 && advanced != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    if (panel->window->app->syncing) return XXWIDGETS_BUSY;
    return apply_view(panel, advanced);
}

int xxwidgets_die_main_panel_get_advanced(const xxwidgets_die_main_panel *panel)
{
    return panel ? panel->advanced : 0;
}

/* What a scan panel control is when a file is loaded: Report only with a
 * report, the tag filters only with tags (the scan panel's own rules). */
static int loaded_enabled(xxwidgets_die_main_panel *panel, xxwidgets_scan_panel_control_id id, xxwidgets_widget *widget)
{
    if (id == XXWIDGETS_SCAN_PANEL_REPORT) {
        size_t required = 0;
        return xxwidgets_scan_panel_get_report(panel->scan, NULL, 0, &required) == XXWIDGETS_OK && required > 1;
    }
    if (id == XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS || id == XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS) return xxwidgets_combobox_count(widget) > 0;
    return 1;
}

xxwidgets_status xxwidgets_die_main_panel_set_loaded(xxwidgets_die_main_panel *panel, int loaded)
{
    xxwidgets_status status;
    size_t i;
    if (!panel || (loaded != 0 && loaded != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_file_header_set_loaded(panel->header, loaded);
    if (status == XXWIDGETS_OK) status = xxwidgets_button_grid_set_enabled_all(panel->tools, loaded);
    if (status == XXWIDGETS_OK) status = xxwidgets_format_panel_set_enabled_all(panel->format_panel, loaded);
    for (i = 0; status == XXWIDGETS_OK && i < sizeof(file_controls) / sizeof(file_controls[0]); ++i) {
        xxwidgets_widget *widget = xxwidgets_scan_panel_control(panel->scan, file_controls[i]);
        if (!widget) continue;
        status = xxwidgets_widget_set_enabled(widget, loaded && loaded_enabled(panel, file_controls[i], widget));
    }
    if (status == XXWIDGETS_OK) panel->loaded = loaded;
    return status;
}

static unsigned int scan_options(const xxwidgets_die_main_panel *panel)
{
    unsigned int options = XXWIDGETS_SCAN_PANEL_ENGINE_ROW | XXWIDGETS_SCAN_PANEL_HIDE_FILE_TYPE | XXWIDGETS_SCAN_PANEL_FIXED_CAPTIONS;
    if (!(panel->flags & XXWIDGETS_DIE_MAIN_PANEL_TAG_FILTERS)) options |= XXWIDGETS_SCAN_PANEL_HIDE_TAG_FILTERS;
    return options;
}

xxwidgets_status xxwidgets_die_main_panel_create(xxwidgets_widget *window, unsigned int flags, xxwidgets_die_main_panel **out)
{
    xxwidgets_die_main_panel *panel;
    xxwidgets_status status;
    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!window || window->kind != XXWIDGETS_WINDOW || (flags & ~(unsigned int)DIE_MAIN_PANEL_FLAGS)) return XXWIDGETS_INVALID_ARGUMENT;
    if (xxwidgets_component_busy(window->app)) return XXWIDGETS_BUSY;
    panel = (xxwidgets_die_main_panel *)calloc(1, sizeof(*panel));
    if (!panel) return XXWIDGETS_OUT_OF_MEMORY;
    panel->window = window;
    panel->flags = flags;
    panel->loaded = 1;
    /* Creation order is the Tab order: A, B, C, D, E-F-G, right column. */
    status = xxwidgets_file_name_bar_create(window, 0, &panel->bar);
    if (status == XXWIDGETS_OK) status = xxwidgets_file_header_create(window, 0, &panel->header);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_button_grid_create(window, tool_items, XXWIDGETS_DIE_TOOL_COUNT, 7,
                                              XXWIDGETS_BUTTON_GRID_FRAMED, &panel->tools);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_button_grid_set_item_state(panel->tools, XXWIDGETS_DIE_TOOL_SIGNATURES,
            XXWIDGETS_GRID_ITEM_DISABLED, XXW_N_("Not available in this version"));
    if (status == XXWIDGETS_OK)
        status = xxwidgets_button_grid_set_item_state(panel->tools, XXWIDGETS_DIE_TOOL_MIME,
            XXWIDGETS_GRID_ITEM_DISABLED, XXW_N_("Not available in this version"));
    if (status == XXWIDGETS_OK)
        status = xxwidgets_button_grid_set_item_state(panel->tools, XXWIDGETS_DIE_TOOL_VISUALIZATION,
            XXWIDGETS_GRID_ITEM_DISABLED, XXW_N_("Not available in this version"));
    if (status == XXWIDGETS_OK)
        status = xxwidgets_button_grid_set_item_state(panel->tools, XXWIDGETS_DIE_TOOL_SEARCH,
            XXWIDGETS_GRID_ITEM_DISABLED, XXW_N_("Not available in this version"));
    if (status == XXWIDGETS_OK)
        status = xxwidgets_button_grid_set_item_state(panel->tools, XXWIDGETS_DIE_TOOL_YARA,
            XXWIDGETS_GRID_ITEM_HIDDEN, NULL);
    if (status == XXWIDGETS_OK) status = xxwidgets_format_panel_create(window, 0, &panel->format_panel);
    if (status == XXWIDGETS_OK) {
        /* The scan panel's cell bounds only matter until the first layout. */
        static const xxwidgets_rect cells = {0, 0, 44, 14};
        status = xxwidgets_scan_panel_create(window, cells, &panel->scan);
    }
    if (status == XXWIDGETS_OK) status = xxwidgets_scan_panel_set_options(panel->scan, scan_options(panel));
    /* B7: DIE's result tree shows expander arrows without connecting lines. */
    if (status == XXWIDGETS_OK)
        status = xxwidgets_treeview_set_style(xxwidgets_scan_panel_control(panel->scan, XXWIDGETS_SCAN_PANEL_RESULTS), XXWIDGETS_TREE_ARROWS);
    if (status == XXWIDGETS_OK) status = xxwidgets_side_panel_create(window, side_items, XXWIDGETS_DIE_SIDE_COUNT, &panel->side);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_side_panel_set_item_visible(panel->side, XXWIDGETS_DIE_SIDE_UPDATE,
                                                        side_item_shown(panel, 0, XXWIDGETS_DIE_SIDE_UPDATE));
    /* Update database waits for the application to report an updater (Q2). */
    if (status == XXWIDGETS_OK) status = xxwidgets_side_panel_set_item_enabled(panel->side, XXWIDGETS_DIE_SIDE_UPDATE, 0);
    if (status == XXWIDGETS_OK) status = xxwidgets_side_panel_set_item_visible(panel->side, XXWIDGETS_DIE_SIDE_DEMANGLE, 0);
    if (status == XXWIDGETS_OK) status = xxwidgets_die_main_panel_set_loaded(panel, 0);
    if (status == XXWIDGETS_OK) status = apply_view(panel, 0);
    if (status != XXWIDGETS_OK) {
        (void)xxwidgets_die_main_panel_destroy(panel);
        return status;
    }
    *out = panel;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_die_main_panel_destroy(xxwidgets_die_main_panel *panel)
{
    xxwidgets_status status;
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    if (xxwidgets_component_busy(panel->window->app)) return XXWIDGETS_BUSY;
    /* Reverse creation order; a part that refuses keeps the panel alive. */
    if (panel->side) {
        status = xxwidgets_side_panel_destroy(panel->side);
        if (status != XXWIDGETS_OK) return status;
        panel->side = NULL;
    }
    if (panel->scan) {
        status = xxwidgets_scan_panel_destroy(panel->scan);
        if (status != XXWIDGETS_OK) return status;
        panel->scan = NULL;
    }
    if (panel->format_panel) {
        status = xxwidgets_format_panel_destroy(panel->format_panel);
        if (status != XXWIDGETS_OK) return status;
        panel->format_panel = NULL;
    }
    if (panel->tools) {
        status = xxwidgets_button_grid_destroy(panel->tools);
        if (status != XXWIDGETS_OK) return status;
        panel->tools = NULL;
    }
    if (panel->header) {
        status = xxwidgets_file_header_destroy(panel->header);
        if (status != XXWIDGETS_OK) return status;
        panel->header = NULL;
    }
    if (panel->bar) {
        status = xxwidgets_file_name_bar_destroy(panel->bar);
        if (status != XXWIDGETS_OK) return status;
        panel->bar = NULL;
    }
    free(panel);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_die_main_panel_retranslate(xxwidgets_die_main_panel *panel)
{
    xxwidgets_app *app;
    xxwidgets_status status;
    size_t i;
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    if (panel->window->app->syncing) return XXWIDGETS_BUSY;
    app = panel->window->app;
    status = xxwidgets_file_name_bar_retranslate(panel->bar);
    if (status == XXWIDGETS_OK) status = xxwidgets_file_header_retranslate(panel->header);
    if (status == XXWIDGETS_OK) status = xxwidgets_button_grid_retranslate(panel->tools);
    if (status == XXWIDGETS_OK) status = xxwidgets_format_panel_retranslate(panel->format_panel);
    if (status == XXWIDGETS_OK) status = xxwidgets_side_panel_retranslate(panel->side);
    for (i = 0; status == XXWIDGETS_OK && i < sizeof(scan_texts) / sizeof(scan_texts[0]); ++i)
        status = xxwidgets_widget_set_text(xxwidgets_scan_panel_control(panel->scan, scan_texts[i].id), XXW_TR(app, scan_texts[i].text));
    if (status == XXWIDGETS_OK) {
        static const char *const engine_names[2] = {XXW_N_("Automatic"), XXW_N_("Detect It Easy (DiE)")};
        static const uint64_t engine_values[2] = {0, 1};
        xxwidgets_widget *engine = xxwidgets_scan_panel_control(panel->scan, XXWIDGETS_SCAN_PANEL_ENGINE);
        int selected = 0;
        status = xxwidgets_widget_get_value(engine, &selected);
        if (status == XXWIDGETS_OK) status = xxwidgets_translated_choices(engine, engine_names, engine_values, 2);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(engine, selected >= 0 && selected < 2 ? selected : 0);
    }
    /* The fixed Flags / Database captions and the row E tooltips. */
    if (status == XXWIDGETS_OK) status = xxwidgets_scan_panel_set_options(panel->scan, scan_options(panel));
    /* Texts changed size: the minimum and the layout follow. */
    if (status == XXWIDGETS_OK) status = apply_view(panel, panel->advanced);
    return status;
}

static int scan_click_action(xxwidgets_scan_panel_control_id id, xxwidgets_die_action_id *action)
{
    switch (id) {
        case XXWIDGETS_SCAN_PANEL_SCAN: *action = XXWIDGETS_DIE_ACTION_SCAN; return 1;
        case XXWIDGETS_SCAN_PANEL_REPORT: *action = XXWIDGETS_DIE_ACTION_REPORT; return 1;
        case XXWIDGETS_SCAN_PANEL_SIGNATURES: *action = XXWIDGETS_DIE_ACTION_SIGNATURES; return 1;
        case XXWIDGETS_SCAN_PANEL_DIRECTORY: *action = XXWIDGETS_DIE_ACTION_DIRECTORY; return 1;
        case XXWIDGETS_SCAN_PANEL_LOG: *action = XXWIDGETS_DIE_ACTION_LOG; return 1;
        default: return 0;
    }
}

static int side_action(int id, xxwidgets_die_action_id *action)
{
    switch (id) {
        case XXWIDGETS_DIE_SIDE_DEMANGLE: *action = XXWIDGETS_DIE_ACTION_DEMANGLE; return 1;
        case XXWIDGETS_DIE_SIDE_UPDATE: *action = XXWIDGETS_DIE_ACTION_UPDATE; return 1;
        case XXWIDGETS_DIE_SIDE_SHORTCUTS: *action = XXWIDGETS_DIE_ACTION_SHORTCUTS; return 1;
        case XXWIDGETS_DIE_SIDE_OPTIONS: *action = XXWIDGETS_DIE_ACTION_OPTIONS; return 1;
        case XXWIDGETS_DIE_SIDE_ABOUT: *action = XXWIDGETS_DIE_ACTION_ABOUT; return 1;
        case XXWIDGETS_DIE_SIDE_EXIT: *action = XXWIDGETS_DIE_ACTION_EXIT; return 1;
        default: return 0;
    }
}

int xxwidgets_die_main_panel_handle_event(xxwidgets_die_main_panel *panel, const xxwidgets_event *event, xxwidgets_die_action *out)
{
    xxwidgets_die_action action;
    xxwidgets_file_name_bar_action bar_action = XXWIDGETS_FILE_NAME_BAR_ACTION_NONE;
    xxwidgets_file_header_action header_action;
    xxwidgets_widget *widget;
    int id = 0, value = 0, i;
    memset(&action, 0, sizeof(action));
    if (out) *out = action;
    if (!panel || !event || !event->widget) return 0;
    widget = event->widget;
    /* The window's RESIZE: lay out again (the application may also act on it). */
    if (widget == panel->window) {
        if (event->type != XXWIDGETS_EVENT_RESIZE) return 0;
        (void)xxwidgets_die_main_panel_layout(panel);
        return 1;
    }
    if (xxwidgets_file_name_bar_handle_event(panel->bar, event, &bar_action)) {
        switch (bar_action) {
            case XXWIDGETS_FILE_NAME_BAR_ACTION_RECENT: action.id = XXWIDGETS_DIE_ACTION_RECENT; break;
            case XXWIDGETS_FILE_NAME_BAR_ACTION_BROWSE: action.id = XXWIDGETS_DIE_ACTION_BROWSE; break;
            case XXWIDGETS_FILE_NAME_BAR_ACTION_OPEN: action.id = XXWIDGETS_DIE_ACTION_OPEN_PATH; break;
            case XXWIDGETS_FILE_NAME_BAR_ACTION_EDITED: action.id = XXWIDGETS_DIE_ACTION_PATH_EDITED; break;
            default: break;
        }
        if (out) *out = action;
        return 1;
    }
    memset(&header_action, 0, sizeof(header_action));
    if (xxwidgets_file_header_handle_event(panel->header, event, &header_action)) {
        if (header_action.id == XXWIDGETS_FILE_HEADER_ACTION_FILE_TYPE_SELECTED) {
            action.id = XXWIDGETS_DIE_ACTION_FILE_TYPE;
            action.value = (int)header_action.index;
            action.type_id = header_action.type_id;
        } else if (header_action.id == XXWIDGETS_FILE_HEADER_ACTION_ENTRY_POINT_ARROW)
            action.id = XXWIDGETS_DIE_ACTION_ENTRY_POINT;
        if (out) *out = action;
        return 1;
    }
    if (xxwidgets_button_grid_handle_event(panel->tools, event, &id)) {
        if (id >= 0 && event->type == XXWIDGETS_EVENT_CLICK && xxwidgets_component_live(widget)) {
            action.id = XXWIDGETS_DIE_ACTION_TOOL;
            action.value = id;
        }
        if (out) *out = action;
        return 1;
    }
    {
        xxwidgets_format_item item;
        int arrow = 0;
        if (xxwidgets_format_panel_handle_event(panel->format_panel, event, &item, &arrow)) {
            if (item < XXWIDGETS_FORMAT_ITEM_COUNT && event->type == XXWIDGETS_EVENT_CLICK && xxwidgets_component_live(widget)) {
                action.id = arrow ? XXWIDGETS_DIE_ACTION_FORMAT_ARROW : XXWIDGETS_DIE_ACTION_FORMAT;
                action.value = (int)item;
            }
            if (out) *out = action;
            return 1;
        }
    }
    if (xxwidgets_side_panel_handle_event(panel->side, event, &id, &value)) {
        if (id == XXWIDGETS_DIE_SIDE_ADVANCED) {
            /* The view first, then the application stores the setting. */
            /* A failed switch keeps the old view: the checkbox goes back to
             * it, so it and panel->advanced never disagree. */
            if (apply_view(panel, value ? 1 : 0) != XXWIDGETS_OK)
                (void)xxwidgets_side_panel_set_checked(panel->side, XXWIDGETS_DIE_SIDE_ADVANCED, panel->advanced);
            action.id = XXWIDGETS_DIE_ACTION_ADVANCED_CHANGED;
            action.value = panel->advanced;
        } else
            (void)side_action(id, &action.id);
        if (out) *out = action;
        return 1;
    }
    if (widget == xxwidgets_side_panel_frame(panel->side)) return 1;
    for (i = 0; i < XXWIDGETS_DIE_SIDE_COUNT; ++i)
        if (widget == xxwidgets_side_panel_control(panel->side, i)) return 1; /* hidden or disabled: no action */
    /* Scan panel: the engine combo and the buttons. Flags, Database, the tag
     * filters and the result tree stay the application's, as before. */
    if (widget == xxwidgets_scan_panel_control(panel->scan, XXWIDGETS_SCAN_PANEL_ENGINE)) {
        if (event->type == XXWIDGETS_EVENT_SELECT && xxwidgets_component_live(widget)) {
            action.id = XXWIDGETS_DIE_ACTION_ENGINE;
            action.value = event->value;
        }
        if (out) *out = action;
        return 1;
    }
    for (i = 0; i < XXWIDGETS_SCAN_PANEL_CONTROL_COUNT; ++i) {
        xxwidgets_die_action_id click;
        if (!scan_click_action((xxwidgets_scan_panel_control_id)i, &click) ||
            widget != xxwidgets_scan_panel_control(panel->scan, (xxwidgets_scan_panel_control_id)i))
            continue;
        if (event->type == XXWIDGETS_EVENT_CLICK && xxwidgets_component_live(widget)) action.id = click;
        if (out) *out = action;
        return 1;
    }
    return 0;
}

xxwidgets_file_name_bar *xxwidgets_die_main_panel_file_name_bar(const xxwidgets_die_main_panel *panel)
{
    return panel ? panel->bar : NULL;
}

xxwidgets_file_header *xxwidgets_die_main_panel_file_header(const xxwidgets_die_main_panel *panel)
{
    return panel ? panel->header : NULL;
}

xxwidgets_button_grid *xxwidgets_die_main_panel_tools(const xxwidgets_die_main_panel *panel)
{
    return panel ? panel->tools : NULL;
}

xxwidgets_format_panel *xxwidgets_die_main_panel_format_panel(const xxwidgets_die_main_panel *panel)
{
    return panel ? panel->format_panel : NULL;
}

xxwidgets_scan_panel *xxwidgets_die_main_panel_scan_panel(const xxwidgets_die_main_panel *panel)
{
    return panel ? panel->scan : NULL;
}

xxwidgets_side_panel *xxwidgets_die_main_panel_side_panel(const xxwidgets_die_main_panel *panel)
{
    return panel ? panel->side : NULL;
}
