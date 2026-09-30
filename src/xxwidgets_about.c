#include "xxwidgets_internal.h"
#include <stdlib.h>
#include <string.h>

xxwidgets_status xxwidgets_about_dialog_create(xxwidgets_about_dialog **out_dialog)
{
    xxwidgets_about_dialog *dialog;
    size_t i;
    if (!out_dialog) return XXWIDGETS_INVALID_ARGUMENT;
    *out_dialog = NULL;
    dialog = (xxwidgets_about_dialog *)calloc(1, sizeof(*dialog));
    if (!dialog) return XXWIDGETS_OUT_OF_MEMORY;
    for (i = 0; i < XXWIDGETS_ABOUT_TEXT_COUNT; ++i) {
        const char *text = i == XXWIDGETS_ABOUT_TITLE ? "About" :
            i == XXWIDGETS_ABOUT_PROGRAM_NAME ? "Application" :
            i == XXWIDGETS_ABOUT_CLOSE_LABEL ? "Close" : "";
        dialog->texts[i] = xxwidgets_strdup(text);
        if (!dialog->texts[i]) { xxwidgets_about_dialog_destroy(dialog); return XXWIDGETS_OUT_OF_MEMORY; }
    }
    *out_dialog = dialog;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_about_dialog_destroy(xxwidgets_about_dialog *dialog)
{
    size_t i;
    if (!dialog) return XXWIDGETS_INVALID_ARGUMENT;
    if (dialog->showing) return XXWIDGETS_BUSY;
    for (i = 0; i < XXWIDGETS_ABOUT_TEXT_COUNT; ++i) free(dialog->texts[i]);
    free(dialog->image);
    free(dialog);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_about_dialog_set_text(xxwidgets_about_dialog *dialog,
    xxwidgets_about_text field, const char *text)
{
    char *copy;
    if (!dialog || field < 0 || field >= XXWIDGETS_ABOUT_TEXT_COUNT || !xxwidgets_valid_utf8(text) ||
        (field == XXWIDGETS_ABOUT_CLOSE_LABEL && !text[0])) return XXWIDGETS_INVALID_ARGUMENT;
    if (dialog->showing) return XXWIDGETS_BUSY;
    copy = xxwidgets_strdup(text);
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    free(dialog->texts[field]); dialog->texts[field] = copy;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_about_dialog_get_text(const xxwidgets_about_dialog *dialog,
    xxwidgets_about_text field, char *buffer, size_t capacity, size_t *required)
{
    size_t length, copied;
    if (!dialog || field < 0 || field >= XXWIDGETS_ABOUT_TEXT_COUNT || (!buffer && capacity))
        return XXWIDGETS_INVALID_ARGUMENT;
    length = strlen(dialog->texts[field]) + 1;
    if (required) *required = length;
    if (!buffer && !capacity) return XXWIDGETS_OK;
    if (capacity) {
        copied = length < capacity ? length - 1 : capacity - 1;
        while (copied && (((unsigned char)dialog->texts[field][copied] & 0xc0) == 0x80)) --copied;
        memcpy(buffer, dialog->texts[field], copied); buffer[copied] = 0;
    }
    return capacity < length ? XXWIDGETS_BUFFER_TOO_SMALL : XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_about_dialog_set_image(xxwidgets_about_dialog *dialog,
    const void *rgba, unsigned int width, unsigned int height, size_t stride)
{
    unsigned char *copy = NULL;
    size_t row_bytes, row;
    if (!dialog) return XXWIDGETS_INVALID_ARGUMENT;
    if (dialog->showing) return XXWIDGETS_BUSY;
    if (!rgba && !width && !height && !stride) {
        free(dialog->image); dialog->image = NULL;
        dialog->image_width = dialog->image_height = 0;
        return XXWIDGETS_OK;
    }
    if (!rgba || !width || !height || width > 32767 || height > 32767) return XXWIDGETS_INVALID_ARGUMENT;
    row_bytes = (size_t)width * 4;
    if (stride < row_bytes || height > SIZE_MAX / row_bytes ||
        (height > 1 && stride > (SIZE_MAX - row_bytes) / (height - 1))) return XXWIDGETS_INVALID_ARGUMENT;
    copy = (unsigned char *)malloc(row_bytes * height);
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    for (row = 0; row < height; ++row)
        memcpy(copy + row * row_bytes, (const unsigned char *)rgba + row * stride, row_bytes);
    free(dialog->image); dialog->image = copy;
    dialog->image_width = width; dialog->image_height = height;
    return XXWIDGETS_OK;
}

static char *about_body(const xxwidgets_about_dialog *dialog)
{
    size_t field, length = 1, used = 0;
    char *body;
    for (field = XXWIDGETS_ABOUT_PROGRAM_NAME; field <= XXWIDGETS_ABOUT_CREDITS; ++field) {
        size_t add = strlen(dialog->texts[field]);
        if (add > SIZE_MAX - 2 || length > SIZE_MAX - add - 2) return NULL;
        length += add + 2;
    }
    body = (char *)malloc(length);
    if (!body) return NULL;
    for (field = XXWIDGETS_ABOUT_PROGRAM_NAME; field <= XXWIDGETS_ABOUT_CREDITS; ++field) {
        size_t add = strlen(dialog->texts[field]);
        if (!add) continue;
        if (used) { body[used++] = '\n'; body[used++] = '\n'; }
        memcpy(body + used, dialog->texts[field], add); used += add;
    }
    body[used] = 0;
    return body;
}

typedef struct about_state {
    xxwidgets_widget *window, *close, *copy;
    const char *body;
    int done;
} about_state;

static void about_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    about_state *state = (about_state *)user;
    (void)app;
    if ((event->widget == state->window && event->type == XXWIDGETS_EVENT_CLOSE) ||
        (event->widget == state->close && event->type == XXWIDGETS_EVENT_CLICK)) state->done = 1;
    else if (event->widget == state->copy && event->type == XXWIDGETS_EVENT_CLICK) {
        xxwidgets_status status = app->ops->copy_text(state->window, state->body);
        xxwidgets_widget_set_text(state->copy, status == XXWIDGETS_OK ? "Copied" : "Copy failed");
    }
}

/* Wrap at Unicode scalar boundaries for the terminal's scrolling text list. */
static xxwidgets_status about_text_content(xxwidgets_widget *window, const char *body)
{
    xxwidgets_widget *list;
    xxwidgets_rect bounds = {2, 1, 68, 17};
    xxwidgets_status status = xxwidgets_widget_create(window->app, window, XXWIDGETS_LISTBOX,
        "About text", bounds, &list);
    const char *cursor = body;
    if (status != XXWIDGETS_OK) return status;
    while (*cursor) {
        const char *start = cursor;
        size_t columns = 0, bytes;
        char *line;
        while (*cursor && *cursor != '\n' && columns < 64) {
            ++cursor;
            while (((unsigned char)*cursor & 0xc0) == 0x80) ++cursor;
            ++columns;
        }
        bytes = (size_t)(cursor - start);
        line = (char *)malloc(bytes + 1);
        if (!line) return XXWIDGETS_OUT_OF_MEMORY;
        memcpy(line, start, bytes); line[bytes] = 0;
        status = xxwidgets_listbox_add(list, line); free(line);
        if (status != XXWIDGETS_OK) return status;
        if (*cursor == '\n') ++cursor;
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status show_content(xxwidgets_about_dialog *dialog, xxwidgets_widget *owner, int copyable)
{
    xxwidgets_app *app;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    xxwidgets_widget *previous_focus = NULL, *widget;
    xxwidgets_rect bounds;
    about_state state = {0};
    xxwidgets_status status, cleanup;
    char *body;
    int owner_enabled, owner_disabled = 0, attached = 0;
    if (!dialog || !owner || owner->kind != XXWIDGETS_WINDOW) return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (dialog->showing || app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit)
        return XXWIDGETS_BUSY;
    body = about_body(dialog);
    if (!body) return XXWIDGETS_OUT_OF_MEMORY;
    state.body = body;
    dialog->showing = 1;
    previous_event = app->on_event; previous_user = app->user_data;
    owner_enabled = owner->enabled;
    for (widget = app->widgets; widget; widget = widget->next)
        if (widget->parent == owner && xxwidgets_focusable(widget)) { previous_focus = widget; break; }
    app->on_event = about_event; app->user_data = &state;
    bounds.x = owner->rect.x < 32765 ? owner->rect.x + 2 : owner->rect.x;
    bounds.y = owner->rect.y < 32765 ? owner->rect.y + 2 : owner->rect.y;
    bounds.width = 72; bounds.height = 22;
    if (app->backend == XXWIDGETS_BACKEND_TUI) {
        bounds.x = owner->rect.x + (owner->rect.width > 72 ? (owner->rect.width - 72) / 2 : 0);
        bounds.y = owner->rect.y;
    }
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW,
        dialog->texts[XXWIDGETS_ABOUT_TITLE], bounds, &state.window);
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_window = state.window;
    status = xxwidgets_widget_set_visible(state.window, 0);
    if (status != XXWIDGETS_OK) goto finish;
    if (app->ops->about_content) {
        ++app->syncing;
        status = app->ops->about_content(state.window, dialog, body);
        --app->syncing;
    } else status = about_text_content(state.window, body);
    if (status != XXWIDGETS_OK) goto finish;
    bounds.x = 58; bounds.y = 19; bounds.width = 12; bounds.height = 2;
    status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON,
        dialog->texts[XXWIDGETS_ABOUT_CLOSE_LABEL], bounds, &state.close);
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_default = state.close;
    if (copyable && app->ops->copy_text) {
        bounds.x = 2; bounds.width = 18;
        status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON,
            "Copy all", bounds, &state.copy);
        if (status != XXWIDGETS_OK) goto finish;
    }
    status = xxwidgets_widget_set_enabled(owner, 0);
    if (status != XXWIDGETS_OK) goto finish;
    owner_disabled = 1;
    if (app->ops->modal_owner) {
        status = app->ops->modal_owner(state.window, owner, 1);
        if (status != XXWIDGETS_OK) goto finish;
        attached = 1;
    }
    status = xxwidgets_widget_set_visible(state.window, 1);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_focus(state.close);
    while (status == XXWIDGETS_OK && !state.done && !app->quit) status = xxwidgets_app_poll(app, 30);
finish:
    if (owner_disabled) {
        cleanup = xxwidgets_widget_set_enabled(owner, owner_enabled);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (attached) {
        cleanup = app->ops->modal_owner(state.window, owner, 0);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (state.window) {
        cleanup = xxwidgets_widget_destroy(state.window);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    app->modal_window = NULL; app->modal_default = NULL;
    app->on_event = previous_event; app->user_data = previous_user;
    dialog->showing = 0;
    free(body);
    if (previous_focus) xxwidgets_widget_focus(previous_focus);
    return status;
}

xxwidgets_status xxwidgets_about_dialog_show(xxwidgets_about_dialog *dialog, xxwidgets_widget *owner)
{
    return show_content(dialog, owner, 0);
}

xxwidgets_status xxwidgets_text_dialog(xxwidgets_widget *owner, const char *title, const char *text)
{
    xxwidgets_about_dialog *content = NULL;
    xxwidgets_status status;
    if (!owner || !xxwidgets_valid_utf8(title) || !xxwidgets_valid_utf8(text))
        return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_about_dialog_create(&content);
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_set_text(content, XXWIDGETS_ABOUT_TITLE, title);
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_set_text(content, XXWIDGETS_ABOUT_PROGRAM_NAME, "");
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_set_text(content, XXWIDGETS_ABOUT_DESCRIPTION, text);
    if (status == XXWIDGETS_OK) status = show_content(content, owner, 1);
    if (content) xxwidgets_about_dialog_destroy(content);
    return status;
}
