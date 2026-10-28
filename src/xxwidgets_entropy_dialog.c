/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_entropy.h"
#include <limits.h>

typedef struct entropy_modal {
    xxwidgets_widget *window, *close;
    xxwidgets_entropy *view;
    int done;
    xxwidgets_status failure;
} entropy_modal;

static void entropy_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    entropy_modal *modal = user;
    xxwidgets_status status = XXWIDGETS_OK;
    (void)app;
    if ((event->type == XXWIDGETS_EVENT_CLOSE && event->widget == modal->window) ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == modal->close) ||
        (event->type == XXWIDGETS_EVENT_SHORTCUT && event->value == 1)) modal->done = 1;
    else if (event->type == XXWIDGETS_EVENT_RESIZE && event->widget == modal->window && modal->view) {
        xxwidgets_rect bounds;
        status = xxwidgets_widget_get_rect(modal->window, &bounds);
        if (status == XXWIDGETS_OK)
            status = xxwidgets_entropy_set_rect(modal->view, (xxwidgets_rect){1, 1, bounds.width - 2, bounds.height - 6});
        if (status == XXWIDGETS_OK)
            status = xxwidgets_widget_set_rect(modal->close, (xxwidgets_rect){bounds.width - 13, bounds.height - 3, 12, 2});
    } else if (modal->view) status = xxwidgets_entropy_handle_event(modal->view, event);
    if (status != XXWIDGETS_OK) { modal->failure = status; modal->done = 1; }
}

xxwidgets_status xxwidgets_entropy_dialog(xxwidgets_widget *owner, const char *title,
    const xxwidgets_entropy_statistics *statistics)
{
    entropy_modal modal = {0};
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
    app->on_event = entropy_event; app->user_data = &modal;
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title,
        (xxwidgets_rect){owner->rect.x + 2, owner->rect.y + 2, 100, 34}, &modal.window);
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_window = modal.window;
    status = xxwidgets_window_set_minimum_size(modal.window, 90, 33);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_entropy_create(modal.window, (xxwidgets_rect){1, 1, 98, 28}, &modal.view);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_entropy_set_statistics(modal.view, statistics);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, modal.window, XXWIDGETS_BUTTON, "Close", (xxwidgets_rect){87, 31, 12, 2}, &modal.close);
    if (status == XXWIDGETS_OK) status = xxwidgets_window_set_shortcuts(modal.window, shortcuts, 1);
    if (status != XXWIDGETS_OK) goto finish;
    /* Export remains available to embedding hosts. This read-only modal has
     * no destination policy, so do not present buttons with no action. */
    status = xxwidgets_widget_set_visible(xxwidgets_entropy_control(modal.view, XXWIDGETS_ENTROPY_SAVE), 0);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_visible(xxwidgets_entropy_control(modal.view, XXWIDGETS_ENTROPY_SAVE_DIAGRAM), 0);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_widget_set_enabled(owner, 0);
    if (status != XXWIDGETS_OK) goto finish;
    disabled = 1;
    status = xxwidgets_window_set_modality(modal.window, owner, XXWIDGETS_WINDOW_MODAL);
    if (status != XXWIDGETS_OK) goto finish;
    attached = 1;
    status = xxwidgets_widget_focus(xxwidgets_entropy_control(modal.view, XXWIDGETS_ENTROPY_REGIONS_TABLE));
    while (status == XXWIDGETS_OK && !modal.done && !app->quit) status = xxwidgets_app_poll(app, 30);
    if (status == XXWIDGETS_OK) status = modal.failure;
finish:
    if (attached) { cleanup = xxwidgets_window_set_modality(modal.window, NULL, XXWIDGETS_MODELESS); if (status == XXWIDGETS_OK) status = cleanup; }
    if (modal.view) { cleanup = xxwidgets_entropy_destroy(modal.view); if (status == XXWIDGETS_OK) status = cleanup; }
    if (modal.window) { cleanup = xxwidgets_widget_destroy(modal.window); if (status == XXWIDGETS_OK) status = cleanup; }
    app->modal_window = NULL; app->on_event = previous_event; app->user_data = previous_user;
    if (disabled) { cleanup = xxwidgets_widget_set_enabled(owner, enabled); if (status == XXWIDGETS_OK) status = cleanup; }
    if (focus) (void)xxwidgets_widget_focus(focus);
    return status;
}
