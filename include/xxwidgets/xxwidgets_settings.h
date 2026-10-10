#ifndef XXWIDGETS_SETTINGS_H
#define XXWIDGETS_SETTINGS_H

#include "xxwidgets.h"
#include "xxfclib/settings/xx_settings.h"
#include "xxfclib/settings/xx_shortcuts.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_setting_option {
    const char *label; /* UTF-8 checkbox label. */
    const char *key;   /* xxfclib slash-separated settings key. */
    int default_value; /* 0 or 1, used for missing or non-boolean values. */
} xxwidgets_setting_option;

typedef struct xxwidgets_shortcut_action {
    const char *name;
    int action;
} xxwidgets_shortcut_action;

/* Install a loaded named shortcut list using the application's action map.
 * Unknown names are ignored. Empty sequences disable actions. Installation
 * has the same validation/transactional behavior as window_set_shortcuts. */
xxwidgets_status xxwidgets_settings_install_shortcuts(xxwidgets_widget *window, const xx_shortcuts *shortcuts, const xxwidgets_shortcut_action *actions, size_t count);

/* Optional xxwidgets::settings adapter. The caller owns and loads the store.
 * Reads typed booleans; missing/non-boolean values use default_value. */
int xxwidgets_settings_get_bool(const xx_settings *settings, const char *key, int default_value);
/* Set a typed boolean and save immediately. Invalid keys/values are rejected.
 * On failure, the previous in-memory value is restored when possible. Native
 * stores may report an I/O error after writing some keys. */
xxwidgets_status xxwidgets_settings_set_bool(xx_settings *settings, const char *key, int value);
/* Common modal options form backed by xxfclib settings. Supports 0..16 unique
 * keys. OK writes typed booleans and saves; accepted is 1 only after success.
 * Cancel/Escape/close never modifies the store. Failure restores previous
 * in-memory values when possible. Same UI-thread rules as options_dialog. */
xxwidgets_status xxwidgets_settings_options_dialog(xxwidgets_widget *owner, const char *title, xx_settings *settings, const xxwidgets_setting_option *options,
                                                   size_t count, int *accepted);

#ifdef __cplusplus
}
#endif
#endif
