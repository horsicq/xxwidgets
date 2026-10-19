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

#ifndef XXWIDGETS_CONTEXT_OPTIONS_H
#define XXWIDGETS_CONTEXT_OPTIONS_H

#include "xxwidgets/xxwidgets.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xxwidgets_context_scope {
    XXWIDGETS_CONTEXT_CURRENT_USER = 0,
    XXWIDGETS_CONTEXT_ALL_USERS
} xxwidgets_context_scope;

typedef struct xxwidgets_context_config {
    const char *application_id; /* One ASCII registry component: letters, digits, '.', '_', '-'. */
    const char *caption;        /* Nonempty UTF-8 Explorer menu text. */
    const char *file_class;     /* One class component, or NULL/empty for all files ('*'). */
    const char *executable;     /* Existing absolute Windows .exe path, valid UTF-8. */
    xxwidgets_context_scope scope;
} xxwidgets_context_config;

/* Explorer file-menu integration under Software\Classes\<class>\shell\<id>.
 * Current-user registration needs no administrator privileges. All-users writes
 * report access denial normally and never request elevation. Components reject
 * separators; executable paths reject quotes, controls and '%' command tokens.
 * Absolute paths are canonicalized, then quoted in both command and Icon.
 * Registration copies the caption and writes: "<exe>" "%1". Other applications'
 * verbs are never modified. Configuration strings are borrowed for each call.
 * Other platforms return UNAVAILABLE. Native errors set Win32 GetLastError.
 */
XXWIDGETS_API xxwidgets_status xxwidgets_context_is_registered(const xxwidgets_context_config *config, int *registered);
XXWIDGETS_API xxwidgets_status xxwidgets_context_register(const xxwidgets_context_config *config);
XXWIDGETS_API xxwidgets_status xxwidgets_context_set_enabled(const xxwidgets_context_config *config, int enabled);

/* An embeddable options form. It owns copied configuration and a checkbox,
 * Apply button and status label; bounds use text cells (width >= 36, height >= 6).
 * Checkbox changes only edit the form. The host handles Apply's CLICK by calling
 * apply(), which writes the registration and refreshes the actual state. Failed
 * writes leave the previous registration and reset the form to its actual state.
 * Destroy before the owner/app; borrowed controls must not be destroyed alone.
 */
typedef struct xxwidgets_context_options xxwidgets_context_options;
typedef enum xxwidgets_context_options_control_id {
    XXWIDGETS_CONTEXT_OPTIONS_ENABLE = 0,
    XXWIDGETS_CONTEXT_OPTIONS_APPLY,
    XXWIDGETS_CONTEXT_OPTIONS_STATUS,
    XXWIDGETS_CONTEXT_OPTIONS_CONTROL_COUNT
} xxwidgets_context_options_control_id;
XXWIDGETS_API xxwidgets_status xxwidgets_context_options_create(xxwidgets_widget *owner, xxwidgets_rect bounds, const xxwidgets_context_config *config,
                                                                xxwidgets_context_options **out_options);
XXWIDGETS_API xxwidgets_status xxwidgets_context_options_destroy(xxwidgets_context_options *options);
XXWIDGETS_API xxwidgets_widget *xxwidgets_context_options_control(const xxwidgets_context_options *options, xxwidgets_context_options_control_id control);
XXWIDGETS_API xxwidgets_status xxwidgets_context_options_refresh(xxwidgets_context_options *options);
XXWIDGETS_API xxwidgets_status xxwidgets_context_options_apply(xxwidgets_context_options *options);

/* Query actual registration, show an Enable checkbox, and apply only on OK.
 * Cancel performs no registry writes. accepted becomes 1 only after successful
 * application, and remains 0 on Cancel/error. Call outside callbacks/polling.
 */
XXWIDGETS_API xxwidgets_status xxwidgets_context_options_dialog(xxwidgets_widget *owner, const char *title, const xxwidgets_context_config *config, int *accepted);

#ifdef __cplusplus
}
#endif
#endif
