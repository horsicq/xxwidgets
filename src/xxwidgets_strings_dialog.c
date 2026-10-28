/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_strings.h"
#include "xxwidgets/xxwidgets_process.h"
#include "xxwidgets/xxwidgets_combobox.h"
#include "xxfclib/threads/xx_threads.h"
#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct strings_job {
    xxwidgets_strings_source *source;
    const char *filter, *const *columns, *path;
    xx_mutex_t *lock;
    xx_pd_struct snapshot;
    uint64_t count;
    xxwidgets_strings_search_options options;
    int extended;
    int stop, done, cancelled;
    xxwidgets_status result;
} strings_job;

static bool strings_observe(const xx_pd_struct *progress, void *user)
{
    strings_job *job = user;
    int stop;
    xx_mutex_lock(job->lock);
    job->snapshot = *progress;
    stop = job->stop;
    xx_mutex_unlock(job->lock);
    return stop != 0;
}

static int strings_worker(void *user)
{
    strings_job *job = user;
    xx_pd_struct progress = xx_pd_init();
    xx_pd_observer previous = xx_pd_set_observer(&progress, strings_observe, job);
    xxwidgets_status result = job->extended ? job->source->search_ex(job->source->user, &job->options, job->filter, job->columns, &progress, &job->count) :
        job->source->search(job->source->user, job->filter, job->columns, &progress, &job->count);
    /* A provider commits only after its own final cancellation check. A late
     * UI request after that commit must not discard the published index. */
    int cancelled = progress.is_stop != 0;
    xx_pd_set_observer(previous.progress, previous.callback, previous.user_data);
    xx_mutex_lock(job->lock);
    job->snapshot = progress;
    job->cancelled = cancelled;
    job->result = result;
    job->done = 1;
    xx_mutex_unlock(job->lock);
    return 0;
}

static xxwidgets_status strings_progress(void *user, int stop, xx_pd_struct *progress, int *finished)
{
    strings_job *job = user;
    xx_mutex_lock(job->lock);
    if (stop) job->stop = 1;
    *progress = job->snapshot;
    *finished = job->done;
    xx_mutex_unlock(job->lock);
    return XXWIDGETS_OK;
}

static xxwidgets_status strings_disk_worker(void *user, xx_pd_struct *pd, xxwidgets_disk_write_reporter *reporter)
{
    strings_job *job = user;
    xxwidgets_status status;
    (void)xxwidgets_disk_write_set_file(reporter, job->path, 0, 1, 1);
    status = job->source->save_ex ? job->source->save_ex(job->source->user, job->path, pd, reporter)
                                 : job->source->save(job->source->user, job->path, pd);
    if (status == XXWIDGETS_OK && !job->source->save_ex && xx_pd_is_stopped(pd))
        status = XXWIDGETS_UNAVAILABLE;
    if (status == XXWIDGETS_OK)
        (void)xxwidgets_disk_write_set_file(reporter, job->path, 1, 1, 1);
    return status;
}
static xxwidgets_status run_job(xxwidgets_widget *owner, strings_job *job, int *cancelled)
{
    xxwidgets_app *app = owner->app;
    xxwidgets_widget *previous_modal, *previous_default;
    xx_pd_struct progress = xx_pd_init();
    xx_thread_t *thread;
    xxwidgets_status status;
    int joined;
    if (job->path) {
        xxwidgets_disk_write_result result;
        status = xxwidgets_disk_write_run(owner, "Saving strings", job->path, strings_disk_worker, job, &result);
        if (cancelled) *cancelled = result.cancelled;
        return status;
    }
    if (app->dispatch_depth || app->polling || app->syncing || app->quit ||
        (app->modal_window && app->modal_window != owner)) return XXWIDGETS_BUSY;
    job->lock = xx_mutex_create();
    if (!job->lock) return XXWIDGETS_OUT_OF_MEMORY;
    thread = xx_thread_create(strings_worker, job);
    if (!thread) { xx_mutex_destroy(job->lock); return XXWIDGETS_PLATFORM_ERROR; }
    /* Progress disables this browser and attaches to it as native owner. */
    previous_modal = app->modal_window;
    previous_default = app->modal_default;
    app->modal_window = NULL;
    app->modal_default = NULL;
    status = xxwidgets_process_dialog(owner, job->path ? "Saving strings" : "Searching strings", &progress, strings_progress, job);
    app->modal_window = previous_modal;
    app->modal_default = previous_default;
    if (status != XXWIDGETS_OK) {
        xx_mutex_lock(job->lock); job->stop = 1; xx_mutex_unlock(job->lock);
    }
    joined = xx_thread_join(thread, NULL);
    xx_mutex_destroy(job->lock);
    if (!joined) return XXWIDGETS_PLATFORM_ERROR;
    if (cancelled) *cancelled = job->cancelled;
    /* The provider's final stop check is its transaction boundary. Once it
     * committed, a late progress-window failure must not make the caller put
     * old counters/options next to the new index. Treat completed work as
     * authoritative and let the browser refresh the committed result. */
    if (job->done && job->result == XXWIDGETS_OK && !job->cancelled) return XXWIDGETS_OK;
    return status == XXWIDGETS_OK ? (job->cancelled ? XXWIDGETS_OK : job->result) : status;
}

void xxwidgets_strings_search_options_init(xxwidgets_strings_search_options *options)
{
    if (options) {
        memset(options, 0, sizeof(*options));
        options->minimum_length = 5;
        options->ansi = options->unicode = 1;
    }
}

static int valid_options(const xxwidgets_strings_source *source, const xxwidgets_strings_search_options *options)
{
    return options && options->minimum_length >= 2 &&
        (options->ansi == 0 || options->ansi == 1) && (options->unicode == 0 || options->unicode == 1) &&
        (options->ansi || options->unicode) &&
        (options->null_terminated == 0 || options->null_terminated == 1) &&
        (options->links == 0 || options->links == 1) && (options->regexp == 0 || options->regexp == 1) &&
        memchr(options->expression, 0, sizeof(options->expression)) && xxwidgets_valid_utf8(options->expression) &&
        (source->format_count ? options->format < source->format_count : options->format == 0) &&
        (source->map_mode_count ? options->map_mode < source->map_mode_count : options->map_mode == 0);
}

static xxwidgets_status search_worker(xxwidgets_widget *owner, xxwidgets_strings_source *source,
    const xxwidgets_strings_search_options *options, const char *filter,
    const char *const columns[XXWIDGETS_STRINGS_COLUMN_COUNT], int *cancelled)
{
    strings_job job = {0};
    xxwidgets_strings_search_options previous_options;
    uint64_t previous_count, previous_total;
    xxwidgets_status status;
    if (cancelled) *cancelled = 0;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !source ||
        (!source->search && !source->search_ex) || !xxwidgets_valid_utf8(filter) || !columns ||
        (source->search_ex && !valid_options(source, options)))
        return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < XXWIDGETS_STRINGS_COLUMN_COUNT; ++i)
        if (!xxwidgets_valid_utf8(columns[i])) return XXWIDGETS_INVALID_ARGUMENT;
    job.source = source; job.filter = filter; job.columns = columns;
    previous_count = source->count; previous_total = source->total_count; previous_options = source->options;
    if (source->search_ex) {
        job.options = *options; job.extended = 1;
        if (source->format_count && !job.options.format) job.options.map_mode = 0;
    }
    status = run_job(owner, &job, cancelled);
    if (status == XXWIDGETS_OK && !job.cancelled) {
        if (job.count > source->total_count) status = XXWIDGETS_INVALID_ARGUMENT;
        else { source->count = job.count; if (job.extended) source->options = job.options; }
    }
    if (status != XXWIDGETS_OK || job.cancelled) {
        source->count = previous_count; source->total_count = previous_total; source->options = previous_options;
    }
    return status;
}

xxwidgets_status xxwidgets_strings_source_search(xxwidgets_widget *owner, xxwidgets_strings_source *source,
    const char *filter, const char *const columns[XXWIDGETS_STRINGS_COLUMN_COUNT], int *cancelled)
{
    return search_worker(owner, source, source ? &source->options : NULL, filter, columns, cancelled);
}

xxwidgets_status xxwidgets_strings_source_search_ex(xxwidgets_widget *owner, xxwidgets_strings_source *source,
    const xxwidgets_strings_search_options *options, const char *filter,
    const char *const columns[XXWIDGETS_STRINGS_COLUMN_COUNT], int *cancelled)
{
    if (!source || !source->search_ex) { if (cancelled) *cancelled = 0; return XXWIDGETS_INVALID_ARGUMENT; }
    return search_worker(owner, source, options, filter, columns, cancelled);
}

xxwidgets_status xxwidgets_strings_source_set_page(xxwidgets_strings *view, const xxwidgets_strings_source *source, uint64_t first)
{
    xxwidgets_string_record records[XXWIDGETS_STRINGS_PAGE_SIZE];
    uint64_t numbers[XXWIDGETS_STRINGS_PAGE_SIZE];
    size_t received = 0, capacity;
    xxwidgets_status status;
    if (!view || !source || !source->read || source->count > source->total_count || first > source->count)
        return XXWIDGETS_INVALID_ARGUMENT;
    capacity = source->count - first < XXWIDGETS_STRINGS_PAGE_SIZE ? (size_t)(source->count - first) : XXWIDGETS_STRINGS_PAGE_SIZE;
    if (!capacity) return xxwidgets_strings_set_records(view, NULL, 0);
    status = source->read(source->user, first, capacity, records, numbers, &received);
    if (status != XXWIDGETS_OK) return status;
    if (received != capacity) return XXWIDGETS_INVALID_ARGUMENT;
    return xxwidgets_strings_set_records_numbered(view, records, numbers, received);
}

typedef struct strings_modal {
    xxwidgets_widget *window, *close, *path, *summary, *search, *previous, *next, *page, *go;
    xxwidgets_widget *format, *map, *ansi, *unicode, *null_terminated, *links, *minimum_label, *minimum, *regexp, *expression, *error;
    xxwidgets_strings *view;
    xxwidgets_strings_source *source;
    uint64_t first, file_size;
    char *filter, *columns[XXWIDGETS_STRINGS_COLUMN_COUNT];
    int done, pending;
    xxwidgets_status failure;
} strings_modal;

static xxwidgets_status source_error(strings_modal *modal, const char *text)
{
    return xxwidgets_widget_set_text(modal->error ? modal->error :
        xxwidgets_strings_control(modal->view, XXWIDGETS_STRINGS_DETAILS), text);
}

static xxwidgets_status source_options_enabled(strings_modal *modal)
{
    xxwidgets_status status = XXWIDGETS_OK;
    if (!modal->source->search_ex) return status;
    if (modal->source->format_count && modal->format->value == 0) {
        status = xxwidgets_widget_set_value(modal->map, 0);
    }
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(modal->map,
        modal->source->map_mode_count > 1 && (!modal->source->format_count || modal->format->value != 0));
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(modal->expression, modal->regexp->value != 0);
    return status;
}

/* Labels are copied by the combo; accept Unicode format names too. */
static xxwidgets_status source_combo(xxwidgets_widget *combo, const char *const *names, size_t count,
    const char *fallback, size_t selected)
{
    xx_meta_string *records;
    xx_str_w_s *labels;
    xxwidgets_status status = XXWIDGETS_OK;
    if (!count) { names = &fallback; count = 1; }
    if (count > INT_MAX || count > SIZE_MAX / sizeof(*records) || count > SIZE_MAX / sizeof(*labels)) return XXWIDGETS_INVALID_ARGUMENT;
    if (!names) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i)
        if (!xxwidgets_valid_utf8(names[i]) || !names[i][0]) return XXWIDGETS_INVALID_ARGUMENT;
    records = calloc(count, sizeof(*records)); labels = calloc(count, sizeof(*labels));
    if (!records || !labels) { free(records); free(labels); return XXWIDGETS_OUT_OF_MEMORY; }
    for (size_t i = 0; i < count; ++i) {
        const unsigned char *p = (const unsigned char *)names[i];
        size_t length = strlen(names[i]), used = 0;
        if (length > (SIZE_MAX / sizeof(wchar_t)) - 1) { status = XXWIDGETS_OUT_OF_MEMORY; break; }
        labels[i].data = calloc(length + 1, sizeof(wchar_t));
        if (!labels[i].data) { status = XXWIDGETS_OUT_OF_MEMORY; break; }
        while (*p) {
            uint32_t cp = *p++;
            if (cp >= 0xc0) {
                unsigned bytes = cp < 0xe0 ? 1 : cp < 0xf0 ? 2 : 3;
                cp &= bytes == 1 ? 31 : bytes == 2 ? 15 : 7;
                while (bytes--) cp = (cp << 6) | (*p++ & 63);
            }
            if (sizeof(wchar_t) == 2 && cp > 0xffff) {
                cp -= 0x10000; labels[i].data[used++] = (wchar_t)(0xd800 | (cp >> 10));
                labels[i].data[used++] = (wchar_t)(0xdc00 | (cp & 1023));
            } else labels[i].data[used++] = (wchar_t)cp;
        }
        labels[i].length = used; labels[i].capacity = length + 1; labels[i].is_view = true;
        records[i].meta_string = labels + i; records[i].var.type = XX_VAR_TYPE_UINT64; records[i].var.val.u64 = i;
    }
    if (status == XXWIDGETS_OK) status = xxwidgets_combobox_set_records(combo, records, count);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(combo, (int)selected);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(combo, count > 1);
    for (size_t i = 0; i < count; ++i) free(labels[i].data);
    free(labels); free(records);
    return status;
}

static uint64_t page_count(const xxwidgets_strings_source *source)
{
    return source->count ? (source->count - 1) / XXWIDGETS_STRINGS_PAGE_SIZE + 1 : 1;
}

static xxwidgets_status source_labels(strings_modal *modal)
{
    char text[192], page[32];
    uint64_t shown = xxwidgets_strings_count(modal->view), pages = page_count(modal->source);
    xxwidgets_status status;
    snprintf(text, sizeof(text), "Rows %" PRIu64 "-%" PRIu64 " | %" PRIu64 " / %" PRIu64 " strings",
        shown ? modal->first + 1 : 0, modal->first + shown, modal->source->count, modal->source->total_count);
    status = xxwidgets_widget_set_text(xxwidgets_strings_control(modal->view, XXWIDGETS_STRINGS_COUNT), text);
    snprintf(text, sizeof(text), "%" PRIu64 " bytes | Page %" PRIu64 " / %" PRIu64 " | Search applies options and filters to the complete source",
        modal->file_size, modal->first / XXWIDGETS_STRINGS_PAGE_SIZE + 1, pages);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(modal->summary, text);
    snprintf(page, sizeof(page), "%" PRIu64, modal->first / XXWIDGETS_STRINGS_PAGE_SIZE + 1);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(modal->page, page);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(modal->previous, modal->first != 0);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(modal->next,
        modal->source->count - modal->first > XXWIDGETS_STRINGS_PAGE_SIZE);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(
        xxwidgets_strings_control(modal->view, XXWIDGETS_STRINGS_SAVE), modal->source->count != 0);
    return status;
}

static xxwidgets_status source_layout(strings_modal *modal, int width, int height)
{
    int extended = modal->source->search_ex != NULL, top = extended ? 11 : 5;
    xxwidgets_status status = xxwidgets_strings_set_rect(modal->view, (xxwidgets_rect){1, top, width - 2, height - top - 5 - extended});
#define PLACE(widget, x, y, w, h) if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_rect(modal->widget, (xxwidgets_rect){x, y, w, h})
    PLACE(path, 1, 1, width - 2, 1);
    PLACE(summary, 1, 3, width - (extended ? 2 : 16), 1);
    PLACE(search, width - 14, extended ? 8 : 3, 12, 2);
    PLACE(previous, 1, height - 3, 12, 2);
    PLACE(next, 14, height - 3, 12, 2);
    PLACE(page, 28, height - 3, 22, 2);
    PLACE(go, 51, height - 3, 8, 2);
    PLACE(close, width - 13, height - 3, 12, 2);
    if (extended) {
        PLACE(format, 1, 5, 17, 2); PLACE(map, 19, 5, 18, 2);
        PLACE(ansi, 38, 5, 9, 2); PLACE(unicode, 47, 5, 12, 2);
        PLACE(null_terminated, 60, 5, 20, 2); PLACE(links, 80, 5, 9, 2);
        PLACE(minimum_label, 90, 5, 15, 2); PLACE(minimum, 105, 5, 7, 2);
        PLACE(regexp, 1, 8, 12, 2); PLACE(expression, 14, 8, width - 29, 2);
        PLACE(error, 1, height - 5, width - 2, 1);
    }
#undef PLACE
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_set_rect(xxwidgets_strings_control(modal->view, XXWIDGETS_STRINGS_FILTER),
            (xxwidgets_rect){9, top, width - (extended ? 11 : 25), 2});
    if (extended && status == XXWIDGETS_OK) status = xxwidgets_widget_set_rect(
        xxwidgets_strings_control(modal->view, XXWIDGETS_STRINGS_SAVE), (xxwidgets_rect){width - 14, 5, 12, 2});
    return status;
}

static void strings_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    strings_modal *modal = user;
    xxwidgets_widget *table = modal->view ? xxwidgets_strings_control(modal->view, XXWIDGETS_STRINGS_RESULTS) : NULL;
    xxwidgets_status status = XXWIDGETS_OK;
    (void)app;
    if (modal->source->search_ex &&
        ((event->widget == modal->regexp && (event->type == XXWIDGETS_EVENT_CHANGE || event->type == XXWIDGETS_EVENT_CLICK)) ||
        (event->widget == modal->format && (event->type == XXWIDGETS_EVENT_SELECT || event->type == XXWIDGETS_EVENT_CHANGE)))) {
        status = source_options_enabled(modal);
        if (status != XXWIDGETS_OK) { modal->failure = status; modal->done = 1; }
        return;
    }
    if ((event->type == XXWIDGETS_EVENT_CLOSE && event->widget == modal->window) ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == modal->close) ||
        (event->type == XXWIDGETS_EVENT_SHORTCUT && event->value == 1)) { modal->done = 1; return; }
    if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == modal->search) { modal->pending = 3; return; }
        if (event->widget == modal->previous) { modal->pending = 4; return; }
        if (event->widget == modal->next) { modal->pending = 5; return; }
        if (event->widget == modal->go) { modal->pending = 6; return; }
        if (event->widget == xxwidgets_strings_control(modal->view, XXWIDGETS_STRINGS_VIEW)) { modal->pending = 1; return; }
        if (event->widget == xxwidgets_strings_control(modal->view, XXWIDGETS_STRINGS_SAVE)) { modal->pending = 2; return; }
    }
    if (event->type == XXWIDGETS_EVENT_ACTIVATE && event->widget == table) { modal->pending = 1; return; }
    if (event->type == XXWIDGETS_EVENT_SHORTCUT && event->value == 2) { modal->pending = 3; return; }
    if (event->type == XXWIDGETS_EVENT_RESIZE && event->widget == modal->window && modal->view) {
        xxwidgets_rect bounds;
        status = xxwidgets_widget_get_rect(modal->window, &bounds);
        if (status == XXWIDGETS_OK) status = source_layout(modal, bounds.width, bounds.height);
    } else if (modal->view && event->widget != modal->page &&
        event->widget != xxwidgets_strings_control(modal->view, XXWIDGETS_STRINGS_FILTER) &&
        !(event->widget == table && event->type == XXWIDGETS_EVENT_CHANGE)) {
        status = xxwidgets_strings_handle_event(modal->view, event);
        if (status == XXWIDGETS_OK) status = source_labels(modal);
    }
    if (status != XXWIDGETS_OK) { modal->failure = status; modal->done = 1; }
}

static xxwidgets_status save_csv(strings_modal *modal)
{
    char *path = NULL;
    int accepted = 0, cancelled = 0;
    xxwidgets_status status = xxwidgets_file_dialog(modal->window, XXWIDGETS_FILE_DIALOG_SAVE,
        XXW_TR(modal->window->app, "Save filtered strings as CSV"), "strings.csv", &path, &accepted);
    if (status == XXWIDGETS_OK && accepted) {
        strings_job job = {0}; job.source = modal->source; job.path = path;
        status = run_job(modal->window, &job, &cancelled);
    }
    free(path);
    if (status != XXWIDGETS_OK || accepted) {
        const char *error = status != XXWIDGETS_OK && modal->source->error ? modal->source->error(modal->source->user) : NULL;
        return source_error(modal, error && error[0] && xxwidgets_valid_utf8(error) ? error :
            status != XXWIDGETS_OK ? "Cannot save CSV" : cancelled ? "Save cancelled" : "CSV saved");
    }
    return status;
}

static xxwidgets_status read_options(strings_modal *modal, xxwidgets_strings_search_options *options, const char **message)
{
    char minimum[32], *end;
    unsigned long value;
    size_t required;
    int format, map;
    xxwidgets_status status;
    *options = modal->source->options;
    *message = "Cannot read search options";
    status = xxwidgets_widget_get_text(modal->minimum, minimum, sizeof(minimum), &required);
    if (status == XXWIDGETS_BUFFER_TOO_SMALL) { *message = "Minimum length must be an integer of at least 2"; return XXWIDGETS_INVALID_ARGUMENT; }
    if (status != XXWIDGETS_OK) return status;
    errno = 0; value = strtoul(minimum, &end, 10);
    if (!minimum[0] || minimum[0] == '-' || minimum[0] == '+' || *end || errno || value < 2 || value > UINT_MAX) {
        *message = "Minimum length must be an integer of at least 2"; return XXWIDGETS_INVALID_ARGUMENT;
    }
    options->minimum_length = (unsigned)value;
#define GET(member) if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(modal->member, &options->member)
    GET(ansi); GET(unicode); GET(null_terminated); GET(links); GET(regexp);
#undef GET
    if (status != XXWIDGETS_OK) return status;
    if (!options->ansi && !options->unicode) { *message = "Select ANSI or Unicode before searching"; return XXWIDGETS_INVALID_ARGUMENT; }
    status = xxwidgets_widget_get_text(modal->expression, options->expression, sizeof(options->expression), &required);
    if (status == XXWIDGETS_BUFFER_TOO_SMALL) { *message = "Regular expression exceeds 1023 UTF-8 bytes"; return XXWIDGETS_INVALID_ARGUMENT; }
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(modal->format, &format);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(modal->map, &map);
    if (status != XXWIDGETS_OK) return status;
    options->format = format < 0 ? SIZE_MAX : (size_t)format;
    options->map_mode = map < 0 ? SIZE_MAX : (size_t)map;
    if (!valid_options(modal->source, options)) { *message = "Select a valid format and memory map"; return XXWIDGETS_INVALID_ARGUMENT; }
    return XXWIDGETS_OK;
}

static xxwidgets_status reload_options(strings_modal *modal)
{
    const xxwidgets_strings_search_options *options = &modal->source->options;
    char minimum[32];
    xxwidgets_status status;
    snprintf(minimum, sizeof(minimum), "%u", options->minimum_length);
    status = xxwidgets_widget_set_text(modal->minimum, minimum);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(modal->expression, options->expression);
    if (status == XXWIDGETS_OK) status = source_combo(modal->format, modal->source->formats, modal->source->format_count, "Binary", options->format);
    if (status == XXWIDGETS_OK) status = source_combo(modal->map, modal->source->map_modes, modal->source->map_mode_count, "Regions", options->map_mode);
#define SET(member) if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(modal->member, options->member)
    SET(ansi); SET(unicode); SET(null_terminated); SET(links); SET(regexp);
#undef SET
    if (status == XXWIDGETS_OK) status = source_options_enabled(modal);
    return status;
}

static xxwidgets_status search_source(strings_modal *modal)
{
    char *filter = xxwidgets_strdup(xxwidgets_strings_control(modal->view, XXWIDGETS_STRINGS_FILTER)->text);
    char *columns[XXWIDGETS_STRINGS_COLUMN_COUNT] = {0};
    xxwidgets_widget *table = xxwidgets_strings_control(modal->view, XXWIDGETS_STRINGS_RESULTS);
    xxwidgets_status status = XXWIDGETS_OK;
    xxwidgets_strings_search_options options;
    const char *message = NULL;
    int cancelled = 0;
    if (!filter) status = XXWIDGETS_OUT_OF_MEMORY;
    for (size_t i = 0; i < XXWIDGETS_STRINGS_COLUMN_COUNT && status == XXWIDGETS_OK; ++i) {
        columns[i] = xxwidgets_strdup(xxwidgets_table_filter(table, i));
        if (!columns[i]) status = XXWIDGETS_OUT_OF_MEMORY;
    }
    if (status == XXWIDGETS_OK && modal->source->search_ex) status = read_options(modal, &options, &message);
    if (status == XXWIDGETS_OK) {
        status = modal->source->search_ex ?
            xxwidgets_strings_source_search_ex(modal->window, modal->source, &options, filter, (const char *const *)columns, &cancelled) :
            xxwidgets_strings_source_search(modal->window, modal->source, filter, (const char *const *)columns, &cancelled);
        if (status != XXWIDGETS_OK && modal->source->error) message = modal->source->error(modal->source->user);
    }
    if (status == XXWIDGETS_OK && !cancelled) {
        free(modal->filter); modal->filter = filter; filter = NULL;
        for (size_t i = 0; i < XXWIDGETS_STRINGS_COLUMN_COUNT; ++i) {
            free(modal->columns[i]); modal->columns[i] = columns[i]; columns[i] = NULL;
        }
        modal->first = 0;
        status = xxwidgets_strings_source_set_page(modal->view, modal->source, 0);
        /* A successful provider can redetect a same-path replacement file or
         * normalize a selector. Both labels and selections must describe the
         * committed source, rather than the copied pre-search choices. */
        if (status == XXWIDGETS_OK && modal->source->search_ex) status = reload_options(modal);
        if (status == XXWIDGETS_OK) status = source_labels(modal);
        if (status == XXWIDGETS_OK) status = source_error(modal, "Search complete");
    } else {
        (void)xxwidgets_widget_set_text(xxwidgets_strings_control(modal->view, XXWIDGETS_STRINGS_FILTER), modal->filter);
        for (size_t i = 0; i < XXWIDGETS_STRINGS_COLUMN_COUNT; ++i) (void)xxwidgets_table_set_filter(table, i, modal->columns[i]);
        status = source_error(modal, cancelled ? "Search cancelled; previous results retained" :
            message && message[0] && xxwidgets_valid_utf8(message) ? message :
            status == XXWIDGETS_INVALID_ARGUMENT ? "Cannot search strings: invalid expression or options" : "Cannot search strings");
    }
    free(filter);
    for (size_t i = 0; i < XXWIDGETS_STRINGS_COLUMN_COUNT; ++i) free(columns[i]);
    return status;
}

static xxwidgets_status create_options(strings_modal *modal)
{
    xxwidgets_app *app = modal->window->app;
    const xxwidgets_strings_search_options *options = &modal->source->options;
    xxwidgets_status status = XXWIDGETS_OK;
    char minimum[32];
    snprintf(minimum, sizeof(minimum), "%u", options->minimum_length);
#define CREATE(member, kind, text) if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(app, modal->window, kind, text, (xxwidgets_rect){0, 0, 1, 1}, &modal->member)
    CREATE(format, XXWIDGETS_COMBOBOX, "Format"); CREATE(map, XXWIDGETS_COMBOBOX, "Memory map");
    CREATE(ansi, XXWIDGETS_CHECKBOX, "ANSI"); CREATE(unicode, XXWIDGETS_CHECKBOX, "Unicode");
    CREATE(null_terminated, XXWIDGETS_CHECKBOX, "Null-terminated"); CREATE(links, XXWIDGETS_CHECKBOX, "Links");
    CREATE(minimum_label, XXWIDGETS_LABEL, "Minimum length"); CREATE(minimum, XXWIDGETS_EDIT, minimum);
    CREATE(regexp, XXWIDGETS_CHECKBOX, "RegExp"); CREATE(expression, XXWIDGETS_EDIT, options->expression);
    CREATE(error, XXWIDGETS_LABEL, "");
#undef CREATE
    if (status == XXWIDGETS_OK) status = reload_options(modal);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(xxwidgets_strings_control(modal->view, XXWIDGETS_STRINGS_SAVE), "Save");
    return status;
}

static xxwidgets_status page_source(strings_modal *modal, int action)
{
    uint64_t first = modal->first;
    xxwidgets_status status;
    if (action == 4) first = first >= XXWIDGETS_STRINGS_PAGE_SIZE ? first - XXWIDGETS_STRINGS_PAGE_SIZE : 0;
    else if (action == 5) {
        if (modal->source->count - first <= XXWIDGETS_STRINGS_PAGE_SIZE) return XXWIDGETS_OK;
        first += XXWIDGETS_STRINGS_PAGE_SIZE;
    } else {
        char *end;
        unsigned long long page;
        const char *text = modal->page->text;
        errno = 0; page = strtoull(text, &end, 10);
        if (!text[0] || text[0] == '-' || *end || errno || !page || page > page_count(modal->source))
            return xxwidgets_widget_set_text(xxwidgets_strings_control(modal->view, XXWIDGETS_STRINGS_DETAILS), "Invalid page number");
        first = ((uint64_t)page - 1) * XXWIDGETS_STRINGS_PAGE_SIZE;
    }
    status = xxwidgets_strings_source_set_page(modal->view, modal->source, first);
    if (status == XXWIDGETS_OK) { modal->first = first; status = source_labels(modal); }
    return status;
}

xxwidgets_status xxwidgets_strings_source_dialog(xxwidgets_widget *owner, const char *title,
    const char *path, uint64_t size, xxwidgets_strings_source *source)
{
    strings_modal modal = {0};
    xxwidgets_app *app;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    xxwidgets_widget *focus = NULL, *widget, *previous_default;
    char description[128];
    xxwidgets_status status, cleanup;
    int enabled, hidden, disabled = 0, attached = 0, width = 128, height = 34;
    const xxwidgets_shortcut shortcuts[] = {{"Esc", 1}, {"Ctrl+F", 2}};
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !title || !xxwidgets_valid_utf8(title) ||
        !path || !xxwidgets_valid_utf8(path) || !source || !source->read || source->count > source->total_count ||
        owner->rect.x > INT_MAX - 2 || owner->rect.y > INT_MAX - 2) return XXWIDGETS_INVALID_ARGUMENT;
    if (source->search_ex) {
        if (!valid_options(source, &source->options) || source->format_count > INT_MAX || source->map_mode_count > INT_MAX ||
            (source->format_count && !source->formats) || (source->map_mode_count && !source->map_modes)) return XXWIDGETS_INVALID_ARGUMENT;
        for (size_t i = 0; i < source->format_count; ++i)
            if (!xxwidgets_valid_utf8(source->formats[i]) || !source->formats[i][0]) return XXWIDGETS_INVALID_ARGUMENT;
        for (size_t i = 0; i < source->map_mode_count; ++i)
            if (!xxwidgets_valid_utf8(source->map_modes[i]) || !source->map_modes[i][0]) return XXWIDGETS_INVALID_ARGUMENT;
        width = 148; height = 40;
    }
    app = owner->app;
    if (xxwidgets_app_backend(app) == XXWIDGETS_BACKEND_TUI) {
        int minimum_width = source->search_ex ? 128 : 90, minimum_height = source->search_ex ? 32 : 24;
        width = owner->rect.width - 4 < minimum_width ? minimum_width : owner->rect.width - 4 > 160 ? 160 : owner->rect.width - 4;
        height = owner->rect.height - 2 < minimum_height ? minimum_height : owner->rect.height - 2 > 48 ? 48 : owner->rect.height - 2;
    }
    if (app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit) return XXWIDGETS_BUSY;
    modal.source = source; modal.file_size = size;
    modal.filter = xxwidgets_strdup("");
    if (!modal.filter) return XXWIDGETS_OUT_OF_MEMORY;
    for (size_t i = 0; i < XXWIDGETS_STRINGS_COLUMN_COUNT; ++i) {
        modal.columns[i] = xxwidgets_strdup("");
        if (!modal.columns[i]) {
            for (size_t j = 0; j < i; ++j) free(modal.columns[j]);
            free(modal.filter); return XXWIDGETS_OUT_OF_MEMORY;
        }
    }
    previous_default = app->modal_default; app->modal_default = NULL;
    previous_event = app->on_event; previous_user = app->user_data; enabled = owner->enabled;
    hidden = app->create_hidden; app->create_hidden = 1;
    for (widget = app->widgets; widget; widget = widget->next)
        if (widget->parent == owner && xxwidgets_focusable(widget)) { focus = widget; break; }
    app->on_event = strings_event; app->user_data = &modal;
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title,
        (xxwidgets_rect){owner->rect.x + 2, owner->rect.y + 2, width, height}, &modal.window);
    app->create_hidden = hidden;
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_window = modal.window;
    status = xxwidgets_window_set_minimum_size(modal.window, source->search_ex ? 128 : 90, source->search_ex ? 32 : 24);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, modal.window, XXWIDGETS_LABEL, path, (xxwidgets_rect){1, 1, width - 2, 1}, &modal.path);
    snprintf(description, sizeof(description), "%" PRIu64 " bytes | %" PRIu64 " strings", size, source->count);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, modal.window, XXWIDGETS_LABEL, description, (xxwidgets_rect){1, 3, width - 2, 1}, &modal.summary);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_strings_create_internal(modal.window, (xxwidgets_rect){1, 5, width - 2, height - 10}, source->search_ex != NULL, &modal.view);
    if (status == XXWIDGETS_OK) {
        xxwidgets_strings_control(modal.view, XXWIDGETS_STRINGS_RESULTS)->table_external_filters = 1;
        status = xxwidgets_strings_source_set_page(modal.view, source, 0);
    }
#define BUTTON(member, label) if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(app, modal.window, XXWIDGETS_BUTTON, label, (xxwidgets_rect){0, 0, 1, 1}, &modal.member)
    BUTTON(close, "Close"); BUTTON(search, "Search"); BUTTON(previous, "Previous"); BUTTON(next, "Next"); BUTTON(go, "Go");
#undef BUTTON
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(app, modal.window, XXWIDGETS_EDIT, "1", (xxwidgets_rect){0, 0, 1, 1}, &modal.page);
    if (status == XXWIDGETS_OK && source->search_ex) status = create_options(&modal);
    if (status == XXWIDGETS_OK) status = source_layout(&modal, width, height);
    if (status == XXWIDGETS_OK) status = source_labels(&modal);
    if (status == XXWIDGETS_OK && source->records_are_previews) {
        status = xxwidgets_widget_set_text(xxwidgets_strings_control(modal.view, XXWIDGETS_STRINGS_VIEW), "View preview");
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(xxwidgets_strings_control(modal.view, XXWIDGETS_STRINGS_COPY), "Copy preview");
        if (status == XXWIDGETS_OK) status = source_error(&modal, "Row text is a preview; Save exports complete strings");
    }
    if (status == XXWIDGETS_OK) status = xxwidgets_window_set_shortcuts(modal.window, shortcuts, 2);
    if (status != XXWIDGETS_OK) goto finish;
    if ((!source->save && !source->save_ex) || !xxwidgets_file_dialog_available(app)) {
        status = xxwidgets_widget_set_visible(xxwidgets_strings_control(modal.view, XXWIDGETS_STRINGS_SAVE), 0);
        if (status != XXWIDGETS_OK) goto finish;
    }
    if (!source->search && !source->search_ex) {
        status = xxwidgets_widget_set_enabled(modal.search, 0);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(xxwidgets_strings_control(modal.view, XXWIDGETS_STRINGS_FILTER), 0);
        if (status != XXWIDGETS_OK) goto finish;
    }
    status = xxwidgets_widget_set_enabled(owner, 0);
    if (status != XXWIDGETS_OK) goto finish;
    disabled = 1;
    status = xxwidgets_window_set_modality(modal.window, owner, XXWIDGETS_WINDOW_MODAL);
    if (status != XXWIDGETS_OK) goto finish;
    attached = 1;
    status = xxwidgets_widget_set_visible(modal.window, 1);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_focus(xxwidgets_strings_control(modal.view, XXWIDGETS_STRINGS_RESULTS));
    while (status == XXWIDGETS_OK && !modal.done && !app->quit) {
        status = xxwidgets_app_poll(app, 30);
        if (status == XXWIDGETS_OK && !modal.done && !app->quit && modal.pending) {
            int action = modal.pending; modal.pending = 0;
            if (action == 1) {
                size_t index;
                xxwidgets_string_record record;
                status = xxwidgets_strings_get_selection(modal.view, &index, &record);
                if (status == XXWIDGETS_OK && index != SIZE_MAX)
                    status = xxwidgets_text_dialog(modal.window, source->records_are_previews ? "Decoded string preview" : "Decoded string", record.text);
            } else if (action == 2 && (source->save || source->save_ex)) status = save_csv(&modal);
            else if (action == 3 && (source->search || source->search_ex)) status = search_source(&modal);
            else if (action >= 4) status = page_source(&modal, action);
        }
    }
    if (status == XXWIDGETS_OK) status = modal.failure;
finish:
    if (modal.window) (void)xxwidgets_widget_set_visible(modal.window, 0);
    if (attached) { cleanup = xxwidgets_window_set_modality(modal.window, NULL, XXWIDGETS_MODELESS); if (status == XXWIDGETS_OK) status = cleanup; }
    if (modal.view) { cleanup = xxwidgets_strings_destroy(modal.view); if (status == XXWIDGETS_OK) status = cleanup; }
    if (modal.window) { cleanup = xxwidgets_widget_destroy(modal.window); if (status == XXWIDGETS_OK) status = cleanup; }
    app->modal_window = NULL; app->modal_default = previous_default; app->on_event = previous_event; app->user_data = previous_user;
    if (disabled) { cleanup = xxwidgets_widget_set_enabled(owner, enabled); if (status == XXWIDGETS_OK) status = cleanup; }
    if (focus) (void)xxwidgets_widget_focus(focus);
    free(modal.filter);
    for (size_t i = 0; i < XXWIDGETS_STRINGS_COLUMN_COUNT; ++i) free(modal.columns[i]);
    return status;
}

/* Legacy metadata receives the same worker search and bounded native pages. */
typedef struct memory_source {
    const xxwidgets_string_record *records;
    size_t count, matched, *indices;
    uint64_t max_offset, max_address, max_size;
    xxwidgets_strings_save_fn save;
    void *user;
} memory_source;

static int contains(const char *text, const char *filter)
{
    size_t n = strlen(filter);
    if (!n) return 1;
    for (; *text; ++text) {
        size_t i;
        for (i = 0; i < n && text[i]; ++i) {
            unsigned char a = (unsigned char)text[i], b = (unsigned char)filter[i];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
        }
        if (i == n) return 1;
    }
    return 0;
}

static xxwidgets_status memory_read(void *user, uint64_t first, size_t capacity,
    xxwidgets_string_record *records, uint64_t *numbers, size_t *received)
{
    memory_source *source = user;
    if (first > source->matched || capacity > source->matched - (size_t)first) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < capacity; ++i) {
        size_t index = source->indices ? source->indices[(size_t)first + i] : (size_t)first + i;
        records[i] = source->records[index]; numbers[i] = index;
    }
    *received = capacity;
    return XXWIDGETS_OK;
}

static int memory_matches(const memory_source *source, size_t index, const char *filter, const char *const columns[7])
{
    const xxwidgets_string_record *r = source->records + index;
    char number[32], offset[32], address[32] = "", size[32];
    const char *cells[7];
    if (!contains(r->text, filter) && !contains(r->encoding, filter)) return 0;
    snprintf(number, sizeof(number), "%zu", index);
    snprintf(offset, sizeof(offset), "%0*" PRIX64, source->max_offset > UINT32_MAX ? 16 : 8, r->offset);
    if (r->has_address) snprintf(address, sizeof(address), "%0*" PRIX64, source->max_address > UINT32_MAX ? 16 : 8, r->address);
    snprintf(size, sizeof(size), "%0*" PRIX64, source->max_size > UINT32_MAX ? 16 : 8, r->byte_size);
    cells[0] = number; cells[1] = offset; cells[2] = address; cells[3] = r->region ? r->region : "";
    cells[4] = size; cells[5] = r->encoding; cells[6] = r->text;
    for (size_t i = 0; i < 7; ++i) if (!contains(cells[i], columns[i])) return 0;
    return 1;
}

static xxwidgets_status memory_search(void *user, const char *filter, const char *const columns[7], xx_pd_struct *progress, uint64_t *count)
{
    memory_source *source = user;
    size_t *indices = NULL, used = 0;
    int active = filter[0] != 0, level = xx_pd_enter_level(progress, source->count, "Filtering strings");
    for (size_t i = 0; i < 7; ++i) active |= columns[i][0] != 0;
    if (active && source->count) {
        if (source->count > SIZE_MAX / sizeof(*indices)) return XXWIDGETS_OUT_OF_MEMORY;
        indices = malloc(source->count * sizeof(*indices));
        if (!indices) return XXWIDGETS_OUT_OF_MEMORY;
    }
    if (active) for (size_t i = 0; i < source->count; ++i) {
        if (!(i & 1023)) {
            xx_pd_set_current(progress, level, i);
            if (xx_pd_is_stopped(progress)) { free(indices); xx_pd_leave_level(progress, level); return XXWIDGETS_OK; }
        }
        if (memory_matches(source, i, filter, columns)) indices[used++] = i;
    }
    else used = source->count;
    xx_pd_set_current(progress, level, source->count);
    xx_pd_leave_level(progress, level);
    if (xx_pd_is_stopped(progress)) { free(indices); return XXWIDGETS_OK; }
    free(source->indices); source->indices = indices; source->matched = used; *count = used;
    return XXWIDGETS_OK;
}

typedef struct memory_csv { char *text; size_t length, capacity; } memory_csv;
static int csv_append(memory_csv *csv, const char *text)
{
    size_t n = strlen(text), required;
    char *grown;
    if (n >= SIZE_MAX - csv->length) return 0;
    required = csv->length + n + 1;
    if (required > csv->capacity) {
        size_t capacity = csv->capacity <= SIZE_MAX / 2 ? csv->capacity * 2 : SIZE_MAX;
        if (capacity < required) capacity = required;
        grown = realloc(csv->text, capacity);
        if (!grown) return 0;
        csv->text = grown; csv->capacity = capacity;
    }
    memcpy(csv->text + csv->length, text, n + 1); csv->length += n;
    return 1;
}
static int csv_cell(memory_csv *csv, const char *text)
{
    if (!csv_append(csv, "\"")) return 0;
    for (; *text; ++text) {
        char c[2] = {*text, 0};
        if (*text == '"' && !csv_append(csv, "\"")) return 0;
        if (!csv_append(csv, c)) return 0;
    }
    return csv_append(csv, "\"");
}
static xxwidgets_status memory_save_impl(void *user, const char *path, xx_pd_struct *progress, xxwidgets_disk_write_reporter *reporter)
{
    memory_source *source = user;
    memory_csv csv = {0};
    int level = xx_pd_enter_level(progress, source->matched, "Saving strings");
    xxwidgets_status status = XXWIDGETS_OK;
    if (!csv_append(&csv, "Offset,Bytes,Characters,Encoding,String\r\n")) status = XXWIDGETS_OUT_OF_MEMORY;
    for (size_t i = 0; i < source->matched && status == XXWIDGETS_OK; ++i) {
        const xxwidgets_string_record *r = source->records + (source->indices ? source->indices[i] : i);
        char numbers[96];
        if (!(i & 1023)) {
            xx_pd_set_current(progress, level, i);
            if (xx_pd_is_stopped(progress)) break;
        }
        snprintf(numbers, sizeof(numbers), "0x%016" PRIX64 ",%" PRIu64 ",%" PRIu64 ",", r->offset, r->byte_size, r->character_length);
        if (!csv_append(&csv, numbers) || !csv_cell(&csv, r->encoding) || !csv_append(&csv, ",") ||
            !csv_cell(&csv, r->text) || !csv_append(&csv, "\r\n")) status = XXWIDGETS_OUT_OF_MEMORY;
    }
    if (status == XXWIDGETS_OK && xx_pd_is_stopped(progress)) status = XXWIDGETS_UNAVAILABLE;
    if (status == XXWIDGETS_OK) {
        if (reporter) (void)xxwidgets_disk_write_set_bytes(reporter, 0, csv.length, 1);
        status = source->save(path, csv.text, csv.length, source->user);
        if (reporter && status == XXWIDGETS_OK)
            (void)xxwidgets_disk_write_set_bytes(reporter, csv.length, csv.length, 1);
    }
    free(csv.text); xx_pd_leave_level(progress, level);
    return status;
}
static xxwidgets_status memory_save(void *user, const char *path, xx_pd_struct *progress)
{
    return memory_save_impl(user, path, progress, NULL);
}
static xxwidgets_status memory_save_ex(void *user, const char *path, xx_pd_struct *progress, xxwidgets_disk_write_reporter *reporter)
{
    return memory_save_impl(user, path, progress, reporter);
}

xxwidgets_status xxwidgets_strings_dialog(xxwidgets_widget *owner, const char *title,
    const char *path, uint64_t size, const xxwidgets_string_record *records, size_t count,
    xxwidgets_strings_save_fn save, void *user)
{
    memory_source memory = {0};
    xxwidgets_strings_source source = {0};
    xxwidgets_status status;
    if ((!records && count) || count > SIZE_MAX / sizeof(size_t)) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i) {
        if (!xxwidgets_valid_utf8(records[i].text) || !xxwidgets_valid_utf8(records[i].encoding) || !records[i].encoding[0] ||
            (records[i].region && !xxwidgets_valid_utf8(records[i].region)) || records[i].byte_size > UINT64_MAX - records[i].offset)
            return XXWIDGETS_INVALID_ARGUMENT;
        if (records[i].offset > memory.max_offset) memory.max_offset = records[i].offset;
        if (records[i].address > memory.max_address) memory.max_address = records[i].address;
        if (records[i].byte_size > memory.max_size) memory.max_size = records[i].byte_size;
    }
    memory.records = records; memory.count = memory.matched = count; memory.save = save; memory.user = user;
    source.count = source.total_count = count; source.read = memory_read; source.search = memory_search;
    source.save = save ? memory_save : NULL; source.save_ex = save ? memory_save_ex : NULL; source.user = &memory;
    status = xxwidgets_strings_source_dialog(owner, title, path, size, &source);
    free(memory.indices);
    return status;
}
