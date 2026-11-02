/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_components_internal.h"
#include "xxwidgets/xxwidgets_format_panel.h"
#include "xxwidgets/xxwidgets_icons.h"

#include <stdlib.h>
#include <string.h>

typedef struct format_spec {
    const char *label;
    xxwidgets_icon_id icon;
    unsigned int page_mask;
    unsigned char field, row2, default_shown, with_arrow;
} format_spec;

#define PAGE(p) (1u << (p))
#define BUTTON(label, icon, pages) {XXW_N_(label), icon, pages, 0, 0, 1, 0}
#define FIELD(label, pages, arrow) {XXW_N_(label), XXWIDGETS_ICON_NONE, pages, 1, 1, 1, arrow}

static const format_spec specs[XXWIDGETS_FORMAT_ITEM_COUNT] = {
    {NULL, XXWIDGETS_ICON_INFO, 0x3ffu, 0, 0, 1, 0},
    BUTTON("Export", XXWIDGETS_ICON_EXPORT, PAGE(XXWIDGETS_FORMAT_PAGE_PE)),
    BUTTON("Import", XXWIDGETS_ICON_IMPORT, PAGE(XXWIDGETS_FORMAT_PAGE_PE)),
    BUTTON("Resources", XXWIDGETS_ICON_RESOURCE, PAGE(XXWIDGETS_FORMAT_PAGE_PE)),
    BUTTON(".NET", XXWIDGETS_ICON_DOT_NET, PAGE(XXWIDGETS_FORMAT_PAGE_PE)),
    BUTTON("TLS", XXWIDGETS_ICON_TLS, PAGE(XXWIDGETS_FORMAT_PAGE_PE)),
    BUTTON("Overlay", XXWIDGETS_ICON_OVERLAY, PAGE(XXWIDGETS_FORMAT_PAGE_PE)),
    FIELD("Sections", PAGE(XXWIDGETS_FORMAT_PAGE_PE), 1),
    FIELD("Time date stamp", PAGE(XXWIDGETS_FORMAT_PAGE_PE), 0),
    FIELD("Size of image", PAGE(XXWIDGETS_FORMAT_PAGE_PE), 0),
    {XXW_N_("Resources"), XXWIDGETS_ICON_NONE, PAGE(XXWIDGETS_FORMAT_PAGE_PE), 1, 1, 1, 0},
    BUTTON("Manifest", XXWIDGETS_ICON_MANIFEST, PAGE(XXWIDGETS_FORMAT_PAGE_PE)),
    BUTTON("Version", XXWIDGETS_ICON_VERSION, PAGE(XXWIDGETS_FORMAT_PAGE_PE)),
    FIELD("Programs", PAGE(XXWIDGETS_FORMAT_PAGE_ELF), 1),
    FIELD("Sections", PAGE(XXWIDGETS_FORMAT_PAGE_ELF), 1),
    FIELD("Commands", PAGE(XXWIDGETS_FORMAT_PAGE_MACH), 1),
    FIELD("Segments", PAGE(XXWIDGETS_FORMAT_PAGE_MACH), 1),
    FIELD("Sections", PAGE(XXWIDGETS_FORMAT_PAGE_MACH), 1),
    FIELD("Libraries", PAGE(XXWIDGETS_FORMAT_PAGE_MACH), 1),
    BUTTON("Overlay", XXWIDGETS_ICON_OVERLAY, PAGE(XXWIDGETS_FORMAT_PAGE_MSDOS)),
    {XXW_N_("MANIFEST.MF"), XXWIDGETS_ICON_MANIFEST, PAGE(XXWIDGETS_FORMAT_PAGE_ARCHIVE), 0, 0, 0, 0},
    {XXW_N_("AndroidManifest.xml"), XXWIDGETS_ICON_MANIFEST, PAGE(XXWIDGETS_FORMAT_PAGE_ARCHIVE), 0, 0, 0, 0},
    {XXW_N_("Files"), XXWIDGETS_ICON_FILE, PAGE(XXWIDGETS_FORMAT_PAGE_ARCHIVE), 0, 0, 0, 0},
    {XXW_N_("Unpack"), XXWIDGETS_ICON_EXTRACT, PAGE(XXWIDGETS_FORMAT_PAGE_ARCHIVE), 0, 0, 0, 0},
    {XXW_N_("Import"), XXWIDGETS_ICON_IMPORT, PAGE(XXWIDGETS_FORMAT_PAGE_ELF) | PAGE(XXWIDGETS_FORMAT_PAGE_MACH), 0, 0, 0, 0},
    {XXW_N_("Export"), XXWIDGETS_ICON_EXPORT, PAGE(XXWIDGETS_FORMAT_PAGE_ELF) | PAGE(XXWIDGETS_FORMAT_PAGE_MACH), 0, 0, 0, 0},
    {XXW_N_("Symbols"), XXWIDGETS_ICON_SYMBOL, PAGE(XXWIDGETS_FORMAT_PAGE_PE) | PAGE(XXWIDGETS_FORMAT_PAGE_ELF) |
                                               PAGE(XXWIDGETS_FORMAT_PAGE_MACH), 0, 0, 0, 0}
};

static const char *const default_main[XXWIDGETS_FORMAT_PAGE_COUNT] = {
    XXW_N_("Binary"), "", NULL, XXW_N_("MSDOS"), XXW_N_("NE"), XXW_N_("LE"), XXW_N_("DEX"),
    XXW_N_("PE"), XXW_N_("ELF"), XXW_N_("Mach-O")};

struct xxwidgets_format_panel {
    xxwidgets_widget *owner, *frame, *buttons[XXWIDGETS_FORMAT_ITEM_COUNT];
    xxwidgets_value_field *fields[XXWIDGETS_FORMAT_ITEM_COUNT];
    int shown[XXWIDGETS_FORMAT_ITEM_COUNT], enabled[XXWIDGETS_FORMAT_ITEM_COUNT];
    int visible, enabled_all;
    xxwidgets_format_page page;
    char *main_label;
    xxwidgets_rect bounds;
    int has_bounds;
};

static int member(const xxwidgets_format_panel *panel, xxwidgets_format_item item)
{
    return (specs[item].page_mask & PAGE(panel->page)) != 0 && panel->shown[item];
}

static const char *main_text(const xxwidgets_format_panel *panel)
{
    return panel->main_label ? panel->main_label : default_main[panel->page];
}

static xxwidgets_status apply_item(xxwidgets_format_panel *panel, xxwidgets_format_item item)
{
    int show = panel->visible && member(panel, item) && !(item == XXWIDGETS_FORMAT_ITEM_MAIN && panel->page == XXWIDGETS_FORMAT_PAGE_COM);
    int enable = panel->enabled_all && panel->enabled[item];
    if (panel->fields[item]) {
        xxwidgets_status status = xxwidgets_value_field_set_visible(panel->fields[item], show);
        return status == XXWIDGETS_OK ? xxwidgets_value_field_set_enabled(panel->fields[item], enable) : status;
    }
    if (panel->buttons[item]) {
        xxwidgets_status status = xxwidgets_widget_set_visible(panel->buttons[item], show);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(panel->buttons[item], enable);
        return status;
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status apply_all(xxwidgets_format_panel *panel)
{
    xxwidgets_status status = panel->frame ? xxwidgets_widget_set_visible(panel->frame, panel->visible) : XXWIDGETS_OK;
    int i;
    for (i = 0; status == XXWIDGETS_OK && i < XXWIDGETS_FORMAT_ITEM_COUNT; ++i)
        status = apply_item(panel, (xxwidgets_format_item)i);
    return status;
}

xxwidgets_status xxwidgets_format_panel_create(xxwidgets_widget *owner, unsigned int flags, xxwidgets_format_panel **out)
{
    static const xxwidgets_rect start = {0, 0, 1, 1};
    xxwidgets_format_panel *panel;
    xxwidgets_status status = XXWIDGETS_OK;
    int i;
    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || flags || xxwidgets_component_busy(owner->app)) return XXWIDGETS_INVALID_ARGUMENT;
    panel = (xxwidgets_format_panel *)calloc(1, sizeof(*panel));
    if (!panel) return XXWIDGETS_OUT_OF_MEMORY;
    panel->owner = owner; panel->visible = panel->enabled_all = 1; panel->page = XXWIDGETS_FORMAT_PAGE_BINARY;
    if (xxwidgets_app_supports_kind(owner->app, XXWIDGETS_GROUP))
        status = xxwidgets_widget_create(owner->app, owner, XXWIDGETS_GROUP, "", start, &panel->frame);
    for (i = 0; status == XXWIDGETS_OK && i < XXWIDGETS_FORMAT_ITEM_COUNT; ++i) {
        panel->shown[i] = specs[i].default_shown; panel->enabled[i] = 1;
        if (specs[i].field) {
            unsigned int field_flags = specs[i].with_arrow ? XXWIDGETS_VALUE_FIELD_WITH_ARROW : 0;
            if (i == XXWIDGETS_FORMAT_ITEM_PE_RESOURCES_GROUP) field_flags = XXWIDGETS_VALUE_FIELD_BUTTONS;
            status = xxwidgets_value_field_create(owner, specs[i].label, field_flags, &panel->fields[i]);
        } else {
            const char *label = i == XXWIDGETS_FORMAT_ITEM_MAIN ? default_main[panel->page] : specs[i].label;
            status = xxwidgets_widget_create(owner->app, owner, XXWIDGETS_BUTTON, label ? XXW_TR(owner->app, label) : "", start, &panel->buttons[i]);
            if (status == XXWIDGETS_OK && specs[i].icon != XXWIDGETS_ICON_NONE)
                status = xxwidgets_button_set_icon_id(panel->buttons[i], specs[i].icon, 0);
            if (status == XXWIDGETS_OK && label)
                status = xxwidgets_widget_set_tooltip(panel->buttons[i], XXW_TR(owner->app,
                    i == XXWIDGETS_FORMAT_ITEM_PE_DOTNET ? XXW_N_("Not available in this version") : label));
        }
    }
    if (status == XXWIDGETS_OK) {
        xxwidgets_widget *resource_buttons[2] = {panel->buttons[XXWIDGETS_FORMAT_ITEM_PE_MANIFEST], panel->buttons[XXWIDGETS_FORMAT_ITEM_PE_VERSION]};
        status = xxwidgets_value_field_set_buttons(panel->fields[XXWIDGETS_FORMAT_ITEM_PE_RESOURCES_GROUP], resource_buttons, 2);
    }
    panel->enabled[XXWIDGETS_FORMAT_ITEM_PE_DOTNET] = 0;
    if (status == XXWIDGETS_OK) status = apply_all(panel);
    if (status != XXWIDGETS_OK) { (void)xxwidgets_format_panel_destroy(panel); return status; }
    *out = panel;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_format_panel_destroy(xxwidgets_format_panel *panel)
{
    int i;
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    if (xxwidgets_component_busy(panel->owner->app)) return XXWIDGETS_BUSY;
    /* Resource buttons are borrowed by the field, and are still destroyed here. */
    for (i = XXWIDGETS_FORMAT_ITEM_COUNT; i-- > 0;) {
        if (panel->fields[i]) (void)xxwidgets_value_field_destroy(panel->fields[i]);
        if (panel->buttons[i]) (void)xxwidgets_widget_destroy(panel->buttons[i]);
    }
    if (panel->frame) (void)xxwidgets_widget_destroy(panel->frame);
    free(panel->main_label); free(panel);
    return XXWIDGETS_OK;
}

static xxwidgets_status item_width(xxwidgets_format_panel *panel, int item, int *width)
{
    const char *sample = "0000";
    int height;
    if (item == XXWIDGETS_FORMAT_ITEM_PE_TIME_DATE_STAMP) sample = "0000-00-00 00:00:00";
    else if (item == XXWIDGETS_FORMAT_ITEM_PE_SIZE_OF_IMAGE) sample = "00000000";
    else if (item == XXWIDGETS_FORMAT_ITEM_PE_RESOURCES_GROUP) sample = "";
    if (panel->fields[item]) return xxwidgets_value_field_get_preferred_size(panel->fields[item], sample, width, &height);
    return xxwidgets_widget_get_preferred_size(panel->buttons[item], width, &height);
}

xxwidgets_status xxwidgets_format_panel_get_preferred_size(xxwidgets_format_panel *panel, int *width, int *height)
{
    xxwidgets_metrics m;
    xxwidgets_status status;
    int w, h, maximum = 0, page, i;
    if (!panel || !width || !height) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_app_get_metrics(panel->owner->app, &m);
    for (page = 0; status == XXWIDGETS_OK && page < XXWIDGETS_FORMAT_PAGE_COUNT; ++page) {
        int row1 = 0, row2 = 0;
        for (i = 0; status == XXWIDGETS_OK && i < XXWIDGETS_FORMAT_ITEM_COUNT; ++i) {
            int include = (specs[i].page_mask & PAGE(page)) != 0;
            if (i >= XXWIDGETS_FORMAT_ITEM_ARCHIVE_MANIFEST_MF && i <= XXWIDGETS_FORMAT_ITEM_ARCHIVE_UNPACK) include = 0;
            if (!include || i == XXWIDGETS_FORMAT_ITEM_PE_MANIFEST || i == XXWIDGETS_FORMAT_ITEM_PE_VERSION) continue;
            status = item_width(panel, i, &w);
            if (status != XXWIDGETS_OK) break;
            if (specs[i].row2) row2 += w + (row2 ? m.spacing : 0);
            else row1 += w + (row1 ? m.spacing : 0);
        }
        if (row1 > maximum) maximum = row1;
        if (row2 > maximum) maximum = row2;
    }
    if (status != XXWIDGETS_OK) return status;
    h = xxwidgets_layout_caption_box_height(&m, m.control_height);
    *width = maximum + 2 * m.panel_inset;
    *height = 2 * m.panel_inset + m.control_height + m.spacing + h;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_format_panel_get_minimum_size(xxwidgets_format_panel *panel, int *width, int *height)
{
    xxwidgets_metrics m;
    xxwidgets_status status;
    int page, i, maximum = 0;
    if (!panel || !width || !height) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_app_get_metrics(panel->owner->app, &m);
    if (status != XXWIDGETS_OK) return status;
    for (page = 0; page < XXWIDGETS_FORMAT_PAGE_COUNT; ++page) {
        int row1 = 0, row2 = 0;
        for (i = 0; i < XXWIDGETS_FORMAT_ITEM_COUNT; ++i) {
            int include = (specs[i].page_mask & PAGE(page)) != 0;
            if (i >= XXWIDGETS_FORMAT_ITEM_ARCHIVE_MANIFEST_MF && i <= XXWIDGETS_FORMAT_ITEM_ARCHIVE_UNPACK) include = 0;
            if (!include || i == XXWIDGETS_FORMAT_ITEM_PE_MANIFEST || i == XXWIDGETS_FORMAT_ITEM_PE_VERSION) continue;
            if (specs[i].row2) ++row2; else ++row1;
        }
        row1 = row1 ? row1 * m.control_height + (row1 - 1) * m.spacing : 0;
        row2 = row2 ? row2 * 2 * m.control_height + (row2 - 1) * m.spacing : 0;
        if (row1 > maximum) maximum = row1;
        if (row2 > maximum) maximum = row2;
    }
    *width = maximum + 2 * m.panel_inset;
    *height = 2 * m.panel_inset + m.control_height + m.spacing + xxwidgets_layout_caption_box_height(&m, m.control_height);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_format_panel_set_bounds(xxwidgets_format_panel *panel, xxwidgets_rect units)
{
    xxwidgets_metrics m;
    xxwidgets_status status;
    xxwidgets_rect inner;
    int left, right, row_left, field_y, field_h, i, main_width = 0, row_total = 0, row_available = 0;
    if (!panel || units.x < 0 || units.y < 0 || units.width < 1 || units.height < 1) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_app_get_metrics(panel->owner->app, &m);
    if (status == XXWIDGETS_OK && panel->frame) status = xxwidgets_layout_place(panel->frame, units);
    inner = xxwidgets_layout_rect(units.x + m.panel_inset, units.y + m.panel_inset,
                                  units.width - 2 * m.panel_inset, units.height - 2 * m.panel_inset);
    left = inner.x; right = inner.x + inner.width;
    /* Row 1: main at left, all other members packed from the right in descriptor order. */
    if (status == XXWIDGETS_OK && member(panel, XXWIDGETS_FORMAT_ITEM_MAIN) && panel->page != XXWIDGETS_FORMAT_PAGE_COM) {
        int w;
        status = item_width(panel, XXWIDGETS_FORMAT_ITEM_MAIN, &w);
        main_width = w;
        if (status == XXWIDGETS_OK) status = xxwidgets_layout_place(panel->buttons[XXWIDGETS_FORMAT_ITEM_MAIN],
            xxwidgets_layout_rect(left, inner.y, w, m.control_height));
    }
    for (i = 1; status == XXWIDGETS_OK && i < XXWIDGETS_FORMAT_ITEM_COUNT; ++i) {
        int w;
        if (specs[i].row2 || !member(panel, (xxwidgets_format_item)i)) continue;
        status = item_width(panel, i, &w);
        if (status == XXWIDGETS_OK) row_total += w + (row_total ? m.spacing : 0);
    }
    row_available = inner.width - main_width - (main_width && row_total ? m.spacing : 0);
    if (row_available < 1) row_available = 1;
    row_left = left + main_width + (main_width && row_total ? m.spacing : 0);
    for (i = XXWIDGETS_FORMAT_ITEM_COUNT; status == XXWIDGETS_OK && i-- > 1;) {
        int w;
        if (specs[i].row2 || !member(panel, (xxwidgets_format_item)i)) continue;
        status = item_width(panel, i, &w);
        if (row_total > row_available) w = xxwidgets_component_max(m.control_height, (int)((long long)w * row_available / row_total));
        if (w > right - row_left) w = xxwidgets_component_max(1, right - row_left);
        right -= w;
        if (status == XXWIDGETS_OK) status = xxwidgets_layout_place(panel->buttons[i], xxwidgets_layout_rect(right, inner.y, w, m.control_height));
        right -= m.spacing;
    }
    field_y = inner.y + m.control_height + m.spacing;
    field_h = xxwidgets_layout_caption_box_height(&m, m.control_height);
    left = inner.x; right = inner.x + inner.width; row_total = 0; row_available = inner.width;
    for (i = 1; status == XXWIDGETS_OK && i < XXWIDGETS_FORMAT_ITEM_COUNT; ++i) {
        int w;
        if (!specs[i].field || !member(panel, (xxwidgets_format_item)i)) continue;
        status = item_width(panel, i, &w);
        if (status == XXWIDGETS_OK) row_total += w + (row_total ? m.spacing : 0);
    }
    /* PE Resources is the right group; other value fields run left to right. */
    if (status == XXWIDGETS_OK && member(panel, XXWIDGETS_FORMAT_ITEM_PE_RESOURCES_GROUP)) {
        int w;
        status = item_width(panel, XXWIDGETS_FORMAT_ITEM_PE_RESOURCES_GROUP, &w);
        if (row_total > row_available) w = xxwidgets_component_max(2 * m.control_height, (int)((long long)w * row_available / row_total));
        right -= w;
        if (status == XXWIDGETS_OK) status = xxwidgets_value_field_set_bounds(panel->fields[XXWIDGETS_FORMAT_ITEM_PE_RESOURCES_GROUP],
            xxwidgets_layout_rect(right, field_y, w, field_h));
        right -= m.spacing;
    }
    for (i = 1; status == XXWIDGETS_OK && i < XXWIDGETS_FORMAT_ITEM_COUNT; ++i) {
        int w;
        if (!specs[i].field || i == XXWIDGETS_FORMAT_ITEM_PE_RESOURCES_GROUP || !member(panel, (xxwidgets_format_item)i)) continue;
        status = item_width(panel, i, &w);
        if (row_total > row_available) w = xxwidgets_component_max(2 * m.control_height, (int)((long long)w * row_available / row_total));
        if (left + w > right) w = right - left;
        if (w < 1) w = 1;
        if (status == XXWIDGETS_OK) status = xxwidgets_value_field_set_bounds(panel->fields[i], xxwidgets_layout_rect(left, field_y, w, field_h));
        left += w + m.spacing;
    }
    if (status == XXWIDGETS_OK) { panel->bounds = units; panel->has_bounds = 1; }
    return status;
}

xxwidgets_status xxwidgets_format_panel_set_visible(xxwidgets_format_panel *panel, int visible)
{
    if (!panel || (visible != 0 && visible != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    panel->visible = visible;
    return apply_all(panel);
}

xxwidgets_status xxwidgets_format_panel_set_enabled_all(xxwidgets_format_panel *panel, int enabled)
{
    if (!panel || (enabled != 0 && enabled != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    panel->enabled_all = enabled;
    return apply_all(panel);
}

xxwidgets_status xxwidgets_format_panel_set_page(xxwidgets_format_panel *panel, xxwidgets_format_page page, const char *main_label)
{
    char *copy = NULL;
    xxwidgets_status status;
    if (!panel || page < 0 || page >= XXWIDGETS_FORMAT_PAGE_COUNT || (main_label && !xxwidgets_valid_utf8(main_label)))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (main_label) { copy = xxwidgets_strdup(main_label); if (!copy) return XXWIDGETS_OUT_OF_MEMORY; }
    free(panel->main_label); panel->main_label = copy; panel->page = page;
    status = xxwidgets_widget_set_text(panel->buttons[XXWIDGETS_FORMAT_ITEM_MAIN], main_text(panel) ?
                                       (panel->main_label ? panel->main_label : XXW_TR(panel->owner->app, main_text(panel))) : "");
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_set_tooltip(panel->buttons[XXWIDGETS_FORMAT_ITEM_MAIN], main_text(panel) ?
                                              (panel->main_label ? panel->main_label : XXW_TR(panel->owner->app, main_text(panel))) : "");
    if (status == XXWIDGETS_OK) status = apply_all(panel);
    if (status == XXWIDGETS_OK && panel->has_bounds) {
        int width, height;
        status = xxwidgets_format_panel_get_minimum_size(panel, &width, &height);
        if (status == XXWIDGETS_OK && panel->bounds.width >= width && panel->bounds.height >= height)
            status = xxwidgets_format_panel_set_bounds(panel, panel->bounds);
    }
    return status;
}

xxwidgets_format_page xxwidgets_format_panel_get_page(const xxwidgets_format_panel *panel)
{
    return panel ? panel->page : XXWIDGETS_FORMAT_PAGE_BINARY;
}

xxwidgets_status xxwidgets_format_panel_set_item_enabled(xxwidgets_format_panel *panel, xxwidgets_format_item item, int enabled)
{
    if (!panel || item < 0 || item >= XXWIDGETS_FORMAT_ITEM_COUNT || (enabled != 0 && enabled != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    panel->enabled[item] = enabled;
    return apply_item(panel, item);
}

xxwidgets_status xxwidgets_format_panel_set_item_shown(xxwidgets_format_panel *panel, xxwidgets_format_item item, int shown)
{
    xxwidgets_status status;
    if (!panel || item < 0 || item >= XXWIDGETS_FORMAT_ITEM_COUNT || item == XXWIDGETS_FORMAT_ITEM_MAIN || (shown != 0 && shown != 1))
        return XXWIDGETS_INVALID_ARGUMENT;
    panel->shown[item] = shown;
    status = apply_item(panel, item);
    if (status == XXWIDGETS_OK && panel->has_bounds) status = xxwidgets_format_panel_set_bounds(panel, panel->bounds);
    return status;
}

xxwidgets_status xxwidgets_format_panel_set_item_hex(xxwidgets_format_panel *panel, xxwidgets_format_item item, uint64_t value, unsigned int digits)
{
    if (!panel || item < 0 || item >= XXWIDGETS_FORMAT_ITEM_COUNT || !panel->fields[item]) return XXWIDGETS_INVALID_ARGUMENT;
    return xxwidgets_value_field_set_hex(panel->fields[item], value, digits);
}

xxwidgets_status xxwidgets_format_panel_set_item_text(xxwidgets_format_panel *panel, xxwidgets_format_item item, const char *utf8)
{
    if (!panel || item < 0 || item >= XXWIDGETS_FORMAT_ITEM_COUNT || !panel->fields[item]) return XXWIDGETS_INVALID_ARGUMENT;
    return xxwidgets_value_field_set_text(panel->fields[item], utf8 ? utf8 : "");
}

xxwidgets_status xxwidgets_format_panel_set_item_caption(xxwidgets_format_panel *panel, xxwidgets_format_item item, const char *caption)
{
    if (!panel || item < 0 || item >= XXWIDGETS_FORMAT_ITEM_COUNT || !panel->fields[item]) return XXWIDGETS_INVALID_ARGUMENT;
    return xxwidgets_value_field_set_caption(panel->fields[item], caption);
}

xxwidgets_widget *xxwidgets_format_panel_item_widget(const xxwidgets_format_panel *panel, xxwidgets_format_item item)
{
    if (!panel || item < 0 || item >= XXWIDGETS_FORMAT_ITEM_COUNT) return NULL;
    return panel->buttons[item] ? panel->buttons[item] :
        xxwidgets_value_field_control(panel->fields[item], XXWIDGETS_VALUE_FIELD_VALUE);
}

xxwidgets_value_field *xxwidgets_format_panel_item_field(const xxwidgets_format_panel *panel, xxwidgets_format_item item)
{
    return panel && item >= 0 && item < XXWIDGETS_FORMAT_ITEM_COUNT ? panel->fields[item] : NULL;
}

xxwidgets_widget *xxwidgets_format_panel_frame(const xxwidgets_format_panel *panel) { return panel ? panel->frame : NULL; }

xxwidgets_status xxwidgets_format_panel_retranslate(xxwidgets_format_panel *panel)
{
    xxwidgets_status status = XXWIDGETS_OK;
    int i;
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; status == XXWIDGETS_OK && i < XXWIDGETS_FORMAT_ITEM_COUNT; ++i) {
        if (panel->fields[i]) status = xxwidgets_value_field_retranslate(panel->fields[i]);
        else if (panel->buttons[i]) {
            const char *label = i == XXWIDGETS_FORMAT_ITEM_MAIN ? main_text(panel) : specs[i].label;
            status = xxwidgets_widget_set_text(panel->buttons[i], label ? ((i == XXWIDGETS_FORMAT_ITEM_MAIN && panel->main_label) ? label :
                                               XXW_TR(panel->owner->app, label)) : "");
            if (status == XXWIDGETS_OK && label) status = xxwidgets_widget_set_tooltip(panel->buttons[i],
                i == XXWIDGETS_FORMAT_ITEM_PE_DOTNET ? XXW_TR(panel->owner->app, XXW_N_("Not available in this version")) :
                (i == XXWIDGETS_FORMAT_ITEM_MAIN && panel->main_label) ? label : XXW_TR(panel->owner->app, label));
        }
    }
    if (status == XXWIDGETS_OK && panel->has_bounds) {
        int width, height;
        status = xxwidgets_format_panel_get_minimum_size(panel, &width, &height);
        if (status == XXWIDGETS_OK && panel->bounds.width >= width && panel->bounds.height >= height)
            status = xxwidgets_format_panel_set_bounds(panel, panel->bounds);
    }
    return status;
}

int xxwidgets_format_panel_handle_event(xxwidgets_format_panel *panel, const xxwidgets_event *event, xxwidgets_format_item *item, int *is_arrow)
{
    int i;
    if (!panel || !event || !event->widget) return 0;
    for (i = 0; i < XXWIDGETS_FORMAT_ITEM_COUNT; ++i) {
        xxwidgets_value_field_control_id part;
        if (panel->buttons[i] == event->widget) {
            if (item) *item = (xxwidgets_format_item)i;
            if (is_arrow) *is_arrow = 0;
            return 1;
        }
        if (panel->fields[i] && xxwidgets_value_field_handle_event(panel->fields[i], event, &part)) {
            if (item) *item = (xxwidgets_format_item)i;
            if (is_arrow) *is_arrow = part == XXWIDGETS_VALUE_FIELD_ARROW;
            return 1;
        }
    }
    if (panel->frame == event->widget) { if (item) *item = XXWIDGETS_FORMAT_ITEM_COUNT; if (is_arrow) *is_arrow = 0; return 1; }
    return 0;
}
