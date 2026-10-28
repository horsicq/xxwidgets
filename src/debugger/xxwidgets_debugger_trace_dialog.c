/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "../xxwidgets_internal.h"
#include "xxwidgets/debugger/xxwidgets_debugger_trace_dialog.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    if (width < 67) width = 67;
    xxwidgets_status status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title,
        (xxwidgets_rect){owner->rect.x + (owner->rect.width > width ? (owner->rect.width - width) / 2 : 0),
            owner->rect.y + (owner->rect.height > 22 ? (owner->rect.height - 22) / 2 : 0), width, 22}, &modal->window);
    if (status == XXWIDGETS_OK) {
        app->modal_window = modal->window;
        status = xxwidgets_window_set_minimum_size(modal->window, 67, 22);
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

typedef struct trace_state {
    modal_context modal;
    xxwidgets_widget *count_label, *count, *limit_hint, *write_file;
    xxwidgets_widget *path_label, *path, *browse, *format_label, *format;
    xxwidgets_widget *examples, *hint, *error, *start, *cancel;
    int done, pending, accepted, output_dirty;
    xxwidgets_status layout_status;
} trace_state;

enum { TRACE_ACCEPT = 1, TRACE_BROWSE, TRACE_CANCEL };

static xxwidgets_status trace_layout(trace_state *state)
{
    if (!state->cancel) return XXWIDGETS_OK;
    xxwidgets_rect bounds;
    xxwidgets_status status = xxwidgets_widget_get_rect(state->modal.window, &bounds);
    if (status != XXWIDGETS_OK) return status;
    int w = bounds.width, bottom = bounds.height - 2;
#define PLACE(member, x, y, width, height) do { status = place(state->member, x, y, width, height); if (status != XXWIDGETS_OK) return status; } while (0)
    PLACE(count_label, 2, 0, w - 4, 1); PLACE(count, 2, 1, 28, 1);
    PLACE(limit_hint, 2, 2, w - 4, 1); PLACE(write_file, 2, 4, w - 4, 1);
    PLACE(path_label, 2, 6, w - 4, 1); PLACE(path, 2, 7, w - 18, 1); PLACE(browse, w - 14, 7, 12, 1);
    PLACE(format_label, 2, 9, w - 4, 1); PLACE(format, 2, 10, w - 4, 1);
    PLACE(examples, 2, 12, w - 4, 2); PLACE(hint, 2, 15, w - 4, 2);
    PLACE(error, 2, bottom - 2, w - 4, 1);
    PLACE(start, w - 36, bottom, 20, 1); PLACE(cancel, w - 14, bottom, 12, 1);
#undef PLACE
    return XXWIDGETS_OK;
}

static void trace_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    trace_state *state = user;
    (void)app;
    if (event->widget == state->modal.window && event->type == XXWIDGETS_EVENT_CLOSE) state->done = 1;
    else if (event->widget == state->modal.window && event->type == XXWIDGETS_EVENT_RESIZE)
        state->layout_status = trace_layout(state);
    else if (event->type == XXWIDGETS_EVENT_SHORTCUT) {
        if (event->value == TRACE_CANCEL) state->done = 1;
        else state->pending = event->value;
    } else if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == state->cancel) state->done = 1;
        else if (event->widget == state->start) state->pending = TRACE_ACCEPT;
        else if (event->widget == state->browse) state->pending = TRACE_BROWSE;
        else if (event->widget == state->write_file) state->output_dirty = 1;
    } else if (event->type == XXWIDGETS_EVENT_CHANGE && event->widget == state->write_file)
        state->output_dirty = 1;
}

static xxwidgets_status trace_apply_output(trace_state *state)
{
    int enabled = 0;
    xxwidgets_status status = xxwidgets_widget_get_value(state->write_file, &enabled);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(state->path, enabled);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(state->browse,
        enabled && xxwidgets_file_dialog_available(state->modal.app));
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(state->format, enabled);
    return status;
}

static int valid_settings(const xxwidgets_debugger_trace_settings *settings)
{
    return settings && (settings->write_file == 0 || settings->write_file == 1) &&
        memchr(settings->file_path, 0, sizeof(settings->file_path)) &&
        memchr(settings->format, 0, sizeof(settings->format)) &&
        xxwidgets_valid_utf8(settings->file_path) && xxwidgets_valid_utf8(settings->format);
}

static int decimal_count(const char *text, uint64_t *count)
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
    *count = value;
    return 1;
}

static int valid_format(const char *format)
{
    if (!*format || strpbrk(format, "\r\n")) return 0;
    while (*format) {
        if (*format++ == '%') {
            const char *end = strchr(format, '%');
            if (!end) return 0;
            format = end + 1;
        }
    }
    return 1;
}

static xxwidgets_status read_settings(trace_state *state, xxwidgets_debugger_trace_settings *selected,
    char *error, size_t error_capacity)
{
    char count[32];
    xxwidgets_status status = xxwidgets_widget_get_text(state->count, count, sizeof(count), NULL);
    if (status == XXWIDGETS_BUFFER_TOO_SMALL || (status == XXWIDGETS_OK && !decimal_count(count, &selected->instruction_limit))) {
        snprintf(error, error_capacity, "Enter a count from 1 to 18446744073709551615.");
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    if (status != XXWIDGETS_OK) return status;
    status = xxwidgets_widget_get_text(state->path, selected->file_path, sizeof(selected->file_path), NULL);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_text(state->format, selected->format, sizeof(selected->format), NULL);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(state->write_file, &selected->write_file);
    if (status != XXWIDGETS_OK) {
        snprintf(error, error_capacity, "The file path or record format is too long.");
        return status;
    }
    if (!valid_settings(selected)) {
        snprintf(error, error_capacity, "Use valid UTF-8 text for the file path and record format.");
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    if (selected->write_file && !selected->file_path[0]) {
        snprintf(error, error_capacity, "Choose an output file path.");
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    if (selected->write_file && !valid_format(selected->format)) {
        snprintf(error, error_capacity, "Use one nonempty line; close each %%NAME%% (%%%% for percent).");
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_debugger_trace_dialog(xxwidgets_widget *owner,
    xxwidgets_debugger_trace_settings *settings, int *accepted)
{
    static const xxwidgets_shortcut shortcuts[] = {{"Escape", TRACE_CANCEL}, {"Ctrl+O", TRACE_BROWSE}};
    trace_state state = {0};
    xxwidgets_debugger_trace_settings selected = {0};
    char count[32];
    xxwidgets_status status;
    if (accepted) *accepted = 0;
    if (!accepted || !valid_settings(settings)) return XXWIDGETS_INVALID_ARGUMENT;
    snprintf(count, sizeof(count), "%" PRIu64, settings->instruction_limit);
    status = modal_begin(&state.modal, owner, "Trace", trace_event, &state);
    if (status != XXWIDGETS_OK) goto done;
#define CONTROL(member, kind, text) do { status = control(&state.modal, &state.member, kind, text); if (status != XXWIDGETS_OK) goto done; } while (0)
    CONTROL(count_label, XXWIDGETS_LABEL, "Instructions to execute"); CONTROL(count, XXWIDGETS_EDIT, count);
    CONTROL(limit_hint, XXWIDGETS_LABEL, "Tracing stops early if the program exits.");
    CONTROL(write_file, XXWIDGETS_CHECKBOX, "Write to file");
    CONTROL(path_label, XXWIDGETS_LABEL, "Output file"); CONTROL(path, XXWIDGETS_EDIT, settings->file_path);
    CONTROL(browse, XXWIDGETS_BUTTON, "Browse...");
    CONTROL(format_label, XXWIDGETS_LABEL, "Record format"); CONTROL(format, XXWIDGETS_EDIT, settings->format);
    CONTROL(examples, XXWIDGETS_LABEL, "Examples: Address: 0x%PC%\n          EIP=%EIP%: EAX=%EAX%");
    CONTROL(hint, XXWIDGETS_LABEL, "Registers are recorded before each instruction.\nExisting files are not replaced.");
    CONTROL(error, XXWIDGETS_LABEL, ""); CONTROL(start, XXWIDGETS_BUTTON, "Start tracing"); CONTROL(cancel, XXWIDGETS_BUTTON, "Cancel");
#undef CONTROL
    status = xxwidgets_widget_set_value(state.write_file, settings->write_file);
    if (status == XXWIDGETS_OK) status = trace_apply_output(&state);
    if (status == XXWIDGETS_OK) status = xxwidgets_window_set_shortcuts(state.modal.window, shortcuts, sizeof(shortcuts) / sizeof(shortcuts[0]));
    if (status == XXWIDGETS_OK) status = trace_layout(&state);
    if (status == XXWIDGETS_OK) status = modal_show(&state.modal, state.count, state.start);
    while (status == XXWIDGETS_OK && !state.done && !state.modal.app->quit) {
        status = xxwidgets_app_poll(state.modal.app, 30);
        if (status == XXWIDGETS_OK) status = state.layout_status;
        if (status != XXWIDGETS_OK || state.done || state.modal.app->quit) break;
        int pending = state.pending; state.pending = 0;
        if (state.output_dirty) {
            state.output_dirty = 0;
            status = trace_apply_output(&state);
            if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(state.error, "");
        }
        if (status != XXWIDGETS_OK) break;
        if (pending == TRACE_BROWSE && xxwidgets_file_dialog_available(state.modal.app)) {
            char initial[4096], *path = NULL;
            int picked = 0, enabled = 0;
            status = xxwidgets_widget_get_value(state.write_file, &enabled);
            if (status == XXWIDGETS_OK && enabled) {
                status = xxwidgets_widget_get_text(state.path, initial, sizeof(initial), NULL);
                if (status == XXWIDGETS_BUFFER_TOO_SMALL) {
                    status = xxwidgets_widget_set_text(state.error, "The output file path is too long.");
                    continue;
                }
                if (status == XXWIDGETS_OK) status = xxwidgets_file_dialog(state.modal.window, XXWIDGETS_FILE_DIALOG_SAVE,
                    "Trace Output File", initial, &path, &picked);
                if (status == XXWIDGETS_OK && picked) {
                    if (strlen(path) >= sizeof(selected.file_path))
                        status = xxwidgets_widget_set_text(state.error, "The output file path is too long.");
                    else {
                        status = xxwidgets_widget_set_text(state.path, path);
                        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(state.error, "");
                    }
                }
                free(path);
            }
        } else if (pending == TRACE_ACCEPT) {
            char message[192] = "";
            status = read_settings(&state, &selected, message, sizeof(message));
            if (status == XXWIDGETS_INVALID_ARGUMENT || status == XXWIDGETS_BUFFER_TOO_SMALL)
                status = xxwidgets_widget_set_text(state.error, message[0] ? message : "Check the trace settings.");
            else if (status == XXWIDGETS_OK) state.accepted = state.done = 1;
        }
    }
    if (state.modal.app->quit) state.accepted = 0;
done:
    status = modal_finish(&state.modal, status);
    if (status == XXWIDGETS_OK && state.accepted) { *settings = selected; *accepted = 1; }
    return status;
}
