/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "../xxwidgets_internal.h"
#include "xxwidgets/debugger/xxwidgets_debugger_project_dialog.h"
#include "xxwidgets/debugger/xxwidgets_debugger_arguments.h"
#include "xxwidgets/xxwidgets_combobox.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct modal_context {
    xxwidgets_widget *owner, *window, *focus, *previous_default;
    xxwidgets_app *app;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    int owner_enabled, disabled, attached, active;
} modal_context;

static xxwidgets_status modal_begin(modal_context *modal, xxwidgets_widget *owner,
    const char *title, xxwidgets_event_fn handler, void *user)
{
    xxwidgets_widget *control;
    xxwidgets_app *app;
    int width;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || owner->rect.x > INT_MAX - 86 || owner->rect.y > INT_MAX - 22)
        return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit) return XXWIDGETS_BUSY;
    modal->owner = owner; modal->app = app;
    modal->previous_event = app->on_event; modal->previous_user = app->user_data;
    modal->previous_default = app->modal_default; modal->owner_enabled = owner->enabled;
    for (control = app->widgets; control; control = control->next)
        if (control->parent == owner && xxwidgets_focusable(control) &&
            (!modal->focus || xxwidgets_widget_has_focus(control) == 1)) modal->focus = control;
    app->on_event = handler; app->user_data = user;
    modal->active = 1;
    width = owner->rect.width < 86 ? owner->rect.width : 86;
    if (width < 66) width = 66;
    xxwidgets_status status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title,
        (xxwidgets_rect){owner->rect.x + (owner->rect.width > width ? (owner->rect.width - width) / 2 : 0),
            owner->rect.y + (owner->rect.height > 22 ? (owner->rect.height - 22) / 2 : 0), width, 22}, &modal->window);
    if (status == XXWIDGETS_OK) {
        app->modal_window = modal->window;
        status = xxwidgets_window_set_minimum_size(modal->window, 66, 22);
    }
    return status;
}

static xxwidgets_status modal_show(modal_context *modal, xxwidgets_widget *focus, xxwidgets_widget *default_button)
{
    xxwidgets_status status = xxwidgets_widget_set_enabled(modal->owner, 0);
    if (status != XXWIDGETS_OK) return status;
    modal->disabled = 1;
    status = xxwidgets_window_set_modality(modal->window, modal->owner, XXWIDGETS_WINDOW_MODAL);
    if (status != XXWIDGETS_OK) return status;
    modal->attached = 1;
    modal->app->modal_default = default_button;
    return xxwidgets_widget_focus(focus);
}

static xxwidgets_status modal_finish(modal_context *modal, xxwidgets_status status)
{
    xxwidgets_status cleanup;
    if (!modal->active) return status;
    if (modal->disabled) {
        cleanup = xxwidgets_widget_set_enabled(modal->owner, modal->owner_enabled);
        if (cleanup != XXWIDGETS_OK) {
            modal->owner->enabled = modal->owner_enabled;
            ++modal->app->syncing;
            (void)modal->app->ops->sync(modal->owner);
            --modal->app->syncing;
        }
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (modal->attached) {
        cleanup = xxwidgets_window_set_modality(modal->window, NULL, XXWIDGETS_MODELESS);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (modal->window) {
        cleanup = xxwidgets_widget_destroy(modal->window);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    modal->app->modal_window = NULL; modal->app->modal_default = modal->previous_default;
    modal->app->on_event = modal->previous_event; modal->app->user_data = modal->previous_user;
    if (modal->focus && !modal->app->quit) (void)xxwidgets_widget_focus(modal->focus);
    return status;
}

static xxwidgets_status control(modal_context *modal, xxwidgets_widget **out, xxwidgets_kind kind, const char *text)
{
    return xxwidgets_widget_create(modal->app, modal->window, kind, text, (xxwidgets_rect){1, 1, 20, 1}, out);
}

static xxwidgets_status place(xxwidgets_widget *widget, int x, int y, int width, int height)
{
    return xxwidgets_widget_set_rect(widget, (xxwidgets_rect){x, y, width, height});
}

static xxwidgets_status choices(xxwidgets_widget *widget, const wchar_t *const *names, size_t count)
{
    xx_meta_string records[7] = {0};
    xx_str_w_s labels[7] = {0};
    if (count > 7) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i) {
        labels[i].data = (wchar_t *)names[i]; labels[i].length = wcslen(names[i]);
        labels[i].capacity = labels[i].length + 1; labels[i].is_view = true;
        records[i].meta_string = &labels[i]; records[i].var.type = XX_VAR_TYPE_UINT64;
        records[i].var.val.u64 = i;
    }
    return xxwidgets_combobox_set_records(widget, records, count);
}

typedef struct project_state {
    modal_context modal;
    xxwidgets_widget *program_label, *program, *browse, *arguments_label, *arguments;
    xxwidgets_widget *directory_label, *directory, *backend_label, *backend, *format_label, *format;
    xxwidgets_widget *breakpoint_label, *breakpoint, *hint, *error, *start, *cancel;
    xxwidgets_debugger_project_options options;
    xxwidgets_debugger_project_info info;
    char probed_program[4096];
    int backend_values[2], backend_count, preferred_emulate;
    int done, pending, accepted, program_dirty, backend_dirty, probed;
    xxwidgets_status layout_status;
} project_state;

enum { PROJECT_ACCEPT = 1, PROJECT_BROWSE, PROJECT_BACKEND, DIALOG_CANCEL };

static xxwidgets_status project_layout(project_state *state)
{
    if (!state->cancel) return XXWIDGETS_OK;
    xxwidgets_rect bounds;
    xxwidgets_status status = xxwidgets_widget_get_rect(state->modal.window, &bounds);
    if (status != XXWIDGETS_OK) return status;
    int w = bounds.width, bottom = bounds.height - 2;
#define PLACE(member, x, y, width, height) do { status = place(state->member, x, y, width, height); if (status != XXWIDGETS_OK) return status; } while (0)
    PLACE(program_label, 2, 0, w - 4, 1);
    PLACE(program, 2, 1, w - 18, 1); PLACE(browse, w - 14, 1, 12, 1);
    PLACE(arguments_label, 2, 3, w - 4, 1); PLACE(arguments, 2, 4, w - 4, 1);
    PLACE(directory_label, 2, 6, w - 4, 1); PLACE(directory, 2, 7, w - 4, 1);
    PLACE(backend_label, 2, 9, (w - 6) / 2, 1); PLACE(backend, 2, 10, (w - 6) / 2, 1);
    PLACE(format_label, w / 2 + 1, 9, w / 2 - 3, 1); PLACE(format, w / 2 + 1, 10, w / 2 - 3, 1);
    PLACE(breakpoint_label, 2, 12, w - 4, 1); PLACE(breakpoint, 2, 13, w - 4, 1);
    PLACE(hint, 2, 15, w - 4, 2); PLACE(error, 2, bottom - 2, w - 4, 1);
    PLACE(start, w - 36, bottom, 20, 1); PLACE(cancel, w - 14, bottom, 12, 1);
#undef PLACE
    return XXWIDGETS_OK;
}

static void project_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    project_state *state = user;
    (void)app;
    if (event->widget == state->modal.window && event->type == XXWIDGETS_EVENT_CLOSE) state->done = 1;
    else if (event->widget == state->modal.window && event->type == XXWIDGETS_EVENT_RESIZE)
        state->layout_status = project_layout(state);
    else if (event->type == XXWIDGETS_EVENT_SHORTCUT) {
        if (event->value == DIALOG_CANCEL) state->done = 1;
        else state->pending = event->value;
    } else if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == state->cancel) state->done = 1;
        else if (event->widget == state->start) state->pending = PROJECT_ACCEPT;
        else if (event->widget == state->browse) state->pending = PROJECT_BROWSE;
    } else if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == state->backend)
        state->backend_dirty = 1;
    else if (event->type == XXWIDGETS_EVENT_CHANGE && event->widget == state->program)
        state->program_dirty = 1;
}

static int project_mode(project_state *state)
{
    int index = -1;
    if (xxwidgets_widget_get_value(state->backend, &index) == XXWIDGETS_OK &&
        index >= 0 && index < state->backend_count) return state->backend_values[index];
    return state->preferred_emulate;
}

static xxwidgets_status project_apply_mode(project_state *state)
{
    int emulate = project_mode(state);
    int detected = state->options.probe && state->info.format != XXWIDGETS_DEBUGGER_FORMAT_AUTO;
    xxwidgets_status status = XXWIDGETS_OK;
    state->preferred_emulate = emulate;
    if (!emulate || detected)
        status = xxwidgets_widget_set_value(state->format,
            emulate ? state->info.format : XXWIDGETS_DEBUGGER_FORMAT_AUTO);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_set_enabled(state->format, state->backend_count && emulate && !detected);
    return status;
}

static int valid_project_info(const xxwidgets_debugger_project_info *info)
{
    return !(info->backends & ~(XXWIDGETS_DEBUGGER_PROJECT_NATIVE | XXWIDGETS_DEBUGGER_PROJECT_EMULATOR)) &&
        info->format >= XXWIDGETS_DEBUGGER_FORMAT_AUTO && info->format <= XXWIDGETS_DEBUGGER_FORMAT_ELF64 &&
        wmemchr(info->native_label, 0, 64) && wmemchr(info->emulator_label, 0, 64) &&
        memchr(info->target, 0, sizeof(info->target)) && memchr(info->message, 0, sizeof(info->message)) &&
        xxwidgets_valid_utf8(info->target) && xxwidgets_valid_utf8(info->message);
}

static xxwidgets_status project_refresh(project_state *state, int *changed)
{
    char program[4096], hint[384];
    const wchar_t *labels[2];
    int selected = 0, old_mode = project_mode(state), old_format = 0, new_format = 0, reset_format;
    xxwidgets_debugger_project_info info = {0};
    xxwidgets_status status;
    *changed = 0;
    if (!state->options.probe) return XXWIDGETS_OK;
    status = xxwidgets_widget_get_text(state->program, program, sizeof(program), NULL);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(state->format, &old_format);
    if (status == XXWIDGETS_OK) status = state->options.probe(program, &info, state->options.user_data);
    if (status != XXWIDGETS_OK) return status;
    if (!valid_project_info(&info)) return XXWIDGETS_INVALID_ARGUMENT;
    reset_format = info.format == XXWIDGETS_DEBUGGER_FORMAT_AUTO && state->probed &&
        (strcmp(program, state->probed_program) || state->info.format != info.format);
    strcpy(state->probed_program, program);
    state->probed = 1;
    state->info = info;
    state->backend_count = 0;
    if (info.backends & XXWIDGETS_DEBUGGER_PROJECT_NATIVE) {
        labels[state->backend_count] = info.native_label[0] ? info.native_label : L"Real debugger";
        state->backend_values[state->backend_count++] = 0;
    }
    if (info.backends & XXWIDGETS_DEBUGGER_PROJECT_EMULATOR) {
        labels[state->backend_count] = info.emulator_label[0] ? info.emulator_label : L"Emulator";
        state->backend_values[state->backend_count++] = 1;
    }
    for (int i = 0; i < state->backend_count; ++i)
        if (state->backend_values[i] == old_mode) selected = i;
    status = choices(state->backend, labels, (size_t)state->backend_count);
    if (status == XXWIDGETS_OK && state->backend_count)
        status = xxwidgets_widget_set_value(state->backend, selected);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(state->backend, state->backend_count != 0);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(state->start, state->backend_count != 0);
    if (status == XXWIDGETS_OK && reset_format)
        status = xxwidgets_widget_set_value(state->format, XXWIDGETS_DEBUGGER_FORMAT_AUTO);
    if (status == XXWIDGETS_OK) status = project_apply_mode(state);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(state->format, &new_format);
    if (status != XXWIDGETS_OK) return status;
    *changed = old_mode != project_mode(state) || old_format != new_format;
    snprintf(hint, sizeof(hint), "%s%s%s", info.target, info.target[0] && info.message[0] ? "\n" : "", info.message);
    status = xxwidgets_widget_set_text(state->hint, hint);
    return status;
}

static int valid_settings(const xxwidgets_debugger_project_settings *settings)
{
    return settings && memchr(settings->program, 0, sizeof(settings->program)) &&
        memchr(settings->arguments, 0, sizeof(settings->arguments)) &&
        memchr(settings->working_directory, 0, sizeof(settings->working_directory)) &&
        xxwidgets_valid_utf8(settings->program) && xxwidgets_valid_utf8(settings->arguments) &&
        xxwidgets_valid_utf8(settings->working_directory) && settings->breakpoint >= XXWIDGETS_DEBUGGER_BREAK_ENTRY_POINT &&
        settings->breakpoint <= XXWIDGETS_DEBUGGER_BREAK_PROCESS_CREATION && (settings->emulate == 0 || settings->emulate == 1) &&
        settings->format >= XXWIDGETS_DEBUGGER_FORMAT_AUTO && settings->format <= XXWIDGETS_DEBUGGER_FORMAT_ELF64;
}

static xxwidgets_status read_settings(project_state *state, xxwidgets_debugger_project_settings *selected,
    char *error, size_t error_capacity)
{
    int breakpoint, emulate, format, argc = 0;
    char **argv = NULL;
    xxwidgets_status status = xxwidgets_widget_get_text(state->program, selected->program, sizeof(selected->program), NULL);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_text(state->arguments, selected->arguments, sizeof(selected->arguments), NULL);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_text(state->directory, selected->working_directory, sizeof(selected->working_directory), NULL);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(state->breakpoint, &breakpoint);
    emulate = project_mode(state);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(state->format, &format);
    if (status != XXWIDGETS_OK) { snprintf(error, error_capacity, "A path or argument list is too long."); return status; }
    selected->breakpoint = (xxwidgets_debugger_initial_breakpoint)breakpoint;
    selected->emulate = emulate; selected->format = (xxwidgets_debugger_project_format)format;
    if (!selected->program[0]) {
        snprintf(error, error_capacity, "Choose a program to debug.");
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    if (!state->backend_count) {
        snprintf(error, error_capacity, "%s", state->info.message[0] ? state->info.message : "No compatible debugger is available for this program.");
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    if (!valid_settings(selected) || (!emulate && format != XXWIDGETS_DEBUGGER_FORMAT_AUTO)) {
        snprintf(error, error_capacity, "Choose a valid backend, format, and debug breakpoint.");
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    status = xxwidgets_debugger_arguments_parse(selected->program, selected->arguments, &argc, &argv, error, error_capacity);
    xxwidgets_debugger_arguments_free(argc, argv);
    return status;
}

xxwidgets_status xxwidgets_debugger_project_dialog_ex(xxwidgets_widget *owner,
    xxwidgets_debugger_project_settings *settings, const xxwidgets_debugger_project_options *options, int *accepted)
{
    static const wchar_t *const breakpoints[] = {L"Program Entry Point", L"None", L"Process creation"};
    static const wchar_t *const backends[] = {L"Native debugger", L"Emulator"};
    static const wchar_t *const formats[] = {L"auto", L"com", L"mz", L"pe32", L"pe64", L"elf32", L"elf64"};
    static const xxwidgets_shortcut shortcuts[] = {{"Escape", DIALOG_CANCEL}, {"Ctrl+O", PROJECT_BROWSE}};
    project_state state = {0};
    xxwidgets_debugger_project_settings selected = {0};
    xxwidgets_status status;
    if (accepted) *accepted = 0;
    if (!accepted || !valid_settings(settings)) return XXWIDGETS_INVALID_ARGUMENT;
    if (options) state.options = *options;
    state.preferred_emulate = settings->emulate;
    state.backend_count = 2; state.backend_values[0] = 0; state.backend_values[1] = 1;
    status = modal_begin(&state.modal, owner, "New Debug Project", project_event, &state);
    if (status != XXWIDGETS_OK) goto done;
#define CONTROL(member, kind, text) do { status = control(&state.modal, &state.member, kind, text); if (status != XXWIDGETS_OK) goto done; } while (0)
    CONTROL(program_label, XXWIDGETS_LABEL, "Program path"); CONTROL(program, XXWIDGETS_EDIT, settings->program);
    CONTROL(browse, XXWIDGETS_BUTTON, "Browse...");
    CONTROL(arguments_label, XXWIDGETS_LABEL, "Arguments"); CONTROL(arguments, XXWIDGETS_EDIT, settings->arguments);
    CONTROL(directory_label, XXWIDGETS_LABEL, "Working directory (empty: current debugger directory)");
    CONTROL(directory, XXWIDGETS_EDIT, settings->working_directory);
    CONTROL(backend_label, XXWIDGETS_LABEL, "Debugger backend"); CONTROL(backend, XXWIDGETS_COMBOBOX, "");
    CONTROL(format_label, XXWIDGETS_LABEL, "Program format"); CONTROL(format, XXWIDGETS_COMBOBOX, "");
    CONTROL(breakpoint_label, XXWIDGETS_LABEL, "Debug breakpoint"); CONTROL(breakpoint, XXWIDGETS_COMBOBOX, "");
    CONTROL(hint, XXWIDGETS_LABEL, "Use quotes around arguments with spaces; no shell expansion.\nEntry Point: stop at entry. None: run immediately.");
    CONTROL(error, XXWIDGETS_LABEL, ""); CONTROL(start, XXWIDGETS_BUTTON, "Start debugging"); CONTROL(cancel, XXWIDGETS_BUTTON, "Cancel");
#undef CONTROL
    status = choices(state.breakpoint, breakpoints, 3);
    if (status == XXWIDGETS_OK) status = choices(state.backend, backends, 2);
    if (status == XXWIDGETS_OK) status = choices(state.format, formats, 7);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(state.breakpoint, settings->breakpoint);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(state.backend, settings->emulate);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(state.format, settings->emulate ? settings->format : XXWIDGETS_DEBUGGER_FORMAT_AUTO);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(state.format, settings->emulate);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(state.browse, xxwidgets_file_dialog_available(state.modal.app));
    if (status == XXWIDGETS_OK) status = xxwidgets_window_set_shortcuts(state.modal.window, shortcuts, sizeof(shortcuts) / sizeof(shortcuts[0]));
    if (status == XXWIDGETS_OK) status = project_layout(&state);
    if (status == XXWIDGETS_OK) {
        int changed;
        status = project_refresh(&state, &changed);
    }
    if (status == XXWIDGETS_OK) status = modal_show(&state.modal, state.arguments, state.start);
    while (status == XXWIDGETS_OK && !state.done && !state.modal.app->quit) {
        status = xxwidgets_app_poll(state.modal.app, 30);
        if (status == XXWIDGETS_OK) status = state.layout_status;
        if (status != XXWIDGETS_OK || state.done || state.modal.app->quit) break;
        int pending = state.pending, changed = 0; state.pending = 0;
        if (state.program_dirty) {
            state.program_dirty = 0;
            status = project_refresh(&state, &changed);
            if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(state.error, "");
        }
        if (status == XXWIDGETS_OK && state.backend_dirty) {
            state.backend_dirty = 0;
            status = project_apply_mode(&state);
        }
        if (status != XXWIDGETS_OK) break;
        if (pending == PROJECT_BROWSE && xxwidgets_file_dialog_available(state.modal.app)) {
            char initial[4096], *path = NULL;
            int picked = 0;
            status = xxwidgets_widget_get_text(state.program, initial, sizeof(initial), NULL);
            if (status == XXWIDGETS_OK) status = xxwidgets_file_dialog(state.modal.window, XXWIDGETS_FILE_DIALOG_OPEN,
                "Open File", initial, &path, &picked);
            if (status == XXWIDGETS_OK && picked) {
                status = xxwidgets_widget_set_text(state.program, path);
                if (status == XXWIDGETS_OK) status = project_refresh(&state, &changed);
                state.program_dirty = 0;
            }
            free(path);
        } else if (pending == PROJECT_ACCEPT) {
            char message[256] = "";
            int checked_change = 0;
            status = project_refresh(&state, &checked_change);
            if (status == XXWIDGETS_OK && (changed || checked_change)) {
                status = xxwidgets_widget_set_text(state.error, "Program compatibility changed. Review the settings and press Start again.");
                continue;
            }
            if (status == XXWIDGETS_OK) status = read_settings(&state, &selected, message, sizeof(message));
            if (status == XXWIDGETS_INVALID_ARGUMENT || status == XXWIDGETS_BUFFER_TOO_SMALL)
                status = xxwidgets_widget_set_text(state.error, message[0] ? message : "Check the project settings.");
            else if (status == XXWIDGETS_OK) state.accepted = state.done = 1;
        }
    }
    if (state.modal.app->quit) state.accepted = 0;
done:
    status = modal_finish(&state.modal, status);
    if (status == XXWIDGETS_OK && state.accepted) { *settings = selected; *accepted = 1; }
    return status;
}

xxwidgets_status xxwidgets_debugger_project_dialog(xxwidgets_widget *owner,
    xxwidgets_debugger_project_settings *settings, int *accepted)
{
    return xxwidgets_debugger_project_dialog_ex(owner, settings, NULL, accepted);
}

typedef struct attach_state {
    modal_context modal;
    xxwidgets_widget *processes, *pid_label, *pid, *refresh, *path, *error, *attach, *cancel;
    int done, pending, accepted;
    xxwidgets_status layout_status;
} attach_state;

enum { ATTACH_ACCEPT = 10, ATTACH_REFRESH, ATTACH_SELECT, ATTACH_ACTIVATE };

static xxwidgets_status attach_layout(attach_state *state)
{
    if (!state->cancel) return XXWIDGETS_OK;
    xxwidgets_rect bounds;
    xxwidgets_status status = xxwidgets_widget_get_rect(state->modal.window, &bounds);
    if (status != XXWIDGETS_OK) return status;
    int w = bounds.width, h = bounds.height;
#define PLACE(member, x, y, width, height) do { status = place(state->member, x, y, width, height); if (status != XXWIDGETS_OK) return status; } while (0)
    PLACE(processes, 2, 1, w - 4, h - 10);
    PLACE(pid_label, 2, h - 8, 6, 1); PLACE(pid, 8, h - 8, w - 28, 1); PLACE(refresh, w - 18, h - 8, 16, 1);
    PLACE(path, 2, h - 6, w - 4, 2); PLACE(error, 2, h - 4, w - 4, 1);
    PLACE(attach, w - 28, h - 2, 12, 1); PLACE(cancel, w - 14, h - 2, 12, 1);
#undef PLACE
    return XXWIDGETS_OK;
}

static void attach_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    attach_state *state = user;
    (void)app;
    if (event->widget == state->modal.window && event->type == XXWIDGETS_EVENT_CLOSE) state->done = 1;
    else if (event->widget == state->modal.window && event->type == XXWIDGETS_EVENT_RESIZE)
        state->layout_status = attach_layout(state);
    else if (event->type == XXWIDGETS_EVENT_SHORTCUT) {
        if (event->value == DIALOG_CANCEL) state->done = 1;
        else state->pending = event->value;
    } else if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == state->cancel) state->done = 1;
        else if (event->widget == state->attach) state->pending = ATTACH_ACCEPT;
        else if (event->widget == state->refresh) state->pending = ATTACH_REFRESH;
    } else if (event->widget == state->processes) {
        if (event->type == XXWIDGETS_EVENT_SELECT) state->pending = ATTACH_SELECT;
        else if (event->type == XXWIDGETS_EVENT_ACTIVATE) state->pending = ATTACH_ACTIVATE;
    }
}

static xxwidgets_status attach_selection(attach_state *state)
{
    xxwidgets_process_info process;
    size_t index;
    xxwidgets_status status = xxwidget_systemprocessesviewer_get_selection(state->processes, &index, &process);
    if (status != XXWIDGETS_OK || index == SIZE_MAX) return status;
    char text[32];
    snprintf(text, sizeof(text), "%" PRIu64, process.pid);
    status = xxwidgets_widget_set_text(state->pid, text);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(state->path, process.path && *process.path ? process.path : process.name);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(state->error, "");
    return status;
}

static int decimal_pid(const char *text, uint64_t *pid)
{
    uint64_t value = 0;
    if (!*text) return 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return 0;
        unsigned digit = (unsigned)(*text - '0');
        if (value > (UINT64_MAX - digit) / 10) return 0;
        value = value * 10 + digit;
    }
    if (!value) return 0;
    *pid = value;
    return 1;
}

/* The process viewer normally selects its first row after population. Keep
 * its highlight consistent with the explicit PID instead of suggesting an
 * unrelated process when no selection has been made yet. */
static xxwidgets_status attach_match_pid(attach_state *state)
{
    char text[128];
    uint64_t pid = 0;
    int selected = -1;
    xxwidgets_process_info process = {0};
    xxwidgets_status status = xxwidgets_widget_get_text(state->pid, text, sizeof(text), NULL);
    if (status != XXWIDGETS_OK && status != XXWIDGETS_BUFFER_TOO_SMALL) return status;
    if (status == XXWIDGETS_OK && decimal_pid(text, &pid)) {
        size_t count = xxwidget_systemprocessesviewer_count(state->processes);
        for (size_t i = 0; i < count; ++i) {
            status = xxwidget_systemprocessesviewer_get_process(state->processes, i, &process);
            if (status != XXWIDGETS_OK) return status;
            if (process.pid == pid) { selected = (int)i; break; }
        }
    }
    status = xxwidgets_widget_set_value(state->processes, selected);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(state->path,
        selected < 0 ? "Select a running process or enter its PID." : process.path && *process.path ? process.path : process.name);
    return status;
}

xxwidgets_status xxwidgets_debugger_attach_dialog(xxwidgets_widget *owner, uint64_t *pid, int *accepted)
{
    static const xxwidgets_shortcut shortcuts[] = {{"Escape", DIALOG_CANCEL}, {"F5", ATTACH_REFRESH}};
    attach_state state = {0};
    xxwidgets_status status;
    uint64_t selected = 0;
    char text[128] = "";
    if (accepted) *accepted = 0;
    if (!accepted || !pid) return XXWIDGETS_INVALID_ARGUMENT;
    if (*pid) snprintf(text, sizeof(text), "%" PRIu64, *pid);
    status = modal_begin(&state.modal, owner, "Attach to Process", attach_event, &state);
    if (status != XXWIDGETS_OK) goto done;
#define CONTROL(member, kind, value) do { status = control(&state.modal, &state.member, kind, value); if (status != XXWIDGETS_OK) goto done; } while (0)
    CONTROL(processes, XXWIDGET_SYSTEMPROCESSESVIEWER, "Running processes");
    CONTROL(pid_label, XXWIDGETS_LABEL, "PID"); CONTROL(pid, XXWIDGETS_EDIT, text);
    CONTROL(refresh, XXWIDGETS_BUTTON, "Refresh / F5");
    CONTROL(path, XXWIDGETS_LABEL, "Select a running process or enter its PID.");
    CONTROL(error, XXWIDGETS_LABEL, ""); CONTROL(attach, XXWIDGETS_BUTTON, "Attach"); CONTROL(cancel, XXWIDGETS_BUTTON, "Cancel");
#undef CONTROL
    status = xxwidgets_window_set_shortcuts(state.modal.window, shortcuts, sizeof(shortcuts) / sizeof(shortcuts[0]));
    if (status == XXWIDGETS_OK) status = attach_layout(&state);
    if (status == XXWIDGETS_OK) {
        xxwidgets_status refresh = xxwidget_systemprocessesviewer_refresh(state.processes);
        if (refresh != XXWIDGETS_OK) status = xxwidgets_widget_set_text(state.error, "Cannot list processes. Enter a PID manually, or try Refresh.");
        else status = attach_match_pid(&state);
    }
    if (status == XXWIDGETS_OK) status = modal_show(&state.modal, *pid ? state.pid : state.processes, state.attach);
    while (status == XXWIDGETS_OK && !state.done && !state.modal.app->quit) {
        status = xxwidgets_app_poll(state.modal.app, 30);
        if (status == XXWIDGETS_OK) status = state.layout_status;
        if (status != XXWIDGETS_OK || state.done || state.modal.app->quit) break;
        int pending = state.pending; state.pending = 0;
        if (pending == ATTACH_REFRESH) {
            xxwidgets_status refresh = xxwidget_systemprocessesviewer_refresh(state.processes);
            status = xxwidgets_widget_set_text(state.error, refresh == XXWIDGETS_OK ? "Process list refreshed." : "Cannot refresh processes. Enter a PID manually.");
            if (status == XXWIDGETS_OK && refresh == XXWIDGETS_OK) status = attach_match_pid(&state);
        } else if (pending == ATTACH_SELECT || pending == ATTACH_ACTIVATE) {
            status = attach_selection(&state);
            if (status == XXWIDGETS_OK && pending == ATTACH_ACTIVATE) pending = ATTACH_ACCEPT;
        }
        if (status == XXWIDGETS_OK && pending == ATTACH_ACCEPT) {
            status = xxwidgets_widget_get_text(state.pid, text, sizeof(text), NULL);
            if (status == XXWIDGETS_BUFFER_TOO_SMALL || (status == XXWIDGETS_OK && !decimal_pid(text, &selected)))
                status = xxwidgets_widget_set_text(state.error, "Select a process or enter a positive decimal PID.");
            else if (status == XXWIDGETS_OK) state.accepted = state.done = 1;
        }
    }
    if (state.modal.app->quit) state.accepted = 0;
done:
    status = modal_finish(&state.modal, status);
    if (status == XXWIDGETS_OK && state.accepted) { *pid = selected; *accepted = 1; }
    return status;
}
