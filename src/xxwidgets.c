#include "xxwidgets_internal.h"

#include <limits.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int valid_utf8(const char *text)
{
    const unsigned char *s = (const unsigned char *)text;
    if (!s) return 0;
    while (*s) {
        unsigned int cp, min;
        int remaining;
        if (*s < 0x80) {
            ++s;
            continue;
        }
        if (*s >= 0xc2 && *s <= 0xdf) {
            cp = *s & 0x1f;
            min = 0x80;
            remaining = 1;
        } else if (*s >= 0xe0 && *s <= 0xef) {
            cp = *s & 0x0f;
            min = 0x800;
            remaining = 2;
        } else if (*s >= 0xf0 && *s <= 0xf4) {
            cp = *s & 7;
            min = 0x10000;
            remaining = 3;
        } else return 0;
        ++s;
        while (remaining--) {
            if ((*s & 0xc0) != 0x80) return 0;
            cp = (cp << 6) | (*s++ & 0x3f);
        }
        if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
    }
    return 1;
}

int xxwidgets_valid_utf8(const char *text)
{
    return valid_utf8(text);
}

char *xxwidgets_strdup(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = (char *)malloc(length);
    if (copy) memcpy(copy, text, length);
    return copy;
}

/* The texts share a prefix and a suffix around one changed span. A caret in
 * the unchanged end stays before the same text. A caret in front of the span
 * stays where it is when the span is small next to the unchanged end (a '~'
 * expanded with the caret at 0), but not when the text was replaced. */
size_t xxwidgets_edit_caret(const char *old, size_t caret, const char *now)
{
    size_t old_length = strlen(old), now_length = strlen(now), prefix = 0, suffix = 0, shorter;
    if (caret > old_length) return SIZE_MAX;
    shorter = old_length < now_length ? old_length : now_length;
    while (prefix < shorter && old[prefix] == now[prefix]) ++prefix;
    while (prefix && ((unsigned char)now[prefix] & 0xc0) == 0x80) --prefix;
    while (suffix < shorter - prefix && old[old_length - 1 - suffix] == now[now_length - 1 - suffix]) ++suffix;
    while (suffix && ((unsigned char)old[old_length - suffix] & 0xc0) == 0x80) --suffix;
    if (caret >= old_length - suffix) return now_length - (old_length - caret);
    if (caret <= prefix && suffix > old_length - prefix - suffix) return caret;
    return SIZE_MAX;
}

xxwidgets_status xxwidgets_store_text(xxwidgets_widget *widget, const char *text)
{
    char *copy;
    if (!widget || !valid_utf8(text)) return XXWIDGETS_INVALID_ARGUMENT;
    if (strcmp(widget->text, text) == 0) return XXWIDGETS_OK;
    copy = xxwidgets_strdup(text);
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    free(widget->text);
    widget->text = copy;
    return XXWIDGETS_OK;
}

int xxwidgets_focusable(const xxwidgets_widget *widget)
{
    return widget && widget->visible && widget->enabled && widget->parent && widget->parent->visible && widget->parent->enabled &&
           (widget->kind == XXWIDGETS_BUTTON || widget->kind == XXWIDGETS_EDIT || widget->kind == XXWIDGETS_CHECKBOX || xxwidgets_list_kind(widget) ||
            xxwidgets_combo_kind(widget));
}

xxwidgets_font_role xxwidgets_widget_font_role(const xxwidgets_widget *widget)
{
    if (widget->kind == XXWIDGETS_TREEVIEW) return XXWIDGETS_FONT_TREE_VIEWS;
    if (widget->kind == XXWIDGETS_EDIT) return XXWIDGETS_FONT_TEXT_EDITS;
    if (xxwidgets_list_kind(widget)) return XXWIDGETS_FONT_TABLE_VIEWS;
    return XXWIDGETS_FONT_CONTROLS;
}

static void dispatch_event(xxwidgets_widget *widget, const xxwidgets_event *event)
{
    xxwidgets_app *app = widget->app;
    if (app->syncing || app->quit) return;
    if (app->on_event) {
        ++app->dispatch_depth;
        app->on_event(app, event, app->user_data);
        --app->dispatch_depth;
    } else if (event->type == XXWIDGETS_EVENT_CLOSE) {
        xxwidgets_app_quit(app, 0);
    }
}

void xxwidgets_emit(xxwidgets_widget *widget, xxwidgets_event_type type, int value)
{
    xxwidgets_event event;
    event.type = type;
    event.widget = widget;
    event.value = value;
    event.x = event.y = 0;
    dispatch_event(widget, &event);
}

void xxwidgets_emit_context(xxwidgets_widget *widget, int value, int x, int y)
{
    xxwidgets_event event;
    event.type = XXWIDGETS_EVENT_CONTEXT_MENU;
    event.widget = widget;
    event.value = value;
    event.x = x;
    event.y = y;
    dispatch_event(widget, &event);
}

static int valid_rect(xxwidgets_rect rect)
{
    /* Bound multiplication by native cell dimensions and terminal arithmetic. */
    return rect.x >= 0 && rect.y >= 0 && rect.x <= 32767 && rect.y <= 32767 && rect.width > 0 && rect.height > 0 && rect.width <= 32767 && rect.height <= 32767;
}

static xxwidgets_status sync_widget(xxwidgets_widget *widget)
{
    xxwidgets_status status;
    ++widget->app->syncing;
    status = widget->app->ops->sync(widget);
    --widget->app->syncing;
    return status;
}

xxwidgets_status xxwidgets_app_create(const xxwidgets_config *config, xxwidgets_app **out_app)
{
    xxwidgets_app *app;
    xxwidgets_status status;
    xxwidgets_backend backend = config ? config->backend : XXWIDGETS_BACKEND_AUTO;
    if (!out_app) return XXWIDGETS_INVALID_ARGUMENT;
    *out_app = NULL;
    if (backend < XXWIDGETS_BACKEND_AUTO || backend > XXWIDGETS_BACKEND_TUI) return XXWIDGETS_INVALID_ARGUMENT;
#if defined(XXWIDGETS_HAS_NATIVE)
    if (backend == XXWIDGETS_BACKEND_AUTO) backend = XXWIDGETS_BACKEND_NATIVE;
#else
    if (backend == XXWIDGETS_BACKEND_NATIVE) return XXWIDGETS_UNAVAILABLE;
    if (backend == XXWIDGETS_BACKEND_AUTO) backend = XXWIDGETS_BACKEND_TUI;
#endif
    app = (xxwidgets_app *)calloc(1, sizeof(*app));
    if (!app) return XXWIDGETS_OUT_OF_MEMORY;
    app->backend = backend;
    app->ops = &xxwidgets_tui_ops;
#if defined(XXWIDGETS_HAS_NATIVE)
    if (backend == XXWIDGETS_BACKEND_NATIVE) app->ops = &xxwidgets_native_ops;
#endif
    if (config) {
        app->on_event = config->on_event;
        app->user_data = config->user_data;
    }
    status = app->ops->init(app);
    if (status != XXWIDGETS_OK) {
        app->ops->shutdown(app);
        free(app);
        return status;
    }
    *out_app = app;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_app_destroy(xxwidgets_app *app)
{
    if (!app) return XXWIDGETS_INVALID_ARGUMENT;
    if (app->dispatch_depth || app->polling) return XXWIDGETS_BUSY;
    while (app->widgets) xxwidgets_widget_destroy(app->widgets);
    app->ops->shutdown(app);
    free(app);
    return XXWIDGETS_OK;
}

xxwidgets_backend xxwidgets_app_backend(const xxwidgets_app *app)
{
    return app ? app->backend : XXWIDGETS_BACKEND_AUTO;
}

const char *xxwidgets_app_backend_name(const xxwidgets_app *app)
{
    return app ? app->ops->name : "none";
}

xxwidgets_status xxwidgets_app_poll(xxwidgets_app *app, int timeout_ms)
{
    xxwidgets_status status;
    if (!app || timeout_ms < 0) return XXWIDGETS_INVALID_ARGUMENT;
    if (app->polling || app->dispatch_depth || app->syncing) return XXWIDGETS_BUSY;
    if (app->quit) return XXWIDGETS_OK;
    app->polling = 1;
    status = app->ops->poll(app, timeout_ms);
    app->polling = 0;
    return status;
}

int xxwidgets_app_run(xxwidgets_app *app)
{
    if (!app || app->polling || app->dispatch_depth || app->syncing) return -1;
    while (!app->quit) {
        if (xxwidgets_app_poll(app, 50) != XXWIDGETS_OK) {
            app->exit_code = -1;
            break;
        }
    }
    return app->exit_code;
}

void xxwidgets_app_quit(xxwidgets_app *app, int exit_code)
{
    if (app) {
        app->quit = 1;
        app->exit_code = exit_code;
    }
}

const char *xxwidgets_status_string(xxwidgets_status status)
{
    switch (status) {
        case XXWIDGETS_OK: return "success";
        case XXWIDGETS_INVALID_ARGUMENT: return "invalid argument";
        case XXWIDGETS_OUT_OF_MEMORY: return "out of memory";
        case XXWIDGETS_UNAVAILABLE: return "backend unavailable";
        case XXWIDGETS_PLATFORM_ERROR: return "platform error";
        case XXWIDGETS_BUFFER_TOO_SMALL: return "buffer too small";
        case XXWIDGETS_BUSY: return "operation unavailable during callback or polling";
        default: return "unknown status";
    }
}

xxwidgets_status xxwidgets_widget_create(xxwidgets_app *app, xxwidgets_widget *parent, xxwidgets_kind kind, const char *text, xxwidgets_rect rect,
                                         xxwidgets_widget **out_widget)
{
    xxwidgets_widget *widget, **tail;
    xxwidgets_status status;
    if (!out_widget) return XXWIDGETS_INVALID_ARGUMENT;
    *out_widget = NULL;
    if (!app || kind < XXWIDGETS_WINDOW || kind > XXWIDGETS_TREEVIEW || !valid_rect(rect) || !valid_utf8(text ? text : "") ||
        (kind == XXWIDGETS_WINDOW ? parent != NULL : (!parent || parent->app != app || parent->kind != XXWIDGETS_WINDOW)))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (app->dispatch_depth || app->polling) return XXWIDGETS_BUSY;
    widget = (xxwidgets_widget *)calloc(1, sizeof(*widget));
    if (!widget) return XXWIDGETS_OUT_OF_MEMORY;
    widget->text = xxwidgets_strdup(text ? text : "");
    if (!widget->text) {
        free(widget);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    widget->app = app;
    widget->parent = parent;
    widget->kind = kind;
    widget->rect = rect;
    widget->visible = 1;
    widget->enabled = 1;
    widget->value = (xxwidgets_list_kind(widget) || xxwidgets_combo_kind(widget)) ? -1 : 0;
    widget->hex_columns = 16;
    widget->scan_column_width[0] = 12;
    widget->scan_column_width[1] = 24;
    widget->scan_column_width[2] = 12;
    widget->scan_column_width[3] = 4;
    tail = &app->widgets;
    while (*tail) tail = &(*tail)->next;
    *tail = widget;
    ++app->syncing;
    status = app->ops->create(widget);
    --app->syncing;
    if (status != XXWIDGETS_OK) {
        ++app->syncing;
        app->ops->destroy(widget);
        --app->syncing;
        *tail = NULL;
        free(widget->text);
        free(widget);
        return status;
    }
    *out_widget = widget;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_widget_destroy(xxwidgets_widget *widget)
{
    xxwidgets_widget **link, *child;
    size_t i;
    xxwidgets_app *app;
    if (!widget) return XXWIDGETS_INVALID_ARGUMENT;
    app = widget->app;
    if (app->dispatch_depth || app->polling) return XXWIDGETS_BUSY;
    for (;;) {
        child = app->widgets;
        while (child && child->parent != widget) child = child->next;
        if (!child) break;
        xxwidgets_widget_destroy(child);
    }
    ++app->syncing;
    app->ops->destroy(widget);
    --app->syncing;
    link = &app->widgets;
    while (*link && *link != widget) link = &(*link)->next;
    if (*link) *link = widget->next;
    for (i = 0; i < widget->item_count; ++i) free(widget->items[i]);
    if (widget->archive_entries)
        for (i = 0; i < widget->item_count; ++i) free((void *)widget->archive_entries[i].path);
    free(widget->items);
    free(widget->archive_entries);
    xxwidgets_archivebrowser_dispose(widget);
    xxwidgets_combobox_dispose(widget);
    xxwidgets_scanresults_dispose(widget);
    xxwidgets_treeview_dispose(widget);
    free(widget->shortcuts);
    free(widget->hex_data);
    free(widget->text);
    free(widget);
    return XXWIDGETS_OK;
}

xxwidgets_kind xxwidgets_widget_kind(const xxwidgets_widget *widget)
{
    return widget ? widget->kind : XXWIDGETS_WINDOW;
}

void *xxwidgets_widget_native_handle(const xxwidgets_widget *widget)
{
    return widget ? widget->native : NULL;
}
void xxwidgets_widget_set_user_data(xxwidgets_widget *widget, void *user_data)
{
    if (widget) widget->user_data = user_data;
}
void *xxwidgets_widget_user_data(const xxwidgets_widget *widget)
{
    return widget ? widget->user_data : NULL;
}

xxwidgets_status xxwidgets_widget_set_text(xxwidgets_widget *widget, const char *text)
{
    char *copy, *previous;
    xxwidgets_status status;
    if (!widget || !valid_utf8(text)) return XXWIDGETS_INVALID_ARGUMENT;
    copy = xxwidgets_strdup(text);
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    previous = widget->text;
    widget->text = copy;
    status = sync_widget(widget);
    if (status != XXWIDGETS_OK) {
        widget->text = previous;
        sync_widget(widget);
        free(copy);
    } else free(previous);
    return status;
}

xxwidgets_status xxwidgets_widget_get_text(xxwidgets_widget *widget, char *buffer, size_t capacity, size_t *required)
{
    size_t length, copied;
    xxwidgets_status status;
    if (!widget || (!buffer && capacity)) return XXWIDGETS_INVALID_ARGUMENT;
    status = widget->app->ops->read_text(widget);
    if (status != XXWIDGETS_OK) return status;
    length = strlen(widget->text) + 1;
    if (required) *required = length;
    if (!buffer && capacity == 0) return XXWIDGETS_OK;
    if (capacity) {
        copied = length < capacity ? length - 1 : capacity - 1;
        /* Keep truncated strings valid UTF-8. */
        while (copied && (((unsigned char)widget->text[copied] & 0xc0) == 0x80)) --copied;
        memcpy(buffer, widget->text, copied);
        buffer[copied] = 0;
    }
    return capacity >= length ? XXWIDGETS_OK : XXWIDGETS_BUFFER_TOO_SMALL;
}

xxwidgets_status xxwidgets_widget_set_rect(xxwidgets_widget *widget, xxwidgets_rect rect)
{
    xxwidgets_rect previous;
    xxwidgets_status status;
    if (!widget || !valid_rect(rect)) return XXWIDGETS_INVALID_ARGUMENT;
    previous = widget->rect;
    widget->rect = rect;
    status = sync_widget(widget);
    if (status != XXWIDGETS_OK) {
        widget->rect = previous;
        sync_widget(widget);
    }
    return status;
}

int xxwidgets_widget_has_focus(const xxwidgets_widget *widget)
{
    if (!widget || !widget->app->ops->has_focus) return -1;
    return widget->app->ops->has_focus(widget) ? 1 : 0;
}

xxwidgets_status xxwidgets_widget_get_rect(const xxwidgets_widget *widget, xxwidgets_rect *rect)
{
    if (!widget || !rect) return XXWIDGETS_INVALID_ARGUMENT;
    *rect = widget->rect;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_window_set_minimum_size(xxwidgets_widget *window, int columns, int rows)
{
    int previous_columns, previous_rows, previous_set;
    xxwidgets_status status;
    if (!window || window->kind != XXWIDGETS_WINDOW || columns < 0 || rows < 0 || columns > 32767 || rows > 32767) return XXWIDGETS_INVALID_ARGUMENT;
    previous_columns = window->min_columns;
    previous_rows = window->min_rows;
    previous_set = window->has_minimum;
    window->min_columns = columns;
    window->min_rows = rows;
    window->has_minimum = 1;
    status = sync_widget(window);
    if (status != XXWIDGETS_OK) {
        window->min_columns = previous_columns;
        window->min_rows = previous_rows;
        window->has_minimum = previous_set;
        sync_widget(window);
    }
    return status;
}

static xxwidgets_status set_flag(xxwidgets_widget *widget, int *flag, int value)
{
    int previous = *flag;
    xxwidgets_status status;
    *flag = value != 0;
    status = sync_widget(widget);
    if (status != XXWIDGETS_OK) {
        *flag = previous;
        sync_widget(widget);
    }
    return status;
}

xxwidgets_status xxwidgets_widget_set_visible(xxwidgets_widget *widget, int visible)
{
    return widget ? set_flag(widget, &widget->visible, visible) : XXWIDGETS_INVALID_ARGUMENT;
}
xxwidgets_status xxwidgets_widget_set_enabled(xxwidgets_widget *widget, int enabled)
{
    return widget ? set_flag(widget, &widget->enabled, enabled) : XXWIDGETS_INVALID_ARGUMENT;
}
xxwidgets_status xxwidgets_widget_focus(xxwidgets_widget *widget)
{
    if (!xxwidgets_focusable(widget)) return XXWIDGETS_INVALID_ARGUMENT;
    return widget->app->ops->focus(widget);
}

xxwidgets_status xxwidgets_widget_set_value(xxwidgets_widget *widget, int value)
{
    if (widget && widget->kind == XXWIDGETS_TREEVIEW) return xxwidgets_treeview_select(widget, value);
    if (widget && widget->kind == XXWIDGETS_ARCHIVEBROWSER) {
        size_t row = (size_t)value;
        if (value < -1) return XXWIDGETS_INVALID_ARGUMENT;
        return xxwidgets_archivebrowser_set_selection(widget, value == -1 ? NULL : &row, value == -1 ? 0 : 1);
    }
    int previous;
    xxwidgets_status status;
    if (!widget || (widget->kind == XXWIDGETS_CHECKBOX                              ? (value != 0 && value != 1)
                    : widget->kind == XXWIDGETS_PROGRESS                            ? (value < 0 || value > 100)
                    : (xxwidgets_list_kind(widget) || xxwidgets_combo_kind(widget)) ? (value < -1 || (value >= 0 && (size_t)value >= widget->item_count))
                                                                                    : 1))
        return XXWIDGETS_INVALID_ARGUMENT;
    previous = widget->value;
    widget->value = value;
    status = sync_widget(widget);
    if (status != XXWIDGETS_OK) {
        widget->value = previous;
        sync_widget(widget);
    }
    return status;
}
xxwidgets_status xxwidgets_widget_get_value(xxwidgets_widget *widget, int *value)
{
    xxwidgets_status status;
    if (!widget || !value || (widget->kind != XXWIDGETS_CHECKBOX && widget->kind != XXWIDGETS_PROGRESS && !xxwidgets_list_kind(widget) && !xxwidgets_combo_kind(widget)))
        return XXWIDGETS_INVALID_ARGUMENT;
    status = widget->app->ops->read_value(widget);
    if (status == XXWIDGETS_OK) *value = widget->value;
    return status;
}

xxwidgets_status xxwidgets_listbox_add(xxwidgets_widget *widget, const char *text)
{
    char *copy, **items;
    xxwidgets_status status;
    if (!widget || widget->kind != XXWIDGETS_LISTBOX || !valid_utf8(text) || widget->item_count >= INT_MAX || widget->item_count >= (size_t)-1 / sizeof(char *) - 1)
        return XXWIDGETS_INVALID_ARGUMENT;
    copy = xxwidgets_strdup(text);
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    items = (char **)realloc(widget->items, (widget->item_count + 1) * sizeof(*items));
    if (!items) {
        free(copy);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    widget->items = items;
    items[widget->item_count++] = copy;
    status = sync_widget(widget);
    if (status != XXWIDGETS_OK) {
        --widget->item_count;
        sync_widget(widget);
        free(copy);
    }
    return status;
}
xxwidgets_status xxwidgets_listbox_clear(xxwidgets_widget *widget)
{
    char **items;
    size_t count, i;
    int value;
    xxwidgets_status status;
    if (!widget || widget->kind != XXWIDGETS_LISTBOX) return XXWIDGETS_INVALID_ARGUMENT;
    items = widget->items;
    count = widget->item_count;
    value = widget->value;
    widget->items = NULL;
    widget->item_count = 0;
    widget->value = -1;
    status = sync_widget(widget);
    if (status != XXWIDGETS_OK) {
        widget->items = items;
        widget->item_count = count;
        widget->value = value;
        sync_widget(widget);
    } else {
        for (i = 0; i < count; ++i) free(items[i]);
        free(items);
    }
    return status;
}
size_t xxwidgets_listbox_count(const xxwidgets_widget *widget)
{
    return widget && widget->kind == XXWIDGETS_LISTBOX ? widget->item_count : 0;
}

static void free_rows(char **rows, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) free(rows[i]);
    free(rows);
}

static void free_archive_entries(xxwidgets_archive_entry *entries, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) free((void *)entries[i].path);
    free(entries);
}

static xxwidgets_status archive_row(const xxwidgets_archive_entry *entry, char **out_row, size_t *out_columns)
{
    static const char digits[] = "0123456789ABCDEF";
    const unsigned char *path = (const unsigned char *)entry->path;
    size_t length = 28, columns = 28, pos;
    char *line;
    while (*path) {
        size_t added = (*path < 32 || *path == 127) ? 4 : 1;
        if (added > SIZE_MAX - length - 1) return XXWIDGETS_INVALID_ARGUMENT;
        length += added;
        if (added == 4) columns += 4;
        else if ((*path & 0xc0) != 0x80) ++columns;
        ++path;
    }
    line = (char *)malloc(length + 1);
    if (!line) return XXWIDGETS_OUT_OF_MEMORY;
    if (entry->is_directory) snprintf(line, 29, "<DIR> %20s  ", "");
    else snprintf(line, 29, "      %20" PRIu64 "  ", entry->size);
    pos = 28;
    path = (const unsigned char *)entry->path;
    while (*path) {
        if (*path < 32 || *path == 127) {
            line[pos++] = '\\';
            line[pos++] = 'x';
            line[pos++] = digits[*path >> 4];
            line[pos++] = digits[*path & 15];
        } else line[pos++] = (char)*path;
        ++path;
    }
    line[pos] = 0;
    *out_row = line;
    *out_columns = columns;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_archiveview_set_entries(xxwidgets_widget *widget, const xxwidgets_archive_entry *entries, size_t count)
{
    xxwidgets_archive_entry *copies = NULL, *previous_entries;
    char **rows = NULL, **previous_rows;
    size_t i, previous_count, previous_columns, columns = 0;
    uint64_t previous_revision;
    int previous_value;
    xxwidgets_status status;
    if (!widget || widget->kind != XXWIDGETS_ARCHIVEVIEW || (!entries && count) || count > INT_MAX || count > SIZE_MAX / sizeof(*copies) ||
        count > SIZE_MAX / sizeof(*rows))
        return XXWIDGETS_INVALID_ARGUMENT;
    /* Validate the complete input before allocating or touching widget state. */
    for (i = 0; i < count; ++i)
        if (!valid_utf8(entries[i].path) || !entries[i].path[0] || (entries[i].is_directory != 0 && entries[i].is_directory != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    if (count) {
        copies = (xxwidgets_archive_entry *)calloc(count, sizeof(*copies));
        rows = (char **)calloc(count, sizeof(*rows));
        if (!copies || !rows) {
            free(copies);
            free(rows);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
    }
    for (i = 0; i < count; ++i) {
        size_t row_columns;
        copies[i] = entries[i];
        copies[i].path = xxwidgets_strdup(entries[i].path);
        if (!copies[i].path) {
            free_archive_entries(copies, i);
            free_rows(rows, i);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        status = archive_row(&copies[i], &rows[i], &row_columns);
        if (status != XXWIDGETS_OK) {
            free_archive_entries(copies, i + 1);
            free_rows(rows, i);
            return status;
        }
        if (row_columns > columns) columns = row_columns;
    }
    previous_entries = widget->archive_entries;
    previous_rows = widget->items;
    previous_count = widget->item_count;
    previous_value = widget->value;
    previous_revision = widget->archive_revision;
    previous_columns = widget->archive_columns;
    widget->archive_entries = copies;
    widget->items = rows;
    widget->item_count = count;
    widget->archive_columns = columns;
    widget->archive_revision = previous_revision + 1;
    widget->value = count ? 0 : -1;
    status = sync_widget(widget);
    if (status != XXWIDGETS_OK) {
        widget->archive_entries = previous_entries;
        widget->items = previous_rows;
        widget->item_count = previous_count;
        widget->value = previous_value;
        widget->archive_revision = previous_revision;
        widget->archive_columns = previous_columns;
        sync_widget(widget);
        free_archive_entries(copies, count);
        free_rows(rows, count);
    } else {
        free_archive_entries(previous_entries, previous_count);
        free_rows(previous_rows, previous_count);
    }
    return status;
}

xxwidgets_status xxwidgets_archiveview_clear(xxwidgets_widget *widget)
{
    return xxwidgets_archiveview_set_entries(widget, NULL, 0);
}

size_t xxwidgets_archiveview_count(const xxwidgets_widget *widget)
{
    return widget && widget->kind == XXWIDGETS_ARCHIVEVIEW ? widget->item_count : 0;
}

xxwidgets_status xxwidgets_archiveview_get_entry(const xxwidgets_widget *widget, size_t index, xxwidgets_archive_entry *entry)
{
    if (!widget || widget->kind != XXWIDGETS_ARCHIVEVIEW || !entry || index >= widget->item_count) return XXWIDGETS_INVALID_ARGUMENT;
    *entry = widget->archive_entries[index];
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_archiveview_get_selection(xxwidgets_widget *widget, size_t *index, xxwidgets_archive_entry *entry)
{
    int value;
    xxwidgets_status status;
    if (!widget || widget->kind != XXWIDGETS_ARCHIVEVIEW || !index || !entry) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_widget_get_value(widget, &value);
    if (status != XXWIDGETS_OK) return status;
    if (value < 0 || (size_t)value >= widget->item_count) {
        *index = SIZE_MAX;
        entry->path = NULL;
        entry->size = 0;
        entry->is_directory = 0;
    } else {
        *index = (size_t)value;
        *entry = widget->archive_entries[value];
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status hex_rows(const unsigned char *data, size_t size, uint64_t base, unsigned int columns, char ***out_rows, size_t *out_count)
{
    static const char digits[] = "0123456789ABCDEF";
    size_t count = size / columns + (size % columns != 0), row;
    char **rows;
    *out_rows = NULL;
    *out_count = 0;
    if (size && (uintmax_t)(size - 1) > UINT64_MAX - base) return XXWIDGETS_INVALID_ARGUMENT;
    if (!count) return XXWIDGETS_OK;
    if (count > INT_MAX || count > SIZE_MAX / sizeof(*rows)) return XXWIDGETS_INVALID_ARGUMENT;
    rows = (char **)calloc(count, sizeof(*rows));
    if (!rows) return XXWIDGETS_OUT_OF_MEMORY;
    for (row = 0; row < count; ++row) {
        size_t offset = row * columns, available = size - offset, pos = 0;
        uint64_t address = base + (uint64_t)offset;
        unsigned int i;
        char *line = (char *)malloc(22 + 4 * (size_t)columns);
        if (!line) {
            free_rows(rows, row);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        if (available > columns) available = columns;
        for (i = 0; i < 16; ++i) line[pos++] = digits[(address >> ((15 - i) * 4)) & 15];
        line[pos++] = ' ';
        line[pos++] = ' ';
        for (i = 0; i < columns; ++i) {
            unsigned char byte = i < available ? data[offset + i] : 0;
            line[pos++] = i < available ? digits[byte >> 4] : ' ';
            line[pos++] = i < available ? digits[byte & 15] : ' ';
            line[pos++] = ' ';
        }
        line[pos++] = ' ';
        line[pos++] = '|';
        for (i = 0; i < columns; ++i) {
            unsigned char byte = i < available ? data[offset + i] : 0;
            line[pos++] = i >= available ? ' ' : (byte >= 32 && byte <= 126 ? (char)byte : '.');
        }
        line[pos++] = '|';
        line[pos] = 0;
        rows[row] = line;
    }
    *out_rows = rows;
    *out_count = count;
    return XXWIDGETS_OK;
}

static xxwidgets_status replace_hex(xxwidgets_widget *widget, unsigned char *data, size_t size, uint64_t base, unsigned int columns, int reset_selection)
{
    char **rows, **previous_rows = widget->items;
    unsigned char *previous_data = widget->hex_data;
    size_t count, previous_count = widget->item_count, previous_size = widget->hex_size;
    uint64_t previous_base = widget->hex_base, previous_revision = widget->hex_revision;
    unsigned int previous_columns = widget->hex_columns;
    int previous_value = widget->value;
    size_t selected_offset = previous_value < 0 ? 0 : (size_t)previous_value * previous_columns;
    xxwidgets_status status = hex_rows(data, size, base, columns, &rows, &count);
    if (status != XXWIDGETS_OK) return status;
    widget->items = rows;
    widget->item_count = count;
    widget->hex_data = data;
    widget->hex_size = size;
    widget->hex_base = base;
    widget->hex_columns = columns;
    widget->hex_revision = previous_revision + 1;
    widget->value = count == 0 ? -1 : reset_selection ? 0 : previous_value < 0 ? -1 : (int)(selected_offset / columns);
    status = sync_widget(widget);
    if (status != XXWIDGETS_OK) {
        widget->items = previous_rows;
        widget->item_count = previous_count;
        widget->hex_data = previous_data;
        widget->hex_size = previous_size;
        widget->hex_base = previous_base;
        widget->hex_columns = previous_columns;
        widget->hex_revision = previous_revision;
        widget->value = previous_value;
        sync_widget(widget);
        free_rows(rows, count);
    } else {
        free_rows(previous_rows, previous_count);
        if (data != previous_data) free(previous_data);
    }
    return status;
}

xxwidgets_status xxwidgets_hexview_set_data(xxwidgets_widget *widget, const void *data, size_t size)
{
    unsigned char *copy = NULL;
    xxwidgets_status status;
    if (!widget || widget->kind != XXWIDGETS_HEXVIEW || (!data && size) || (size && (uintmax_t)(size - 1) > UINT64_MAX - widget->hex_base) ||
        size / widget->hex_columns + (size % widget->hex_columns != 0) > INT_MAX)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (size) {
        copy = (unsigned char *)malloc(size);
        if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
        memcpy(copy, data, size);
    }
    status = replace_hex(widget, copy, size, widget->hex_base, widget->hex_columns, 1);
    if (status != XXWIDGETS_OK) free(copy);
    return status;
}

xxwidgets_status xxwidgets_hexview_set_layout(xxwidgets_widget *widget, uint64_t base_address, unsigned int bytes_per_row)
{
    if (!widget || widget->kind != XXWIDGETS_HEXVIEW || (bytes_per_row != 8 && bytes_per_row != 16 && bytes_per_row != 32)) return XXWIDGETS_INVALID_ARGUMENT;
    if (base_address == widget->hex_base && bytes_per_row == widget->hex_columns) return XXWIDGETS_OK;
    return replace_hex(widget, widget->hex_data, widget->hex_size, base_address, bytes_per_row, 0);
}

size_t xxwidgets_hexview_size(const xxwidgets_widget *widget)
{
    return widget && widget->kind == XXWIDGETS_HEXVIEW ? widget->hex_size : 0;
}

xxwidgets_status xxwidgets_hexview_get_selection(xxwidgets_widget *widget, size_t *offset, size_t *length)
{
    int value;
    xxwidgets_status status;
    if (!widget || widget->kind != XXWIDGETS_HEXVIEW || !offset || !length) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_widget_get_value(widget, &value);
    if (status != XXWIDGETS_OK) return status;
    if (value < 0) {
        *offset = SIZE_MAX;
        *length = 0;
        return XXWIDGETS_OK;
    }
    if ((size_t)value >= widget->item_count) return XXWIDGETS_PLATFORM_ERROR;
    *offset = (size_t)value * widget->hex_columns;
    *length = widget->hex_size - *offset;
    if (*length > widget->hex_columns) *length = widget->hex_columns;
    return XXWIDGETS_OK;
}
