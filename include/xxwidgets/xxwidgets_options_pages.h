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

#ifndef XXWIDGETS_OPTIONS_PAGES_H
#define XXWIDGETS_OPTIONS_PAGES_H

#include "xxwidgets/xxwidgets.h"
#include "xxwidgets/xxwidgets_context_options.h"
#include "xxwidgets/xxwidgets_database_paths.h"
#include "xxwidgets/xxwidgets_font_options.h"
#include "xxwidgets/xxwidgets_hex_options.h"
#include "xxwidgets/xxwidgets_optimization_options.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XXWIDGETS_OPTIONS_PAGES_MAX 32

/* One page of the options dialog. Callbacks receive the state returned by
 * create. Every control that create makes in the dialog belongs to the page:
 * the dialog shows those controls only while the page is selected, keeping
 * controls the page hides itself hidden.
 *
 * create:   builds the page inside bounds (at least width x height cells).
 * event:    optional; input on the page's controls during event dispatch.
 *           Record work and return; do not open dialogs here.
 * process:  optional; runs after every poll, outside dispatch. Nested modals
 *           owned by the dialog window (choosers) may be opened here.
 * reset:    the Default button: fill the form with defaults. Edits the form only.
 * validate: optional; OK checks every page before any page is applied.
 *           Return 0 and write a NUL-terminated message to reject.
 * apply:    OK commits the form. Return 0 and write a message to keep the
 *           dialog open on this page. Pages are applied in order.
 * destroy:  destroys the page's forms; called before the dialog window. */
typedef struct xxwidgets_options_page {
    const char *title; /* UTF-8 category label, borrowed for the dialog's lifetime. */
    int width, height; /* Minimum page bounds in text cells, 1..200 x 1..60. */
    xxwidgets_status (*create)(xxwidgets_widget *dialog, xxwidgets_rect bounds, void *user, void **state);
    void (*event)(void *state, const xxwidgets_event *event);
    xxwidgets_status (*process)(void *state);
    xxwidgets_status (*reset)(void *state);
    int (*validate)(void *state, char *error, size_t capacity);
    int (*apply)(void *state, char *error, size_t capacity);
    xxwidgets_status (*destroy)(void *state);
    void *user;
} xxwidgets_options_page;

/* Application-modal options window: all other application windows are disabled
 * while open, then restored to their previous enabled states. A category list
 * on the left and a stack of pages on
 * the right, one visible at a time, with Default, OK and Cancel. Default resets
 * every page. OK validates every page, then applies them in order; accepted is
 * 1 only after all pages applied. Cancel, Escape and close apply nothing.
 * page selects the first page shown (NULL selects page 0) and receives the
 * last selected page on return. 1..XXWIDGETS_OPTIONS_PAGES_MAX pages. Same
 * UI-thread and BUSY rules as the other modal dialogs. */
XXWIDGETS_API xxwidgets_status xxwidgets_options_pages_dialog(xxwidgets_widget *owner, const char *title, const xxwidgets_options_page *pages,
                                                              size_t count, size_t *page, int *accepted);

/* Ready-made pages for the xxwidgets option forms. Each fills page with
 * callbacks bound to caller-owned values that must outlive the dialog. OK
 * writes the form into the values; apply them to the application after the
 * dialog returns accepted. Default restores each form's documented defaults. */

/* Fonts form: four roles with family, size, style, Choose and a live preview. */
XXWIDGETS_API xxwidgets_status xxwidgets_options_page_fonts(xxwidgets_options_page *page, const char *title, xxwidgets_font_options *options);

typedef struct xxwidgets_optimization_page_values {
    xxwidgets_optimization_capabilities capabilities;
    xxwidgets_optimization_options options;
} xxwidgets_optimization_page_values;
XXWIDGETS_API xxwidgets_status xxwidgets_options_page_optimization(xxwidgets_options_page *page, const char *title,
                                                                   xxwidgets_optimization_page_values *values);

typedef struct xxwidgets_hex_page_values {
    xxwidgets_hex_options options;
    int show_editable; /* 0 hides Editable and keeps options.editable. */
} xxwidgets_hex_page_values;
XXWIDGETS_API xxwidgets_status xxwidgets_options_page_hex(xxwidgets_options_page *page, const char *title, xxwidgets_hex_page_values *values);

/* OK calls callbacks.apply with the edited folders (empty = default), which
 * loads them; a failure keeps the dialog open with its message. Default
 * empties all three folders. paths are borrowed until the dialog returns. */
typedef struct xxwidgets_database_paths_page_values {
    const char *paths[3];
    xxwidgets_database_paths_callbacks callbacks;
    void *user;
} xxwidgets_database_paths_page_values;
XXWIDGETS_API xxwidgets_status xxwidgets_options_page_database_paths(xxwidgets_options_page *page, const char *title,
                                                                     xxwidgets_database_paths_page_values *values);

/* Explorer context menu. OK registers or removes the menu when the checkbox
 * differs from the actual registration; Default unchecks it. Returns
 * UNAVAILABLE on platforms without the integration; leave the page out. */
XXWIDGETS_API xxwidgets_status xxwidgets_options_page_context(xxwidgets_options_page *page, const char *title, const xxwidgets_context_config *config);

#ifdef __cplusplus
}
#endif

#endif
