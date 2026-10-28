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

#ifndef XXWIDGETS_FONT_OPTIONS_H
#define XXWIDGETS_FONT_OPTIONS_H

#include "xxwidgets/xxwidgets.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xxwidgets_font_role {
    XXWIDGETS_FONT_CONTROLS = 0,
    XXWIDGETS_FONT_TABLE_VIEWS,
    XXWIDGETS_FONT_TREE_VIEWS,
    XXWIDGETS_FONT_TEXT_EDITS,
    XXWIDGETS_FONT_HEX_VIEWS,
    XXWIDGETS_FONT_DISASM_VIEWS,
    XXWIDGETS_FONT_ROLE_COUNT
} xxwidgets_font_role;

typedef struct xxwidgets_font {
    char family[128];        /* NUL-terminated UTF-8; empty selects the backend family. */
    unsigned int point_size; /* 0 = backend default size, otherwise 4..96 points. */
    int bold, italic;        /* Each 0 or 1. */
} xxwidgets_font;

typedef struct xxwidgets_font_options {
    xxwidgets_font fonts[XXWIDGETS_FONT_ROLE_COUNT];
} xxwidgets_font_options;

/* Zero descriptors restore backend defaults for all six roles. */
XXWIDGETS_API xxwidgets_status xxwidgets_font_options_init(xxwidgets_font_options *options);
/* Copies and applies to current and future widgets, including native report
 * text and dialogs. Geometry stays unchanged. Allocation failure preserves the
 * preceding fonts. Programmatic changes emit no input events. TUI stores these
 * settings; the terminal host controls its actual font. */
XXWIDGETS_API xxwidgets_status xxwidgets_app_set_font_options(xxwidgets_app *app, const xxwidgets_font_options *options);
XXWIDGETS_API xxwidgets_status xxwidgets_app_get_font_options(const xxwidgets_app *app, xxwidgets_font_options *options);
/* Font override for one HexView, independent of the application's table font.
 * Empty family or "monospace" selects the backend's fixed-pitch family; size 0
 * selects its default size. Unconfigured views report monospace/0/regular.
 * Data, cursor, selection and column widths are preserved. A failed update
 * leaves the preceding font intact. TUI stores the descriptor; its terminal
 * host controls the visible font. Programmatic updates emit no input events. */
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_set_font(xxwidgets_widget *view, const xxwidgets_font *font);
XXWIDGETS_API xxwidgets_status xxwidgets_hexview_get_font(const xxwidgets_widget *view, xxwidgets_font *font);
/* Modal editable family/size/style rows with Choose, Default, and previews.
 * Only OK changes options; this dialog does not apply them to the application.
 * Call app_set_font_options after acceptance. Cancel and chooser cancellation
 * preserve the prior values. Call outside callbacks/polling; nested UI is BUSY.
 * Backends without a native chooser retain the editable portable rows. */
XXWIDGETS_API xxwidgets_status xxwidgets_font_options_dialog(xxwidgets_widget *owner, const char *title, xxwidgets_font_options *options, int *accepted);
/* Native chooser for one role. UNAVAILABLE when a backend has no font chooser.
 * Cancellation is successful with accepted == 0 and leaves font untouched. */
XXWIDGETS_API xxwidgets_status xxwidgets_font_choose_dialog(xxwidgets_widget *owner, xxwidgets_font_role role, xxwidgets_font *font, int *accepted);

/* Embedded fonts form: per role a label, family and size edits, Bold and
 * Italic, a Choose button and a preview line drawn in the edited font.
 * Bounds use text cells and require at least 56 x 23. Children belong to the
 * owner WINDOW and use its app callback. Destroy before the owner/app;
 * borrowed controls must not be destroyed separately. */
typedef struct xxwidgets_font_options_widget xxwidgets_font_options_widget;
typedef enum xxwidgets_font_options_control_id {
    XXWIDGETS_FONT_OPTIONS_LABEL = 0,
    XXWIDGETS_FONT_OPTIONS_FAMILY,
    XXWIDGETS_FONT_OPTIONS_SIZE,
    XXWIDGETS_FONT_OPTIONS_BOLD,
    XXWIDGETS_FONT_OPTIONS_ITALIC,
    XXWIDGETS_FONT_OPTIONS_CHOOSE,
    XXWIDGETS_FONT_OPTIONS_PREVIEW,
    XXWIDGETS_FONT_OPTIONS_CONTROL_COUNT
} xxwidgets_font_options_control_id;
XXWIDGETS_API xxwidgets_status xxwidgets_font_options_widget_create(xxwidgets_widget *owner, xxwidgets_rect bounds,
                                                                    xxwidgets_font_options_widget **out_widget);
XXWIDGETS_API xxwidgets_status xxwidgets_font_options_widget_destroy(xxwidgets_font_options_widget *widget);
XXWIDGETS_API xxwidgets_widget *xxwidgets_font_options_widget_control(const xxwidgets_font_options_widget *widget, xxwidgets_font_role role,
                                                                      xxwidgets_font_options_control_id control);
/* INVALID_ARGUMENT when a family or size edit holds an invalid descriptor. */
XXWIDGETS_API xxwidgets_status xxwidgets_font_options_widget_get(const xxwidgets_font_options_widget *widget, xxwidgets_font_options *options);
/* Transactional for validation; emits no input events and refreshes previews. */
XXWIDGETS_API xxwidgets_status xxwidgets_font_options_widget_set(xxwidgets_font_options_widget *widget, const xxwidgets_font_options *options);
XXWIDGETS_API xxwidgets_status xxwidgets_font_options_widget_set_rect(xxwidgets_font_options_widget *widget, xxwidgets_rect bounds);
/* Forward the owner's input events, then call process after polling. Choose
 * opens the native chooser (the owner must be the active modal window or no
 * modal may be open); edits refresh their row's preview once valid. */
XXWIDGETS_API xxwidgets_status xxwidgets_font_options_widget_handle_event(xxwidgets_font_options_widget *widget, const xxwidgets_event *event);
XXWIDGETS_API xxwidgets_status xxwidgets_font_options_widget_process(xxwidgets_font_options_widget *widget);

#ifdef __cplusplus
}
#endif
#endif
