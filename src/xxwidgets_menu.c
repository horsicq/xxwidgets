/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include <stdlib.h>
#include <string.h>

static void free_items(xxwidgets_menu_item *items, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        free((void *)items[i].menu);
        free((void *)items[i].label);
    }
    free(items);
}

void xxwidgets_menu_dispose(xxwidgets_widget *window)
{
    free_items(window->menu_items, window->menu_count);
    window->menu_items = NULL;
    window->menu_count = 0;
}

static char *copy_text(const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);
    if (copy) memcpy(copy, text, size);
    return copy;
}

xxwidgets_status xxwidgets_window_set_menu(xxwidgets_widget *window, const xxwidgets_menu_item *items, size_t count)
{
    xxwidgets_menu_item *copy = NULL, *previous;
    size_t previous_count;
    xxwidgets_status status;
    if (!window || window->kind != XXWIDGETS_WINDOW || (!items && count) || count > 4096)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (window->app->dispatch_depth || window->app->syncing || window->app->quit) return XXWIDGETS_BUSY;
    if (!window->app->ops->window_menu) return XXWIDGETS_UNAVAILABLE;
    for (size_t i = 0; i < count; ++i) {
        if (!items[i].menu || !*items[i].menu || !xxwidgets_valid_utf8(items[i].menu) ||
            (items[i].label && !xxwidgets_valid_utf8(items[i].label))) return XXWIDGETS_INVALID_ARGUMENT;
        if (items[i].label && *items[i].label) {
            if (!items[i].action || (items[i].enabled != 0 && items[i].enabled != 1)) return XXWIDGETS_INVALID_ARGUMENT;
            for (size_t j = 0; j < i; ++j)
                if (items[j].label && *items[j].label && items[j].action == items[i].action) return XXWIDGETS_INVALID_ARGUMENT;
        }
        /* A group has one contiguous span, preserving the supplied order. */
        if (i && strcmp(items[i].menu, items[i - 1].menu))
            for (size_t j = 0; j + 1 < i; ++j)
                if (!strcmp(items[j].menu, items[i].menu)) return XXWIDGETS_INVALID_ARGUMENT;
    }
    if (count) {
        copy = calloc(count, sizeof(*copy));
        if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
        for (size_t i = 0; i < count; ++i) {
            copy[i] = items[i];
            copy[i].menu = copy_text(items[i].menu);
            copy[i].label = copy_text(items[i].label ? items[i].label : "");
            if (!copy[i].menu || !copy[i].label) { free_items(copy, i + 1); return XXWIDGETS_OUT_OF_MEMORY; }
        }
    }
    previous = window->menu_items;
    previous_count = window->menu_count;
    window->menu_items = copy;
    window->menu_count = count;
    ++window->app->syncing;
    status = window->app->ops->window_menu(window);
    --window->app->syncing;
    if (status != XXWIDGETS_OK) {
        window->menu_items = previous;
        window->menu_count = previous_count;
        free_items(copy, count);
    } else free_items(previous, previous_count);
    return status;
}

xxwidgets_status xxwidgets_window_set_menu_item_enabled(xxwidgets_widget *window, int action, int enabled)
{
    if (!window || window->kind != XXWIDGETS_WINDOW || (enabled != 0 && enabled != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    if (window->app->dispatch_depth || window->app->syncing || window->app->quit) return XXWIDGETS_BUSY;
    if (!window->app->ops->window_menu) return XXWIDGETS_UNAVAILABLE;
    for (size_t i = 0; i < window->menu_count; ++i) {
        xxwidgets_menu_item *item = &window->menu_items[i];
        if (item->action != action || !*item->label) continue;
        if (item->enabled == enabled) return XXWIDGETS_OK;
        int previous = item->enabled;
        item->enabled = enabled;
        ++window->app->syncing;
        xxwidgets_status status = window->app->ops->window_menu(window);
        --window->app->syncing;
        if (status != XXWIDGETS_OK) item->enabled = previous;
        return status;
    }
    return XXWIDGETS_INVALID_ARGUMENT;
}

int xxwidgets_menu_can_activate(const xxwidgets_widget *window, size_t index)
{
    return window && window->kind == XXWIDGETS_WINDOW && index < window->menu_count &&
        window->visible && window->enabled && !window->app->quit && !window->app->syncing &&
        (!window->app->modal_window || window->app->modal_window == window) &&
        *window->menu_items[index].label && window->menu_items[index].enabled;
}

void xxwidgets_menu_activate(xxwidgets_widget *window, size_t index)
{
    if (xxwidgets_menu_can_activate(window, index))
        xxwidgets_emit(window, XXWIDGETS_EVENT_SHORTCUT, window->menu_items[index].action);
}
