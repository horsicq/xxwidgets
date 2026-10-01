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
    char family[128]; /* NUL-terminated UTF-8; empty selects the backend family. */
    unsigned int point_size; /* 0 = backend default size, otherwise 4..96 points. */
    int bold, italic; /* Each 0 or 1. */
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
XXWIDGETS_API xxwidgets_status xxwidgets_app_set_font_options(xxwidgets_app *app,
    const xxwidgets_font_options *options);
XXWIDGETS_API xxwidgets_status xxwidgets_app_get_font_options(const xxwidgets_app *app,
    xxwidgets_font_options *options);
/* Modal editable family/size/style rows with Choose, Default, and previews.
 * Only OK changes options; this dialog does not apply them to the application.
 * Call app_set_font_options after acceptance. Cancel and chooser cancellation
 * preserve the prior values. Call outside callbacks/polling; nested UI is BUSY.
 * Backends without a native chooser retain the editable portable rows. */
XXWIDGETS_API xxwidgets_status xxwidgets_font_options_dialog(xxwidgets_widget *owner,
    const char *title, xxwidgets_font_options *options, int *accepted);
/* Native chooser for one role. UNAVAILABLE when a backend has no font chooser.
 * Cancellation is successful with accepted == 0 and leaves font untouched. */
XXWIDGETS_API xxwidgets_status xxwidgets_font_choose_dialog(xxwidgets_widget *owner,
    xxwidgets_font_role role, xxwidgets_font *font, int *accepted);

#ifdef __cplusplus
}
#endif
#endif
