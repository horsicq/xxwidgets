/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_entropy.h"
#include <limits.h>
#include <stdlib.h>

typedef struct entropy_modal {
    xxwidgets_widget *window, *close, *copy, *copied;
    xxwidgets_entropy *view;
    int done;
    xxwidgets_status failure;
} entropy_modal;

/* "Copy all": the current tab's visible table, as Save would write it. A
 * failure is reported next to the button and keeps the dialog open. */
static xxwidgets_status entropy_copy_all(entropy_modal *modal)
{
    xxwidgets_app *app = modal->window->app;
    const char *message = XXW_TR(app, "Cannot copy");
    size_t required = 0;
    char *text = NULL;
    if (xxwidgets_entropy_export_csv(modal->view, NULL, 0, &required) == XXWIDGETS_OK && required && (text = malloc(required)) != NULL &&
        xxwidgets_entropy_export_csv(modal->view, text, required, NULL) == XXWIDGETS_OK)
        message = xxwidgets_clipboard_set_text(modal->window, text) == XXWIDGETS_OK ? XXW_TR(app, "Copied") : XXW_TR(app, "Clipboard unavailable");
    free(text);
    return xxwidgets_widget_set_text(modal->copied, message);
}

static void entropy_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    entropy_modal *modal = user;
    xxwidgets_status status = XXWIDGETS_OK;
    (void)app;
    if ((event->type == XXWIDGETS_EVENT_CLOSE && event->widget == modal->window) ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == modal->close) ||
        (event->type == XXWIDGETS_EVENT_SHORTCUT && event->value == 1)) modal->done = 1;
    else if (event->type == XXWIDGETS_EVENT_CLICK && event->widget == modal->copy && modal->view) status = entropy_copy_all(modal);
    else if (event->type == XXWIDGETS_EVENT_RESIZE && event->widget == modal->window && modal->view && modal->close) {
        xxwidgets_rect bounds = {0};
        status = xxwidgets_widget_get_rect(modal->window, &bounds);
        /* Backends may report less than the window minimum: lay out at the
         * minimum and let the window clip, rather than fail the dialog. */
        if (bounds.width < modal->window->min_columns) bounds.width = modal->window->min_columns;
        if (bounds.height < modal->window->min_rows) bounds.height = modal->window->min_rows;
        if (status == XXWIDGETS_OK)
            status = xxwidgets_entropy_set_rect(modal->view, (xxwidgets_rect){1, 1, bounds.width - 2, bounds.height - 6});
        if (status == XXWIDGETS_OK)
            status = xxwidgets_widget_set_rect(modal->copy, (xxwidgets_rect){1, bounds.height - 3, 14, 2});
        if (status == XXWIDGETS_OK)
            status = xxwidgets_widget_set_rect(modal->copied, (xxwidgets_rect){16, bounds.height - 3, bounds.width - 30, 2});
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
    /* Return focus to the owner's focused control, else its first one. */
    for (widget = app->widgets; widget; widget = widget->next)
        if (widget->parent == owner && xxwidgets_focusable(widget)) {
            if (!focus) focus = widget;
            if (xxwidgets_widget_has_focus(widget) == 1) { focus = widget; break; }
        }
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
        status = xxwidgets_widget_create(app, modal.window, XXWIDGETS_BUTTON, XXW_TR(app, "Copy all"), (xxwidgets_rect){1, 31, 14, 2}, &modal.copy);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, modal.window, XXWIDGETS_LABEL, "", (xxwidgets_rect){16, 31, 70, 2}, &modal.copied);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, modal.window, XXWIDGETS_BUTTON, XXW_TR(app, "Close"), (xxwidgets_rect){87, 31, 12, 2}, &modal.close);
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
