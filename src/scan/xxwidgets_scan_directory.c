/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets/xxwidgets_scan_directory.h"
#include "xxfclib/threads/xx_threads.h"
#include "xxwidgets_internal.h"
#include "xxwidgets_scan_directory_platform.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QUEUE_LIMIT 64
/* Keep one iterator per ancestor. No recursive C calls or eager whole-tree list. */
typedef struct directory_frame {
    char *path;
    xxwidgets_directory_iterator *iterator;
} directory_frame;
typedef struct directory_record {
    xxwidgets_scan_directory_result result;
    xxwidgets_scan_result *detections;
    size_t capacity, root_index;
    int displayed;
    struct directory_record *next;
} directory_record;
typedef struct directory_job {
    xx_mutex_t *mutex;
    xx_thread_t *thread;
    volatile long cancel;
    char *root, *current;
    int recursive, done;
    xxwidgets_scan_options options;
    xxwidgets_scan_directory_file_fn scanner;
    void *user;
    directory_record *first, *last;
    size_t queued;
    xxwidgets_scan_directory_progress progress;
} directory_job;
struct xxwidgets_scan_directory_task {
    directory_job *job;
    directory_record *record;
    xxwidgets_status failure;
};
struct xxwidgets_scan_directory {
    xxwidgets_widget *owner, *controls[XXWIDGETS_SCAN_DIRECTORY_CONTROL_COUNT];
    xxwidgets_scan_options options;
    xxwidgets_scan_directory_file_fn scanner;
    void *user;
    directory_job *job;
    char *root;
    directory_record **records;
    size_t count, capacity;
    int pending, dirty, reset_tree;
    xxwidgets_scan_directory_progress progress;
};
#define CONTROL(v, id) ((v)->controls[XXWIDGETS_SCAN_DIRECTORY_##id])

static void free_record(directory_record *record)
{
    if (!record)
        return;
    for (size_t i = 0; i < record->result.detection_count; ++i) {
        xxwidgets_scan_result *r = record->detections + i;
        free((void *)r->type);
        free((void *)r->name);
        free((void *)r->version);
        free((void *)r->info);
    }
    free(record->detections);
    free((void *)record->result.path);
    free(record);
}
static void clear_records(xxwidgets_scan_directory *view)
{
    for (size_t i = 0; i < view->count; ++i)
        free_record(view->records[i]);
    free(view->records);
    view->records = NULL;
    view->count = view->capacity = 0;
}
static void free_job(directory_job *job)
{
    directory_record *record = job->first;
    while (record) {
        directory_record *next = record->next;
        free_record(record);
        record = next;
    }
    if (job->mutex)
        xx_mutex_destroy(job->mutex);
    free(job->root);
    free(job->current);
    free(job);
}
static int ui_busy(const xxwidgets_scan_directory *view)
{
    const xxwidgets_app *app = view->owner->app;
    return app->dispatch_depth || app->polling || app->syncing || app->modal_window;
}
int xxwidgets_scan_directory_cancelled(const xxwidgets_scan_directory_task *task)
{
    return !task || xx_atomic_load(&task->job->cancel) != 0;
}
xxwidgets_status xxwidgets_scan_directory_emit(xxwidgets_scan_directory_task *task, const xxwidgets_scan_result *source)
{
    xxwidgets_scan_result copy;
    const char *fields[4];
    char *owned[4] = { 0 };
    directory_record *record;
    if (!task || !source)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (task->failure != XXWIDGETS_OK)
        return task->failure;
    if (xxwidgets_scan_directory_cancelled(task))
        return XXWIDGETS_BUSY;
    fields[0] = source->type ? source->type : "";
    fields[1] = source->name ? source->name : "";
    fields[2] = source->version ? source->version : "";
    fields[3] = source->info ? source->info : "";
    for (size_t i = 0; i < 4; ++i) {
        if (!xxwidgets_valid_utf8(fields[i])) {
            task->failure = XXWIDGETS_INVALID_ARGUMENT;
            break;
        }
        owned[i] = xxwidgets_strdup(fields[i]);
        if (!owned[i]) {
            task->failure = XXWIDGETS_OUT_OF_MEMORY;
            break;
        }
    }
    if (task->failure != XXWIDGETS_OK) {
        for (size_t i = 0; i < 4; ++i)
            free(owned[i]);
        return task->failure;
    }
    record = task->record;
    if (record->result.detection_count == record->capacity) {
        size_t capacity = record->capacity ? record->capacity * 2 : 4;
        xxwidgets_scan_result *grown = capacity > record->capacity && capacity <= SIZE_MAX / sizeof(*grown)
            ? realloc(record->detections, capacity * sizeof(*grown))
            : NULL;
        if (!grown) {
            for (size_t i = 0; i < 4; ++i)
                free(owned[i]);
            return task->failure = XXWIDGETS_OUT_OF_MEMORY;
        }
        record->detections = grown;
        record->capacity = capacity;
    }
    copy.type = owned[0];
    copy.name = owned[1];
    copy.version = owned[2];
    copy.info = owned[3];
    record->detections[record->result.detection_count++] = copy;
    record->result.detections = record->detections;
    return XXWIDGETS_OK;
}
static directory_record *new_record(char *path, int is_directory, xxwidgets_status status)
{
    directory_record *record = calloc(1, sizeof(*record));
    if (!record) {
        free(path);
        return NULL;
    }
    record->result.path = path;
    record->result.is_directory = is_directory;
    record->result.status = status;
    return record;
}
static int enqueue(directory_job *job, directory_record *record)
{
    for (;;) {
        if (xx_atomic_load(&job->cancel)) {
            free_record(record);
            return 0;
        }
        xx_mutex_lock(job->mutex);
        if (job->queued < QUEUE_LIMIT)
            break;
        xx_mutex_unlock(job->mutex);
        xx_thread_sleep(1);
    }
    if (job->last)
        job->last->next = record;
    else
        job->first = record;
    job->last = record;
    ++job->queued;
    if (record->result.is_directory)
        ++job->progress.directories_failed;
    else {
        ++job->progress.files_scanned;
        if (record->result.status != XXWIDGETS_OK)
            ++job->progress.files_failed;
    }
    job->progress.detections += record->result.detection_count;
    xx_mutex_unlock(job->mutex);
    return 1;
}
static void skipped(directory_job *job)
{
    xx_mutex_lock(job->mutex);
    ++job->progress.entries_skipped;
    xx_mutex_unlock(job->mutex);
}
static char *join_path(const char *parent, const char *name)
{
    size_t a = strlen(parent), b = strlen(name);
    char *path;
    if (a > SIZE_MAX - 2 || b > SIZE_MAX - a - 2)
        return NULL;
    path = malloc(a + b + 2);
    if (!path)
        return NULL;
    memcpy(path, parent, a);
#ifdef _WIN32
    if (a && parent[a - 1] != '/' && parent[a - 1] != '\\' && !(a == 2 && parent[1] == ':'))
        path[a++] = '/';
#else
    if (a && parent[a - 1] != '/')
        path[a++] = '/';
#endif
    memcpy(path + a, name, b + 1);
    return path;
}
static xxwidgets_status set_current(directory_job *job, const char *path)
{
    char *copy = xxwidgets_strdup(path);
    if (!copy)
        return XXWIDGETS_OUT_OF_MEMORY;
    xx_mutex_lock(job->mutex);
    free(job->current);
    job->current = copy;
    xx_mutex_unlock(job->mutex);
    return XXWIDGETS_OK;
}
static xxwidgets_status scan_file(directory_job *job, char *path)
{
    directory_record *record = new_record(path, 0, XXWIDGETS_OK);
    xxwidgets_scan_directory_task task;
    if (!record)
        return XXWIDGETS_OUT_OF_MEMORY;
    task.job = job;
    task.record = record;
    task.failure = XXWIDGETS_OK;
    record->result.status = set_current(job, path);
    if (record->result.status == XXWIDGETS_OK)
        record->result.status = job->scanner(&task, path, &job->options, job->user);
    if (task.failure != XXWIDGETS_OK)
        record->result.status = task.failure;
    if (record->result.status == XXWIDGETS_OUT_OF_MEMORY) {
        free_record(record);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    if (record->result.status < XXWIDGETS_OK || record->result.status > XXWIDGETS_BUSY)
        record->result.status = XXWIDGETS_INVALID_ARGUMENT;
    enqueue(job, record);
    return XXWIDGETS_OK;
}
static xxwidgets_status directory_error(directory_job *job, const char *path)
{
    char *copy = xxwidgets_strdup(path);
    directory_record *record = copy ? new_record(copy, 1, XXWIDGETS_PLATFORM_ERROR) : NULL;
    if (!record)
        return XXWIDGETS_OUT_OF_MEMORY;
    enqueue(job, record);
    return XXWIDGETS_OK;
}
static int worker(void *data)
{
    directory_job *job = data;
    directory_frame *stack = NULL;
    size_t depth = 0, capacity = 0;
    xxwidgets_directory_iterator *root = NULL;
    xxwidgets_status status = xxwidgets_directory_open(job->root, &root);
    if (status != XXWIDGETS_OK) {
        if (status != XXWIDGETS_OUT_OF_MEMORY && directory_error(job, job->root) == XXWIDGETS_OUT_OF_MEMORY)
            status = XXWIDGETS_OUT_OF_MEMORY;
        goto finish;
    }
    capacity = 16;
    stack = calloc(capacity, sizeof(*stack));
    if (!stack) {
        xxwidgets_directory_close(root);
        status = XXWIDGETS_OUT_OF_MEMORY;
        goto finish;
    }
    stack[0].iterator = root;
    stack[0].path = xxwidgets_strdup(job->root);
    if (!stack[0].path) {
        xxwidgets_directory_close(root);
        status = XXWIDGETS_OUT_OF_MEMORY;
        goto finish;
    }
    depth = 1;
    while (depth && !xx_atomic_load(&job->cancel)) {
        directory_frame *frame = stack + depth - 1;
        char *name = NULL, *path;
        int kind;
        status = xxwidgets_directory_next(frame->iterator, &name, &kind);
        if (status == XXWIDGETS_OUT_OF_MEMORY) {
            free(name);
            break;
        }
        if (status != XXWIDGETS_OK || kind == XXW_DIRECTORY_END) {
            free(name);
            if (status != XXWIDGETS_OK)
                status = directory_error(job, frame->path);
            xxwidgets_directory_close(frame->iterator);
            free(frame->path);
            --depth;
            if (status != XXWIDGETS_OK)
                break;
            continue;
        }
        if (kind == XXW_DIRECTORY_SKIP || !name || !xxwidgets_valid_utf8(name) || (kind == XXW_DIRECTORY_SUBDIR && !job->recursive)) {
            free(name);
            skipped(job);
            continue;
        }
        path = join_path(frame->path, name);
        if (!path) {
            free(name);
            status = XXWIDGETS_OUT_OF_MEMORY;
            break;
        }
        if (kind == XXW_DIRECTORY_SUBDIR) {
            xxwidgets_directory_iterator *child = NULL;
            status = xxwidgets_directory_open_child(frame->iterator, name, path, &child);
            free(name);
            if (status != XXWIDGETS_OK) {
                if (status != XXWIDGETS_OUT_OF_MEMORY)
                    status = directory_error(job, path);
                free(path);
                if (status != XXWIDGETS_OK)
                    break;
                continue;
            }
            if (depth == capacity) {
                size_t next = capacity <= SIZE_MAX / 2 ? capacity * 2 : 0;
                directory_frame *grown = next && next <= SIZE_MAX / sizeof(*grown) ? realloc(stack, next * sizeof(*grown)) : NULL;
                if (!grown) {
                    xxwidgets_directory_close(child);
                    free(path);
                    status = XXWIDGETS_OUT_OF_MEMORY;
                    break;
                }
                stack = grown;
                capacity = next;
            }
            stack[depth].path = path;
            stack[depth++].iterator = child;
        } else {
            free(name);
            if (kind == XXW_DIRECTORY_FILE)
                status = scan_file(job, path);
            else {
                directory_record *record = new_record(path, 0, XXWIDGETS_PLATFORM_ERROR);
                if (!record)
                    status = XXWIDGETS_OUT_OF_MEMORY;
                else {
                    enqueue(job, record);
                    status = XXWIDGETS_OK;
                }
            }
            if (status != XXWIDGETS_OK)
                break;
        }
    }
finish:
    while (depth) {
        --depth;
        xxwidgets_directory_close(stack[depth].iterator);
        free(stack[depth].path);
    }
    free(stack);
    xx_mutex_lock(job->mutex);
    job->progress.status = status;
    job->progress.state = status != XXWIDGETS_OK ? XXWIDGETS_SCAN_DIRECTORY_FAILED
        : xx_atomic_load(&job->cancel)           ? XXWIDGETS_SCAN_DIRECTORY_CANCELLED
                                                 : XXWIDGETS_SCAN_DIRECTORY_FINISHED;
    job->done = 1;
    xx_mutex_unlock(job->mutex);
    return 0;
}
static int valid_bounds(xxwidgets_rect bounds)
{
    return bounds.x >= 0 && bounds.y >= 0 && bounds.width >= 64 && bounds.height >= 15 && bounds.x <= INT_MAX - bounds.width
        && bounds.y <= INT_MAX - bounds.height;
}
static void rectangles(xxwidgets_rect b, xxwidgets_rect *r)
{
    r[XXWIDGETS_SCAN_DIRECTORY_LABEL] = (xxwidgets_rect) { b.x, b.y, b.width, 1 };
    r[XXWIDGETS_SCAN_DIRECTORY_PATH] = (xxwidgets_rect) { b.x, b.y + 1, b.width, 2 };
    r[XXWIDGETS_SCAN_DIRECTORY_RECURSIVE] = (xxwidgets_rect) { b.x, b.y + 3, b.width - 40, 2 };
    r[XXWIDGETS_SCAN_DIRECTORY_OPTIONS] = (xxwidgets_rect) { b.x + b.width - 38, b.y + 3, 12, 2 };
    r[XXWIDGETS_SCAN_DIRECTORY_SCAN] = (xxwidgets_rect) { b.x + b.width - 25, b.y + 3, 12, 2 };
    r[XXWIDGETS_SCAN_DIRECTORY_STOP] = (xxwidgets_rect) { b.x + b.width - 12, b.y + 3, 12, 2 };
    r[XXWIDGETS_SCAN_DIRECTORY_STATUS] = (xxwidgets_rect) { b.x, b.y + 5, b.width, 1 };
    r[XXWIDGETS_SCAN_DIRECTORY_CURRENT_FILE] = (xxwidgets_rect) { b.x, b.y + 6, b.width, 1 };
    r[XXWIDGETS_SCAN_DIRECTORY_RESULTS] = (xxwidgets_rect) { b.x, b.y + 8, b.width, b.height - 8 };
}
static void recover(xxwidgets_widget *widget)
{
    ++widget->app->syncing;
    (void)widget->app->ops->sync(widget);
    --widget->app->syncing;
}
xxwidgets_status xxwidgets_scan_directory_set_rect(xxwidgets_scan_directory *view, xxwidgets_rect bounds)
{
    xxwidgets_rect rects[XXWIDGETS_SCAN_DIRECTORY_CONTROL_COUNT], previous[XXWIDGETS_SCAN_DIRECTORY_CONTROL_COUNT];
    if (!view || !valid_bounds(bounds))
        return XXWIDGETS_INVALID_ARGUMENT;
    rectangles(bounds, rects);
    for (size_t i = 0; i < XXWIDGETS_SCAN_DIRECTORY_CONTROL_COUNT; ++i)
        previous[i] = view->controls[i]->rect;
    for (size_t i = 0; i < XXWIDGETS_SCAN_DIRECTORY_CONTROL_COUNT; ++i) {
        xxwidgets_status status = xxwidgets_widget_set_rect(view->controls[i], rects[i]);
        if (status != XXWIDGETS_OK) {
            for (size_t j = 0; j < i; ++j) {
                view->controls[j]->rect = previous[j];
                recover(view->controls[j]);
            }
            return status;
        }
    }
    return XXWIDGETS_OK;
}
static xxwidgets_status enable_controls(xxwidgets_scan_directory *view, int running)
{
    static const xxwidgets_scan_directory_control_id ids[] = { XXWIDGETS_SCAN_DIRECTORY_PATH, XXWIDGETS_SCAN_DIRECTORY_RECURSIVE,
        XXWIDGETS_SCAN_DIRECTORY_OPTIONS, XXWIDGETS_SCAN_DIRECTORY_SCAN, XXWIDGETS_SCAN_DIRECTORY_STOP };
    int previous[5];
    for (size_t i = 0; i < 5; ++i)
        previous[i] = view->controls[ids[i]]->enabled;
    for (size_t i = 0; i < 5; ++i) {
        int enabled = i == 4 ? running : !running && (i != 3 || view->scanner != NULL);
        xxwidgets_status status = xxwidgets_widget_set_enabled(view->controls[ids[i]], enabled);
        if (status != XXWIDGETS_OK) {
            for (size_t j = 0; j < i; ++j) {
                view->controls[ids[j]]->enabled = previous[j];
                recover(view->controls[ids[j]]);
            }
            return status;
        }
    }
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_scan_directory_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_scan_directory **out)
{
    static const xxwidgets_kind kinds[] = { XXWIDGETS_LABEL, XXWIDGETS_EDIT, XXWIDGETS_CHECKBOX, XXWIDGETS_BUTTON, XXWIDGETS_BUTTON,
        XXWIDGETS_BUTTON, XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_TREEVIEW };
    static const char *const names[] = { XXW_N_("Directory"), "", XXW_N_("Recursive"), XXW_N_("Options"), XXW_N_("Scan"), XXW_N_("Stop"),
        XXW_N_("Ready"), "", XXW_N_("Directory scan results") };
    xxwidgets_scan_directory *view;
    xxwidgets_rect rects[XXWIDGETS_SCAN_DIRECTORY_CONTROL_COUNT];
    xxwidgets_status status;
    if (!out)
        return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !valid_bounds(bounds))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing || owner->app->modal_window)
        return XXWIDGETS_BUSY;
    view = calloc(1, sizeof(*view));
    if (!view)
        return XXWIDGETS_OUT_OF_MEMORY;
    view->owner = owner;
    xxwidgets_scan_options_init(&view->options);
    rectangles(bounds, rects);
    for (size_t i = 0; i < XXWIDGETS_SCAN_DIRECTORY_CONTROL_COUNT; ++i) {
        status = xxwidgets_widget_create(owner->app, owner, kinds[i], XXW_TR(owner->app, names[i]), rects[i], view->controls + i);
        if (status != XXWIDGETS_OK)
            goto failed;
    }
    status = xxwidgets_widget_set_value(CONTROL(view, RECURSIVE), 1);
    if (status == XXWIDGETS_OK)
        status = enable_controls(view, 0);
    if (status != XXWIDGETS_OK)
        goto failed;
    *out = view;
    return XXWIDGETS_OK;
failed:
    for (size_t i = 0; i < XXWIDGETS_SCAN_DIRECTORY_CONTROL_COUNT; ++i)
        if (view->controls[i])
            xxwidgets_widget_destroy(view->controls[i]);
    free(view);
    return status;
}
xxwidgets_widget *xxwidgets_scan_directory_control(const xxwidgets_scan_directory *view, xxwidgets_scan_directory_control_id id)
{
    return view && id >= 0 && id < XXWIDGETS_SCAN_DIRECTORY_CONTROL_COUNT ? view->controls[id] : NULL;
}
xxwidgets_status xxwidgets_scan_directory_set_scanner(xxwidgets_scan_directory *view, xxwidgets_scan_directory_file_fn scanner, void *user)
{
    xxwidgets_scan_directory_file_fn previous;
    void *previous_user;
    xxwidgets_status status;
    if (!view)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (view->job || ui_busy(view))
        return XXWIDGETS_BUSY;
    previous = view->scanner;
    previous_user = view->user;
    view->scanner = scanner;
    view->user = user;
    status = enable_controls(view, 0);
    if (status != XXWIDGETS_OK) {
        view->scanner = previous;
        view->user = previous_user;
    }
    return status;
}
xxwidgets_status xxwidgets_scan_directory_set_path(xxwidgets_scan_directory *view, const char *path)
{
    if (!view || !xxwidgets_valid_utf8(path))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (view->job)
        return XXWIDGETS_BUSY;
    return xxwidgets_widget_set_text(CONTROL(view, PATH), path);
}
xxwidgets_status xxwidgets_scan_directory_set_options(xxwidgets_scan_directory *view, const xxwidgets_scan_options *options)
{
    if (!view || !options || (options->flags & ~63u) || (options->databases & ~3u))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (view->job)
        return XXWIDGETS_BUSY;
    view->options = *options;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_scan_directory_get_options(const xxwidgets_scan_directory *view, xxwidgets_scan_options *options)
{
    if (!view || !options)
        return XXWIDGETS_INVALID_ARGUMENT;
    *options = view->options;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_scan_directory_start(xxwidgets_scan_directory *view)
{
    directory_job *job;
    char *root;
    size_t size;
    int recursive;
    xxwidgets_status status;
    if (!view || !view->scanner)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (view->job || ui_busy(view))
        return XXWIDGETS_BUSY;
    status = xxwidgets_widget_get_text(CONTROL(view, PATH), NULL, 0, &size);
    if (status != XXWIDGETS_OK)
        return status;
    if (size <= 1)
        return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_widget_get_value(CONTROL(view, RECURSIVE), &recursive);
    if (status != XXWIDGETS_OK)
        return status;
    job = calloc(1, sizeof(*job));
    if (!job)
        return XXWIDGETS_OUT_OF_MEMORY;
    job->root = malloc(size);
    job->mutex = xx_mutex_create();
    if (!job->root || !job->mutex) {
        free_job(job);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    status = xxwidgets_widget_get_text(CONTROL(view, PATH), job->root, size, NULL);
    if (status != XXWIDGETS_OK) {
        free_job(job);
        return status;
    }
    job->recursive = recursive != 0;
    job->options = view->options;
    job->scanner = view->scanner;
    job->user = view->user;
    root = xxwidgets_strdup(job->root);
    if (!root) {
        free_job(job);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    status = enable_controls(view, 1);
    if (status != XXWIDGETS_OK) {
        free(root);
        free_job(job);
        return status;
    }
    job->thread = xx_thread_create(worker, job);
    if (!job->thread) {
        free(root);
        free_job(job);
        (void)enable_controls(view, 0);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    clear_records(view);
    free(view->root);
    view->root = root;
    memset(&view->progress, 0, sizeof(view->progress));
    view->progress.state = XXWIDGETS_SCAN_DIRECTORY_RUNNING;
    view->job = job;
    view->dirty = 1;
    view->reset_tree = 1;
    view->pending = 0;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_scan_directory_cancel(xxwidgets_scan_directory *view)
{
    if (!view)
        return XXWIDGETS_INVALID_ARGUMENT;
    view->pending = 0;
    if (view->job)
        xx_atomic_store(&view->job->cancel, 1);
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_scan_directory_handle_event(xxwidgets_scan_directory *view, const xxwidgets_event *event)
{
    if (!view || !event)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (event->type != XXWIDGETS_EVENT_CLICK)
        return XXWIDGETS_OK;
    if (event->widget == CONTROL(view, STOP))
        return xxwidgets_scan_directory_cancel(view);
    if (!view->job) {
        if (event->widget == CONTROL(view, SCAN))
            view->pending = 1;
        else if (event->widget == CONTROL(view, OPTIONS))
            view->pending = 2;
    }
    return XXWIDGETS_OK;
}
static char *detection_text(const xxwidgets_scan_result *r)
{
    const char *parts[] = { r->type, r->type[0] ? ": " : "", r->name, r->version[0] ? " (" : "", r->version, r->version[0] ? ")" : "",
        r->info[0] ? " [" : "", r->info, r->info[0] ? "]" : "" };
    size_t length = 0, at = 0;
    char *text;
    for (size_t i = 0; i < 9; ++i) {
        size_t n = strlen(parts[i]);
        if (n >= SIZE_MAX - length)
            return NULL;
        length += n;
    }
    text = malloc(length + 1);
    if (text) {
        for (size_t i = 0; i < 9; ++i) {
            size_t n = strlen(parts[i]);
            memcpy(text + at, parts[i], n);
            at += n;
        }
        text[at] = 0;
    }
    return text;
}
static const char *display_path(const xxwidgets_scan_directory *view, const char *path)
{
    size_t length = view->root ? strlen(view->root) : 0;
    if (!length || strncmp(path, view->root, length) || !path[length])
        return path;
#ifdef _WIN32
    if (length == 2 && view->root[1] == ':')
        return path + length;
    if (view->root[length - 1] == '/' || view->root[length - 1] == '\\')
        return path + length;
    if (path[length] == '/' || path[length] == '\\')
        return path + length + 1;
#else
    if (view->root[length - 1] == '/')
        return path + length;
    if (path[length] == '/')
        return path + length + 1;
#endif
    return path;
}
static xxwidgets_status render_tree(xxwidgets_scan_directory *view)
{
    xxwidgets_tree_node *nodes;
    xxwidgets_tree_color *colors;
    size_t count = view->count, at = 0, selected = SIZE_MAX;
    xxwidgets_tree_node previous;
    xxwidgets_status status = XXWIDGETS_OK;
    xxwidgets_widget *tree = CONTROL(view, RESULTS);
    for (size_t i = 0; i < view->count; ++i) {
        if (view->records[i]->result.detection_count > (size_t)INT_MAX - count)
            return XXWIDGETS_OUT_OF_MEMORY;
        count += view->records[i]->result.detection_count;
    }
    if (!count) {
        xxwidgets_status cleared = xxwidgets_treeview_clear(tree);
        if (cleared == XXWIDGETS_OK)
            view->reset_tree = 0;
        return cleared;
    }
    if (count > SIZE_MAX / sizeof(*nodes) || count > SIZE_MAX / sizeof(*colors))
        return XXWIDGETS_OUT_OF_MEMORY;
    nodes = calloc(count, sizeof(*nodes));
    colors = malloc(count * sizeof(*colors));
    if (!nodes || !colors) {
        free(nodes);
        free(colors);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    if (!view->reset_tree)
        (void)xxwidgets_treeview_get_selection(tree, &selected, &previous);
    for (size_t i = 0; i < view->count; ++i) {
        directory_record *record = view->records[i];
        size_t root = at;
        int expanded
            = record->displayed && xxwidgets_treeview_get_node(tree, record->root_index, &previous) == XXWIDGETS_OK ? previous.expanded : 0;
        nodes[at].parent = SIZE_MAX;
        nodes[at].expanded = expanded;
        if (record->result.status == XXWIDGETS_OK)
            nodes[at].text = xxwidgets_strdup(display_path(view, record->result.path));
        else {
            const char *error = XXW_TR(view->owner->app, "Error"), *message = xxwidgets_status_string(record->result.status);
            size_t length = strlen(display_path(view, record->result.path)) + strlen(error) + strlen(message) + 8;
            char *text = malloc(length);
            if (text)
                snprintf(text, length, "%s [%s: %s]", display_path(view, record->result.path), error, message);
            nodes[at].text = text;
        }
        colors[at++] = (xxwidgets_tree_color) { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
        if (!nodes[root].text) {
            status = XXWIDGETS_OUT_OF_MEMORY;
            break;
        }
        for (size_t j = 0; j < record->result.detection_count; ++j) {
            nodes[at].parent = root;
            nodes[at].text = detection_text(record->detections + j);
            colors[at] = xxwidgets_scan_result_color(record->detections[j].type);
            if (!nodes[at++].text) {
                status = XXWIDGETS_OUT_OF_MEMORY;
                break;
            }
        }
        if (status != XXWIDGETS_OK)
            break;
    }
    if (status == XXWIDGETS_OK)
        status = xxwidgets_treeview_set_colored_nodes(tree, nodes, colors, count);
    if (status == XXWIDGETS_OK) {
        at = 0;
        for (size_t i = 0; i < view->count; ++i) {
            view->records[i]->root_index = at;
            view->records[i]->displayed = 1;
            at += 1 + view->records[i]->result.detection_count;
        }
        if (selected < count)
            status = xxwidgets_widget_set_value(tree, (int)selected);
        view->reset_tree = 0;
    }
    for (size_t i = 0; i < count; ++i)
        free((void *)nodes[i].text);
    free(nodes);
    free(colors);
    return status;
}
static xxwidgets_status render_status(xxwidgets_scan_directory *view)
{
    static const char *const states[] = { XXW_N_("Ready"), XXW_N_("Scanning"), XXW_N_("Finished"), XXW_N_("Cancelled"), XXW_N_("Failed") };
    xxwidgets_app *app = view->owner->app;
    const xxwidgets_scan_directory_progress *p = &view->progress;
    const char *state = XXW_TR(app, states[p->state]), *files = XXW_TR(app, "Files"), *errors = XXW_TR(app, "File errors"),
               *directories = XXW_TR(app, "Directory errors"), *detections = XXW_TR(app, "Detections"), *skips = XXW_TR(app, "Skipped");
    int length = snprintf(NULL, 0, "%s | %s: %zu | %s: %zu | %s: %zu | %s: %zu | %s: %zu", state, files, p->files_scanned, errors,
        p->files_failed, directories, p->directories_failed, detections, p->detections, skips, p->entries_skipped);
    char *text;
    xxwidgets_status status;
    if (length < 0)
        return XXWIDGETS_PLATFORM_ERROR;
    text = malloc((size_t)length + 1);
    if (!text)
        return XXWIDGETS_OUT_OF_MEMORY;
    snprintf(text, (size_t)length + 1, "%s | %s: %zu | %s: %zu | %s: %zu | %s: %zu | %s: %zu", state, files, p->files_scanned, errors,
        p->files_failed, directories, p->directories_failed, detections, p->detections, skips, p->entries_skipped);
    status = xxwidgets_widget_set_text(CONTROL(view, STATUS), text);
    free(text);
    return status;
}
xxwidgets_status xxwidgets_scan_directory_poll(xxwidgets_scan_directory *view)
{
    directory_job *job;
    xxwidgets_status status;
    if (!view)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (ui_busy(view))
        return XXWIDGETS_BUSY;
    if (view->pending == 1) {
        view->pending = 0;
        status = xxwidgets_scan_directory_start(view);
        if (status != XXWIDGETS_OK)
            return status;
    } else if (view->pending == 2) {
        int accepted;
        view->pending = 0;
        status = xxwidgets_scan_options_dialog(view->owner, XXW_TR(view->owner->app, "Scan options"), &view->options, &accepted);
        if (status != XXWIDGETS_OK)
            return status;
    }
    job = view->job;
    if (job) {
        int done;
        char *current;
        /* Only the UI removes queue entries. The worker appends under mutex. */
        for (size_t drained = 0; drained < QUEUE_LIMIT; ++drained) {
            directory_record *record;
            xx_mutex_lock(job->mutex);
            record = job->first;
            xx_mutex_unlock(job->mutex);
            if (!record)
                break;
            if (view->count == view->capacity) {
                size_t next = view->capacity ? view->capacity * 2 : 64;
                directory_record **grown = next > view->capacity && next <= (size_t)INT_MAX && next <= SIZE_MAX / sizeof(*grown)
                    ? realloc(view->records, next * sizeof(*grown))
                    : NULL;
                if (!grown) {
                    xx_atomic_store(&job->cancel, 1);
                    return XXWIDGETS_OUT_OF_MEMORY;
                }
                view->records = grown;
                view->capacity = next;
            }
            xx_mutex_lock(job->mutex);
            job->first = record->next;
            if (!job->first)
                job->last = NULL;
            --job->queued;
            xx_mutex_unlock(job->mutex);
            record->next = NULL;
            view->records[view->count++] = record;
            view->dirty = 1;
        }
        xx_mutex_lock(job->mutex);
        view->progress = job->progress;
        done = job->done && !job->queued;
        current = xxwidgets_strdup(job->current ? job->current : "");
        xx_mutex_unlock(job->mutex);
        if (!done)
            view->progress.state = XXWIDGETS_SCAN_DIRECTORY_RUNNING;
        if (!current) {
            xx_atomic_store(&job->cancel, 1);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        status = xxwidgets_widget_set_text(CONTROL(view, CURRENT_FILE), current);
        free(current);
        if (status != XXWIDGETS_OK)
            return status;
        if (done) {
            if (!xx_thread_join(job->thread, NULL))
                return XXWIDGETS_PLATFORM_ERROR;
            free_job(job);
            view->job = NULL;
        }
    }
    status = enable_controls(view, view->job != NULL);
    if (status == XXWIDGETS_OK)
        status = render_status(view);
    if (status == XXWIDGETS_OK && view->dirty) {
        status = render_tree(view);
        if (status == XXWIDGETS_OK)
            view->dirty = 0;
    }
    return status;
}
xxwidgets_status xxwidgets_scan_directory_get_progress(const xxwidgets_scan_directory *view, xxwidgets_scan_directory_progress *progress)
{
    if (!view || !progress)
        return XXWIDGETS_INVALID_ARGUMENT;
    *progress = view->progress;
    /* RUNNING remains so until poll has drained/reaped the worker. */
    return XXWIDGETS_OK;
}
size_t xxwidgets_scan_directory_result_count(const xxwidgets_scan_directory *view)
{
    return view ? view->count : 0;
}
xxwidgets_status xxwidgets_scan_directory_get_result(
    const xxwidgets_scan_directory *view, size_t index, xxwidgets_scan_directory_result *result)
{
    if (!view || !result || index >= view->count)
        return XXWIDGETS_INVALID_ARGUMENT;
    *result = view->records[index]->result;
    return XXWIDGETS_OK;
}
typedef struct directory_dialog {
    xxwidgets_widget *window, *close;
    xxwidgets_scan_directory *view;
    int close_requested, relayout;
} directory_dialog;

static void directory_dialog_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    directory_dialog *state = (directory_dialog *)user;
    (void)app;
    if ((event->type == XXWIDGETS_EVENT_CLOSE && event->widget == state->window) ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == state->close) ||
        (event->type == XXWIDGETS_EVENT_SHORTCUT && event->value == 1)) {
        state->close_requested = 1;
        return;
    }
    if (event->type == XXWIDGETS_EVENT_RESIZE && event->widget == state->window) {
        state->relayout = 1;
        return;
    }
    if (state->view) (void)xxwidgets_scan_directory_handle_event(state->view, event);
}

xxwidgets_status xxwidgets_scan_directory_dialog(xxwidgets_widget *owner, const char *title, const char *path,
    const xxwidgets_scan_options *options, xxwidgets_scan_directory_file_fn scanner, void *user)
{
    const xxwidgets_shortcut shortcuts[] = {{"Esc", 1}};
    directory_dialog state = {0};
    xxwidgets_app *app;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    xxwidgets_widget *previous_default;
    xxwidgets_status status, cleanup;
    int width = 110, height = 32, owner_enabled, disabled = 0, attached = 0, closing = 0;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !title || !xxwidgets_valid_utf8(title) || !scanner) return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit) return XXWIDGETS_BUSY;
    if (xxwidgets_app_backend(app) == XXWIDGETS_BACKEND_TUI) {
        width = owner->rect.width - 4 > 66 ? owner->rect.width - 4 : 66;
        height = owner->rect.height - 2 > 19 ? owner->rect.height - 2 : 19;
    }
    if (owner->rect.x > INT_MAX - width - 2 || owner->rect.y > INT_MAX - height - 1) return XXWIDGETS_INVALID_ARGUMENT;
    previous_event = app->on_event;
    previous_user = app->user_data;
    previous_default = app->modal_default;
    owner_enabled = owner->enabled;
    /* The form refuses to start under a modal window, so it comes first. */
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title, (xxwidgets_rect){owner->rect.x + 2, owner->rect.y + 1, width, height}, &state.window);
    if (status == XXWIDGETS_OK) status = xxwidgets_window_set_minimum_size(state.window, 66, 19);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON, XXW_TR(app, "Close"), (xxwidgets_rect){width - 13, height - 3, 12, 2}, &state.close);
    if (status == XXWIDGETS_OK) status = xxwidgets_scan_directory_create(state.window, (xxwidgets_rect){1, 1, width - 2, height - 4}, &state.view);
    if (status == XXWIDGETS_OK) status = xxwidgets_scan_directory_set_scanner(state.view, scanner, user);
    if (status == XXWIDGETS_OK && path && path[0]) status = xxwidgets_scan_directory_set_path(state.view, path);
    if (status == XXWIDGETS_OK && options) status = xxwidgets_scan_directory_set_options(state.view, options);
    if (status == XXWIDGETS_OK) status = xxwidgets_window_set_shortcuts(state.window, shortcuts, 1);
    if (status != XXWIDGETS_OK) goto finish;
    app->on_event = directory_dialog_event;
    app->user_data = &state;
    app->modal_window = state.window;
    app->modal_default = NULL;
    status = xxwidgets_widget_set_enabled(owner, 0);
    if (status != XXWIDGETS_OK) goto finish;
    disabled = 1;
    status = xxwidgets_window_set_modality(state.window, owner, XXWIDGETS_WINDOW_MODAL);
    if (status != XXWIDGETS_OK) goto finish;
    attached = 1;
    (void)xxwidgets_widget_focus(CONTROL(state.view, PATH));
    while (status == XXWIDGETS_OK && !app->quit) {
        xxwidgets_scan_directory_progress progress;
        status = xxwidgets_app_poll(app, 30);
        if (status != XXWIDGETS_OK || app->quit) break;
        if (state.relayout) {
            xxwidgets_rect bounds;
            state.relayout = 0;
            if (xxwidgets_widget_get_rect(state.window, &bounds) == XXWIDGETS_OK && bounds.width >= 66 && bounds.height >= 19) {
                (void)xxwidgets_scan_directory_set_rect(state.view, (xxwidgets_rect){1, 1, bounds.width - 2, bounds.height - 4});
                (void)xxwidgets_widget_set_rect(state.close, (xxwidgets_rect){bounds.width - 13, bounds.height - 3, 12, 2});
            }
        }
        if (state.close_requested) {
            state.close_requested = 0;
            closing = 1;
            (void)xxwidgets_scan_directory_cancel(state.view);
        }
        /* The form's own Options dialog is modal too: step aside for it. */
        app->modal_window = NULL;
        status = xxwidgets_scan_directory_poll(state.view);
        app->modal_window = state.window;
        if (closing && xxwidgets_scan_directory_get_progress(state.view, &progress) == XXWIDGETS_OK &&
            progress.state != XXWIDGETS_SCAN_DIRECTORY_RUNNING)
            break;
    }
finish:
    if (state.view) {
        /* A worker still running must end before the form can go. */
        (void)xxwidgets_scan_directory_cancel(state.view);
        app->modal_window = NULL;
        for (int tries = 0; tries < 6000; ++tries) {
            xxwidgets_scan_directory_progress progress;
            if (xxwidgets_scan_directory_get_progress(state.view, &progress) != XXWIDGETS_OK || progress.state != XXWIDGETS_SCAN_DIRECTORY_RUNNING) break;
            xx_thread_sleep(10);
            (void)xxwidgets_scan_directory_poll(state.view);
        }
    }
    if (disabled) {
        cleanup = xxwidgets_widget_set_enabled(owner, owner_enabled);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (attached) {
        cleanup = xxwidgets_window_set_modality(state.window, NULL, XXWIDGETS_MODELESS);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    app->modal_window = NULL;
    app->modal_default = previous_default;
    app->on_event = previous_event;
    app->user_data = previous_user;
    if (state.view) {
        cleanup = xxwidgets_scan_directory_destroy(state.view);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (state.window) {
        cleanup = xxwidgets_widget_destroy(state.window);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    return status;
}

xxwidgets_status xxwidgets_scan_directory_destroy(xxwidgets_scan_directory *view)
{
    if (!view)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (ui_busy(view))
        return XXWIDGETS_BUSY;
    if (view->job) {
        int done;
        xx_mutex_lock(view->job->mutex);
        done = view->job->done;
        xx_mutex_unlock(view->job->mutex);
        if (!done)
            return XXWIDGETS_BUSY;
        if (!xx_thread_join(view->job->thread, NULL))
            return XXWIDGETS_PLATFORM_ERROR;
        free_job(view->job);
        view->job = NULL;
    }
    for (size_t i = 0; i < XXWIDGETS_SCAN_DIRECTORY_CONTROL_COUNT; ++i) {
        if (view->controls[i]) {
            xxwidgets_status status = xxwidgets_widget_destroy(view->controls[i]);
            if (status != XXWIDGETS_OK)
                return status;
            view->controls[i] = NULL;
        }
    }
    clear_records(view);
    free(view->root);
    free(view);
    return XXWIDGETS_OK;
}
