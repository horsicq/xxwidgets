/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidget_treediffviewer.h"
#include "xxwidgets/xxwidgets_combobox.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Parents met walking up from a search result before giving up. */
#define TREEDIFF_PATH_MAX 4096
#define TREEDIFF_FIELDS_MAX 65536

struct xxwidget_treediffviewer {
    xxwidgets_widget *owner, *controls[XXWIDGET_TREEDIFF_CONTROL_COUNT];
    xxwidget_treediff_source source;
    xxwidgets_tree_source tree_source;
    unsigned int kinds;
    char *path;        /* Path filter in effect, owned. */
    int flat;          /* The roots are search results. */
    uint64_t selected; /* Row whose fields are shown, 0 for none. */
    xxwidget_treediff_change selected_change;
    xxwidget_treediff_row *rows;
    size_t row_capacity;
    char *text; /* Formatted row texts of the last request. */
    size_t text_capacity;
    xxwidget_treediff_field *fields;
    size_t field_capacity, field_count;
};

#define C(v, id) ((v)->controls[XXWIDGET_TREEDIFF_##id])

static const char *const change_names[XXWIDGET_TREEDIFF_CHANGE_COUNT] = {XXW_N_("Context"),  XXW_N_("Added"),        XXW_N_("Removed"),
                                                                         XXW_N_("Modified"), XXW_N_("Kind changed"), XXW_N_("Access changed")};
static const char change_signs[XXWIDGET_TREEDIFF_CHANGE_COUNT] = {' ', '+', '-', '*', '!', '~'};

static xxwidget_treediff_change valid_change(xxwidget_treediff_change change)
{
    return (int)change > XXWIDGET_TREEDIFF_CONTEXT && change < XXWIDGET_TREEDIFF_CHANGE_COUNT ? change : XXWIDGET_TREEDIFF_CONTEXT;
}

xxwidgets_tree_color xxwidget_treediff_change_color(xxwidget_treediff_change change)
{
    /* Light / dark foreground pairs; the dark ones are the terminal's colors. */
    static const xxwidgets_tree_color colors[XXWIDGET_TREEDIFF_CHANGE_COUNT] = {
        {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX}, {0x008000, UINT32_MAX, 0x00ff00, UINT32_MAX}, {0xc00000, UINT32_MAX, 0xff0000, UINT32_MAX},
        {0xa06000, UINT32_MAX, 0xffbf00, UINT32_MAX},     {0x800080, UINT32_MAX, 0xd080ff, UINT32_MAX}, {0x808080, UINT32_MAX, 0xc0c0c0, UINT32_MAX}};
    return colors[valid_change(change)];
}

const char *xxwidget_treediff_change_name(xxwidget_treediff_change change)
{
    return change_names[valid_change(change)];
}

static int bounds_valid(xxwidgets_rect b)
{
    return b.x >= 0 && b.y >= 0 && b.width >= 60 && b.height >= 16 && b.x <= 32767 - b.width && b.y <= 32767 - b.height;
}

/* The tree's source: diff rows (or search results) formatted as
 * "<sign> <kind>  <text>", colored by change. */
static size_t viewer_children(void *user, uint64_t parent, size_t offset, xxwidgets_tree_item *items, size_t capacity, size_t *total)
{
    xxwidget_treediffviewer *v = (xxwidget_treediffviewer *)user;
    xxwidget_treediff_filter filter;
    size_t n, i, need = 1;
    char *at;
    *total = 0;
    if (capacity > v->row_capacity) {
        xxwidget_treediff_row *rows;
        if (capacity > SIZE_MAX / sizeof(*rows)) return SIZE_MAX;
        rows = (xxwidget_treediff_row *)realloc(v->rows, capacity * sizeof(*rows));
        if (!rows) return SIZE_MAX;
        v->rows = rows;
        v->row_capacity = capacity;
    }
    memset(v->rows, 0, capacity * sizeof(*v->rows));
    filter.kinds = v->kinds;
    filter.path = v->path;
    if (!parent && v->flat) n = v->source.search(v->source.user, &filter, offset, capacity, v->rows, total);
    else n = v->source.children(v->source.user, parent, &filter, offset, capacity, v->rows, total);
    if (n == SIZE_MAX) return SIZE_MAX;
    if (n > capacity) n = capacity;
    for (i = 0; i < n; ++i) {
        size_t length = v->rows[i].text ? strlen(v->rows[i].text) : 0;
        if (length > SIZE_MAX - need - 8) return SIZE_MAX;
        need += length + 6;
    }
    if (need > v->text_capacity) {
        char *text = (char *)realloc(v->text, need);
        if (!text) return SIZE_MAX;
        v->text = text;
        v->text_capacity = need;
    }
    at = v->text;
    for (i = 0; i < n; ++i) {
        xxwidget_treediff_change change = valid_change(v->rows[i].change);
        const char *name = v->rows[i].text ? v->rows[i].text : "";
        size_t length = strlen(name);
        char kind = v->rows[i].kind && (unsigned char)v->rows[i].kind >= 32 && (unsigned char)v->rows[i].kind < 127 ? v->rows[i].kind : ' ';
        at[0] = change_signs[change];
        at[1] = ' ';
        at[2] = kind;
        at[3] = ' ';
        at[4] = ' ';
        memcpy(at + 5, name, length + 1);
        items[i].id = v->rows[i].id;
        items[i].text = at;
        /* Search results are leaves: their rows are reached in the tree. */
        items[i].has_children = !parent && v->flat ? 0 : v->rows[i].has_children;
        items[i].color = xxwidget_treediff_change_color(change);
        at += length + 6;
    }
    return n;
}

/* Fields of id into v->fields (borrowed from the source until its next call). */
static xxwidgets_status load_fields(xxwidget_treediffviewer *v, uint64_t id)
{
    size_t total = 0, n;
    v->field_count = 0;
    if (!id || !v->source.fields) return XXWIDGETS_OK;
    for (;;) {
        memset(v->fields, 0, v->field_capacity * sizeof(*v->fields));
        /* As in an event callback: no widget or app destruction, no polling. */
        ++v->owner->app->dispatch_depth;
        n = v->source.fields(v->source.user, id, v->fields, v->field_capacity, &total);
        --v->owner->app->dispatch_depth;
        if (n == SIZE_MAX) return XXWIDGETS_UNAVAILABLE;
        if (n > v->field_capacity) n = v->field_capacity;
        if (total <= v->field_capacity || v->field_capacity >= TREEDIFF_FIELDS_MAX) break;
        {
            size_t capacity = total > TREEDIFF_FIELDS_MAX ? TREEDIFF_FIELDS_MAX : total;
            xxwidget_treediff_field *fields = (xxwidget_treediff_field *)realloc(v->fields, capacity * sizeof(*fields));
            if (!fields) return XXWIDGETS_OUT_OF_MEMORY;
            v->fields = fields;
            v->field_capacity = capacity;
        }
    }
    v->field_count = n;
    return XXWIDGETS_OK;
}

static int field_has_blob(const xxwidget_treediff_field *field)
{
    return (field->before_blob && field->before_size) || (field->after_blob && field->after_size);
}

/* Shows field row's blob in the hex view; Before/After follows the side that
 * has one when the chosen side has none. */
static xxwidgets_status show_blob(xxwidget_treediffviewer *v, int row)
{
    const xxwidget_treediff_field *field;
    int side = 1;
    xxwidgets_status status;
    if (row < 0 || (size_t)row >= v->field_count || !field_has_blob(&v->fields[row])) {
        status = xxwidgets_hexview_set_data(C(v, HEX), NULL, 0);
        return status == XXWIDGETS_OK ? xxwidgets_widget_set_enabled(C(v, HEX), 0) : status;
    }
    field = &v->fields[row];
    status = xxwidgets_widget_get_value(C(v, SIDE), &side);
    if (status != XXWIDGETS_OK) return status;
    if (side != 0 && side != 1) side = 1;
    if (side == 1 && !(field->after_blob && field->after_size)) side = 0;
    else if (side == 0 && !(field->before_blob && field->before_size)) side = 1;
    status = xxwidgets_widget_set_value(C(v, SIDE), side);
    if (status == XXWIDGETS_OK)
        status = side ? xxwidgets_hexview_set_data(C(v, HEX), field->after_blob, field->after_size)
                      : xxwidgets_hexview_set_data(C(v, HEX), field->before_blob, field->before_size);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(C(v, HEX), 1);
    return status;
}

static xxwidgets_status show_status(xxwidget_treediffviewer *v)
{
    char text[256];
    const char *name = "";
    xxwidgets_tree_node node;
    size_t index;
    if (xxwidgets_treeview_get_selection(C(v, TREE), &index, &node) == XXWIDGETS_OK && index != SIZE_MAX && node.text) name = node.text;
    if (!v->selected) snprintf(text, sizeof(text), "%s", XXW_TR(v->owner->app, "No selection"));
    else
        snprintf(text, sizeof(text), "%s: %s | %zu %s", XXW_TR(v->owner->app, change_names[v->selected_change]), name, v->field_count,
                 xxwidgets_app_ngettext(v->owner->app, "field", "fields", v->field_count));
    /* A cut name may end inside a UTF-8 sequence: drop the partial character. */
    if (!xxwidgets_valid_utf8(text)) {
        size_t length = strlen(text);
        while (length && ((unsigned char)text[length - 1] & 0xc0) == 0x80) text[--length] = 0;
        if (length && ((unsigned char)text[length - 1] & 0x80)) text[--length] = 0;
    }
    return xxwidgets_widget_set_text(C(v, STATUS), xxwidgets_valid_utf8(text) ? text : "");
}

/* The selected row's change, read back from its sign (the tree keeps text). */
static xxwidget_treediff_change selected_change(xxwidget_treediffviewer *v)
{
    xxwidgets_tree_node node;
    size_t index, i;
    if (xxwidgets_treeview_get_selection(C(v, TREE), &index, &node) != XXWIDGETS_OK || index == SIZE_MAX || !node.text) return XXWIDGET_TREEDIFF_CONTEXT;
    for (i = 1; i < XXWIDGET_TREEDIFF_CHANGE_COUNT; ++i)
        if (node.text[0] == change_signs[i]) return (xxwidget_treediff_change)i;
    return XXWIDGET_TREEDIFF_CONTEXT;
}

/* Details of the selected row: the table (first row with a blob selected)
 * and that blob. */
static xxwidgets_status show_details(xxwidget_treediffviewer *v)
{
    uint64_t id = 0;
    const char **cells = NULL;
    size_t i;
    int row = -1;
    xxwidgets_status status = xxwidgets_treeview_source_selection(C(v, TREE), &id);
    if (status != XXWIDGETS_OK) return status;
    status = load_fields(v, id);
    if (status != XXWIDGETS_OK) {
        /* The source failed: what was shown stays, for the row shown. */
        v->field_count = 0;
        return status;
    }
    v->selected = id;
    v->selected_change = selected_change(v);
    if (v->field_count) {
        if (v->field_count > SIZE_MAX / 3 / sizeof(*cells)) return XXWIDGETS_OUT_OF_MEMORY;
        cells = (const char **)malloc(v->field_count * 3 * sizeof(*cells));
        if (!cells) return XXWIDGETS_OUT_OF_MEMORY;
        for (i = 0; i < v->field_count; ++i) {
            cells[i * 3] = v->fields[i].field;
            cells[i * 3 + 1] = v->fields[i].before;
            cells[i * 3 + 2] = v->fields[i].after;
            if (row < 0 && field_has_blob(&v->fields[i])) row = (int)i;
        }
        if (row < 0) row = 0;
    }
    status = xxwidgets_table_set_rows(C(v, DETAILS), (const char *const *)cells, v->field_count);
    free(cells);
    if (status == XXWIDGETS_OK && row >= 0) status = xxwidgets_widget_set_value(C(v, DETAILS), row);
    if (status == XXWIDGETS_OK) status = show_blob(v, row);
    if (status == XXWIDGETS_OK) status = show_status(v);
    return status;
}

/* The blob of the selected field, after its row or Before/After changed. */
static xxwidgets_status show_selected_blob(xxwidget_treediffviewer *v)
{
    int row = -1;
    xxwidgets_status status = xxwidgets_widget_get_value(C(v, DETAILS), &row);
    if (status != XXWIDGETS_OK) return status;
    /* The fields are borrowed until the source's next call: ask again. */
    status = load_fields(v, v->selected);
    if (status != XXWIDGETS_OK) return status;
    return show_blob(v, row);
}

static uint64_t source_parent(xxwidget_treediffviewer *v, uint64_t id)
{
    uint64_t parent;
    ++v->owner->app->dispatch_depth;
    parent = v->source.parent(v->source.user, id);
    --v->owner->app->dispatch_depth;
    return parent;
}

static xxwidgets_status reveal(xxwidget_treediffviewer *v, uint64_t id)
{
    uint64_t *path;
    size_t count = 0, i;
    uint64_t at = id;
    xxwidgets_status status;
    if (!id || !v->source.parent) return XXWIDGETS_UNAVAILABLE;
    while (at && count < TREEDIFF_PATH_MAX) {
        ++count;
        at = source_parent(v, at);
    }
    if (at) return XXWIDGETS_UNAVAILABLE;
    path = (uint64_t *)malloc(count * sizeof(*path));
    if (!path) return XXWIDGETS_OUT_OF_MEMORY;
    for (i = count, at = id; i;) {
        path[--i] = at;
        if (i) at = source_parent(v, at);
    }
    status = xxwidgets_treeview_source_select(C(v, TREE), path, count);
    free(path);
    return status;
}

/* Applies kinds and path (copied): the roots change between the tree and the
 * search results; expanded and selected ids are kept where they exist. */
static xxwidgets_status apply_filter(xxwidget_treediffviewer *v, unsigned int kinds, const char *path)
{
    char *copy, *previous = v->path;
    unsigned int previous_kinds = v->kinds;
    int previous_flat = v->flat;
    uint64_t selected = 0;
    xxwidgets_status status;
    if (!path) path = "";
    if (!xxwidgets_valid_utf8(path)) return XXWIDGETS_INVALID_ARGUMENT;
    copy = xxwidgets_strdup(path);
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    (void)xxwidgets_treeview_source_selection(C(v, TREE), &selected);
    v->path = copy;
    v->kinds = kinds & XXWIDGET_TREEDIFF_ALL_CHANGES;
    v->flat = copy[0] && v->source.search;
    status = xxwidgets_treeview_source_refresh(C(v, TREE));
    if (status != XXWIDGETS_OK) {
        v->path = previous;
        v->kinds = previous_kinds;
        v->flat = previous_flat;
        free(copy);
        return status;
    }
    free(previous);
    /* A search result chosen before the search was cleared stays selected. */
    if (previous_flat && !v->flat && selected) {
        uint64_t now = 0;
        (void)xxwidgets_treeview_source_selection(C(v, TREE), &now);
        if (now != selected) (void)reveal(v, selected);
    }
    return show_details(v);
}

static unsigned int checked_kinds(xxwidget_treediffviewer *v)
{
    unsigned int kinds = 0;
    size_t i;
    for (i = 1; i < XXWIDGET_TREEDIFF_CHANGE_COUNT; ++i) {
        int checked = 0;
        if (xxwidgets_checkcombobox_is_checked(C(v, KINDS), i - 1, &checked) == XXWIDGETS_OK && checked) kinds |= XXWIDGET_TREEDIFF_MASK(i);
    }
    return kinds;
}

static xxwidgets_status filter_from_controls(xxwidget_treediffviewer *v)
{
    size_t required = 0;
    char *text;
    xxwidgets_status status = xxwidgets_widget_get_text(C(v, PATH), NULL, 0, &required);
    if (status != XXWIDGETS_OK) return status;
    text = (char *)malloc(required ? required : 1);
    if (!text) return XXWIDGETS_OUT_OF_MEMORY;
    text[0] = 0;
    status = required ? xxwidgets_widget_get_text(C(v, PATH), text, required, NULL) : XXWIDGETS_OK;
    if (status == XXWIDGETS_OK) status = apply_filter(v, checked_kinds(v), text);
    free(text);
    return status;
}

xxwidgets_status xxwidget_treediffviewer_set_rect(xxwidget_treediffviewer *v, xxwidgets_rect b)
{
    xxwidgets_rect r[XXWIDGET_TREEDIFF_CONTROL_COUNT], old[XXWIDGET_TREEDIFF_CONTROL_COUNT];
    int tree_width, right, right_width, main_height, details_height;
    size_t i;
    if (!v || !bounds_valid(b)) return XXWIDGETS_INVALID_ARGUMENT;
    tree_width = b.width * 11 / 20;
    right = tree_width + 1;
    right_width = b.width - right;
    main_height = b.height - 4;
    details_height = (main_height - 2) / 2;
#define R(id, dx, dy, w, h)                      \
    r[XXWIDGET_TREEDIFF_##id] = (xxwidgets_rect) \
    {                                            \
        b.x + (dx), b.y + (dy), w, h             \
    }
    R(PATH_LABEL, 0, 0, 5, 2);
    R(PATH, 6, 0, b.width - 6 - 27, 2);
    R(KINDS, b.width - 26, 0, 26, 2);
    R(TREE, 0, 3, tree_width, main_height);
    R(DETAILS, right, 3, right_width, details_height);
    R(SIDE_LABEL, right, 3 + details_height, 6, 2);
    R(SIDE, right + 7, 3 + details_height, 14, 2);
    R(HEX, right, 3 + details_height + 2, right_width, main_height - details_height - 2);
    R(STATUS, 0, b.height - 1, b.width, 1);
#undef R
    for (i = 0; i < XXWIDGET_TREEDIFF_CONTROL_COUNT; ++i) old[i] = v->controls[i]->rect;
    for (i = 0; i < XXWIDGET_TREEDIFF_CONTROL_COUNT; ++i) {
        xxwidgets_status status = xxwidgets_widget_set_rect(v->controls[i], r[i]);
        if (status != XXWIDGETS_OK) {
            size_t j;
            for (j = 0; j < i; ++j) (void)xxwidgets_widget_set_rect(v->controls[j], old[j]);
            return status;
        }
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidget_treediffviewer_create(xxwidgets_widget *owner, xxwidgets_rect bounds, const xxwidget_treediff_source *source, xxwidget_treediffviewer **out)
{
    static const xxwidgets_kind kinds[XXWIDGET_TREEDIFF_CONTROL_COUNT] = {XXWIDGETS_LABEL,    XXWIDGETS_EDIT,    XXWIDGETS_CHECKCOMBOBOX,
                                                                          XXWIDGETS_TREEVIEW, XXWIDGETS_TABLE,   XXWIDGETS_LABEL,
                                                                          XXWIDGETS_COMBOBOX, XXWIDGETS_HEXVIEW, XXWIDGETS_LABEL};
    static const char *const texts[XXWIDGET_TREEDIFF_CONTROL_COUNT] = {XXW_N_("Path"),        "", XXW_N_("Changes"), XXW_N_("Changes"), "", XXW_N_("Value"), "",
                                                                       XXW_N_("Value bytes"), ""};
    static const char *const sides[] = {XXW_N_("Before"), XXW_N_("After")};
    const char *titles[3];
    xxwidget_treediffviewer *v;
    xxwidgets_status status = XXWIDGETS_OK;
    size_t i;
    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !bounds_valid(bounds) || !source || !source->children || source->page_size > XXWIDGETS_TREE_PAGE_MAX)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing) return XXWIDGETS_BUSY;
    v = (xxwidget_treediffviewer *)calloc(1, sizeof(*v));
    if (!v) return XXWIDGETS_OUT_OF_MEMORY;
    v->owner = owner;
    v->source = *source;
    v->kinds = XXWIDGET_TREEDIFF_ALL_CHANGES;
    v->path = xxwidgets_strdup("");
    v->field_capacity = 32;
    v->fields = (xxwidget_treediff_field *)calloc(v->field_capacity, sizeof(*v->fields));
    if (!v->path || !v->fields) {
        free(v->path);
        free(v->fields);
        free(v);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    titles[0] = XXW_TR(owner->app, "Field");
    titles[1] = XXW_TR(owner->app, "Before");
    titles[2] = XXW_TR(owner->app, "After");
    for (i = 0; i < XXWIDGET_TREEDIFF_CONTROL_COUNT && status == XXWIDGETS_OK; ++i) {
        if (kinds[i] == XXWIDGETS_TABLE) status = xxwidgets_table_create(owner, (xxwidgets_rect){0, 0, 1, 1}, titles, 3, &v->controls[i]);
        else
            status = xxwidgets_widget_create(owner->app, owner, kinds[i], texts[i][0] ? XXW_TR(owner->app, texts[i]) : "", (xxwidgets_rect){0, 0, 1, 1}, &v->controls[i]);
    }
    if (status == XXWIDGETS_OK) status = xxwidget_treediffviewer_set_rect(v, bounds);
    if (status == XXWIDGETS_OK) status = xxwidgets_translated_choices(C(v, KINDS), change_names + 1, NULL, XXWIDGET_TREEDIFF_CHANGE_COUNT - 1);
    for (i = 0; status == XXWIDGETS_OK && i + 1 < XXWIDGET_TREEDIFF_CHANGE_COUNT; ++i) status = xxwidgets_checkcombobox_set_checked(C(v, KINDS), i, 1);
    if (status == XXWIDGETS_OK) status = xxwidgets_translated_choices(C(v, SIDE), sides, NULL, 2);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(C(v, SIDE), 1);
    if (status == XXWIDGETS_OK) status = xxwidgets_hexview_set_layout(C(v, HEX), 0, 8);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(C(v, HEX), 0);
    if (status == XXWIDGETS_OK) {
        v->tree_source.children = viewer_children;
        v->tree_source.user = v;
        v->tree_source.page_size = source->page_size;
        status = xxwidgets_treeview_set_source(C(v, TREE), &v->tree_source);
    }
    if (status == XXWIDGETS_OK) status = show_details(v);
    if (status != XXWIDGETS_OK) {
        (void)xxwidget_treediffviewer_destroy(v);
        return status;
    }
    *out = v;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidget_treediffviewer_destroy(xxwidget_treediffviewer *v)
{
    size_t i;
    if (!v) return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling || v->owner->app->syncing) return XXWIDGETS_BUSY;
    for (i = 0; i < XXWIDGET_TREEDIFF_CONTROL_COUNT; ++i)
        if (v->controls[i]) (void)xxwidgets_widget_destroy(v->controls[i]);
    free(v->path);
    free(v->rows);
    free(v->text);
    free(v->fields);
    free(v);
    return XXWIDGETS_OK;
}

xxwidgets_widget *xxwidget_treediffviewer_control(const xxwidget_treediffviewer *v, xxwidget_treediffviewer_control_id id)
{
    return v && (int)id >= 0 && id < XXWIDGET_TREEDIFF_CONTROL_COUNT ? v->controls[id] : NULL;
}

xxwidgets_status xxwidget_treediffviewer_handle_event(xxwidget_treediffviewer *v, const xxwidgets_event *event)
{
    if (!v || !event) return XXWIDGETS_INVALID_ARGUMENT;
    if (event->widget == C(v, TREE) && event->type == XXWIDGETS_EVENT_SELECT) return show_details(v);
    if (event->widget == C(v, TREE) && event->type == XXWIDGETS_EVENT_CHANGE) {
        /* Loading a selected "more" row or collapsing the selection's
         * ancestor moves the selection without SELECT. */
        uint64_t id = 0;
        xxwidgets_status status = xxwidgets_treeview_source_selection(C(v, TREE), &id);
        return status == XXWIDGETS_OK && id != v->selected ? show_details(v) : status;
    }
    if (event->widget == C(v, PATH) && event->type == XXWIDGETS_EVENT_ACTIVATE) return filter_from_controls(v);
    if (event->widget == C(v, KINDS) && event->type == XXWIDGETS_EVENT_CHANGE) return filter_from_controls(v);
    if (event->widget == C(v, DETAILS) && event->type == XXWIDGETS_EVENT_SELECT) return show_selected_blob(v);
    if (event->widget == C(v, SIDE) && event->type == XXWIDGETS_EVENT_SELECT) return show_selected_blob(v);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidget_treediffviewer_refresh(xxwidget_treediffviewer *v)
{
    xxwidgets_status status;
    if (!v) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_treeview_source_refresh(C(v, TREE));
    return status == XXWIDGETS_OK ? show_details(v) : status;
}

xxwidgets_status xxwidget_treediffviewer_set_filter(xxwidget_treediffviewer *v, unsigned int kinds, const char *path)
{
    size_t i;
    xxwidgets_status status;
    char *previous_text;
    int previous_checks[XXWIDGET_TREEDIFF_CHANGE_COUNT] = {0};
    if (!v) return XXWIDGETS_INVALID_ARGUMENT;
    if (!path) path = "";
    if (!xxwidgets_valid_utf8(path)) return XXWIDGETS_INVALID_ARGUMENT;
    previous_text = xxwidgets_strdup(C(v, PATH)->text);
    if (!previous_text) return XXWIDGETS_OUT_OF_MEMORY;
    for (i = 1; i < XXWIDGET_TREEDIFF_CHANGE_COUNT; ++i) (void)xxwidgets_checkcombobox_is_checked(C(v, KINDS), i - 1, &previous_checks[i]);
    status = xxwidgets_widget_set_text(C(v, PATH), path);
    for (i = 1; status == XXWIDGETS_OK && i < XXWIDGET_TREEDIFF_CHANGE_COUNT; ++i)
        status = xxwidgets_checkcombobox_set_checked(C(v, KINDS), i - 1, (kinds & XXWIDGET_TREEDIFF_MASK(i)) != 0);
    if (status == XXWIDGETS_OK) status = apply_filter(v, kinds, path);
    if (status != XXWIDGETS_OK) {
        /* Not applied: the controls show the filter in effect again. */
        (void)xxwidgets_widget_set_text(C(v, PATH), previous_text);
        for (i = 1; i < XXWIDGET_TREEDIFF_CHANGE_COUNT; ++i) (void)xxwidgets_checkcombobox_set_checked(C(v, KINDS), i - 1, previous_checks[i] != 0);
    }
    free(previous_text);
    return status;
}

xxwidgets_status xxwidget_treediffviewer_get_filter(const xxwidget_treediffviewer *v, unsigned int *kinds, const char **path)
{
    if (!v || !kinds || !path) return XXWIDGETS_INVALID_ARGUMENT;
    *kinds = v->kinds;
    *path = v->path;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidget_treediffviewer_selection(xxwidget_treediffviewer *v, uint64_t *id)
{
    if (id) *id = 0;
    if (!v || !id) return XXWIDGETS_INVALID_ARGUMENT;
    return xxwidgets_treeview_source_selection(C(v, TREE), id);
}

xxwidgets_status xxwidget_treediffviewer_select_path(xxwidget_treediffviewer *v, const uint64_t *path, size_t count)
{
    xxwidgets_status status;
    if (!v) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_treeview_source_select(C(v, TREE), path, count);
    return status == XXWIDGETS_OK ? show_details(v) : status;
}
