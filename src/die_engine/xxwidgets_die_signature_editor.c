/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "../xxwidgets_internal.h"
#include "xxwidgets/die_engine/xxwidgets_die_signature_editor.h"
#include "xxfclib/io/xx_io.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIGNATURE_MAX_SIZE (8U * 1024U * 1024U)

struct xxwidgets_die_signature_editor {
    xxwidgets_widget *owner;
    xxwidgets_widget *controls[XXWIDGETS_DIE_EDITOR_CONTROL_COUNT];
    char *path;
    xxwidgets_die_signature_editor_run_fn run_callback;
    void *run_user;
    uint64_t temp_sequence;
    int pending, executing;
    char notice[192];
};

static char *copy_text(const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = (char *)malloc(size);
    if (copy) memcpy(copy, text, size);
    return copy;
}

static int valid_bounds(xxwidgets_rect bounds)
{
    return bounds.x >= 0 && bounds.y >= 0 && bounds.width >= 72 && bounds.height >= 22 &&
        bounds.x <= INT_MAX - bounds.width && bounds.y <= INT_MAX - bounds.height;
}

static int outside_dispatch(const xxwidgets_die_signature_editor *editor)
{
    return !editor->executing && !editor->owner->app->dispatch_depth &&
        !editor->owner->app->polling && !editor->owner->app->syncing;
}

static const char *const editor_labels[XXWIDGETS_DIE_EDITOR_CONTROL_COUNT] = {
    XXW_N_("Open"), XXW_N_("Save"), XXW_N_("Save As"), XXW_N_("Read-only"), "", "", "", XXW_N_("Find next"), XXW_N_("Run"), XXW_N_("Clear result"), "", ""};

/* A toolbar control as wide as its translated label (a check box adds its
 * box), never narrower than the English layout nor wider than limit unless
 * that layout needs it. */
static int label_width(xxwidgets_app *app, xxwidgets_die_signature_editor_control_id id, int minimum, int limit)
{
    const unsigned char *s = (const unsigned char *)XXW_TR(app, editor_labels[id]);
    int cells = id == XXWIDGETS_DIE_EDITOR_READ_ONLY ? 5 : 3;
    for (; *s; ++s)
        if ((*s & 0xc0) != 0x80) ++cells;
    if (limit < minimum) limit = minimum;
    return cells < minimum ? minimum : cells > limit ? limit : cells;
}

static void rectangles(xxwidgets_app *app, xxwidgets_rect bounds, xxwidgets_rect *rects)
{
    int x = bounds.x, y = bounds.y, width = bounds.width, height = bounds.height;
    /* Whole labels when the row has room; otherwise six equal shares of it
     * (the English layout always fits the 72-cell minimum). */
    int limit = width, open, save, save_as, read_only, run, clear, find, pass;
    for (pass = 0; pass < 2; ++pass) {
        open = label_width(app, XXWIDGETS_DIE_EDITOR_OPEN, 7, limit);
        save = label_width(app, XXWIDGETS_DIE_EDITOR_SAVE, 7, limit);
        save_as = label_width(app, XXWIDGETS_DIE_EDITOR_SAVE_AS, 10, limit);
        read_only = label_width(app, XXWIDGETS_DIE_EDITOR_READ_ONLY, 14, limit);
        run = label_width(app, XXWIDGETS_DIE_EDITOR_RUN, 7, limit);
        clear = label_width(app, XXWIDGETS_DIE_EDITOR_CLEAR_RESULT, 12, limit);
        if (open + save + save_as + read_only + run + clear + 6 <= width) break;
        limit = (width - 6) / 6;
    }
    find = label_width(app, XXWIDGETS_DIE_EDITOR_FIND_NEXT, 11, width / 3);
    rects[XXWIDGETS_DIE_EDITOR_OPEN] = (xxwidgets_rect){x, y, open, 2};
    rects[XXWIDGETS_DIE_EDITOR_SAVE] = (xxwidgets_rect){x + open + 1, y, save, 2};
    rects[XXWIDGETS_DIE_EDITOR_SAVE_AS] = (xxwidgets_rect){x + open + save + 2, y, save_as, 2};
    rects[XXWIDGETS_DIE_EDITOR_READ_ONLY] = (xxwidgets_rect){x + open + save + save_as + 3, y, read_only, 2};
    rects[XXWIDGETS_DIE_EDITOR_RUN] = (xxwidgets_rect){x + width - clear - run - 1, y, run, 2};
    rects[XXWIDGETS_DIE_EDITOR_CLEAR_RESULT] = (xxwidgets_rect){x + width - clear, y, clear, 2};
    rects[XXWIDGETS_DIE_EDITOR_PATH] = (xxwidgets_rect){x, y + 2, width, 2};
    rects[XXWIDGETS_DIE_EDITOR_FIND_TEXT] = (xxwidgets_rect){x, y + 4, width - find - 1, 2};
    rects[XXWIDGETS_DIE_EDITOR_FIND_NEXT] = (xxwidgets_rect){x + width - find, y + 4, find, 2};
    rects[XXWIDGETS_DIE_EDITOR_SOURCE] = (xxwidgets_rect){x, y + 6, width, height - 13};
    rects[XXWIDGETS_DIE_EDITOR_RESULT] = (xxwidgets_rect){x, y + height - 7, width, 6};
    rects[XXWIDGETS_DIE_EDITOR_STATUS] = (xxwidgets_rect){x, y + height - 1, width, 1};
}

static xxwidgets_status describe(xxwidgets_die_signature_editor *editor, const char *notice)
{
    char text[320];
    size_t line = 1, column = 1;
    xxwidgets_status status, next;
    int modified = xxwidgets_die_signature_editor_modified(editor);
    if (notice) {
        snprintf(editor->notice, sizeof(editor->notice), "%s", notice);
    }
    (void)xxwidgets_die_signature_edit_position(editor->controls[XXWIDGETS_DIE_EDITOR_SOURCE], &line, &column);
    snprintf(text, sizeof(text), "%s | %s | Line %zu, column %zu%s%s",
        xxwidgets_die_signature_editor_read_only(editor) ? "Read-only" : "Editable",
        modified ? "Modified" : "Saved", line, column,
        editor->notice[0] ? " | " : "", editor->notice);
    status = xxwidgets_widget_set_text(editor->controls[XXWIDGETS_DIE_EDITOR_STATUS], text);
    next = xxwidgets_widget_set_enabled(editor->controls[XXWIDGETS_DIE_EDITOR_SAVE], !editor->executing && modified);
    if (status == XXWIDGETS_OK) status = next;
    next = xxwidgets_widget_set_enabled(editor->controls[XXWIDGETS_DIE_EDITOR_RUN], !editor->executing && editor->run_callback != NULL);
    if (status == XXWIDGETS_OK) status = next;
    return status;
}

xxwidgets_widget *xxwidgets_die_signature_editor_control(const xxwidgets_die_signature_editor *editor,
    xxwidgets_die_signature_editor_control_id control)
{
    return editor && control >= 0 && control < XXWIDGETS_DIE_EDITOR_CONTROL_COUNT ? editor->controls[control] : NULL;
}

xxwidgets_status xxwidgets_die_signature_editor_create(xxwidgets_widget *owner, xxwidgets_rect bounds,
    xxwidgets_die_signature_editor **out_editor)
{
    static const xxwidgets_kind kinds[XXWIDGETS_DIE_EDITOR_CONTROL_COUNT] = {
        XXWIDGETS_BUTTON, XXWIDGETS_BUTTON, XXWIDGETS_BUTTON, XXWIDGETS_CHECKBOX,
        XXWIDGETS_EDIT, XXWIDGETS_DIE_SIGNATURE_EDIT, XXWIDGETS_EDIT, XXWIDGETS_BUTTON,
        XXWIDGETS_BUTTON, XXWIDGETS_BUTTON, XXWIDGETS_DIE_SIGNATURE_EDIT, XXWIDGETS_LABEL};
    xxwidgets_die_signature_editor *editor;
    xxwidgets_rect rects[XXWIDGETS_DIE_EDITOR_CONTROL_COUNT];
    xxwidgets_status status;
    size_t i;
    if (!out_editor) return XXWIDGETS_INVALID_ARGUMENT;
    *out_editor = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !valid_bounds(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing) return XXWIDGETS_BUSY;
    editor = (xxwidgets_die_signature_editor *)calloc(1, sizeof(*editor));
    if (!editor) return XXWIDGETS_OUT_OF_MEMORY;
    editor->owner = owner;
    editor->path = copy_text("");
    if (!editor->path) { free(editor); return XXWIDGETS_OUT_OF_MEMORY; }
    rectangles(owner->app, bounds, rects);
    for (i = 0; i < XXWIDGETS_DIE_EDITOR_CONTROL_COUNT; ++i) {
        status = xxwidgets_widget_create(owner->app, owner, kinds[i], XXW_TR(owner->app, editor_labels[i]), rects[i], &editor->controls[i]);
        if (status != XXWIDGETS_OK) goto failed;
    }
    status = xxwidgets_die_signature_edit_set_read_only(editor->controls[XXWIDGETS_DIE_EDITOR_RESULT], 1);
    if (status != XXWIDGETS_OK) goto failed;
    status = xxwidgets_die_signature_editor_set_read_only(editor, 1);
    if (status != XXWIDGETS_OK) goto failed;
    status = describe(editor, "Run unavailable");
    if (status != XXWIDGETS_OK) goto failed;
    *out_editor = editor;
    return XXWIDGETS_OK;
failed:
    (void)xxwidgets_die_signature_editor_destroy(editor);
    return status;
}

xxwidgets_status xxwidgets_die_signature_editor_destroy(xxwidgets_die_signature_editor *editor)
{
    size_t i;
    if (!editor) return XXWIDGETS_INVALID_ARGUMENT;
    if (!outside_dispatch(editor)) return XXWIDGETS_BUSY;
    for (i = 0; i < XXWIDGETS_DIE_EDITOR_CONTROL_COUNT; ++i) {
        xxwidgets_status status;
        if (!editor->controls[i]) continue;
        status = xxwidgets_widget_destroy(editor->controls[i]);
        if (status != XXWIDGETS_OK) return status;
        editor->controls[i] = NULL;
    }
    free(editor->path);
    free(editor);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_die_signature_editor_set_rect(xxwidgets_die_signature_editor *editor, xxwidgets_rect bounds)
{
    xxwidgets_rect rects[XXWIDGETS_DIE_EDITOR_CONTROL_COUNT], previous[XXWIDGETS_DIE_EDITOR_CONTROL_COUNT];
    xxwidgets_status status;
    size_t i, j;
    if (!editor || !valid_bounds(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    if (editor->executing) return XXWIDGETS_BUSY;
    rectangles(editor->owner->app, bounds, rects);
    for (i = 0; i < XXWIDGETS_DIE_EDITOR_CONTROL_COUNT; ++i) previous[i] = editor->controls[i]->rect;
    for (i = 0; i < XXWIDGETS_DIE_EDITOR_CONTROL_COUNT; ++i) {
        status = xxwidgets_widget_set_rect(editor->controls[i], rects[i]);
        if (status != XXWIDGETS_OK) {
            for (j = 0; j < i; ++j) (void)xxwidgets_widget_set_rect(editor->controls[j], previous[j]);
            return status;
        }
    }
    return XXWIDGETS_OK;
}

const char *xxwidgets_die_signature_editor_source(const xxwidgets_die_signature_editor *editor)
{
    return editor ? xxwidgets_die_signature_edit_source(editor->controls[XXWIDGETS_DIE_EDITOR_SOURCE]) : NULL;
}

const char *xxwidgets_die_signature_editor_path(const xxwidgets_die_signature_editor *editor)
{
    return editor ? editor->path : NULL;
}

int xxwidgets_die_signature_editor_modified(const xxwidgets_die_signature_editor *editor)
{
    return editor && xxwidgets_die_signature_edit_modified(editor->controls[XXWIDGETS_DIE_EDITOR_SOURCE]);
}

int xxwidgets_die_signature_editor_read_only(const xxwidgets_die_signature_editor *editor)
{
    return editor && xxwidgets_die_signature_edit_read_only(editor->controls[XXWIDGETS_DIE_EDITOR_SOURCE]);
}

xxwidgets_status xxwidgets_die_signature_editor_set_read_only(xxwidgets_die_signature_editor *editor, int read_only)
{
    xxwidgets_status status;
    int previous;
    if (!editor) return XXWIDGETS_INVALID_ARGUMENT;
    if (editor->executing) return XXWIDGETS_BUSY;
    previous = xxwidgets_die_signature_editor_read_only(editor);
    status = xxwidgets_die_signature_edit_set_read_only(editor->controls[XXWIDGETS_DIE_EDITOR_SOURCE], !!read_only);
    if (status != XXWIDGETS_OK) return status;
    status = xxwidgets_widget_set_value(editor->controls[XXWIDGETS_DIE_EDITOR_READ_ONLY], !!read_only);
    if (status != XXWIDGETS_OK) {
        (void)xxwidgets_die_signature_edit_set_read_only(editor->controls[XXWIDGETS_DIE_EDITOR_SOURCE], previous);
        return status;
    }
    return describe(editor, NULL);
}

/* Restore the logical path even if backend synchronization itself fails. */
static void restore_path_display(xxwidgets_die_signature_editor *editor, char *previous)
{
    xxwidgets_widget *control = editor->controls[XXWIDGETS_DIE_EDITOR_PATH];
    free(control->text);
    control->text = previous;
    ++control->app->syncing;
    (void)control->app->ops->sync(control);
    --control->app->syncing;
}

xxwidgets_status xxwidgets_die_signature_editor_set_source(xxwidgets_die_signature_editor *editor,
    const char *source, const char *path, int discard_modified)
{
    char *new_path, *previous, *source_copy;
    xxwidgets_status status;
    if (!editor || !source || (path && !xxwidgets_valid_utf8(path))) return XXWIDGETS_INVALID_ARGUMENT;
    if (!outside_dispatch(editor) || (!discard_modified && xxwidgets_die_signature_editor_modified(editor))) return XXWIDGETS_BUSY;
    if (strlen(source) > SIGNATURE_MAX_SIZE) return XXWIDGETS_INVALID_ARGUMENT;
    new_path = copy_text(path ? path : "");
    previous = copy_text(editor->controls[XXWIDGETS_DIE_EDITOR_PATH]->text);
    source_copy = copy_text(source);
    if (!new_path || !previous || !source_copy) { free(new_path); free(previous); free(source_copy); return XXWIDGETS_OUT_OF_MEMORY; }
    status = xxwidgets_widget_set_text(editor->controls[XXWIDGETS_DIE_EDITOR_PATH], new_path);
    if (status != XXWIDGETS_OK) { free(new_path); free(previous); free(source_copy); return status; }
    status = xxwidgets_die_signature_edit_set_source(editor->controls[XXWIDGETS_DIE_EDITOR_SOURCE], source_copy);
    free(source_copy);
    if (status != XXWIDGETS_OK) {
        restore_path_display(editor, previous);
        free(new_path);
        return status;
    }
    free(previous);
    free(editor->path);
    editor->path = new_path;
    (void)describe(editor, "Document loaded");
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_die_signature_editor_open(xxwidgets_die_signature_editor *editor, const char *path, int discard_modified)
{
    xx_io_device *device;
    int64_t size;
    size_t position = 0;
    char *source;
    xxwidgets_status status = XXWIDGETS_OK;
    if (!editor || !path || !*path || !xxwidgets_valid_utf8(path)) return XXWIDGETS_INVALID_ARGUMENT;
    if (!outside_dispatch(editor) || (!discard_modified && xxwidgets_die_signature_editor_modified(editor))) return XXWIDGETS_BUSY;
    device = xx_io_file_open(path, "rb");
    if (!device) return XXWIDGETS_PLATFORM_ERROR;
    size = xx_io_total_size(device);
    if (size < 0 || size > SIGNATURE_MAX_SIZE) {
        (void)xx_io_close(device);
        return size < 0 ? XXWIDGETS_PLATFORM_ERROR : XXWIDGETS_INVALID_ARGUMENT;
    }
    source = (char *)malloc((size_t)size + 1);
    if (!source) { (void)xx_io_close(device); return XXWIDGETS_OUT_OF_MEMORY; }
    while (position < (size_t)size) {
        ssize_t count = xx_io_read(device, source + position, (size_t)size - position);
        if (count <= 0 || (size_t)count > (size_t)size - position) { status = XXWIDGETS_PLATFORM_ERROR; break; }
        position += (size_t)count;
    }
    if (status == XXWIDGETS_OK) {
        char extra;
        if (xx_io_read(device, &extra, 1) != 0) status = XXWIDGETS_PLATFORM_ERROR;
    }
    if (xx_io_close(device) != 0) status = XXWIDGETS_PLATFORM_ERROR;
    source[(size_t)size] = 0;
    if (status == XXWIDGETS_OK && memchr(source, 0, (size_t)size)) status = XXWIDGETS_INVALID_ARGUMENT;
    if (status == XXWIDGETS_OK) status = xxwidgets_die_signature_editor_set_source(editor, source, path, discard_modified);
    free(source);
    return status;
}

xxwidgets_status xxwidgets_die_signature_editor_save(xxwidgets_die_signature_editor *editor, const char *path)
{
    xx_io_device *device = NULL;
    const char *source;
    char *destination, *temporary, *previous;
    size_t length, position = 0, path_size;
    unsigned int attempt;
    xxwidgets_status status = XXWIDGETS_OK;
    if (!editor) return XXWIDGETS_INVALID_ARGUMENT;
    if (!outside_dispatch(editor)) return XXWIDGETS_BUSY;
    if (!path) path = editor->path;
    if (!path || !*path || !xxwidgets_valid_utf8(path)) return XXWIDGETS_INVALID_ARGUMENT;
    path_size = strlen(path);
    if (path_size > SIZE_MAX - 100) return XXWIDGETS_INVALID_ARGUMENT;
    destination = copy_text(path);
    temporary = (char *)malloc(path_size + 100);
    previous = copy_text(editor->controls[XXWIDGETS_DIE_EDITOR_PATH]->text);
    if (!destination || !temporary || !previous) {
        free(destination); free(temporary); free(previous); return XXWIDGETS_OUT_OF_MEMORY;
    }
    /* Stage the visible destination before any filesystem mutation. */
    status = xxwidgets_widget_set_text(editor->controls[XXWIDGETS_DIE_EDITOR_PATH], destination);
    if (status != XXWIDGETS_OK) goto finish;
    for (attempt = 0; attempt < 100; ++attempt) {
        snprintf(temporary, path_size + 100, "%s.xxwidgets-signature-%" PRIxPTR "-%" PRIu64 ".tmp",
            destination, (uintptr_t)editor, ++editor->temp_sequence);
        device = xx_io_file_open(temporary, "wbx");
        if (device) break;
    }
    if (!device) { status = XXWIDGETS_PLATFORM_ERROR; goto restore; }
    source = xxwidgets_die_signature_editor_source(editor);
    length = strlen(source);
    while (position < length) {
        ssize_t written = xx_io_write(device, source + position, length - position);
        if (written <= 0 || (size_t)written > length - position) { status = XXWIDGETS_PLATFORM_ERROR; break; }
        position += (size_t)written;
    }
    if (xx_io_close(device) != 0) status = XXWIDGETS_PLATFORM_ERROR;
    device = NULL;
    if (status == XXWIDGETS_OK && !xx_io_file_replace_a(temporary, destination, true)) status = XXWIDGETS_PLATFORM_ERROR;
    if (status != XXWIDGETS_OK) {
        (void)xx_io_file_remove_a(temporary);
        goto restore;
    }
    free(editor->path);
    editor->path = destination;
    destination = NULL;
    status = xxwidgets_die_signature_edit_mark_saved(editor->controls[XXWIDGETS_DIE_EDITOR_SOURCE]);
    (void)describe(editor, status == XXWIDGETS_OK ? "Saved" : "File saved; could not record the savepoint");
    goto finish;
restore:
    restore_path_display(editor, previous);
    previous = NULL;
    (void)describe(editor, "Save failed; source retained");
finish:
    free(previous); free(destination); free(temporary);
    return status;
}

xxwidgets_status xxwidgets_die_signature_editor_find(xxwidgets_die_signature_editor *editor,
    const char *text, int match_case, int backwards)
{
    xxwidgets_status status;
    if (!editor || !text || !*text) return XXWIDGETS_INVALID_ARGUMENT;
    if (editor->executing) return XXWIDGETS_BUSY;
    status = xxwidgets_die_signature_edit_find(editor->controls[XXWIDGETS_DIE_EDITOR_SOURCE], text, match_case, backwards);
    (void)describe(editor, status == XXWIDGETS_UNAVAILABLE ? "Text not found" : status == XXWIDGETS_OK ? "Match selected" : "Search failed");
    return status;
}

xxwidgets_status xxwidgets_die_signature_editor_set_result(xxwidgets_die_signature_editor *editor, const char *text)
{
    if (!editor || !text) return XXWIDGETS_INVALID_ARGUMENT;
    return xxwidgets_die_signature_edit_set_source(editor->controls[XXWIDGETS_DIE_EDITOR_RESULT], text);
}

xxwidgets_status xxwidgets_die_signature_editor_clear_result(xxwidgets_die_signature_editor *editor)
{
    return xxwidgets_die_signature_editor_set_result(editor, "");
}

typedef struct editor_dialog {
    xxwidgets_widget *window, *close, *list; /* list: the Signatures window only */
    xxwidgets_die_signature_editor *editor;
    int close_requested, relayout, warned, done, list_width, pending_select;
} editor_dialog;

static void editor_dialog_layout(editor_dialog *state)
{
    xxwidgets_rect bounds;
    int left = state->list ? state->list_width + 2 : 1;
    if (xxwidgets_widget_get_rect(state->window, &bounds) != XXWIDGETS_OK || bounds.width < left + 73 || bounds.height < 25) return;
    if (state->list) (void)xxwidgets_widget_set_rect(state->list, (xxwidgets_rect){1, 1, state->list_width, bounds.height - 4});
    (void)xxwidgets_die_signature_editor_set_rect(state->editor, (xxwidgets_rect){left, 1, bounds.width - left - 1, bounds.height - 4});
    (void)xxwidgets_widget_set_rect(state->close, (xxwidgets_rect){bounds.width - 13, bounds.height - 3, 12, 2});
}

static void editor_dialog_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    editor_dialog *state = (editor_dialog *)user;
    (void)app;
    if ((event->type == XXWIDGETS_EVENT_CLOSE && event->widget == state->window) ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == state->close) ||
        (event->type == XXWIDGETS_EVENT_SHORTCUT && event->value == 1)) {
        state->close_requested = 1;
        return;
    }
    if (event->type == XXWIDGETS_EVENT_RESIZE && event->widget == state->window) {
        state->relayout = 1;
        return;
    }
    if (state->list && event->widget == state->list && event->type == XXWIDGETS_EVENT_SELECT) {
        state->pending_select = event->value;
        return;
    }
    if (!state->editor) return;
    if (event->type == XXWIDGETS_EVENT_SHORTCUT) {
        xxwidgets_event action = {0};
        if (event->value == 2) {
            (void)xxwidgets_widget_focus(state->editor->controls[XXWIDGETS_DIE_EDITOR_FIND_TEXT]);
            return;
        }
        action.type = XXWIDGETS_EVENT_CLICK;
        action.widget = state->editor->controls[event->value == 3 ? XXWIDGETS_DIE_EDITOR_FIND_NEXT : XXWIDGETS_DIE_EDITOR_SAVE];
        (void)xxwidgets_die_signature_editor_handle_event(state->editor, &action);
        return;
    }
    (void)xxwidgets_die_signature_editor_handle_event(state->editor, event);
}

typedef struct catalog {
    const char *const *names;
    size_t count, current;
    xxwidgets_die_signature_load_fn load;
    void *user;
} catalog;

/* Puts entry index in the editor; the list follows what is shown. */
static xxwidgets_status catalog_show(editor_dialog *state, catalog *entries, size_t index)
{
    char *source = NULL;
    const char *path = NULL;
    xxwidgets_status status = entries->load(index, &source, &path, entries->user);
    if (status == XXWIDGETS_OK && !source) status = XXWIDGETS_INVALID_ARGUMENT;
    if (status == XXWIDGETS_OK) status = xxwidgets_die_signature_editor_set_source(state->editor, source, path, 1);
    free(source);
    if (status != XXWIDGETS_OK) return status;
    entries->current = index;
    state->warned = 0;
    return state->list ? xxwidgets_widget_set_value(state->list, (int)index) : XXWIDGETS_OK;
}

static xxwidgets_status editor_dialog_run(xxwidgets_widget *owner, const char *title, const char *source, const char *path,
    catalog *entries, xxwidgets_die_signature_editor_run_fn run, void *user_data)
{
    const xxwidgets_shortcut shortcuts[] = {{"Esc", 1}, {"Ctrl+F", 2}, {"Meta+F", 2}, {"F3", 3}, {"Ctrl+S", 4}, {"Meta+S", 4}};
    editor_dialog state = {0};
    xxwidgets_app *app;
    xxwidgets_widget *focus = NULL, *widget, *previous_default;
    xxwidgets_event_fn previous_event;
    void *previous_user;
    xxwidgets_status status, cleanup;
    int width = 100, height = 36, owner_enabled, disabled = 0, attached = 0, left = 1;
    size_t i;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !title || !xxwidgets_valid_utf8(title) || (!source && !entries)) return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit) return XXWIDGETS_BUSY;
    if (xxwidgets_app_backend(app) == XXWIDGETS_BACKEND_TUI) {
        /* A terminal has one screen: take what the owner has, at least the minimum. */
        width = owner->rect.width - 4 > 74 ? owner->rect.width - 4 : 74;
        height = owner->rect.height - 2 > 25 ? owner->rect.height - 2 : 25;
    }
    if (entries && (xxwidgets_app_backend(app) != XXWIDGETS_BACKEND_TUI || width >= 100)) {
        state.list_width = xxwidgets_app_backend(app) == XXWIDGETS_BACKEND_TUI ? width / 4 : 32;
        left = state.list_width + 2;
        if (xxwidgets_app_backend(app) != XXWIDGETS_BACKEND_TUI) width += left - 1;
    }
    state.pending_select = -1;
    if (owner->rect.x > INT_MAX - width - 2 || owner->rect.y > INT_MAX - height - 1) return XXWIDGETS_INVALID_ARGUMENT;
    previous_event = app->on_event;
    previous_user = app->user_data;
    previous_default = app->modal_default;
    owner_enabled = owner->enabled;
    for (widget = app->widgets; widget; widget = widget->next)
        if (widget->parent == owner && xxwidgets_focusable(widget)) {
            focus = widget;
            break;
        }
    app->on_event = editor_dialog_event;
    app->user_data = &state;
    app->modal_default = NULL;
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title,
        (xxwidgets_rect){owner->rect.x + 2, owner->rect.y + 1, width, height}, &state.window);
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_window = state.window;
    status = xxwidgets_window_set_minimum_size(state.window, left + 73, 25);
    if (status == XXWIDGETS_OK && state.list_width)
        status = xxwidgets_widget_create(app, state.window, XXWIDGETS_LISTBOX, "", (xxwidgets_rect){1, 1, state.list_width, height - 4}, &state.list);
    for (i = 0; status == XXWIDGETS_OK && state.list && i < entries->count; ++i) status = xxwidgets_listbox_add(state.list, entries->names[i]);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON, XXW_TR(app, "Close"),
            (xxwidgets_rect){width - 13, height - 3, 12, 2}, &state.close);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_die_signature_editor_create(state.window, (xxwidgets_rect){left, 1, width - left - 1, height - 4}, &state.editor);
    if (status == XXWIDGETS_OK)
        status = entries ? catalog_show(&state, entries, entries->current) : xxwidgets_die_signature_editor_set_source(state.editor, source, path, 1);
    if (status == XXWIDGETS_OK && run) status = xxwidgets_die_signature_editor_set_run_callback(state.editor, run, user_data);
    if (status == XXWIDGETS_OK) status = xxwidgets_window_set_shortcuts(state.window, shortcuts, sizeof(shortcuts) / sizeof(shortcuts[0]));
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_default = state.close;
    status = xxwidgets_widget_set_enabled(owner, 0);
    if (status != XXWIDGETS_OK) goto finish;
    disabled = 1;
    if (app->ops->modal_owner) {
        status = app->ops->modal_owner(state.window, owner, 1);
        if (status != XXWIDGETS_OK) goto finish;
        attached = 1;
    }
    status = xxwidgets_widget_focus(state.editor->controls[XXWIDGETS_DIE_EDITOR_SOURCE]);
    while (status == XXWIDGETS_OK && !state.done && !app->quit) {
        status = xxwidgets_app_poll(app, 30);
        if (status != XXWIDGETS_OK || app->quit) break;
        if (state.relayout) {
            state.relayout = 0;
            editor_dialog_layout(&state);
        }
        if (state.close_requested) {
            state.close_requested = 0;
            if (!xxwidgets_die_signature_editor_modified(state.editor) || state.warned) {
                state.done = 1;
                break;
            }
            state.warned = 1;
            (void)xxwidgets_widget_set_text(state.editor->controls[XXWIDGETS_DIE_EDITOR_STATUS],
                XXW_TR(app, "Unsaved changes. Close again to discard them."));
        }
        if (state.pending_select >= 0) {
            size_t chosen = (size_t)state.pending_select;
            state.pending_select = -1;
            if (chosen < entries->count && chosen != entries->current) {
                if (xxwidgets_die_signature_editor_modified(state.editor) && !state.warned) {
                    /* Keep the draft once: the list goes back to it. */
                    state.warned = 1;
                    (void)xxwidgets_widget_set_value(state.list, (int)entries->current);
                    (void)xxwidgets_widget_set_text(state.editor->controls[XXWIDGETS_DIE_EDITOR_STATUS],
                        XXW_TR(app, "Unsaved changes. Choose again to discard them."));
                } else if (catalog_show(&state, entries, chosen) != XXWIDGETS_OK)
                    (void)xxwidgets_widget_set_value(state.list, (int)entries->current);
            }
        }
        (void)xxwidgets_die_signature_editor_process_pending(state.editor);
    }
finish:
    if (disabled) {
        cleanup = xxwidgets_widget_set_enabled(owner, owner_enabled);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (attached) {
        cleanup = app->ops->modal_owner(state.window, owner, 0);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (state.editor) {
        cleanup = xxwidgets_die_signature_editor_destroy(state.editor);
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
    if (focus) (void)xxwidgets_widget_focus(focus);
    return status;
}

xxwidgets_status xxwidgets_die_signature_editor_dialog(xxwidgets_widget *owner, const char *title,
    const char *source, const char *path, xxwidgets_die_signature_editor_run_fn run, void *user_data)
{
    return editor_dialog_run(owner, title, source, path, NULL, run, user_data);
}

xxwidgets_status xxwidgets_die_signatures_dialog(xxwidgets_widget *owner, const char *title,
    const char *const *names, size_t count, size_t selected, xxwidgets_die_signature_load_fn load,
    xxwidgets_die_signature_editor_run_fn run, void *user_data)
{
    catalog entries;
    size_t i;
    if (!names || !count || count > INT_MAX || selected >= count || !load) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < count; ++i) if (!names[i] || !xxwidgets_valid_utf8(names[i])) return XXWIDGETS_INVALID_ARGUMENT;
    entries.names = names; entries.count = count; entries.current = selected;
    entries.load = load; entries.user = user_data;
    return editor_dialog_run(owner, title, NULL, NULL, &entries, run, user_data);
}

xxwidgets_status xxwidgets_die_signature_editor_set_run_callback(xxwidgets_die_signature_editor *editor,
    xxwidgets_die_signature_editor_run_fn callback, void *user_data)
{
    if (!editor) return XXWIDGETS_INVALID_ARGUMENT;
    if (editor->executing) return XXWIDGETS_BUSY;
    editor->run_callback = callback;
    editor->run_user = callback ? user_data : NULL;
    return describe(editor, callback ? "Ready to run" : "Run unavailable");
}

xxwidgets_status xxwidgets_die_signature_editor_run(xxwidgets_die_signature_editor *editor)
{
    xxwidgets_status status, result_status;
    char *result = NULL;
    if (!editor) return XXWIDGETS_INVALID_ARGUMENT;
    if (!outside_dispatch(editor)) return XXWIDGETS_BUSY;
    if (!editor->run_callback) return XXWIDGETS_UNAVAILABLE;
    status = xxwidgets_die_signature_editor_clear_result(editor);
    if (status != XXWIDGETS_OK) return status;
    editor->executing = 1;
    (void)describe(editor, "Running");
    status = editor->run_callback(xxwidgets_die_signature_editor_source(editor), editor->path, &result, editor->run_user);
    editor->executing = 0;
    result_status = result ? xxwidgets_die_signature_editor_set_result(editor, result) : XXWIDGETS_OK;
    free(result);
    if (status == XXWIDGETS_OK) status = result_status;
    (void)describe(editor, status == XXWIDGETS_OK ? "Run complete" : "Run failed");
    return status;
}

xxwidgets_status xxwidgets_die_signature_editor_handle_event(xxwidgets_die_signature_editor *editor, const xxwidgets_event *event)
{
    int value;
    if (!editor || !event) return XXWIDGETS_INVALID_ARGUMENT;
    if (editor->executing) return XXWIDGETS_BUSY;
    if (event->widget == editor->controls[XXWIDGETS_DIE_EDITOR_SOURCE] &&
        (event->type == XXWIDGETS_EVENT_CHANGE || event->type == XXWIDGETS_EVENT_SELECT)) return describe(editor, "");
    if (event->widget == editor->controls[XXWIDGETS_DIE_EDITOR_READ_ONLY] &&
        (event->type == XXWIDGETS_EVENT_CLICK || event->type == XXWIDGETS_EVENT_CHANGE)) {
        xxwidgets_status status = xxwidgets_widget_get_value(event->widget, &value);
        return status == XXWIDGETS_OK ? xxwidgets_die_signature_editor_set_read_only(editor, value) : status;
    }
    if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == editor->controls[XXWIDGETS_DIE_EDITOR_OPEN]) editor->pending = XXWIDGETS_DIE_EDITOR_OPEN + 1;
        else if (event->widget == editor->controls[XXWIDGETS_DIE_EDITOR_SAVE]) editor->pending = XXWIDGETS_DIE_EDITOR_SAVE + 1;
        else if (event->widget == editor->controls[XXWIDGETS_DIE_EDITOR_SAVE_AS]) editor->pending = XXWIDGETS_DIE_EDITOR_SAVE_AS + 1;
        else if (event->widget == editor->controls[XXWIDGETS_DIE_EDITOR_FIND_NEXT]) editor->pending = XXWIDGETS_DIE_EDITOR_FIND_NEXT + 1;
        else if (event->widget == editor->controls[XXWIDGETS_DIE_EDITOR_RUN]) editor->pending = XXWIDGETS_DIE_EDITOR_RUN + 1;
        else if (event->widget == editor->controls[XXWIDGETS_DIE_EDITOR_CLEAR_RESULT]) editor->pending = XXWIDGETS_DIE_EDITOR_CLEAR_RESULT + 1;
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_die_signature_editor_process_pending(xxwidgets_die_signature_editor *editor)
{
    xxwidgets_status status = XXWIDGETS_OK;
    char *path = NULL;
    int action, accepted = 0, discard_modified = 0;
    if (!editor) return XXWIDGETS_INVALID_ARGUMENT;
    if (!outside_dispatch(editor)) return XXWIDGETS_BUSY;
    action = editor->pending - 1;
    editor->pending = 0;
    if (action < 0) return XXWIDGETS_OK;
    if (action == XXWIDGETS_DIE_EDITOR_OPEN || action == XXWIDGETS_DIE_EDITOR_SAVE_AS ||
        (action == XXWIDGETS_DIE_EDITOR_SAVE && !editor->path[0])) {
        if (action == XXWIDGETS_DIE_EDITOR_OPEN && xxwidgets_die_signature_editor_modified(editor)) {
            xxwidgets_option discard = {"Discard the unsaved signature changes", 0};
            status = xxwidgets_options_dialog(editor->owner, "Unsaved signature", &discard, 1, &accepted);
            if (status != XXWIDGETS_OK || !accepted || !discard.value) return status;
            discard_modified = 1;
            accepted = 0;
        }
        if (xxwidgets_file_dialog_available(editor->owner->app)) {
            status = xxwidgets_file_dialog(editor->owner,
                action == XXWIDGETS_DIE_EDITOR_OPEN ? XXWIDGETS_FILE_DIALOG_OPEN : XXWIDGETS_FILE_DIALOG_SAVE,
                action == XXWIDGETS_DIE_EDITOR_OPEN ? "Open DIE signature" : "Save DIE signature",
                editor->controls[XXWIDGETS_DIE_EDITOR_PATH]->text, &path, &accepted);
            if (status != XXWIDGETS_OK || !accepted) { free(path); return status; }
        } else {
            path = copy_text(editor->controls[XXWIDGETS_DIE_EDITOR_PATH]->text);
            if (!path) return XXWIDGETS_OUT_OF_MEMORY;
            if (!*path) { free(path); (void)describe(editor, "Enter a file path above first"); return XXWIDGETS_INVALID_ARGUMENT; }
            if (action != XXWIDGETS_DIE_EDITOR_OPEN && xx_io_file_exists_a(path)) {
                xxwidgets_option replace = {"Replace the existing file", 0};
                status = xxwidgets_options_dialog(editor->owner, "Confirm signature replacement", &replace, 1, &accepted);
                if (status != XXWIDGETS_OK || !accepted || !replace.value) { free(path); return status; }
            }
        }
        status = action == XXWIDGETS_DIE_EDITOR_OPEN ? xxwidgets_die_signature_editor_open(editor, path, discard_modified) :
            xxwidgets_die_signature_editor_save(editor, path);
        free(path);
    } else if (action == XXWIDGETS_DIE_EDITOR_SAVE) status = xxwidgets_die_signature_editor_save(editor, NULL);
    else if (action == XXWIDGETS_DIE_EDITOR_FIND_NEXT) status = xxwidgets_die_signature_editor_find(editor,
        editor->controls[XXWIDGETS_DIE_EDITOR_FIND_TEXT]->text, 0, 0);
    else if (action == XXWIDGETS_DIE_EDITOR_RUN) status = xxwidgets_die_signature_editor_run(editor);
    else if (action == XXWIDGETS_DIE_EDITOR_CLEAR_RESULT) status = xxwidgets_die_signature_editor_clear_result(editor);
    if (status != XXWIDGETS_OK && !(action == XXWIDGETS_DIE_EDITOR_FIND_NEXT && status == XXWIDGETS_UNAVAILABLE)) {
        char message[160];
        snprintf(message, sizeof(message), "%s; document retained", xxwidgets_status_string(status));
        (void)describe(editor, message);
    }
    return status;
}
