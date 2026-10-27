/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets/xxwidgets_memory_map.h"
#include "xxwidgets_internal.h"
#include <limits.h>

typedef struct memory_map_modal {
    xxwidgets_widget *window, *close;
    xxwidgets_memory_map *view;
    int done;
    xxwidgets_status failure;
} memory_map_modal;

static void memory_map_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    memory_map_modal *modal = user;
    xxwidgets_status status = XXWIDGETS_OK;
    (void)app;
    if (event->type == XXWIDGETS_EVENT_SHORTCUT && event->widget == modal->window && modal->view) {
        if (event->value == 3)
            status = xxwidgets_widget_focus(xxwidgets_memory_map_control(modal->view, XXWIDGETS_MEMORY_MAP_FIND));
        if (status != XXWIDGETS_OK) {
            modal->failure = status;
            modal->done = 1;
        }
        if (event->value != 1)
            return;
    }
    if ((event->type == XXWIDGETS_EVENT_CLOSE && event->widget == modal->window) ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == modal->close) ||
        (event->type == XXWIDGETS_EVENT_SHORTCUT && event->value == 1))
        modal->done = 1;
    else if (event->type == XXWIDGETS_EVENT_RESIZE && event->widget == modal->window && modal->view) {
        xxwidgets_rect bounds;
        status = xxwidgets_widget_get_rect(modal->window, &bounds);
        if (status == XXWIDGETS_OK)
            status = xxwidgets_memory_map_set_rect(modal->view, (xxwidgets_rect){1, 1, bounds.width - 2, bounds.height - 6});
        if (status == XXWIDGETS_OK)
            status = xxwidgets_widget_set_rect(modal->close, (xxwidgets_rect){bounds.width - 13, bounds.height - 3, 12, 2});
    } else if (modal->view)
        status = xxwidgets_memory_map_handle_event(modal->view, event);
    if (status != XXWIDGETS_OK) {
        modal->failure = status;
        modal->done = 1;
    }
}

xxwidgets_status xxwidgets_memory_map_dialog(xxwidgets_widget *owner, const char *title, const xxwidgets_memory_map_data *data)
{
    memory_map_modal modal = {0};
    xxwidgets_app *app;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    xxwidgets_widget *focus = NULL, *widget, *previous_default;
    xxwidgets_status status, cleanup;
    int enabled, disabled = 0, attached = 0;
    const xxwidgets_shortcut shortcuts[] = {{"Esc", 1}, {"Ctrl+G", 3}, {"Meta+G", 3}};
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !title || !xxwidgets_valid_utf8(title) || owner->rect.x > INT_MAX - 2 ||
        owner->rect.y > INT_MAX - 2)
        return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit)
        return XXWIDGETS_BUSY;
    previous_default = app->modal_default;
    app->modal_default = NULL;
    previous_event = app->on_event;
    previous_user = app->user_data;
    enabled = owner->enabled;
    for (widget = app->widgets; widget; widget = widget->next)
        if (widget->parent == owner && xxwidgets_focusable(widget)) {
            focus = widget;
            break;
        }
    app->on_event = memory_map_event;
    app->user_data = &modal;
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title, (xxwidgets_rect){owner->rect.x + 2, owner->rect.y + 2, 108, 32},
                                     &modal.window);
    if (status != XXWIDGETS_OK)
        goto finish;
    app->modal_window = modal.window;
    status = xxwidgets_window_set_minimum_size(modal.window, 98, 26);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_memory_map_create(modal.window, (xxwidgets_rect){1, 1, 106, 26}, &modal.view);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_memory_map_set_data(modal.view, data);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, modal.window, XXWIDGETS_BUTTON, "Close", (xxwidgets_rect){95, 29, 12, 2}, &modal.close);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_window_set_shortcuts(modal.window, shortcuts, sizeof(shortcuts) / sizeof(shortcuts[0]));
    if (status != XXWIDGETS_OK)
        goto finish;
    status = xxwidgets_widget_set_enabled(owner, 0);
    if (status != XXWIDGETS_OK)
        goto finish;
    disabled = 1;
    if (app->ops->modal_owner) {
        status = app->ops->modal_owner(modal.window, owner, 1);
        if (status != XXWIDGETS_OK)
            goto finish;
        attached = 1;
    }
    status = xxwidgets_widget_focus(xxwidgets_memory_map_control(modal.view, XXWIDGETS_MEMORY_MAP_TABLE));
    while (status == XXWIDGETS_OK && !modal.done && !app->quit) {
        status = xxwidgets_app_poll(app, 30);
        if (status == XXWIDGETS_OK && !modal.done && !app->quit)
            status = xxwidgets_memory_map_process(modal.view);
    }
    if (status == XXWIDGETS_OK)
        status = modal.failure;
finish:
    if (attached) {
        cleanup = app->ops->modal_owner(modal.window, owner, 0);
        if (status == XXWIDGETS_OK)
            status = cleanup;
    }
    if (modal.view) {
        cleanup = xxwidgets_memory_map_destroy(modal.view);
        if (status == XXWIDGETS_OK)
            status = cleanup;
    }
    if (modal.window) {
        cleanup = xxwidgets_widget_destroy(modal.window);
        if (status == XXWIDGETS_OK)
            status = cleanup;
    }
    app->modal_window = NULL;
    app->modal_default = previous_default;
    app->on_event = previous_event;
    app->user_data = previous_user;
    if (disabled) {
        cleanup = xxwidgets_widget_set_enabled(owner, enabled);
        if (status == XXWIDGETS_OK)
            status = cleanup;
    }
    if (focus)
        (void)xxwidgets_widget_focus(focus);
    return status;
}
