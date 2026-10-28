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
#include "xxwidgets/xxwidgets_options_pages.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The dialog stacks every page in one window, since windows cannot be nested:
 * each page owns the controls its create callback made, and only the selected
 * page's controls are visible. */
typedef struct options_slot {
    const xxwidgets_options_page *page;
    void *state;
    int created;
    xxwidgets_widget **controls;
    unsigned char *own_visible; /* Visibility the page chose for each control. */
    size_t count;
} options_slot;

typedef struct options_state {
    xxwidgets_widget *window, *list, *error, *defaults, *ok, *cancel;
    options_slot slots[XXWIDGETS_OPTIONS_PAGES_MAX];
    size_t count, current;
    int done, accepted, pending_ok, pending_defaults, pending_select;
} options_state;

static int page_valid(const xxwidgets_options_page *page)
{
    return page->title && xxwidgets_valid_utf8(page->title) && page->create && page->reset && page->apply && page->destroy &&
        page->width >= 1 && page->width <= 200 && page->height >= 1 && page->height <= 60;
}

static size_t utf8_length(const char *text)
{
    size_t length = 0;
    for (; *text; ++text)
        if (((unsigned char)*text & 0xC0) != 0x80) ++length;
    return length;
}

static int slot_owns(const options_slot *slot, const xxwidgets_widget *widget)
{
    size_t i;
    for (i = 0; i < slot->count; ++i)
        if (slot->controls[i] == widget) return 1;
    return 0;
}

static void options_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    options_state *state = (options_state *)user;
    size_t i;
    (void)app;
    if ((event->type == XXWIDGETS_EVENT_CLOSE && event->widget == state->window) ||
        (event->type == XXWIDGETS_EVENT_SHORTCUT && event->widget == state->window && event->value == 1) ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == state->cancel)) {
        state->done = 1;
        return;
    }
    if (event->type == XXWIDGETS_EVENT_CLICK && event->widget == state->ok) {
        state->pending_ok = 1;
        return;
    }
    if (event->type == XXWIDGETS_EVENT_CLICK && event->widget == state->defaults) {
        state->pending_defaults = 1;
        return;
    }
    if (event->widget == state->list) {
        if (event->type == XXWIDGETS_EVENT_SELECT && event->value >= 0 && (size_t)event->value < state->count) state->pending_select = event->value;
        return;
    }
    for (i = 0; i < state->count; ++i)
        if (state->slots[i].created && slot_owns(&state->slots[i], event->widget)) {
            if (state->slots[i].page->event) state->slots[i].page->event(state->slots[i].state, event);
            return;
        }
}

/* Hides a page, remembering which of its controls the page itself shows. */
static xxwidgets_status hide_slot(options_slot *slot)
{
    size_t i;
    for (i = 0; i < slot->count; ++i) {
        if (!slot->controls[i]->visible) continue;
        slot->own_visible[i] = 1;
        xxwidgets_status status = xxwidgets_widget_set_visible(slot->controls[i], 0);
        if (status != XXWIDGETS_OK) return status;
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status show_slot(options_slot *slot)
{
    size_t i;
    for (i = 0; i < slot->count; ++i) {
        if (!slot->own_visible[i] || slot->controls[i]->visible) continue;
        xxwidgets_status status = xxwidgets_widget_set_visible(slot->controls[i], 1);
        if (status != XXWIDGETS_OK) return status;
    }
    return XXWIDGETS_OK;
}

/* Callbacks may change visibility while their page is hidden: keep it hidden. */
static xxwidgets_status conceal_hidden(options_state *state)
{
    size_t i;
    xxwidgets_status status = XXWIDGETS_OK;
    for (i = 0; i < state->count && status == XXWIDGETS_OK; ++i)
        if (i != state->current) status = hide_slot(&state->slots[i]);
    return status;
}

static xxwidgets_status select_page(options_state *state, size_t index)
{
    xxwidgets_status status = XXWIDGETS_OK;
    if (index != state->current) {
        options_slot *old = &state->slots[state->current];
        size_t i;
        /* Record the old page's own choices before hiding it. */
        for (i = 0; i < old->count; ++i) old->own_visible[i] = (unsigned char)old->controls[i]->visible;
        status = hide_slot(old);
        if (status == XXWIDGETS_OK) status = show_slot(&state->slots[index]);
        if (status == XXWIDGETS_OK) state->current = index;
    }
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(state->list, (int)state->current);
    return status;
}

static xxwidgets_status create_slot(options_state *state, options_slot *slot, xxwidgets_rect bounds)
{
    xxwidgets_app *app = state->window->app;
    xxwidgets_widget *last = NULL, *widget;
    xxwidgets_status status;
    size_t count = 0;
    for (widget = app->widgets; widget; widget = widget->next) last = widget;
    status = slot->page->create(state->window, bounds, slot->page->user, &slot->state);
    if (status != XXWIDGETS_OK) return status;
    slot->created = 1;
    for (widget = last ? last->next : app->widgets; widget; widget = widget->next)
        if (widget->parent == state->window) ++count;
    if (!count) return XXWIDGETS_OK;
    slot->controls = (xxwidgets_widget **)calloc(count, sizeof(*slot->controls));
    slot->own_visible = (unsigned char *)calloc(count, 1);
    if (!slot->controls || !slot->own_visible) return XXWIDGETS_OUT_OF_MEMORY;
    for (widget = last ? last->next : app->widgets; widget; widget = widget->next)
        if (widget->parent == state->window) {
            slot->own_visible[slot->count] = (unsigned char)widget->visible;
            slot->controls[slot->count++] = widget;
        }
    return XXWIDGETS_OK;
}

static void set_error(options_state *state, const char *text)
{
    (void)xxwidgets_widget_set_text(state->error, text && text[0] && xxwidgets_valid_utf8(text) ? text : "");
}

static xxwidgets_status reset_pages(options_state *state)
{
    xxwidgets_status status = XXWIDGETS_OK;
    size_t i;
    for (i = 0; i < state->count && status == XXWIDGETS_OK; ++i) status = state->slots[i].page->reset(state->slots[i].state);
    if (status == XXWIDGETS_OK) set_error(state, "");
    return status;
}

/* Validates every page, then applies them in order. Returns OK when the
 * dialog stays open on a rejected page; done/accepted mark success. */
static xxwidgets_status commit_pages(options_state *state)
{
    char error[512];
    size_t i;
    for (i = 0; i < state->count; ++i) {
        const xxwidgets_options_page *page = state->slots[i].page;
        error[0] = 0;
        if (page->validate && !page->validate(state->slots[i].state, error, sizeof(error))) goto rejected;
    }
    for (i = 0; i < state->count; ++i) {
        const xxwidgets_options_page *page = state->slots[i].page;
        error[0] = 0;
        if (!page->apply(state->slots[i].state, error, sizeof(error))) goto rejected;
    }
    state->accepted = state->done = 1;
    return XXWIDGETS_OK;
rejected:
    error[sizeof(error) - 1] = 0;
    set_error(state, error[0] ? error : XXW_TR(state->window->app, "Cannot apply these settings."));
    return select_page(state, i);
}

xxwidgets_status xxwidgets_options_pages_dialog(xxwidgets_widget *owner, const char *title, const xxwidgets_options_page *pages,
                                                size_t count, size_t *page, int *accepted)
{
    const xxwidgets_shortcut shortcuts[] = {{"Esc", 1}};
    options_state state = {0};
    xxwidgets_app *app;
    xxwidgets_widget *focus = NULL, *widget, *previous_default, *previous_modal;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    xxwidgets_status status, cleanup;
    int list_width = 14, page_width = 1, page_height = 10, width, height, button_y, create_hidden;
    size_t i, first = page ? *page : 0;
    if (accepted) *accepted = 0;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !title || !xxwidgets_valid_utf8(title) || !pages || !count ||
        count > XXWIDGETS_OPTIONS_PAGES_MAX || first >= count || !accepted)
        return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < count; ++i) {
        size_t length;
        if (!page_valid(&pages[i])) return XXWIDGETS_INVALID_ARGUMENT;
        length = utf8_length(pages[i].title);
        if (length + 4 > (size_t)list_width) list_width = length + 4 > 30 ? 30 : (int)length + 4;
        if (pages[i].width > page_width) page_width = pages[i].width;
        if (pages[i].height > page_height) page_height = pages[i].height;
    }
    width = list_width + page_width + 6;
    if (width < 52) {
        page_width += 52 - width;
        width = 52;
    }
    button_y = page_height + 3;
    height = button_y + 3;
    if (owner->rect.x > INT_MAX - width - 2 || owner->rect.y > INT_MAX - height - 2) return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->quit ||
        (app->modal_window && app->modal_window != owner)) return XXWIDGETS_BUSY;
    state.count = count;
    state.current = first;
    state.pending_select = -1;
    for (i = 0; i < count; ++i) state.slots[i].page = &pages[i];
    previous_event = app->on_event;
    previous_user = app->user_data;
    previous_default = app->modal_default;
    previous_modal = app->modal_window;
    for (widget = app->widgets; widget; widget = widget->next)
        if (widget->parent == owner && xxwidgets_focusable(widget)) {
            if (!focus || xxwidgets_widget_has_focus(widget) == 1) focus = widget;
        }
    app->on_event = options_event;
    app->user_data = &state;
    app->modal_default = NULL;
    /* Build all pages and attach the owner before the first native show. */
    create_hidden = app->create_hidden;
    app->create_hidden = 1;
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title, (xxwidgets_rect){owner->rect.x + 2, owner->rect.y + 2, width, height}, &state.window);
    app->create_hidden = create_hidden;
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_window = state.window;
    status = xxwidgets_window_set_minimum_size(state.window, width, height);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, state.window, XXWIDGETS_LISTBOX, "", (xxwidgets_rect){2, 1, list_width, page_height}, &state.list);
    for (i = 0; i < count && status == XXWIDGETS_OK; ++i) status = xxwidgets_listbox_add(state.list, pages[i].title);
    for (i = 0; i < count && status == XXWIDGETS_OK; ++i)
        status = create_slot(&state, &state.slots[i], (xxwidgets_rect){list_width + 4, 1, page_width, page_height});
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, state.window, XXWIDGETS_LABEL, "", (xxwidgets_rect){list_width + 4, page_height + 1, page_width, 1}, &state.error);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON, XXW_TR(app, "Default"), (xxwidgets_rect){2, button_y, 12, 2}, &state.defaults);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON, XXW_TR(app, "OK"), (xxwidgets_rect){width - 28, button_y, 12, 2}, &state.ok);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON, XXW_TR(app, "Cancel"), (xxwidgets_rect){width - 14, button_y, 12, 2}, &state.cancel);
    if (status == XXWIDGETS_OK) status = xxwidgets_window_set_shortcuts(state.window, shortcuts, 1);
    if (status == XXWIDGETS_OK) status = conceal_hidden(&state);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(state.list, (int)first);
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_default = state.ok;
    status = xxwidgets_window_set_modality(state.window, owner, XXWIDGETS_APPLICATION_MODAL);
    if (status != XXWIDGETS_OK) goto finish;
    if (!create_hidden) {
        status = xxwidgets_widget_set_visible(state.window, 1);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_focus(state.list);
    }
    while (status == XXWIDGETS_OK && !state.done && !app->quit) {
        status = xxwidgets_app_poll(app, 30);
        if (status != XXWIDGETS_OK || state.done || app->quit) break;
        if (state.pending_select >= 0) {
            size_t selected = (size_t)state.pending_select;
            state.pending_select = -1;
            status = select_page(&state, selected);
        }
        for (i = 0; i < count && status == XXWIDGETS_OK; ++i)
            if (pages[i].process) status = pages[i].process(state.slots[i].state);
        if (status == XXWIDGETS_OK && state.pending_defaults) {
            state.pending_defaults = 0;
            status = reset_pages(&state);
        }
        if (status == XXWIDGETS_OK && state.pending_ok) {
            state.pending_ok = 0;
            status = commit_pages(&state);
        }
        if (status == XXWIDGETS_OK && !state.done) status = conceal_hidden(&state);
    }
    if (app->quit) state.accepted = 0;
finish:
    /* Do not paint partially dismantled pages while closing the dialog. */
    if (state.window) {
        cleanup = xxwidgets_widget_set_visible(state.window, 0);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (state.window) {
        cleanup = xxwidgets_window_set_modality(state.window, NULL, XXWIDGETS_MODELESS);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    for (i = 0; i < count; ++i) {
        if (state.slots[i].created) {
            cleanup = pages[i].destroy(state.slots[i].state);
            if (status == XXWIDGETS_OK) status = cleanup;
        }
        free(state.slots[i].controls);
        free(state.slots[i].own_visible);
    }
    if (state.window) {
        cleanup = xxwidgets_widget_destroy(state.window);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    app->modal_window = previous_modal;
    app->modal_default = previous_default;
    app->on_event = previous_event;
    app->user_data = previous_user;
    if (focus) (void)xxwidgets_widget_focus(focus);
    if (page) *page = state.current;
    if (status == XXWIDGETS_OK && state.accepted) *accepted = 1;
    return status;
}

static void copy_error(char *error, size_t capacity, const char *text)
{
    if (!error || !capacity) return;
    snprintf(error, capacity, "%s", text);
}

/* Fonts page. */
typedef struct font_page {
    xxwidgets_font_options_widget *form;
    xxwidgets_font_options *options;
    xxwidgets_app *app;
} font_page;

static xxwidgets_status font_page_create(xxwidgets_widget *dialog, xxwidgets_rect bounds, void *user, void **out)
{
    font_page *page = (font_page *)calloc(1, sizeof(*page));
    xxwidgets_status status;
    if (!page) return XXWIDGETS_OUT_OF_MEMORY;
    page->options = (xxwidgets_font_options *)user;
    page->app = dialog->app;
    status = xxwidgets_font_options_widget_create(dialog, bounds, &page->form);
    if (status == XXWIDGETS_OK) status = xxwidgets_font_options_widget_set(page->form, page->options);
    if (status != XXWIDGETS_OK) {
        if (page->form) (void)xxwidgets_font_options_widget_destroy(page->form);
        free(page);
        return status;
    }
    *out = page;
    return XXWIDGETS_OK;
}

static void font_page_event(void *state, const xxwidgets_event *event)
{
    (void)xxwidgets_font_options_widget_handle_event(((font_page *)state)->form, event);
}

static xxwidgets_status font_page_process(void *state)
{
    return xxwidgets_font_options_widget_process(((font_page *)state)->form);
}

static xxwidgets_status font_page_reset(void *state)
{
    xxwidgets_font_options defaults;
    xxwidgets_status status = xxwidgets_font_options_init(&defaults);
    return status == XXWIDGETS_OK ? xxwidgets_font_options_widget_set(((font_page *)state)->form, &defaults) : status;
}

static int font_page_validate(void *state, char *error, size_t capacity)
{
    font_page *page = (font_page *)state;
    xxwidgets_font_options options;
    if (xxwidgets_font_options_widget_get(page->form, &options) == XXWIDGETS_OK) return 1;
    copy_error(error, capacity, XXW_TR(page->app, "Enter a font family and size 4..96; 0 uses the default size."));
    return 0;
}

static int font_page_apply(void *state, char *error, size_t capacity)
{
    font_page *page = (font_page *)state;
    return font_page_validate(state, error, capacity) && xxwidgets_font_options_widget_get(page->form, page->options) == XXWIDGETS_OK;
}

static xxwidgets_status font_page_destroy(void *state)
{
    font_page *page = (font_page *)state;
    xxwidgets_status status = xxwidgets_font_options_widget_destroy(page->form);
    free(page);
    return status;
}

xxwidgets_status xxwidgets_options_page_fonts(xxwidgets_options_page *page, const char *title, xxwidgets_font_options *options)
{
    if (!page || !title || !options) return XXWIDGETS_INVALID_ARGUMENT;
    memset(page, 0, sizeof(*page));
    page->title = title;
    page->width = 60;
    page->height = 23;
    page->create = font_page_create;
    page->event = font_page_event;
    page->process = font_page_process;
    page->reset = font_page_reset;
    page->validate = font_page_validate;
    page->apply = font_page_apply;
    page->destroy = font_page_destroy;
    page->user = options;
    return XXWIDGETS_OK;
}

/* Optimization page. */
typedef struct optimization_page {
    xxwidgets_optimization_options_widget *form;
    xxwidgets_optimization_page_values *values;
} optimization_page;

static xxwidgets_status optimization_page_create(xxwidgets_widget *dialog, xxwidgets_rect bounds, void *user, void **out)
{
    optimization_page *page = (optimization_page *)calloc(1, sizeof(*page));
    xxwidgets_status status;
    if (!page) return XXWIDGETS_OUT_OF_MEMORY;
    page->values = (xxwidgets_optimization_page_values *)user;
    status = xxwidgets_optimization_options_widget_create(dialog, bounds, &page->values->capabilities, &page->form);
    if (status == XXWIDGETS_OK) status = xxwidgets_optimization_options_widget_set(page->form, &page->values->options);
    if (status != XXWIDGETS_OK) {
        if (page->form) (void)xxwidgets_optimization_options_widget_destroy(page->form);
        free(page);
        return status;
    }
    *out = page;
    return XXWIDGETS_OK;
}

static xxwidgets_status optimization_page_reset(void *state)
{
    xxwidgets_optimization_options defaults;
    xxwidgets_status status = xxwidgets_optimization_options_init(&defaults);
    return status == XXWIDGETS_OK ? xxwidgets_optimization_options_widget_set(((optimization_page *)state)->form, &defaults) : status;
}

static int optimization_page_apply(void *state, char *error, size_t capacity)
{
    optimization_page *page = (optimization_page *)state;
    if (xxwidgets_optimization_options_widget_get(page->form, &page->values->options) == XXWIDGETS_OK) return 1;
    copy_error(error, capacity, "Cannot read the optimization settings.");
    return 0;
}

static xxwidgets_status optimization_page_destroy(void *state)
{
    optimization_page *page = (optimization_page *)state;
    xxwidgets_status status = xxwidgets_optimization_options_widget_destroy(page->form);
    free(page);
    return status;
}

xxwidgets_status xxwidgets_options_page_optimization(xxwidgets_options_page *page, const char *title, xxwidgets_optimization_page_values *values)
{
    if (!page || !title || !values) return XXWIDGETS_INVALID_ARGUMENT;
    memset(page, 0, sizeof(*page));
    page->title = title;
    page->width = 60;
    /* The embedded form needs 12 rows on targets that show both families. */
    page->height = 12;
    page->create = optimization_page_create;
    page->reset = optimization_page_reset;
    page->apply = optimization_page_apply;
    page->destroy = optimization_page_destroy;
    page->user = values;
    return XXWIDGETS_OK;
}

/* HexView page: the embedded form plus a font chooser. */
typedef struct hex_page {
    xxwidgets_hex_options_widget *form;
    xxwidgets_widget *choose, *note;
    xxwidgets_hex_page_values *values;
    int pending_choose;
} hex_page;

static xxwidgets_status hex_page_create(xxwidgets_widget *dialog, xxwidgets_rect bounds, void *user, void **out)
{
    hex_page *page = (hex_page *)calloc(1, sizeof(*page));
    xxwidgets_app *app = dialog->app;
    xxwidgets_status status;
    if (!page) return XXWIDGETS_OUT_OF_MEMORY;
    page->values = (xxwidgets_hex_page_values *)user;
    status = xxwidgets_hex_options_widget_create(dialog, (xxwidgets_rect){bounds.x, bounds.y, bounds.width, 5}, &page->form);
    if (status == XXWIDGETS_OK) status = xxwidgets_hex_options_widget_set(page->form, &page->values->options);
    if (status == XXWIDGETS_OK && !page->values->show_editable)
        status = xxwidgets_widget_set_visible(xxwidgets_hex_options_widget_control(page->form, XXWIDGETS_HEX_OPTIONS_EDITABLE), 0);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, dialog, XXWIDGETS_BUTTON, XXW_TR(app, "Choose font"), (xxwidgets_rect){bounds.x + 15, bounds.y + 6, 16, 1},
                                         &page->choose);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, dialog, XXWIDGETS_LABEL,
                                         XXW_TR(app, app->backend == XXWIDGETS_BACKEND_TUI ? "TUI uses the terminal font; this preference is saved for GUI."
                                                                                         : "Applies to Hex views opened after OK."),
                                         (xxwidgets_rect){bounds.x, bounds.y + 8, bounds.width, 1}, &page->note);
    if (status != XXWIDGETS_OK) {
        if (page->note) (void)xxwidgets_widget_destroy(page->note);
        if (page->choose) (void)xxwidgets_widget_destroy(page->choose);
        if (page->form) (void)xxwidgets_hex_options_widget_destroy(page->form);
        free(page);
        return status;
    }
    *out = page;
    return XXWIDGETS_OK;
}

static void hex_page_event(void *state, const xxwidgets_event *event)
{
    hex_page *page = (hex_page *)state;
    if (event->type == XXWIDGETS_EVENT_CLICK && event->widget == page->choose) page->pending_choose = 1;
}

static xxwidgets_status hex_page_process(void *state)
{
    hex_page *page = (hex_page *)state;
    xxwidgets_hex_options options;
    xxwidgets_status status;
    int chosen = 0;
    if (!page->pending_choose) return XXWIDGETS_OK;
    page->pending_choose = 0;
    /* An invalid edit leaves nothing to start the chooser from. */
    if (xxwidgets_hex_options_widget_get(page->form, &options) != XXWIDGETS_OK)
        return xxwidgets_widget_focus(xxwidgets_hex_options_widget_control(page->form, XXWIDGETS_HEX_OPTIONS_FONT_SIZE));
    status = xxwidgets_font_choose_dialog(page->choose->parent, XXWIDGETS_FONT_TABLE_VIEWS, &options.font, &chosen);
    if (status == XXWIDGETS_UNAVAILABLE) return xxwidgets_widget_focus(xxwidgets_hex_options_widget_control(page->form, XXWIDGETS_HEX_OPTIONS_FONT_FAMILY));
    if (status == XXWIDGETS_OK && chosen) status = xxwidgets_hex_options_widget_set(page->form, &options);
    return status;
}

static xxwidgets_status hex_page_reset(void *state)
{
    hex_page *page = (hex_page *)state;
    xxwidgets_hex_options defaults;
    xxwidgets_status status = xxwidgets_hex_options_init(&defaults);
    if (status == XXWIDGETS_OK && !page->values->show_editable) defaults.editable = page->values->options.editable;
    return status == XXWIDGETS_OK ? xxwidgets_hex_options_widget_set(page->form, &defaults) : status;
}

static int hex_page_validate(void *state, char *error, size_t capacity)
{
    hex_page *page = (hex_page *)state;
    xxwidgets_hex_options options;
    if (xxwidgets_hex_options_widget_get(page->form, &options) == XXWIDGETS_OK) return 1;
    copy_error(error, capacity, XXW_TR(page->choose->app, "Font family: up to 127 UTF-8 bytes. Size: 4..96, or 0 for automatic."));
    return 0;
}

static int hex_page_apply(void *state, char *error, size_t capacity)
{
    hex_page *page = (hex_page *)state;
    xxwidgets_hex_options options;
    if (!hex_page_validate(state, error, capacity) || xxwidgets_hex_options_widget_get(page->form, &options) != XXWIDGETS_OK) return 0;
    if (!page->values->show_editable) options.editable = page->values->options.editable;
    page->values->options = options;
    return 1;
}

static xxwidgets_status hex_page_destroy(void *state)
{
    hex_page *page = (hex_page *)state;
    xxwidgets_status status = xxwidgets_hex_options_widget_destroy(page->form), cleanup;
    cleanup = xxwidgets_widget_destroy(page->choose);
    if (status == XXWIDGETS_OK) status = cleanup;
    cleanup = xxwidgets_widget_destroy(page->note);
    if (status == XXWIDGETS_OK) status = cleanup;
    free(page);
    return status;
}

xxwidgets_status xxwidgets_options_page_hex(xxwidgets_options_page *page, const char *title, xxwidgets_hex_page_values *values)
{
    if (!page || !title || !values || (values->show_editable != 0 && values->show_editable != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    memset(page, 0, sizeof(*page));
    page->title = title;
    page->width = 60;
    page->height = 9;
    page->create = hex_page_create;
    page->event = hex_page_event;
    page->process = hex_page_process;
    page->reset = hex_page_reset;
    page->validate = hex_page_validate;
    page->apply = hex_page_apply;
    page->destroy = hex_page_destroy;
    page->user = values;
    return XXWIDGETS_OK;
}

/* Database folders page. */
typedef struct database_page {
    xxwidgets_database_paths_widget *form;
    xxwidgets_database_paths_page_values *values;
    xxwidgets_app *app;
} database_page;

static xxwidgets_status database_page_create(xxwidgets_widget *dialog, xxwidgets_rect bounds, void *user, void **out)
{
    database_page *page = (database_page *)calloc(1, sizeof(*page));
    xxwidgets_status status;
    if (!page) return XXWIDGETS_OUT_OF_MEMORY;
    page->values = (xxwidgets_database_paths_page_values *)user;
    page->app = dialog->app;
    status = xxwidgets_database_paths_widget_create(dialog, (xxwidgets_rect){bounds.x, bounds.y, bounds.width, 10}, &page->values->callbacks,
                                                    page->values->user, &page->form);
    if (status == XXWIDGETS_OK) status = xxwidgets_database_paths_widget_set(page->form, page->values->paths);
    if (status != XXWIDGETS_OK) {
        if (page->form) (void)xxwidgets_database_paths_widget_destroy(page->form);
        free(page);
        return status;
    }
    *out = page;
    return XXWIDGETS_OK;
}

static void database_page_event(void *state, const xxwidgets_event *event)
{
    (void)xxwidgets_database_paths_widget_handle_event(((database_page *)state)->form, event);
}

static xxwidgets_status database_page_process(void *state)
{
    return xxwidgets_database_paths_widget_process(((database_page *)state)->form);
}

static xxwidgets_status database_page_reset(void *state)
{
    const char *const empty[3] = {"", "", ""};
    return xxwidgets_database_paths_widget_set(((database_page *)state)->form, empty);
}

static int database_page_validate(void *state, char *error, size_t capacity)
{
    database_page *page = (database_page *)state;
    size_t i, size;
    for (i = 0; i < 3; ++i)
        if (xxwidgets_database_paths_widget_get(page->form, i, NULL, 0, &size) != XXWIDGETS_OK) {
            copy_error(error, capacity, XXW_TR(page->app, "Enter a valid UTF-8 path shorter than 16384 bytes."));
            return 0;
        }
    return 1;
}

static int database_page_apply(void *state, char *error, size_t capacity)
{
    database_page *page = (database_page *)state;
    char *paths[3] = {0};
    int applied = 0;
    size_t i, size;
    for (i = 0; i < 3; ++i) {
        if (xxwidgets_database_paths_widget_get(page->form, i, NULL, 0, &size) != XXWIDGETS_OK || !(paths[i] = (char *)malloc(size)) ||
            xxwidgets_database_paths_widget_get(page->form, i, paths[i], size, NULL) != XXWIDGETS_OK)
            break;
    }
    if (i == 3) {
        const char *const borrowed[3] = {paths[0], paths[1], paths[2]};
        if (error && capacity) error[0] = 0;
        applied = page->values->callbacks.apply ? page->values->callbacks.apply(borrowed, error, capacity, page->values->user) : 1;
        if (!applied && error && capacity && !error[0])
            copy_error(error, capacity, XXW_TR(page->app, "Cannot load databases. Check the main folder and try again."));
    } else copy_error(error, capacity, XXW_TR(page->app, "Enter a valid UTF-8 path shorter than 16384 bytes."));
    for (i = 0; i < 3; ++i) free(paths[i]);
    return applied;
}

static xxwidgets_status database_page_destroy(void *state)
{
    database_page *page = (database_page *)state;
    xxwidgets_status status = xxwidgets_database_paths_widget_destroy(page->form);
    free(page);
    return status;
}

xxwidgets_status xxwidgets_options_page_database_paths(xxwidgets_options_page *page, const char *title, xxwidgets_database_paths_page_values *values)
{
    size_t i;
    if (!page || !title || !values) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < 3; ++i)
        if (values->paths[i] && !xxwidgets_valid_utf8(values->paths[i])) return XXWIDGETS_INVALID_ARGUMENT;
    memset(page, 0, sizeof(*page));
    page->title = title;
    page->width = 60;
    page->height = 10;
    page->create = database_page_create;
    page->event = database_page_event;
    page->process = database_page_process;
    page->reset = database_page_reset;
    page->validate = database_page_validate;
    page->apply = database_page_apply;
    page->destroy = database_page_destroy;
    page->user = values;
    return XXWIDGETS_OK;
}

/* Explorer context menu page; its own Apply button is replaced by OK. */
typedef struct context_page {
    xxwidgets_context_options *form;
    const xxwidgets_context_config *config;
    xxwidgets_app *app;
} context_page;

static xxwidgets_status context_page_create(xxwidgets_widget *dialog, xxwidgets_rect bounds, void *user, void **out)
{
    context_page *page = (context_page *)calloc(1, sizeof(*page));
    xxwidgets_status status;
    if (!page) return XXWIDGETS_OUT_OF_MEMORY;
    page->config = (const xxwidgets_context_config *)user;
    page->app = dialog->app;
    status = xxwidgets_context_options_create(dialog, (xxwidgets_rect){bounds.x, bounds.y, bounds.width, 6}, page->config, &page->form);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_visible(xxwidgets_context_options_control(page->form, XXWIDGETS_CONTEXT_OPTIONS_APPLY), 0);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_set_rect(xxwidgets_context_options_control(page->form, XXWIDGETS_CONTEXT_OPTIONS_STATUS),
                                           (xxwidgets_rect){bounds.x, bounds.y + 2, bounds.width, 2});
    if (status != XXWIDGETS_OK) {
        if (page->form) (void)xxwidgets_context_options_destroy(page->form);
        free(page);
        return status;
    }
    *out = page;
    return XXWIDGETS_OK;
}

static xxwidgets_status context_page_reset(void *state)
{
    context_page *page = (context_page *)state;
    return xxwidgets_widget_set_value(xxwidgets_context_options_control(page->form, XXWIDGETS_CONTEXT_OPTIONS_ENABLE), 0);
}

static int context_page_apply(void *state, char *error, size_t capacity)
{
    context_page *page = (context_page *)state;
    int wanted = 0, registered = 0;
    /* Leave the registry untouched when nothing changed. */
    if (xxwidgets_widget_get_value(xxwidgets_context_options_control(page->form, XXWIDGETS_CONTEXT_OPTIONS_ENABLE), &wanted) == XXWIDGETS_OK &&
        xxwidgets_context_is_registered(page->config, &registered) == XXWIDGETS_OK && wanted == registered)
        return 1;
    if (xxwidgets_context_options_apply(page->form) == XXWIDGETS_OK) return 1;
    copy_error(error, capacity, XXW_TR(page->app, "Cannot change the context menu. Check permissions and the application path."));
    return 0;
}

static xxwidgets_status context_page_destroy(void *state)
{
    context_page *page = (context_page *)state;
    xxwidgets_status status = xxwidgets_context_options_destroy(page->form);
    free(page);
    return status;
}

xxwidgets_status xxwidgets_options_page_context(xxwidgets_options_page *page, const char *title, const xxwidgets_context_config *config)
{
    int registered;
    xxwidgets_status status;
    if (!page || !title || !config) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_context_is_registered(config, &registered);
    if (status != XXWIDGETS_OK) return status;
    memset(page, 0, sizeof(*page));
    page->title = title;
    page->width = 40;
    page->height = 6;
    page->create = context_page_create;
    page->reset = context_page_reset;
    page->apply = context_page_apply;
    page->destroy = context_page_destroy;
    page->user = (void *)config;
    return XXWIDGETS_OK;
}
