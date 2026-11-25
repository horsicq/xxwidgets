/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_type_picker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const k_on = "[x]", *const k_off = "[ ]";

const char **xxwidgets_type_picker_cells(const xxwidgets_type_picker_item *items, size_t count, const unsigned char *checked)
{
    const char **cells;
    size_t i;
    if (!items || !checked || count > SIZE_MAX / (4 * sizeof(*cells))) return NULL;
    cells = (const char **)malloc((count ? count : 1) * 4 * sizeof(*cells));
    if (!cells) return NULL;
    for (i = 0; i < count; ++i) {
        cells[i * 4] = checked[i] ? k_on : k_off;
        cells[i * 4 + 1] = items[i].name;
        cells[i * 4 + 2] = items[i].category ? items[i].category : "";
        cells[i * 4 + 3] = items[i].note ? items[i].note : "";
    }
    return cells;
}

typedef struct picker {
    xxwidgets_widget *window, *table, *toggle, *check, *uncheck, *defaults, *ok, *cancel, *summary;
    const xxwidgets_type_picker_item *items;
    size_t count;
    unsigned char *state;
    const unsigned char *initial_defaults;
    int done, accepted, action;
} picker;

enum {
    ACT_NONE,
    ACT_TOGGLE,
    ACT_CHECK,
    ACT_UNCHECK,
    ACT_DEFAULTS
};

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

static int contains(const char *h, const char *n)
{
    size_t i, j, hn = strlen(h), nn = strlen(n);
    if (!nn) return 1;
    for (i = 0; i + nn <= hn; ++i) {
        for (j = 0; j < nn && lower((unsigned char)h[i + j]) == lower((unsigned char)n[j]); ++j) {
        }
        if (j == nn) return 1;
    }
    return 0;
}

/* The table's own rule: a row stays when every non-empty filter is a
 * case-insensitive substring of its cell. */
static int shown(const picker *p, size_t row)
{
    const char *cells[4];
    size_t c;
    cells[0] = p->state[row] ? k_on : k_off;
    cells[1] = p->items[row].name;
    cells[2] = p->items[row].category ? p->items[row].category : "";
    cells[3] = p->items[row].note ? p->items[row].note : "";
    for (c = 0; c < 4; ++c) {
        const char *f = xxwidgets_table_filter(p->table, c);
        if (f && f[0] && !contains(cells[c], f)) return 0;
    }
    return 1;
}

static xxwidgets_status refresh(picker *p, int keep_row)
{
    const char **cells = xxwidgets_type_picker_cells(p->items, p->count, p->state);
    char text[96];
    size_t i, on = 0;
    xxwidgets_status s;
    if (!cells) return XXWIDGETS_OUT_OF_MEMORY;
    s = xxwidgets_table_set_rows(p->table, cells, p->count);
    free(cells);
    if (s == XXWIDGETS_OK && keep_row >= 0 && (size_t)keep_row < p->count) s = xxwidgets_widget_set_value(p->table, keep_row);
    for (i = 0; i < p->count; ++i) on += p->state[i] != 0;
    snprintf(text, sizeof(text), "%zu of %zu selected", on, p->count);
    if (s == XXWIDGETS_OK) s = xxwidgets_widget_set_text(p->summary, text);
    return s;
}

static void on_event(xxwidgets_app *app, const xxwidgets_event *e, void *user)
{
    picker *p = (picker *)user;
    (void)app;
    if ((e->type == XXWIDGETS_EVENT_CLOSE && e->widget == p->window) || (e->type == XXWIDGETS_EVENT_SHORTCUT && e->value == 1) ||
        (e->type == XXWIDGETS_EVENT_CLICK && e->widget == p->cancel)) {
        p->done = 1;
        return;
    }
    if (e->type == XXWIDGETS_EVENT_CLICK && e->widget == p->ok) {
        p->done = 1;
        p->accepted = 1;
        return;
    }
    if ((e->type == XXWIDGETS_EVENT_ACTIVATE && e->widget == p->table) || (e->type == XXWIDGETS_EVENT_CLICK && e->widget == p->toggle) ||
        (e->type == XXWIDGETS_EVENT_SHORTCUT && e->value == 2))
        p->action = ACT_TOGGLE;
    else if (e->type == XXWIDGETS_EVENT_CLICK && e->widget == p->check) p->action = ACT_CHECK;
    else if (e->type == XXWIDGETS_EVENT_CLICK && e->widget == p->uncheck) p->action = ACT_UNCHECK;
    else if (e->type == XXWIDGETS_EVENT_CLICK && e->widget == p->defaults) p->action = ACT_DEFAULTS;
}

static xxwidgets_status apply(picker *p)
{
    int row = -1, action = p->action;
    size_t i;
    xxwidgets_status s = xxwidgets_widget_get_value(p->table, &row);
    p->action = ACT_NONE;
    if (s != XXWIDGETS_OK) return s;
    if (action == ACT_TOGGLE) {
        if (row < 0 || (size_t)row >= p->count) return XXWIDGETS_OK;
        p->state[row] = (unsigned char)!p->state[row];
    } else if (action == ACT_CHECK || action == ACT_UNCHECK) {
        unsigned char *mask = (unsigned char *)malloc(p->count ? p->count : 1);
        if (!mask) return XXWIDGETS_OUT_OF_MEMORY;
        /* decide visibility first: the mark column may itself be filtered */
        for (i = 0; i < p->count; ++i) mask[i] = (unsigned char)shown(p, i);
        for (i = 0; i < p->count; ++i)
            if (mask[i]) p->state[i] = action == ACT_CHECK;
        free(mask);
    } else if (action == ACT_DEFAULTS && p->initial_defaults) {
        for (i = 0; i < p->count; ++i) p->state[i] = p->initial_defaults[i] != 0;
    } else return XXWIDGETS_OK;
    return refresh(p, row);
}

xxwidgets_status xxwidgets_type_picker_dialog(xxwidgets_widget *owner, const char *title, const xxwidgets_type_picker_item *items, size_t count, unsigned char *checked,
                                              const unsigned char *defaults, int *accepted)
{
    picker p;
    xxwidgets_app *app;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    xxwidgets_widget *previous_modal, *previous_default;
    int enabled, disabled = 0, attached = 0, hidden;
    xxwidgets_status status, cleanup;
    const xxwidgets_shortcut shortcuts[] = {{"Esc", 1}, {"F2", 2}};
    static const char *const titles[] = {XXW_N_("On"), XXW_N_("Type"), XXW_N_("Category"), XXW_N_("Presets")};
    const char *translated[4];
    size_t i;
    if (accepted) *accepted = 0;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !items || !checked || !accepted || !xxwidgets_valid_utf8(title)) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < count; ++i)
        if (!items[i].name || !xxwidgets_valid_utf8(items[i].name) || (items[i].category && !xxwidgets_valid_utf8(items[i].category)) ||
            (items[i].note && !xxwidgets_valid_utf8(items[i].note)))
            return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    memset(&p, 0, sizeof(p));
    p.items = items;
    p.count = count;
    p.initial_defaults = defaults;
    p.state = (unsigned char *)malloc(count ? count : 1);
    if (!p.state) return XXWIDGETS_OUT_OF_MEMORY;
    for (i = 0; i < count; ++i) p.state[i] = checked[i] != 0;
    previous_event = app->on_event;
    previous_user = app->user_data;
    previous_modal = app->modal_window;
    previous_default = app->modal_default;
    enabled = owner->enabled;
    for (i = 0; i < 4; ++i) translated[i] = XXW_TR(app, titles[i]);
    app->on_event = on_event;
    app->user_data = &p;
    app->modal_default = NULL;
    hidden = app->create_hidden;
    app->create_hidden = 1;
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title, (xxwidgets_rect){owner->rect.x + 2, owner->rect.y + 2, 100, 36}, &p.window);
    app->create_hidden = hidden;
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_window = p.window;
    if (status == XXWIDGETS_OK) status = xxwidgets_window_set_minimum_size(p.window, 100, 36);
    if (status == XXWIDGETS_OK) status = xxwidgets_table_create(p.window, (xxwidgets_rect){1, 1, 98, 27}, translated, 4, &p.table);
#define MAKE(var, kind, text, x, y, w, h) \
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(app, p.window, kind, text, (xxwidgets_rect){x, y, w, h}, &p.var)
    MAKE(summary, XXWIDGETS_LABEL, "", 1, 29, 40, 1);
    MAKE(toggle, XXWIDGETS_BUTTON, XXW_TR(app, "Toggle"), 1, 31, 12, 2);
    MAKE(check, XXWIDGETS_BUTTON, XXW_TR(app, "Check shown"), 14, 31, 17, 2);
    MAKE(uncheck, XXWIDGETS_BUTTON, XXW_TR(app, "Uncheck shown"), 32, 31, 19, 2);
    MAKE(defaults, XXWIDGETS_BUTTON, XXW_TR(app, "Defaults"), 52, 31, 14, 2);
    MAKE(ok, XXWIDGETS_BUTTON, XXW_TR(app, "OK"), 74, 31, 10, 2);
    MAKE(cancel, XXWIDGETS_BUTTON, XXW_TR(app, "Cancel"), 86, 31, 12, 2);
#undef MAKE
    if (status == XXWIDGETS_OK && !defaults) status = xxwidgets_widget_set_enabled(p.defaults, 0);
    if (status == XXWIDGETS_OK) status = refresh(&p, 0);
    if (status == XXWIDGETS_OK) app->modal_default = p.ok;
    if (status == XXWIDGETS_OK) status = xxwidgets_window_set_shortcuts(p.window, shortcuts, 2);
    if (status == XXWIDGETS_OK) {
        status = xxwidgets_widget_set_enabled(owner, 0);
        if (status == XXWIDGETS_OK) disabled = 1;
    }
    if (status == XXWIDGETS_OK) {
        status = xxwidgets_window_set_modality(p.window, owner, XXWIDGETS_WINDOW_MODAL);
        if (status == XXWIDGETS_OK) attached = 1;
    }
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_visible(p.window, 1);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_focus(p.table);
    while (status == XXWIDGETS_OK && !p.done && !app->quit) {
        status = xxwidgets_app_poll(app, 30);
        if (status == XXWIDGETS_OK && p.action != ACT_NONE) status = apply(&p);
    }
    if (status == XXWIDGETS_OK && p.accepted && !app->quit) {
        for (i = 0; i < count; ++i) checked[i] = p.state[i];
        *accepted = 1;
    }
finish:
    if (p.window) {
        cleanup = xxwidgets_widget_set_visible(p.window, 0);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (attached) {
        cleanup = xxwidgets_window_set_modality(p.window, NULL, XXWIDGETS_MODELESS);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (p.window) {
        cleanup = xxwidgets_widget_destroy(p.window);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    app->modal_window = previous_modal;
    app->modal_default = previous_default;
    app->on_event = previous_event;
    app->user_data = previous_user;
    if (disabled) {
        cleanup = xxwidgets_widget_set_enabled(owner, enabled);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    free(p.state);
    return status;
}
