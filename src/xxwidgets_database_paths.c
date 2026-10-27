/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_database_paths.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct paths_state {
    xxwidgets_widget *window, *edits[3], *browse[3], *error, *ok, *cancel, *defaults;
    int done, action, accepted;
} paths_state;

static int valid_path(const char *path)
{
    size_t i;
    if (!path) return 0;
    for (i = 0; i < 16384 && path[i]; ++i) {}
    return i < 16384 && xxwidgets_valid_utf8(path);
}

static void paths_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    paths_state *state = user;
    (void)app;
    if ((event->type == XXWIDGETS_EVENT_CLOSE && event->widget == state->window) ||
        (event->type == XXWIDGETS_EVENT_SHORTCUT && event->value == 1) ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == state->cancel)) state->done = 1;
    else if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == state->ok) state->action = 1;
        else if (event->widget == state->defaults) state->action = 2;
        else for (int i = 0; i < 3; ++i)
            if (event->widget == state->browse[i]) state->action = 3 + i;
    }
}

static xxwidgets_status read_path(xxwidgets_widget *edit, char **path)
{
    size_t size = 0;
    xxwidgets_status status = xxwidgets_widget_get_text(edit, NULL, 0, &size);
    *path = NULL;
    if (status != XXWIDGETS_OK) return status;
    if (!size || size > 16384) return XXWIDGETS_INVALID_ARGUMENT;
    *path = malloc(size);
    if (!*path) return XXWIDGETS_OUT_OF_MEMORY;
    status = xxwidgets_widget_get_text(edit, *path, size, NULL);
    if (status == XXWIDGETS_OK && !valid_path(*path)) status = XXWIDGETS_INVALID_ARGUMENT;
    if (status != XXWIDGETS_OK) { free(*path); *path = NULL; }
    return status;
}

xxwidgets_status xxwidgets_database_paths_dialog(xxwidgets_widget *owner, const char *title,
    const char *const paths[3], const xxwidgets_database_paths_callbacks *callbacks, void *user, int *accepted)
{
    const char *labels[] = {"Main database", "Extra database", "Custom database"};
    const xxwidgets_shortcut shortcuts[] = {{"Esc", 1}};
    paths_state state = {0};
    xxwidgets_app *app;
    xxwidgets_widget *focus = NULL, *widget, *previous_default;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    xxwidgets_status status, cleanup;
    int enabled, disabled = 0, attached = 0;
    if (accepted) *accepted = 0;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !title || !xxwidgets_valid_utf8(title) ||
        !accepted || !paths || !callbacks || !callbacks->apply || owner->rect.x > INT_MAX - 2 || owner->rect.y > INT_MAX - 2)
        return XXWIDGETS_INVALID_ARGUMENT;
    for (int i = 0; i < 3; ++i) if (!valid_path(paths[i])) return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit) return XXWIDGETS_BUSY;
    previous_event = app->on_event; previous_user = app->user_data; previous_default = app->modal_default;
    enabled = owner->enabled;
    for (widget = app->widgets; widget; widget = widget->next)
        if (widget->parent == owner && xxwidgets_focusable(widget)) { focus = widget; break; }
    app->on_event = paths_event; app->user_data = &state; app->modal_default = NULL;
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title,
        (xxwidgets_rect){owner->rect.x + 2, owner->rect.y + 2, 100, 22}, &state.window);
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_window = state.window;
    status = xxwidgets_window_set_minimum_size(state.window, 100, 22);
    for (int i = 0; status == XXWIDGETS_OK && i < 3; ++i) {
        xxwidgets_widget *label;
        status = xxwidgets_widget_create(app, state.window, XXWIDGETS_LABEL, labels[i], (xxwidgets_rect){2, 2 + i * 4, 94, 1}, &label);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(app, state.window, XXWIDGETS_EDIT, paths[i],
            (xxwidgets_rect){2, 3 + i * 4, 78, 2}, &state.edits[i]);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON, "Browse...",
            (xxwidgets_rect){82, 3 + i * 4, 16, 2}, &state.browse[i]);
        if (status == XXWIDGETS_OK && !callbacks->browse) status = xxwidgets_widget_set_enabled(state.browse[i], 0);
    }
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(app, state.window, XXWIDGETS_LABEL,
        "Empty paths use the default signature folders.", (xxwidgets_rect){2, 15, 96, 1}, &widget);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(app, state.window, XXWIDGETS_LABEL,
        "", (xxwidgets_rect){2, 16, 96, 2}, &state.error);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON,
        "Defaults", (xxwidgets_rect){2, 19, 16, 2}, &state.defaults);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON,
        "OK", (xxwidgets_rect){66, 19, 14, 2}, &state.ok);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON,
        "Cancel", (xxwidgets_rect){82, 19, 16, 2}, &state.cancel);
    if (status == XXWIDGETS_OK) status = xxwidgets_window_set_shortcuts(state.window, shortcuts, 1);
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_default = state.ok;
    status = xxwidgets_widget_set_enabled(owner, 0);
    if (status != XXWIDGETS_OK) goto finish;
    disabled = 1;
    if (app->ops->modal_owner) {
        status = app->ops->modal_owner(state.window, owner, 1);
        if (status != XXWIDGETS_OK) goto finish;
        attached = 1;
    }
    status = xxwidgets_widget_focus(state.edits[0]);
    while (status == XXWIDGETS_OK && !state.done && !app->quit) {
        status = xxwidgets_app_poll(app, 30);
        if (status != XXWIDGETS_OK || state.done || app->quit) break;
        int action = state.action; state.action = 0;
        if (action == 2) {
            for (int i = 0; status == XXWIDGETS_OK && i < 3; ++i) status = xxwidgets_widget_set_text(state.edits[i], "");
        } else if (action >= 3 && action <= 5) {
            char *initial = NULL, *path = NULL; int chosen = 0;
            status = read_path(state.edits[action - 3], &initial);
            if (status == XXWIDGETS_OK && callbacks->browse)
                status = callbacks->browse(state.window, initial, &path, &chosen, user);
            if (status == XXWIDGETS_OK && chosen) status = valid_path(path) ?
                xxwidgets_widget_set_text(state.edits[action - 3], path) : XXWIDGETS_INVALID_ARGUMENT;
            free(initial); free(path);
        } else if (action == 1) {
            char *values[3] = {0}, error[512] = {0};
            for (int i = 0; status == XXWIDGETS_OK && i < 3; ++i) status = read_path(state.edits[i], &values[i]);
            if (status == XXWIDGETS_OK) {
                const char *borrowed[3] = {values[0], values[1], values[2]};
                int applied = callbacks->apply(borrowed, error, sizeof(error), user);
                error[sizeof(error) - 1] = 0;
                if (applied) state.done = state.accepted = 1;
                else status = xxwidgets_widget_set_text(state.error,
                    error[0] && xxwidgets_valid_utf8(error) ? error : "Cannot load databases. Check the main folder and try again.");
            }
            for (int i = 0; i < 3; ++i) free(values[i]);
        }
        if (status == XXWIDGETS_INVALID_ARGUMENT) {
            status = xxwidgets_widget_set_text(state.error, "Enter a valid UTF-8 path shorter than 16384 bytes.");
        }
    }
finish:
    if (attached) { cleanup = app->ops->modal_owner(state.window, owner, 0); if (status == XXWIDGETS_OK) status = cleanup; }
    if (state.window) { cleanup = xxwidgets_widget_destroy(state.window); if (status == XXWIDGETS_OK) status = cleanup; }
    app->modal_window = NULL; app->modal_default = previous_default; app->on_event = previous_event; app->user_data = previous_user;
    if (disabled) { cleanup = xxwidgets_widget_set_enabled(owner, enabled); if (status == XXWIDGETS_OK) status = cleanup; }
    if (focus) (void)xxwidgets_widget_focus(focus);
    if (status == XXWIDGETS_OK && state.accepted) *accepted = 1;
    return status;
}

struct xxwidgets_database_paths_widget {
    xxwidgets_widget *owner;
    xxwidgets_widget *controls[XXWIDGETS_DATABASE_PATHS_CONTROL_COUNT];
    xxwidgets_database_paths_callbacks callbacks;
    void *user;
    int pending_browse; /* 1..3 = folder index + 1. */
};

static int widget_bounds_valid(xxwidgets_rect bounds)
{
    return bounds.x >= 0 && bounds.y >= 0 && bounds.width >= 48 && bounds.height >= 10 &&
        bounds.x <= INT_MAX - bounds.width && bounds.y <= INT_MAX - bounds.height;
}

static void widget_rects(xxwidgets_rect bounds, xxwidgets_rect *rects)
{
    for (int i = 0; i < 3; ++i) {
        int y = bounds.y + i * 3;
        rects[i * 3] = (xxwidgets_rect){bounds.x, y, bounds.width, 1};
        rects[i * 3 + 1] = (xxwidgets_rect){bounds.x, y + 1, bounds.width - 14, 1};
        rects[i * 3 + 2] = (xxwidgets_rect){bounds.x + bounds.width - 12, y + 1, 12, 1};
    }
    rects[XXWIDGETS_DATABASE_PATHS_NOTE] = (xxwidgets_rect){bounds.x, bounds.y + 9, bounds.width, 1};
}

xxwidgets_status xxwidgets_database_paths_widget_create(xxwidgets_widget *owner, xxwidgets_rect bounds,
    const xxwidgets_database_paths_callbacks *callbacks, void *user, xxwidgets_database_paths_widget **out_widget)
{
    static const char *const labels[XXWIDGETS_DATABASE_PATHS_CONTROL_COUNT] = {
        XXW_N_("Main database"), "", XXW_N_("Browse..."), XXW_N_("Extra database"), "", XXW_N_("Browse..."),
        XXW_N_("Custom database"), "", XXW_N_("Browse..."), XXW_N_("Empty paths use the default signature folders.")};
    xxwidgets_rect rects[XXWIDGETS_DATABASE_PATHS_CONTROL_COUNT];
    xxwidgets_database_paths_widget *widget;
    xxwidgets_status status = XXWIDGETS_OK;
    if (!out_widget) return XXWIDGETS_INVALID_ARGUMENT;
    *out_widget = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !widget_bounds_valid(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing) return XXWIDGETS_BUSY;
    widget = (xxwidgets_database_paths_widget *)calloc(1, sizeof(*widget));
    if (!widget) return XXWIDGETS_OUT_OF_MEMORY;
    widget->owner = owner;
    if (callbacks) widget->callbacks = *callbacks;
    widget->user = user;
    widget_rects(bounds, rects);
    for (int i = 0; status == XXWIDGETS_OK && i < XXWIDGETS_DATABASE_PATHS_CONTROL_COUNT; ++i) {
        xxwidgets_kind kind = i == XXWIDGETS_DATABASE_PATHS_NOTE || i % 3 == 0 ? XXWIDGETS_LABEL : i % 3 == 1 ? XXWIDGETS_EDIT : XXWIDGETS_BUTTON;
        status = xxwidgets_widget_create(owner->app, owner, kind, labels[i][0] ? XXW_TR(owner->app, labels[i]) : "", rects[i], &widget->controls[i]);
        if (status == XXWIDGETS_OK && kind == XXWIDGETS_BUTTON && !widget->callbacks.browse)
            status = xxwidgets_widget_set_enabled(widget->controls[i], 0);
    }
    if (status != XXWIDGETS_OK) {
        xxwidgets_database_paths_widget_destroy(widget);
        return status;
    }
    *out_widget = widget;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_database_paths_widget_destroy(xxwidgets_database_paths_widget *widget)
{
    if (!widget) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->owner->app->dispatch_depth || widget->owner->app->polling || widget->owner->app->syncing) return XXWIDGETS_BUSY;
    for (int i = 0; i < XXWIDGETS_DATABASE_PATHS_CONTROL_COUNT; ++i) {
        if (!widget->controls[i]) continue;
        xxwidgets_status status = xxwidgets_widget_destroy(widget->controls[i]);
        if (status != XXWIDGETS_OK) return status;
        widget->controls[i] = NULL;
    }
    free(widget);
    return XXWIDGETS_OK;
}

xxwidgets_widget *xxwidgets_database_paths_widget_control(const xxwidgets_database_paths_widget *widget,
    xxwidgets_database_paths_control_id control)
{
    return widget && control >= 0 && control < XXWIDGETS_DATABASE_PATHS_CONTROL_COUNT ? widget->controls[control] : NULL;
}

xxwidgets_status xxwidgets_database_paths_widget_set(xxwidgets_database_paths_widget *widget, const char *const paths[3])
{
    xxwidgets_status status = XXWIDGETS_OK;
    if (!widget || !paths) return XXWIDGETS_INVALID_ARGUMENT;
    for (int i = 0; i < 3; ++i)
        if (paths[i] && !valid_path(paths[i])) return XXWIDGETS_INVALID_ARGUMENT;
    for (int i = 0; status == XXWIDGETS_OK && i < 3; ++i)
        status = xxwidgets_widget_set_text(widget->controls[i * 3 + 1], paths[i] ? paths[i] : "");
    return status;
}

xxwidgets_status xxwidgets_database_paths_widget_get(const xxwidgets_database_paths_widget *widget, size_t index,
    char *buffer, size_t capacity, size_t *required)
{
    size_t size = 0;
    xxwidgets_status status;
    if (!widget || index > 2) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_widget_get_text(widget->controls[index * 3 + 1], NULL, 0, &size);
    if (status != XXWIDGETS_OK) return status;
    if (!size || size > 16384) return XXWIDGETS_INVALID_ARGUMENT;
    return xxwidgets_widget_get_text(widget->controls[index * 3 + 1], buffer, capacity, required);
}

xxwidgets_status xxwidgets_database_paths_widget_set_rect(xxwidgets_database_paths_widget *widget, xxwidgets_rect bounds)
{
    xxwidgets_rect rects[XXWIDGETS_DATABASE_PATHS_CONTROL_COUNT], previous[XXWIDGETS_DATABASE_PATHS_CONTROL_COUNT];
    if (!widget || !widget_bounds_valid(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    widget_rects(bounds, rects);
    for (int i = 0; i < XXWIDGETS_DATABASE_PATHS_CONTROL_COUNT; ++i) previous[i] = widget->controls[i]->rect;
    for (int i = 0; i < XXWIDGETS_DATABASE_PATHS_CONTROL_COUNT; ++i) {
        xxwidgets_status status = xxwidgets_widget_set_rect(widget->controls[i], rects[i]);
        if (status != XXWIDGETS_OK) {
            for (int j = 0; j < i; ++j) (void)xxwidgets_widget_set_rect(widget->controls[j], previous[j]);
            return status;
        }
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_database_paths_widget_handle_event(xxwidgets_database_paths_widget *widget, const xxwidgets_event *event)
{
    if (!widget || !event) return XXWIDGETS_INVALID_ARGUMENT;
    if (event->type == XXWIDGETS_EVENT_CLICK)
        for (int i = 0; i < 3; ++i)
            if (event->widget == widget->controls[i * 3 + 2]) widget->pending_browse = i + 1;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_database_paths_widget_process(xxwidgets_database_paths_widget *widget)
{
    char *initial = NULL, *path = NULL;
    int index, chosen = 0;
    xxwidgets_status status;
    if (!widget) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->owner->app->dispatch_depth || widget->owner->app->polling || widget->owner->app->syncing) return XXWIDGETS_BUSY;
    index = widget->pending_browse - 1;
    widget->pending_browse = 0;
    if (index < 0 || !widget->callbacks.browse) return XXWIDGETS_OK;
    status = read_path(widget->controls[index * 3 + 1], &initial);
    if (status == XXWIDGETS_OK) status = widget->callbacks.browse(widget->owner, initial, &path, &chosen, widget->user);
    if (status == XXWIDGETS_OK && chosen && path)
        status = valid_path(path) ? xxwidgets_widget_set_text(widget->controls[index * 3 + 1], path) : XXWIDGETS_INVALID_ARGUMENT;
    free(initial);
    free(path);
    if (status == XXWIDGETS_OK) (void)xxwidgets_widget_focus(widget->controls[index * 3 + 1]);
    return status;
}
