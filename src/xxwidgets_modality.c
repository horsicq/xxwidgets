/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"

static int owned_by(const xxwidgets_widget *window, const xxwidgets_widget *ancestor)
{
    for (; window; window = window->modality_owner)
        if (window == ancestor) return 1;
    return 0;
}

int xxwidgets_modality_enabled(const xxwidgets_widget *window)
{
    const xxwidgets_widget *modal;
    if (!window->requested_enabled) return 0;
    for (modal = window->app->widgets; modal; modal = modal->next) {
        if (modal->kind != XXWIDGETS_WINDOW || modal->modality == XXWIDGETS_MODELESS || owned_by(window, modal)) continue;
        if (modal->modality == XXWIDGETS_APPLICATION_MODAL || modal->modality_owner == window) return 0;
    }
    return 1;
}

/* Keep the caller's enabled request separate from the effective native state.
 * Recompute instead of saving pointers to blocked windows: nested scopes,
 * newly created windows, and destroyed peers then follow the same rules. */
xxwidgets_status xxwidgets_modality_refresh(xxwidgets_app *app)
{
    xxwidgets_widget *window;
    xxwidgets_status first = XXWIDGETS_OK, status;
    for (window = app->widgets; window; window = window->next) {
        int enabled, previous;
        if (window->kind != XXWIDGETS_WINDOW) continue;
        enabled = xxwidgets_modality_enabled(window);
        previous = window->enabled;
        if (previous == enabled) continue;
        window->enabled = enabled;
        ++app->syncing;
        status = app->ops->sync(window);
        if (status != XXWIDGETS_OK) {
            window->enabled = previous;
            (void)app->ops->sync(window);
            if (first == XXWIDGETS_OK) first = status;
        }
        --app->syncing;
    }
    return first;
}

static xxwidgets_status native_owner(xxwidgets_widget *window, xxwidgets_widget *owner, int active)
{
    xxwidgets_status status;
    if (!owner || !window->app->ops->modal_owner) return XXWIDGETS_OK;
    ++window->app->syncing;
    status = window->app->ops->modal_owner(window, owner, active);
    --window->app->syncing;
    return status;
}

static xxwidgets_status set_modality(xxwidgets_widget *window, xxwidgets_widget *owner, xxwidgets_modality modality)
{
    xxwidgets_widget *old_owner = window->modality_owner;
    xxwidgets_modality old = window->modality;
    xxwidgets_status status;
    int detached = 0, attaching = 0;
    if (old == modality && old_owner == owner) return XXWIDGETS_OK;
    window->modality = modality;
    window->modality_owner = owner;
    status = xxwidgets_modality_refresh(window->app);
    if (status == XXWIDGETS_OK && old != XXWIDGETS_MODELESS) {
        status = native_owner(window, old_owner, 0);
        detached = status == XXWIDGETS_OK;
    }
    if (status == XXWIDGETS_OK && modality != XXWIDGETS_MODELESS) {
        attaching = 1;
        status = native_owner(window, owner, 1);
    }
    if (status == XXWIDGETS_OK) return status;
    if (attaching) (void)native_owner(window, owner, 0);
    window->modality = old;
    window->modality_owner = old_owner;
    (void)xxwidgets_modality_refresh(window->app);
    if (detached) (void)native_owner(window, old_owner, 1);
    return status;
}

xxwidgets_modality xxwidgets_window_get_modality(const xxwidgets_widget *window)
{
    return window && window->kind == XXWIDGETS_WINDOW ? window->modality : XXWIDGETS_MODELESS;
}

xxwidgets_status xxwidgets_window_set_modality(xxwidgets_widget *window, xxwidgets_widget *owner, xxwidgets_modality modality)
{
    xxwidgets_widget *other;
    xxwidgets_app *app;
    if (!window || window->kind != XXWIDGETS_WINDOW || modality < XXWIDGETS_MODELESS || modality > XXWIDGETS_APPLICATION_MODAL ||
        (modality == XXWIDGETS_MODELESS ? owner != NULL :
         (!owner || owner == window || owner->kind != XXWIDGETS_WINDOW || owner->app != window->app || owned_by(owner, window))))
        return XXWIDGETS_INVALID_ARGUMENT;
    app = window->app;
    if (app->dispatch_depth || app->polling || app->syncing || (app->quit && modality != XXWIDGETS_MODELESS)) return XXWIDGETS_BUSY;
    if (window->modality == modality && window->modality_owner == owner) return XXWIDGETS_OK;
    for (other = app->widgets; other; other = other->next) {
        if (other == window || other->kind != XXWIDGETS_WINDOW || other->modality == XXWIDGETS_MODELESS) continue;
        /* End nested scopes from the inside out. Independent window-modal
         * dialogs may coexist, but application-modal scopes form one chain. */
        if (owned_by(other, window)) return XXWIDGETS_BUSY;
        if (modality != XXWIDGETS_MODELESS &&
            ((other->modality == XXWIDGETS_APPLICATION_MODAL && !owned_by(owner, other)) ||
             (other->modality == XXWIDGETS_WINDOW_MODAL && other->modality_owner == owner))) return XXWIDGETS_BUSY;
    }
    return set_modality(window, owner, modality);
}

xxwidgets_status xxwidgets_modality_release(xxwidgets_widget *window)
{
    xxwidgets_widget *other;
    xxwidgets_status status;
    for (other = window->app->widgets; other; other = other->next) {
        if (other->kind != XXWIDGETS_WINDOW || other->modality_owner != window) continue;
        status = xxwidgets_modality_release(other);
        if (status != XXWIDGETS_OK) return status;
    }
    return set_modality(window, NULL, XXWIDGETS_MODELESS);
}
