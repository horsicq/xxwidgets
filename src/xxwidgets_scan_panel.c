#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_scan_panel.h"
#include "xxwidgets/xxwidgets_combobox.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

struct xxwidgets_scan_panel {
    xxwidgets_widget *owner;
    xxwidgets_widget *controls[XXWIDGETS_SCAN_PANEL_CONTROL_COUNT];
    char *report;
    size_t count;
};

static int panel_rect(xxwidgets_rect bounds)
{
    return bounds.x >= 0 && bounds.y >= 0 && bounds.width >= 44 && bounds.height >= 11 &&
        bounds.x <= INT_MAX - bounds.width && bounds.y <= INT_MAX - bounds.height;
}

static void control_rects(xxwidgets_rect bounds, xxwidgets_rect *rects)
{
    int first = (bounds.width - 19) / 2, second = bounds.width - 19 - first;
    rects[XXWIDGETS_SCAN_PANEL_FILE_TYPE_LABEL] = (xxwidgets_rect){bounds.x, bounds.y, bounds.width, 1};
    rects[XXWIDGETS_SCAN_PANEL_FILE_TYPE] = (xxwidgets_rect){bounds.x, bounds.y + 1, bounds.width, 2};
    rects[XXWIDGETS_SCAN_PANEL_FLAGS_LABEL] = (xxwidgets_rect){bounds.x, bounds.y + 3, first, 1};
    rects[XXWIDGETS_SCAN_PANEL_DATABASES_LABEL] = (xxwidgets_rect){bounds.x + first + 1, bounds.y + 3, second, 1};
    rects[XXWIDGETS_SCAN_PANEL_FLAGS] = (xxwidgets_rect){bounds.x, bounds.y + 4, first, 2};
    rects[XXWIDGETS_SCAN_PANEL_DATABASES] = (xxwidgets_rect){bounds.x + first + 1, bounds.y + 4, second, 2};
    rects[XXWIDGETS_SCAN_PANEL_SCAN] = (xxwidgets_rect){bounds.x + bounds.width - 17, bounds.y + 4, 8, 2};
    rects[XXWIDGETS_SCAN_PANEL_REPORT] = (xxwidgets_rect){bounds.x + bounds.width - 8, bounds.y + 4, 8, 2};
    rects[XXWIDGETS_SCAN_PANEL_RESULTS] = (xxwidgets_rect){bounds.x, bounds.y + 7, bounds.width, bounds.height - 7};
}

xxwidgets_widget *xxwidgets_scan_panel_control(const xxwidgets_scan_panel *panel,
    xxwidgets_scan_panel_control_id control)
{
    return panel && control >= 0 && control < XXWIDGETS_SCAN_PANEL_CONTROL_COUNT
        ? panel->controls[control] : NULL;
}

static xxwidgets_status fill_choices(xxwidgets_widget *widget, const wchar_t *const *names,
    size_t count)
{
    xx_meta_string records[6] = {0};
    xx_str_w_s labels[6] = {0};
    size_t i;
    for (i = 0; i < count; ++i) {
        labels[i].data = (wchar_t *)names[i];
        labels[i].length = wcslen(names[i]);
        labels[i].capacity = labels[i].length + 1;
        labels[i].is_view = true;
        records[i].meta_string = labels + i;
        records[i].var.type = XX_VAR_TYPE_UINT64;
        records[i].var.val.u64 = UINT64_C(1) << i;
    }
    return xxwidgets_combobox_set_records(widget, records, count);
}

static xxwidgets_status get_checks(const xxwidgets_widget *widget, unsigned int *mask, size_t count)
{
    unsigned int value = 0;
    size_t i;
    if (!widget || !mask || xxwidgets_combobox_count(widget) != count)
        return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < count; ++i) {
        int checked;
        xxwidgets_status status = xxwidgets_checkcombobox_is_checked(widget, i, &checked);
        if (status != XXWIDGETS_OK) return status;
        if (checked) value |= 1u << i;
    }
    *mask = value;
    return XXWIDGETS_OK;
}

static xxwidgets_status set_checks(xxwidgets_widget *widget, unsigned int mask, size_t count)
{
    return xxwidgets_checkcombobox_set_mask(widget, mask, count);
}

/* Logical rollback is unconditional even if the native recovery also fails.
 * All setters suppress notifications during this best-effort recovery. */
static void recover_control(xxwidgets_widget *widget)
{
    ++widget->app->syncing;
    widget->app->ops->sync(widget);
    --widget->app->syncing;
}

static void recover_enabled(xxwidgets_widget *widget, int enabled)
{
    widget->enabled = enabled;
    recover_control(widget);
}

xxwidgets_status xxwidgets_scan_panel_set_flags(xxwidgets_scan_panel *panel, unsigned int flags)
{
    return panel ? set_checks(panel->controls[XXWIDGETS_SCAN_PANEL_FLAGS], flags, 6)
        : XXWIDGETS_INVALID_ARGUMENT;
}
xxwidgets_status xxwidgets_scan_panel_get_flags(const xxwidgets_scan_panel *panel, unsigned int *flags)
{
    return panel ? get_checks(panel->controls[XXWIDGETS_SCAN_PANEL_FLAGS], flags, 6)
        : XXWIDGETS_INVALID_ARGUMENT;
}
xxwidgets_status xxwidgets_scan_panel_set_databases(xxwidgets_scan_panel *panel, unsigned int databases)
{
    return panel ? set_checks(panel->controls[XXWIDGETS_SCAN_PANEL_DATABASES], databases, 2)
        : XXWIDGETS_INVALID_ARGUMENT;
}
xxwidgets_status xxwidgets_scan_panel_get_databases(const xxwidgets_scan_panel *panel, unsigned int *databases)
{
    return panel ? get_checks(panel->controls[XXWIDGETS_SCAN_PANEL_DATABASES], databases, 2)
        : XXWIDGETS_INVALID_ARGUMENT;
}

xxwidgets_status xxwidgets_scan_panel_create(xxwidgets_widget *owner, xxwidgets_rect bounds,
    xxwidgets_scan_panel **out_panel)
{
    static const xxwidgets_kind kinds[XXWIDGETS_SCAN_PANEL_CONTROL_COUNT] = {
        XXWIDGETS_CHECKCOMBOBOX, XXWIDGETS_CHECKCOMBOBOX, XXWIDGETS_BUTTON,
        XXWIDGETS_BUTTON, XXWIDGETS_TREEVIEW, XXWIDGETS_LABEL, XXWIDGETS_LABEL,
        XXWIDGETS_COMBOBOX, XXWIDGETS_LABEL
    };
    static const char *const names[XXWIDGETS_SCAN_PANEL_CONTROL_COUNT] = {
        "No flags", "Main database only", "Scan", "Report", "Scan results", "Flags", "Databases",
        "Automatic", "File type"
    };
    static const xxwidgets_scan_panel_control_id creation_order[XXWIDGETS_SCAN_PANEL_CONTROL_COUNT] = {
        XXWIDGETS_SCAN_PANEL_FILE_TYPE_LABEL, XXWIDGETS_SCAN_PANEL_FILE_TYPE,
        XXWIDGETS_SCAN_PANEL_FLAGS_LABEL, XXWIDGETS_SCAN_PANEL_FLAGS,
        XXWIDGETS_SCAN_PANEL_DATABASES_LABEL, XXWIDGETS_SCAN_PANEL_DATABASES,
        XXWIDGETS_SCAN_PANEL_SCAN, XXWIDGETS_SCAN_PANEL_REPORT, XXWIDGETS_SCAN_PANEL_RESULTS
    };
    static const wchar_t *const flag_names[6] = {
        L"Deep scan", L"Heuristic scan", L"Verbose results", L"Aggressive scan",
        L"Hide unknown results", L"Format text report"
    };
    static const wchar_t *const database_names[2] = {L"Extra database", L"Custom database"};
    xxwidgets_scan_panel *panel;
    xxwidgets_rect rects[XXWIDGETS_SCAN_PANEL_CONTROL_COUNT];
    xxwidgets_status status;
    size_t i;
    if (!out_panel) return XXWIDGETS_INVALID_ARGUMENT;
    *out_panel = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !panel_rect(bounds))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing)
        return XXWIDGETS_BUSY;
    panel = (xxwidgets_scan_panel *)calloc(1, sizeof(*panel));
    if (!panel) return XXWIDGETS_OUT_OF_MEMORY;
    panel->owner = owner;
    control_rects(bounds, rects);
    for (i = 0; i < XXWIDGETS_SCAN_PANEL_CONTROL_COUNT; ++i) {
        xxwidgets_scan_panel_control_id control = creation_order[i];
        status = xxwidgets_widget_create(owner->app, owner, kinds[control], names[control],
            rects[control], &panel->controls[control]);
        if (status != XXWIDGETS_OK) goto failed;
    }
    {
        xx_str_w_s label = {0};
        xx_meta_string record = {0};
        label.data = L"Automatic";
        label.length = 9;
        label.capacity = 10;
        label.is_view = true;
        record.meta_string = &label;
        record.var.type = XX_VAR_TYPE_UINT64;
        record.var.val.u64 = 0;
        status = xxwidgets_combobox_set_records(panel->controls[XXWIDGETS_SCAN_PANEL_FILE_TYPE], &record, 1);
        if (status != XXWIDGETS_OK) goto failed;
    }
    status = fill_choices(panel->controls[XXWIDGETS_SCAN_PANEL_FLAGS], flag_names, 6);
    if (status != XXWIDGETS_OK) goto failed;
    status = fill_choices(panel->controls[XXWIDGETS_SCAN_PANEL_DATABASES], database_names, 2);
    if (status != XXWIDGETS_OK) goto failed;
    status = xxwidgets_scan_panel_set_flags(panel, XXWIDGETS_SCAN_DEEP | XXWIDGETS_SCAN_HEURISTIC | XXWIDGETS_SCAN_VERBOSE);
    if (status != XXWIDGETS_OK) goto failed;
    status = xxwidgets_scan_panel_set_databases(panel, XXWIDGETS_SCAN_DATABASE_EXTRA | XXWIDGETS_SCAN_DATABASE_CUSTOM);
    if (status != XXWIDGETS_OK) goto failed;
    status = xxwidgets_widget_set_enabled(panel->controls[XXWIDGETS_SCAN_PANEL_REPORT], 0);
    if (status != XXWIDGETS_OK) goto failed;
    *out_panel = panel;
    return XXWIDGETS_OK;
failed:
    xxwidgets_scan_panel_destroy(panel);
    return status;
}

xxwidgets_status xxwidgets_scan_panel_destroy(xxwidgets_scan_panel *panel)
{
    size_t i;
    xxwidgets_status status;
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    if (panel->owner->app->dispatch_depth || panel->owner->app->polling || panel->owner->app->syncing)
        return XXWIDGETS_BUSY;
    for (i = 0; i < XXWIDGETS_SCAN_PANEL_CONTROL_COUNT; ++i) {
        if (!panel->controls[i]) continue;
        status = xxwidgets_widget_destroy(panel->controls[i]);
        if (status != XXWIDGETS_OK) return status;
        panel->controls[i] = NULL;
    }
    free(panel->report);
    free(panel);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_scan_panel_set_rect(xxwidgets_scan_panel *panel, xxwidgets_rect bounds)
{
    xxwidgets_rect rects[XXWIDGETS_SCAN_PANEL_CONTROL_COUNT], previous[XXWIDGETS_SCAN_PANEL_CONTROL_COUNT];
    xxwidgets_status status;
    size_t i;
    if (!panel || !panel_rect(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    control_rects(bounds, rects);
    for (i = 0; i < XXWIDGETS_SCAN_PANEL_CONTROL_COUNT; ++i)
        previous[i] = panel->controls[i]->rect;
    for (i = 0; i < XXWIDGETS_SCAN_PANEL_CONTROL_COUNT; ++i) {
        status = xxwidgets_widget_set_rect(panel->controls[i], rects[i]);
        if (status != XXWIDGETS_OK) {
            size_t j;
            for (j = 0; j < i; ++j) panel->controls[j]->rect = previous[j];
            for (j = 0; j < i; ++j) recover_control(panel->controls[j]);
            return status;
        }
    }
    return XXWIDGETS_OK;
}

static char *join_text(const char *const *parts, size_t count)
{
    size_t i, size = 0, used = 0;
    char *text;
    for (i = 0; i < count; ++i) {
        size_t length = strlen(parts[i]);
        if (length >= SIZE_MAX - size) return NULL;
        size += length;
    }
    text = (char *)malloc(size + 1);
    if (!text) return NULL;
    for (i = 0; i < count; ++i) {
        size_t length = strlen(parts[i]);
        memcpy(text + used, parts[i], length); used += length;
    }
    text[used] = 0;
    return text;
}

static void free_nodes(xxwidgets_tree_node *nodes, size_t count)
{
    size_t i;
    if (nodes) for (i = 0; i < count; ++i) free((void *)nodes[i].text);
    free(nodes);
}

xxwidgets_status xxwidgets_scan_panel_set_results(xxwidgets_scan_panel *panel,
    const char *file_label, const xxwidgets_scan_result *results, size_t count, const char *report)
{
    static const char *const field_names[4] = {"Type: ", "Name: ", "Version: ", "Info: "};
    xxwidgets_tree_node *nodes;
    char *new_report;
    size_t i, j, node_count;
    int previous_enabled;
    xxwidgets_status status;
    if (!file_label) file_label = "";
    if (!report) report = "";
    if (!panel || (!results && count) || count > (INT_MAX - 1u) / 5u ||
        !xxwidgets_valid_utf8(file_label) || !xxwidgets_valid_utf8(report))
        return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < count; ++i) {
        const char *fields[4] = {results[i].type, results[i].name, results[i].version, results[i].info};
        for (j = 0; j < 4; ++j)
            if (fields[j] && !xxwidgets_valid_utf8(fields[j])) return XXWIDGETS_INVALID_ARGUMENT;
    }
    node_count = count ? 1 + count * 5 : 2;
    if (node_count > SIZE_MAX / sizeof(*nodes)) return XXWIDGETS_INVALID_ARGUMENT;
    new_report = xxwidgets_strdup(report);
    nodes = (xxwidgets_tree_node *)calloc(node_count, sizeof(*nodes));
    if (!new_report || !nodes) { free(new_report); free(nodes); return XXWIDGETS_OUT_OF_MEMORY; }
    {
        const char *parts[2] = {file_label[0] ? "File: " : "", file_label[0] ? file_label : "Scan results"};
        nodes[0].text = join_text(parts, 2);
        nodes[0].parent = SIZE_MAX; nodes[0].expanded = 1;
    }
    if (!count) {
        nodes[1].text = xxwidgets_strdup("No detections");
        nodes[1].parent = 0;
    }
    for (i = 0; i < count; ++i) {
        size_t detection = 1 + i * 5;
        const char *fields[4] = {results[i].type ? results[i].type : "",
            results[i].name ? results[i].name : "", results[i].version ? results[i].version : "",
            results[i].info ? results[i].info : ""};
        const char *parts[6] = {fields[0][0] ? fields[0] : "Detection", ": ",
            fields[1][0] ? fields[1] : "Unknown", fields[2][0] ? " [" : "", fields[2], fields[2][0] ? "]" : ""};
        nodes[detection].text = join_text(parts, 6);
        nodes[detection].parent = 0; nodes[detection].expanded = 1;
        for (j = 0; j < 4; ++j) {
            const char *detail[2] = {field_names[j], fields[j]};
            nodes[detection + 1 + j].text = join_text(detail, 2);
            nodes[detection + 1 + j].parent = detection;
        }
    }
    for (i = 0; i < node_count; ++i)
        if (!nodes[i].text) { free_nodes(nodes, node_count); free(new_report); return XXWIDGETS_OUT_OF_MEMORY; }
    previous_enabled = panel->controls[XXWIDGETS_SCAN_PANEL_REPORT]->enabled;
    status = xxwidgets_widget_set_enabled(panel->controls[XXWIDGETS_SCAN_PANEL_REPORT], report[0] != 0);
    if (status == XXWIDGETS_OK) {
        status = xxwidgets_treeview_set_nodes(panel->controls[XXWIDGETS_SCAN_PANEL_RESULTS], nodes, node_count);
        if (status != XXWIDGETS_OK)
            recover_enabled(panel->controls[XXWIDGETS_SCAN_PANEL_REPORT], previous_enabled);
    }
    free_nodes(nodes, node_count);
    if (status != XXWIDGETS_OK) { free(new_report); return status; }
    free(panel->report); panel->report = new_report; panel->count = count;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_scan_panel_clear(xxwidgets_scan_panel *panel)
{
    xxwidgets_status status;
    int previous_enabled;
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    previous_enabled = panel->controls[XXWIDGETS_SCAN_PANEL_REPORT]->enabled;
    status = xxwidgets_widget_set_enabled(panel->controls[XXWIDGETS_SCAN_PANEL_REPORT], 0);
    if (status != XXWIDGETS_OK) return status;
    status = xxwidgets_treeview_clear(panel->controls[XXWIDGETS_SCAN_PANEL_RESULTS]);
    if (status != XXWIDGETS_OK) {
        recover_enabled(panel->controls[XXWIDGETS_SCAN_PANEL_REPORT], previous_enabled);
        return status;
    }
    free(panel->report); panel->report = NULL; panel->count = 0;
    return XXWIDGETS_OK;
}

size_t xxwidgets_scan_panel_count(const xxwidgets_scan_panel *panel)
{
    return panel ? panel->count : 0;
}

xxwidgets_status xxwidgets_scan_panel_get_report(const xxwidgets_scan_panel *panel,
    char *buffer, size_t capacity, size_t *required)
{
    const char *report;
    size_t length, copied;
    if (!panel || (!buffer && capacity)) return XXWIDGETS_INVALID_ARGUMENT;
    report = panel->report ? panel->report : "";
    length = strlen(report);
    if (required) *required = length + 1;
    if (!buffer && !capacity) return XXWIDGETS_OK;
    if (capacity) {
        copied = length < capacity - 1 ? length : capacity - 1;
        if (copied < length)
            while (copied && ((unsigned char)report[copied] & 0xc0) == 0x80) --copied;
        memcpy(buffer, report, copied); buffer[copied] = 0;
    }
    return capacity > length ? XXWIDGETS_OK : XXWIDGETS_BUFFER_TOO_SMALL;
}

xxwidgets_status xxwidgets_scan_panel_show_report(xxwidgets_scan_panel *panel)
{
    return panel ? xxwidgets_text_dialog(panel->owner, "Scan report", panel->report ? panel->report : "")
        : XXWIDGETS_INVALID_ARGUMENT;
}
