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
#include "xxwidgets/xxwidgets_shortcuts_dialog.h"

#include <stdlib.h>
#include <string.h>

#define SHORTCUTS_MAX_ENTRIES 4096

typedef struct shortcuts_state {
    xxwidgets_widget *window, *filter, *table, *close;
    const xxwidgets_shortcut_entry *entries;
    size_t count;
    const char **cells; /* 2 * count cell pointers, borrowed from entries */
    xxwidgets_status error;
    int done;
} shortcuts_state;

static int lower_ascii(int ch)
{
    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}

/* Case-insensitive (ASCII) substring test; "" matches everything. */
static int contains(const char *text, const char *needle)
{
    size_t i, j;
    if (!needle[0]) return 1;
    for (i = 0; text[i]; ++i) {
        for (j = 0; needle[j] && text[i + j] && lower_ascii((unsigned char)text[i + j]) == lower_ascii((unsigned char)needle[j]); ++j) {}
        if (!needle[j]) return 1;
    }
    return 0;
}

/* The rows whose name or key contains the filter text. */
static xxwidgets_status fill_rows(shortcuts_state *state)
{
    const char *needle = state->filter->text ? state->filter->text : "";
    size_t i, rows = 0;
    for (i = 0; i < state->count; ++i) {
        const char *name = state->entries[i].name, *key = state->entries[i].key ? state->entries[i].key : "";
        if (!contains(name, needle) && !contains(key, needle)) continue;
        state->cells[rows * 2] = name;
        state->cells[rows * 2 + 1] = key;
        ++rows;
    }
    return xxwidgets_table_set_rows(state->table, state->cells, rows);
}

static void shortcuts_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    shortcuts_state *state = (shortcuts_state *)user;
    (void)app;
    if (event->widget == state->window && event->type == XXWIDGETS_EVENT_CLOSE) state->done = 1;
    else if (event->widget == state->close && event->type == XXWIDGETS_EVENT_CLICK) state->done = 1;
    else if (event->widget == state->filter && event->type == XXWIDGETS_EVENT_CHANGE) {
        xxwidgets_status status = fill_rows(state);
        if (status != XXWIDGETS_OK && state->error == XXWIDGETS_OK) state->error = status;
    }
}

xxwidgets_status xxwidgets_shortcuts_dialog(xxwidgets_widget *owner, const xxwidgets_shortcut_entry *entries, size_t count)
{
    xxwidgets_widget *previous_focus = NULL, *widget, *previous_modal, *previous_default, *label;
    int owner_enabled, owner_disabled = 0, modal_attached = 0;
    xxwidgets_rect bounds;
    xxwidgets_app *app;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    shortcuts_state state;
    xxwidgets_status status = XXWIDGETS_OK, cleanup;
    const char *titles[2];
    size_t i;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || (!entries && count) || count > SHORTCUTS_MAX_ENTRIES) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < count; ++i)
        if (!entries[i].name || !xxwidgets_valid_utf8(entries[i].name) || (entries[i].key && !xxwidgets_valid_utf8(entries[i].key)))
            return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || (app->modal_window && app->modal_window != owner) || app->quit) return XXWIDGETS_BUSY;
    memset(&state, 0, sizeof(state));
    state.entries = entries;
    state.count = count;
    state.cells = (const char **)calloc(count ? count * 2 : 1, sizeof(*state.cells));
    if (!state.cells) return XXWIDGETS_OUT_OF_MEMORY;
    previous_event = app->on_event;
    previous_user = app->user_data;
    previous_modal = app->modal_window;
    previous_default = app->modal_default;
    owner_enabled = owner->enabled;
    for (widget = app->widgets; widget; widget = widget->next)
        if (widget->parent == owner && xxwidgets_focusable(widget)) {
            if (!previous_focus) previous_focus = widget;
            if (xxwidgets_widget_has_focus(widget) == 1) {
                previous_focus = widget;
                break;
            }
        }
    app->on_event = shortcuts_event;
    app->user_data = &state;
    bounds.x = owner->rect.x + 2;
    bounds.y = owner->rect.y + 2;
    bounds.width = 64;
    bounds.height = 22;
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, XXW_TR(app, XXW_N_("Shortcuts")), bounds, &state.window);
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_window = state.window;
    bounds.x = 2;
    bounds.y = 1;
    bounds.width = 10;
    bounds.height = 1;
    status = xxwidgets_widget_create(app, state.window, XXWIDGETS_LABEL, XXW_TR(app, XXW_N_("Filter")), bounds, &label);
    if (status != XXWIDGETS_OK) goto finish;
    bounds.x = 13;
    bounds.width = 49;
    status = xxwidgets_widget_create(app, state.window, XXWIDGETS_EDIT, "", bounds, &state.filter);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_tooltip(state.filter, XXW_TR(app, XXW_N_("Filter")));
    if (status != XXWIDGETS_OK) goto finish;
    titles[0] = XXW_TR(app, XXW_N_("Name"));
    titles[1] = XXW_TR(app, XXW_N_("Shortcut"));
    bounds.x = 2;
    bounds.y = 3;
    bounds.width = 60;
    bounds.height = 15;
    status = xxwidgets_table_create(state.window, bounds, titles, 2, &state.table);
    if (status == XXWIDGETS_OK) status = fill_rows(&state);
    if (status != XXWIDGETS_OK) goto finish;
    bounds.x = 50;
    bounds.y = 19;
    bounds.width = 12;
    bounds.height = 1;
    status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON, XXW_TR(app, XXW_N_("Close")), bounds, &state.close);
    if (status != XXWIDGETS_OK) goto finish;
    /* Enter closes the dialog, as its only button. */
    app->modal_default = state.close;
    status = xxwidgets_widget_set_enabled(owner, 0);
    if (status != XXWIDGETS_OK) goto finish;
    owner_disabled = 1;
    status = xxwidgets_window_set_modality(state.window, owner, XXWIDGETS_WINDOW_MODAL);
    if (status != XXWIDGETS_OK) goto finish;
    modal_attached = 1;
    status = xxwidgets_widget_focus(state.filter);
    while (status == XXWIDGETS_OK && state.error == XXWIDGETS_OK && !state.done && !app->quit) status = xxwidgets_app_poll(app, 30);
    if (status == XXWIDGETS_OK) status = state.error;
finish:
    if (owner_disabled) {
        cleanup = xxwidgets_widget_set_enabled(owner, owner_enabled);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (modal_attached) {
        cleanup = xxwidgets_window_set_modality(state.window, NULL, XXWIDGETS_MODELESS);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (state.window) {
        cleanup = xxwidgets_widget_destroy(state.window);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    app->modal_window = previous_modal;
    app->modal_default = previous_default;
    app->on_event = previous_event;
    app->user_data = previous_user;
    if (previous_focus) xxwidgets_widget_focus(previous_focus);
    free((void *)state.cells);
    return status;
}
