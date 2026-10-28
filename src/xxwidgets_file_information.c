/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_file_information.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct xxwidgets_file_information {
    xxwidgets_widget *owner;
    xxwidgets_widget *controls[XXWIDGETS_FILE_INFORMATION_CONTROL_COUNT];
    xxwidgets_data_struct_view *view;
    char *path, *summary;
};

static int valid_bounds(xxwidgets_rect bounds)
{
    return bounds.x >= 0 && bounds.y >= 0 && bounds.width >= 72 && bounds.height >= 12 &&
        bounds.x <= INT_MAX - bounds.width && bounds.y <= INT_MAX - bounds.height;
}

xxwidgets_status xxwidgets_file_information_set_rect(xxwidgets_file_information *information, xxwidgets_rect bounds)
{
    xxwidgets_status status;
    if (!information || !valid_bounds(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_widget_set_rect(information->controls[0], (xxwidgets_rect){bounds.x, bounds.y, bounds.width, 1});
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_set_rect(information->controls[1], (xxwidgets_rect){bounds.x, bounds.y + 2, bounds.width, 1});
    if (status == XXWIDGETS_OK)
        status = xxwidgets_data_struct_view_set_rect(information->view,
            (xxwidgets_rect){bounds.x, bounds.y + 4, bounds.width, bounds.height - 4});
    return status;
}

xxwidgets_status xxwidgets_file_information_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_file_information **out)
{
    xxwidgets_file_information *information;
    xxwidgets_status status;
    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !valid_bounds(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing) return XXWIDGETS_BUSY;
    information = calloc(1, sizeof(*information));
    if (!information) return XXWIDGETS_OUT_OF_MEMORY;
    information->owner = owner;
    status = xxwidgets_widget_create(owner->app, owner, XXWIDGETS_LABEL, "", (xxwidgets_rect){bounds.x, bounds.y, bounds.width, 1}, &information->controls[0]);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(owner->app, owner, XXWIDGETS_LABEL, "", (xxwidgets_rect){bounds.x, bounds.y + 2, bounds.width, 1}, &information->controls[1]);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_data_struct_view_create(owner, (xxwidgets_rect){bounds.x, bounds.y + 4, bounds.width, bounds.height - 4}, &information->view);
    if (status != XXWIDGETS_OK) {
        (void)xxwidgets_file_information_destroy(information);
        return status;
    }
    *out = information;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_file_information_destroy(xxwidgets_file_information *information)
{
    size_t i;
    xxwidgets_status status;
    if (!information) return XXWIDGETS_INVALID_ARGUMENT;
    if (information->owner->app->dispatch_depth || information->owner->app->polling || information->owner->app->syncing) return XXWIDGETS_BUSY;
    if (information->view) {
        status = xxwidgets_data_struct_view_destroy(information->view);
        if (status != XXWIDGETS_OK) return status;
        information->view = NULL;
    }
    for (i = 0; i < XXWIDGETS_FILE_INFORMATION_CONTROL_COUNT; ++i) if (information->controls[i]) {
        status = xxwidgets_widget_destroy(information->controls[i]);
        if (status != XXWIDGETS_OK) return status;
        information->controls[i] = NULL;
    }
    free(information->path); free(information->summary); free(information);
    return XXWIDGETS_OK;
}

xxwidgets_widget *xxwidgets_file_information_control(const xxwidgets_file_information *information, xxwidgets_file_information_control_id control)
{
    return information && control >= 0 && control < XXWIDGETS_FILE_INFORMATION_CONTROL_COUNT ? information->controls[control] : NULL;
}
xxwidgets_data_struct_view *xxwidgets_file_information_view(const xxwidgets_file_information *information)
{
    return information ? information->view : NULL;
}
xxwidgets_status xxwidgets_file_information_handle_event(xxwidgets_file_information *information, const xxwidgets_event *event)
{
    return information ? xxwidgets_data_struct_view_handle_event(information->view, event) : XXWIDGETS_INVALID_ARGUMENT;
}

xxwidgets_status xxwidgets_file_information_set_data(xxwidgets_file_information *information,
    const char *path, const char *file_type, int64_t size, const xxwidgets_data_struct_item *items, size_t count)
{
    char *new_path, *summary;
    size_t capacity;
    xxwidgets_status status;
    if (!information || !path || !file_type || !xxwidgets_valid_utf8(path) || !xxwidgets_valid_utf8(file_type) ||
        size < 0 || (!items && count) || strlen(file_type) > SIZE_MAX - 128) return XXWIDGETS_INVALID_ARGUMENT;
    capacity = strlen(file_type) + 128;
    new_path = xxwidgets_strdup(path); summary = malloc(capacity);
    if (!new_path || !summary) { free(new_path); free(summary); return XXWIDGETS_OUT_OF_MEMORY; }
    snprintf(summary, capacity, "%s | %" PRId64 " bytes | %zu structures | Select a structure to inspect its fields", file_type, size, count);
    status = xxwidgets_widget_set_text(information->controls[0], new_path);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(information->controls[1], summary);
    if (status == XXWIDGETS_OK) status = xxwidgets_data_struct_view_set_items(information->view, items, count);
    if (status != XXWIDGETS_OK) {
        (void)xxwidgets_widget_set_text(information->controls[0], information->path ? information->path : "");
        (void)xxwidgets_widget_set_text(information->controls[1], information->summary ? information->summary : "");
        free(new_path); free(summary);
        return status;
    }
    free(information->path); free(information->summary);
    information->path = new_path; information->summary = summary;
    return XXWIDGETS_OK;
}

typedef struct information_modal {
    xxwidgets_widget *window, *close;
    xxwidgets_file_information *information;
    int done;
    xxwidgets_status failure;
} information_modal;

static void information_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    information_modal *modal = user;
    xxwidgets_status status = XXWIDGETS_OK;
    (void)app;
    if ((event->type == XXWIDGETS_EVENT_CLOSE && event->widget == modal->window) ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == modal->close) ||
        (event->type == XXWIDGETS_EVENT_SHORTCUT && event->value == 1)) modal->done = 1;
    else if (event->type == XXWIDGETS_EVENT_RESIZE && event->widget == modal->window && modal->information) {
        xxwidgets_rect bounds;
        status = xxwidgets_widget_get_rect(modal->window, &bounds);
        if (status == XXWIDGETS_OK)
            status = xxwidgets_file_information_set_rect(modal->information, (xxwidgets_rect){1, 1, bounds.width - 2, bounds.height - 6});
        if (status == XXWIDGETS_OK)
            status = xxwidgets_widget_set_rect(modal->close, (xxwidgets_rect){bounds.width - 13, bounds.height - 3, 12, 2});
    } else if (modal->information) status = xxwidgets_file_information_handle_event(modal->information, event);
    if (status != XXWIDGETS_OK) { modal->failure = status; modal->done = 1; }
}

xxwidgets_status xxwidgets_file_information_dialog(xxwidgets_widget *owner, const char *title,
    const char *path, const char *file_type, int64_t size, const xxwidgets_data_struct_item *items, size_t count)
{
    information_modal modal = {0};
    xxwidgets_app *app;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    xxwidgets_widget *focus = NULL, *widget;
    xxwidgets_status status, cleanup;
    int enabled, disabled = 0, attached = 0;
    const xxwidgets_shortcut shortcuts[] = {{"Esc", 1}};
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !title || !xxwidgets_valid_utf8(title) ||
        owner->rect.x > INT_MAX - 2 || owner->rect.y > INT_MAX - 2) return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit) return XXWIDGETS_BUSY;
    previous_event = app->on_event; previous_user = app->user_data; enabled = owner->enabled;
    for (widget = app->widgets; widget; widget = widget->next)
        if (widget->parent == owner && xxwidgets_focusable(widget)) { focus = widget; break; }
    app->on_event = information_event; app->user_data = &modal;
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title,
        (xxwidgets_rect){owner->rect.x + 2, owner->rect.y + 2, 100, 30}, &modal.window);
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_window = modal.window;
    status = xxwidgets_window_set_minimum_size(modal.window, 74, 18);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_file_information_create(modal.window, (xxwidgets_rect){1, 1, 98, 24}, &modal.information);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_file_information_set_data(modal.information, path, file_type, size, items, count);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, modal.window, XXWIDGETS_BUTTON, "Close", (xxwidgets_rect){87, 27, 12, 2}, &modal.close);
    if (status == XXWIDGETS_OK) status = xxwidgets_window_set_shortcuts(modal.window, shortcuts, 1);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_widget_set_enabled(owner, 0);
    if (status != XXWIDGETS_OK) goto finish;
    disabled = 1;
    status = xxwidgets_window_set_modality(modal.window, owner, XXWIDGETS_WINDOW_MODAL);
    if (status != XXWIDGETS_OK) goto finish;
    attached = 1;
    status = xxwidgets_widget_focus(xxwidgets_data_struct_view_control(modal.information->view, XXWIDGETS_DATA_STRUCT_VIEW_LIST));
    while (status == XXWIDGETS_OK && !modal.done && !app->quit) status = xxwidgets_app_poll(app, 30);
    if (status == XXWIDGETS_OK) status = modal.failure;
finish:
    if (attached) { cleanup = xxwidgets_window_set_modality(modal.window, NULL, XXWIDGETS_MODELESS); if (status == XXWIDGETS_OK) status = cleanup; }
    if (modal.information) { cleanup = xxwidgets_file_information_destroy(modal.information); if (status == XXWIDGETS_OK) status = cleanup; }
    if (modal.window) { cleanup = xxwidgets_widget_destroy(modal.window); if (status == XXWIDGETS_OK) status = cleanup; }
    app->modal_window = NULL; app->on_event = previous_event; app->user_data = previous_user;
    if (disabled) { cleanup = xxwidgets_widget_set_enabled(owner, enabled); if (status == XXWIDGETS_OK) status = cleanup; }
    if (focus) (void)xxwidgets_widget_focus(focus);
    return status;
}
