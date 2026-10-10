#include "xxwidgets_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int xxwidgets_font_valid(const xxwidgets_font *font)
{
    const unsigned char *text;
    if (!font || !memchr(font->family, 0, sizeof(font->family)) || !xxwidgets_valid_utf8(font->family) ||
        (font->point_size && (font->point_size < 4 || font->point_size > 96)) || (font->bold != 0 && font->bold != 1) || (font->italic != 0 && font->italic != 1))
        return 0;
    text = (const unsigned char *)font->family;
    while (*text) {
        if (*text < 32 || *text == 127) return 0;
        ++text;
    }
    return 1;
}

static int valid_options(const xxwidgets_font_options *options)
{
    size_t role;
    if (!options) return 0;
    for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role)
        if (!xxwidgets_font_valid(&options->fonts[role])) return 0;
    return 1;
}

xxwidgets_status xxwidgets_font_options_init(xxwidgets_font_options *options)
{
    if (!options) return XXWIDGETS_INVALID_ARGUMENT;
    memset(options, 0, sizeof(*options));
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_app_get_font_options(const xxwidgets_app *app, xxwidgets_font_options *options)
{
    if (!app || !options) return XXWIDGETS_INVALID_ARGUMENT;
    *options = app->font_options;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_app_set_font_options(xxwidgets_app *app, const xxwidgets_font_options *options)
{
    xxwidgets_status status = XXWIDGETS_OK;
    xxwidgets_font_options copied;
    if (!app || !valid_options(options)) return XXWIDGETS_INVALID_ARGUMENT;
    copied = *options;
    if (!app->ops->apply_fonts && app->backend != XXWIDGETS_BACKEND_TUI) return XXWIDGETS_UNAVAILABLE;
    ++app->syncing;
    if (app->ops->apply_fonts) status = app->ops->apply_fonts(app, &copied);
    --app->syncing;
    if (status == XXWIDGETS_OK) app->font_options = copied;
    return status;
}

xxwidgets_status xxwidgets_font_choose_dialog(xxwidgets_widget *owner, xxwidgets_font_role role, xxwidgets_font *font, int *accepted)
{
    xxwidgets_font copied;
    xxwidgets_status status;
    xxwidgets_app *app;
    if (accepted) *accepted = 0;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || role < 0 || role >= XXWIDGETS_FONT_ROLE_COUNT || !xxwidgets_font_valid(font) || !accepted)
        return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->quit || (app->modal_window && app->modal_window != owner)) return XXWIDGETS_BUSY;
    if (!app->ops->choose_font) return XXWIDGETS_UNAVAILABLE;
    copied = *font;
    status = app->ops->choose_font(owner, role, &copied, accepted);
    if (status == XXWIDGETS_OK && *accepted) {
        if (!xxwidgets_font_valid(&copied)) {
            *accepted = 0;
            return XXWIDGETS_PLATFORM_ERROR;
        }
        *font = copied;
    } else *accepted = 0;
    return status;
}

typedef struct font_row {
    xxwidgets_widget *family, *size, *bold, *italic, *choose, *reset, *preview;
} font_row;

typedef struct font_state {
    xxwidgets_widget *window, *ok, *cancel, *preview, *error;
    font_row rows[XXWIDGETS_FONT_ROLE_COUNT];
    xxwidgets_font_options options;
    int done, accepted, pending_choose, layout_dirty;
} font_state;

static xxwidgets_status read_row(font_state *state, size_t role, xxwidgets_font *font)
{
    char size_text[32], *end;
    unsigned long points;
    xxwidgets_status status;
    memset(font, 0, sizeof(*font));
    status = xxwidgets_widget_get_text(state->rows[role].family, font->family, sizeof(font->family), NULL);
    if (status != XXWIDGETS_OK) return status == XXWIDGETS_BUFFER_TOO_SMALL ? XXWIDGETS_INVALID_ARGUMENT : status;
    status = xxwidgets_widget_get_text(state->rows[role].size, size_text, sizeof(size_text), NULL);
    if (status != XXWIDGETS_OK) return status == XXWIDGETS_BUFFER_TOO_SMALL ? XXWIDGETS_INVALID_ARGUMENT : status;
    points = strtoul(size_text, &end, 10);
    if (end == size_text || *end || points > 96 || (points && points < 4)) return XXWIDGETS_INVALID_ARGUMENT;
    font->point_size = (unsigned int)points;
    status = xxwidgets_widget_get_value(state->rows[role].bold, &font->bold);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(state->rows[role].italic, &font->italic);
    if (status != XXWIDGETS_OK) return status;
    return xxwidgets_font_valid(font) ? XXWIDGETS_OK : XXWIDGETS_INVALID_ARGUMENT;
}

static xxwidgets_status preview_row(font_state *state, size_t role, const xxwidgets_font *font)
{
    char text[192];
    xxwidgets_status status;
    snprintf(text, sizeof(text), "AaBb 0123 - %s, %u pt%s%s", font->family[0] ? font->family : "Default family", font->point_size, font->bold ? ", bold" : "",
             font->italic ? ", italic" : "");
    status = xxwidgets_widget_set_text(state->rows[role].preview, text);
    if (status == XXWIDGETS_OK && state->window->app->ops->preview_font)
        status = state->window->app->ops->preview_font(state->rows[role].preview, (xxwidgets_font_role)role, font);
    return status;
}

static xxwidgets_status fill_row(font_state *state, size_t role)
{
    char size_text[16];
    xxwidgets_font *font = &state->options.fonts[role];
    xxwidgets_status status = xxwidgets_widget_set_text(state->rows[role].family, font->family);
    snprintf(size_text, sizeof(size_text), "%u", font->point_size);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(state->rows[role].size, size_text);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(state->rows[role].bold, font->bold);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(state->rows[role].italic, font->italic);
    if (status == XXWIDGETS_OK) status = preview_row(state, role, font);
    return status;
}

static void font_error(font_state *state, xxwidgets_status status)
{
    xxwidgets_widget_set_text(state->error,
                              status == XXWIDGETS_INVALID_ARGUMENT ? "Enter a font family and size 4..96; 0 uses the default size." : xxwidgets_status_string(status));
}

static void font_event(xxwidgets_app *app, const xxwidgets_event *event, void *user_data)
{
    font_state *state = (font_state *)user_data;
    size_t role;
    (void)app;
    if ((event->widget == state->window && event->type == XXWIDGETS_EVENT_CLOSE) || (event->widget == state->cancel && event->type == XXWIDGETS_EVENT_CLICK)) {
        state->done = 1;
        return;
    }
    if (event->type != XXWIDGETS_EVENT_CLICK) return;
    for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role) {
        if (event->widget == state->rows[role].choose) {
            state->pending_choose = (int)role;
            return;
        }
        if (event->widget == state->rows[role].reset) {
            xxwidgets_status status;
            memset(&state->options.fonts[role], 0, sizeof(xxwidgets_font));
            status = fill_row(state, role);
            if (status != XXWIDGETS_OK) font_error(state, status);
            state->layout_dirty = 1;
            return;
        }
    }
    if (event->widget == state->ok || event->widget == state->preview) {
        xxwidgets_font_options copied;
        xxwidgets_status status = XXWIDGETS_OK;
        for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role) {
            status = read_row(state, role, &copied.fonts[role]);
            if (status != XXWIDGETS_OK) break;
        }
        if (status != XXWIDGETS_OK) {
            font_error(state, status);
            return;
        }
        state->options = copied;
        for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT && status == XXWIDGETS_OK; ++role) status = preview_row(state, role, &copied.fonts[role]);
        state->layout_dirty = 1;
        if (status != XXWIDGETS_OK) {
            font_error(state, status);
            return;
        }
        xxwidgets_widget_set_text(state->error, "");
        if (event->widget == state->ok) {
            state->accepted = 1;
            state->done = 1;
        }
    }
}

xxwidgets_status xxwidgets_font_options_dialog(xxwidgets_widget *owner, const char *title, xxwidgets_font_options *options, int *accepted)
{
    static const char *roles[] = {"Controls", "Table views", "Tree views", "Text edits"};
    font_state state = {0};
    xxwidgets_app *app;
    xxwidgets_widget *widget, *focus = NULL, *previous_default;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    int owner_enabled, disabled = 0, attached = 0;
    size_t role;
    xxwidgets_status status = XXWIDGETS_OK, cleanup;
    if (accepted) *accepted = 0;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !xxwidgets_valid_utf8(title) || !valid_options(options) || !accepted) return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit) return XXWIDGETS_BUSY;
    state.options = *options;
    state.pending_choose = -1;
    previous_event = app->on_event;
    previous_user = app->user_data;
    previous_default = app->modal_default;
    owner_enabled = owner->enabled;
    for (widget = app->widgets; widget; widget = widget->next)
        if (widget->parent == owner && xxwidgets_focusable(widget)) {
            focus = widget;
            break;
        }
    app->on_event = font_event;
    app->user_data = &state;
    status = xxwidgets_widget_create(
        app, NULL, XXWIDGETS_WINDOW, title,
        (xxwidgets_rect){owner->rect.x < 32765 ? owner->rect.x + 2 : owner->rect.x, owner->rect.y < 32765 ? owner->rect.y + 2 : owner->rect.y, 100, 23}, &state.window);
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_window = state.window;
#define FONT_CONTROL(slot, kind, text, x, y, width)                                                              \
    do {                                                                                                         \
        status = xxwidgets_widget_create(app, state.window, kind, text, (xxwidgets_rect){x, y, width, 1}, slot); \
        if (status != XXWIDGETS_OK) goto finish;                                                                 \
    } while (0)
    for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role) {
        xxwidgets_widget *label;
        int y = 1 + (int)role * 4;
        FONT_CONTROL(&label, XXWIDGETS_LABEL, roles[role], 2, y, 18);
        FONT_CONTROL(&state.rows[role].family, XXWIDGETS_EDIT, "", 22, y, 36);
        FONT_CONTROL(&state.rows[role].size, XXWIDGETS_EDIT, "0", 60, y, 6);
        FONT_CONTROL(&state.rows[role].bold, XXWIDGETS_CHECKBOX, "Bold", 68, y, 8);
        FONT_CONTROL(&state.rows[role].italic, XXWIDGETS_CHECKBOX, "Italic", 77, y, 9);
        FONT_CONTROL(&state.rows[role].choose, XXWIDGETS_BUTTON, "Choose", 88, y, 10);
        FONT_CONTROL(&state.rows[role].reset, XXWIDGETS_BUTTON, "Default", 88, y + 2, 10);
        FONT_CONTROL(&state.rows[role].preview, XXWIDGETS_LABEL, "", 22, y + 2, 64);
        status = fill_row(&state, role);
        if (status != XXWIDGETS_OK) goto finish;
    }
    FONT_CONTROL(&state.error, XXWIDGETS_LABEL, "", 2, 18, 96);
    FONT_CONTROL(&state.preview, XXWIDGETS_BUTTON, "Preview", 2, 20, 14);
    FONT_CONTROL(&state.ok, XXWIDGETS_BUTTON, "OK", 72, 20, 10);
    FONT_CONTROL(&state.cancel, XXWIDGETS_BUTTON, "Cancel", 84, 20, 14);
#undef FONT_CONTROL
    if (app->ops->font_options_layout) {
        status = app->ops->font_options_layout(state.window, 1);
        if (status != XXWIDGETS_OK) goto finish;
    }
    app->modal_default = state.ok;
    status = xxwidgets_widget_set_enabled(owner, 0);
    if (status != XXWIDGETS_OK) goto finish;
    disabled = 1;
    if (app->ops->modal_owner) {
        status = app->ops->modal_owner(state.window, owner, 1);
        if (status != XXWIDGETS_OK) goto finish;
        attached = 1;
    }
    status = xxwidgets_widget_focus(state.rows[0].family);
    while (status == XXWIDGETS_OK && !state.done && !app->quit) {
        status = xxwidgets_app_poll(app, 30);
        if (status == XXWIDGETS_OK && state.pending_choose >= 0) {
            xxwidgets_font selected;
            int chosen = 0, pending = state.pending_choose;
            state.pending_choose = -1;
            status = read_row(&state, (size_t)pending, &selected);
            if (status == XXWIDGETS_OK) status = xxwidgets_font_choose_dialog(state.window, (xxwidgets_font_role)pending, &selected, &chosen);
            if (status == XXWIDGETS_UNAVAILABLE) {
                xxwidgets_widget_focus(state.rows[pending].family);
                xxwidgets_widget_set_text(state.error, "Edit family, size, and style, then select Preview.");
                status = XXWIDGETS_OK;
            } else if (status != XXWIDGETS_OK) {
                font_error(&state, status);
                status = XXWIDGETS_OK;
            } else if (chosen) {
                state.options.fonts[pending] = selected;
                status = fill_row(&state, (size_t)pending);
                state.layout_dirty = 1;
            }
        }
        if (status == XXWIDGETS_OK && !state.done && app->ops->font_options_layout) {
            status = app->ops->font_options_layout(state.window, state.layout_dirty);
            state.layout_dirty = 0;
        }
    }
    if (app->quit) state.accepted = 0;
finish:
    if (disabled) {
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
    app->modal_window = NULL;
    app->modal_default = previous_default;
    app->on_event = previous_event;
    app->user_data = previous_user;
    if (focus) xxwidgets_widget_focus(focus);
    if (status == XXWIDGETS_OK && state.accepted) {
        *options = state.options;
        *accepted = 1;
    }
    return status;
}
