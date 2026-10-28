/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_disk_write.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/threads/xx_threads.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef XXWIDGETS_TEST_DISK_CLOCK
extern xxwidgets_status xxwidgets_disk_write_clock(uint64_t *milliseconds);
extern bool xxwidgets_disk_write_test_join(xx_thread_t *thread);
#else
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
static xxwidgets_status xxwidgets_disk_write_clock(uint64_t *milliseconds)
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

struct xxwidgets_disk_write_reporter {
    xx_mutex_t *lock;
    uint64_t worker_id;
    int files_level, bytes_level, current_file_level;
    xxwidgets_disk_write_progress state;
};
typedef struct disk_job {
    xxwidgets_disk_write_reporter reporter;
    xxwidgets_disk_write_worker_fn worker;
    void *user;
    xx_pd_struct progress;
    int stop, done, cancelled;
    volatile long exited;
    xxwidgets_status status;
} disk_job;
typedef struct disk_ui {
    xxwidgets_widget *owner, *window, *cancel;
    xxwidgets_widget *destination_label, *destination, *current_label, *current;
    xxwidgets_widget *files, *file_bar, *bytes, *byte_bar, *elapsed, *speed, *remaining, *status;
    char destination_text[XXWIDGETS_DISK_WRITE_PATH_CAPACITY];
    int stop;
    xxwidgets_status failure;
} disk_ui;

static int reporter_ready(const xxwidgets_disk_write_reporter *r)
{
    return r && r->lock && r->worker_id == xx_thread_current_id();
}
static int known_value(int value)
{
    return value == 0 || value == 1;
}
static void clean_controls(char *text)
{
    for (size_t i = 0; text[i]; ++i)
        if ((unsigned char)text[i] < 32 || text[i] == 127) text[i] = ' ';
}
xxwidgets_status xxwidgets_disk_write_report(xxwidgets_disk_write_reporter *r, const xxwidgets_disk_write_progress *p)
{
    xxwidgets_disk_write_progress copy;
    if (!reporter_ready(r) || !p || !memchr(p->current_file, 0, sizeof(p->current_file)) || !xxwidgets_valid_utf8(p->current_file) ||
        !known_value(p->bytes_total_known) || !known_value(p->files_total_known) || (p->bytes_total_known && p->bytes_done > p->bytes_total) ||
        (p->files_total_known && p->files_done > p->files_total))
        return XXWIDGETS_INVALID_ARGUMENT;
    copy = *p;
    clean_controls(copy.current_file);
    xx_mutex_lock(r->lock);
    r->state = copy;
    xx_mutex_unlock(r->lock);
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_disk_write_set_file(xxwidgets_disk_write_reporter *r, const char *path, uint64_t completed, uint64_t total, int known)
{
    char text[XXWIDGETS_DISK_WRITE_PATH_CAPACITY];
    if (!reporter_ready(r) || !xxwidgets_valid_utf8(path) || !known_value(known) || (known && completed > total)) return XXWIDGETS_INVALID_ARGUMENT;
    xxwidgets_process_status_tail(text, sizeof(text), path);
    clean_controls(text);
    xx_mutex_lock(r->lock);
    memcpy(r->state.current_file, text, strlen(text) + 1);
    r->state.files_done = completed;
    r->state.files_total = total;
    r->state.files_total_known = known;
    xx_mutex_unlock(r->lock);
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_disk_write_set_bytes(xxwidgets_disk_write_reporter *r, uint64_t completed, uint64_t total, int known)
{
    if (!reporter_ready(r) || !known_value(known) || (known && completed > total)) return XXWIDGETS_INVALID_ARGUMENT;
    xx_mutex_lock(r->lock);
    r->state.bytes_done = completed;
    r->state.bytes_total = total;
    r->state.bytes_total_known = known;
    xx_mutex_unlock(r->lock);
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_disk_write_bind_levels(xxwidgets_disk_write_reporter *r, int files, int bytes)
{
    if (!reporter_ready(r) || files < -1 || files >= XX_PD_LEVELS || bytes < -1 || bytes >= XX_PD_LEVELS) return XXWIDGETS_INVALID_ARGUMENT;
    xx_mutex_lock(r->lock);
    r->files_level = files;
    r->bytes_level = bytes;
    xx_mutex_unlock(r->lock);
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_disk_write_bind_current_file_level(xxwidgets_disk_write_reporter *r, int level)
{
    if (!reporter_ready(r) || level < -1 || level >= XX_PD_LEVELS) return XXWIDGETS_INVALID_ARGUMENT;
    xx_mutex_lock(r->lock);
    r->current_file_level = level;
    xx_mutex_unlock(r->lock);
    return XXWIDGETS_OK;
}
static bool disk_observe(const xx_pd_struct *pd, void *user)
{
    disk_job *job = user;
    xxwidgets_disk_write_reporter *r = &job->reporter;
    int stop;
    xx_mutex_lock(r->lock);
    job->progress = *pd;
    if (r->files_level >= 0 && pd->records[r->files_level].is_busy) {
        const xx_pd_record *p = &pd->records[r->files_level];
        r->state.files_done = p->current;
        r->state.files_total = p->total;
        r->state.files_total_known = p->total != 0;
    }
    if (r->bytes_level >= 0 && pd->records[r->bytes_level].is_busy) {
        const xx_pd_record *p = &pd->records[r->bytes_level];
        r->state.bytes_done = p->current;
        r->state.bytes_total = p->total;
        r->state.bytes_total_known = p->total != 0;
    }
    if (r->current_file_level >= 0 && pd->records[r->current_file_level].is_busy && pd->records[r->current_file_level].status[0]) {
        const xx_pd_record *p = &pd->records[r->current_file_level];
        memcpy(r->state.current_file, p->status, sizeof(p->status));
        r->state.current_file[sizeof(p->status)] = 0;
        while (r->state.current_file[0] && !xxwidgets_valid_utf8(r->state.current_file)) r->state.current_file[strlen(r->state.current_file) - 1] = 0;
        clean_controls(r->state.current_file);
    }
    stop = job->stop;
    xx_mutex_unlock(r->lock);
    /* xx_pd_is_stopped observes callbacks without storing their true return.
     * Persist the acknowledged request on this worker-owned progress object. */
    if (stop) xx_pd_stop((xx_pd_struct *)pd);
    return stop != 0;
}
static int disk_worker(void *user)
{
    disk_job *job = user;
    xx_pd_struct progress = xx_pd_init();
    xx_pd_observer previous;
    xxwidgets_status status;
    job->reporter.worker_id = xx_thread_current_id();
    previous = xx_pd_set_observer(&progress, disk_observe, job);
    status = job->worker(job->user, &progress, &job->reporter);
    /* The provider's final stop check decides whether its output committed.
     * Do not ask the observer again after a successful callback has returned. */
    xx_pd_set_observer(previous.progress, previous.callback, previous.user_data);
    xx_mutex_lock(job->reporter.lock);
    job->progress = progress;
    job->cancelled = status != XXWIDGETS_OK && progress.is_stop != 0;
    job->status = status;
    if (status == XXWIDGETS_OK && !job->cancelled) {
        if (job->reporter.state.files_total_known) job->reporter.state.files_done = job->reporter.state.files_total;
        if (job->reporter.state.bytes_total_known) job->reporter.state.bytes_done = job->reporter.state.bytes_total;
    }
    job->done = 1;
    xx_mutex_unlock(job->reporter.lock);
    /* No callback, job or mutex access remains after this final publication. */
    xx_atomic_store(&job->exited, 1);
    return 0;
}
static int percent(uint64_t current, uint64_t total, int known, int finished)
{
    int low = 0, high = 100;
    if (!known) return 0;
    if (!total) return finished ? 100 : 0;
    if (current >= total) return 100;
    while (high - low > 1) {
        int middle = (low + high) / 2;
        uint64_t threshold = (total / 100) * (unsigned)middle + ((total % 100) * (unsigned)middle + 99) / 100;
        if (current >= threshold) low = middle;
        else high = middle;
    }
    return low;
}
static int activity(uint64_t elapsed)
{
    unsigned phase = (unsigned)(elapsed % 1800);
    return 15 + (int)((phase < 900 ? phase : 1800 - phase) * 70 / 900);
}
static void size_text(char *text, size_t capacity, uint64_t bytes)
{
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
    long double number = (long double)bytes;
    size_t unit = 0;
    while (number >= 1024 && unit < 6) {
        number /= 1024;
        ++unit;
    }
    if (unit) snprintf(text, capacity, "%.2f %s", (double)number, units[unit]);
    else snprintf(text, capacity, "%" PRIu64 " B", bytes);
}
static void duration_text(char *text, size_t capacity, uint64_t milliseconds)
{
    uint64_t seconds = milliseconds / 1000;
    snprintf(text, capacity, "%02" PRIu64 ":%02u:%02u", seconds / 3600, (unsigned)(seconds / 60 % 60), (unsigned)(seconds % 60));
}
static xxwidgets_status disk_layout(disk_ui *ui, int width, int height)
{
    xxwidgets_status status = XXWIDGETS_OK;
    if (width < 90 || height < 23) return XXWIDGETS_INVALID_ARGUMENT;
#define RECT(member, x, y, w, h) \
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_rect(ui->member, (xxwidgets_rect){x, y, w, h})
    RECT(destination_label, 2, 2, 17, 1);
    RECT(destination, 20, 2, width - 22, 1);
    RECT(current_label, 2, 4, 17, 1);
    RECT(current, 20, 4, width - 22, 1);
    RECT(files, 2, 7, width - 4, 1);
    RECT(file_bar, 2, 9, width - 4, 1);
    RECT(bytes, 2, 12, width - 4, 1);
    RECT(byte_bar, 2, 14, width - 4, 1);
    RECT(elapsed, 2, 17, 28, 1);
    RECT(speed, 32, 17, 28, 1);
    RECT(remaining, 62, 17, width - 64, 1);
    RECT(status, 2, height - 3, width - 18, 2);
    RECT(cancel, width - 14, height - 3, 12, 2);
#undef RECT
    return status;
}
static void disk_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    disk_ui *ui = user;
    (void)app;
    if ((event->type == XXWIDGETS_EVENT_CLOSE && (event->widget == ui->window || event->widget == ui->owner)) ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == ui->cancel) || (event->type == XXWIDGETS_EVENT_SHORTCUT && event->value == 1))
        ui->stop = 1;
    if (event->type == XXWIDGETS_EVENT_RESIZE && event->widget == ui->window) {
        xxwidgets_rect bounds;
        ui->failure = xxwidgets_widget_get_rect(ui->window, &bounds);
        if (ui->failure == XXWIDGETS_OK) ui->failure = disk_layout(ui, bounds.width, bounds.height);
    }
}
static xxwidgets_status create_disk_window(disk_ui *ui, const char *title, const char *destination)
{
    xxwidgets_app *app = ui->owner->app;
    xxwidgets_status status;
    int hidden = app->create_hidden;
    const xxwidgets_shortcut shortcuts[] = {{"Esc", 1}};
    app->create_hidden = 1;
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title, (xxwidgets_rect){ui->owner->rect.x, ui->owner->rect.y, 104, 23}, &ui->window);
    app->create_hidden = hidden;
    if (status != XXWIDGETS_OK) return status;
    app->modal_window = ui->window;
#define CREATE(member, kind, text) \
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(app, ui->window, kind, text, (xxwidgets_rect){0, 0, 1, 1}, &ui->member)
    CREATE(destination_label, XXWIDGETS_LABEL, "Destination:");
    CREATE(destination, XXWIDGETS_LABEL, destination);
    CREATE(current_label, XXWIDGETS_LABEL, "Current file:");
    CREATE(current, XXWIDGETS_LABEL, "");
    CREATE(files, XXWIDGETS_LABEL, "Files");
    CREATE(file_bar, XXWIDGETS_PROGRESS, "");
    CREATE(bytes, XXWIDGETS_LABEL, "Bytes");
    CREATE(byte_bar, XXWIDGETS_PROGRESS, "");
    CREATE(elapsed, XXWIDGETS_LABEL, "");
    CREATE(speed, XXWIDGETS_LABEL, "");
    CREATE(remaining, XXWIDGETS_LABEL, "");
    CREATE(status, XXWIDGETS_LABEL, "Writing to disk...");
    CREATE(cancel, XXWIDGETS_BUTTON, "Cancel");
#undef CREATE
    if (status == XXWIDGETS_OK) status = xxwidgets_window_set_minimum_size(ui->window, 90, 23);
    if (status == XXWIDGETS_OK) status = disk_layout(ui, 104, 23);
    if (status == XXWIDGETS_OK) status = xxwidgets_window_set_shortcuts(ui->window, shortcuts, 1);
    return status;
}
static xxwidgets_status render_disk(disk_ui *ui, const xxwidgets_disk_write_progress *p, const xx_pd_struct *pd, uint64_t elapsed, int finished)
{
    char text[256], done[64], total[64], time[64], path[128];
    xxwidgets_status status;
    size_t path_capacity = ui->window->rect.width > 22 ? (size_t)ui->window->rect.width - 21 : 4;
    if (path_capacity > sizeof(path)) path_capacity = sizeof(path);
    xxwidgets_process_status_tail(path, path_capacity, ui->destination_text);
    status = xxwidgets_widget_set_text(ui->destination, path);
    xxwidgets_process_status_tail(path, path_capacity, p->current_file);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(ui->current, path);
    if (p->files_total_known) snprintf(text, sizeof(text), "Files: %" PRIu64 " / %" PRIu64, p->files_done, p->files_total);
    else snprintf(text, sizeof(text), "Files: %" PRIu64 " / undetermined", p->files_done);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(ui->files, text);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_set_value(
            ui->file_bar, !p->files_total_known && !ui->stop && !finished ? activity(elapsed) : percent(p->files_done, p->files_total, p->files_total_known, finished));
    size_text(done, sizeof(done), p->bytes_done);
    size_text(total, sizeof(total), p->bytes_total);
    snprintf(text, sizeof(text), "Bytes: %s / %s", done, p->bytes_total_known ? total : "undetermined");
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(ui->bytes, text);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_set_value(ui->byte_bar, !p->bytes_total_known && !ui->stop && !finished
                                                              ? activity(elapsed + 450)
                                                              : percent(p->bytes_done, p->bytes_total, p->bytes_total_known, finished));
    duration_text(time, sizeof(time), elapsed);
    snprintf(text, sizeof(text), "Elapsed: %s", time);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(ui->elapsed, text);
    if (elapsed && p->bytes_done) {
        long double speed = (long double)p->bytes_done * 1000 / elapsed;
        uint64_t rate = speed >= (long double)UINT64_MAX ? UINT64_MAX : (uint64_t)speed;
        size_text(done, sizeof(done), rate);
        snprintf(text, sizeof(text), "Speed: %s/s", done);
    } else snprintf(text, sizeof(text), "Speed: --");
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(ui->speed, text);
    if (elapsed && p->bytes_done && p->bytes_total_known && p->bytes_done < p->bytes_total) {
        long double milliseconds = (long double)(p->bytes_total - p->bytes_done) * elapsed / p->bytes_done;
        duration_text(time, sizeof(time), milliseconds >= (long double)UINT64_MAX ? UINT64_MAX : (uint64_t)milliseconds);
        snprintf(text, sizeof(text), "Remaining: %s", time);
    } else snprintf(text, sizeof(text), "Remaining: %s", finished ? "00:00:00" : "--");
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(ui->remaining, text);
    if (ui->stop) snprintf(text, sizeof(text), "Stopping safely...");
    else if (pd->last_error) {
        memcpy(text, pd->error_string, sizeof(pd->error_string));
        text[sizeof(pd->error_string)] = 0;
        while (text[0] && !xxwidgets_valid_utf8(text)) text[strlen(text) - 1] = 0;
        clean_controls(text);
    } else {
        snprintf(text, sizeof(text), "Writing to disk...");
        for (size_t i = 0; i < XX_PD_LEVELS; ++i)
            if (pd->records[i].is_busy && pd->records[i].status[0]) {
                memcpy(text, pd->records[i].status, sizeof(pd->records[i].status));
                text[sizeof(pd->records[i].status)] = 0;
                while (text[0] && !xxwidgets_valid_utf8(text)) text[strlen(text) - 1] = 0;
                clean_controls(text);
            }
    }
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(ui->status, text);
    if (ui->stop && status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(ui->cancel, "Stopping...");
    if (ui->stop && status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(ui->cancel, 0);
    return status;
}
static void result_error(xxwidgets_disk_write_result *result, const xx_pd_struct *pd)
{
    if (result->status == XXWIDGETS_OK && !result->cancelled) return;
    if (pd->last_error && pd->error_string[0]) {
        memcpy(result->error, pd->error_string, sizeof(pd->error_string));
        result->error[sizeof(pd->error_string)] = 0;
        while (result->error[0] && !xxwidgets_valid_utf8(result->error)) result->error[strlen(result->error) - 1] = 0;
        clean_controls(result->error);
    }
    if (!result->error[0]) snprintf(result->error, sizeof(result->error), "%s", result->cancelled ? "Writing cancelled" : "Cannot write output to disk");
}
xxwidgets_status xxwidgets_disk_write_run(xxwidgets_widget *owner, const char *title, const char *destination, xxwidgets_disk_write_worker_fn worker, void *user,
                                          xxwidgets_disk_write_result *output)
{
    disk_job job = {0};
    disk_ui ui = {0};
    xxwidgets_disk_write_result result = {0};
    xxwidgets_app *app;
    xxwidgets_widget *previous_modal, *previous_default, *focus = NULL, *w;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    xx_thread_t *thread = NULL;
    xxwidgets_status status = XXWIDGETS_OK, cleanup;
    uint64_t started = 0, now = 0;
    int enabled, disabled = 0, attached = 0, done = 0;
    char display_destination[128];
    if (output) memset(output, 0, sizeof(*output));
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !xxwidgets_valid_utf8(title) || !xxwidgets_valid_utf8(destination) || !destination[0] || !worker) {
        if (output) output->status = output->ui_status = XXWIDGETS_INVALID_ARGUMENT;
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->quit || !owner->enabled || (app->modal_window && app->modal_window != owner)) {
        if (output) output->status = output->ui_status = XXWIDGETS_BUSY;
        return XXWIDGETS_BUSY;
    }
    if (xx_io_memory_only_active()) {
        if (output) output->status = XXWIDGETS_UNAVAILABLE;
        return XXWIDGETS_UNAVAILABLE;
    }
    status = xxwidgets_disk_write_clock(&started);
    if (status != XXWIDGETS_OK) {
        if (output) output->status = output->ui_status = status;
        return status;
    }
    now = started;
    job.reporter.lock = xx_mutex_create();
    if (!job.reporter.lock) {
        if (output) output->status = XXWIDGETS_OUT_OF_MEMORY;
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    job.reporter.files_level = job.reporter.bytes_level = job.reporter.current_file_level = -1;
    xxwidgets_process_status_tail(job.reporter.state.current_file, sizeof(job.reporter.state.current_file), destination);
    clean_controls(job.reporter.state.current_file);
    job.reporter.state.files_total = 1;
    job.reporter.state.files_total_known = 1;
    xxwidgets_process_status_tail(display_destination, sizeof(display_destination), destination);
    clean_controls(display_destination);
    job.worker = worker;
    job.user = user;
    ui.owner = owner;
    memcpy(ui.destination_text, job.reporter.state.current_file, strlen(job.reporter.state.current_file) + 1);
    previous_modal = app->modal_window;
    previous_default = app->modal_default;
    previous_event = app->on_event;
    previous_user = app->user_data;
    enabled = owner->requested_enabled;
    for (w = app->widgets; w; w = w->next)
        if (w->parent == owner && xxwidgets_focusable(w)) {
            if (!focus) focus = w;
            if (xxwidgets_widget_has_focus(w) == 1) {
                focus = w;
                break;
            }
        }
    app->on_event = disk_event;
    app->user_data = &ui;
    app->modal_default = NULL;
    status = xxwidgets_widget_set_enabled(owner, 0);
    if (status != XXWIDGETS_OK) goto finish;
    disabled = 1;
    thread = xx_thread_create(disk_worker, &job);
    if (!thread) {
        status = XXWIDGETS_PLATFORM_ERROR;
        goto finish;
    }
    while (status == XXWIDGETS_OK && !app->quit) {
        xxwidgets_disk_write_progress snapshot;
        xx_pd_struct pd;
        xx_mutex_lock(job.reporter.lock);
        if (ui.stop) job.stop = 1;
        done = job.done;
        snapshot = job.reporter.state;
        pd = job.progress;
        xx_mutex_unlock(job.reporter.lock);
        if (done) break;
        status = xxwidgets_disk_write_clock(&now);
        if (status != XXWIDGETS_OK) break;
        if (now < started) now = started;
        if (!ui.window && now - started >= 350) {
            status = create_disk_window(&ui, title, display_destination);
            if (status == XXWIDGETS_OK) status = render_disk(&ui, &snapshot, &pd, now - started, 0);
            if (status == XXWIDGETS_OK) {
                status = xxwidgets_window_set_modality(ui.window, owner, XXWIDGETS_WINDOW_MODAL);
                if (status == XXWIDGETS_OK) attached = 1;
            }
            if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_visible(ui.window, 1);
            if (status == XXWIDGETS_OK) status = xxwidgets_widget_focus(ui.cancel);
        } else if (ui.window) status = render_disk(&ui, &snapshot, &pd, now - started, 0);
        if (status == XXWIDGETS_OK) status = xxwidgets_app_poll(app, 30);
        if (status == XXWIDGETS_OK) status = ui.failure;
    }
finish:
    if (thread) {
        xx_mutex_lock(job.reporter.lock);
        if (!job.done) job.stop = 1;
        xx_mutex_unlock(job.reporter.lock);
        {
            bool joined;
#ifdef XXWIDGETS_TEST_DISK_CLOCK
            joined = xxwidgets_disk_write_test_join(thread);
#else
            joined = xx_thread_join(thread, NULL);
#endif
            if (!joined) {
                /* A private valid thread should always join. If the platform
                 * rejects its handle, still wait for the last worker access
                 * before releasing stack data or its mutex, then let the thread
                 * handle detach release the caller reference safely. */
                while (!xx_atomic_load(&job.exited)) xx_thread_sleep(1);
                xx_thread_detach(thread);
                if (status == XXWIDGETS_OK) status = XXWIDGETS_PLATFORM_ERROR;
            }
        }
        result.status = job.status;
        result.cancelled = job.cancelled;
        result.progress = job.reporter.state;
        result_error(&result, &job.progress);
    } else result.status = status;
    if (xxwidgets_disk_write_clock(&now) != XXWIDGETS_OK || now < started) now = started;
    result.elapsed_ms = now - started;
    if (ui.window) {
        cleanup = xxwidgets_widget_set_visible(ui.window, 0);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (attached) {
        cleanup = xxwidgets_window_set_modality(ui.window, NULL, XXWIDGETS_MODELESS);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (ui.window) {
        cleanup = xxwidgets_widget_destroy(ui.window);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    app->modal_window = previous_modal;
    app->modal_default = previous_default;
    app->on_event = previous_event;
    app->user_data = previous_user;
    if (disabled) {
        cleanup = xxwidgets_widget_set_enabled(owner, enabled);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (focus) {
        cleanup = xxwidgets_widget_focus(focus);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    result.ui_status = status;
    xx_mutex_destroy(job.reporter.lock);
    if (output) *output = result;
    if (thread && job.done && job.status == XXWIDGETS_OK && !job.cancelled) return XXWIDGETS_OK;
    if (status != XXWIDGETS_OK) return status;
    return result.cancelled ? XXWIDGETS_OK : result.status;
}
