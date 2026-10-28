/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_disk_write.h"
#include "xxfclib/io/xx_io.h"
#include "xxwidgets/xxwidgets_forms.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int process_widget(const xxwidgets_widget *widget)
{
    return widget && widget->kind == XXWIDGET_SYSTEMPROCESSESVIEWER;
}

xxwidgets_status xxwidget_systemprocessesviewer_set_processes(xxwidgets_widget *widget, const xxwidgets_process_info *processes, size_t count)
{
    xxwidgets_process_info *copies = NULL, selected = {0};
    xxwidgets_scan_result *rows = NULL;
    char (*pids)[32] = NULL;
    size_t i, selected_index = SIZE_MAX;
    xxwidgets_status status;
    if (!process_widget(widget) || (!processes && count) || count > INT_MAX || count > SIZE_MAX / sizeof(*copies) || count > SIZE_MAX / sizeof(*rows))
        return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidget_systemprocessesviewer_get_selection(widget, &selected_index, &selected);
    if (status != XXWIDGETS_OK) return status;
    if (count) {
        copies = (xxwidgets_process_info *)calloc(count, sizeof(*copies));
        rows = (xxwidgets_scan_result *)calloc(count, sizeof(*rows));
        pids = (char (*)[32])calloc(count, sizeof(*pids));
        if (!copies || !rows || !pids) { status = XXWIDGETS_OUT_OF_MEMORY; goto done; }
    }
    widget->process_selection = count ? 0 : -1;
    for (i = 0; i < count; ++i) {
        copies[i] = processes[i];
        snprintf(pids[i], sizeof(*pids), "%" PRIu64, processes[i].pid);
        rows[i].type = processes[i].name ? processes[i].name : "";
        rows[i].name = pids[i]; rows[i].version = "";
        rows[i].info = processes[i].path ? processes[i].path : "";
        if (selected_index != SIZE_MAX && selected.pid == processes[i].pid && selected.start_time == processes[i].start_time)
            widget->process_selection = (int)i;
    }
    status = xxwidgets_scanresults_set_table_results(widget, rows, count);
    if (status == XXWIDGETS_OK) {
        for (i = 0; i < count; ++i) {
            copies[i].name = widget->scan_results[i].type;
            copies[i].path = widget->scan_results[i].info;
        }
        free(widget->processes); widget->processes = copies; copies = NULL;
    }
done:
    free(copies); free(rows); free(pids); return status;
}

xxwidgets_status xxwidget_systemprocessesviewer_clear(xxwidgets_widget *widget)
{ return xxwidget_systemprocessesviewer_set_processes(widget, NULL, 0); }

size_t xxwidget_systemprocessesviewer_count(const xxwidgets_widget *widget)
{ return process_widget(widget) ? widget->item_count : 0; }

xxwidgets_status xxwidget_systemprocessesviewer_get_process(const xxwidgets_widget *widget, size_t index, xxwidgets_process_info *process)
{
    if (!process_widget(widget) || !process || index >= widget->item_count || !widget->processes) return XXWIDGETS_INVALID_ARGUMENT;
    *process = widget->processes[index]; return XXWIDGETS_OK;
}

xxwidgets_status xxwidget_systemprocessesviewer_get_selection(xxwidgets_widget *widget, size_t *index, xxwidgets_process_info *process)
{
    int value; xxwidgets_status status;
    if (!process_widget(widget) || !index || !process) return XXWIDGETS_INVALID_ARGUMENT;
    *index = SIZE_MAX; memset(process, 0, sizeof(*process));
    status = xxwidgets_widget_get_value(widget, &value);
    if (status != XXWIDGETS_OK) return status;
    if (value >= 0 && (size_t)value < widget->item_count && widget->processes) { *index = (size_t)value; *process = widget->processes[value]; }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidget_systemprocessesviewer_refresh(xxwidgets_widget *widget)
{
    xxwidgets_process_info *processes = NULL; size_t count = 0;
    xxwidgets_status status;
    if (!process_widget(widget)) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidget_systemprocessesviewer_enumerate(&processes, &count);
    if (status == XXWIDGETS_OK) status = xxwidget_systemprocessesviewer_set_processes(widget, processes, count);
    xxwidget_systemprocessesviewer_free_processes(processes, count); return status;
}

typedef struct process_actions {
    xxwidgets_widget *window, *buttons[8];
    size_t count;
    int done, choice;
} process_actions;

static void actions_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    process_actions *state = (process_actions *)user; size_t i; (void)app;
    if (event->widget == state->window && event->type == XXWIDGETS_EVENT_CLOSE) state->done = 1;
    if (event->type == XXWIDGETS_EVENT_CLICK) for (i = 0; i < state->count; ++i)
        if (event->widget == state->buttons[i]) { state->choice = (int)i; state->done = 1; }
}

static xxwidgets_status action_dialog(xxwidgets_widget *viewer, const char *title, const char *message, const char *const *labels,
    size_t count, unsigned disabled, int *choice)
{
    xxwidgets_app *app = viewer->app; xxwidgets_widget *owner = viewer->parent, *label = NULL;
    xxwidgets_event_fn previous_event = app->on_event; void *previous_user = app->user_data;
    int owner_enabled = owner->enabled, disabled_owner = 0, attached = 0;
    process_actions state = {0}; xxwidgets_status status = XXWIDGETS_OK, cleanup;
    xxwidgets_rect rect = {owner->rect.x + 2, owner->rect.y + 2, 62, (int)count * 2 + 4}; size_t i;
    if (!choice || !count || count > sizeof(state.buttons) / sizeof(state.buttons[0])) return XXWIDGETS_INVALID_ARGUMENT;
    if (app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit) return XXWIDGETS_BUSY;
    state.count = count; state.choice = -1; *choice = -1;
    app->on_event = actions_event; app->user_data = &state;
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title, rect, &state.window);
    if (status != XXWIDGETS_OK) goto done;
    app->modal_window = state.window;
    rect.x = 2; rect.y = 1; rect.width = 58; rect.height = 1;
    status = xxwidgets_widget_create(app, state.window, XXWIDGETS_LABEL, message, rect, &label);
    if (status != XXWIDGETS_OK) goto done;
    for (i = 0; i < count; ++i) {
        rect.y = 3 + (int)i * 2;
        status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON, labels[i], rect, &state.buttons[i]);
        if (status != XXWIDGETS_OK) goto done;
        if (disabled & (1u << i)) { status = xxwidgets_widget_set_enabled(state.buttons[i], 0); if (status != XXWIDGETS_OK) goto done; }
    }
    status = xxwidgets_widget_set_enabled(owner, 0);
    if (status != XXWIDGETS_OK) goto done;
    disabled_owner = 1;
    status = xxwidgets_window_set_modality(state.window, owner, XXWIDGETS_WINDOW_MODAL); if (status != XXWIDGETS_OK) goto done; attached = 1;
    status = xxwidgets_widget_focus(state.buttons[count - 1]);
    while (status == XXWIDGETS_OK && !state.done && !app->quit) status = xxwidgets_app_poll(app, 30);
done:
    if (attached) { cleanup = xxwidgets_window_set_modality(state.window, NULL, XXWIDGETS_MODELESS); if (status == XXWIDGETS_OK) status = cleanup; }
    if (disabled_owner) {
        cleanup = xxwidgets_widget_set_enabled(owner, owner_enabled);
        if (cleanup != XXWIDGETS_OK) {
            owner->enabled = owner_enabled;
            ++app->syncing; (void)app->ops->sync(owner); --app->syncing;
        }
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (state.window) { cleanup = xxwidgets_widget_destroy(state.window); if (status == XXWIDGETS_OK) status = cleanup; }
    app->modal_window = NULL; app->on_event = previous_event; app->user_data = previous_user;
    if (owner_enabled && !app->quit) (void)xxwidgets_widget_focus(viewer);
    if (status == XXWIDGETS_OK && !app->quit) *choice = state.choice;
    return status;
}

typedef struct process_dump_job {
    xxwidgets_process_info process;
    const char *path;
    xxwidgets_process_dump_type type;
    int overwrite;
} process_dump_job;

static xxwidgets_status process_dump_worker(void *user, xx_pd_struct *progress, xxwidgets_disk_write_reporter *reporter)
{
    process_dump_job *job = user;
    xxwidgets_status status;
    (void)xxwidgets_disk_write_set_file(reporter, job->path, 0, 1, 1);
    (void)xxwidgets_disk_write_bind_levels(reporter, -1, 0);
    status = xxwidget_systemprocessesviewer_dump_process_ex(&job->process, job->path, job->type, job->overwrite, progress);
    if (status == XXWIDGETS_OK) {
        xx_io_device *file = xx_io_file_open(job->path, "rb");
        if (file) {
            int64_t size = xx_io_total_size(file);
            if (size >= 0) (void)xxwidgets_disk_write_set_bytes(reporter, (uint64_t)size, (uint64_t)size, 1);
            (void)xx_io_close(file);
        }
        (void)xxwidgets_disk_write_set_file(reporter, job->path, 1, 1, 1);
    }
    return status;
}

static xxwidgets_status dump_action(xxwidgets_widget *widget, const xxwidgets_process_info *process, xxwidgets_process_dump_type type)
{
    xxwidgets_widget *owner = widget->parent;
    char name[96], title[128], *path = NULL, *message = NULL;
    const char *extension = type == XXWIDGETS_PROCESS_DUMP_IMAGE ? "bin" :
#ifdef _WIN32
        "dmp";
#else
        "core";
#endif
    const char *description = type == XXWIDGETS_PROCESS_DUMP_IMAGE ? "executable image" : "process memory";
    xxwidgets_status status, notice;
    process_dump_job job = {0};
    xxwidgets_disk_write_result result = {0};
    int accepted = 0, overwrite = 0;
    snprintf(name, sizeof(name), "process-%" PRIu64 ".%s", process->pid, extension);
    snprintf(title, sizeof(title), "Dump %s - PID %" PRIu64, description, process->pid);
    if (xxwidgets_file_dialog_available(widget->app)) {
        status = xxwidgets_file_dialog(owner, XXWIDGETS_FILE_DIALOG_SAVE, title, name, &path, &accepted);
        overwrite = accepted; /* The native chooser confirms replacement. */
    } else {
        const size_t capacity = 65536;
        xxwidgets_form_field fields[2] = {0};
        path = (char *)calloc(capacity, 1);
        if (!path) return XXWIDGETS_OUT_OF_MEMORY;
        memcpy(path, name, strlen(name) + 1);
        fields[0].label = "Output file";
        fields[0].value = path;
        fields[0].capacity = capacity;
        fields[1].label = "Replace existing file";
        fields[1].checkbox = 1;
        do {
            status = xxwidgets_form_dialog(owner, path[0] ? title : "Enter an output file path", fields, 2, &accepted);
        } while (status == XXWIDGETS_OK && accepted && !path[0]);
        overwrite = fields[1].checked;
    }
    if (status != XXWIDGETS_OK || !accepted) {
        free(path);
        if (owner->enabled && !widget->app->quit) (void)xxwidgets_widget_focus(widget);
        return status;
    }
    job.process.pid = process->pid;
    job.process.start_time = process->start_time;
    job.path = path; job.type = type; job.overwrite = overwrite;
    status = xxwidgets_disk_write_run(owner, title, path, process_dump_worker, &job, &result);
    if (result.cancelled) {
        free(path);
        if (owner->enabled && !widget->app->quit) (void)xxwidgets_widget_focus(widget);
        return status;
    }
    if (status == XXWIDGETS_OK) {
        size_t length = strlen(path);
        if (length > SIZE_MAX - 128) notice = XXWIDGETS_OUT_OF_MEMORY;
        else {
            message = (char *)malloc(length + 128);
            if (!message) notice = XXWIDGETS_OUT_OF_MEMORY;
            else {
                snprintf(message, length + 128, "Saved %s to:\n%s", description, path);
                notice = xxwidgets_text_dialog(owner, XXW_TR((owner)->app, "Dump saved"), message);
            }
        }
    } else {
        char failure[640];
        const char *hint = type == XXWIDGETS_PROCESS_DUMP_IMAGE ?
            "The main executable must have a readable memory image." :
#ifdef _WIN32
            "Process memory dumps require a compatible process architecture.";
#elif defined(__APPLE__)
            "Process memory dumps require /usr/bin/gcore and permission to inspect the target.";
#else
            "Process memory dumps require GNU gcore and permission to inspect the target.";
#endif
        snprintf(failure, sizeof(failure),
            "Could not dump %s: %s.\n\n"
            "The process must still exist and allow memory access. Check the output folder "
            "and replacement choice. %s The existing output file is preserved on failure.",
            description, status == XXWIDGETS_UNAVAILABLE ? "process access or output unavailable" :
                status == XXWIDGETS_BUSY ? "output already exists or process mappings changed" : xxwidgets_status_string(status), hint);
        notice = xxwidgets_text_dialog(owner, XXW_TR((owner)->app, "Dump failed"), failure);
    }
    free(message);
    free(path);
    if (owner->enabled && !widget->app->quit) (void)xxwidgets_widget_focus(widget);
    return status == XXWIDGETS_OK ? notice : status;
}

xxwidgets_status xxwidget_systemprocessesviewer_show_actions(xxwidgets_widget *widget)
{
    static const char *const actions[] = {"Open executable location", "Copy PID", "Refresh", "Kill process...",
        "Dump process memory...", "Dump executable image...", "Hex view...", "Cancel"};
    static const char *const confirm[] = {"Terminate process", "Cancel"};
    xxwidgets_process_info process; size_t index; int choice = -1; unsigned disabled = 0;
    xxwidgets_status status; char title[96], pid[32];
    if (!process_widget(widget)) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidget_systemprocessesviewer_get_selection(widget, &index, &process);
    if (status != XXWIDGETS_OK) return status;
    if (index == SIZE_MAX) disabled = (1u << 0) | (1u << 1) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 6);
    else {
        if (!process.path || !*process.path) disabled |= 1u << 0;
        if (!process.start_time || process.pid <= 1) disabled |= (1u << 3) | (1u << 4) | (1u << 5) | (1u << 6);
    }
    if (!widget->app->ops->copy_text || widget->app->backend == XXWIDGETS_BACKEND_TUI) disabled |= 1u << 1;
    snprintf(title, sizeof(title), "Process actions - PID %" PRIu64, process.pid);
    status = action_dialog(widget, title, index != SIZE_MAX ? widget->scan_cells[index * 4] : "No process selected", actions,
        sizeof(actions) / sizeof(actions[0]), disabled, &choice);
    if (status != XXWIDGETS_OK || choice < 0 || (disabled & (1u << choice))) return status;
    if (choice == 0) return xxwidget_systemprocessesviewer_open_process_path(&process);
    if (choice == 1) { snprintf(pid, sizeof(pid), "%" PRIu64, process.pid); return widget->app->ops->copy_text(widget->parent, pid); }
    if (choice == 2) return xxwidget_systemprocessesviewer_refresh(widget);
    if (choice == 6) return xxwidget_systemprocessesviewer_show_hexview(widget);
    if (choice == 4 || choice == 5) return dump_action(widget, &process,
        choice == 4 ? XXWIDGETS_PROCESS_DUMP_MEMORY : XXWIDGETS_PROCESS_DUMP_IMAGE);
    if (choice == 3) {
        snprintf(title, sizeof(title), "Terminate PID %" PRIu64 "?", process.pid);
        status = action_dialog(widget, title, "Unsaved work in this process may be lost.", confirm, 2, 0, &choice);
        if (status == XXWIDGETS_OK && choice == 0) {
            status = xxwidget_systemprocessesviewer_kill_process(&process, 0);
            if (status == XXWIDGETS_OK) status = xxwidget_systemprocessesviewer_refresh(widget);
        }
    }
    return status;
}
