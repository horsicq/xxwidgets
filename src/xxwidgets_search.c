/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_search.h"
#include "xxwidgets/xxwidgets_combobox.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

struct xxwidgets_search_widget {
    xxwidgets_widget *owner;
    xxwidgets_widget *controls[XXWIDGETS_SEARCH_CONTROL_COUNT];
};

static int boolean(int value) { return value == 0 || value == 1; }

xxwidgets_status xxwidgets_search_options_init(xxwidgets_search_options *options)
{
    if (!options) return XXWIDGETS_INVALID_ARGUMENT;
    memset(options, 0, sizeof(*options));
    strcpy(options->encoding, "UTF-8");
    options->wrap = options->case_sensitive = 1;
    options->length = UINT64_MAX;
    options->alignment = 1;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_search_options_validate(const xxwidgets_search_options *options)
{
    if (!options || !memchr(options->pattern, 0, sizeof(options->pattern)) ||
        !memchr(options->encoding, 0, sizeof(options->encoding)) || !options->encoding[0] ||
        !xxwidgets_valid_utf8(options->pattern) || !xxwidgets_valid_utf8(options->encoding) ||
        options->mode < XXWIDGETS_SEARCH_TEXT || options->mode > XXWIDGETS_SEARCH_REGEX ||
        !boolean(options->backward) || !boolean(options->wrap) || !boolean(options->selection_only) ||
        !boolean(options->case_sensitive) || !options->alignment ||
        (options->length != UINT64_MAX && options->length > UINT64_MAX - options->start))
        return XXWIDGETS_INVALID_ARGUMENT;
    return XXWIDGETS_OK;
}

static int valid_bounds(xxwidgets_rect b)
{
    return b.x >= 0 && b.y >= 0 && b.width >= 70 && b.height >= 15 &&
        b.x <= INT_MAX - b.width && b.y <= INT_MAX - b.height;
}

static void rectangles(xxwidgets_rect b, xxwidgets_rect r[XXWIDGETS_SEARCH_CONTROL_COUNT])
{
    int half = (b.width - 2) / 2;
    r[XXWIDGETS_SEARCH_PATTERN_LABEL] = (xxwidgets_rect){b.x, b.y, b.width, 1};
    r[XXWIDGETS_SEARCH_PATTERN] = (xxwidgets_rect){b.x, b.y + 1, b.width, 1};
    r[XXWIDGETS_SEARCH_MODE_LABEL] = (xxwidgets_rect){b.x, b.y + 3, 15, 1};
    r[XXWIDGETS_SEARCH_MODE] = (xxwidgets_rect){b.x + 16, b.y + 3, 18, 1};
    r[XXWIDGETS_SEARCH_ENCODING_LABEL] = (xxwidgets_rect){b.x + 37, b.y + 3, 10, 1};
    r[XXWIDGETS_SEARCH_ENCODING] = (xxwidgets_rect){b.x + 48, b.y + 3, b.width - 48, 1};
    r[XXWIDGETS_SEARCH_BACKWARD] = (xxwidgets_rect){b.x, b.y + 5, half, 1};
    r[XXWIDGETS_SEARCH_WRAP] = (xxwidgets_rect){b.x + half + 2, b.y + 5, b.width - half - 2, 1};
    r[XXWIDGETS_SEARCH_SELECTION_ONLY] = (xxwidgets_rect){b.x, b.y + 7, half, 1};
    r[XXWIDGETS_SEARCH_CASE_SENSITIVE] = (xxwidgets_rect){b.x + half + 2, b.y + 7, b.width - half - 2, 1};
    r[XXWIDGETS_SEARCH_START_LABEL] = (xxwidgets_rect){b.x, b.y + 9, 15, 1};
    r[XXWIDGETS_SEARCH_START] = (xxwidgets_rect){b.x + 16, b.y + 9, 20, 1};
    r[XXWIDGETS_SEARCH_LENGTH_LABEL] = (xxwidgets_rect){b.x + 38, b.y + 9, 7, 1};
    r[XXWIDGETS_SEARCH_LENGTH] = (xxwidgets_rect){b.x + 46, b.y + 9, b.width - 46, 1};
    r[XXWIDGETS_SEARCH_ALIGNMENT_LABEL] = (xxwidgets_rect){b.x, b.y + 11, 15, 1};
    r[XXWIDGETS_SEARCH_ALIGNMENT] = (xxwidgets_rect){b.x + 16, b.y + 11, 20, 1};
    r[XXWIDGETS_SEARCH_HELP] = (xxwidgets_rect){b.x, b.y + 13, b.width, 2};
}

xxwidgets_widget *xxwidgets_search_widget_control(const xxwidgets_search_widget *widget, xxwidgets_search_control_id control)
{
    return widget && control >= 0 && control < XXWIDGETS_SEARCH_CONTROL_COUNT ? widget->controls[control] : NULL;
}

static int numeric(const char *text, uint64_t *value, int allow_blank)
{
    uint64_t result = 0;
    unsigned int base = 10;
    const unsigned char *p = (const unsigned char *)text;
    int digits = 0;
    while (*p == ' ' || *p == '\t') ++p;
    if (!*p && allow_blank) { *value = UINT64_MAX; return 1; }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
    for (; *p && *p != ' ' && *p != '\t'; ++p) {
        unsigned int digit = *p >= '0' && *p <= '9' ? (unsigned int)(*p - '0') :
            *p >= 'a' && *p <= 'f' ? (unsigned int)(*p - 'a' + 10) :
            *p >= 'A' && *p <= 'F' ? (unsigned int)(*p - 'A' + 10) : 16;
        if (digit >= base || result > (UINT64_MAX - digit) / base) return 0;
        result = result * base + digit;
        digits = 1;
    }
    while (*p == ' ' || *p == '\t') ++p;
    if (*p || !digits) return 0;
    *value = result;
    return 1;
}

static xxwidgets_status read_options(const xxwidgets_search_widget *widget, xxwidgets_search_options *options, const char **error)
{
    xxwidgets_search_options selected = {0};
    xxwidgets_status status;
    char text[64];
    int mode;
    size_t i;
    uint64_t *numbers[] = {&selected.start, &selected.length, &selected.alignment};
    int *flags[] = {&selected.backward, &selected.wrap, &selected.selection_only, &selected.case_sensitive};
    static const xxwidgets_search_control_id number_ids[] = {XXWIDGETS_SEARCH_START, XXWIDGETS_SEARCH_LENGTH, XXWIDGETS_SEARCH_ALIGNMENT};
    static const xxwidgets_search_control_id flag_ids[] = {XXWIDGETS_SEARCH_BACKWARD, XXWIDGETS_SEARCH_WRAP, XXWIDGETS_SEARCH_SELECTION_ONLY, XXWIDGETS_SEARCH_CASE_SENSITIVE};
    if (error) *error = XXW_N_("Check the search settings.");
    if (!widget || !options) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_widget_get_text(widget->controls[XXWIDGETS_SEARCH_PATTERN], selected.pattern, sizeof(selected.pattern), NULL);
    if (status == XXWIDGETS_BUFFER_TOO_SMALL && error) *error = XXW_N_("Pattern is too long (maximum 4095 UTF-8 bytes).");
    if (status != XXWIDGETS_OK) return status;
    status = xxwidgets_widget_get_text(widget->controls[XXWIDGETS_SEARCH_ENCODING], selected.encoding, sizeof(selected.encoding), NULL);
    if (status == XXWIDGETS_BUFFER_TOO_SMALL && error) *error = XXW_N_("Encoding name is too long (maximum 63 UTF-8 bytes).");
    if (status != XXWIDGETS_OK) return status;
    status = xxwidgets_widget_get_value(widget->controls[XXWIDGETS_SEARCH_MODE], &mode);
    if (status != XXWIDGETS_OK) return status;
    selected.mode = (xxwidgets_search_mode)mode;
    for (i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i) {
        status = xxwidgets_widget_get_value(widget->controls[flag_ids[i]], flags[i]);
        if (status != XXWIDGETS_OK) return status;
    }
    for (i = 0; i < sizeof(numbers) / sizeof(numbers[0]); ++i) {
        status = xxwidgets_widget_get_text(widget->controls[number_ids[i]], text, sizeof(text), NULL);
        if (status != XXWIDGETS_OK) return status;
        if (!numeric(text, numbers[i], i == 1)) {
            if (error) *error = XXW_N_("Enter offsets, length, and alignment in decimal or 0x hexadecimal.");
            return XXWIDGETS_INVALID_ARGUMENT;
        }
    }
    status = xxwidgets_search_options_validate(&selected);
    if (!selected.alignment && error) *error = XXW_N_("Alignment must be at least 1.");
    else if (selected.length != UINT64_MAX && selected.length > UINT64_MAX - selected.start && error)
        *error = XXW_N_("The search range exceeds the maximum source offset.");
    else if (!selected.encoding[0] && error) *error = XXW_N_("Enter an encoding name, for example UTF-8.");
    if (status == XXWIDGETS_OK) *options = selected;
    return status;
}

xxwidgets_status xxwidgets_search_widget_get(const xxwidgets_search_widget *widget, xxwidgets_search_options *options)
{
    return read_options(widget, options, NULL);
}

static void recover(xxwidgets_widget *control)
{
    ++control->app->syncing;
    control->app->ops->sync(control);
    --control->app->syncing;
}

xxwidgets_status xxwidgets_search_widget_set(xxwidgets_search_widget *widget, const xxwidgets_search_options *options)
{
    static const xxwidgets_search_control_id text_ids[] = {XXWIDGETS_SEARCH_PATTERN, XXWIDGETS_SEARCH_ENCODING, XXWIDGETS_SEARCH_START, XXWIDGETS_SEARCH_LENGTH, XXWIDGETS_SEARCH_ALIGNMENT};
    static const xxwidgets_search_control_id value_ids[] = {XXWIDGETS_SEARCH_MODE, XXWIDGETS_SEARCH_BACKWARD, XXWIDGETS_SEARCH_WRAP, XXWIDGETS_SEARCH_SELECTION_ONLY, XXWIDGETS_SEARCH_CASE_SENSITIVE};
    char *previous_text[5] = {0};
    int previous_value[5], values[5];
    char start[32], length[32], alignment[32];
    const char *texts[5];
    xxwidgets_status status;
    size_t i;
    if (!widget || xxwidgets_search_options_validate(options) != XXWIDGETS_OK) return XXWIDGETS_INVALID_ARGUMENT;
    snprintf(start, sizeof(start), "0x%" PRIX64, options->start);
    if (options->length == UINT64_MAX) length[0] = 0;
    else snprintf(length, sizeof(length), "0x%" PRIX64, options->length);
    snprintf(alignment, sizeof(alignment), "%" PRIu64, options->alignment);
    texts[0] = options->pattern; texts[1] = options->encoding;
    texts[2] = start; texts[3] = length; texts[4] = alignment;
    values[0] = options->mode; values[1] = options->backward; values[2] = options->wrap;
    values[3] = options->selection_only; values[4] = options->case_sensitive;
    for (i = 0; i < 5; ++i) {
        previous_text[i] = xxwidgets_strdup(widget->controls[text_ids[i]]->text);
        if (!previous_text[i]) { status = XXWIDGETS_OUT_OF_MEMORY; goto finish; }
        previous_value[i] = widget->controls[value_ids[i]]->value;
    }
    for (i = 0; i < 5; ++i) {
        status = xxwidgets_widget_set_text(widget->controls[text_ids[i]], texts[i]);
        if (status != XXWIDGETS_OK) goto rollback;
        status = xxwidgets_widget_set_value(widget->controls[value_ids[i]], values[i]);
        if (status != XXWIDGETS_OK) goto rollback;
    }
    status = XXWIDGETS_OK;
    goto finish;
rollback:
    for (i = 0; i < 5; ++i) {
        xxwidgets_widget *text = widget->controls[text_ids[i]], *value = widget->controls[value_ids[i]];
        free(text->text); text->text = previous_text[i]; previous_text[i] = NULL;
        value->value = previous_value[i];
        recover(text); recover(value);
    }
finish:
    for (i = 0; i < 5; ++i) free(previous_text[i]);
    return status;
}

xxwidgets_status xxwidgets_search_widget_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_search_widget **out_widget)
{
    static const char *const names[XXWIDGETS_SEARCH_CONTROL_COUNT] = {
        "", "", "", XXW_N_("Search backward"), XXW_N_("Wrap at source boundaries"), XXW_N_("Selection only"), XXW_N_("Case sensitive"), "", "", "",
        XXW_N_("Search pattern"), XXW_N_("Search mode"), XXW_N_("Encoding"), XXW_N_("Start offset"), XXW_N_("Length"), XXW_N_("Alignment"),
        "Number: u32le:123 or f64be:1.5. Regex: . [] () | ? {n,m}; no unbounded repeats."};
    static const xxwidgets_kind kinds[XXWIDGETS_SEARCH_CONTROL_COUNT] = {
        XXWIDGETS_EDIT, XXWIDGETS_COMBOBOX, XXWIDGETS_EDIT, XXWIDGETS_CHECKBOX, XXWIDGETS_CHECKBOX,
        XXWIDGETS_CHECKBOX, XXWIDGETS_CHECKBOX, XXWIDGETS_EDIT, XXWIDGETS_EDIT, XXWIDGETS_EDIT,
        XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_LABEL};
    static const wchar_t *const modes[] = {L"Text", L"Hex bytes", L"Signature", L"Number", L"Bounded regex"};
    xxwidgets_rect rects[XXWIDGETS_SEARCH_CONTROL_COUNT];
    xxwidgets_search_options defaults;
    xxwidgets_search_widget *widget;
    xx_meta_string records[5] = {0};
    xx_str_w_s labels[5] = {0};
    xxwidgets_status status;
    size_t i;
    if (!out_widget) return XXWIDGETS_INVALID_ARGUMENT;
    *out_widget = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !valid_bounds(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing) return XXWIDGETS_BUSY;
    widget = (xxwidgets_search_widget *)calloc(1, sizeof(*widget));
    if (!widget) return XXWIDGETS_OUT_OF_MEMORY;
    widget->owner = owner;
    rectangles(bounds, rects);
    for (i = 0; i < XXWIDGETS_SEARCH_CONTROL_COUNT; ++i) {
        status = xxwidgets_widget_create(owner->app, owner, kinds[i], XXW_TR(owner->app, names[i]), rects[i], &widget->controls[i]);
        if (status != XXWIDGETS_OK) goto failed;
    }
    for (i = 0; i < 5; ++i) {
        labels[i].data = (wchar_t *)modes[i]; labels[i].length = wcslen(modes[i]);
        labels[i].capacity = labels[i].length + 1; labels[i].is_view = true;
        records[i].meta_string = &labels[i]; records[i].var.type = XX_VAR_TYPE_UINT64; records[i].var.val.u64 = i;
    }
    status = xxwidgets_combobox_set_records(widget->controls[XXWIDGETS_SEARCH_MODE], records, 5);
    if (status != XXWIDGETS_OK) goto failed;
    xxwidgets_search_options_init(&defaults);
    status = xxwidgets_search_widget_set(widget, &defaults);
    if (status != XXWIDGETS_OK) goto failed;
    *out_widget = widget;
    return XXWIDGETS_OK;
failed:
    xxwidgets_search_widget_destroy(widget);
    return status;
}

xxwidgets_status xxwidgets_search_widget_destroy(xxwidgets_search_widget *widget)
{
    size_t i;
    if (!widget) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->owner->app->dispatch_depth || widget->owner->app->polling || widget->owner->app->syncing) return XXWIDGETS_BUSY;
    for (i = 0; i < XXWIDGETS_SEARCH_CONTROL_COUNT; ++i) {
        xxwidgets_status status;
        if (!widget->controls[i]) continue;
        status = xxwidgets_widget_destroy(widget->controls[i]);
        if (status != XXWIDGETS_OK) return status;
        widget->controls[i] = NULL;
    }
    free(widget);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_search_widget_set_rect(xxwidgets_search_widget *widget, xxwidgets_rect bounds)
{
    xxwidgets_rect rects[XXWIDGETS_SEARCH_CONTROL_COUNT], previous[XXWIDGETS_SEARCH_CONTROL_COUNT];
    size_t i;
    if (!widget || !valid_bounds(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    rectangles(bounds, rects);
    for (i = 0; i < XXWIDGETS_SEARCH_CONTROL_COUNT; ++i) previous[i] = widget->controls[i]->rect;
    for (i = 0; i < XXWIDGETS_SEARCH_CONTROL_COUNT; ++i) {
        xxwidgets_status status = xxwidgets_widget_set_rect(widget->controls[i], rects[i]);
        if (status != XXWIDGETS_OK) {
            size_t j;
            for (j = 0; j < i; ++j) { widget->controls[j]->rect = previous[j]; recover(widget->controls[j]); }
            return status;
        }
    }
    return XXWIDGETS_OK;
}

typedef struct search_state {
    xxwidgets_widget *window, *find, *find_all, *cancel;
    xxwidgets_search_action pending;
    int done;
} search_state;

static void search_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    search_state *state = (search_state *)user;
    (void)app;
    if (event->widget == state->window && event->type == XXWIDGETS_EVENT_CLOSE) state->done = 1;
    else if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == state->find) state->pending = XXWIDGETS_SEARCH_FIND;
        else if (event->widget == state->find_all) state->pending = XXWIDGETS_SEARCH_FIND_ALL;
        else if (event->widget == state->cancel) state->done = 1;
    }
}

xxwidgets_status xxwidgets_search_dialog(xxwidgets_widget *owner, const char *title,
    xxwidgets_search_options *options, xxwidgets_search_action *action)
{
    xxwidgets_search_widget *form = NULL;
    xxwidgets_widget *focus = NULL, *control, *error = NULL, *previous_default;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    xxwidgets_app *app;
    xxwidgets_search_options selected;
    xxwidgets_search_action accepted = XXWIDGETS_SEARCH_CANCEL;
    search_state state = {0};
    xxwidgets_status status, cleanup;
    int enabled, disabled = 0, attached = 0;
    if (action) *action = XXWIDGETS_SEARCH_CANCEL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !title || !xxwidgets_valid_utf8(title) || !action ||
        xxwidgets_search_options_validate(options) != XXWIDGETS_OK || owner->rect.x > INT_MAX - 74 || owner->rect.y > INT_MAX - 22)
        return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit) return XXWIDGETS_BUSY;
    previous_event = app->on_event; previous_user = app->user_data; previous_default = app->modal_default;
    enabled = owner->enabled;
    for (control = app->widgets; control; control = control->next)
        if (control->parent == owner && xxwidgets_focusable(control) && (!focus || xxwidgets_widget_has_focus(control) == 1)) focus = control;
    app->on_event = search_event; app->user_data = &state;
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title,
        (xxwidgets_rect){owner->rect.x + (owner->rect.width > 74 ? (owner->rect.width - 74) / 2 : 0),
            owner->rect.y + (owner->rect.height > 22 ? (owner->rect.height - 22) / 2 : 0), 74, 22}, &state.window);
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_window = state.window;
    status = xxwidgets_search_widget_create(state.window, (xxwidgets_rect){2, 1, 70, 15}, &form);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_search_widget_set(form, options);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_widget_create(app, state.window, XXWIDGETS_LABEL, "", (xxwidgets_rect){2, 17, 70, 1}, &error);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON, XXW_TR(app, "Find"), (xxwidgets_rect){36, 19, 10, 1}, &state.find);
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_default = state.find;
    status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON, XXW_TR(app, "Find All"), (xxwidgets_rect){48, 19, 12, 1}, &state.find_all);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON, XXW_TR(app, "Cancel"), (xxwidgets_rect){62, 19, 10, 1}, &state.cancel);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_widget_set_enabled(owner, 0);
    if (status != XXWIDGETS_OK) goto finish;
    disabled = 1;
    if (app->ops->modal_owner) {
        status = app->ops->modal_owner(state.window, owner, 1);
        if (status != XXWIDGETS_OK) goto finish;
        attached = 1;
    }
    status = xxwidgets_widget_focus(form->controls[XXWIDGETS_SEARCH_PATTERN]);
    while (status == XXWIDGETS_OK && !state.done && !app->quit) {
        const char *message = NULL;
        status = xxwidgets_app_poll(app, 30);
        if (status != XXWIDGETS_OK || state.done || app->quit || !state.pending) continue;
        status = read_options(form, &selected, &message);
        if (status == XXWIDGETS_OK && !selected.pattern[0]) {
            status = XXWIDGETS_INVALID_ARGUMENT;
            message = "Enter a search pattern.";
        }
        if (status == XXWIDGETS_INVALID_ARGUMENT || status == XXWIDGETS_BUFFER_TOO_SMALL) {
            status = xxwidgets_widget_set_text(error, XXW_TR(app, message ? message : XXW_N_("Check the search settings.")));
            state.pending = XXWIDGETS_SEARCH_CANCEL;
        } else if (status == XXWIDGETS_OK) { accepted = state.pending; state.done = 1; }
    }
    if (app->quit) accepted = XXWIDGETS_SEARCH_CANCEL;
finish:
    if (disabled) {
        cleanup = xxwidgets_widget_set_enabled(owner, enabled);
        if (cleanup != XXWIDGETS_OK) { owner->enabled = enabled; recover(owner); }
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (attached) { cleanup = app->ops->modal_owner(state.window, owner, 0); if (status == XXWIDGETS_OK) status = cleanup; }
    if (form) { cleanup = xxwidgets_search_widget_destroy(form); if (status == XXWIDGETS_OK) status = cleanup; }
    if (state.window) { cleanup = xxwidgets_widget_destroy(state.window); if (status == XXWIDGETS_OK) status = cleanup; }
    app->modal_window = NULL; app->modal_default = previous_default;
    app->on_event = previous_event; app->user_data = previous_user;
    if (focus) xxwidgets_widget_focus(focus);
    if (status == XXWIDGETS_OK && accepted != XXWIDGETS_SEARCH_CANCEL) { *options = selected; *action = accepted; }
    return status;
}
