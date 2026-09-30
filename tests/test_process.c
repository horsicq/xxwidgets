#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "process line %d: %s\n", __LINE__, #expr); exit(1); } } while (0)

enum { FAST, BOUNDARY, VISIBLE, CANCEL, CLOSE, CREATE_ERROR, SYNC_ERROR,
    POLL_ERROR, OWNER_ERROR, UPDATE_ERROR, QUIT, CLOCK_ERROR, DISABLE_ERROR };
static struct {
    xxwidgets_app *app;
    xxwidgets_widget *owner;
    const xxwidgets_backend_ops *original;
    xx_pd_struct progress;
    uint64_t now;
    int mode, phase, calls, polls, creates, fail_create, stop_calls, attached, detached, failed;
} fixture;

xxwidgets_status xxwidgets_process_clock(uint64_t *milliseconds)
{
    if (fixture.mode == CLOCK_ERROR) return XXWIDGETS_PLATFORM_ERROR;
    *milliseconds = fixture.now;
    return XXWIDGETS_OK;
}

static void seed(xx_pd_struct *progress)
{
    int i;
    for (i = 0; i < XX_PD_LEVELS; ++i) {
        progress->records[i].is_busy = true;
        memcpy(progress->records[i].status, "Working \xce\xbb", sizeof("Working \xce\xbb"));
    }
    progress->records[0].current = 50; progress->records[0].total = 100;
    progress->records[1].current = UINT64_MAX - 1; progress->records[1].total = UINT64_MAX;
    progress->records[2].current = UINT64_MAX; progress->records[2].total = 1;
    progress->records[3].current = 50; progress->records[3].total = 0;
    progress->records[4].current = 1; progress->records[4].total = 3;
    memset(progress->records[4].status, 'x', sizeof(progress->records[4].status));
    progress->records[4].status[63] = (char)0xe2; /* Truncated UTF-8, no NUL. */
}

static xxwidgets_status update(void *user, int stop, xx_pd_struct *progress, int *finished)
{
    int i;
    CHECK(user == &fixture && progress == &fixture.progress && !*finished);
    CHECK(!fixture.owner->enabled && fixture.app->dispatch_depth);
    CHECK(xxwidgets_process_dialog(fixture.owner, "nested", progress, update, user) == XXWIDGETS_BUSY);
    CHECK(xxwidgets_app_destroy(fixture.app) == XXWIDGETS_BUSY);
    CHECK(xxwidgets_widget_destroy(fixture.owner) == XXWIDGETS_BUSY);
    CHECK(xxwidgets_app_poll(fixture.app, 0) == XXWIDGETS_BUSY);
    ++fixture.calls;
    if (stop) {
        CHECK(progress->is_stop);
        ++fixture.stop_calls;
        progress->is_stop = false; /* A copied worker snapshot cannot lose the UI stop. */
        *finished = (fixture.mode != CANCEL && fixture.mode != CLOSE) || fixture.stop_calls > 1;
        return XXWIDGETS_OK;
    }
    seed(progress);
    if (fixture.mode == FAST) *finished = 1;
    else if (fixture.mode == BOUNDARY && fixture.now > 1000) *finished = 1;
    else if (fixture.mode == UPDATE_ERROR) return XXWIDGETS_UNAVAILABLE;
    else if (fixture.mode == VISIBLE) {
        if (fixture.phase == 1) {
            progress->records[0].is_busy = progress->records[3].is_busy = false;
            progress->records[1].current = 1;
            progress->records[2].current = 0; progress->records[2].total = 2;
            progress->records[4].current = progress->records[4].total = UINT64_MAX;
        } else if (fixture.phase == 2) {
            for (i = 0; i < XX_PD_LEVELS; ++i) progress->records[i].is_busy = false;
        } else if (fixture.phase == 3) {
            /* Explicit completion wins even if the last snapshot remains busy. */
            progress->last_error = 7;
            memcpy(progress->error_string, "Job error", sizeof("Job error"));
            *finished = 1;
        }
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status create(xxwidgets_widget *widget)
{
    xxwidgets_status status = fixture.original->create(widget);
    ++fixture.creates;
    CHECK(fixture.now > 1000); /* Nothing is created even at exactly one second. */
    if (fixture.mode == CREATE_ERROR && fixture.creates == fixture.fail_create)
        return XXWIDGETS_PLATFORM_ERROR;
    return status;
}

static xxwidgets_status sync_widget(xxwidgets_widget *widget)
{
    xxwidgets_status status = fixture.original->sync(widget);
    if (!fixture.failed && ((fixture.mode == SYNC_ERROR && widget->kind == XXWIDGETS_PROGRESS) ||
        (fixture.mode == DISABLE_ERROR && widget == fixture.owner && !widget->enabled))) {
        fixture.failed = 1;
        return XXWIDGETS_PLATFORM_ERROR;
    }
    return status;
}

static xxwidgets_status modal_owner(xxwidgets_widget *dialog, xxwidgets_widget *owner, int active)
{
    CHECK(dialog == fixture.app->modal_window && owner == fixture.owner);
    if (active) {
        CHECK(!owner->enabled && !dialog->visible);
        if (fixture.mode == OWNER_ERROR) return XXWIDGETS_PLATFORM_ERROR;
        if (fixture.mode == VISIBLE) { dialog->rect.x = -3; dialog->rect.y = -2; }
        ++fixture.attached;
    } else ++fixture.detached;
    return XXWIDGETS_OK;
}

static xxwidgets_status poll(xxwidgets_app *app, int timeout)
{
    xxwidgets_widget *widget;
    int bars = 0, labels = 0, row = 0;
    const int percentages[] = {50, 99, 100, 0, 33};
    CHECK(app == fixture.app && timeout == 30 && !fixture.owner->enabled);
    ++fixture.polls;
    CHECK(fixture.polls < 10);
    if (fixture.mode == QUIT) { xxwidgets_app_quit(app, 0); return XXWIDGETS_OK; }
    if (!app->modal_window) {
        CHECK(fixture.now <= 1000 && !app->modal_default);
        fixture.now += fixture.now == 0 ? 1000 : 1;
        return XXWIDGETS_OK;
    }
    CHECK(app->modal_window->visible && app->modal_default);
    if (fixture.mode == VISIBLE && fixture.phase) CHECK(app->modal_window->rect.x == 0 && app->modal_window->rect.y == 0);
    if (fixture.mode == POLL_ERROR) return XXWIDGETS_PLATFORM_ERROR;
    for (widget = app->widgets; widget; widget = widget->next) {
        if (widget->parent != app->modal_window) continue;
        if (widget->kind == XXWIDGETS_PROGRESS) {
            const xx_pd_record *record = &fixture.progress.records[bars];
            CHECK(widget->visible == (record->is_busy != 0));
            if (record->is_busy) {
                CHECK(widget->rect.y == 2 + row * 3);
                ++row;
                if (!fixture.phase) CHECK(widget->value == percentages[bars]);
                else if (fixture.mode == VISIBLE) CHECK(widget->value == (bars == 4 ? 100 : 0));
            }
            ++bars;
        } else if (widget->kind == XXWIDGETS_LABEL && labels < XX_PD_LEVELS) {
            CHECK(widget->visible == (fixture.progress.records[labels].is_busy != 0));
            CHECK(xxwidgets_valid_utf8(widget->text));
            if (!fixture.phase && labels == 0) CHECK(!strcmp(widget->text, "[50/100] Working \xce\xbb"));
            if (!fixture.phase && labels == 3) CHECK(!strcmp(widget->text, "[50/?] Working \xce\xbb"));
            if (!fixture.phase && labels == 4) CHECK(strlen(widget->text) == strlen("[1/3] ") + 63);
            ++labels;
        }
    }
    CHECK(bars == XX_PD_LEVELS && labels == XX_PD_LEVELS);
    CHECK(app->modal_window->rect.height == row * 3 + 5);
    if (fixture.mode == CANCEL || fixture.mode == CLOSE) {
        if (!fixture.phase) {
            if (fixture.mode == CLOSE) xxwidgets_emit(app->modal_window, XXWIDGETS_EVENT_CLOSE, 0);
            else xxwidgets_emit(app->modal_default, XXWIDGETS_EVENT_CLICK, 0);
        } else CHECK(!app->modal_default->enabled && !strcmp(app->modal_default->text, "Stopping..."));
    }
    ++fixture.phase; fixture.now += 30;
    return XXWIDGETS_OK;
}

static void run(int mode, int fail_create)
{
    xxwidgets_status expected = XXWIDGETS_OK;
    xxwidgets_event_fn previous_event = fixture.app->on_event;
    void *previous_user = fixture.app->user_data;
    xxwidgets_widget *tail = fixture.app->widgets;
    int owner_enabled = fixture.owner->enabled;
    while (tail->next) tail = tail->next;
    fixture.now = 0; fixture.mode = mode; fixture.phase = fixture.calls = fixture.polls = fixture.creates = 0;
    fixture.stop_calls = fixture.attached = fixture.detached = fixture.failed = 0;
    fixture.fail_create = fail_create;
    memset(&fixture.progress, 0, sizeof(fixture.progress));
    if (mode == UPDATE_ERROR) expected = XXWIDGETS_UNAVAILABLE;
    else if (mode >= CREATE_ERROR && mode != QUIT) expected = XXWIDGETS_PLATFORM_ERROR;
    CHECK(xxwidgets_process_dialog(fixture.owner, "Process", &fixture.progress, update, &fixture) == expected);
    CHECK(fixture.owner->enabled == owner_enabled && !tail->next && !fixture.app->modal_window && !fixture.app->modal_default);
    CHECK(fixture.app->on_event == previous_event && fixture.app->user_data == previous_user);
    CHECK(fixture.attached == fixture.detached);
    if (mode == FAST || mode == BOUNDARY) CHECK(!fixture.creates && !fixture.progress.is_stop);
    if (mode == BOUNDARY) CHECK(fixture.calls == 3 && fixture.polls == 2);
    if (mode == VISIBLE) CHECK(fixture.creates == 13 && fixture.phase == 3 && fixture.progress.last_error == 7 && !fixture.progress.is_stop);
    if (mode == CANCEL || mode == CLOSE) CHECK(fixture.progress.is_stop && fixture.stop_calls == 2 && fixture.phase == 2);
    if (mode >= CREATE_ERROR && mode != CLOCK_ERROR && mode != DISABLE_ERROR) CHECK(fixture.stop_calls == 1 && fixture.progress.is_stop);
    if (mode == CLOCK_ERROR || mode == DISABLE_ERROR) CHECK(!fixture.calls && !fixture.progress.is_stop);
    if (mode == QUIT) fixture.app->quit = 0;
}

void xxwidgets_test_process(xxwidgets_app *app, xxwidgets_widget *owner)
{
    xxwidgets_backend_ops ops = *app->ops;
    int i;
    fixture.app = app; fixture.owner = owner; fixture.original = app->ops;
    CHECK(xxwidgets_process_dialog(NULL, "Process", &fixture.progress, update, &fixture) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_process_dialog(owner, NULL, &fixture.progress, update, &fixture) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_process_dialog(owner, "\xff", &fixture.progress, update, &fixture) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_process_dialog(owner, "Process", NULL, update, &fixture) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_process_dialog(owner, "Process", &fixture.progress, NULL, &fixture) == XXWIDGETS_INVALID_ARGUMENT);
    ops.create = create; ops.sync = sync_widget; ops.poll = poll; ops.modal_owner = modal_owner;
    app->ops = &ops;
    run(FAST, 0); run(BOUNDARY, 0); run(VISIBLE, 0); run(CANCEL, 0); run(CLOSE, 0);
    for (i = 1; i <= 13; ++i) run(CREATE_ERROR, i);
    run(SYNC_ERROR, 0); run(POLL_ERROR, 0); run(OWNER_ERROR, 0); run(UPDATE_ERROR, 0);
    run(QUIT, 0); run(CLOCK_ERROR, 0); run(DISABLE_ERROR, 0);
    CHECK(xxwidgets_widget_set_enabled(owner, 0) == XXWIDGETS_OK);
    run(FAST, 0);
    CHECK(xxwidgets_widget_set_enabled(owner, 1) == XXWIDGETS_OK);
    app->ops = fixture.original;
}
