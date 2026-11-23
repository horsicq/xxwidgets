/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/* Sort panel (DIE-sort form) built from public xxwidgets controls only. */
#include "xxwidgets/xxwidgets_sort_panel.h"
#include "xxwidgets/xxwidgets_type_picker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SORT_MIN_WIDTH 100
#define SORT_ROW 2

enum {
    PENDING_NONE = 0,
    PENDING_BROWSE_DIRECTORY,
    PENDING_BROWSE_MAIN,
    PENDING_BROWSE_CUSTOM,
    PENDING_BROWSE_RESULT,
    PENDING_FILE_TYPES,
    PENDING_TYPES,
    PENDING_HELP,
    PENDING_SCAN
};

static const struct {
    xxwidgets_sort_panel_control_id id;
    unsigned int flag;
    const char *label;
} k_flags[] = {
    {XXWIDGETS_SORT_FLAG_RECURSIVE, XXWIDGETS_SORT_SF_RECURSIVE, "Recursive"},
    {XXWIDGETS_SORT_FLAG_OVERLAY, XXWIDGETS_SORT_SF_OVERLAY, "Overlay"},
    {XXWIDGETS_SORT_FLAG_RESOURCES, XXWIDGETS_SORT_SF_RESOURCES, "Resources"},
    {XXWIDGETS_SORT_FLAG_ARCHIVES, XXWIDGETS_SORT_SF_ARCHIVES, "Archives"},
    {XXWIDGETS_SORT_FLAG_DEEP, XXWIDGETS_SORT_SF_DEEP, "Deep"},
    {XXWIDGETS_SORT_FLAG_HEURISTIC, XXWIDGETS_SORT_SF_HEURISTIC, "Heuristic"},
    {XXWIDGETS_SORT_FLAG_VERBOSE, XXWIDGETS_SORT_SF_VERBOSE, "Verbose"},
    {XXWIDGETS_SORT_FLAG_ALLTYPES, XXWIDGETS_SORT_SF_ALLTYPES, "All types"},
};
#define FLAG_COUNT (sizeof(k_flags) / sizeof(k_flags[0]))

typedef struct sort_list {
    char **names;
    size_t count;
    unsigned char *checked;
} sort_list;

struct xxwidgets_sort_panel {
    xxwidgets_widget *owner;
    xxwidgets_widget *controls[XXWIDGETS_SORT_CONTROL_COUNT];
    xxwidgets_rect bounds;
    sort_list file_types;
    sort_list types;
    char *help;
    int pending;
};

static const char k_default_help[] =
    "{ft} - File type\n{arch} - Architecture\n{type} - Type\n{name} - Name\n{version} - Version\n{info} - Info\n{md5} - MD5\n"
    "{original_filename} - Original file name\n{original_filebasename} - Original file base name\n"
    "{original_fileextension} - Original file extension";

/* ------------------------------------------------------------ settings */

static char *sort_strdup(const char *text)
{
    size_t length = text ? strlen(text) : 0;
    char *copy = (char *)malloc(length + 1);
    if (!copy) return NULL;
    if (length) memcpy(copy, text, length);
    copy[length] = 0;
    return copy;
}

xxwidgets_status xxwidgets_sort_settings_set(char **field, const char *value)
{
    char *copy;
    if (!field) return XXWIDGETS_INVALID_ARGUMENT;
    copy = sort_strdup(value ? value : "");
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    free(*field);
    *field = copy;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_sort_settings_init(xxwidgets_sort_settings *settings)
{
    if (!settings) return XXWIDGETS_INVALID_ARGUMENT;
    memset(settings, 0, sizeof(*settings));
    settings->subdirectories = 1;
    settings->use_custom_database = 1;
    settings->all_file_types = 1;
    settings->all_types = 1;
    if (xxwidgets_sort_settings_set(&settings->directory, "") != XXWIDGETS_OK ||
        xxwidgets_sort_settings_set(&settings->read_buffer_size, "") != XXWIDGETS_OK ||
        xxwidgets_sort_settings_set(&settings->file_buffer_size, "") != XXWIDGETS_OK ||
        xxwidgets_sort_settings_set(&settings->main_database, "$data/db") != XXWIDGETS_OK ||
        xxwidgets_sort_settings_set(&settings->custom_database, "$data/db_custom") != XXWIDGETS_OK ||
        xxwidgets_sort_settings_set(&settings->file_types, "") != XXWIDGETS_OK ||
        xxwidgets_sort_settings_set(&settings->types, "") != XXWIDGETS_OK ||
        xxwidgets_sort_settings_set(&settings->result_directory, "collection") != XXWIDGETS_OK ||
        xxwidgets_sort_settings_set(&settings->catalog_format, "{ft}.{type}.{name}.{version}.{info}.txt") != XXWIDGETS_OK ||
        xxwidgets_sort_settings_set(&settings->copy_format, "{ft}/{type}/{name}({version})[{info}]/{md5}_{original_filename}") != XXWIDGETS_OK) {
        xxwidgets_sort_settings_free(settings);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    return XXWIDGETS_OK;
}

void xxwidgets_sort_settings_free(xxwidgets_sort_settings *settings)
{
    if (!settings) return;
    free(settings->directory);
    free(settings->read_buffer_size);
    free(settings->file_buffer_size);
    free(settings->main_database);
    free(settings->custom_database);
    free(settings->file_types);
    free(settings->types);
    free(settings->result_directory);
    free(settings->catalog_format);
    free(settings->copy_format);
    memset(settings, 0, sizeof(*settings));
}

/* ---------------------------------------------------------------- lists */

static void list_free(sort_list *list)
{
    size_t i;
    for (i = 0; i < list->count; i++) free(list->names[i]);
    free(list->names);
    free(list->checked);
    memset(list, 0, sizeof(*list));
}

static xxwidgets_status list_init(sort_list *list, const char *const *names, size_t count)
{
    size_t i;
    memset(list, 0, sizeof(*list));
    if (!count) return XXWIDGETS_OK;
    list->names = (char **)calloc(count, sizeof(char *));
    list->checked = (unsigned char *)calloc(count, 1);
    if (!list->names || !list->checked) {
        list_free(list);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    for (i = 0; i < count; i++) {
        list->names[i] = sort_strdup(names[i] ? names[i] : "");
        if (!list->names[i]) {
            list->count = i;
            list_free(list);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
    }
    list->count = count;
    return XXWIDGETS_OK;
}

static int same_name(const char *a, const char *b, size_t b_length)
{
    size_t i;
    for (i = 0; i < b_length; i++) {
        char x = a[i];
        char y = b[i];
        if (!x) return 0;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return 0;
    }
    return a[b_length] == 0;
}

/* Check the names of a '|' / ',' separated string. */
static void list_set_from_string(sort_list *list, const char *text)
{
    const char *p = text ? text : "";
    if (list->count) memset(list->checked, 0, list->count);
    while (*p) {
        const char *end = p;
        const char *a;
        const char *b;
        size_t i;
        while (*end && *end != '|' && *end != ',') end++;
        a = p;
        b = end;
        while (a < b && *a == ' ') a++;
        while (b > a && b[-1] == ' ') b--;
        for (i = 0; i < list->count; i++) {
            if (b > a && same_name(list->names[i], a, (size_t)(b - a))) list->checked[i] = 1;
        }
        p = *end ? end + 1 : end;
    }
}

static char *list_to_string(const sort_list *list)
{
    size_t i;
    size_t length = 1;
    char *text;
    for (i = 0; i < list->count; i++)
        if (list->checked[i]) length += strlen(list->names[i]) + 1;
    text = (char *)malloc(length);
    if (!text) return NULL;
    length = 0;
    for (i = 0; i < list->count; i++) {
        size_t n;
        if (!list->checked[i]) continue;
        if (length) text[length++] = '|';
        n = strlen(list->names[i]);
        memcpy(text + length, list->names[i], n);
        length += n;
    }
    text[length] = 0;
    return text;
}

static void list_summary(const sort_list *list, char *out, size_t capacity)
{
    size_t i;
    size_t checked = 0;
    size_t used;
    for (i = 0; i < list->count; i++) checked += list->checked[i] ? 1 : 0;
    if (!checked) {
        snprintf(out, capacity, "nothing selected");
        return;
    }
    snprintf(out, capacity, "%zu: ", checked);
    used = strlen(out);
    for (i = 0; i < list->count && used + 4 < capacity; i++) {
        if (!list->checked[i]) continue;
        snprintf(out + used, capacity - used, "%s%s", used > 4 && out[used - 1] != ' ' ? ", " : "", list->names[i]);
        used = strlen(out);
    }
}

/* --------------------------------------------------------------- layout */

xxwidgets_status xxwidgets_sort_panel_minimum_size(int *width, int *height)
{
    if (width) *width = SORT_MIN_WIDTH;
    if (height) *height = 13 * SORT_ROW + 1;
    return XXWIDGETS_OK;
}

static void place(xxwidgets_sort_panel *panel, xxwidgets_sort_panel_control_id id, int x, int y, int width, int height)
{
    xxwidgets_widget *widget = panel->controls[id];
    xxwidgets_rect rect;
    if (!widget) return;
    rect.x = panel->bounds.x + x;
    rect.y = panel->bounds.y + y;
    rect.width = width < 1 ? 1 : width;
    rect.height = height;
    xxwidgets_widget_set_rect(widget, rect);
}

static void layout(xxwidgets_sort_panel *panel)
{
    int w = panel->bounds.width < SORT_MIN_WIDTH ? SORT_MIN_WIDTH : panel->bounds.width;
    int h = 1; /* one text row per control, one blank row between rows */
    int y = 0;
    int label = 16;
    int browse = 8;
    int edit = w - label - browse - 2;
    int x;
    size_t i;

    place(panel, XXWIDGETS_SORT_DIRECTORY_LABEL, 0, y, label - 1, h);
    place(panel, XXWIDGETS_SORT_DIRECTORY, label, y, edit, h);
    place(panel, XXWIDGETS_SORT_DIRECTORY_BROWSE, w - browse, y, browse, h);
    y += SORT_ROW;
    place(panel, XXWIDGETS_SORT_SUBDIRECTORIES, label, y, 20, h);
    place(panel, XXWIDGETS_SORT_LOG, label + 22, y, 10, h);
    place(panel, XXWIDGETS_SORT_SCAN, w - 14, y, 14, h);
    y += SORT_ROW;
    place(panel, XXWIDGETS_SORT_FLAGS_LABEL, 0, y, label - 1, h);
    x = label;
    for (i = 0; i < FLAG_COUNT; i++) {
        int cell = (w - label) / 4;
        if (i == 4) {
            y += SORT_ROW;
            x = label;
        }
        place(panel, k_flags[i].id, x, y, cell - 1, h);
        x += cell;
    }
    y += SORT_ROW;
    place(panel, XXWIDGETS_SORT_READ_BUFFER_LABEL, 0, y, label - 1, h);
    place(panel, XXWIDGETS_SORT_READ_BUFFER, label, y, 16, h);
    place(panel, XXWIDGETS_SORT_FILE_BUFFER_LABEL, label + 18, y, 16, h);
    place(panel, XXWIDGETS_SORT_FILE_BUFFER, label + 35, y, 16, h);
    y += SORT_ROW;
    place(panel, XXWIDGETS_SORT_MAIN_DATABASE_LABEL, 0, y, label - 1, h);
    place(panel, XXWIDGETS_SORT_MAIN_DATABASE, label, y, edit, h);
    place(panel, XXWIDGETS_SORT_MAIN_DATABASE_BROWSE, w - browse, y, browse, h);
    y += SORT_ROW;
    place(panel, XXWIDGETS_SORT_CUSTOM_DATABASE_ENABLED, 0, y, label - 1, h);
    place(panel, XXWIDGETS_SORT_CUSTOM_DATABASE, label, y, edit, h);
    place(panel, XXWIDGETS_SORT_CUSTOM_DATABASE_BROWSE, w - browse, y, browse, h);
    y += SORT_ROW;
    place(panel, XXWIDGETS_SORT_ALL_FILE_TYPES, 0, y, label + 6, h);
    place(panel, XXWIDGETS_SORT_FILE_TYPES_CHOOSE, label + 8, y, 12, h);
    place(panel, XXWIDGETS_SORT_FILE_TYPES_SUMMARY, label + 22, y, w - label - 22, h);
    y += SORT_ROW;
    place(panel, XXWIDGETS_SORT_ALL_TYPES, 0, y, label + 6, h);
    place(panel, XXWIDGETS_SORT_TYPES_CHOOSE, label + 8, y, 12, h);
    place(panel, XXWIDGETS_SORT_TYPES_SUMMARY, label + 22, y, w - label - 36, h);
    place(panel, XXWIDGETS_SORT_UNKNOWN, w - 12, y, 12, h);
    y += SORT_ROW;
    place(panel, XXWIDGETS_SORT_RESULT_LABEL, 0, y, label - 1, h);
    place(panel, XXWIDGETS_SORT_RESULT, label, y, edit, h);
    place(panel, XXWIDGETS_SORT_RESULT_BROWSE, w - browse, y, browse, h);
    y += SORT_ROW;
    place(panel, XXWIDGETS_SORT_CATALOG, 0, y, label - 1, h);
    place(panel, XXWIDGETS_SORT_CATALOG_FORMAT, label, y, edit, h);
    place(panel, XXWIDGETS_SORT_CATALOG_HELP, w - browse, y, browse, h);
    y += SORT_ROW;
    place(panel, XXWIDGETS_SORT_COPY, 0, y, label - 1, h);
    place(panel, XXWIDGETS_SORT_COPY_FORMAT, label, y, edit, h);
    place(panel, XXWIDGETS_SORT_COPY_HELP, w - browse, y, browse, h);
    y += SORT_ROW;
    place(panel, XXWIDGETS_SORT_REMOVE_SOURCE, label, y, 20, h);
    place(panel, XXWIDGETS_SORT_MOVE_TO_FIRST, label + 22, y, 20, h);
}

/* ------------------------------------------------------------- controls */

static int checked(const xxwidgets_sort_panel *panel, xxwidgets_sort_panel_control_id id)
{
    int value = 0;
    if (panel->controls[id]) xxwidgets_widget_get_value(panel->controls[id], &value);
    return value != 0;
}

static void set_checked(xxwidgets_sort_panel *panel, xxwidgets_sort_panel_control_id id, int value)
{
    if (panel->controls[id]) xxwidgets_widget_set_value(panel->controls[id], value ? 1 : 0);
}

static char *text_of(const xxwidgets_sort_panel *panel, xxwidgets_sort_panel_control_id id)
{
    size_t need = 0;
    char *text;
    xxwidgets_widget *widget = panel->controls[id];
    if (!widget) return sort_strdup("");
    if (xxwidgets_widget_get_text(widget, NULL, 0, &need) != XXWIDGETS_OK && !need) return sort_strdup("");
    text = (char *)malloc(need + 1);
    if (!text) return NULL;
    if (xxwidgets_widget_get_text(widget, text, need + 1, NULL) != XXWIDGETS_OK) text[0] = 0;
    return text;
}

static void set_text(xxwidgets_sort_panel *panel, xxwidgets_sort_panel_control_id id, const char *text)
{
    if (panel->controls[id]) xxwidgets_widget_set_text(panel->controls[id], text ? text : "");
}

static int checked(const xxwidgets_sort_panel *panel, xxwidgets_sort_panel_control_id id);

static void update_summaries(xxwidgets_sort_panel *panel)
{
    char text[512];
    if (checked(panel, XXWIDGETS_SORT_ALL_FILE_TYPES)) snprintf(text, sizeof(text), "all");
    else list_summary(&panel->file_types, text, sizeof(text));
    set_text(panel, XXWIDGETS_SORT_FILE_TYPES_SUMMARY, text);
    if (checked(panel, XXWIDGETS_SORT_ALL_TYPES)) snprintf(text, sizeof(text), "all");
    else list_summary(&panel->types, text, sizeof(text));
    set_text(panel, XXWIDGETS_SORT_TYPES_SUMMARY, text);
}

/* As XScanSortWidget: the pickers follow their "All" boxes, Unknown follows All
 * types, and a checkable group's controls follow its box. */
static void update_enabled(xxwidgets_sort_panel *panel)
{
    int all_file_types = checked(panel, XXWIDGETS_SORT_ALL_FILE_TYPES);
    int all_types = checked(panel, XXWIDGETS_SORT_ALL_TYPES);
    int catalog = checked(panel, XXWIDGETS_SORT_CATALOG);
    int copy = checked(panel, XXWIDGETS_SORT_COPY);
    xxwidgets_widget_set_enabled(panel->controls[XXWIDGETS_SORT_FILE_TYPES_CHOOSE], !all_file_types);
    xxwidgets_widget_set_enabled(panel->controls[XXWIDGETS_SORT_FILE_TYPES_SUMMARY], !all_file_types);
    xxwidgets_widget_set_enabled(panel->controls[XXWIDGETS_SORT_TYPES_CHOOSE], !all_types);
    xxwidgets_widget_set_enabled(panel->controls[XXWIDGETS_SORT_TYPES_SUMMARY], !all_types);
    xxwidgets_widget_set_enabled(panel->controls[XXWIDGETS_SORT_UNKNOWN], !all_types);
    xxwidgets_widget_set_enabled(panel->controls[XXWIDGETS_SORT_CATALOG_FORMAT], catalog);
    xxwidgets_widget_set_enabled(panel->controls[XXWIDGETS_SORT_CATALOG_HELP], catalog);
    xxwidgets_widget_set_enabled(panel->controls[XXWIDGETS_SORT_COPY_FORMAT], copy);
    xxwidgets_widget_set_enabled(panel->controls[XXWIDGETS_SORT_COPY_HELP], copy);
    xxwidgets_widget_set_enabled(panel->controls[XXWIDGETS_SORT_REMOVE_SOURCE], copy);
    xxwidgets_widget_set_enabled(panel->controls[XXWIDGETS_SORT_MOVE_TO_FIRST], copy);
}

xxwidgets_status xxwidgets_sort_panel_create(xxwidgets_widget *owner, xxwidgets_rect bounds, const xxwidgets_sort_panel_config *config,
                                             xxwidgets_sort_panel **out)
{
    static const struct {
        xxwidgets_sort_panel_control_id id;
        xxwidgets_kind kind;
        const char *text;
        const char *tooltip;
    } k_controls[] = {
        {XXWIDGETS_SORT_DIRECTORY_LABEL, XXWIDGETS_LABEL, "Directory:", NULL},
        {XXWIDGETS_SORT_DIRECTORY, XXWIDGETS_EDIT, "", "The directory to sort"},
        {XXWIDGETS_SORT_DIRECTORY_BROWSE, XXWIDGETS_BUTTON, "...", "Choose the directory to scan"},
        {XXWIDGETS_SORT_SUBDIRECTORIES, XXWIDGETS_CHECKBOX, "Subdirectories", NULL},
        {XXWIDGETS_SORT_LOG, XXWIDGETS_CHECKBOX, "Log", "Write info.log: every result before and after the filter"},
        {XXWIDGETS_SORT_SCAN, XXWIDGETS_BUTTON, "Scan", "Sort the directory (F5)"},
        {XXWIDGETS_SORT_FLAGS_LABEL, XXWIDGETS_LABEL, "Flags:", NULL},
        {XXWIDGETS_SORT_READ_BUFFER_LABEL, XXWIDGETS_LABEL, "Read buffer:", NULL},
        {XXWIDGETS_SORT_READ_BUFFER, XXWIDGETS_EDIT, "", "Read buffer size in bytes; empty = default"},
        {XXWIDGETS_SORT_FILE_BUFFER_LABEL, XXWIDGETS_LABEL, "File buffer:", NULL},
        {XXWIDGETS_SORT_FILE_BUFFER, XXWIDGETS_EDIT, "", "Files smaller than this are scanned in memory; empty = default"},
        {XXWIDGETS_SORT_MAIN_DATABASE_LABEL, XXWIDGETS_LABEL, "Database:", NULL},
        {XXWIDGETS_SORT_MAIN_DATABASE, XXWIDGETS_EDIT, "", "Main signature database directory ($data = program directory)"},
        {XXWIDGETS_SORT_MAIN_DATABASE_BROWSE, XXWIDGETS_BUTTON, "...", "Choose the main signature database directory"},
        {XXWIDGETS_SORT_CUSTOM_DATABASE_ENABLED, XXWIDGETS_CHECKBOX, "Custom", "Also load the custom database"},
        {XXWIDGETS_SORT_CUSTOM_DATABASE, XXWIDGETS_EDIT, "", "Custom signature database directory"},
        {XXWIDGETS_SORT_CUSTOM_DATABASE_BROWSE, XXWIDGETS_BUTTON, "...", "Choose the custom signature database directory"},
        {XXWIDGETS_SORT_ALL_FILE_TYPES, XXWIDGETS_CHECKBOX, "All file types", NULL},
        {XXWIDGETS_SORT_FILE_TYPES_CHOOSE, XXWIDGETS_BUTTON, "File types...", "Choose the file types to sort"},
        {XXWIDGETS_SORT_FILE_TYPES_SUMMARY, XXWIDGETS_LABEL, "", NULL},
        {XXWIDGETS_SORT_ALL_TYPES, XXWIDGETS_CHECKBOX, "All types", NULL},
        {XXWIDGETS_SORT_TYPES_CHOOSE, XXWIDGETS_BUTTON, "Types...", "Choose the detection types to sort"},
        {XXWIDGETS_SORT_TYPES_SUMMARY, XXWIDGETS_LABEL, "", NULL},
        {XXWIDGETS_SORT_UNKNOWN, XXWIDGETS_CHECKBOX, "Unknown", "Also sort files without a detection that passes the filter"},
        {XXWIDGETS_SORT_RESULT_LABEL, XXWIDGETS_LABEL, "Result:", NULL},
        {XXWIDGETS_SORT_RESULT, XXWIDGETS_EDIT, "", "The directory the results are written to"},
        {XXWIDGETS_SORT_RESULT_BROWSE, XXWIDGETS_BUTTON, "...", "Choose the directory the results are written to"},
        {XXWIDGETS_SORT_CATALOG, XXWIDGETS_CHECKBOX, "Catalog", "Write catalog files: one per detection, listing its files"},
        {XXWIDGETS_SORT_CATALOG_FORMAT, XXWIDGETS_EDIT, "", "Catalog file name format"},
        {XXWIDGETS_SORT_CATALOG_HELP, XXWIDGETS_BUTTON, "?", "Show the variables available in the catalog format"},
        {XXWIDGETS_SORT_COPY, XXWIDGETS_CHECKBOX, "Copy", "Copy the files into a tree named after their detections"},
        {XXWIDGETS_SORT_COPY_FORMAT, XXWIDGETS_EDIT, "", "Copy path format"},
        {XXWIDGETS_SORT_COPY_HELP, XXWIDGETS_BUTTON, "?", "Show the variables available in the copy format"},
        {XXWIDGETS_SORT_REMOVE_SOURCE, XXWIDGETS_CHECKBOX, "Remove source", "Delete each source file after it was copied"},
        {XXWIDGETS_SORT_MOVE_TO_FIRST, XXWIDGETS_CHECKBOX, "Move to first", "Move each file into its first detection only"},
    };
    xxwidgets_sort_panel *panel;
    xxwidgets_app *app;
    xxwidgets_status status = XXWIDGETS_OK;
    size_t i;
    if (out) *out = NULL;
    if (!owner || !out) return XXWIDGETS_INVALID_ARGUMENT;
    app = xxwidgets_widget_app(owner);
    if (!app) return XXWIDGETS_INVALID_ARGUMENT;
    panel = (xxwidgets_sort_panel *)calloc(1, sizeof(*panel));
    if (!panel) return XXWIDGETS_OUT_OF_MEMORY;
    panel->owner = owner;
    panel->bounds = bounds;
    if (config) {
        status = list_init(&panel->file_types, config->file_types, config->file_type_count);
        if (status == XXWIDGETS_OK) status = list_init(&panel->types, config->types, config->type_count);
    }
    panel->help = sort_strdup(config && config->variables_help ? config->variables_help : k_default_help);
    if (!panel->help) status = XXWIDGETS_OUT_OF_MEMORY;
    for (i = 0; status == XXWIDGETS_OK && i < sizeof(k_controls) / sizeof(k_controls[0]); i++) {
        status = xxwidgets_widget_create(app, owner, k_controls[i].kind, k_controls[i].text, (xxwidgets_rect){bounds.x, bounds.y, 10, 2},
                                         &panel->controls[k_controls[i].id]);
        if (status == XXWIDGETS_OK && k_controls[i].tooltip) xxwidgets_widget_set_tooltip(panel->controls[k_controls[i].id], k_controls[i].tooltip);
    }
    for (i = 0; status == XXWIDGETS_OK && i < FLAG_COUNT; i++) {
        status = xxwidgets_widget_create(app, owner, XXWIDGETS_CHECKBOX, k_flags[i].label, (xxwidgets_rect){bounds.x, bounds.y, 10, 2},
                                         &panel->controls[k_flags[i].id]);
    }
    if (status != XXWIDGETS_OK) {
        xxwidgets_sort_panel_destroy(panel);
        return status;
    }
    layout(panel);
    {
        xxwidgets_sort_settings defaults;
        if (xxwidgets_sort_settings_init(&defaults) == XXWIDGETS_OK) {
            xxwidgets_sort_panel_set(panel, &defaults);
            xxwidgets_sort_settings_free(&defaults);
        }
    }
    *out = panel;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_sort_panel_destroy(xxwidgets_sort_panel *panel)
{
    size_t i;
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < XXWIDGETS_SORT_CONTROL_COUNT; i++) {
        if (panel->controls[i]) xxwidgets_widget_destroy(panel->controls[i]);
    }
    list_free(&panel->file_types);
    list_free(&panel->types);
    free(panel->help);
    free(panel);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_sort_panel_set_rect(xxwidgets_sort_panel *panel, xxwidgets_rect bounds)
{
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    panel->bounds = bounds;
    layout(panel);
    return XXWIDGETS_OK;
}

xxwidgets_widget *xxwidgets_sort_panel_control(const xxwidgets_sort_panel *panel, xxwidgets_sort_panel_control_id id)
{
    if (!panel || (int)id < 0 || id >= XXWIDGETS_SORT_CONTROL_COUNT) return NULL;
    return panel->controls[id];
}

xxwidgets_status xxwidgets_sort_panel_set(xxwidgets_sort_panel *panel, const xxwidgets_sort_settings *settings)
{
    size_t i;
    if (!panel || !settings) return XXWIDGETS_INVALID_ARGUMENT;
    set_text(panel, XXWIDGETS_SORT_DIRECTORY, settings->directory);
    set_checked(panel, XXWIDGETS_SORT_SUBDIRECTORIES, settings->subdirectories);
    set_checked(panel, XXWIDGETS_SORT_LOG, settings->log);
    for (i = 0; i < FLAG_COUNT; i++) set_checked(panel, k_flags[i].id, (settings->flags & k_flags[i].flag) != 0);
    set_text(panel, XXWIDGETS_SORT_READ_BUFFER, settings->read_buffer_size);
    set_text(panel, XXWIDGETS_SORT_FILE_BUFFER, settings->file_buffer_size);
    set_text(panel, XXWIDGETS_SORT_MAIN_DATABASE, settings->main_database);
    set_checked(panel, XXWIDGETS_SORT_CUSTOM_DATABASE_ENABLED, settings->use_custom_database);
    set_text(panel, XXWIDGETS_SORT_CUSTOM_DATABASE, settings->custom_database);
    set_checked(panel, XXWIDGETS_SORT_ALL_FILE_TYPES, settings->all_file_types);
    list_set_from_string(&panel->file_types, settings->file_types);
    set_checked(panel, XXWIDGETS_SORT_ALL_TYPES, settings->all_types);
    list_set_from_string(&panel->types, settings->types);
    set_checked(panel, XXWIDGETS_SORT_UNKNOWN, settings->unknown);
    set_text(panel, XXWIDGETS_SORT_RESULT, settings->result_directory);
    set_checked(panel, XXWIDGETS_SORT_CATALOG, settings->catalog);
    set_text(panel, XXWIDGETS_SORT_CATALOG_FORMAT, settings->catalog_format);
    set_checked(panel, XXWIDGETS_SORT_COPY, settings->copy);
    set_text(panel, XXWIDGETS_SORT_COPY_FORMAT, settings->copy_format);
    set_checked(panel, XXWIDGETS_SORT_REMOVE_SOURCE, settings->remove_source);
    set_checked(panel, XXWIDGETS_SORT_MOVE_TO_FIRST, settings->move_to_first);
    update_summaries(panel);
    update_enabled(panel);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_sort_panel_get(const xxwidgets_sort_panel *panel, xxwidgets_sort_settings *settings)
{
    size_t i;
    if (!panel || !settings) return XXWIDGETS_INVALID_ARGUMENT;
    xxwidgets_sort_settings_free(settings);
    settings->directory = text_of(panel, XXWIDGETS_SORT_DIRECTORY);
    settings->subdirectories = checked(panel, XXWIDGETS_SORT_SUBDIRECTORIES);
    settings->log = checked(panel, XXWIDGETS_SORT_LOG);
    settings->flags = 0;
    for (i = 0; i < FLAG_COUNT; i++)
        if (checked(panel, k_flags[i].id)) settings->flags |= k_flags[i].flag;
    settings->read_buffer_size = text_of(panel, XXWIDGETS_SORT_READ_BUFFER);
    settings->file_buffer_size = text_of(panel, XXWIDGETS_SORT_FILE_BUFFER);
    settings->main_database = text_of(panel, XXWIDGETS_SORT_MAIN_DATABASE);
    settings->use_custom_database = checked(panel, XXWIDGETS_SORT_CUSTOM_DATABASE_ENABLED);
    settings->custom_database = text_of(panel, XXWIDGETS_SORT_CUSTOM_DATABASE);
    settings->all_file_types = checked(panel, XXWIDGETS_SORT_ALL_FILE_TYPES);
    settings->file_types = list_to_string(&panel->file_types);
    settings->all_types = checked(panel, XXWIDGETS_SORT_ALL_TYPES);
    settings->types = list_to_string(&panel->types);
    settings->unknown = checked(panel, XXWIDGETS_SORT_UNKNOWN);
    settings->result_directory = text_of(panel, XXWIDGETS_SORT_RESULT);
    settings->catalog = checked(panel, XXWIDGETS_SORT_CATALOG);
    settings->catalog_format = text_of(panel, XXWIDGETS_SORT_CATALOG_FORMAT);
    settings->copy = checked(panel, XXWIDGETS_SORT_COPY);
    settings->copy_format = text_of(panel, XXWIDGETS_SORT_COPY_FORMAT);
    settings->remove_source = checked(panel, XXWIDGETS_SORT_REMOVE_SOURCE);
    settings->move_to_first = checked(panel, XXWIDGETS_SORT_MOVE_TO_FIRST);
    if (!settings->directory || !settings->read_buffer_size || !settings->file_buffer_size || !settings->main_database ||
        !settings->custom_database || !settings->file_types || !settings->types || !settings->result_directory || !settings->catalog_format ||
        !settings->copy_format) {
        xxwidgets_sort_settings_free(settings);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    return XXWIDGETS_OK;
}

static int owns(const xxwidgets_sort_panel *panel, const xxwidgets_widget *widget)
{
    size_t i;
    if (!widget) return 0;
    for (i = 0; i < XXWIDGETS_SORT_CONTROL_COUNT; i++)
        if (panel->controls[i] == widget) return 1;
    return 0;
}

int xxwidgets_sort_panel_handle_event(xxwidgets_sort_panel *panel, const xxwidgets_event *event)
{
    xxwidgets_widget *w;
    if (!panel || !event || !owns(panel, event->widget)) return 0;
    w = event->widget;
    if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (w == panel->controls[XXWIDGETS_SORT_DIRECTORY_BROWSE]) panel->pending = PENDING_BROWSE_DIRECTORY;
        else if (w == panel->controls[XXWIDGETS_SORT_MAIN_DATABASE_BROWSE]) panel->pending = PENDING_BROWSE_MAIN;
        else if (w == panel->controls[XXWIDGETS_SORT_CUSTOM_DATABASE_BROWSE]) panel->pending = PENDING_BROWSE_CUSTOM;
        else if (w == panel->controls[XXWIDGETS_SORT_RESULT_BROWSE]) panel->pending = PENDING_BROWSE_RESULT;
        else if (w == panel->controls[XXWIDGETS_SORT_FILE_TYPES_CHOOSE]) panel->pending = PENDING_FILE_TYPES;
        else if (w == panel->controls[XXWIDGETS_SORT_TYPES_CHOOSE]) panel->pending = PENDING_TYPES;
        else if (w == panel->controls[XXWIDGETS_SORT_CATALOG_HELP] || w == panel->controls[XXWIDGETS_SORT_COPY_HELP]) panel->pending = PENDING_HELP;
        else if (w == panel->controls[XXWIDGETS_SORT_SCAN]) panel->pending = PENDING_SCAN;
        else { /* a checkbox */
            update_enabled(panel);
            update_summaries(panel);
        }
    } else if (event->type == XXWIDGETS_EVENT_CHANGE) {
        update_enabled(panel);
        update_summaries(panel);
    } else if (event->type == XXWIDGETS_EVENT_ACTIVATE && w == panel->controls[XXWIDGETS_SORT_DIRECTORY]) {
        panel->pending = PENDING_SCAN;
    }
    return 1;
}

void xxwidgets_sort_panel_request_scan(xxwidgets_sort_panel *panel)
{
    if (panel) panel->pending = PENDING_SCAN;
}

static xxwidgets_status browse_folder(xxwidgets_sort_panel *panel, xxwidgets_sort_panel_control_id id, const char *title)
{
    char *current = text_of(panel, id);
    char *chosen = NULL;
    int accepted = 0;
    xxwidgets_status status = xxwidgets_file_dialog(panel->owner, XXWIDGETS_FILE_DIALOG_FOLDER, title, current ? current : "", &chosen, &accepted);
    if (status == XXWIDGETS_OK && accepted && chosen) set_text(panel, id, chosen);
    free(chosen);
    free(current);
    return status;
}

static xxwidgets_status pick(xxwidgets_sort_panel *panel, sort_list *list, const char *title, const char *category)
{
    xxwidgets_type_picker_item *items;
    unsigned char *state;
    size_t i;
    int accepted = 0;
    xxwidgets_status status;
    if (!list->count) return xxwidgets_text_dialog(panel->owner, title, "Nothing to choose from.");
    items = (xxwidgets_type_picker_item *)calloc(list->count, sizeof(*items));
    state = (unsigned char *)malloc(list->count);
    if (!items || !state) {
        free(items);
        free(state);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    for (i = 0; i < list->count; i++) {
        items[i].name = list->names[i];
        items[i].category = category;
        items[i].note = "";
    }
    memcpy(state, list->checked, list->count);
    status = xxwidgets_type_picker_dialog(panel->owner, title, items, list->count, state, NULL, &accepted);
    if (status == XXWIDGETS_OK && accepted) memcpy(list->checked, state, list->count);
    free(items);
    free(state);
    update_summaries(panel);
    return status;
}

xxwidgets_status xxwidgets_sort_panel_process(xxwidgets_sort_panel *panel, int *scan)
{
    int pending;
    if (scan) *scan = 0;
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    pending = panel->pending;
    panel->pending = PENDING_NONE;
    switch (pending) {
    case PENDING_BROWSE_DIRECTORY: return browse_folder(panel, XXWIDGETS_SORT_DIRECTORY, "Open Directory");
    case PENDING_BROWSE_MAIN: return browse_folder(panel, XXWIDGETS_SORT_MAIN_DATABASE, "Open directory...");
    case PENDING_BROWSE_CUSTOM: return browse_folder(panel, XXWIDGETS_SORT_CUSTOM_DATABASE, "Open directory...");
    case PENDING_BROWSE_RESULT: return browse_folder(panel, XXWIDGETS_SORT_RESULT, "Open Directory");
    case PENDING_FILE_TYPES: return pick(panel, &panel->file_types, "File types", "File type");
    case PENDING_TYPES: return pick(panel, &panel->types, "Types", "Type");
    case PENDING_HELP: return xxwidgets_text_dialog(panel->owner, "Format", panel->help);
    case PENDING_SCAN:
        if (scan) *scan = 1;
        return XXWIDGETS_OK;
    default: return XXWIDGETS_OK;
    }
}

xxwidgets_status xxwidgets_sort_panel_set_enabled(xxwidgets_sort_panel *panel, int enabled)
{
    size_t i;
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < XXWIDGETS_SORT_CONTROL_COUNT; i++)
        if (panel->controls[i]) xxwidgets_widget_set_enabled(panel->controls[i], enabled);
    if (enabled) update_enabled(panel);
    return XXWIDGETS_OK;
}
