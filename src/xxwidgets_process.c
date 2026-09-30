#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_process.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#ifdef XXWIDGETS_TEST_CLOCK
/* Deterministic delay tests supply a monotonic clock without sleeping. */
extern xxwidgets_status xxwidgets_process_clock(uint64_t *milliseconds);
#else
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
static xxwidgets_status xxwidgets_process_clock(uint64_t *milliseconds)
{
#ifdef _WIN32
    *milliseconds = (uint64_t)GetTickCount64();
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return XXWIDGETS_PLATFORM_ERROR;
    *milliseconds = (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
#endif
    return XXWIDGETS_OK;
}
#endif

typedef struct process_state {
    xxwidgets_widget *owner, *window, *cancel;
    xxwidgets_widget *bars[XX_PD_LEVELS], *labels[XX_PD_LEVELS], *elapsed;
    int stop_requested;
} process_state;

static void process_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    process_state *state = (process_state *)user;
    (void)app;
    if ((event->type == XXWIDGETS_EVENT_CLOSE &&
            (event->widget == state->window || event->widget == state->owner)) ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == state->cancel))
        state->stop_requested = 1;
}

static xxwidgets_status process_update(xxwidgets_app *app, process_state *state,
    xx_pd_struct *progress, xxwidgets_process_update_fn update, void *user, int *finished)
{
    xxwidgets_status status;
    state->stop_requested |= progress->is_stop != 0;
    progress->is_stop = state->stop_requested != 0;
    *finished = 0;
    ++app->dispatch_depth;
    status = update(user, state->stop_requested, progress, finished);
    --app->dispatch_depth;
    state->stop_requested |= progress->is_stop != 0;
    progress->is_stop = state->stop_requested != 0;
    return status;
}

static int process_percent(uint64_t current, uint64_t total)
{
    int low = 0, high = 100;
    if (!total) return 0;
    if (current >= total) return 100;
    /* Compare against ceil(total * percent / 100), without a 64-bit product
     * overflow or rounding 99.999... up to 100 on platforms with 64-bit double. */
    while (high - low > 1) {
        int middle = (low + high) / 2;
        uint64_t threshold = (total / 100) * (unsigned int)middle +
            ((total % 100) * (unsigned int)middle + 99) / 100;
        if (current >= threshold) low = middle;
        else high = middle;
    }
    return low;
}

static void process_status(char *text, const char *status, size_t capacity)
{
    size_t i;
    /* xxfclib copies status into a fixed byte buffer. Bound the read even if
     * a producer omitted NUL, and tolerate a UTF-8 sequence cut at its end. */
    memcpy(text, status, capacity);
    text[capacity] = '\0';
    while (text[0] && !xxwidgets_valid_utf8(text)) text[strlen(text) - 1] = '\0';
    for (i = 0; text[i]; ++i)
        if ((unsigned char)text[i] < 32 || text[i] == 127) text[i] = ' ';
}

static xxwidgets_status process_render(process_state *state, const xx_pd_struct *progress, uint64_t elapsed)
{
    xxwidgets_rect rect = {2, 1, 60, 1};
    xxwidgets_status status;
    char text[160], label[sizeof(progress->records[0].status) + 1];
    int i, row = 0;
    for (i = 0; i < XX_PD_LEVELS; ++i) {
        const xx_pd_record *record = &progress->records[i];
        int busy = record->is_busy != 0;
        if (busy) {
            process_status(label, record->status, sizeof(record->status));
            if (record->total)
                snprintf(text, sizeof(text), "[%" PRIu64 "/%" PRIu64 "] %s", record->current, record->total, label);
            else snprintf(text, sizeof(text), "[%" PRIu64 "/?] %s", record->current, label);
            status = xxwidgets_widget_set_text(state->labels[i], text);
            if (status != XXWIDGETS_OK) return status;
            rect.y = 1 + row * 3;
            status = xxwidgets_widget_set_rect(state->labels[i], rect);
            if (status != XXWIDGETS_OK) return status;
            ++rect.y;
            status = xxwidgets_widget_set_rect(state->bars[i], rect);
            if (status != XXWIDGETS_OK) return status;
            status = xxwidgets_widget_set_value(state->bars[i], process_percent(record->current, record->total));
            if (status != XXWIDGETS_OK) return status;
            ++row;
        }
        status = xxwidgets_widget_set_visible(state->bars[i], busy);
        if (status != XXWIDGETS_OK) return status;
        status = xxwidgets_widget_set_visible(state->labels[i], busy);
        if (status != XXWIDGETS_OK) return status;
    }
    snprintf(text, sizeof(text), "Elapsed: %" PRIu64 ".%03u s", elapsed / 1000, (unsigned int)(elapsed % 1000));
    status = xxwidgets_widget_set_text(state->elapsed, text);
    if (status != XXWIDGETS_OK) return status;
    rect.y = row * 3 + 1; rect.width = 42;
    status = xxwidgets_widget_set_rect(state->elapsed, rect);
    if (status != XXWIDGETS_OK) return status;
    rect.x = 48; rect.width = 14; rect.height = 2;
    status = xxwidgets_widget_set_rect(state->cancel, rect);
    if (status != XXWIDGETS_OK) return status;
    if (state->stop_requested) {
        status = xxwidgets_widget_set_text(state->cancel, "Stopping...");
        if (status != XXWIDGETS_OK) return status;
        status = xxwidgets_widget_set_enabled(state->cancel, 0);
        if (status != XXWIDGETS_OK) return status;
    }
    rect = state->window->rect; rect.height = row * 3 + 5;
    /* Native centering or user movement can put the frame above/left of the
     * primary display. Public logical rectangles require nonnegative bounds. */
    if (rect.x < 0) rect.x = 0;
    if (rect.y < 0) rect.y = 0;
    if (rect.x > 32767) rect.x = 32767;
    if (rect.y > 32767) rect.y = 32767;
    return xxwidgets_widget_set_rect(state->window, rect);
}

static xxwidgets_status process_create(xxwidgets_app *app, process_state *state)
{
    xxwidgets_rect rect = {2, 1, 60, 1};
    xxwidgets_status status;
    int i;
    for (i = 0; i < XX_PD_LEVELS; ++i) {
        status = xxwidgets_widget_create(app, state->window, XXWIDGETS_LABEL, "", rect, &state->labels[i]);
        if (status != XXWIDGETS_OK) return status;
        status = xxwidgets_widget_create(app, state->window, XXWIDGETS_PROGRESS, "", rect, &state->bars[i]);
        if (status != XXWIDGETS_OK) return status;
    }
    status = xxwidgets_widget_create(app, state->window, XXWIDGETS_LABEL, "", rect, &state->elapsed);
    if (status != XXWIDGETS_OK) return status;
    rect.x = 48; rect.width = 14; rect.height = 2;
    status = xxwidgets_widget_create(app, state->window, XXWIDGETS_BUTTON, "Cancel", rect, &state->cancel);
    if (status == XXWIDGETS_OK) app->modal_default = state->cancel;
    return status;
}

xxwidgets_status xxwidgets_process_dialog(xxwidgets_widget *owner, const char *title,
    xx_pd_struct *progress, xxwidgets_process_update_fn update, void *user)
{
    process_state state = {0};
    xxwidgets_app *app;
    xxwidgets_widget *previous_focus = NULL, *widget;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    xxwidgets_rect rect;
    xxwidgets_status status, cleanup;
    uint64_t started, now;
    int owner_enabled, modal_attached = 0, finished = 0, updated = 0;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !title || !xxwidgets_valid_utf8(title) || !progress || !update)
        return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit)
        return XXWIDGETS_BUSY;
    status = xxwidgets_process_clock(&started);
    if (status != XXWIDGETS_OK) return status;
    state.owner = owner;
    previous_event = app->on_event; previous_user = app->user_data;
    owner_enabled = owner->enabled;
    for (widget = app->widgets; widget; widget = widget->next)
        if (widget->parent == owner && xxwidgets_focusable(widget)) { previous_focus = widget; break; }
    app->on_event = process_event; app->user_data = &state;
    status = xxwidgets_widget_set_enabled(owner, 0);
    while (status == XXWIDGETS_OK && !app->quit) {
        updated = 1;
        status = process_update(app, &state, progress, update, user, &finished);
        if (status != XXWIDGETS_OK || finished || app->quit) break;
        status = xxwidgets_process_clock(&now);
        if (status != XXWIDGETS_OK) break;
        if (!state.window && now - started > 1000) {
            rect = owner->rect;
            rect.width = 64; rect.height = 20;
            if (app->backend == XXWIDGETS_BACKEND_TUI) {
                if (owner->rect.width > rect.width) rect.x += (owner->rect.width - rect.width) / 2;
                if (owner->rect.height > rect.height) rect.y += (owner->rect.height - rect.height) / 2;
                if (rect.x > 32767) rect.x = 32767;
                if (rect.y > 32767) rect.y = 32767;
            }
            status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title, rect, &state.window);
            if (status != XXWIDGETS_OK) break;
            app->modal_window = state.window;
            status = xxwidgets_widget_set_visible(state.window, 0);
            if (status != XXWIDGETS_OK) break;
            status = process_create(app, &state);
            if (status != XXWIDGETS_OK) break;
            status = process_render(&state, progress, now - started);
            if (status != XXWIDGETS_OK) break;
            if (app->ops->modal_owner) {
                status = app->ops->modal_owner(state.window, owner, 1);
                if (status != XXWIDGETS_OK) break;
                modal_attached = 1;
            }
            status = xxwidgets_widget_set_visible(state.window, 1);
            if (status != XXWIDGETS_OK) break;
            if (!state.stop_requested) status = xxwidgets_widget_focus(state.cancel);
        } else if (state.window) status = process_render(&state, progress, now - started);
        if (status == XXWIDGETS_OK) status = xxwidgets_app_poll(app, 30);
    }
    if (updated && !finished) {
        /* UI shutdown/failure must not silently abandon the caller's worker.
         * Joining belongs to the caller; deliver the stop before returning. */
        state.stop_requested = 1;
        cleanup = process_update(app, &state, progress, update, user, &finished);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    cleanup = xxwidgets_widget_set_enabled(owner, owner_enabled);
    if (status == XXWIDGETS_OK) status = cleanup;
    if (modal_attached) {
        cleanup = app->ops->modal_owner(state.window, owner, 0);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (state.window) {
        cleanup = xxwidgets_widget_destroy(state.window);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    app->modal_window = NULL; app->modal_default = NULL;
    app->on_event = previous_event; app->user_data = previous_user;
    if (previous_focus) xxwidgets_widget_focus(previous_focus);
    return status;
}
