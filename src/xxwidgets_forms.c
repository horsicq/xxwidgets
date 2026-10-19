/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_forms.h"

#include <limits.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct form_modal {
    xxwidgets_app *app;
    xxwidgets_widget *owner, *window, *ok, *cancel, *list, *focus, *previous_default, *plot;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    int done, pending, enabled, disabled, attached;
    xxwidgets_widget *filter; int filter_changed;
    xxwidgets_status failure;
    const xxwidgets_canvas_region *canvas_regions;size_t canvas_region_count;
    xxwidgets_widget *canvas_status;
} form_modal;

static int owner_valid(xxwidgets_widget *owner, const char *title, int height)
{
    return owner && owner->kind == XXWIDGETS_WINDOW && title && xxwidgets_valid_utf8(title) &&
        owner->rect.x <= INT_MAX - 74 && owner->rect.y <= INT_MAX - height;
}

/* ENTROPY_PLOT has an internal selected sample, rather than the generic
 * checkbox/progress/list value API. Keep redraws silent and transactional. */
static xxwidgets_status form_plot_select(form_modal *modal, int value)
{
    xxwidgets_widget *plot = modal->plot;
    if (!plot || value < -1 || (value >= 0 && (size_t)value >= plot->entropy_plot_count)) return XXWIDGETS_INVALID_ARGUMENT;
    int previous = plot->value;
    plot->value = value;
    ++plot->app->syncing;
    xxwidgets_status status = plot->app->ops->sync(plot);
    --plot->app->syncing;
    if (status != XXWIDGETS_OK) {
        plot->value = previous;
        ++plot->app->syncing; (void)plot->app->ops->sync(plot); --plot->app->syncing;
    }
    return status;
}

static void form_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    form_modal *modal = (form_modal *)user;
    (void)app;
    if (event->widget == modal->window && event->type == XXWIDGETS_EVENT_CLOSE) modal->done = 1;
    else if (event->widget == modal->filter && event->type == XXWIDGETS_EVENT_CHANGE) modal->filter_changed = 1;
    else if (event->widget == modal->filter && event->type == XXWIDGETS_EVENT_ACTIVATE) modal->pending = 1;
    else if (event->widget == modal->list && event->type == XXWIDGETS_EVENT_ACTIVATE) modal->pending = 1;
    else if (modal->plot && modal->plot->kind==XXWIDGETS_CANVAS && event->type==XXWIDGETS_EVENT_SELECT && (event->widget==modal->plot||event->widget==modal->list)) {
        if(event->widget==modal->plot){size_t region=SIZE_MAX;for(size_t i=0;i<modal->canvas_region_count;++i){const xxwidgets_canvas_region *r=modal->canvas_regions+i;if((unsigned)event->x>=r->x&&(unsigned)event->x-r->x<r->width&&(unsigned)event->y>=r->y&&(unsigned)event->y-r->y<r->height){region=i;break;}}if(region!=SIZE_MAX)xxwidgets_widget_set_value(modal->list,(int)region);char text[128];snprintf(text,sizeof(text),"Pixel %d,%d | index %d | Enter selects; Tab changes focus",event->x,event->y,event->value);xxwidgets_widget_set_text(modal->canvas_status,text);}
        else if(event->value>=0&&(size_t)event->value<modal->canvas_region_count){const xxwidgets_canvas_region *r=modal->canvas_regions+event->value;xxwidgets_canvas_select(modal->plot,r->x+r->width/2,r->y+r->height/2);xxwidgets_widget_set_text(modal->canvas_status,r->label);}
    }
    else if (modal->plot && modal->plot->kind==XXWIDGETS_CANVAS && event->widget==modal->plot && event->type==XXWIDGETS_EVENT_ACTIVATE) modal->pending=1;
    else if (modal->plot && event->type == XXWIDGETS_EVENT_SELECT) {
        xxwidgets_status status = XXWIDGETS_OK;
        if (event->widget == modal->plot) status = xxwidgets_widget_set_value(modal->list, event->value);
        if (status == XXWIDGETS_OK && (event->widget == modal->plot || event->widget == modal->list)) status = form_plot_select(modal, event->value);
        if (status != XXWIDGETS_OK) { modal->failure = status; modal->done = 1; }
    }
    else if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == modal->ok) modal->pending = 1;
        else if (event->widget == modal->cancel) modal->done = 1;
    }
}

static xxwidgets_status modal_begin(form_modal *modal, xxwidgets_widget *owner, const char *title, int height)
{
    xxwidgets_widget *widget;
    xxwidgets_app *app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit) return XXWIDGETS_BUSY;
    modal->app = app; modal->owner = owner; modal->enabled = owner->enabled;
    modal->previous_event = app->on_event; modal->previous_user = app->user_data; modal->previous_default = app->modal_default;
    for (widget = app->widgets; widget; widget = widget->next)
        if (widget->parent == owner && xxwidgets_focusable(widget) && (!modal->focus || xxwidgets_widget_has_focus(widget) == 1)) modal->focus = widget;
    app->on_event = form_event; app->user_data = modal;
    {
        xxwidgets_status status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title,
            (xxwidgets_rect){owner->rect.x + (owner->rect.width > 74 ? (owner->rect.width - 74) / 2 : 0),
                owner->rect.y + (owner->rect.height > height ? (owner->rect.height - height) / 2 : 0), 74, height}, &modal->window);
        if (status != XXWIDGETS_OK) return status;
    }
    app->modal_window = modal->window;
    return XXWIDGETS_OK;
}

static xxwidgets_status modal_show(form_modal *modal, xxwidgets_widget *focus)
{
    xxwidgets_status status;
    modal->app->modal_default = modal->ok ? modal->ok : modal->cancel;
    status = xxwidgets_widget_set_enabled(modal->owner, 0);
    if (status != XXWIDGETS_OK) return status;
    modal->disabled = 1;
    if (modal->app->ops->modal_owner) {
        status = modal->app->ops->modal_owner(modal->window, modal->owner, 1);
        if (status != XXWIDGETS_OK) return status;
        modal->attached = 1;
    }
    return xxwidgets_widget_focus(focus);
}

static xxwidgets_status modal_finish(form_modal *modal, xxwidgets_status status)
{
    xxwidgets_status cleanup;
    if (!modal->app) return status;
    if (modal->disabled) {
        cleanup = xxwidgets_widget_set_enabled(modal->owner, modal->enabled);
        if (cleanup != XXWIDGETS_OK) {
            modal->owner->enabled = modal->enabled;
            ++modal->app->syncing;
            modal->app->ops->sync(modal->owner);
            --modal->app->syncing;
        }
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (modal->attached) {
        cleanup = modal->app->ops->modal_owner(modal->window, modal->owner, 0);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (modal->window) {
        cleanup = xxwidgets_widget_destroy(modal->window);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    modal->app->modal_window = NULL;
    modal->app->modal_default = modal->previous_default;
    modal->app->on_event = modal->previous_event;
    modal->app->user_data = modal->previous_user;
    if (modal->focus) xxwidgets_widget_focus(modal->focus);
    return status;
}

static int fields_valid(const xxwidgets_form_field *fields, size_t count)
{
    size_t i, j;
    if ((!fields && count) || count > XXWIDGETS_FORM_MAX_FIELDS) return 0;
    for (i = 0; i < count; ++i) {
        const xxwidgets_form_field *field = &fields[i];
        if (!field->label || !xxwidgets_valid_utf8(field->label) || (field->checkbox != 0 && field->checkbox != 1)) return 0;
        if (field->checkbox) {
            if (field->checked != 0 && field->checked != 1) return 0;
        } else {
            uintptr_t begin = (uintptr_t)field->value;
            if (!field->value || !field->capacity || field->capacity > XXWIDGETS_FORM_MAX_CAPACITY ||
                begin > UINTPTR_MAX - field->capacity || !memchr(field->value, 0, field->capacity) || !xxwidgets_valid_utf8(field->value)) return 0;
            for (j = 0; j < i; ++j) {
                uintptr_t other;
                if (fields[j].checkbox) continue;
                other = (uintptr_t)fields[j].value;
                if (begin < other + fields[j].capacity && other < begin + field->capacity) return 0;
            }
        }
    }
    return 1;
}

xxwidgets_status xxwidgets_form_dialog(xxwidgets_widget *owner, const char *title,
    xxwidgets_form_field *fields, size_t count, int *accepted)
{
    form_modal modal = {0};
    xxwidgets_widget *controls[XXWIDGETS_FORM_MAX_FIELDS] = {0}, *error = NULL;
    char *results[XXWIDGETS_FORM_MAX_FIELDS] = {0};
    int checked[XXWIDGETS_FORM_MAX_FIELDS] = {0};
    xxwidgets_status status = XXWIDGETS_OK;
    size_t i;
    int commit = 0, height;
    if (accepted) *accepted = 0;
    if (!accepted || !fields_valid(fields, count)) return XXWIDGETS_INVALID_ARGUMENT;
    height = (int)count + 7;
    if (!owner_valid(owner, title, height)) return XXWIDGETS_INVALID_ARGUMENT;
    status = modal_begin(&modal, owner, title, height);
    if (status != XXWIDGETS_OK) goto finish;
    for (i = 0; i < count; ++i) {
        const xxwidgets_form_field *field = &fields[i];
        int row = 1 + (int)i;
        if (field->checkbox) {
            status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_CHECKBOX, field->label,
                (xxwidgets_rect){2, row, 70, 1}, &controls[i]);
            if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(controls[i], field->checked);
        } else {
            xxwidgets_widget *label;
            results[i] = (char *)malloc(field->capacity);
            if (!results[i]) { status = XXWIDGETS_OUT_OF_MEMORY; goto finish; }
            status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_LABEL, field->label,
                (xxwidgets_rect){2, row, 22, 1}, &label);
            if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_EDIT, field->value,
                (xxwidgets_rect){25, row, 47, 1}, &controls[i]);
        }
        if (status != XXWIDGETS_OK) goto finish;
    }
    status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_LABEL, "", (xxwidgets_rect){2, (int)count + 2, 70, 1}, &error);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_BUTTON, "OK", (xxwidgets_rect){48, (int)count + 4, 10, 1}, &modal.ok);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_BUTTON, "Cancel", (xxwidgets_rect){60, (int)count + 4, 12, 1}, &modal.cancel);
    if (status != XXWIDGETS_OK) goto finish;
    status = modal_show(&modal, count ? controls[0] : modal.ok);
    while (status == XXWIDGETS_OK && !modal.done && !modal.app->quit) {
        status = xxwidgets_app_poll(modal.app, 30);
        if (status != XXWIDGETS_OK || modal.done || modal.app->quit || !modal.pending) continue;
        modal.pending = 0;
        for (i = 0; i < count; ++i) {
            status = fields[i].checkbox ? xxwidgets_widget_get_value(controls[i], &checked[i]) :
                xxwidgets_widget_get_text(controls[i], results[i], fields[i].capacity, NULL);
            if (status != XXWIDGETS_OK) break;
        }
        if (status == XXWIDGETS_BUFFER_TOO_SMALL) status = xxwidgets_widget_set_text(error, "One of the values is too long. Shorten it and choose OK.");
        else if (status == XXWIDGETS_OK) { commit = 1; modal.done = 1; }
    }
    if (modal.app->quit) commit = 0;
finish:
    status = modal_finish(&modal, status);
    if (status == XXWIDGETS_OK && commit) {
        for (i = 0; i < count; ++i) {
            if (fields[i].checkbox) fields[i].checked = checked[i];
            else memcpy(fields[i].value, results[i], strlen(results[i]) + 1);
        }
        *accepted = 1;
    }
    for (i = 0; i < count; ++i) free(results[i]);
    return status;
}

xxwidgets_status xxwidgets_choice_dialog(xxwidgets_widget *owner, const char *title,
    const char *const *items, size_t count, size_t *selection, int *accepted)
{
    form_modal modal = {0};
    xxwidgets_tree_node *nodes = NULL;
    xxwidgets_widget *error = NULL;
    xxwidgets_status status;
    size_t i, selected = SIZE_MAX;
    int commit = 0;
    if (accepted) *accepted = 0;
    if (!owner_valid(owner, title, 22) || !selection || !accepted || (!items && count) || count > INT_MAX ||
        count > SIZE_MAX / sizeof(*nodes) || (count && *selection != SIZE_MAX && *selection >= count)) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < count; ++i) if (!items[i] || !xxwidgets_valid_utf8(items[i])) return XXWIDGETS_INVALID_ARGUMENT;
    status = modal_begin(&modal, owner, title, 22);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_TREEVIEW, "", (xxwidgets_rect){2, 1, 70, 16}, &modal.list);
    if (status != XXWIDGETS_OK) goto finish;
    if (count) {
        nodes = (xxwidgets_tree_node *)calloc(count, sizeof(*nodes));
        if (!nodes) { status = XXWIDGETS_OUT_OF_MEMORY; goto finish; }
        for (i = 0; i < count; ++i) { nodes[i].parent = SIZE_MAX; nodes[i].text = items[i]; }
        status = xxwidgets_treeview_set_nodes(modal.list, nodes, count);
        if (status != XXWIDGETS_OK) goto finish;
        status = xxwidgets_widget_set_value(modal.list, *selection == SIZE_MAX ? -1 : (int)*selection);
        if (status != XXWIDGETS_OK) goto finish;
    }
    status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_LABEL, count ? "" : "No items.", (xxwidgets_rect){2, 18, 70, 1}, &error);
    if (status != XXWIDGETS_OK) goto finish;
    if (count) {
        status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_BUTTON, "OK", (xxwidgets_rect){48, 20, 10, 1}, &modal.ok);
        if (status != XXWIDGETS_OK) goto finish;
    }
    status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_BUTTON, count ? "Cancel" : "Close", (xxwidgets_rect){60, 20, 12, 1}, &modal.cancel);
    if (status != XXWIDGETS_OK) goto finish;
    status = modal_show(&modal, count ? modal.list : modal.cancel);
    while (status == XXWIDGETS_OK && !modal.done && !modal.app->quit) {
        xxwidgets_tree_node node;
        status = xxwidgets_app_poll(modal.app, 30);
        if (status != XXWIDGETS_OK || modal.done || modal.app->quit || !modal.pending) continue;
        modal.pending = 0;
        status = xxwidgets_treeview_get_selection(modal.list, &selected, &node);
        if (status == XXWIDGETS_OK && selected >= count) status = xxwidgets_widget_set_text(error, "Select an item, then choose OK.");
        else if (status == XXWIDGETS_OK) { commit = 1; modal.done = 1; }
    }
    if (modal.app->quit) commit = 0;
finish:
    free(nodes);
    status = modal_finish(&modal, status);
    if (status == XXWIDGETS_OK && commit) { *selection = selected; *accepted = 1; }
    return status;
}

xxwidgets_status xxwidgets_structure_dialog(xxwidgets_widget *owner, const char *title,
    const xxwidgets_data_struct_field *fields, size_t count, size_t *selection, int *accepted)
{
    form_modal modal = {0};
    xxwidgets_status status;
    size_t selected = SIZE_MAX;
    int commit = 0;
    if (accepted) *accepted = 0;
    if (!owner_valid(owner, title, 22) || !selection || !accepted || (!fields && count) || count > 4096 ||
        (count && *selection != SIZE_MAX && *selection >= count)) return XXWIDGETS_INVALID_ARGUMENT;
    status = modal_begin(&modal, owner, title, 22);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_DATA_STRUCT_TABLE, "",
        (xxwidgets_rect){2, 1, 70, 17}, &modal.list);
    if (status == XXWIDGETS_OK) status = xxwidgets_data_struct_table_set_fields(modal.list, fields, count);
    if (status == XXWIDGETS_OK && count) status = xxwidgets_widget_set_value(modal.list, *selection == SIZE_MAX ? 0 : (int)*selection);
    if (status == XXWIDGETS_OK && count) status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_BUTTON, "Select bytes",
        (xxwidgets_rect){43, 20, 15, 1}, &modal.ok);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_BUTTON, count ? "Cancel" : "Close",
        (xxwidgets_rect){60, 20, 12, 1}, &modal.cancel);
    if (status == XXWIDGETS_OK) status = modal_show(&modal, count ? modal.list : modal.cancel);
    while (status == XXWIDGETS_OK && !modal.done && !modal.app->quit) {
        xxwidgets_data_struct_field field;
        status = xxwidgets_app_poll(modal.app, 30);
        if (status != XXWIDGETS_OK || modal.done || modal.app->quit || !modal.pending) continue;
        modal.pending = 0;
        status = xxwidgets_data_struct_table_get_selection(modal.list, &selected, &field);
        if (status == XXWIDGETS_OK && selected < count) { commit = 1; modal.done = 1; }
    }
    if (modal.app->quit) commit = 0;
finish:
    status = modal_finish(&modal, status);
    if (status == XXWIDGETS_OK && commit) { *selection = selected; *accepted = 1; }
    return status;
}

xxwidgets_status xxwidgets_entropy_samples_dialog(xxwidgets_widget *owner, const char *title,
    const xxwidgets_entropy_sample *samples, size_t count, uint64_t base, uint64_t size,
    size_t *selection, int *accepted)
{
    form_modal modal = {0};
    xxwidgets_status status;
    xxwidgets_tree_node *nodes = NULL;
    char (*labels)[128] = NULL;
    size_t selected = SIZE_MAX;
    int commit = 0;
    if (accepted) *accepted = 0;
    if (!owner_valid(owner, title, 22) || !selection || !accepted || (!samples && count) || count > 4096 ||
        (count && *selection != SIZE_MAX && *selection >= count)) return XXWIDGETS_INVALID_ARGUMENT;
    status = modal_begin(&modal, owner, title, 22);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_ENTROPY_PLOT, "",
        (xxwidgets_rect){2, 0, 70, 11}, &modal.plot);
    if (status == XXWIDGETS_OK) status = xxwidgets_entropy_plot_set_samples(modal.plot, samples, count, base, size);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_TREEVIEW, "",
        (xxwidgets_rect){2, 12, 70, 6}, &modal.list);
    if (status != XXWIDGETS_OK) goto finish;
    if (count) {
        labels = calloc(count, sizeof(*labels)); nodes = calloc(count, sizeof(*nodes));
        if (!labels || !nodes) { status = XXWIDGETS_OUT_OF_MEMORY; goto finish; }
        for (size_t i = 0; i < count; ++i) {
            snprintf(labels[i], sizeof(labels[i]), "0x%" PRIX64 " | %" PRIu64 " bytes | %.5f bits/byte%s",
                samples[i].offset, samples[i].size, samples[i].entropy, samples[i].entropy >= 6.5 ? " | packed" : "");
            nodes[i].parent = SIZE_MAX; nodes[i].text = labels[i];
        }
        status = xxwidgets_treeview_set_nodes(modal.list, nodes, count);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(modal.list, *selection == SIZE_MAX ? 0 : (int)*selection);
        if (status == XXWIDGETS_OK) status = form_plot_select(&modal, *selection == SIZE_MAX ? 0 : (int)*selection);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_BUTTON, "Select block",
            (xxwidgets_rect){43, 20, 15, 1}, &modal.ok);
    }
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_BUTTON, count ? "Cancel" : "Close",
        (xxwidgets_rect){60, 20, 12, 1}, &modal.cancel);
    if (status == XXWIDGETS_OK) status = modal_show(&modal, count ? modal.list : modal.cancel);
    while (status == XXWIDGETS_OK && !modal.done && !modal.app->quit) {
        xxwidgets_tree_node node;
        status = xxwidgets_app_poll(modal.app, 30);
        if (status != XXWIDGETS_OK || modal.done || modal.app->quit || !modal.pending) continue;
        modal.pending = 0;
        status = xxwidgets_treeview_get_selection(modal.list, &selected, &node);
        if (status == XXWIDGETS_OK && selected < count) { commit = 1; modal.done = 1; }
    }
    if (modal.app->quit) commit = 0;
finish:
    free(labels); free(nodes);
    if (status == XXWIDGETS_OK && modal.failure != XXWIDGETS_OK) status = modal.failure;
    status = modal_finish(&modal, status);
    if (status == XXWIDGETS_OK && commit) { *selection = selected; *accepted = 1; }
    return status;
}

xxwidgets_status xxwidgets_command_palette(xxwidgets_widget *owner, const char *title,
    const xxwidgets_command_item *items, size_t count, int *action, int *accepted)
{
    form_modal modal = {0};
    xxwidgets_status status;
    xxwidgets_tree_node *nodes = NULL;
    char (*labels)[512] = NULL;
    size_t *mapping = NULL, visible = 0;
    int selected_action = 0, commit = 0;
    if (accepted) *accepted = 0;
    if (!accepted || !action || (!items && count) || count > 4096 || !owner_valid(owner, title, 22)) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i)
        if (!items[i].label || !xxwidgets_valid_utf8(items[i].label) || (items[i].detail && !xxwidgets_valid_utf8(items[i].detail)) || (items[i].shortcut && !xxwidgets_valid_utf8(items[i].shortcut))) return XXWIDGETS_INVALID_ARGUMENT;
    nodes = calloc(count ? count : 1, sizeof(*nodes)); labels = calloc(count ? count : 1, sizeof(*labels)); mapping = calloc(count ? count : 1, sizeof(*mapping));
    if (!nodes || !labels || !mapping) { status = XXWIDGETS_OUT_OF_MEMORY; goto finish; }
    status = modal_begin(&modal, owner, title, 22);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_EDIT, "", (xxwidgets_rect){2,1,70,1}, &modal.filter);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_TREEVIEW, "", (xxwidgets_rect){2,3,70,15}, &modal.list);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_BUTTON, "Run", (xxwidgets_rect){48,20,10,1}, &modal.ok);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(modal.app, modal.window, XXWIDGETS_BUTTON, "Cancel", (xxwidgets_rect){60,20,12,1}, &modal.cancel);
    if (status == XXWIDGETS_OK) status = modal_show(&modal, modal.filter);
    modal.filter_changed = 1;
    while (status == XXWIDGETS_OK && !modal.done && !modal.app->quit) {
        if (modal.filter_changed) {
            char filter[1024]; modal.filter_changed = 0;
            status = xxwidgets_widget_get_text(modal.filter, filter, sizeof(filter), NULL);
            if (status == XXWIDGETS_BUFFER_TOO_SMALL) { status = XXWIDGETS_OK; filter[sizeof(filter)-1] = 0; }
            if (status != XXWIDGETS_OK) break;
            visible = 0;
            for (size_t i = 0; i < count; ++i) if (xxwidgets_command_matches(&items[i], filter)) {
                snprintf(labels[visible], sizeof(labels[visible]), "%s%s%s", items[i].label, items[i].shortcut && *items[i].shortcut ? "  |  " : "", items[i].shortcut ? items[i].shortcut : "");
                nodes[visible].parent = SIZE_MAX; nodes[visible].text = labels[visible]; mapping[visible++] = i;
            }
            status = xxwidgets_treeview_set_nodes(modal.list, nodes, visible);
            if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(modal.list, visible ? 0 : -1);
            if (status != XXWIDGETS_OK) break;
        }
        status = xxwidgets_app_poll(modal.app, 30);
        /* A terminal/native poll may deliver an entire filter plus Enter.
         * Rebuild the filtered mapping before accepting that queued Run. */
        if (status == XXWIDGETS_OK && modal.filter_changed && !modal.done && !modal.app->quit) continue;
        if (status == XXWIDGETS_OK && modal.pending && !modal.done && !modal.app->quit) {
            int value = -1; modal.pending = 0;
            status = xxwidgets_widget_get_value(modal.list, &value);
            if (status == XXWIDGETS_OK && value >= 0 && (size_t)value < visible) { selected_action = items[mapping[value]].action; commit = 1; modal.done = 1; }
        }
    }
finish:
    status = modal_finish(&modal, status);
    free(nodes); free(labels); free(mapping);
    if (status == XXWIDGETS_OK && commit) { *action = selected_action; *accepted = 1; }
    return status;
}

xxwidgets_status xxwidgets_canvas_dialog(xxwidgets_widget *owner,const char *title,const uint32_t *pixels,unsigned width,unsigned height,const xxwidgets_canvas_label *labels,size_t label_count,const xxwidgets_canvas_region *regions,size_t count,size_t *selection,int *accepted) {
    form_modal modal={0};xxwidgets_status status;int commit=0;size_t selected=SIZE_MAX;xxwidgets_tree_node *nodes=NULL;
    if(accepted)*accepted=0;
    if(!owner_valid(owner,title,22)||!selection||!accepted||!width||!height||width>4096||height>4096||!pixels||count>4096||(count&&!regions)||(*selection!=SIZE_MAX&&*selection>=(count?count:(size_t)width*height)))return XXWIDGETS_INVALID_ARGUMENT;
    for(size_t i=0;i<count;++i){const xxwidgets_canvas_region *r=regions+i;if(!r->label||!xxwidgets_valid_utf8(r->label)||!r->width||!r->height||r->x>=width||r->y>=height||r->width>width-r->x||r->height>height-r->y)return XXWIDGETS_INVALID_ARGUMENT;}
    status=modal_begin(&modal,owner,title,22);if(status!=XXWIDGETS_OK)goto finish;modal.canvas_regions=regions;modal.canvas_region_count=count;
    status=xxwidgets_widget_create(modal.app,modal.window,XXWIDGETS_CANVAS,"",(xxwidgets_rect){2,1,count?47:70,17},&modal.plot);
    if(status==XXWIDGETS_OK)status=xxwidgets_canvas_set_pixels(modal.plot,width,height,pixels);
    if(status==XXWIDGETS_OK)status=xxwidgets_canvas_set_labels(modal.plot,labels,label_count);
    if(status!=XXWIDGETS_OK)goto finish;
    if(count){status=xxwidgets_widget_create(modal.app,modal.window,XXWIDGETS_TREEVIEW,"",(xxwidgets_rect){50,1,22,17},&modal.list);if(status!=XXWIDGETS_OK)goto finish;nodes=calloc(count,sizeof(*nodes));if(!nodes){status=XXWIDGETS_OUT_OF_MEMORY;goto finish;}for(size_t i=0;i<count;++i){nodes[i].parent=SIZE_MAX;nodes[i].text=regions[i].label;}status=xxwidgets_treeview_set_nodes(modal.list,nodes,count);if(status==XXWIDGETS_OK)status=xxwidgets_widget_set_value(modal.list,*selection==SIZE_MAX?0:(int)*selection);size_t initial=*selection==SIZE_MAX?0:*selection;const xxwidgets_canvas_region *r=regions+initial;if(status==XXWIDGETS_OK)status=xxwidgets_canvas_select(modal.plot,r->x+r->width/2,r->y+r->height/2);}
    else {size_t initial=*selection==SIZE_MAX?0:*selection;status=xxwidgets_canvas_select(modal.plot,(unsigned)(initial%width),(unsigned)(initial/width));}
    if(status==XXWIDGETS_OK)status=xxwidgets_widget_create(modal.app,modal.window,XXWIDGETS_LABEL,"Arrow keys move; Enter selects; Tab changes focus",(xxwidgets_rect){2,18,70,1},&modal.canvas_status);
    if(status==XXWIDGETS_OK)status=xxwidgets_widget_create(modal.app,modal.window,XXWIDGETS_BUTTON,"Select bytes",(xxwidgets_rect){43,20,15,1},&modal.ok);
    if(status==XXWIDGETS_OK)status=xxwidgets_widget_create(modal.app,modal.window,XXWIDGETS_BUTTON,"Cancel",(xxwidgets_rect){60,20,12,1},&modal.cancel);
    if(status==XXWIDGETS_OK)status=modal_show(&modal,count?modal.list:modal.plot);
    while(status==XXWIDGETS_OK&&!modal.done&&!modal.app->quit){status=xxwidgets_app_poll(modal.app,30);if(status!=XXWIDGETS_OK||modal.done||modal.app->quit||!modal.pending)continue;modal.pending=0;if(count){xxwidgets_tree_node node;status=xxwidgets_treeview_get_selection(modal.list,&selected,&node);}else selected=modal.plot->value<0?SIZE_MAX:(size_t)modal.plot->value;if(status==XXWIDGETS_OK&&selected<(count?count:(size_t)width*height)){commit=1;modal.done=1;}}
    if(modal.app&&modal.app->quit)commit=0;
finish:free(nodes);status=modal_finish(&modal,status);if(status==XXWIDGETS_OK&&commit){*selection=selected;*accepted=1;}return status;
}
