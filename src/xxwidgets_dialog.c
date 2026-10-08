#include "xxwidgets_internal.h"
#include <stdlib.h>
#include <string.h>

typedef struct options_state {
    xxwidgets_widget *window, *ok, *cancel;
    int done, accepted;
} options_state;

static void options_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    options_state *state = (options_state *)user;
    (void)app;
    if (event->widget == state->window && event->type == XXWIDGETS_EVENT_CLOSE)
        state->done = 1;
    else if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == state->ok) { state->accepted = 1; state->done = 1; }
        else if (event->widget == state->cancel) state->done = 1;
    }
}

xxwidgets_status xxwidgets_options_dialog(xxwidgets_widget *owner, const char *title,
    xxwidgets_option *options, size_t count, int *accepted)
{
    xxwidgets_widget *controls[16] = {0}, *previous_focus = NULL, *widget;
    int values[16], owner_enabled, owner_disabled = 0, modal_attached = 0;
    size_t i;
    xxwidgets_rect bounds;
    xxwidgets_app *app;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    options_state state = {0};
    xxwidgets_status status = XXWIDGETS_OK, cleanup;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !title || !accepted ||
        (!options && count) || count > 16) return XXWIDGETS_INVALID_ARGUMENT;
    *accepted = 0;
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit)
        return XXWIDGETS_BUSY;
    for (i = 0; i < count; ++i)
        if (!options[i].label || (options[i].value != 0 && options[i].value != 1))
            return XXWIDGETS_INVALID_ARGUMENT;
    previous_event = app->on_event; previous_user = app->user_data;
    owner_enabled = owner->enabled;
    for (widget = app->widgets; widget; widget = widget->next)
        if (widget->parent == owner && xxwidgets_focusable(widget)) { previous_focus = widget; break; }
    app->on_event = options_event; app->user_data = &state;
    bounds.x = owner->rect.x + 2; bounds.y = owner->rect.y + 2;
    bounds.width = 60; bounds.height = (int)count * 2 + 5;
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title, bounds, &state.window);
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_window = state.window;
    for (i = 0; i < count; ++i) {
        bounds.x = 2; bounds.y = 1 + (int)i * 2; bounds.width = 56; bounds.height = 1;
        status = xxwidgets_widget_create(app, state.window, XXWIDGETS_CHECKBOX,
            options[i].label, bounds, &controls[i]);
        if (status != XXWIDGETS_OK) goto finish;
        status = xxwidgets_widget_set_value(controls[i], options[i].value);
        if (status != XXWIDGETS_OK) goto finish;
    }
    bounds.y = (int)count * 2 + 2; bounds.x = 34; bounds.width = 10;
    status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON, "OK", bounds, &state.ok);
    if (status != XXWIDGETS_OK) goto finish;
    bounds.x = 46;
    status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON, "Cancel", bounds, &state.cancel);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_widget_set_enabled(owner, 0);
    if (status != XXWIDGETS_OK) goto finish;
    owner_disabled = 1;
    if (app->ops->modal_owner) {
        status = app->ops->modal_owner(state.window, owner, 1);
        if (status != XXWIDGETS_OK) goto finish;
        modal_attached = 1;
    }
    status = xxwidgets_widget_focus(count ? controls[0] : state.ok);
    while (status == XXWIDGETS_OK && !state.done && !app->quit)
        status = xxwidgets_app_poll(app, 30);
    if (status == XXWIDGETS_OK && state.accepted && !app->quit) {
        for (i = 0; i < count; ++i) {
            status = xxwidgets_widget_get_value(controls[i], &values[i]);
            if (status != XXWIDGETS_OK) break;
        }
    } else state.accepted = 0;
finish:
    if (owner_disabled) {
        cleanup = xxwidgets_widget_set_enabled(owner, owner_enabled);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (modal_attached) {
        cleanup = app->ops->modal_owner(state.window, owner, 0);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (state.window) {
        cleanup = xxwidgets_widget_destroy(state.window);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    app->modal_window = NULL;
    app->on_event = previous_event; app->user_data = previous_user;
    if (previous_focus) xxwidgets_widget_focus(previous_focus);
    if (status == XXWIDGETS_OK && state.accepted) {
        for (i = 0; i < count; ++i) options[i].value = values[i];
        *accepted = 1;
    }
    return status;
}

xxwidgets_status xxwidgets_file_dialog(xxwidgets_widget *owner, xxwidgets_file_dialog_mode mode,
    const char *title, const char *initial, char **path, int *accepted)
{
    xxwidgets_status status;
    xxwidgets_app *app;
    if (path) *path = NULL;
    if (accepted) *accepted = 0;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !path || !accepted ||
        (mode != XXWIDGETS_FILE_DIALOG_OPEN && mode != XXWIDGETS_FILE_DIALOG_SAVE) ||
        (title && !xxwidgets_valid_utf8(title))) return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->quit ||
        (app->modal_window && app->modal_window != owner)) return XXWIDGETS_BUSY;
    if (!app->ops->choose_file) return XXWIDGETS_UNAVAILABLE;
    status = app->ops->choose_file(owner, mode, title, initial, path, accepted);
    /* Cancellation, failure and an empty selection all leave no path behind. */
    if (status != XXWIDGETS_OK || !*accepted || !*path || !**path) {
        free(*path);
        *path = NULL;
        *accepted = 0;
    }
    return status;
}

int xxwidgets_file_dialog_available(const xxwidgets_app *app)
{
    return app && app->ops && app->ops->choose_file;
}
