#include "xxwidgets/xxwidgets_settings.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

xxwidgets_status xxwidgets_settings_install_shortcuts(xxwidgets_widget *window,
    const xx_shortcuts *shortcuts, const xxwidgets_shortcut_action *actions, size_t count)
{
    xxwidgets_shortcut *bindings;
    size_t length = xx_shortcuts_count(shortcuts), used = 0;
    xxwidgets_status status;
    if (!shortcuts || (!actions && count) || length > SIZE_MAX / sizeof(*bindings))
        return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i) {
        if (!actions[i].name || !actions[i].name[0]) return XXWIDGETS_INVALID_ARGUMENT;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(actions[i].name, actions[j].name)) return XXWIDGETS_INVALID_ARGUMENT;
    }
    bindings = length ? (xxwidgets_shortcut *)malloc(length * sizeof(*bindings)) : NULL;
    if (length && !bindings) return XXWIDGETS_OUT_OF_MEMORY;
    for (size_t i = 0; i < length; ++i) {
        const xx_shortcut *item = xx_shortcuts_at(shortcuts, i);
        for (size_t j = 0; j < count; ++j) if (!strcmp(item->action, actions[j].name)) {
            bindings[used].sequence = item->sequence;
            bindings[used++].action = actions[j].action;
            break;
        }
    }
    status = xxwidgets_window_set_shortcuts(window, bindings, used);
    free(bindings);
    return status;
}

static xxwidgets_status settings_status(xxfc_status_t status)
{
    if (status == XXFC_OK) return XXWIDGETS_OK;
    if (status == XXFC_ERR_OUT_OF_MEMORY) return XXWIDGETS_OUT_OF_MEMORY;
    if (status == XXFC_ERR_INVALID_ARG || status == XXFC_ERR_NULL_PARAM)
        return XXWIDGETS_INVALID_ARGUMENT;
    return XXWIDGETS_PLATFORM_ERROR;
}

int xxwidgets_settings_get_bool(const xx_settings *settings, const char *key,
    int default_value)
{
    const xx_settings_value *value = xx_settings_get(settings, key);
    return value && value->type == XX_SETTINGS_VALUE_BOOL ?
        value->data.boolean : !!default_value;
}

/* Deep-copy previous values before displaying or changing anything. Setting
 * a default in the scratch store also validates each key through xxfclib. */
static xxwidgets_status snapshot_options(xx_settings *settings,
    const xxwidgets_setting_option *options, size_t count, xx_settings **out)
{
    xx_settings *previous;
    size_t i, j;
    xxfc_status_t status = XXFC_OK;
    *out = NULL;
    if (!settings || (!options && count) || count > 16)
        return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < count; ++i) {
        if (!options[i].label || !options[i].key ||
            (options[i].default_value != 0 && options[i].default_value != 1))
            return XXWIDGETS_INVALID_ARGUMENT;
        for (j = 0; j < i; ++j)
            if (!strcmp(options[i].key, options[j].key)) return XXWIDGETS_INVALID_ARGUMENT;
    }
    previous = xx_settings_create_memory();
    if (!previous) return XXWIDGETS_OUT_OF_MEMORY;
    for (i = 0; i < count && status == XXFC_OK; ++i) {
        const xx_settings_value *value = xx_settings_get(settings, options[i].key);
        xx_settings_value fallback = {0};
        fallback.type = XX_SETTINGS_VALUE_BOOL;
        fallback.data.boolean = options[i].default_value != 0;
        status = xx_settings_set(previous, options[i].key, value ? value : &fallback);
        if (status == XXFC_OK && !value) status = xx_settings_remove(previous, options[i].key);
    }
    if (status != XXFC_OK) { xx_settings_destroy(previous); return settings_status(status); }
    *out = previous;
    return XXWIDGETS_OK;
}

static xxwidgets_status save_options(xx_settings *settings, xx_settings *previous,
    const xxwidgets_setting_option *options, const xxwidgets_option *values, size_t count)
{
    size_t i;
    unsigned int changed = 0;
    xxfc_status_t status = XXFC_OK;
    for (i = 0; i < count; ++i) {
        const xx_settings_value *old = xx_settings_get(settings, options[i].key);
        xx_settings_value value = {0};
        value.type = XX_SETTINGS_VALUE_BOOL;
        value.data.boolean = values[i].value != 0;
        if (old && old->type == XX_SETTINGS_VALUE_BOOL && old->data.boolean == value.data.boolean)
            continue;
        status = xx_settings_set(settings, options[i].key, &value);
        if (status != XXFC_OK) break;
        changed |= 1u << i;
    }
    if (status == XXFC_OK && count) status = xx_settings_save(settings);
    if (status != XXFC_OK) {
        for (i = 0; i < count; ++i) {
            const xx_settings_value *old = xx_settings_get(previous, options[i].key);
            if (!(changed & (1u << i))) continue;
            if (old) xx_settings_set(settings, options[i].key, old);
            else xx_settings_remove(settings, options[i].key);
        }
    }
    return settings_status(status);
}

xxwidgets_status xxwidgets_settings_set_bool(xx_settings *settings,
    const char *key, int value)
{
    const xxwidgets_setting_option option = {"", key, 0};
    const xxwidgets_option current = {"", value};
    xx_settings *previous = NULL;
    xxwidgets_status status;
    if (value != 0 && value != 1) return XXWIDGETS_INVALID_ARGUMENT;
    status = snapshot_options(settings, &option, 1, &previous);
    if (status == XXWIDGETS_OK) status = save_options(settings, previous, &option, &current, 1);
    xx_settings_destroy(previous);
    return status;
}

xxwidgets_status xxwidgets_settings_options_dialog(xxwidgets_widget *owner,
    const char *title, xx_settings *settings,
    const xxwidgets_setting_option *options, size_t count, int *accepted)
{
    xxwidgets_option values[16];
    xx_settings *previous = NULL;
    xxwidgets_status status;
    size_t i;
    int chosen = 0;
    if (!accepted) return XXWIDGETS_INVALID_ARGUMENT;
    *accepted = 0;
    status = snapshot_options(settings, options, count, &previous);
    if (status != XXWIDGETS_OK) return status;
    for (i = 0; i < count; ++i) {
        values[i].label = options[i].label;
        values[i].value = xxwidgets_settings_get_bool(settings, options[i].key, options[i].default_value);
    }
    status = xxwidgets_options_dialog(owner, title, values, count, &chosen);
    if (status == XXWIDGETS_OK && chosen) {
        status = save_options(settings, previous, options, values, count);
        if (status == XXWIDGETS_OK) *accepted = 1;
    }
    xx_settings_destroy(previous);
    return status;
}
