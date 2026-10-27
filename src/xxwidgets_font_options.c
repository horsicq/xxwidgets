/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

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

int xxwidgets_font_is_monospace(const xxwidgets_font *font)
{
    static const char generic[] = "monospace";
    size_t i;
    if (!font->family[0]) return 1;
    for (i = 0; i < sizeof(generic); ++i) {
        unsigned char c = (unsigned char)font->family[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (unsigned char)generic[i]) return 0;
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

xxwidgets_status xxwidgets_hexview_get_font(const xxwidgets_widget *view, xxwidgets_font *font)
{
    if (!view || view->kind != XXWIDGETS_HEXVIEW || !font) return XXWIDGETS_INVALID_ARGUMENT;
    if (view->hex_has_font) *font = view->hex_font;
    else {
        memset(font, 0, sizeof(*font));
        memcpy(font->family, "monospace", sizeof("monospace"));
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_hexview_set_font(xxwidgets_widget *view, const xxwidgets_font *font)
{
    xxwidgets_status status = XXWIDGETS_OK;
    xxwidgets_font copied;
    if (!view || view->kind != XXWIDGETS_HEXVIEW || !xxwidgets_font_valid(font)) return XXWIDGETS_INVALID_ARGUMENT;
    copied = *font;
    if (!copied.family[0]) memcpy(copied.family, "monospace", sizeof("monospace"));
    if (view->hex_has_font && !strcmp(view->hex_font.family, copied.family) && view->hex_font.point_size == copied.point_size &&
        view->hex_font.bold == copied.bold && view->hex_font.italic == copied.italic) return XXWIDGETS_OK;
    if (!view->app->ops->hexview_font && view->app->backend != XXWIDGETS_BACKEND_TUI) return XXWIDGETS_UNAVAILABLE;
    ++view->app->syncing;
    if (view->app->ops->hexview_font) status = view->app->ops->hexview_font(view, &copied);
    --view->app->syncing;
    if (status == XXWIDGETS_OK) {
        view->hex_font = copied;
        view->hex_has_font = 1;
    }
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

/* Shared by the modal dialog and the embedded form. */
static xxwidgets_status read_font_controls(const font_row *row, xxwidgets_font *font)
{
    char size_text[32], *end;
    unsigned long points;
    xxwidgets_status status;
    memset(font, 0, sizeof(*font));
    status = xxwidgets_widget_get_text(row->family, font->family, sizeof(font->family), NULL);
    if (status != XXWIDGETS_OK) return status == XXWIDGETS_BUFFER_TOO_SMALL ? XXWIDGETS_INVALID_ARGUMENT : status;
    status = xxwidgets_widget_get_text(row->size, size_text, sizeof(size_text), NULL);
    if (status != XXWIDGETS_OK) return status == XXWIDGETS_BUFFER_TOO_SMALL ? XXWIDGETS_INVALID_ARGUMENT : status;
    points = strtoul(size_text, &end, 10);
    if (end == size_text || *end || points > 96 || (points && points < 4)) return XXWIDGETS_INVALID_ARGUMENT;
    font->point_size = (unsigned int)points;
    status = xxwidgets_widget_get_value(row->bold, &font->bold);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(row->italic, &font->italic);
    if (status != XXWIDGETS_OK) return status;
    return xxwidgets_font_valid(font) ? XXWIDGETS_OK : XXWIDGETS_INVALID_ARGUMENT;
}

static xxwidgets_status preview_font_row(const font_row *row, size_t role, const xxwidgets_font *font)
{
    char text[192];
    xxwidgets_status status;
    snprintf(text, sizeof(text), "AaBb 0123 - %s, %u pt%s%s", font->family[0] ? font->family : "Default family", font->point_size, font->bold ? ", bold" : "",
             font->italic ? ", italic" : "");
    status = xxwidgets_widget_set_text(row->preview, text);
    if (status == XXWIDGETS_OK && row->preview->app->ops->preview_font)
        status = row->preview->app->ops->preview_font(row->preview, (xxwidgets_font_role)role, font);
    return status;
}

static xxwidgets_status fill_font_row(const font_row *row, size_t role, const xxwidgets_font *font)
{
    char size_text[16];
    xxwidgets_status status = xxwidgets_widget_set_text(row->family, font->family);
    snprintf(size_text, sizeof(size_text), "%u", font->point_size);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(row->size, size_text);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(row->bold, font->bold);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(row->italic, font->italic);
    if (status == XXWIDGETS_OK) status = preview_font_row(row, role, font);
    return status;
}

static xxwidgets_status read_row(font_state *state, size_t role, xxwidgets_font *font)
{
    return read_font_controls(&state->rows[role], font);
}

static xxwidgets_status preview_row(font_state *state, size_t role, const xxwidgets_font *font)
{
    return preview_font_row(&state->rows[role], role, font);
}

static xxwidgets_status fill_row(font_state *state, size_t role)
{
    return fill_font_row(&state->rows[role], role, &state->options.fonts[role]);
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
    static const char *roles[] = {XXW_N_("Controls"), XXW_N_("Table views"), XXW_N_("Tree views"), XXW_N_("Text edits")};
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
        status = xxwidgets_widget_create(app, state.window, kind, XXW_TR(app, text), (xxwidgets_rect){x, y, width, 1}, slot); \
        if (status != XXWIDGETS_OK) goto finish;                                                                 \
    } while (0)
    for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role) {
        xxwidgets_widget *label;
        int y = 1 + (int)role * 4;
        FONT_CONTROL(&label, XXWIDGETS_LABEL, roles[role], 2, y, 18);
        FONT_CONTROL(&state.rows[role].family, XXWIDGETS_EDIT, "", 22, y, 32);
        FONT_CONTROL(&state.rows[role].size, XXWIDGETS_EDIT, "0", 56, y, 6);
        FONT_CONTROL(&state.rows[role].bold, XXWIDGETS_CHECKBOX, XXW_N_("Bold"), 64, y, 11);
        FONT_CONTROL(&state.rows[role].italic, XXWIDGETS_CHECKBOX, XXW_N_("Italic"), 76, y, 11);
        FONT_CONTROL(&state.rows[role].choose, XXWIDGETS_BUTTON, XXW_N_("Choose"), 88, y, 10);
        FONT_CONTROL(&state.rows[role].reset, XXWIDGETS_BUTTON, XXW_N_("Default"), 88, y + 2, 10);
        FONT_CONTROL(&state.rows[role].preview, XXWIDGETS_LABEL, "", 22, y + 2, 64);
        status = fill_row(&state, role);
        if (status != XXWIDGETS_OK) goto finish;
    }
    FONT_CONTROL(&state.error, XXWIDGETS_LABEL, "", 2, 18, 96);
    FONT_CONTROL(&state.preview, XXWIDGETS_BUTTON, XXW_N_("Preview"), 2, 20, 14);
    FONT_CONTROL(&state.ok, XXWIDGETS_BUTTON, XXW_N_("OK"), 72, 20, 10);
    FONT_CONTROL(&state.cancel, XXWIDGETS_BUTTON, XXW_N_("Cancel"), 84, 20, 14);
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

struct xxwidgets_font_options_widget {
    xxwidgets_widget *owner;
    xxwidgets_widget *controls[XXWIDGETS_FONT_ROLE_COUNT][XXWIDGETS_FONT_OPTIONS_CONTROL_COUNT];
    int pending_choose;        /* Role, or -1. */
    unsigned int dirty_rows;   /* One bit per role whose preview needs refreshing. */
};

static font_row widget_row(const xxwidgets_font_options_widget *widget, size_t role)
{
    font_row row = {0};
    row.family = widget->controls[role][XXWIDGETS_FONT_OPTIONS_FAMILY];
    row.size = widget->controls[role][XXWIDGETS_FONT_OPTIONS_SIZE];
    row.bold = widget->controls[role][XXWIDGETS_FONT_OPTIONS_BOLD];
    row.italic = widget->controls[role][XXWIDGETS_FONT_OPTIONS_ITALIC];
    row.choose = widget->controls[role][XXWIDGETS_FONT_OPTIONS_CHOOSE];
    row.preview = widget->controls[role][XXWIDGETS_FONT_OPTIONS_PREVIEW];
    return row;
}

static int font_widget_bounds_valid(xxwidgets_rect bounds)
{
    return bounds.x >= 0 && bounds.y >= 0 && bounds.width >= 56 && bounds.height >= 15 &&
        bounds.x <= 32767 - bounds.width && bounds.y <= 32767 - bounds.height;
}

static void font_widget_rects(xxwidgets_rect bounds, size_t role, xxwidgets_rect *rects)
{
    int x = bounds.x, y = bounds.y + (int)role * 4, w = bounds.width;
    rects[XXWIDGETS_FONT_OPTIONS_LABEL] = (xxwidgets_rect){x, y, w, 1};
    rects[XXWIDGETS_FONT_OPTIONS_FAMILY] = (xxwidgets_rect){x, y + 1, w - 37, 1};
    rects[XXWIDGETS_FONT_OPTIONS_SIZE] = (xxwidgets_rect){x + w - 36, y + 1, 5, 1};
    rects[XXWIDGETS_FONT_OPTIONS_BOLD] = (xxwidgets_rect){x + w - 30, y + 1, 9, 1};
    rects[XXWIDGETS_FONT_OPTIONS_ITALIC] = (xxwidgets_rect){x + w - 20, y + 1, 9, 1};
    rects[XXWIDGETS_FONT_OPTIONS_CHOOSE] = (xxwidgets_rect){x + w - 10, y + 1, 10, 1};
    rects[XXWIDGETS_FONT_OPTIONS_PREVIEW] = (xxwidgets_rect){x, y + 2, w, 1};
}

xxwidgets_status xxwidgets_font_options_widget_create(xxwidgets_widget *owner, xxwidgets_rect bounds,
                                                      xxwidgets_font_options_widget **out_widget)
{
    static const char *const roles[XXWIDGETS_FONT_ROLE_COUNT] = {XXW_N_("Controls"), XXW_N_("Table views"), XXW_N_("Tree views"), XXW_N_("Text edits")};
    static const xxwidgets_kind kinds[XXWIDGETS_FONT_OPTIONS_CONTROL_COUNT] = {
        XXWIDGETS_LABEL, XXWIDGETS_EDIT, XXWIDGETS_EDIT, XXWIDGETS_CHECKBOX, XXWIDGETS_CHECKBOX, XXWIDGETS_BUTTON, XXWIDGETS_LABEL};
    static const char *const texts[XXWIDGETS_FONT_OPTIONS_CONTROL_COUNT] = {"", "", "0", XXW_N_("Bold"), XXW_N_("Italic"), XXW_N_("Choose"), ""};
    xxwidgets_font_options_widget *widget;
    xxwidgets_font_options defaults;
    xxwidgets_status status = XXWIDGETS_OK;
    size_t role, i;
    if (!out_widget) return XXWIDGETS_INVALID_ARGUMENT;
    *out_widget = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !font_widget_bounds_valid(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing) return XXWIDGETS_BUSY;
    widget = (xxwidgets_font_options_widget *)calloc(1, sizeof(*widget));
    if (!widget) return XXWIDGETS_OUT_OF_MEMORY;
    widget->owner = owner;
    widget->pending_choose = -1;
    for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT && status == XXWIDGETS_OK; ++role) {
        xxwidgets_rect rects[XXWIDGETS_FONT_OPTIONS_CONTROL_COUNT];
        font_widget_rects(bounds, role, rects);
        for (i = 0; i < XXWIDGETS_FONT_OPTIONS_CONTROL_COUNT && status == XXWIDGETS_OK; ++i) {
            const char *text = i == XXWIDGETS_FONT_OPTIONS_LABEL ? roles[role] : texts[i];
            if (kinds[i] != XXWIDGETS_EDIT && text[0]) text = XXW_TR(owner->app, text);
            status = xxwidgets_widget_create(owner->app, owner, kinds[i], text, rects[i], &widget->controls[role][i]);
        }
    }
    if (status == XXWIDGETS_OK) status = xxwidgets_font_options_init(&defaults);
    if (status == XXWIDGETS_OK) status = xxwidgets_font_options_widget_set(widget, &defaults);
    if (status != XXWIDGETS_OK) {
        xxwidgets_font_options_widget_destroy(widget);
        return status;
    }
    *out_widget = widget;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_font_options_widget_destroy(xxwidgets_font_options_widget *widget)
{
    size_t role, i;
    if (!widget) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->owner->app->dispatch_depth || widget->owner->app->polling || widget->owner->app->syncing) return XXWIDGETS_BUSY;
    for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role)
        for (i = 0; i < XXWIDGETS_FONT_OPTIONS_CONTROL_COUNT; ++i) {
            xxwidgets_status status;
            if (!widget->controls[role][i]) continue;
            status = xxwidgets_widget_destroy(widget->controls[role][i]);
            if (status != XXWIDGETS_OK) return status;
            widget->controls[role][i] = NULL;
        }
    free(widget);
    return XXWIDGETS_OK;
}

xxwidgets_widget *xxwidgets_font_options_widget_control(const xxwidgets_font_options_widget *widget, xxwidgets_font_role role,
                                                        xxwidgets_font_options_control_id control)
{
    if (!widget || role < 0 || role >= XXWIDGETS_FONT_ROLE_COUNT || control < 0 || control >= XXWIDGETS_FONT_OPTIONS_CONTROL_COUNT) return NULL;
    return widget->controls[role][control];
}

xxwidgets_status xxwidgets_font_options_widget_get(const xxwidgets_font_options_widget *widget, xxwidgets_font_options *options)
{
    xxwidgets_font_options result;
    size_t role;
    if (!widget || !options) return XXWIDGETS_INVALID_ARGUMENT;
    for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role) {
        font_row row = widget_row(widget, role);
        xxwidgets_status status = read_font_controls(&row, &result.fonts[role]);
        if (status != XXWIDGETS_OK) return status;
    }
    *options = result;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_font_options_widget_set(xxwidgets_font_options_widget *widget, const xxwidgets_font_options *options)
{
    xxwidgets_status status = XXWIDGETS_OK;
    size_t role;
    if (!widget || !valid_options(options)) return XXWIDGETS_INVALID_ARGUMENT;
    for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT && status == XXWIDGETS_OK; ++role) {
        font_row row = widget_row(widget, role);
        status = fill_font_row(&row, role, &options->fonts[role]);
    }
    if (status == XXWIDGETS_OK) widget->dirty_rows = 0;
    return status;
}

xxwidgets_status xxwidgets_font_options_widget_set_rect(xxwidgets_font_options_widget *widget, xxwidgets_rect bounds)
{
    xxwidgets_rect previous[XXWIDGETS_FONT_ROLE_COUNT][XXWIDGETS_FONT_OPTIONS_CONTROL_COUNT];
    size_t role, i;
    if (!widget || !font_widget_bounds_valid(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role)
        for (i = 0; i < XXWIDGETS_FONT_OPTIONS_CONTROL_COUNT; ++i) previous[role][i] = widget->controls[role][i]->rect;
    for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role) {
        xxwidgets_rect rects[XXWIDGETS_FONT_OPTIONS_CONTROL_COUNT];
        font_widget_rects(bounds, role, rects);
        for (i = 0; i < XXWIDGETS_FONT_OPTIONS_CONTROL_COUNT; ++i) {
            xxwidgets_status status = xxwidgets_widget_set_rect(widget->controls[role][i], rects[i]);
            if (status != XXWIDGETS_OK) {
                size_t r, j;
                for (r = 0; r < XXWIDGETS_FONT_ROLE_COUNT; ++r)
                    for (j = 0; j < XXWIDGETS_FONT_OPTIONS_CONTROL_COUNT; ++j)
                        (void)xxwidgets_widget_set_rect(widget->controls[r][j], previous[r][j]);
                return status;
            }
        }
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_font_options_widget_handle_event(xxwidgets_font_options_widget *widget, const xxwidgets_event *event)
{
    size_t role;
    if (!widget || !event) return XXWIDGETS_INVALID_ARGUMENT;
    for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role) {
        xxwidgets_widget *const *row = widget->controls[role];
        if (event->type == XXWIDGETS_EVENT_CLICK && event->widget == row[XXWIDGETS_FONT_OPTIONS_CHOOSE]) widget->pending_choose = (int)role;
        else if (event->type == XXWIDGETS_EVENT_CHANGE &&
                 (event->widget == row[XXWIDGETS_FONT_OPTIONS_FAMILY] || event->widget == row[XXWIDGETS_FONT_OPTIONS_SIZE] ||
                  event->widget == row[XXWIDGETS_FONT_OPTIONS_BOLD] || event->widget == row[XXWIDGETS_FONT_OPTIONS_ITALIC]))
            widget->dirty_rows |= 1u << role;
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_font_options_widget_process(xxwidgets_font_options_widget *widget)
{
    xxwidgets_status status = XXWIDGETS_OK;
    size_t role;
    if (!widget) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->owner->app->dispatch_depth || widget->owner->app->polling || widget->owner->app->syncing) return XXWIDGETS_BUSY;
    for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT && status == XXWIDGETS_OK; ++role) {
        font_row row;
        xxwidgets_font font;
        if (!(widget->dirty_rows & (1u << role))) continue;
        widget->dirty_rows &= ~(1u << role);
        row = widget_row(widget, role);
        /* Incomplete edits keep the last valid preview. */
        if (read_font_controls(&row, &font) == XXWIDGETS_OK) status = preview_font_row(&row, role, &font);
    }
    if (status == XXWIDGETS_OK && widget->pending_choose >= 0) {
        size_t chosen_role = (size_t)widget->pending_choose;
        font_row row = widget_row(widget, chosen_role);
        xxwidgets_font font;
        int chosen = 0;
        widget->pending_choose = -1;
        if (read_font_controls(&row, &font) != XXWIDGETS_OK) memset(&font, 0, sizeof(font));
        status = xxwidgets_font_choose_dialog(widget->owner, (xxwidgets_font_role)chosen_role, &font, &chosen);
        if (status == XXWIDGETS_UNAVAILABLE) status = xxwidgets_widget_focus(row.family);
        else if (status == XXWIDGETS_OK && chosen) status = fill_font_row(&row, chosen_role, &font);
    }
    return status;
}
