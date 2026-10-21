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

/* Zero descriptors restore backend defaults for all four roles. */
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

#ifdef __cplusplus
}
#endif
#endif
