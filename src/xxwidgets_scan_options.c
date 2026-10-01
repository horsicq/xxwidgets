#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_scan_options.h"

#include <limits.h>
#include <stdlib.h>

struct xxwidgets_scan_options_widget {
    xxwidgets_widget *owner;
    xxwidgets_widget *controls[XXWIDGETS_SCAN_OPTIONS_CONTROL_COUNT];
};

static int valid_options(const xxwidgets_scan_options *options)
{
    return options && !(options->flags & ~63u) && !(options->databases & ~3u);
}

static int valid_bounds(xxwidgets_rect bounds)
{
    return bounds.x >= 0 && bounds.y >= 0 && bounds.width >= 60 && bounds.height >= 13 &&
        bounds.x <= INT_MAX - bounds.width && bounds.y <= INT_MAX - bounds.height;
}

static void option_rects(xxwidgets_rect bounds, xxwidgets_rect *rects)
{
    int first = (bounds.width - 2) / 2, second = bounds.width - first - 2;
    size_t i;
    for (i = 0; i < 6; ++i)
        rects[i] = (xxwidgets_rect){bounds.x, bounds.y + 1 + (int)i * 2, first, 1};
    for (i = 6; i < 8; ++i)
        rects[i] = (xxwidgets_rect){bounds.x + first + 2, bounds.y + 1 + (int)(i - 6) * 2, second, 1};
    rects[XXWIDGETS_SCAN_OPTIONS_FLAGS_LABEL] = (xxwidgets_rect){bounds.x, bounds.y, first, 1};
    rects[XXWIDGETS_SCAN_OPTIONS_DATABASES_LABEL] = (xxwidgets_rect){bounds.x + first + 2, bounds.y, second, 1};
    rects[XXWIDGETS_SCAN_OPTIONS_MAIN_DATABASE_LABEL] = (xxwidgets_rect){bounds.x + first + 2, bounds.y + 6, second, 2};
}

static void recover_control(xxwidgets_widget *control)
{
    ++control->app->syncing;
    control->app->ops->sync(control);
    --control->app->syncing;
}

xxwidgets_status xxwidgets_scan_options_init(xxwidgets_scan_options *options)
{
    if (!options) return XXWIDGETS_INVALID_ARGUMENT;
    options->flags = XXWIDGETS_SCAN_DEEP | XXWIDGETS_SCAN_HEURISTIC | XXWIDGETS_SCAN_VERBOSE;
    options->databases = XXWIDGETS_SCAN_DATABASE_EXTRA | XXWIDGETS_SCAN_DATABASE_CUSTOM;
    return XXWIDGETS_OK;
}

xxwidgets_widget *xxwidgets_scan_options_widget_control(
    const xxwidgets_scan_options_widget *widget, xxwidgets_scan_options_control_id control)
{
    return widget && control >= 0 && control < XXWIDGETS_SCAN_OPTIONS_CONTROL_COUNT
        ? widget->controls[control] : NULL;
}

xxwidgets_status xxwidgets_scan_options_widget_get(
    const xxwidgets_scan_options_widget *widget, xxwidgets_scan_options *options)
{
    xxwidgets_scan_options result = {0};
    size_t i;
    if (!widget || !options) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < 8; ++i) {
        int value;
        xxwidgets_status status = xxwidgets_widget_get_value(widget->controls[i], &value);
        if (status != XXWIDGETS_OK) return status;
        if (value) {
            if (i < 6) result.flags |= 1u << i;
            else result.databases |= 1u << (i - 6);
        }
    }
    *options = result;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_scan_options_widget_set(
    xxwidgets_scan_options_widget *widget, const xxwidgets_scan_options *options)
{
    int previous[8];
    size_t i;
    if (!widget || !valid_options(options)) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < 8; ++i) previous[i] = widget->controls[i]->value;
    for (i = 0; i < 8; ++i) {
        unsigned int mask = i < 6 ? options->flags : options->databases;
        int value = (int)((mask >> (i < 6 ? i : i - 6)) & 1u);
        xxwidgets_status status = xxwidgets_widget_set_value(widget->controls[i], value);
        if (status != XXWIDGETS_OK) {
            size_t j;
            for (j = 0; j < i; ++j) widget->controls[j]->value = previous[j];
            for (j = 0; j < i; ++j) recover_control(widget->controls[j]);
            return status;
        }
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_scan_options_widget_create(xxwidgets_widget *owner,
    xxwidgets_rect bounds, xxwidgets_scan_options_widget **out_widget)
{
    static const char *const labels[XXWIDGETS_SCAN_OPTIONS_CONTROL_COUNT] = {
        "Deep scan", "Heuristic scan", "Verbose results", "Aggressive scan",
        "Hide unknown results", "Format text report", "Extra database", "Custom database",
        "Scan flags", "Databases", "Main database is always used"
    };
    xxwidgets_scan_options_widget *widget;
    xxwidgets_scan_options defaults;
    xxwidgets_rect rects[XXWIDGETS_SCAN_OPTIONS_CONTROL_COUNT];
    xxwidgets_status status;
    size_t i;
    if (!out_widget) return XXWIDGETS_INVALID_ARGUMENT;
    *out_widget = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !valid_bounds(bounds))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing)
        return XXWIDGETS_BUSY;
    widget = (xxwidgets_scan_options_widget *)calloc(1, sizeof(*widget));
    if (!widget) return XXWIDGETS_OUT_OF_MEMORY;
    widget->owner = owner;
    option_rects(bounds, rects);
    for (i = 0; i < XXWIDGETS_SCAN_OPTIONS_CONTROL_COUNT; ++i) {
        status = xxwidgets_widget_create(owner->app, owner,
            i < 8 ? XXWIDGETS_CHECKBOX : XXWIDGETS_LABEL, labels[i], rects[i], &widget->controls[i]);
        if (status != XXWIDGETS_OK) goto failed;
    }
    xxwidgets_scan_options_init(&defaults);
    status = xxwidgets_scan_options_widget_set(widget, &defaults);
    if (status != XXWIDGETS_OK) goto failed;
    *out_widget = widget;
    return XXWIDGETS_OK;
failed:
    xxwidgets_scan_options_widget_destroy(widget);
    return status;
}

xxwidgets_status xxwidgets_scan_options_widget_destroy(xxwidgets_scan_options_widget *widget)
{
    size_t i;
    if (!widget) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->owner->app->dispatch_depth || widget->owner->app->polling || widget->owner->app->syncing)
        return XXWIDGETS_BUSY;
    for (i = 0; i < XXWIDGETS_SCAN_OPTIONS_CONTROL_COUNT; ++i) {
        xxwidgets_status status;
        if (!widget->controls[i]) continue;
        status = xxwidgets_widget_destroy(widget->controls[i]);
        if (status != XXWIDGETS_OK) return status;
        widget->controls[i] = NULL;
    }
    free(widget);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_scan_options_widget_set_rect(
    xxwidgets_scan_options_widget *widget, xxwidgets_rect bounds)
{
    xxwidgets_rect rects[XXWIDGETS_SCAN_OPTIONS_CONTROL_COUNT], previous[XXWIDGETS_SCAN_OPTIONS_CONTROL_COUNT];
    size_t i;
    if (!widget || !valid_bounds(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    option_rects(bounds, rects);
    for (i = 0; i < XXWIDGETS_SCAN_OPTIONS_CONTROL_COUNT; ++i) previous[i] = widget->controls[i]->rect;
    for (i = 0; i < XXWIDGETS_SCAN_OPTIONS_CONTROL_COUNT; ++i) {
        xxwidgets_status status = xxwidgets_widget_set_rect(widget->controls[i], rects[i]);
        if (status != XXWIDGETS_OK) {
            size_t j;
            for (j = 0; j < i; ++j) widget->controls[j]->rect = previous[j];
            for (j = 0; j < i; ++j) recover_control(widget->controls[j]);
            return status;
        }
    }
    return XXWIDGETS_OK;
}

typedef struct scan_options_state {
    xxwidgets_widget *window, *ok, *cancel;
    int done, accepted;
} scan_options_state;

static void scan_options_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    scan_options_state *state = (scan_options_state *)user;
    (void)app;
    if (event->widget == state->window && event->type == XXWIDGETS_EVENT_CLOSE) state->done = 1;
    else if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == state->ok) { state->accepted = 1; state->done = 1; }
        else if (event->widget == state->cancel) state->done = 1;
    }
}

xxwidgets_status xxwidgets_scan_options_dialog(xxwidgets_widget *owner,
    const char *title, xxwidgets_scan_options *options, int *accepted)
{
    xxwidgets_scan_options_widget *form = NULL;
    xxwidgets_widget *previous_focus = NULL, *control;
    xxwidgets_scan_options selected = {0};
    xxwidgets_event_fn previous_event;
    void *previous_user;
    xxwidgets_app *app;
    scan_options_state state = {0};
    xxwidgets_status status, cleanup;
    int owner_enabled, owner_disabled = 0, modal_attached = 0;
    if (accepted) *accepted = 0;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !title || !accepted || !valid_options(options) ||
        owner->rect.x > INT_MAX - 66 || owner->rect.y > INT_MAX - 21)
        return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit)
        return XXWIDGETS_BUSY;
    previous_event = app->on_event; previous_user = app->user_data;
    owner_enabled = owner->enabled;
    for (control = app->widgets; control; control = control->next)
        if (control->parent == owner && xxwidgets_focusable(control)) { previous_focus = control; break; }
    app->on_event = scan_options_event; app->user_data = &state;
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title,
        (xxwidgets_rect){owner->rect.x + 2, owner->rect.y + 2, 64, 19}, &state.window);
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_window = state.window;
    status = xxwidgets_scan_options_widget_create(state.window, (xxwidgets_rect){2, 1, 60, 13}, &form);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_scan_options_widget_set(form, options);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON, "OK",
        (xxwidgets_rect){38, 15, 10, 2}, &state.ok);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON, "Cancel",
        (xxwidgets_rect){50, 15, 12, 2}, &state.cancel);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_widget_set_enabled(owner, 0);
    if (status != XXWIDGETS_OK) goto finish;
    owner_disabled = 1;
    if (app->ops->modal_owner) {
        status = app->ops->modal_owner(state.window, owner, 1);
        if (status != XXWIDGETS_OK) goto finish;
        modal_attached = 1;
    }
    status = xxwidgets_widget_focus(form->controls[XXWIDGETS_SCAN_OPTIONS_DEEP]);
    while (status == XXWIDGETS_OK && !state.done && !app->quit) status = xxwidgets_app_poll(app, 30);
    if (status == XXWIDGETS_OK && state.accepted && !app->quit)
        status = xxwidgets_scan_options_widget_get(form, &selected);
    else state.accepted = 0;
finish:
    if (owner_disabled) {
        cleanup = xxwidgets_widget_set_enabled(owner, owner_enabled);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (modal_attached) {
        cleanup = app->ops->modal_owner(state.window, owner, 0);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (form) {
        cleanup = xxwidgets_scan_options_widget_destroy(form);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (state.window) {
        cleanup = xxwidgets_widget_destroy(state.window);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    app->modal_window = NULL;
    app->on_event = previous_event; app->user_data = previous_user;
    if (previous_focus) xxwidgets_widget_focus(previous_focus);
    if (status == XXWIDGETS_OK && state.accepted) { *options = selected; *accepted = 1; }
    return status;
}
