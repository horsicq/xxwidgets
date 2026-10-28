/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_extractor.h"
#include "xxwidgets/xxwidgets_process.h"
#include "xxwidgets/xxwidgets_combobox.h"
#include "xxfclib/threads/xx_threads.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

struct xxwidgets_extractor {
    xxwidgets_widget *owner, *controls[XXWIDGETS_EXTRACTOR_CONTROL_COUNT];
    xxwidgets_extractor_source *source;
    xxwidgets_rect bounds;
    xxwidgets_extractor_record records[XXWIDGETS_EXTRACTOR_PAGE_SIZE];
    char *columns[XXWIDGETS_EXTRACTOR_COLUMN_COUNT];
    uint64_t first, staged_types;
    size_t rows;
    xxwidgets_extractor_mode offered_modes[3];
    size_t offered_mode_count;
    int pending;
};
#define C(v, id) ((v)->controls[XXWIDGETS_EXTRACTOR_##id])
enum {
    ACTION_SCAN = 1,
    ACTION_PREVIOUS,
    ACTION_NEXT,
    ACTION_GO,
    ACTION_SELECT,
    ACTION_SAVE,
    ACTION_DUMP_ALL,
    ACTION_DUMP,
    ACTION_TYPES
};
static const char *const titles[9] = {"#", "Offset", "Address", "Region", "Size", "Method", "Type", "Info", "Name"};
static const char *const modes[] = {"Heuristic", "Format", "Raw"};

void xxwidgets_extractor_options_init(xxwidgets_extractor_options *o)
{
    if (o) {
        memset(o, 0, sizeof(*o));
        o->deep_scan = 1;
    }
}
static int valid_options(const xxwidgets_extractor_source *s, const xxwidgets_extractor_options *o)
{
    return s && o && o->mode >= XXWIDGETS_EXTRACTOR_HEURISTIC && o->mode <= XXWIDGETS_EXTRACTOR_RAW && (o->all_types == 0 || o->all_types == 1) &&
           (o->deep_scan == 0 || o->deep_scan == 1) && s->type_count <= 64 && (s->type_count == 64 || !(o->type_mask >> s->type_count)) &&
           (s->format_count ? o->format < s->format_count : o->format == 0) && (s->map_mode_count ? o->map_mode < s->map_mode_count : o->map_mode == 0) &&
           (!s->format_mode_masks || (s->format_count && !(s->format_mode_masks[o->format] & ~7u) && (s->format_mode_masks[o->format] & (1u << o->mode))));
}
typedef struct extractor_job {
    xxwidgets_extractor_source *source;
    xxwidgets_extractor_options options;
    const char *const *columns;
    const char *path;
    const uint64_t *indices;
    size_t index_count;
    int action, directory, stop, done, cancelled;
    uint64_t count;
    xx_mutex_t *lock;
    xx_pd_struct snapshot;
    xxwidgets_status result;
} extractor_job;
static bool observe_job(const xx_pd_struct *pd, void *user)
{
    extractor_job *j = user;
    int stop;
    xx_mutex_lock(j->lock);
    j->snapshot = *pd;
    stop = j->stop;
    xx_mutex_unlock(j->lock);
    return stop != 0;
}
static int worker_job(void *user)
{
    extractor_job *j = user;
    xx_pd_struct pd = xx_pd_init();
    xx_pd_observer previous = xx_pd_set_observer(&pd, observe_job, j);
    xxwidgets_status result = j->source->scan(j->source->user, &j->options, j->columns, &pd, &j->count);
    int cancelled = pd.is_stop != 0;
    xx_pd_set_observer(previous.progress, previous.callback, previous.user_data);
    xx_mutex_lock(j->lock);
    j->result = result;
    j->snapshot = pd;
    j->cancelled = cancelled;
    j->done = 1;
    xx_mutex_unlock(j->lock);
    return 0;
}
static xxwidgets_status poll_job(void *user, int stop, xx_pd_struct *pd, int *finished)
{
    extractor_job *j = user;
    xx_mutex_lock(j->lock);
    if (stop) j->stop = 1;
    *pd = j->snapshot;
    *finished = j->done;
    xx_mutex_unlock(j->lock);
    return XXWIDGETS_OK;
}
static xxwidgets_status extractor_disk_worker(void *user, xx_pd_struct *pd, xxwidgets_disk_write_reporter *reporter)
{
    extractor_job *j = user;
    uint64_t files = j->directory ? j->source->count : 1;
    xxwidgets_status status;
    (void)xxwidgets_disk_write_set_file(reporter, j->path, 0, files, 1);
    if (j->action == ACTION_SAVE)
        status = j->source->save_ex ? j->source->save_ex(j->source->user, j->path, pd, reporter)
                                    : j->source->save(j->source->user, j->path, pd);
    else {
        /* Opaque legacy callbacks have no promised byte-counter layout.
         * Leave unreported progress undetermined instead of guessing units. */
        status = j->source->dump_ex ? j->source->dump_ex(j->source->user, j->path, j->directory, j->indices, j->index_count, pd, reporter)
                                    : j->source->dump(j->source->user, j->path, j->directory, j->indices, j->index_count, pd);
    }
    /* Legacy callbacks signal cancellation through pd alone. Disk-aware
     * callbacks return UNAVAILABLE on cancellation and OK after commit. */
    if (status == XXWIDGETS_OK && xx_pd_is_stopped(pd) &&
        ((j->action == ACTION_SAVE && !j->source->save_ex) ||
         (j->action != ACTION_SAVE && !j->source->dump_ex)))
        status = XXWIDGETS_UNAVAILABLE;
    if (status == XXWIDGETS_OK)
        (void)xxwidgets_disk_write_set_file(reporter, j->path, files, files, 1);
    return status;
}
static xxwidgets_status run_job(xxwidgets_widget *owner, extractor_job *j, int *cancelled)
{
    xxwidgets_app *app = owner->app;
    xxwidgets_widget *previous_modal, *previous_default;
    xx_pd_struct pd = xx_pd_init();
    xx_thread_t *thread;
    xxwidgets_status status;
    int joined;
    if (cancelled) *cancelled = 0;
    if (j->action != ACTION_SCAN) {
        xxwidgets_disk_write_result result;
        status = xxwidgets_disk_write_run(owner, j->action == ACTION_SAVE ? "Saving extractor results" : "Dumping objects",
            j->path, extractor_disk_worker, j, &result);
        if (cancelled) *cancelled = result.cancelled;
        return status;
    }
    if (app->dispatch_depth || app->polling || app->syncing || app->quit || (app->modal_window && app->modal_window != owner)) return XXWIDGETS_BUSY;
    j->lock = xx_mutex_create();
    if (!j->lock) return XXWIDGETS_OUT_OF_MEMORY;
    thread = xx_thread_create(worker_job, j);
    if (!thread) {
        xx_mutex_destroy(j->lock);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    previous_modal = app->modal_window;
    previous_default = app->modal_default;
    app->modal_window = NULL;
    app->modal_default = NULL;
    status = xxwidgets_process_dialog(owner,
                                      j->action == ACTION_SCAN   ? "Scanning extractable objects"
                                      : j->action == ACTION_SAVE ? "Saving extractor results"
                                                                 : "Extracting objects",
                                      &pd, poll_job, j);
    app->modal_window = previous_modal;
    app->modal_default = previous_default;
    if (status != XXWIDGETS_OK) {
        xx_mutex_lock(j->lock);
        j->stop = 1;
        xx_mutex_unlock(j->lock);
    }
    joined = xx_thread_join(thread, NULL);
    xx_mutex_destroy(j->lock);
    if (!joined) return XXWIDGETS_PLATFORM_ERROR;
    if (cancelled) *cancelled = j->cancelled;
    /* A provider's final cancellation check is its commit boundary. */
    if (j->done && j->result == XXWIDGETS_OK && !j->cancelled) return XXWIDGETS_OK;
    return status == XXWIDGETS_OK ? (j->cancelled ? XXWIDGETS_OK : j->result) : status;
}
xxwidgets_status xxwidgets_extractor_source_scan(xxwidgets_widget *owner, xxwidgets_extractor_source *s, const xxwidgets_extractor_options *options,
                                                 const char *const columns[9], int *cancelled)
{
    extractor_job job = {0};
    xxwidgets_extractor_options old;
    uint64_t count;
    xxwidgets_status status;
    int stopped = 0;
    if (cancelled) *cancelled = 0;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !s || !s->scan || !columns || !valid_options(s, options)) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < 9; ++i)
        if (!xxwidgets_valid_utf8(columns[i])) return XXWIDGETS_INVALID_ARGUMENT;
    job.source = s;
    job.options = *options;
    job.columns = columns;
    job.action = ACTION_SCAN;
    old = s->options;
    count = s->count;
    status = run_job(owner, &job, &stopped);
    if (status == XXWIDGETS_OK && !stopped) {
        s->options = job.options;
        s->count = job.count;
    } else {
        s->options = old;
        s->count = count;
    }
    if (cancelled) *cancelled = stopped;
    return status;
}
static xxwidgets_status combo(xxwidgets_widget *combo, const char *const *names, size_t count, const char *fallback, size_t selected)
{
    xx_meta_string *records;
    xx_str_w_s *labels;
    xxwidgets_status status = XXWIDGETS_OK;
    if (!count) {
        names = &fallback;
        count = 1;
    }
    if (count > INT_MAX || count > SIZE_MAX / sizeof(*records) || count > SIZE_MAX / sizeof(*labels)) return XXWIDGETS_INVALID_ARGUMENT;
    if (!names) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i)
        if (!xxwidgets_valid_utf8(names[i]) || !names[i][0]) return XXWIDGETS_INVALID_ARGUMENT;
    records = calloc(count, sizeof(*records));
    labels = calloc(count, sizeof(*labels));
    if (!records || !labels) {
        free(records);
        free(labels);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < count; ++i) {
        const unsigned char *p = (const unsigned char *)names[i];
        size_t length = strlen(names[i]), used = 0;
        if (length > (SIZE_MAX / sizeof(wchar_t)) - 1) {
            status = XXWIDGETS_OUT_OF_MEMORY;
            break;
        }
        labels[i].data = calloc(length + 1, sizeof(wchar_t));
        if (!labels[i].data) {
            status = XXWIDGETS_OUT_OF_MEMORY;
            break;
        }
        while (*p) {
            uint32_t cp = *p++;
            if (cp >= 0xc0) {
                unsigned bytes = cp < 0xe0 ? 1 : cp < 0xf0 ? 2 : 3;
                cp &= bytes == 1 ? 31 : bytes == 2 ? 15 : 7;
                while (bytes--) cp = (cp << 6) | (*p++ & 63);
            }
            if (sizeof(wchar_t) == 2 && cp > 0xffff) {
                cp -= 0x10000;
                labels[i].data[used++] = (wchar_t)(0xd800 | (cp >> 10));
                labels[i].data[used++] = (wchar_t)(0xdc00 | (cp & 1023));
            } else labels[i].data[used++] = (wchar_t)cp;
        }
        labels[i].length = used;
        labels[i].capacity = length + 1;
        labels[i].is_view = true;
        records[i].meta_string = labels + i;
        records[i].var.type = XX_VAR_TYPE_UINT64;
        records[i].var.val.u64 = i;
    }
    if (status == XXWIDGETS_OK) status = xxwidgets_combobox_set_records(combo, records, count);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(combo, (int)selected);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(combo, count > 1);
    for (size_t i = 0; i < count; ++i) free(labels[i].data);
    free(labels);
    free(records);
    return status;
}

static xxwidgets_status status_text(xxwidgets_extractor *v, const char *message)
{
    return xxwidgets_widget_set_text(C(v, STATUS), message);
}
static xxwidgets_status labels(xxwidgets_extractor *v)
{
    char text[160], page[32];
    uint64_t pages = v->source->count ? (v->source->count - 1) / XXWIDGETS_EXTRACTOR_PAGE_SIZE + 1 : 1;
    xxwidgets_status status;
    snprintf(text, sizeof(text), "0x%" PRIX64 " bytes | %" PRIu64 " objects | Page %" PRIu64 " / %" PRIu64, v->source->size, v->source->count,
             v->first / XXWIDGETS_EXTRACTOR_PAGE_SIZE + 1, pages);
    status = xxwidgets_widget_set_text(C(v, SUMMARY), text);
    snprintf(page, sizeof(page), "%" PRIu64, v->first / XXWIDGETS_EXTRACTOR_PAGE_SIZE + 1);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(C(v, PAGE), page);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(C(v, PREVIOUS), v->first != 0);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(C(v, NEXT), v->source->count - v->first > XXWIDGETS_EXTRACTOR_PAGE_SIZE);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(C(v, DUMP_ALL), (v->source->dump || v->source->dump_ex) && v->source->count != 0);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(C(v, DUMP), (v->source->dump || v->source->dump_ex) && v->rows != 0);
    return status;
}
static xxwidgets_status load_modes(xxwidgets_extractor *v, size_t format, xxwidgets_extractor_mode desired)
{
    const char *labels[3];
    xxwidgets_extractor_mode offered[3];
    size_t count = 0, selected = SIZE_MAX;
    unsigned mask = v->source->format_mode_masks ? v->source->format_mode_masks[format] : 7u;
    xxwidgets_status status;
    if (!mask || (mask & ~7u)) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < 3; ++i)
        if (mask & (1u << i)) {
            labels[count] = modes[i];
            offered[count] = (xxwidgets_extractor_mode)i;
            if (i == (size_t)desired) selected = count;
            ++count;
        }
    /* Heuristic sorts first when available; otherwise the first supported mode. */
    if (selected == SIZE_MAX) selected = 0;
    status = combo(C(v, MODE_SELECTOR), labels, count, "Heuristic", selected);
    if (status == XXWIDGETS_OK) {
        memcpy(v->offered_modes, offered, count * sizeof(*offered));
        v->offered_mode_count = count;
    }
    return status;
}
static xxwidgets_status controls_enabled(xxwidgets_extractor *v)
{
    int all = 0, format = 0, selection = 0;
    xxwidgets_extractor_mode desired = XXWIDGETS_EXTRACTOR_HEURISTIC;
    xxwidgets_status status = xxwidgets_widget_get_value(C(v, ALL), &all);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(C(v, FORMAT_SELECTOR), &format);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(C(v, MODE_SELECTOR), &selection);
    if (status == XXWIDGETS_OK && selection >= 0 && (size_t)selection < v->offered_mode_count) desired = v->offered_modes[selection];
    if (status == XXWIDGETS_OK && (format < 0 || (v->source->format_count && (size_t)format >= v->source->format_count))) return XXWIDGETS_INVALID_ARGUMENT;
    if (status == XXWIDGETS_OK) status = load_modes(v, (size_t)format, desired);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(C(v, TYPES), !all && v->source->type_count && v->source->scan);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(C(v, MAP_SELECTOR), v->source->map_mode_count > 1 && v->source->scan);
    return status;
}
static xxwidgets_status load_options(xxwidgets_extractor *v)
{
    const xxwidgets_extractor_options *o = &v->source->options;
    xxwidgets_status status = combo(C(v, FORMAT_SELECTOR), v->source->formats, v->source->format_count, "Binary", o->format);
    if (status == XXWIDGETS_OK) status = combo(C(v, MAP_SELECTOR), v->source->map_modes, v->source->map_mode_count, "Regions", o->map_mode);
    if (status == XXWIDGETS_OK) status = load_modes(v, o->format, o->mode);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(C(v, ALL), o->all_types);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(C(v, DEEP), o->deep_scan);
    v->staged_types = o->type_mask;
    if (status == XXWIDGETS_OK) status = controls_enabled(v);
    if (!v->source->scan) {
        const xxwidgets_extractor_control_id ids[] = {XXWIDGETS_EXTRACTOR_FORMAT_SELECTOR,
                                                      XXWIDGETS_EXTRACTOR_MAP_SELECTOR,
                                                      XXWIDGETS_EXTRACTOR_MODE_SELECTOR,
                                                      XXWIDGETS_EXTRACTOR_ALL,
                                                      XXWIDGETS_EXTRACTOR_DEEP,
                                                      XXWIDGETS_EXTRACTOR_SCAN};
        for (size_t i = 0; status == XXWIDGETS_OK && i < sizeof(ids) / sizeof(ids[0]); ++i) status = xxwidgets_widget_set_enabled(v->controls[ids[i]], 0);
    }
    return status;
}
static int valid_bounds(xxwidgets_rect b)
{
    return b.x >= 0 && b.y >= 0 && b.width >= 118 && b.height >= 30 && b.x <= 32767 - b.width && b.y <= 32767 - b.height;
}
xxwidgets_status xxwidgets_extractor_set_rect(xxwidgets_extractor *v, xxwidgets_rect b)
{
    xxwidgets_rect r[XXWIDGETS_EXTRACTOR_CONTROL_COUNT];
    int table_height;
    xxwidgets_status status = XXWIDGETS_OK;
    if (!v || !valid_bounds(b)) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < XXWIDGETS_EXTRACTOR_CONTROL_COUNT; ++i) r[i] = (xxwidgets_rect){b.x, b.y, 1, 1};
    table_height = (b.height - 12) * 3 / 5;
#define R(id, dx, dy, w, h) r[XXWIDGETS_EXTRACTOR_##id] = (xxwidgets_rect){b.x + (dx), b.y + (dy), w, h}
    R(FORMAT_SELECTOR, 0, 0, 19, 2);
    R(MAP_SELECTOR, 20, 0, 20, 2);
    R(SUMMARY, 41, 0, b.width - 81, 2);
    R(DUMP_ALL, b.width - 38, 0, 13, 2);
    R(SAVE, b.width - 24, 0, 11, 2);
    R(SCAN, b.width - 12, 0, 12, 2);
    R(MODE_SELECTOR, 0, 3, 19, 2);
    R(ALL, 21, 3, 8, 2);
    R(TYPES, 30, 3, b.width - 49, 2);
    R(DEEP, b.width - 18, 3, 18, 2);
    R(TABLE, 0, 6, b.width, table_height);
    R(HEX_LABEL, 0, 7 + table_height, b.width, 1);
    R(HEX, 0, 8 + table_height, b.width, b.height - table_height - 14);
    R(STATUS, 0, b.height - 5, b.width, 1);
    R(PREVIOUS, 0, b.height - 3, 13, 2);
    R(PAGE, 14, b.height - 3, 14, 2);
    R(GO, 29, b.height - 3, 7, 2);
    R(NEXT, 37, b.height - 3, 11, 2);
    R(DUMP, b.width - 19, b.height - 3, 19, 2);
#undef R
    for (size_t i = 0; status == XXWIDGETS_OK && i < XXWIDGETS_EXTRACTOR_CONTROL_COUNT; ++i) status = xxwidgets_widget_set_rect(v->controls[i], r[i]);
    if (status == XXWIDGETS_OK) v->bounds = b;
    return status;
}
xxwidgets_widget *xxwidgets_extractor_control(const xxwidgets_extractor *v, xxwidgets_extractor_control_id id)
{
    return v && id >= 0 && id < XXWIDGETS_EXTRACTOR_CONTROL_COUNT ? v->controls[id] : NULL;
}
xxwidgets_status xxwidgets_extractor_set_page(xxwidgets_extractor *v, uint64_t first)
{
    xxwidgets_extractor_record records[XXWIDGETS_EXTRACTOR_PAGE_SIZE];
    char numbers[XXWIDGETS_EXTRACTOR_PAGE_SIZE][4][32];
    const char *cells[XXWIDGETS_EXTRACTOR_PAGE_SIZE * 9];
    size_t count, received = 0;
    xxwidgets_status status;
    if (!v || first > v->source->count || (first == v->source->count && first)) return XXWIDGETS_INVALID_ARGUMENT;
    count = v->source->count - first > XXWIDGETS_EXTRACTOR_PAGE_SIZE ? XXWIDGETS_EXTRACTOR_PAGE_SIZE : (size_t)(v->source->count - first);
    status = v->source->read(v->source->user, first, count, records, &received);
    if (status != XXWIDGETS_OK) return status;
    if (received != count) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i) {
        const xxwidgets_extractor_record *r = records + i;
        const char *strings[] = {r->region, r->method, r->type, r->info, r->name};
        if (r->size > UINT64_MAX - r->offset || (r->has_address != 0 && r->has_address != 1)) return XXWIDGETS_INVALID_ARGUMENT;
        for (size_t k = 0; k < 5; ++k)
            if (strings[k] && !xxwidgets_valid_utf8(strings[k])) return XXWIDGETS_INVALID_ARGUMENT;
        snprintf(numbers[i][0], 32, "%" PRIu64, r->number);
        snprintf(numbers[i][1], 32, "%0*" PRIX64, r->offset > UINT32_MAX ? 16 : 8, r->offset);
        numbers[i][2][0] = 0;
        if (r->has_address) snprintf(numbers[i][2], 32, "%0*" PRIX64, r->address > UINT32_MAX ? 16 : 8, r->address);
        snprintf(numbers[i][3], 32, "%0*" PRIX64, r->size > UINT32_MAX ? 16 : 8, r->size);
        cells[i * 9] = numbers[i][0];
        cells[i * 9 + 1] = numbers[i][1];
        cells[i * 9 + 2] = numbers[i][2];
        cells[i * 9 + 3] = r->region;
        cells[i * 9 + 4] = numbers[i][3];
        cells[i * 9 + 5] = r->method;
        cells[i * 9 + 6] = r->type;
        cells[i * 9 + 7] = r->info;
        cells[i * 9 + 8] = r->name;
    }
    status = xxwidgets_table_set_rows(C(v, TABLE), cells, count);
    if (status == XXWIDGETS_OK) {
        memcpy(v->records, records, count * sizeof(*records));
        v->rows = count;
        v->first = first;
        /* Only numeric metadata is used after the provider's next read. */
        for (size_t i = 0; i < count; ++i) v->records[i].region = v->records[i].method = v->records[i].type = v->records[i].info = v->records[i].name = NULL;
        status = labels(v);
        if (status == XXWIDGETS_OK) v->pending = ACTION_SELECT;
    }
    return status;
}
xxwidgets_status xxwidgets_extractor_destroy(xxwidgets_extractor *v)
{
    xxwidgets_status status = XXWIDGETS_OK, cleanup;
    if (!v) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < XXWIDGETS_EXTRACTOR_CONTROL_COUNT; ++i)
        if (v->controls[i]) {
            cleanup = xxwidgets_widget_destroy(v->controls[i]);
            if (status == XXWIDGETS_OK) status = cleanup;
        }
    for (size_t i = 0; i < 9; ++i) free(v->columns[i]);
    free(v);
    return status;
}
xxwidgets_status xxwidgets_extractor_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_extractor_source *source, xxwidgets_extractor **out)
{
    xxwidgets_extractor *v;
    xxwidgets_status status = XXWIDGETS_OK;
    static const xxwidgets_kind kinds[] = {XXWIDGETS_COMBOBOX, XXWIDGETS_COMBOBOX, XXWIDGETS_LABEL,    XXWIDGETS_BUTTON, XXWIDGETS_BUTTON,
                                           XXWIDGETS_BUTTON,   XXWIDGETS_COMBOBOX, XXWIDGETS_CHECKBOX, XXWIDGETS_BUTTON, XXWIDGETS_CHECKBOX,
                                           XXWIDGETS_TABLE,    XXWIDGETS_LABEL,    XXWIDGETS_HEXVIEW,  XXWIDGETS_LABEL,  XXWIDGETS_BUTTON,
                                           XXWIDGETS_EDIT,     XXWIDGETS_BUTTON,   XXWIDGETS_BUTTON,   XXWIDGETS_BUTTON};
    static const char *const texts[] = {"Format", "Memory map", "", "Dump all", "Save",     "Scan", "Mode", "All",  "Options...",   "Deep scan",
                                        "",       "Hex",        "", "",         "Previous", "1",    "Go",   "Next", "Dump selected"};
    if (out) *out = NULL;
    if (!out || !owner || owner->kind != XXWIDGETS_WINDOW || !valid_bounds(bounds) || !source || !source->read || !valid_options(source, &source->options) ||
        (source->type_count && !source->types))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (source->format_mode_masks)
        for (size_t i = 0; i < source->format_count; ++i)
            if (!source->format_mode_masks[i] || (source->format_mode_masks[i] & ~7u)) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < source->type_count; ++i)
        if (!xxwidgets_valid_utf8(source->types[i]) || !source->types[i][0]) return XXWIDGETS_INVALID_ARGUMENT;
    v = calloc(1, sizeof(*v));
    if (!v) return XXWIDGETS_OUT_OF_MEMORY;
    v->owner = owner;
    v->source = source;
    for (size_t i = 0; i < 9; ++i) {
        v->columns[i] = xxwidgets_strdup("");
        if (!v->columns[i]) status = XXWIDGETS_OUT_OF_MEMORY;
    }
    for (size_t i = 0; status == XXWIDGETS_OK && i < XXWIDGETS_EXTRACTOR_CONTROL_COUNT; ++i) {
        if (i == XXWIDGETS_EXTRACTOR_TABLE) status = xxwidgets_table_create(owner, (xxwidgets_rect){0, 0, 1, 1}, titles, 9, &v->controls[i]);
        else status = xxwidgets_widget_create(owner->app, owner, kinds[i], texts[i], (xxwidgets_rect){0, 0, 1, 1}, &v->controls[i]);
    }
    if (status == XXWIDGETS_OK) {
        C(v, TABLE)->table_external_filters = source->scan != NULL;
        status = load_options(v);
    }
    if (status == XXWIDGETS_OK) status = xxwidgets_extractor_set_rect(v, bounds);
    if (status == XXWIDGETS_OK && source->device) status = xxwidgets_hexview_set_device(C(v, HEX), source->device, 0, 1024, 0);
    if (status == XXWIDGETS_OK) status = xxwidgets_hexview_set_editable(C(v, HEX), 0);
    if (status == XXWIDGETS_OK) status = xxwidgets_extractor_set_page(v, 0);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(C(v, SAVE), (source->save || source->save_ex) && xxwidgets_file_dialog_available(owner->app));
    if (status != XXWIDGETS_OK) {
        xxwidgets_extractor_destroy(v);
        return status;
    }
    *out = v;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_extractor_handle_event(xxwidgets_extractor *v, const xxwidgets_event *e)
{
    if (!v || !e) return XXWIDGETS_INVALID_ARGUMENT;
    if ((e->widget == C(v, ALL) || e->widget == C(v, FORMAT_SELECTOR)) &&
        (e->type == XXWIDGETS_EVENT_CLICK || e->type == XXWIDGETS_EVENT_CHANGE || e->type == XXWIDGETS_EVENT_SELECT)) {
        xxwidgets_status status = controls_enabled(v);
        if (status == XXWIDGETS_OK && e->widget == C(v, FORMAT_SELECTOR) && v->source->scan) v->pending = ACTION_SCAN;
        return status;
    }
    if (e->widget == C(v, MAP_SELECTOR) && (e->type == XXWIDGETS_EVENT_CHANGE || e->type == XXWIDGETS_EVENT_SELECT) && v->source->scan) v->pending = ACTION_SCAN;
    else if (e->widget == C(v, TABLE) && e->type == XXWIDGETS_EVENT_CHANGE && v->source->scan) v->pending = ACTION_SCAN;
    else if (e->widget == C(v, TABLE) && (e->type == XXWIDGETS_EVENT_SELECT || e->type == XXWIDGETS_EVENT_ACTIVATE)) v->pending = ACTION_SELECT;
    else if (e->type == XXWIDGETS_EVENT_CLICK) {
        const xxwidgets_extractor_control_id ids[] = {XXWIDGETS_EXTRACTOR_SCAN, XXWIDGETS_EXTRACTOR_PREVIOUS, XXWIDGETS_EXTRACTOR_NEXT, XXWIDGETS_EXTRACTOR_GO,
                                                      XXWIDGETS_EXTRACTOR_SAVE, XXWIDGETS_EXTRACTOR_DUMP_ALL, XXWIDGETS_EXTRACTOR_DUMP, XXWIDGETS_EXTRACTOR_TYPES};
        const int actions[] = {ACTION_SCAN, ACTION_PREVIOUS, ACTION_NEXT, ACTION_GO, ACTION_SAVE, ACTION_DUMP_ALL, ACTION_DUMP, ACTION_TYPES};
        for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
            if (e->widget == v->controls[ids[i]]) v->pending = actions[i];
    }
    return XXWIDGETS_OK;
}

typedef struct types_modal {
    xxwidgets_widget *window, *ok, *cancel, *checks[64];
    int done, accepted;
} types_modal;
static void types_event(xxwidgets_app *app, const xxwidgets_event *e, void *user)
{
    types_modal *m = user;
    (void)app;
    if ((e->type == XXWIDGETS_EVENT_CLOSE && e->widget == m->window) || (e->type == XXWIDGETS_EVENT_SHORTCUT && e->value == 1) ||
        (e->type == XXWIDGETS_EVENT_CLICK && e->widget == m->cancel))
        m->done = 1;
    if (e->type == XXWIDGETS_EVENT_CLICK && e->widget == m->ok) {
        m->done = 1;
        m->accepted = 1;
    }
}
static xxwidgets_status choose_types(xxwidgets_extractor *v)
{
    types_modal m = {0};
    xxwidgets_app *app = v->owner->app;
    xxwidgets_event_fn previous_event = app->on_event;
    void *previous_user = app->user_data;
    xxwidgets_widget *previous_modal = app->modal_window, *previous_default = app->modal_default;
    int enabled = v->owner->enabled, disabled = 0, attached = 0, hidden = app->create_hidden;
    int rows = (int)((v->source->type_count + 3) / 4), height = rows * 2 + 6;
    xxwidgets_status status, cleanup;
    uint64_t mask = 0;
    const xxwidgets_shortcut shortcuts[] = {{"Esc", 1}};
    if (height < 10) height = 10;
    app->on_event = types_event;
    app->user_data = &m;
    app->modal_default = NULL;
    app->create_hidden = 1;
    status =
        xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, "Extractor types", (xxwidgets_rect){v->owner->rect.x + 2, v->owner->rect.y + 2, 104, height}, &m.window);
    app->create_hidden = hidden;
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_window = m.window;
    for (size_t i = 0; status == XXWIDGETS_OK && i < v->source->type_count; ++i) {
        status = xxwidgets_widget_create(app, m.window, XXWIDGETS_CHECKBOX, v->source->types[i],
                                         (xxwidgets_rect){2 + (int)(i / (size_t)rows) * 25, 1 + (int)(i % (size_t)rows) * 2, 24, 1}, &m.checks[i]);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(m.checks[i], (int)((v->staged_types >> i) & 1));
    }
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(app, m.window, XXWIDGETS_BUTTON, "OK", (xxwidgets_rect){78, height - 3, 10, 2}, &m.ok);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(app, m.window, XXWIDGETS_BUTTON, "Cancel", (xxwidgets_rect){90, height - 3, 12, 2}, &m.cancel);
    if (status == XXWIDGETS_OK) status = xxwidgets_window_set_shortcuts(m.window, shortcuts, 1);
    if (status == XXWIDGETS_OK) {
        status = xxwidgets_widget_set_enabled(v->owner, 0);
        if (status == XXWIDGETS_OK) disabled = 1;
    }
    if (status == XXWIDGETS_OK) {
        status = xxwidgets_window_set_modality(m.window, v->owner, XXWIDGETS_WINDOW_MODAL);
        if (status == XXWIDGETS_OK) attached = 1;
    }
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_visible(m.window, 1);
    while (status == XXWIDGETS_OK && !m.done && !app->quit) status = xxwidgets_app_poll(app, 30);
    if (status == XXWIDGETS_OK && m.accepted && !app->quit)
        for (size_t i = 0; status == XXWIDGETS_OK && i < v->source->type_count; ++i) {
            int checked = 0;
            status = xxwidgets_widget_get_value(m.checks[i], &checked);
            if (checked) mask |= UINT64_C(1) << i;
        }
finish:
    if (m.window) {
        cleanup = xxwidgets_widget_set_visible(m.window, 0);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (attached) {
        cleanup = xxwidgets_window_set_modality(m.window, NULL, XXWIDGETS_MODELESS);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (m.window) {
        cleanup = xxwidgets_widget_destroy(m.window);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    app->modal_window = previous_modal;
    app->modal_default = previous_default;
    app->on_event = previous_event;
    app->user_data = previous_user;
    if (disabled) {
        cleanup = xxwidgets_widget_set_enabled(v->owner, enabled);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (status == XXWIDGETS_OK && m.accepted && !app->quit) v->staged_types = mask;
    (void)xxwidgets_widget_focus(C(v, TYPES));
    return status;
}
static xxwidgets_status choose_directory(xxwidgets_widget *owner, char **path, int *accepted)
{
    *path = NULL;
    *accepted = 0;
#ifdef _WIN32
    if (owner->app->backend != XXWIDGETS_BACKEND_TUI && xxwidgets_widget_native_handle(owner)) {
        BROWSEINFOW browse = {0};
        PIDLIST_ABSOLUTE item;
        wchar_t selected[MAX_PATH];
        HRESULT initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        browse.hwndOwner = (HWND)xxwidgets_widget_native_handle(owner);
        browse.lpszTitle = L"Dump all extractor objects";
        browse.ulFlags = BIF_RETURNONLYFSDIRS | (SUCCEEDED(initialized) ? BIF_NEWDIALOGSTYLE : 0);
        item = SHBrowseForFolderW(&browse);
        if (item) {
            if (SHGetPathFromIDListW(item, selected)) {
                int length = WideCharToMultiByte(CP_UTF8, 0, selected, -1, NULL, 0, NULL, NULL);
                *path = length ? malloc((size_t)length) : NULL;
                if (*path) {
                    WideCharToMultiByte(CP_UTF8, 0, selected, -1, *path, length, NULL, NULL);
                    *accepted = 1;
                }
            }
            CoTaskMemFree(item);
            if (SUCCEEDED(initialized)) CoUninitialize();
            return *accepted ? XXWIDGETS_OK : XXWIDGETS_PLATFORM_ERROR;
        }
        if (SUCCEEDED(initialized)) CoUninitialize();
        return XXWIDGETS_OK;
    }
#endif
    {
        char *chosen = NULL, *slash, *forward;
        xxwidgets_status status =
            xxwidgets_file_dialog(owner, XXWIDGETS_FILE_DIALOG_SAVE, "Choose extraction folder using manifest location", "extractor.patch.json", &chosen, accepted);
        if (status != XXWIDGETS_OK || !*accepted) {
            free(chosen);
            return status;
        }
        slash = strrchr(chosen, '\\');
        forward = strrchr(chosen, '/');
        if (forward && (!slash || forward > slash)) slash = forward;
        if (slash) {
            if (slash == chosen || (slash == chosen + 2 && chosen[1] == ':')) slash[1] = 0;
            else *slash = 0;
            *path = chosen;
        } else {
            free(chosen);
            *path = xxwidgets_strdup(".");
        }
        return *path ? XXWIDGETS_OK : XXWIDGETS_OUT_OF_MEMORY;
    }
}
static const char *provider_error(xxwidgets_extractor *v, const char *fallback)
{
    const char *error = v->source->error ? v->source->error(v->source->user) : NULL;
    return error && error[0] && xxwidgets_valid_utf8(error) ? error : fallback;
}
static xxwidgets_status scan_view(xxwidgets_extractor *v)
{
    xxwidgets_extractor_options options = v->source->options;
    char *columns[9] = {0};
    xxwidgets_status status = XXWIDGETS_OK;
    int value = 0, cancelled = 0;
    for (size_t i = 0; i < 9; ++i) {
        columns[i] = xxwidgets_strdup(xxwidgets_table_filter(C(v, TABLE), i));
        if (!columns[i]) status = XXWIDGETS_OUT_OF_MEMORY;
    }
#define GET(id, member, type)                                  \
    if (status == XXWIDGETS_OK) {                              \
        status = xxwidgets_widget_get_value(C(v, id), &value); \
        options.member = (type)value;                          \
    }
    GET(FORMAT_SELECTOR, format, size_t);
    GET(MAP_SELECTOR, map_mode, size_t);
    if (status == XXWIDGETS_OK) {
        status = xxwidgets_widget_get_value(C(v, MODE_SELECTOR), &value);
        if (status == XXWIDGETS_OK && (value < 0 || (size_t)value >= v->offered_mode_count)) status = XXWIDGETS_INVALID_ARGUMENT;
        if (status == XXWIDGETS_OK) options.mode = v->offered_modes[value];
    }
    GET(ALL, all_types, int);
    GET(DEEP, deep_scan, int);
#undef GET
    options.type_mask = v->staged_types;
    if (status == XXWIDGETS_OK) status = xxwidgets_extractor_source_scan(v->owner, v->source, &options, (const char *const *)columns, &cancelled);
    if (status == XXWIDGETS_OK && !cancelled) {
        for (size_t i = 0; i < 9; ++i) {
            free(v->columns[i]);
            v->columns[i] = columns[i];
            columns[i] = NULL;
        }
        status = xxwidgets_extractor_set_page(v, 0);
        if (status == XXWIDGETS_OK) status = xxwidgets_hexview_set_device(C(v, HEX), v->source->device, 0, v->source->device ? 1024 : 0, 0);
        if (status == XXWIDGETS_OK) status = load_options(v);
        if (status == XXWIDGETS_OK) status = status_text(v, "Scan complete");
    } else {
        for (size_t i = 0; i < 9; ++i) (void)xxwidgets_table_set_filter(C(v, TABLE), i, v->columns[i]);
        status = status_text(v, cancelled ? "Scan cancelled; previous results retained" : provider_error(v, "Cannot scan extractable objects"));
    }
    for (size_t i = 0; i < 9; ++i) free(columns[i]);
    return status;
}
static xxwidgets_status select_row(xxwidgets_extractor *v)
{
    int row = -1;
    xxwidgets_status status = xxwidgets_widget_get_value(C(v, TABLE), &row);
    if (status != XXWIDGETS_OK) return status;
    if (row < 0 || (size_t)row >= v->rows) return xxwidgets_widget_set_enabled(C(v, DUMP), 0);
    if (v->source->device) {
        const xxwidgets_extractor_record *r = v->records + row;
        status = xxwidgets_hexview_seek_device(C(v, HEX), r->offset);
        if (status == XXWIDGETS_OK) status = xxwidgets_hexview_set_source_selection(C(v, HEX), r->offset, r->size);
        if (status != XXWIDGETS_OK) return status_text(v, "Cannot read selected object's source bytes");
    }
    return xxwidgets_widget_set_enabled(C(v, DUMP), v->source->dump || v->source->dump_ex);
}
static xxwidgets_status output_view(xxwidgets_extractor *v, int action)
{
    extractor_job job = {0};
    char *path = NULL;
    int accepted = 0, cancelled = 0, row = -1;
    uint64_t index = 0;
    xxwidgets_status status;
    if (action == ACTION_SAVE && !v->source->save && !v->source->save_ex) return XXWIDGETS_OK;
    if (action != ACTION_SAVE && !v->source->dump && !v->source->dump_ex) return XXWIDGETS_OK;
    if (action == ACTION_DUMP) {
        status = xxwidgets_widget_get_value(C(v, TABLE), &row);
        if (status != XXWIDGETS_OK) return status;
        if (row < 0 || (size_t)row >= v->rows) return status_text(v, "Select an object to dump");
        index = v->first + (uint64_t)row;
        job.indices = &index;
        job.index_count = 1;
    }
    if (action == ACTION_DUMP_ALL) status = choose_directory(v->owner, &path, &accepted);
    else
        status = xxwidgets_file_dialog(v->owner, XXWIDGETS_FILE_DIALOG_SAVE, action == ACTION_SAVE ? "Save extractor results" : "Save object dump",
                                       action == ACTION_SAVE ? "extractor.csv" : "object.bin", &path, &accepted);
    if (status == XXWIDGETS_OK && accepted) {
        job.source = v->source;
        job.path = path;
        job.action = action;
        job.directory = action == ACTION_DUMP_ALL;
        status = run_job(v->owner, &job, &cancelled);
    }
    free(path);
    if (status != XXWIDGETS_OK) return status_text(v, provider_error(v, "Cannot write extractor output"));
    if (accepted) return status_text(v, cancelled ? "Output cancelled; completed files may remain" : action == ACTION_SAVE ? "Results saved" : "Objects dumped");
    return status;
}
xxwidgets_status xxwidgets_extractor_process(xxwidgets_extractor *v)
{
    int action;
    uint64_t first;
    xxwidgets_status status;
    if (!v) return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling || v->owner->app->syncing) return XXWIDGETS_BUSY;
    action = v->pending;
    v->pending = 0;
    if (action == ACTION_SCAN) return v->source->scan ? scan_view(v) : XXWIDGETS_OK;
    if (action == ACTION_SELECT) return select_row(v);
    if (action == ACTION_TYPES) return choose_types(v);
    if (action >= ACTION_SAVE && action <= ACTION_DUMP) return output_view(v, action);
    if (action < ACTION_PREVIOUS || action > ACTION_GO) return XXWIDGETS_OK;
    first = v->first;
    if (action == ACTION_PREVIOUS) first = first >= 128 ? first - 128 : 0;
    else if (action == ACTION_NEXT) {
        if (v->source->count - first <= 128) return XXWIDGETS_OK;
        first += 128;
    } else {
        const char *text = C(v, PAGE)->text;
        char *end;
        unsigned long long page;
        uint64_t pages = v->source->count ? (v->source->count - 1) / 128 + 1 : 1;
        errno = 0;
        page = strtoull(text, &end, 10);
        if (!text[0] || text[0] == '-' || text[0] == '+' || *end || errno || !page || page > pages) return status_text(v, "Invalid page number");
        first = ((uint64_t)page - 1) * 128;
    }
    status = xxwidgets_extractor_set_page(v, first);
    return status == XXWIDGETS_OK ? status : status_text(v, provider_error(v, "Cannot read extractor results"));
}

typedef struct extractor_modal {
    xxwidgets_widget *window, *close;
    xxwidgets_extractor *view;
    int done;
    xxwidgets_status failure;
} extractor_modal;
static void extractor_event(xxwidgets_app *app, const xxwidgets_event *e, void *user)
{
    extractor_modal *m = user;
    xxwidgets_status status = XXWIDGETS_OK;
    (void)app;
    if ((e->type == XXWIDGETS_EVENT_CLOSE && e->widget == m->window) || (e->type == XXWIDGETS_EVENT_CLICK && e->widget == m->close) ||
        (e->type == XXWIDGETS_EVENT_SHORTCUT && e->value == 1)) {
        m->done = 1;
        return;
    }
    if (e->type == XXWIDGETS_EVENT_RESIZE && e->widget == m->window && m->view) {
        xxwidgets_rect b;
        status = xxwidgets_widget_get_rect(m->window, &b);
        if (status == XXWIDGETS_OK) status = xxwidgets_extractor_set_rect(m->view, (xxwidgets_rect){1, 1, b.width - 2, b.height - 6});
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_rect(m->close, (xxwidgets_rect){b.width - 13, b.height - 3, 12, 2});
    } else if (m->view) {
        if (e->type == XXWIDGETS_EVENT_SHORTCUT && e->value == 2) m->view->pending = ACTION_SCAN;
        else status = xxwidgets_extractor_handle_event(m->view, e);
    }
    if (status != XXWIDGETS_OK) {
        m->failure = status;
        m->done = 1;
    }
}
xxwidgets_status xxwidgets_extractor_dialog(xxwidgets_widget *owner, const char *title, xxwidgets_extractor_source *source, int auto_scan)
{
    extractor_modal m = {0};
    xxwidgets_app *app;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    xxwidgets_widget *previous_default, *focus = NULL, *w;
    xxwidgets_status status, cleanup;
    int enabled, hidden, disabled = 0, attached = 0;
    const xxwidgets_shortcut shortcuts[] = {{"Esc", 1}, {"F5", 2}};
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !xxwidgets_valid_utf8(title) || !source || (auto_scan != 0 && auto_scan != 1) || owner->rect.x > INT_MAX - 2 ||
        owner->rect.y > INT_MAX - 2)
        return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit) return XXWIDGETS_BUSY;
    previous_default = app->modal_default;
    app->modal_default = NULL;
    previous_event = app->on_event;
    previous_user = app->user_data;
    enabled = owner->enabled;
    for (w = app->widgets; w; w = w->next)
        if (w->parent == owner && xxwidgets_focusable(w)) {
            focus = w;
            break;
        }
    app->on_event = extractor_event;
    app->user_data = &m;
    hidden = app->create_hidden;
    app->create_hidden = 1;
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title, (xxwidgets_rect){owner->rect.x + 2, owner->rect.y + 2, 156, 44}, &m.window);
    app->create_hidden = hidden;
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_window = m.window;
    status = xxwidgets_window_set_minimum_size(m.window, 120, 36);
    if (status == XXWIDGETS_OK) status = xxwidgets_extractor_create(m.window, (xxwidgets_rect){1, 1, 154, 38}, source, &m.view);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(app, m.window, XXWIDGETS_BUTTON, "Close", (xxwidgets_rect){143, 41, 12, 2}, &m.close);
    if (status == XXWIDGETS_OK) status = xxwidgets_window_set_shortcuts(m.window, shortcuts, 2);
    if (status == XXWIDGETS_OK) {
        status = xxwidgets_widget_set_enabled(owner, 0);
        if (status == XXWIDGETS_OK) disabled = 1;
    }
    if (status == XXWIDGETS_OK) {
        status = xxwidgets_window_set_modality(m.window, owner, XXWIDGETS_WINDOW_MODAL);
        if (status == XXWIDGETS_OK) attached = 1;
    }
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_visible(m.window, 1);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_focus(C(m.view, TABLE));
    if (status == XXWIDGETS_OK && auto_scan && source->scan) m.view->pending = ACTION_SCAN;
    while (status == XXWIDGETS_OK && !m.done && !app->quit) {
        status = xxwidgets_app_poll(app, 30);
        if (status == XXWIDGETS_OK && !m.done && !app->quit) status = xxwidgets_extractor_process(m.view);
    }
    if (status == XXWIDGETS_OK) status = m.failure;
finish:
    if (m.window) {
        cleanup = xxwidgets_widget_set_visible(m.window, 0);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (attached) {
        cleanup = xxwidgets_window_set_modality(m.window, NULL, XXWIDGETS_MODELESS);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (m.view) {
        cleanup = xxwidgets_extractor_destroy(m.view);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (m.window) {
        cleanup = xxwidgets_widget_destroy(m.window);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    app->modal_window = NULL;
    app->modal_default = previous_default;
    app->on_event = previous_event;
    app->user_data = previous_user;
    if (disabled) {
        cleanup = xxwidgets_widget_set_enabled(owner, enabled);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (focus) (void)xxwidgets_widget_focus(focus);
    return status;
}
