/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_process.h"
#include "xxfclib/history/xx_history.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/threads/xx_threads.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEX_EDIT_CHUNK ((size_t)65536)
#define HEX_EDIT_HISTORY_LIMIT ((size_t)128)

typedef struct hex_edit_selection {
    uint64_t base, cursor, anchor, anchor_bytes, offset, length;
    unsigned int nibble;
} hex_edit_selection;

/* A command retains only the changed bytes. The much larger suffix journal
 * belongs to one transaction and is discarded after the view accepts it. */
typedef struct hex_edit_command {
    xx_io_device *device, *payload;
    uint64_t offset, deleted, inserted, old_size, new_size;
    hex_edit_selection before, after;
    /* Copied data uses a compact command: typing one nibble must not create a
     * temporary file or copy the whole visible window. Device overwrites use
     * the ordinary payload stream, but journal only their affected range. */
    int overwrite, memory;
    unsigned char inline_bytes[2], *memory_bytes;
    xxwidgets_hex_position memory_before, memory_after;
} hex_edit_command;

typedef struct hex_history_context {
    xxwidgets_widget *widget;
    int interactive, fault;
    xxwidgets_status status;
} hex_history_context;

/* Structural transactions journal the suffix; fixed-size overwrites journal
 * only their changed range. Both use bounded memory with a temporary stream.
 * The copied view stays on the UI thread: storage workers never call a widget
 * or backend API. */
typedef struct hex_edit_job {
    xx_io_device *device, *journal, *insert_source, *expected_source;
    const unsigned char *insert;
    unsigned char *buffer, *verify_buffer;
    uint64_t offset, old_size, new_size, suffix, deleted, inserted;
    uint64_t insert_offset, expected_offset;
    hex_edit_command *record;
    int64_t cursor;
    int buffered, prepared, mutated, rollback_only, overwrite, unchanged;
    xxwidgets_status result;
    xx_mutex_t *mutex;
    int stop, finished;
    uint64_t current, total;
    const char *phase;
} hex_edit_job;

typedef struct hex_edit_view {
    xxwidgets_widget previous;
    unsigned char *data;
    char **rows;
} hex_edit_view;

static void edit_free_rows(char **rows, size_t count)
{
    for (size_t i = 0; i < count; ++i) free(rows[i]);
    free(rows);
}

static void edit_view_dispose(hex_edit_view *view)
{
    free(view->data);
    edit_free_rows(view->rows, view->rows ? view->previous.item_count : 0);
    view->data = NULL;
    view->rows = NULL;
}

static xxwidgets_status edit_view_save(xxwidgets_widget *w, hex_edit_view *view)
{
    memset(view, 0, sizeof(*view));
    view->previous = *w;
    if (w->hex_size) {
        view->data = (unsigned char *)malloc(w->hex_size);
        if (!view->data) return XXWIDGETS_OUT_OF_MEMORY;
        memcpy(view->data, w->hex_data, w->hex_size);
    }
    if (w->item_count) {
        view->rows = (char **)calloc(w->item_count, sizeof(*view->rows));
        if (!view->rows) { edit_view_dispose(view); return XXWIDGETS_OUT_OF_MEMORY; }
        for (size_t i = 0; i < w->item_count; ++i) {
            view->rows[i] = xxwidgets_strdup(w->items[i]);
            if (!view->rows[i]) { edit_view_dispose(view); return XXWIDGETS_OUT_OF_MEMORY; }
        }
    }
    return XXWIDGETS_OK;
}

static void edit_view_restore(xxwidgets_widget *w, hex_edit_view *view)
{
    const xxwidgets_widget *old = &view->previous;
    free(w->hex_data);
    edit_free_rows(w->items, w->item_count);
    w->hex_data = view->data; view->data = NULL;
    w->items = view->rows; view->rows = NULL;
    w->hex_size = old->hex_size; w->item_count = old->item_count;
    w->hex_base = old->hex_base; w->hex_columns = old->hex_columns;
    w->hex_revision = old->hex_revision;
    w->hex_cursor = old->hex_cursor; w->hex_nibble = old->hex_nibble; w->value = old->value;
    w->hex_anchor = old->hex_anchor; w->hex_anchor_bytes = old->hex_anchor_bytes;
    w->hex_selection = old->hex_selection; w->hex_selection_length = old->hex_selection_length;
    w->hex_source_size = old->hex_source_size;
    w->hex_source_anchor = old->hex_source_anchor; w->hex_source_anchor_bytes = old->hex_source_anchor_bytes;
    w->hex_source_selection = old->hex_source_selection; w->hex_source_length = old->hex_source_length;
    w->hex_vertical_active = old->hex_vertical_active;
    w->hex_vertical_row = old->hex_vertical_row; w->hex_vertical_column = old->hex_vertical_column;
    w->hex_source_vertical_row = old->hex_source_vertical_row;
    w->hex_dragging = old->hex_dragging; w->hex_pending_dump = old->hex_pending_dump;
    w->hex_pending_edit = old->hex_pending_edit;
    w->hex_pending_edit_offset = old->hex_pending_edit_offset;
    w->hex_pending_edit_length = old->hex_pending_edit_length;
    w->hex_sync_full = 1; w->hex_sync_dirty_count = 0;
    ++w->app->syncing;
    (void)w->app->ops->sync(w);
    --w->app->syncing;
    w->hex_sync_full = 0;
}

int xxwidgets_hexview_can_resize(const xxwidgets_widget *w)
{
    if (!w || w->kind != XXWIDGETS_HEXVIEW || w->hex_edit_busy || xx_io_memory_only_active()) return 0;
    xx_io_device *device = xxwidgets_hexview_get_device(w);
    if (!device || !device->resize || !xx_io_can_read_write_at(device)) return 0;
    return xx_io_total_size(device) >= 0 && xx_io_tell(device) >= 0;
}

int xxwidgets_hexview_can_overwrite(const xxwidgets_widget *w)
{
    xx_io_device *device;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || w->hex_edit_busy) return 0;
    if (!w->hex_source_read) return w->hex_editable != 0;
    if (xx_io_memory_only_active()) return 0;
    device = xxwidgets_hexview_get_device(w);
    return device && xx_io_can_read_write_at(device) &&
           xx_io_total_size(device) >= 0 && xx_io_tell(device) >= 0;
}

static void edit_progress(hex_edit_job *job, const char *phase, uint64_t current, uint64_t total)
{
    if (job->mutex) xx_mutex_lock(job->mutex);
    job->phase = phase; job->current = current; job->total = total;
    if (job->mutex) xx_mutex_unlock(job->mutex);
}

static int edit_stopped(hex_edit_job *job)
{
    int stop;
    if (job->mutex) xx_mutex_lock(job->mutex);
    stop = job->stop;
    if (job->mutex) xx_mutex_unlock(job->mutex);
    return stop;
}

static int edit_read_exact(xx_io_device *device, uint64_t offset, unsigned char *data, size_t length)
{
    size_t done = 0;
    if (xx_io_seek64(device, (int64_t)offset, SEEK_SET)) return 0;
    while (done < length) {
        ssize_t count = xx_io_read(device, data + done, length - done);
        if (count <= 0 || (size_t)count > length - done) return 0;
        done += (size_t)count;
    }
    return xx_io_tell(device) == (int64_t)(offset + length);
}

static int edit_write_exact(xx_io_device *device, uint64_t offset, const unsigned char *data, size_t length)
{
    size_t done = 0;
    if (xx_io_seek64(device, (int64_t)offset, SEEK_SET)) return 0;
    while (done < length) {
        ssize_t count = xx_io_write(device, data + done, length - done);
        if (count <= 0 || (size_t)count > length - done) return 0;
        done += (size_t)count;
        /* Reject a backend that ignored the requested write position. */
        if (xx_io_tell(device) != (int64_t)(offset + done)) return 0;
    }
    return 1;
}

static int edit_copy(hex_edit_job *job, xx_io_device *source, uint64_t from, xx_io_device *destination,
                     uint64_t to, uint64_t length, const char *phase, int cancellable)
{
    uint64_t done = 0;
    edit_progress(job, phase, 0, length);
    while (done < length) {
        size_t amount = length - done < HEX_EDIT_CHUNK ? (size_t)(length - done) : HEX_EDIT_CHUNK;
        if (cancellable && edit_stopped(job)) return 0;
        if (!edit_read_exact(source, from + done, job->buffer, amount) ||
            !edit_write_exact(destination, to + done, job->buffer, amount)) return 0;
        done += amount;
        edit_progress(job, phase, done, length);
    }
    return 1;
}

/* The journal was read before any writes. Compare it with the command's
 * expected removed range, rejecting stale history without touching storage. */
static int edit_matches(hex_edit_job *job)
{
    uint64_t done = 0;
    edit_progress(job, "Checking bytes for undo/redo", 0, job->deleted);
    while (done < job->deleted) {
        size_t amount = job->deleted - done < HEX_EDIT_CHUNK ? (size_t)(job->deleted - done) : HEX_EDIT_CHUNK;
        if (edit_stopped(job)) return 0;
        if (!edit_read_exact(job->journal, done, job->buffer, amount) ||
            !edit_read_exact(job->expected_source, job->expected_offset + done, job->verify_buffer, amount)) return -1;
        if (memcmp(job->buffer, job->verify_buffer, amount)) return 0;
        done += amount;
        edit_progress(job, "Checking bytes for undo/redo", done, job->deleted);
    }
    return 1;
}

static int edit_restore_cursor(hex_edit_job *job)
{
    int ok = xx_io_seek64(job->device, job->cursor, SEEK_SET) == 0;
    /* A failed preparatory read must not leave the attached cache's sticky
     * error behind after the underlying one-shot failure has cleared. */
    if (job->buffered && job->device->buffer_enabled && xx_io_buffer_failed(job->device))
        if (!xx_io_set_buffer_enabled(job->device, false)) ok = 0;
    if (job->device->buffer_enabled != (job->buffered != 0))
        if (!xx_io_set_buffer_enabled(job->device, job->buffered != 0)) ok = 0;
    return ok;
}

/* Rollback deliberately ignores cancellation. A backend that keeps failing may
 * prevent restoration; callers receive PLATFORM_ERROR and must inspect/reopen
 * their device in that case. The original journal remains alive until return. */
static int edit_rollback(hex_edit_job *job)
{
    int ok = 1;
    edit_progress(job, "Restoring original bytes", 0, job->suffix);
    if (job->mutated) {
        if (!job->prepared || (!job->overwrite && xx_io_resize(job->device, (int64_t)job->old_size))) ok = 0;
        if (ok && !edit_copy(job, job->journal, 0, job->device, job->offset, job->suffix,
                             "Restoring original bytes", 0)) ok = 0;
        if (ok && !job->overwrite && xx_io_resize(job->device, (int64_t)job->old_size)) ok = 0;
        if (ok) job->mutated = 0;
    }
    if (!edit_restore_cursor(job)) ok = 0;
    return ok;
}

static void edit_storage(hex_edit_job *job)
{
    int cancelled = 0;
    xxwidgets_status failure = XXWIDGETS_PLATFORM_ERROR;
    if (job->rollback_only) {
        job->result = edit_rollback(job) ? XXWIDGETS_OK : XXWIDGETS_PLATFORM_ERROR;
        return;
    }
    job->result = XXWIDGETS_PLATFORM_ERROR;
    job->journal = xx_io_temp_open();
    if (!job->journal) goto failed;
    if (!edit_copy(job, job->device, job->offset, job->journal, 0, job->suffix,
                   "Saving original bytes", 1)) goto failed;
    if (job->expected_source) {
        int matches = edit_matches(job);
        if (matches <= 0) {
            if (!matches) failure = XXWIDGETS_UNAVAILABLE;
            goto failed;
        }
    }
    edit_progress(job, "Preparing inserted bytes", 0, job->inserted);
    for (uint64_t done = 0; done < job->inserted;) {
        size_t amount = job->inserted - done < HEX_EDIT_CHUNK ? (size_t)(job->inserted - done) : HEX_EDIT_CHUNK;
        if (edit_stopped(job)) goto failed;
        const unsigned char *data = job->insert ? job->insert + (size_t)done : job->buffer;
        if (job->insert_source && !edit_read_exact(job->insert_source, job->insert_offset + done, job->buffer, amount)) goto failed;
        if (!edit_write_exact(job->journal, job->suffix + done, data, amount)) goto failed;
        done += amount;
        edit_progress(job, "Preparing inserted bytes", done, job->inserted);
    }
    if (job->overwrite) {
        int identical = 1;
        /* Both ranges are staged before mutation. Besides preserving redo for
         * a no-op this avoids publishing a new caret/selection for one. */
        edit_progress(job, "Comparing overwrite bytes", 0, job->deleted);
        for (uint64_t done = 0; done < job->deleted;) {
            size_t amount = job->deleted - done < HEX_EDIT_CHUNK ? (size_t)(job->deleted - done) : HEX_EDIT_CHUNK;
            if (edit_stopped(job) ||
                !edit_read_exact(job->journal, done, job->buffer, amount) ||
                !edit_read_exact(job->journal, job->suffix + done, job->verify_buffer, amount)) goto failed;
            if (memcmp(job->buffer, job->verify_buffer, amount)) identical = 0;
            done += amount;
            edit_progress(job, "Comparing overwrite bytes", done, job->deleted);
        }
        if (identical) {
            if (!edit_restore_cursor(job)) goto failed;
            job->unchanged = 1;
            job->result = XXWIDGETS_OK;
            return;
        }
    }
    if (job->record) {
        job->record->payload = xx_io_temp_open();
        if (!job->record->payload ||
            !edit_copy(job, job->journal, 0, job->record->payload, 0, job->deleted,
                       "Saving deleted bytes for undo", 1) ||
            !edit_copy(job, job->journal, job->suffix, job->record->payload, job->deleted, job->inserted,
                       "Saving inserted bytes for redo", 1)) goto failed;
    }
    /* Detect external size changes during preparation, before the first write.
     * The device remains exclusively borrowed during the actual transaction. */
    if (xx_io_total_size(job->device) != (int64_t)job->old_size) {
        failure = XXWIDGETS_UNAVAILABLE;
        goto failed;
    }
    job->prepared = 1;
    if (edit_stopped(job)) goto failed;
    /* Mark the transaction before calling a user-supplied callback: even a
     * callback reporting failure might already have changed its storage. */
    job->mutated = 1;
    if (job->new_size > job->old_size && xx_io_resize(job->device, (int64_t)job->new_size)) goto failed;
    if (!edit_copy(job, job->journal, job->suffix, job->device, job->offset, job->inserted,
                   job->overwrite ? "Overwriting bytes" : "Inserting bytes", 1)) goto failed;
    if (!job->overwrite && !edit_copy(job, job->journal, job->deleted, job->device, job->offset + job->inserted,
                   job->suffix - job->deleted, "Moving following bytes", 1)) goto failed;
    if (edit_stopped(job)) goto failed;
    if (!job->overwrite && job->new_size <= job->old_size && xx_io_resize(job->device, (int64_t)job->new_size)) goto failed;
    if (xx_io_total_size(job->device) != (int64_t)job->new_size) goto failed;
    if (!edit_restore_cursor(job)) goto failed;
    if (edit_stopped(job)) goto failed;
    job->result = XXWIDGETS_OK;
    return;
failed:
    cancelled = edit_stopped(job);
    if (edit_rollback(job)) job->result = cancelled ? XXWIDGETS_UNAVAILABLE : failure;
    else job->result = XXWIDGETS_PLATFORM_ERROR;
}

static int edit_worker(void *user)
{
    hex_edit_job *job = (hex_edit_job *)user;
    edit_storage(job);
    xx_mutex_lock(job->mutex); job->finished = 1; xx_mutex_unlock(job->mutex);
    return 0;
}

static xxwidgets_status edit_poll(void *user, int stop, xx_pd_struct *progress, int *finished)
{
    hex_edit_job *job = (hex_edit_job *)user;
    xx_mutex_lock(job->mutex);
    if (!job->rollback_only) job->stop |= stop;
    progress->records[0].current = job->current;
    progress->records[0].total = job->total;
    progress->records[0].is_busy = job->finished == 0;
    snprintf(progress->records[0].status, sizeof(progress->records[0].status), "%s",
             job->phase ? job->phase : "Preparing byte edit");
    *finished = job->finished;
    xx_mutex_unlock(job->mutex);
    return XXWIDGETS_OK;
}

static xxwidgets_status edit_run_worker(xxwidgets_widget *owner, hex_edit_job *job)
{
    xxwidgets_status status;
    xx_pd_struct progress = {0};
    xx_thread_t *thread;
    job->finished = 0;
    thread = xx_thread_create(edit_worker, job);
    if (!thread) { job->result = XXWIDGETS_PLATFORM_ERROR; return XXWIDGETS_PLATFORM_ERROR; }
    status = xxwidgets_process_dialog(owner, job->rollback_only ? "Restore original bytes" : "Edit bytes", &progress, edit_poll, job);
    if (status != XXWIDGETS_OK) {
        xx_mutex_lock(job->mutex); job->stop = 1; xx_mutex_unlock(job->mutex);
    }
    if (!xx_thread_join(thread, NULL)) {
        int finished = 0;
        while (!finished) {
            xx_mutex_lock(job->mutex); job->stop = 1; finished = job->finished; xx_mutex_unlock(job->mutex);
            if (!finished) xx_thread_sleep(1);
        }
        xx_thread_detach(thread);
        status = XXWIDGETS_PLATFORM_ERROR;
    }
    return status;
}

static void edit_restore_storage(xxwidgets_widget *owner, hex_edit_job *job, int interactive)
{
    job->rollback_only = 1;
    if (interactive) {
        (void)edit_run_worker(owner, job);
        /* A thread-allocation failure must not leave a committed edit behind
         * after the view rejected it. The exceptional fallback is synchronous. */
        if (!job->finished) edit_storage(job);
    } else edit_storage(job);
}

static xxwidgets_status edit_symbol_range(xxwidgets_widget *w, uint64_t at, uint64_t *offset, uint64_t *length)
{
    unsigned char data[12];
    xxwidgets_widget view = {0};
    xxwidgets_hex_span symbol;
    uint64_t base = at > 4 ? (at - 4) & ~UINT64_C(3) : 0;
    uint64_t after = w->hex_source_size - at;
    size_t size = (size_t)(at - base) + (size_t)(after < 4 ? after : 4);
    if (!xx_io_read_at(xxwidgets_hexview_get_device(w), (int64_t)base, data, size)) return XXWIDGETS_PLATFORM_ERROR;
    view.kind = XXWIDGETS_HEXVIEW; view.hex_data = data; view.hex_size = size;
    view.hex_columns = 16; view.item_count = 1; view.hex_display = w->hex_display;
    if (!xxwidgets_hexview_hit_span(&view, 0, 2, (size_t)(at - base), &symbol)) return XXWIDGETS_PLATFORM_ERROR;
    *offset = base + symbol.offset; *length = symbol.bytes;
    return XXWIDGETS_OK;
}

static xxwidgets_status edit_publish(xxwidgets_widget *w, hex_edit_job *job)
{
    uint64_t at, length, base;
    xxwidgets_status status;
    if (!job->new_size) return xxwidgets_hexview_seek_device(w, 0);
    at = job->offset < job->new_size ? job->offset : job->new_size - 1;
    base = at > 4 ? (at - 4) & ~UINT64_C(3) : 0;
    status = xxwidgets_hexview_seek_device(w, base);
    if (status != XXWIDGETS_OK) return status;
    length = job->inserted ? job->inserted : 1;
    if (!job->inserted && w->hex_header == 2) {
        status = edit_symbol_range(w, at, &at, &length);
        if (status != XXWIDGETS_OK) return status;
    }
    return xxwidgets_hexview_set_source_selection(w, at, length);
}

static void edit_selection_save(const xxwidgets_widget *w, hex_edit_selection *selection)
{
    selection->base = w->hex_base;
    selection->cursor = w->hex_cursor < w->hex_size ? w->hex_base + w->hex_cursor : UINT64_MAX;
    selection->anchor = w->hex_source_anchor;
    selection->anchor_bytes = w->hex_source_anchor_bytes;
    selection->offset = w->hex_source_selection;
    selection->length = w->hex_source_length;
    selection->nibble = w->hex_nibble;
}

static xxwidgets_status edit_selection_restore(xxwidgets_widget *w, const hex_edit_selection *selection)
{
    uint64_t offset = selection->offset, length = selection->length, cursor = selection->cursor;
    xxwidgets_status status;
    unsigned int previous_nibble;
    if (!length) {
        uint64_t base = selection->base < w->hex_source_size ? selection->base : 0;
        status = xxwidgets_hexview_seek_device(w, base);
        if (status != XXWIDGETS_OK) return status;
        return xxwidgets_hexview_set_selection(w, SIZE_MAX, 0);
    }
    if (offset >= w->hex_source_size || length > w->hex_source_size - offset) return XXWIDGETS_OK;
    if (cursor >= w->hex_source_size) cursor = offset;
    /* Encoding and active column can change between an edit and its replay.
     * Keep the current presentation and normalize its restored Symbols range. */
    if (w->hex_header == 2) {
        uint64_t first, first_length, last, last_length, cursor_length;
        status = edit_symbol_range(w, offset, &first, &first_length);
        if (status != XXWIDGETS_OK) return status;
        status = edit_symbol_range(w, offset + length - 1, &last, &last_length);
        if (status != XXWIDGETS_OK) return status;
        status = edit_symbol_range(w, cursor, &cursor, &cursor_length);
        if (status != XXWIDGETS_OK) return status;
        offset = first; length = last + last_length - first;
    }
    status = xxwidgets_hexview_set_source_range(w, cursor, offset, length);
    if (status != XXWIDGETS_OK) return status;
    previous_nibble = w->hex_nibble;
    w->hex_nibble = w->hex_header == 1 ? selection->nibble : 0;
    if (selection->anchor_bytes && selection->anchor < w->hex_source_size &&
        selection->anchor_bytes <= w->hex_source_size - selection->anchor) {
        uint64_t anchor = selection->anchor, bytes = selection->anchor_bytes;
        if (w->hex_header == 2) {
            status = edit_symbol_range(w, anchor, &anchor, &bytes);
            if (status != XXWIDGETS_OK) return status;
        }
        w->hex_source_anchor = anchor; w->hex_source_anchor_bytes = bytes;
        w->hex_anchor = anchor >= w->hex_base && anchor - w->hex_base < w->hex_size
                            ? (size_t)(anchor - w->hex_base) : SIZE_MAX;
        w->hex_anchor_bytes = w->hex_anchor == SIZE_MAX ? 0 : (size_t)bytes;
    }
    if (previous_nibble != w->hex_nibble) {
        ++w->app->syncing;
        status = w->app->ops->sync(w);
        --w->app->syncing;
    }
    return status;
}

static void edit_command_dispose(void *data)
{
    hex_edit_command *command = (hex_edit_command *)data;
    if (!command) return;
    if (command->payload) (void)xx_io_close(command->payload);
    free(command->memory_bytes);
    free(command);
}

static xxwidgets_status edit_history_status(xx_history_status status)
{
    switch (status) {
    case XX_HISTORY_OK: return XXWIDGETS_OK;
    case XX_HISTORY_INVALID_ARGUMENT: return XXWIDGETS_INVALID_ARGUMENT;
    case XX_HISTORY_OUT_OF_MEMORY: return XXWIDGETS_OUT_OF_MEMORY;
    case XX_HISTORY_EMPTY: return XXWIDGETS_UNAVAILABLE;
    case XX_HISTORY_BUSY: return XXWIDGETS_BUSY;
    default: return XXWIDGETS_PLATFORM_ERROR;
    }
}

static int edit_command_undo(void *data, void *context);
static int edit_command_redo(void *data, void *context);

typedef struct hex_edit_replay {
    xx_io_device *source;
    uint64_t insert_offset, expected_offset;
    const hex_edit_selection *selection;
} hex_edit_replay;

static xxwidgets_status edit_bytes(xxwidgets_widget *w, uint64_t offset, const void *data, uint64_t inserted,
                                   uint64_t deleted, int overwrite, int interactive, const hex_edit_replay *replay, int *fault)
{
    hex_edit_job job = {0};
    hex_edit_view view;
    hex_edit_command *command = NULL;
    xx_history_entry *entry = NULL;
    xxwidgets_widget *owner = w;
    xxwidgets_status status, run_status = XXWIDGETS_OK;
    int64_t size;
    int discard_history = 0;
    if (fault) *fault = 0;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || (!data && inserted && (!replay || !replay->source))) return XXWIDGETS_INVALID_ARGUMENT;
    if (w->hex_source_seeking || w->hex_edit_busy || w->app->syncing) return XXWIDGETS_BUSY;
    if (!(overwrite ? xxwidgets_hexview_can_overwrite(w) : xxwidgets_hexview_can_resize(w))) return XXWIDGETS_UNAVAILABLE;
    /* A worker must never bypass the caller thread's mutation policy. */
    if (xx_io_memory_only_active()) return XXWIDGETS_UNAVAILABLE;
    job.device = xxwidgets_hexview_get_device(w);
    size = xx_io_total_size(job.device);
    if (size < 0) return XXWIDGETS_UNAVAILABLE;
    if (offset > (uint64_t)size || deleted > (uint64_t)size - offset ||
        inserted > (uint64_t)INT64_MAX - ((uint64_t)size - deleted)) return XXWIDGETS_INVALID_ARGUMENT;
    /* The journal holds the old suffix/overwrite range followed by new bytes. */
    if (inserted > (uint64_t)INT64_MAX - (overwrite ? deleted : (uint64_t)size - offset)) return XXWIDGETS_INVALID_ARGUMENT;
    if (!inserted && !deleted) return XXWIDGETS_OK;
    if (interactive) {
        while (owner->parent) owner = owner->parent;
        if (owner->kind != XXWIDGETS_WINDOW) return XXWIDGETS_INVALID_ARGUMENT;
        if (w->app->dispatch_depth || w->app->polling || w->app->modal_window || w->app->quit) return XXWIDGETS_BUSY;
    }
    status = edit_view_save(w, &view);
    if (status != XXWIDGETS_OK) return status;
    job.buffer = (unsigned char *)malloc(HEX_EDIT_CHUNK);
    if (!job.buffer) { edit_view_dispose(&view); return XXWIDGETS_OUT_OF_MEMORY; }
    if ((replay && deleted) || overwrite) {
        job.verify_buffer = (unsigned char *)malloc(HEX_EDIT_CHUNK);
        if (!job.verify_buffer) { status = XXWIDGETS_OUT_OF_MEMORY; goto finish; }
    }
    job.cursor = xx_io_tell(job.device);
    job.buffered = job.device->buffer_enabled;
    job.overwrite = overwrite;
    job.old_size = (uint64_t)size; job.offset = offset; job.suffix = overwrite ? deleted : job.old_size - offset;
    job.deleted = deleted; job.inserted = inserted; job.insert = (const unsigned char *)data;
    job.new_size = job.old_size - deleted + inserted;
    if (job.cursor < 0) { status = XXWIDGETS_PLATFORM_ERROR; goto finish; }
    if (replay) {
        job.insert_source = job.expected_source = replay->source;
        job.insert_offset = replay->insert_offset; job.expected_offset = replay->expected_offset;
    } else {
        xx_history_command reversible = {edit_command_undo, edit_command_redo, edit_command_dispose, NULL, NULL};
        if (!w->hex_history) w->hex_history = xx_history_create(HEX_EDIT_HISTORY_LIMIT);
        if (!w->hex_history) { status = XXWIDGETS_OUT_OF_MEMORY; goto finish; }
        command = (hex_edit_command *)calloc(1, sizeof(*command));
        if (!command) { status = XXWIDGETS_OUT_OF_MEMORY; goto finish; }
        command->device = job.device; command->offset = offset;
        command->overwrite = overwrite;
        command->deleted = deleted; command->inserted = inserted;
        command->old_size = job.old_size; command->new_size = job.new_size;
        edit_selection_save(w, &command->before);
        reversible.data = command;
        reversible.label = overwrite ? "Overwrite bytes" : inserted && deleted ? "Replace bytes" : inserted ? "Insert bytes" : "Delete bytes";
        status = edit_history_status(xx_history_prepare(w->hex_history, &reversible, &entry));
        if (status != XXWIDGETS_OK) { edit_command_dispose(command); command = NULL; goto finish; }
        job.record = command;
    }
    w->hex_edit_busy = 1;
    if (interactive) {
        job.mutex = xx_mutex_create();
        if (!job.mutex) { status = XXWIDGETS_PLATFORM_ERROR; goto finish; }
        run_status = edit_run_worker(owner, &job);
    } else edit_storage(&job);
    status = job.result;
    if (run_status != XXWIDGETS_OK || (job.result == XXWIDGETS_OK && edit_stopped(&job))) {
        if (job.mutated) {
            edit_restore_storage(owner, &job, interactive);
        }
        status = run_status != XXWIDGETS_OK || job.result != XXWIDGETS_OK ? XXWIDGETS_PLATFORM_ERROR : XXWIDGETS_UNAVAILABLE;
    } else if (status == XXWIDGETS_OK && !job.unchanged) {
        status = edit_publish(w, &job);
        if (status == XXWIDGETS_OK && replay && replay->selection)
            status = edit_selection_restore(w, replay->selection);
        if (status == XXWIDGETS_OK && command) {
            edit_selection_save(w, &command->after);
            status = edit_history_status(xx_history_commit(w->hex_history, entry));
            if (status == XXWIDGETS_OK) entry = NULL;
        }
        if (status != XXWIDGETS_OK) {
            edit_restore_storage(owner, &job, interactive);
            if (job.result == XXWIDGETS_OK) edit_view_restore(w, &view);
            else (void)xxwidgets_hexview_seek_device(w, 0);
            status = XXWIDGETS_PLATFORM_ERROR;
        }
    }
    /* A persistent storage fault may defeat rollback. Refresh whatever remains
     * readable, rather than leaving a successful-looking old cached page. */
    if (status != XXWIDGETS_OK && job.mutated) {
        discard_history = 1;
        (void)xxwidgets_hexview_seek_device(w, 0);
    }
finish:
    w->hex_edit_busy = 0;
    if (entry) (void)xx_history_abort(w->hex_history, entry);
    if (discard_history) {
        if (fault) *fault = 1;
        else if (w->hex_history) (void)xx_history_clear(w->hex_history);
    }
    if (job.journal) (void)xx_io_close(job.journal);
    if (job.mutex) xx_mutex_destroy(job.mutex);
    free(job.buffer);
    free(job.verify_buffer);
    edit_view_dispose(&view);
    return status;
}

xxwidgets_status xxwidgets_hexview_insert_bytes(xxwidgets_widget *w, uint64_t offset, const void *data, size_t length)
{
    return edit_bytes(w, offset, data, length, 0, 0, 0, NULL, NULL);
}

xxwidgets_status xxwidgets_hexview_delete_bytes(xxwidgets_widget *w, uint64_t offset, uint64_t length)
{
    return edit_bytes(w, offset, NULL, 0, length, 0, 0, NULL, NULL);
}

xxwidgets_status xxwidgets_hexview_edit_interactive(xxwidgets_widget *w, uint64_t offset, const void *data,
                                                  size_t insert_length, uint64_t delete_length)
{
    return edit_bytes(w, offset, data, insert_length, delete_length, 0, 1, NULL, NULL);
}

static void edit_memory_position(const xxwidgets_widget *w, xxwidgets_hex_position *position)
{
    position->cursor = w->hex_cursor;
    position->anchor = w->hex_anchor;
    position->anchor_bytes = w->hex_anchor_bytes;
    position->selection = w->hex_selection;
    position->length = w->hex_selection_length;
    position->nibble = w->hex_nibble;
}

static unsigned char *edit_memory_payload(hex_edit_command *command)
{
    return command->memory_bytes ? command->memory_bytes : command->inline_bytes;
}

/* Stage the complete reversible command before asking the view to mutate.
 * The one-byte path stores both versions inline; larger explicit overwrites
 * allocate exactly two changed ranges, independently of the view/file size. */
static xxwidgets_status edit_memory(xxwidgets_widget *w, size_t offset, const void *data, size_t length,
                                    const xxwidgets_hex_position *after, int advance_noop)
{
    hex_edit_command *command;
    xx_history_entry *entry = NULL;
    xx_history_command reversible = {edit_command_undo, edit_command_redo, edit_command_dispose, NULL, "Overwrite bytes"};
    xxwidgets_status status;
    unsigned char *payload;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || (!data && length)) return XXWIDGETS_INVALID_ARGUMENT;
    if (w->hex_source_seeking || w->hex_edit_busy || w->app->syncing) return XXWIDGETS_BUSY;
    if (w->hex_source_read || !w->hex_editable) return XXWIDGETS_UNAVAILABLE;
    if (offset > w->hex_size || length > w->hex_size - offset || length > SIZE_MAX / 2) return XXWIDGETS_INVALID_ARGUMENT;
    if (!length) return XXWIDGETS_OK;
    if (!memcmp(w->hex_data + offset, data, length)) {
        if (!advance_noop) return XXWIDGETS_OK;
        w->hex_edit_busy = 1;
        status = xxwidgets_hexview_apply_memory_overwrite(w, offset, data, length, after);
        w->hex_edit_busy = 0;
        return status;
    }
    if (!w->hex_history) w->hex_history = xx_history_create(HEX_EDIT_HISTORY_LIMIT);
    if (!w->hex_history) return XXWIDGETS_OUT_OF_MEMORY;
    command = (hex_edit_command *)calloc(1, sizeof(*command));
    if (!command) return XXWIDGETS_OUT_OF_MEMORY;
    command->memory = command->overwrite = 1;
    command->offset = offset; command->deleted = command->inserted = length;
    command->old_size = command->new_size = w->hex_size;
    edit_memory_position(w, &command->memory_before);
    command->memory_after = *after;
    if (length > 1) {
        command->memory_bytes = (unsigned char *)malloc(length * 2);
        if (!command->memory_bytes) { edit_command_dispose(command); return XXWIDGETS_OUT_OF_MEMORY; }
    }
    payload = edit_memory_payload(command);
    memcpy(payload, w->hex_data + offset, length);
    memcpy(payload + length, data, length);
    reversible.data = command;
    status = edit_history_status(xx_history_prepare(w->hex_history, &reversible, &entry));
    if (status != XXWIDGETS_OK) { edit_command_dispose(command); return status; }
    w->hex_edit_busy = 1;
    status = xxwidgets_hexview_apply_memory_overwrite(w, offset, payload + length, length, after);
    if (status == XXWIDGETS_OK) {
        status = edit_history_status(xx_history_commit(w->hex_history, entry));
        if (status == XXWIDGETS_OK) entry = NULL;
        else {
            (void)xxwidgets_hexview_apply_memory_overwrite(w, offset, payload, length, &command->memory_before);
            status = XXWIDGETS_PLATFORM_ERROR;
        }
    }
    w->hex_edit_busy = 0;
    if (entry) (void)xx_history_abort(w->hex_history, entry);
    return status;
}

xxwidgets_status xxwidgets_hexview_overwrite_input(xxwidgets_widget *w, size_t offset, unsigned char byte,
                                                  size_t next_cursor, unsigned int next_nibble)
{
    xxwidgets_hex_position after = {next_cursor, next_cursor, 1, next_cursor, 1, next_nibble};
    return edit_memory(w, offset, &byte, 1, &after, 1);
}

static xxwidgets_status edit_overwrite(xxwidgets_widget *w, uint64_t offset, const void *data, size_t length, int interactive)
{
    if (!w || w->kind != XXWIDGETS_HEXVIEW || (!data && length)) return XXWIDGETS_INVALID_ARGUMENT;
    if (!w->hex_source_read) {
        xxwidgets_hex_position after;
        if (offset < w->hex_base || offset - w->hex_base > w->hex_size ||
            length > w->hex_size - (size_t)(offset - w->hex_base)) return XXWIDGETS_INVALID_ARGUMENT;
        after.cursor = after.anchor = after.selection = (size_t)(offset - w->hex_base);
        after.anchor_bytes = length ? 1 : 0; after.length = length; after.nibble = 0;
        return edit_memory(w, after.cursor, data, length, &after, 0);
    }
    return edit_bytes(w, offset, data, length, length, 1, interactive, NULL, NULL);
}

xxwidgets_status xxwidgets_hexview_overwrite_bytes(xxwidgets_widget *w, uint64_t offset, const void *data, size_t length)
{
    return edit_overwrite(w, offset, data, length, 0);
}

xxwidgets_status xxwidgets_hexview_overwrite_interactive(xxwidgets_widget *w, uint64_t offset, const void *data, size_t length)
{
    return edit_overwrite(w, offset, data, length, 1);
}

static int edit_memory_apply(hex_edit_command *command, hex_history_context *context, int redo)
{
    xxwidgets_widget *w = context->widget;
    size_t length = (size_t)command->deleted;
    const unsigned char *payload = edit_memory_payload(command);
    const unsigned char *expected = payload + (redo ? 0 : length);
    const unsigned char *replacement = payload + (redo ? length : 0);
    const xxwidgets_hex_position *position = redo ? &command->memory_after : &command->memory_before;
    if (w->hex_source_read || w->hex_size != command->old_size || command->offset > w->hex_size ||
        length > w->hex_size - (size_t)command->offset ||
        memcmp(w->hex_data + (size_t)command->offset, expected, length)) {
        context->status = XXWIDGETS_UNAVAILABLE;
        return -1;
    }
    w->hex_edit_busy = 1;
    context->status = xxwidgets_hexview_apply_memory_overwrite(w, (size_t)command->offset, replacement, length, position);
    w->hex_edit_busy = 0;
    return context->status == XXWIDGETS_OK ? 0 : -1;
}

static int edit_command_apply(hex_edit_command *command, hex_history_context *context, int redo)
{
    xxwidgets_widget *w = context->widget;
    hex_edit_replay replay;
    uint64_t expected_size = redo ? command->old_size : command->new_size;
    uint64_t inserted = redo ? command->inserted : command->deleted;
    uint64_t deleted = redo ? command->deleted : command->inserted;
    if (command->memory) return edit_memory_apply(command, context, redo);
    /* Identity plus length protects against a reattached or externally resized
     * device. The transaction also checks every byte that replay will remove. */
    if (xxwidgets_hexview_get_device(w) != command->device || !command->payload ||
        xx_io_total_size(command->device) != (int64_t)expected_size) {
        context->status = XXWIDGETS_UNAVAILABLE;
        return -1;
    }
    replay.source = command->payload;
    replay.insert_offset = redo ? command->deleted : 0;
    replay.expected_offset = redo ? 0 : command->deleted;
    replay.selection = redo ? &command->after : &command->before;
    context->status = edit_bytes(w, command->offset, NULL, inserted, deleted,
                                 command->overwrite, context->interactive, &replay, &context->fault);
    return context->status == XXWIDGETS_OK ? 0 : -1;
}

static int edit_command_undo(void *data, void *context)
{
    return edit_command_apply((hex_edit_command *)data, (hex_history_context *)context, 0);
}

static int edit_command_redo(void *data, void *context)
{
    return edit_command_apply((hex_edit_command *)data, (hex_history_context *)context, 1);
}

int xxwidgets_hexview_can_undo(const xxwidgets_widget *w)
{
    const xx_history_command *entry;
    const hex_edit_command *command;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || w->hex_source_seeking || w->hex_edit_busy || w->app->syncing) return 0;
    entry = xx_history_undo_command(w->hex_history);
    if (!entry || entry->undo != edit_command_undo) return 0;
    command = (const hex_edit_command *)entry->data;
    if (command->memory) return !w->hex_source_read && w->hex_size == command->new_size;
    return xxwidgets_hexview_get_device(w) == command->device &&
           xx_io_total_size(command->device) == (int64_t)command->new_size &&
           (command->overwrite ? xxwidgets_hexview_can_overwrite(w) : xxwidgets_hexview_can_resize(w));
}

int xxwidgets_hexview_can_redo(const xxwidgets_widget *w)
{
    const xx_history_command *entry;
    const hex_edit_command *command;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || w->hex_source_seeking || w->hex_edit_busy || w->app->syncing) return 0;
    entry = xx_history_redo_command(w->hex_history);
    if (!entry || entry->redo != edit_command_redo) return 0;
    command = (const hex_edit_command *)entry->data;
    if (command->memory) return !w->hex_source_read && w->hex_size == command->old_size;
    return xxwidgets_hexview_get_device(w) == command->device &&
           xx_io_total_size(command->device) == (int64_t)command->old_size &&
           (command->overwrite ? xxwidgets_hexview_can_overwrite(w) : xxwidgets_hexview_can_resize(w));
}

static xxwidgets_status edit_history_apply(xxwidgets_widget *w, int redo, int interactive)
{
    hex_history_context context = {w, interactive, 0, XXWIDGETS_UNAVAILABLE};
    xx_history_status result;
    if (!w || w->kind != XXWIDGETS_HEXVIEW) return XXWIDGETS_INVALID_ARGUMENT;
    if (w->hex_source_seeking || w->hex_edit_busy || w->app->syncing) return XXWIDGETS_BUSY;
    if (!(redo ? xxwidgets_hexview_can_redo(w) : xxwidgets_hexview_can_undo(w))) return XXWIDGETS_UNAVAILABLE;
    result = redo ? xx_history_redo(w->hex_history, &context) : xx_history_undo(w->hex_history, &context);
    /* clear is intentionally outside the replay callback: the generic history
     * blocks mutations while callbacks run, including an unrecoverable fault. */
    if (context.fault) (void)xx_history_clear(w->hex_history);
    return result == XX_HISTORY_APPLY_FAILED ? context.status : edit_history_status(result);
}

xxwidgets_status xxwidgets_hexview_undo(xxwidgets_widget *w)
{
    return edit_history_apply(w, 0, 0);
}

xxwidgets_status xxwidgets_hexview_redo(xxwidgets_widget *w)
{
    return edit_history_apply(w, 1, 0);
}

xxwidgets_status xxwidgets_hexview_history_interactive(xxwidgets_widget *w, int redo)
{
    return edit_history_apply(w, redo != 0, 1);
}

xxwidgets_status xxwidgets_hexview_clear_history(xxwidgets_widget *w)
{
    if (!w || w->kind != XXWIDGETS_HEXVIEW) return XXWIDGETS_INVALID_ARGUMENT;
    if (w->hex_source_seeking || w->hex_edit_busy || w->app->syncing) return XXWIDGETS_BUSY;
    return w->hex_history ? edit_history_status(xx_history_clear(w->hex_history)) : XXWIDGETS_OK;
}

void xxwidgets_hexview_history_dispose(xxwidgets_widget *w)
{
    if (!w) return;
    xx_history_destroy(w->hex_history);
    w->hex_history = NULL;
}
