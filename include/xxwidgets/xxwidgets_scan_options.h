#ifndef XXWIDGETS_SCAN_OPTIONS_H
#define XXWIDGETS_SCAN_OPTIONS_H

#include "xxwidgets/xxwidgets_scan_panel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_scan_options {
    unsigned int flags;
    unsigned int databases;
} xxwidgets_scan_options;

typedef struct xxwidgets_scan_options_widget xxwidgets_scan_options_widget;

typedef enum xxwidgets_scan_options_control_id {
    XXWIDGETS_SCAN_OPTIONS_DEEP = 0,
    XXWIDGETS_SCAN_OPTIONS_HEURISTIC,
    XXWIDGETS_SCAN_OPTIONS_VERBOSE,
    XXWIDGETS_SCAN_OPTIONS_AGGRESSIVE,
    XXWIDGETS_SCAN_OPTIONS_HIDE_UNKNOWN,
    XXWIDGETS_SCAN_OPTIONS_FORMAT,
    XXWIDGETS_SCAN_OPTIONS_DATABASE_EXTRA,
    XXWIDGETS_SCAN_OPTIONS_DATABASE_CUSTOM,
    XXWIDGETS_SCAN_OPTIONS_FLAGS_LABEL,
    XXWIDGETS_SCAN_OPTIONS_DATABASES_LABEL,
    XXWIDGETS_SCAN_OPTIONS_MAIN_DATABASE_LABEL,
    XXWIDGETS_SCAN_OPTIONS_CONTROL_COUNT
} xxwidgets_scan_options_control_id;

/* Deep/Heuristic/Verbose and both optional databases are enabled initially.
 * Flags/databases use the XXWIDGETS_SCAN_* masks; other bits are invalid. */
XXWIDGETS_API xxwidgets_status xxwidgets_scan_options_init(xxwidgets_scan_options *options);

/* Embedded scan settings form with six flag checkboxes, two database choices,
 * and labels. Bounds use text cells and require at least 60 x 13. Children
 * belong to the owner WINDOW and use its app callback. Destroy this form before
 * its owner/app; borrowed controls must not be destroyed separately. */
XXWIDGETS_API xxwidgets_status xxwidgets_scan_options_widget_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_scan_options_widget **out_widget);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_options_widget_destroy(xxwidgets_scan_options_widget *widget);
XXWIDGETS_API xxwidgets_widget *xxwidgets_scan_options_widget_control(const xxwidgets_scan_options_widget *widget, xxwidgets_scan_options_control_id control);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_options_widget_get(const xxwidgets_scan_options_widget *widget, xxwidgets_scan_options *options);
/* Atomic updates copy scalar values and emit no input events. */
XXWIDGETS_API xxwidgets_status xxwidgets_scan_options_widget_set(xxwidgets_scan_options_widget *widget, const xxwidgets_scan_options *options);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_options_widget_set_rect(xxwidgets_scan_options_widget *widget, xxwidgets_rect bounds);

/* Shows the same form with OK/Cancel. Successful OK copies settings back;
 * Cancel, Escape, closing, or errors preserve the supplied settings. Start
 * modal dialogs after returning from event callbacks/polling. */
XXWIDGETS_API xxwidgets_status xxwidgets_scan_options_dialog(xxwidgets_widget *owner, const char *title, xxwidgets_scan_options *options, int *accepted);

#ifdef __cplusplus
}
#endif
#endif
