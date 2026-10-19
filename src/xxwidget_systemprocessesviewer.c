/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxwidgets_internal.h"
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
    xxwidgets_widget *window, *buttons[5];
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
    if (app->ops->modal_owner) { status = app->ops->modal_owner(state.window, owner, 1); if (status != XXWIDGETS_OK) goto done; attached = 1; }
    status = xxwidgets_widget_focus(state.buttons[count - 1]);
    while (status == XXWIDGETS_OK && !state.done && !app->quit) status = xxwidgets_app_poll(app, 30);
done:
    if (attached) { cleanup = app->ops->modal_owner(state.window, owner, 0); if (status == XXWIDGETS_OK) status = cleanup; }
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

xxwidgets_status xxwidget_systemprocessesviewer_show_actions(xxwidgets_widget *widget)
{
    static const char *const actions[] = {"Open executable location", "Copy PID", "Refresh", "Kill process...", "Cancel"};
    static const char *const confirm[] = {"Terminate process", "Cancel"};
    xxwidgets_process_info process; size_t index; int choice = -1; unsigned disabled = 0;
    xxwidgets_status status; char title[96], pid[32];
    if (!process_widget(widget)) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidget_systemprocessesviewer_get_selection(widget, &index, &process);
    if (status != XXWIDGETS_OK) return status;
    if (index == SIZE_MAX) disabled = (1u << 0) | (1u << 1) | (1u << 3);
    else {
        if (!process.path || !*process.path) disabled |= 1u << 0;
        if (!process.start_time || process.pid <= 1) disabled |= 1u << 3;
    }
    if (!widget->app->ops->copy_text || widget->app->backend == XXWIDGETS_BACKEND_TUI) disabled |= 1u << 1;
    snprintf(title, sizeof(title), "Process actions - PID %" PRIu64, process.pid);
    status = action_dialog(widget, title, index != SIZE_MAX ? widget->scan_cells[index * 4] : "No process selected", actions, 5, disabled, &choice);
    if (status != XXWIDGETS_OK || choice < 0 || (disabled & (1u << choice))) return status;
    if (choice == 0) return xxwidget_systemprocessesviewer_open_process_path(&process);
    if (choice == 1) { snprintf(pid, sizeof(pid), "%" PRIu64, process.pid); return widget->app->ops->copy_text(widget->parent, pid); }
    if (choice == 2) return xxwidget_systemprocessesviewer_refresh(widget);
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
