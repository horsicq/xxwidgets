/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_strings.h"
#include <limits.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct strings_modal {
    xxwidgets_widget *window, *close, *path, *summary;
    xxwidgets_strings *view;
    int done, pending;
    xxwidgets_status failure;
} strings_modal;

static void strings_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    strings_modal *modal = user;
    xxwidgets_status status = XXWIDGETS_OK;
    (void)app;
    if (modal->view && ((event->type == XXWIDGETS_EVENT_CLICK && event->widget == xxwidgets_strings_control(modal->view, XXWIDGETS_STRINGS_VIEW)) ||
        (event->type == XXWIDGETS_EVENT_ACTIVATE && event->widget == xxwidgets_strings_control(modal->view, XXWIDGETS_STRINGS_RESULTS)))) {
        modal->pending = 1; return;
    }
    if (modal->view && event->type == XXWIDGETS_EVENT_CLICK && event->widget == xxwidgets_strings_control(modal->view, XXWIDGETS_STRINGS_SAVE)) {
        modal->pending = 2; return;
    }
    if ((event->type == XXWIDGETS_EVENT_CLOSE && event->widget == modal->window) ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == modal->close) ||
        (event->type == XXWIDGETS_EVENT_SHORTCUT && event->value == 1)) modal->done = 1;
    else if (event->type == XXWIDGETS_EVENT_RESIZE && event->widget == modal->window && modal->view) {
        xxwidgets_rect bounds;
        status = xxwidgets_widget_get_rect(modal->window, &bounds);
        if (status == XXWIDGETS_OK)
            status = xxwidgets_strings_set_rect(modal->view, (xxwidgets_rect){1, 5, bounds.width - 2, bounds.height - 10});
        if (status == XXWIDGETS_OK)
            status = xxwidgets_widget_set_rect(modal->path, (xxwidgets_rect){1, 1, bounds.width - 2, 1});
        if (status == XXWIDGETS_OK)
            status = xxwidgets_widget_set_rect(modal->summary, (xxwidgets_rect){1, 3, bounds.width - 2, 1});
        if (status == XXWIDGETS_OK)
            status = xxwidgets_widget_set_rect(modal->close, (xxwidgets_rect){bounds.width - 13, bounds.height - 3, 12, 2});
    } else if (modal->view) status = xxwidgets_strings_handle_event(modal->view, event);
    if (status != XXWIDGETS_OK) { modal->failure = status; modal->done = 1; }
}

static xxwidgets_status save_csv(strings_modal *modal, xxwidgets_strings_save_fn save, void *user)
{
    char *path = NULL, *text = NULL;
    int accepted = 0;
    size_t required = 0;
    xxwidgets_status status = xxwidgets_file_dialog(modal->window, XXWIDGETS_FILE_DIALOG_SAVE,
        "Save visible strings as CSV", "strings.csv", &path, &accepted);
    if (status == XXWIDGETS_OK && accepted) {
        status = xxwidgets_strings_export_csv(modal->view, NULL, 0, &required);
        if (status == XXWIDGETS_OK) { text = malloc(required); if (!text) status = XXWIDGETS_OUT_OF_MEMORY; }
        if (status == XXWIDGETS_OK) status = xxwidgets_strings_export_csv(modal->view, text, required, NULL);
        if (status == XXWIDGETS_OK) status = save(path, text, required - 1, user);
    }
    free(path); free(text);
    if (status != XXWIDGETS_OK || accepted)
        return xxwidgets_widget_set_text(xxwidgets_strings_control(modal->view, XXWIDGETS_STRINGS_DETAILS),
            status == XXWIDGETS_OK ? "CSV saved" : "Cannot save CSV");
    return status;
}

xxwidgets_status xxwidgets_strings_dialog(xxwidgets_widget *owner, const char *title,
    const char *path, uint64_t size, const xxwidgets_string_record *records, size_t count,
    xxwidgets_strings_save_fn save, void *user)
{
    strings_modal modal = {0};
    xxwidgets_app *app;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    xxwidgets_widget *focus = NULL, *widget, *previous_default;
    char description[128];
    xxwidgets_status status, cleanup;
    int enabled, disabled = 0, attached = 0;
    const xxwidgets_shortcut shortcuts[] = {{"Esc", 1}};
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !title || !xxwidgets_valid_utf8(title) ||
        !path || !xxwidgets_valid_utf8(path) || owner->rect.x > INT_MAX - 2 || owner->rect.y > INT_MAX - 2) return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit) return XXWIDGETS_BUSY;
    previous_default = app->modal_default; app->modal_default = NULL;
    previous_event = app->on_event; previous_user = app->user_data; enabled = owner->enabled;
    for (widget = app->widgets; widget; widget = widget->next)
        if (widget->parent == owner && xxwidgets_focusable(widget)) { focus = widget; break; }
    app->on_event = strings_event; app->user_data = &modal;
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title,
        (xxwidgets_rect){owner->rect.x + 2, owner->rect.y + 2, 100, 30}, &modal.window);
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_window = modal.window;
    status = xxwidgets_window_set_minimum_size(modal.window, 74, 24);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, modal.window, XXWIDGETS_LABEL, path, (xxwidgets_rect){1, 1, 98, 1}, &modal.path);
    snprintf(description, sizeof(description), "%" PRIu64 " bytes | %zu strings | Select a row to copy or view its full text", size, count);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, modal.window, XXWIDGETS_LABEL, description, (xxwidgets_rect){1, 3, 98, 1}, &modal.summary);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_strings_create(modal.window, (xxwidgets_rect){1, 5, 98, 20}, &modal.view);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_strings_set_records(modal.view, records, count);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, modal.window, XXWIDGETS_BUTTON, "Close", (xxwidgets_rect){87, 27, 12, 2}, &modal.close);
    if (status == XXWIDGETS_OK) status = xxwidgets_window_set_shortcuts(modal.window, shortcuts, 1);
    if (status != XXWIDGETS_OK) goto finish;
    if (!save || !xxwidgets_file_dialog_available(app)) {
        status = xxwidgets_widget_set_visible(xxwidgets_strings_control(modal.view, XXWIDGETS_STRINGS_SAVE), 0);
        if (status != XXWIDGETS_OK) goto finish;
    }
    status = xxwidgets_widget_set_enabled(owner, 0);
    if (status != XXWIDGETS_OK) goto finish;
    disabled = 1;
    if (app->ops->modal_owner) {
        status = app->ops->modal_owner(modal.window, owner, 1);
        if (status != XXWIDGETS_OK) goto finish;
        attached = 1;
    }
    status = xxwidgets_widget_focus(xxwidgets_strings_control(modal.view, XXWIDGETS_STRINGS_RESULTS));
    while (status == XXWIDGETS_OK && !modal.done && !app->quit) {
        status = xxwidgets_app_poll(app, 30);
        if (status == XXWIDGETS_OK && !modal.done && !app->quit && modal.pending) {
            int action = modal.pending; modal.pending = 0;
            if (action == 1) {
                size_t index;
                xxwidgets_string_record record;
                status = xxwidgets_strings_get_selection(modal.view, &index, &record);
                if (status == XXWIDGETS_OK && index != SIZE_MAX)
                    status = xxwidgets_text_dialog(modal.window, "Decoded string", record.text);
            } else if (save) status = save_csv(&modal, save, user);
        }
    }
    if (status == XXWIDGETS_OK) status = modal.failure;
finish:
    if (attached) { cleanup = app->ops->modal_owner(modal.window, owner, 0); if (status == XXWIDGETS_OK) status = cleanup; }
    if (modal.view) { cleanup = xxwidgets_strings_destroy(modal.view); if (status == XXWIDGETS_OK) status = cleanup; }
    if (modal.window) { cleanup = xxwidgets_widget_destroy(modal.window); if (status == XXWIDGETS_OK) status = cleanup; }
    app->modal_window = NULL; app->modal_default = previous_default; app->on_event = previous_event; app->user_data = previous_user;
    if (disabled) { cleanup = xxwidgets_widget_set_enabled(owner, enabled); if (status == XXWIDGETS_OK) status = cleanup; }
    if (focus) (void)xxwidgets_widget_focus(focus);
    return status;
}
