/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_forms.h"
#include "platform/xxwidget_systemprocessesviewer_memory.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct process_hex_dialog {
    xxwidgets_widget *window, *hex, *address, *go, *refresh, *close, *status, *hint;
    int done, pending, resized, selected, read_error;
} process_hex_dialog;

static xxwidgets_status process_hex_dump(xxwidgets_widget *hex, void *user)
{
    process_hex_dialog *state = (process_hex_dialog *)user;
    (void)hex;
    state->pending = 3;
    return XXWIDGETS_OK;
}

static void process_hex_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    process_hex_dialog *state = (process_hex_dialog *)user;
    (void)app;
    if (event->widget == state->window) {
        if (event->type == XXWIDGETS_EVENT_CLOSE) state->done = 1;
        else if (event->type == XXWIDGETS_EVENT_RESIZE) state->resized = 1;
        else if (event->type == XXWIDGETS_EVENT_SHORTCUT) {
            if (event->value == 1) state->pending = 2;
            else if (event->value == 2) state->done = 1;
        }
    }
    if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == state->close) state->done = 1;
        else if (event->widget == state->go) state->pending = 1;
        else if (event->widget == state->refresh) state->pending = 2;
    }
    if (event->widget == state->address && event->type == XXWIDGETS_EVENT_ACTIVATE) state->pending = 1;
    if (event->widget == state->hex && event->type == XXWIDGETS_EVENT_SELECT) {
        state->selected = 1; state->read_error = 0;
    } else if (event->widget == state->hex && event->type == XXWIDGETS_EVENT_READ_ERROR) {
        state->read_error = 1; state->selected = 0;
    }
}

static xxwidgets_status process_hex_layout(process_hex_dialog *state)
{
    int width = state->window->rect.width, height = state->window->rect.height;
    xxwidgets_status status;
    if (width < 78) width = 78;
    if (height < 18) height = 18;
    status = xxwidgets_widget_set_rect(state->hex, (xxwidgets_rect){2, 5, width - 4, height - 8});
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_rect(state->status, (xxwidgets_rect){2, height - 2, width - 4, 1});
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_rect(state->hint, (xxwidgets_rect){2, 3, width - 4, 1});
    return status;
}

static xxwidgets_status process_hex_location(process_hex_dialog *state, int update_address)
{
    char text[192];
    uint64_t base, selection, length;
    unsigned columns;
    xxwidgets_status status = xxwidgets_hexview_get_layout(state->hex, &base, &columns);
    if (status == XXWIDGETS_OK && update_address) {
        snprintf(text, sizeof(text), "0x%" PRIX64, base);
        status = xxwidgets_widget_set_text(state->address, text);
    }
    if (status != XXWIDGETS_OK) return status;
    status = xxwidgets_hexview_get_source_selection(state->hex, &selection, &length);
    if (status != XXWIDGETS_OK) return status;
    if (length) snprintf(text, sizeof(text), "Read only | Window 0x%" PRIX64 " | Selection 0x%" PRIX64 " (%" PRIu64 " bytes)", base, selection, length);
    else snprintf(text, sizeof(text), "Read only | Window 0x%" PRIX64 " | F5 refresh | Esc close", base);
    return xxwidgets_widget_set_text(state->status, text);
}

static xxwidgets_status process_hex_navigate(process_hex_dialog *state, int action)
{
    xxwidgets_status status;
    if (action == 3) {
        status = xxwidgets_hexview_dump_dialog(state->hex);
        if (status == XXWIDGETS_OK) return process_hex_location(state, 0);
        return xxwidgets_widget_set_text(state->status, "Could not save selected bytes. Check the path, replacement choice and process access.");
    }
    if (action == 1) {
        char text[128], *end, *start;
        unsigned long long address;
        size_t required;
        status = xxwidgets_widget_get_text(state->address, text, sizeof(text), &required);
        if (status != XXWIDGETS_OK) goto invalid;
        start = text;
        while (isspace((unsigned char)*start)) ++start;
        if (!*start || *start == '-' || *start == '+') goto invalid;
        errno = 0;
        /* Unprefixed addresses are decimal; 0x explicitly selects hex. */
        address = strtoull(start, &end, start[0] == '0' && (start[1] == 'x' || start[1] == 'X') ? 16 : 10);
        if (errno || end == start || address >= UINT64_MAX) goto invalid;
        while (isspace((unsigned char)*end)) ++end;
        if (*end) goto invalid;
        status = xxwidgets_hexview_seek_device(state->hex, (uint64_t)address);
    } else status = xxwidgets_hexview_refresh_device(state->hex);
    if (status == XXWIDGETS_OK) return process_hex_location(state, 1);
    return xxwidgets_widget_set_text(state->status,
        "Cannot read this window. Check the address or process access; previous bytes retained.");
invalid:
    return xxwidgets_widget_set_text(state->status, "Enter a decimal address or hexadecimal with 0x (for example 0x1000).");
}

xxwidgets_status xxwidget_systemprocessesviewer_show_hexview(xxwidgets_widget *viewer)
{
    xxwidgets_process_info process;
    xxwidgets_process_memory memory = {0};
    process_hex_dialog state = {0};
    xxwidgets_widget *owner, *label = NULL, *previous_default;
    xxwidgets_app *app;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    size_t index;
    int enabled, disabled = 0, attached = 0, initial_read_failed = 0;
    char title[96], hint[160];
    xxwidgets_status status, cleanup;
    static const xxwidgets_shortcut shortcuts[] = {{"F5", 1}, {"Esc", 2}};
    if (!viewer || viewer->kind != XXWIDGET_SYSTEMPROCESSESVIEWER || !viewer->parent) return XXWIDGETS_INVALID_ARGUMENT;
    app = viewer->app; owner = viewer->parent;
    if (app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit) return XXWIDGETS_BUSY;
    status = xxwidget_systemprocessesviewer_get_selection(viewer, &index, &process);
    if (status != XXWIDGETS_OK) return status;
    if (index == SIZE_MAX) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_process_memory_open(&process, &memory);
    if (status != XXWIDGETS_OK) {
        (void)xxwidgets_text_dialog(owner, XXW_TR((owner)->app, "Hex view unavailable"), "Cannot open process memory. The selected process must still exist and allow read access.");
        return status;
    }
    enabled = owner->enabled;
    previous_event = app->on_event; previous_user = app->user_data; previous_default = app->modal_default;
    app->on_event = process_hex_event; app->user_data = &state;
    snprintf(title, sizeof(title), "Hex view - PID %" PRIu64, process.pid);
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title,
        (xxwidgets_rect){owner->rect.x, owner->rect.y, 78, 22}, &state.window);
    if (status != XXWIDGETS_OK) goto done;
    app->modal_window = state.window;
    status = xxwidgets_window_set_minimum_size(state.window, 78, 18);
    if (status != XXWIDGETS_OK) goto done;
#define PROCESS_HEX_CONTROL(field, kind, text, x, y, w, h) \
    do { status = xxwidgets_widget_create(app, state.window, kind, text, (xxwidgets_rect){x, y, w, h}, &(field)); \
         if (status != XXWIDGETS_OK) goto done; } while (0)
    PROCESS_HEX_CONTROL(label, XXWIDGETS_LABEL, "Address:", 2, 1, 9, 1);
    PROCESS_HEX_CONTROL(state.address, XXWIDGETS_EDIT, "", 12, 1, 28, 1);
    PROCESS_HEX_CONTROL(state.go, XXWIDGETS_BUTTON, "Go", 42, 1, 8, 1);
    PROCESS_HEX_CONTROL(state.refresh, XXWIDGETS_BUTTON, "Refresh", 52, 1, 12, 1);
    PROCESS_HEX_CONTROL(state.close, XXWIDGETS_BUTTON, "Close", 66, 1, 10, 1);
    snprintf(hint, sizeof(hint), "Initial readable region: 0x%" PRIX64 " - 0x%" PRIX64, memory.base, memory.base + memory.size - 1);
    PROCESS_HEX_CONTROL(state.hint, XXWIDGETS_LABEL, hint, 2, 3, 74, 1);
    PROCESS_HEX_CONTROL(state.hex, XXWIDGETS_HEXVIEW, "", 2, 5, 74, 14);
    PROCESS_HEX_CONTROL(state.status, XXWIDGETS_LABEL, "Read only", 2, 20, 74, 1);
#undef PROCESS_HEX_CONTROL
    status = xxwidgets_hexview_set_layout(state.hex, 0, 8);
    if (status == XXWIDGETS_OK) {
        status = xxwidgets_hexview_set_device(state.hex, memory.device, memory.base,
            memory.size < 4096 ? (size_t)memory.size : 4096, 0);
        initial_read_failed = status != XXWIDGETS_OK;
    }
    if (status == XXWIDGETS_OK) status = xxwidgets_hexview_set_dump_handler(state.hex, process_hex_dump, &state);
    if (status == XXWIDGETS_OK) status = process_hex_location(&state, 1);
    if (status == XXWIDGETS_OK) status = xxwidgets_window_set_shortcuts(state.window, shortcuts, sizeof(shortcuts) / sizeof(shortcuts[0]));
    if (status != XXWIDGETS_OK) goto done;
    app->modal_default = state.go;
    status = xxwidgets_widget_set_enabled(owner, 0);
    if (status != XXWIDGETS_OK) goto done;
    disabled = 1;
    if (app->ops->modal_owner) {
        status = app->ops->modal_owner(state.window, owner, 1);
        if (status != XXWIDGETS_OK) goto done;
        attached = 1;
    }
    status = xxwidgets_widget_focus(state.hex);
    while (status == XXWIDGETS_OK && !state.done && !app->quit) {
        status = xxwidgets_app_poll(app, 30);
        if (status != XXWIDGETS_OK || state.done || app->quit) break;
        if (state.resized) { state.resized = 0; status = process_hex_layout(&state); }
        if (status == XXWIDGETS_OK && state.read_error) {
            state.read_error = 0;
            status = xxwidgets_widget_set_text(state.status, "Cannot read this address; previous bytes retained. Enter a readable address or refresh.");
        }
        if (status == XXWIDGETS_OK && state.pending) {
            int action = state.pending; state.pending = 0; state.selected = 0;
            status = process_hex_navigate(&state, action);
        } else if (status == XXWIDGETS_OK && state.selected) {
            state.selected = 0;
            status = process_hex_location(&state, 1);
        }
    }
done:
    if (attached) { cleanup = app->ops->modal_owner(state.window, owner, 0); if (status == XXWIDGETS_OK) status = cleanup; }
    if (disabled) {
        cleanup = xxwidgets_widget_set_enabled(owner, enabled);
        if (cleanup != XXWIDGETS_OK) {
            owner->enabled = enabled;
            ++app->syncing; (void)app->ops->sync(owner); --app->syncing;
        }
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (state.window) { cleanup = xxwidgets_widget_destroy(state.window); if (status == XXWIDGETS_OK) status = cleanup; }
    /* HexView borrows the wrapper; destroy its UI before native handle cleanup. */
    xxwidgets_process_memory_close(&memory);
    app->modal_window = NULL; app->modal_default = previous_default;
    app->on_event = previous_event; app->user_data = previous_user;
    if (initial_read_failed && !app->quit)
        (void)xxwidgets_text_dialog(owner, XXW_TR((owner)->app, "Hex view unavailable"), "The initial memory region could not be read. Process mappings or read permissions may have changed.");
    if (enabled && !app->quit) (void)xxwidgets_widget_focus(viewer);
    return status;
}
