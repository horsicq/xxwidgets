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
           (widget->kind == XXWIDGETS_CANVAS || widget->kind == XXWIDGETS_SPLITTER || widget->kind == XXWIDGETS_BUTTON || widget->kind == XXWIDGETS_EDIT || widget->kind == XXWIDGETS_CHECKBOX || xxwidgets_list_kind(widget) ||
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
    xxwidgets_event event = {0};
    event.type = type;
    event.widget = widget;
    event.value = value;
    event.x = event.y = 0;
    dispatch_event(widget, &event);
}

void xxwidgets_emit_point(xxwidgets_widget *widget,xxwidgets_event_type type,int value,int x,int y){xxwidgets_event event={0};event.type=type;event.widget=widget;event.value=value;event.x=x;event.y=y;dispatch_event(widget,&event);}

void xxwidgets_emit_context(xxwidgets_widget *widget, int value, int x, int y)
{
    xxwidgets_event event = {0};
    event.type = XXWIDGETS_EVENT_CONTEXT_MENU;
    event.widget = widget;
    event.value = value;
    event.x = x;
    event.y = y;
    dispatch_event(widget, &event);
}

int xxwidgets_can_drop_files(const xxwidgets_widget *window)
{
    return window && window->kind == XXWIDGETS_WINDOW && window->accept_file_drops && window->visible && window->enabled &&
           !window->app->syncing && !window->app->quit && window->app->on_event &&
           (!window->app->modal_window || window->app->modal_window == window);
}

int xxwidgets_emit_drop_files(xxwidgets_widget *window, const char *const *paths, size_t count)
{
    xxwidgets_event event = {0};
    size_t i;
    if (!xxwidgets_can_drop_files(window) || !paths || !count) return 0;
    for (i = 0; i < count; ++i)
        if (!paths[i] || !paths[i][0] || !xxwidgets_valid_utf8(paths[i])) return 0;
    event.type = XXWIDGETS_EVENT_DROP_FILES;
    event.widget = window;
    event.paths = paths;
    event.path_count = count;
    dispatch_event(window, &event);
    return 1;
}

static int valid_rect(xxwidgets_rect rect, int window)
{
    /* Bound multiplication by native cell dimensions and terminal arithmetic.
     * A window may sit left of or above the primary monitor (Win32 reports
     * such a frame, and a maximized one, at negative cells). */
    int least = window ? -32767 : 0;
    return rect.x >= least && rect.y >= least && rect.x <= 32767 && rect.y <= 32767 && rect.width > 0 && rect.height > 0 && rect.width <= 32767 && rect.height <= 32767;
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
    if (status == XXWIDGETS_OK && !app->modal_window) xxwidgets_hexview_deferred(app);
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

void xxwidgets_app_set_create_hidden(xxwidgets_app* app, int hidden)
{
  if (app) app->create_hidden = hidden != 0;
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
    if (!app || kind < XXWIDGETS_WINDOW || kind > XXWIDGETS_DEBUGGER_STACK || !valid_rect(rect, kind == XXWIDGETS_WINDOW) || !valid_utf8(text ? text : "") ||
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
    widget->visible = (kind == XXWIDGETS_WINDOW && app->create_hidden) ? 0 : 1;
    widget->enabled = 1;
    widget->value = (xxwidgets_list_kind(widget) || xxwidgets_combo_kind(widget)) ? -1 : 0;
    if (kind == XXWIDGETS_HEXVIEW) xxwidgets_hexview_init(widget);
    if (kind == XXWIDGETS_SCANRESULTS || kind == XXWIDGET_SYSTEMPROCESSESVIEWER) xxwidgets_scanresults_init(widget);
    if (kind == XXWIDGETS_DATA_STRUCT_TABLE) xxwidgets_data_struct_table_init(widget);
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
    free(widget->items);
    xxwidgets_archiveview_dispose(widget);
    xxwidgets_archivebrowser_dispose(widget);
    xxwidgets_combobox_dispose(widget);
    xxwidgets_scanresults_dispose(widget);
    xxwidgets_binaryviewer_dispose(widget);
    xxwidgets_debugger_table_dispose(widget);
    free(widget->processes);
    xxwidgets_data_struct_table_dispose(widget);
    xxwidgets_entropy_table_dispose(widget);
    xxwidgets_canvas_dispose(widget);
    xxwidgets_treeview_dispose(widget);
    xxwidgets_shortcuts_dispose(widget);
    xxwidgets_hexview_dispose(widget);
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
    if (!widget || !valid_rect(rect, widget->kind == XXWIDGETS_WINDOW)) return XXWIDGETS_INVALID_ARGUMENT;
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
    if (widget && widget->kind == XXWIDGETS_HEXVIEW) return xxwidgets_hexview_selection_input(widget, value);
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

xxwidgets_status xxwidgets_clipboard_set_text(xxwidgets_widget *owner, const char *text)
{
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !xxwidgets_valid_utf8(text)) return XXWIDGETS_INVALID_ARGUMENT;
    if (!owner->app->ops->copy_text) return XXWIDGETS_UNAVAILABLE;
    return owner->app->ops->copy_text(owner, text);
}

xxwidgets_status xxwidgets_clipboard_get_text(xxwidgets_widget *owner, char *text, size_t capacity, size_t *required)
{
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !required || (!text && capacity)) return XXWIDGETS_INVALID_ARGUMENT;
    *required = 0;
    return owner->app->ops->paste_text ? owner->app->ops->paste_text(owner, text, capacity, required) : XXWIDGETS_UNAVAILABLE;
}
