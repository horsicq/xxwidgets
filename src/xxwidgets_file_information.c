/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_file_information.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct xxwidgets_file_information {
    xxwidgets_widget *owner;
    xxwidgets_widget *controls[XXWIDGETS_FILE_INFORMATION_CONTROL_COUNT];
    xxwidgets_data_struct_view *view;
    char *path, *summary;
};

static int valid_bounds(xxwidgets_rect bounds)
{
    return bounds.x >= 0 && bounds.y >= 0 && bounds.width >= 72 && bounds.height >= 12 &&
        bounds.x <= INT_MAX - bounds.width && bounds.y <= INT_MAX - bounds.height;
}

xxwidgets_status xxwidgets_file_information_set_rect(xxwidgets_file_information *information, xxwidgets_rect bounds)
{
    xxwidgets_status status;
    if (!information || !valid_bounds(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_widget_set_rect(information->controls[0], (xxwidgets_rect){bounds.x, bounds.y, bounds.width, 1});
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_set_rect(information->controls[1], (xxwidgets_rect){bounds.x, bounds.y + 2, bounds.width, 1});
    if (status == XXWIDGETS_OK)
        status = xxwidgets_data_struct_view_set_rect(information->view,
            (xxwidgets_rect){bounds.x, bounds.y + 4, bounds.width, bounds.height - 4});
    return status;
}

xxwidgets_status xxwidgets_file_information_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_file_information **out)
{
    xxwidgets_file_information *information;
    xxwidgets_status status;
    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !valid_bounds(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing) return XXWIDGETS_BUSY;
    information = calloc(1, sizeof(*information));
    if (!information) return XXWIDGETS_OUT_OF_MEMORY;
    information->owner = owner;
    status = xxwidgets_widget_create(owner->app, owner, XXWIDGETS_LABEL, "", (xxwidgets_rect){bounds.x, bounds.y, bounds.width, 1}, &information->controls[0]);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(owner->app, owner, XXWIDGETS_LABEL, "", (xxwidgets_rect){bounds.x, bounds.y + 2, bounds.width, 1}, &information->controls[1]);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_data_struct_view_create(owner, (xxwidgets_rect){bounds.x, bounds.y + 4, bounds.width, bounds.height - 4}, &information->view);
    if (status != XXWIDGETS_OK) {
        (void)xxwidgets_file_information_destroy(information);
        return status;
    }
    /* One row: a long path keeps its file name, and '&' is shown as is. */
    information->controls[XXWIDGETS_FILE_INFORMATION_PATH]->keep_end = 1;
    *out = information;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_file_information_destroy(xxwidgets_file_information *information)
{
    size_t i;
    xxwidgets_status status;
    if (!information) return XXWIDGETS_INVALID_ARGUMENT;
    if (information->owner->app->dispatch_depth || information->owner->app->polling || information->owner->app->syncing) return XXWIDGETS_BUSY;
    if (information->view) {
        status = xxwidgets_data_struct_view_destroy(information->view);
        if (status != XXWIDGETS_OK) return status;
        information->view = NULL;
    }
    for (i = 0; i < XXWIDGETS_FILE_INFORMATION_CONTROL_COUNT; ++i) if (information->controls[i]) {
        status = xxwidgets_widget_destroy(information->controls[i]);
        if (status != XXWIDGETS_OK) return status;
        information->controls[i] = NULL;
    }
    free(information->path); free(information->summary); free(information);
    return XXWIDGETS_OK;
}

xxwidgets_widget *xxwidgets_file_information_control(const xxwidgets_file_information *information, xxwidgets_file_information_control_id control)
{
    return information && control >= 0 && control < XXWIDGETS_FILE_INFORMATION_CONTROL_COUNT ? information->controls[control] : NULL;
}
xxwidgets_data_struct_view *xxwidgets_file_information_view(const xxwidgets_file_information *information)
{
    return information ? information->view : NULL;
}
xxwidgets_status xxwidgets_file_information_handle_event(xxwidgets_file_information *information, const xxwidgets_event *event)
{
    return information ? xxwidgets_data_struct_view_handle_event(information->view, event) : XXWIDGETS_INVALID_ARGUMENT;
}

xxwidgets_status xxwidgets_file_information_set_data(xxwidgets_file_information *information,
    const char *path, const char *file_type, int64_t size, const xxwidgets_data_struct_item *items, size_t count)
{
    char *new_path, *summary;
    size_t capacity;
    xxwidgets_status status;
    if (!information || !path || !file_type || !xxwidgets_valid_utf8(path) || !xxwidgets_valid_utf8(file_type) ||
        size < 0 || (!items && count) || strlen(file_type) > SIZE_MAX - 256) return XXWIDGETS_INVALID_ARGUMENT;
    capacity = strlen(file_type) + 256;
    new_path = xxwidgets_strdup(path); summary = malloc(capacity);
    if (!new_path || !summary) { free(new_path); free(summary); return XXWIDGETS_OUT_OF_MEMORY; }
    snprintf(summary, capacity, "%s | %" PRId64 " %s | %zu %s | %s", file_type, size, XXW_TR(information->owner->app, "bytes"), count,
        xxwidgets_app_ngettext(information->owner->app, "structure", "structures", count),
        XXW_TR(information->owner->app, "Select a structure to inspect its fields"));
    status = xxwidgets_widget_set_text(information->controls[0], new_path);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(information->controls[1], summary);
    if (status == XXWIDGETS_OK) status = xxwidgets_data_struct_view_set_items(information->view, items, count);
    if (status != XXWIDGETS_OK) {
        (void)xxwidgets_widget_set_text(information->controls[0], information->path ? information->path : "");
        (void)xxwidgets_widget_set_text(information->controls[1], information->summary ? information->summary : "");
        free(new_path); free(summary);
        return status;
    }
    free(information->path); free(information->summary);
    information->path = new_path; information->summary = summary;
    return XXWIDGETS_OK;
}

typedef struct information_modal {
    xxwidgets_widget *window, *close, *copy, *copied;
    xxwidgets_file_information *information;
    /* The caller's data: borrowed for the duration of the dialog call. */
    const char *path, *file_type;
    int64_t size;
    const xxwidgets_data_struct_item *items;
    size_t count;
    int done;
    xxwidgets_status failure;
} information_modal;

/* Growable "Copy all" text, capped like the other tools' copies. */
typedef struct information_text {
    char *data;
    size_t length, capacity;
    int failed;
} information_text;

static void text_append(information_text *text, const char *value)
{
    size_t length = strlen(value ? value : "");
    if (text->failed) return;
    if (length > 32U * 1024U * 1024U - text->length) { text->failed = 1; return; }
    if (text->length + length + 1 > text->capacity) {
        size_t capacity = text->capacity ? text->capacity : 4096;
        char *grown;
        while (capacity < text->length + length + 1) capacity *= 2;
        grown = realloc(text->data, capacity);
        if (!grown) { text->failed = 1; return; }
        text->data = grown; text->capacity = capacity;
    }
    memcpy(text->data + text->length, value ? value : "", length + 1);
    text->length += length;
}

/* Offsets and sizes as the field table shows them: hexadecimal, or "-". */
static void text_number(information_text *text, int64_t value)
{
    char buffer[32];
    if (value < 0) snprintf(buffer, sizeof(buffer), "-");
    else snprintf(buffer, sizeof(buffer), "0x%" PRIX64, (uint64_t)value);
    text_append(text, buffer);
}

/* Path, summary, then every structure: its fields as tab-separated rows
 * under the field table's titles, or its descriptor when it has none. */
static xxwidgets_status information_copy_all(information_modal *modal)
{
    static const char *const titles[] = {XXW_N_("Name"), XXW_N_("Type"), XXW_N_("Relative offset"), XXW_N_("File offset"),
        XXW_N_("Size"), XXW_N_("Raw"), XXW_N_("Value"), XXW_N_("Comment")};
    xxwidgets_app *app = modal->window->app;
    information_text text = {0};
    char line[160];
    const char *message;
    size_t i, j;
    text_append(&text, modal->path); text_append(&text, "\n");
    snprintf(line, sizeof(line), "%" PRId64 " %s | %zu %s", modal->size, XXW_TR(app, "bytes"), modal->count,
        xxwidgets_app_ngettext(app, "structure", "structures", modal->count));
    text_append(&text, modal->file_type); text_append(&text, " | "); text_append(&text, line); text_append(&text, "\n");
    for (i = 0; i < modal->count && !text.failed; ++i) {
        const xxwidgets_data_struct_item *item = modal->items + i;
        text_append(&text, "\n"); text_append(&text, item->name); text_append(&text, "\n");
        if (!item->field_count) {
            const xx_data_struct *data = &item->data_struct;
            text_append(&text, "File offset: "); text_number(&text, data->offset);
            text_append(&text, " | Address: "); text_number(&text, data->address);
            text_append(&text, " | Total size: "); text_number(&text, data->total_size);
            snprintf(line, sizeof(line), " | Count: %" PRIu64 "\n", data->count);
            text_append(&text, line);
            continue;
        }
        for (j = 0; j < sizeof(titles) / sizeof(titles[0]); ++j) {
            text_append(&text, j ? "\t" : ""); text_append(&text, XXW_TR(app, titles[j]));
        }
        text_append(&text, "\n");
        for (j = 0; j < item->field_count && !text.failed; ++j) {
            const xxwidgets_data_struct_field *field = item->fields + j;
            text_append(&text, field->name); text_append(&text, "\t");
            text_append(&text, field->type); text_append(&text, "\t");
            text_number(&text, field->relative_offset); text_append(&text, "\t");
            text_number(&text, field->file_offset); text_append(&text, "\t");
            text_number(&text, field->size); text_append(&text, "\t");
            text_append(&text, field->raw); text_append(&text, "\t");
            text_append(&text, field->value); text_append(&text, "\t");
            text_append(&text, field->comment); text_append(&text, "\n");
        }
    }
    if (text.failed) message = XXW_TR(app, "Cannot copy that much data");
    else message = xxwidgets_clipboard_set_text(modal->window, text.data) == XXWIDGETS_OK ? XXW_TR(app, "Copied") : XXW_TR(app, "Clipboard unavailable");
    free(text.data);
    return xxwidgets_widget_set_text(modal->copied, message);
}

static void information_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    information_modal *modal = user;
    xxwidgets_status status = XXWIDGETS_OK;
    (void)app;
    if ((event->type == XXWIDGETS_EVENT_CLOSE && event->widget == modal->window) ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == modal->close) ||
        (event->type == XXWIDGETS_EVENT_SHORTCUT && event->value == 1)) modal->done = 1;
    else if (event->type == XXWIDGETS_EVENT_CLICK && event->widget == modal->copy && modal->information) status = information_copy_all(modal);
    else if (event->type == XXWIDGETS_EVENT_RESIZE && event->widget == modal->window && modal->information && modal->close) {
        xxwidgets_rect bounds = {0};
        status = xxwidgets_widget_get_rect(modal->window, &bounds);
        /* Backends may report less than the window minimum (Win32 and Cocoa
         * do not enforce it, GTK caps it at the work area): lay out at the
         * minimum and let the window clip, rather than fail the dialog. */
        if (bounds.width < modal->window->min_columns) bounds.width = modal->window->min_columns;
        if (bounds.height < modal->window->min_rows) bounds.height = modal->window->min_rows;
        if (status == XXWIDGETS_OK)
            status = xxwidgets_file_information_set_rect(modal->information, (xxwidgets_rect){1, 1, bounds.width - 2, bounds.height - 6});
        if (status == XXWIDGETS_OK)
            status = xxwidgets_widget_set_rect(modal->copy, (xxwidgets_rect){1, bounds.height - 3, 14, 2});
        if (status == XXWIDGETS_OK)
            status = xxwidgets_widget_set_rect(modal->copied, (xxwidgets_rect){16, bounds.height - 3, bounds.width - 30, 2});
        if (status == XXWIDGETS_OK)
            status = xxwidgets_widget_set_rect(modal->close, (xxwidgets_rect){bounds.width - 13, bounds.height - 3, 12, 2});
    } else if (modal->information) status = xxwidgets_file_information_handle_event(modal->information, event);
    if (status != XXWIDGETS_OK) { modal->failure = status; modal->done = 1; }
}

static int same_name(const char *left, const char *right, int fold)
{
    for (;; ++left, ++right) {
        unsigned char a = (unsigned char)*left, b = (unsigned char)*right;
        if (fold && a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
        if (fold && b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
        if (a != b) return 0;
        if (!a) return 1;
    }
}

size_t xxwidgets_file_information_find(const xxwidgets_data_struct_item *items, size_t count, const char *selector)
{
    size_t i;
    int fold;
    if (!items || !selector || !*selector) return SIZE_MAX;
    for (fold = 0; fold < 2; ++fold)
        for (i = 0; i < count; ++i)
            if (items[i].name && same_name(items[i].name, selector, fold)) return i;
    return SIZE_MAX;
}

static xxwidgets_status information_dialog(xxwidgets_widget *owner, const char *title, const char *path,
    const char *file_type, int64_t size, const xxwidgets_data_struct_item *items, size_t count, size_t selected);

xxwidgets_status xxwidgets_file_information_dialog(xxwidgets_widget *owner, const char *title,
    const char *path, const char *file_type, int64_t size, const xxwidgets_data_struct_item *items, size_t count)
{
    return information_dialog(owner, title, path, file_type, size, items, count, SIZE_MAX);
}

xxwidgets_status xxwidgets_file_information_dialog_at(xxwidgets_widget *owner, const char *title,
    const char *path, const char *file_type, int64_t size, const xxwidgets_data_struct_item *items, size_t count,
    const char *selector)
{
    return information_dialog(owner, title, path, file_type, size, items, count,
        xxwidgets_file_information_find(items, count, selector));
}

static xxwidgets_status information_dialog(xxwidgets_widget *owner, const char *title, const char *path,
    const char *file_type, int64_t size, const xxwidgets_data_struct_item *items, size_t count, size_t selected)
{
    information_modal modal = {0};
    xxwidgets_app *app;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    xxwidgets_widget *focus = NULL, *widget;
    xxwidgets_status status, cleanup;
    int enabled, disabled = 0, attached = 0;
    const xxwidgets_shortcut shortcuts[] = {{"Esc", 1}};
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !title || !xxwidgets_valid_utf8(title) ||
        owner->rect.x > INT_MAX - 2 || owner->rect.y > INT_MAX - 2) return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit) return XXWIDGETS_BUSY;
    previous_event = app->on_event; previous_user = app->user_data; enabled = owner->enabled;
    /* Return focus to the owner's focused control, else its first one. */
    for (widget = app->widgets; widget; widget = widget->next)
        if (widget->parent == owner && xxwidgets_focusable(widget)) {
            if (!focus) focus = widget;
            if (xxwidgets_widget_has_focus(widget) == 1) { focus = widget; break; }
        }
    modal.path = path; modal.file_type = file_type; modal.size = size; modal.items = items; modal.count = count;
    app->on_event = information_event; app->user_data = &modal;
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title,
        (xxwidgets_rect){owner->rect.x + 2, owner->rect.y + 2, 154, 36}, &modal.window);
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_window = modal.window;
    status = xxwidgets_window_set_minimum_size(modal.window, 120, 24);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_file_information_create(modal.window, (xxwidgets_rect){1, 1, 152, 30}, &modal.information);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_file_information_set_data(modal.information, path, file_type, size, items, count);
    if (status == XXWIDGETS_OK && selected < count) status = xxwidgets_data_struct_view_select(modal.information->view, selected);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, modal.window, XXWIDGETS_BUTTON, XXW_TR(app, "Copy all"), (xxwidgets_rect){1, 33, 14, 2}, &modal.copy);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, modal.window, XXWIDGETS_LABEL, "", (xxwidgets_rect){16, 33, 124, 2}, &modal.copied);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, modal.window, XXWIDGETS_BUTTON, XXW_TR(app, "Close"), (xxwidgets_rect){140, 33, 12, 2}, &modal.close);
    if (status == XXWIDGETS_OK) status = xxwidgets_window_set_shortcuts(modal.window, shortcuts, 1);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_widget_set_enabled(owner, 0);
    if (status != XXWIDGETS_OK) goto finish;
    disabled = 1;
    status = xxwidgets_window_set_modality(modal.window, owner, XXWIDGETS_WINDOW_MODAL);
    if (status != XXWIDGETS_OK) goto finish;
    attached = 1;
    status = xxwidgets_widget_focus(xxwidgets_data_struct_view_control(modal.information->view, XXWIDGETS_DATA_STRUCT_VIEW_LIST));
    while (status == XXWIDGETS_OK && !modal.done && !app->quit) status = xxwidgets_app_poll(app, 30);
    if (status == XXWIDGETS_OK) status = modal.failure;
finish:
    if (attached) { cleanup = xxwidgets_window_set_modality(modal.window, NULL, XXWIDGETS_MODELESS); if (status == XXWIDGETS_OK) status = cleanup; }
    if (modal.information) { cleanup = xxwidgets_file_information_destroy(modal.information); if (status == XXWIDGETS_OK) status = cleanup; }
    if (modal.window) { cleanup = xxwidgets_widget_destroy(modal.window); if (status == XXWIDGETS_OK) status = cleanup; }
    app->modal_window = NULL; app->on_event = previous_event; app->user_data = previous_user;
    if (disabled) { cleanup = xxwidgets_widget_set_enabled(owner, enabled); if (status == XXWIDGETS_OK) status = cleanup; }
    if (focus) (void)xxwidgets_widget_focus(focus);
    return status;
}
