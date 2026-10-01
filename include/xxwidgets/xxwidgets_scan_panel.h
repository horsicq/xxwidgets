#ifndef XXWIDGETS_SCAN_PANEL_H
#define XXWIDGETS_SCAN_PANEL_H

#include "xxwidgets/xxwidgets.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_scan_panel xxwidgets_scan_panel;

typedef enum xxwidgets_scan_panel_control_id {
    XXWIDGETS_SCAN_PANEL_FLAGS = 0,
    XXWIDGETS_SCAN_PANEL_DATABASES,
    XXWIDGETS_SCAN_PANEL_SCAN,
    XXWIDGETS_SCAN_PANEL_REPORT,
    XXWIDGETS_SCAN_PANEL_RESULTS,
    XXWIDGETS_SCAN_PANEL_FLAGS_LABEL,
    XXWIDGETS_SCAN_PANEL_DATABASES_LABEL,
    XXWIDGETS_SCAN_PANEL_FILE_TYPE,
    XXWIDGETS_SCAN_PANEL_FILE_TYPE_LABEL,
    XXWIDGETS_SCAN_PANEL_CONTROL_COUNT
} xxwidgets_scan_panel_control_id;

enum {
    XXWIDGETS_SCAN_DEEP = 1u << 0,
    XXWIDGETS_SCAN_HEURISTIC = 1u << 1,
    XXWIDGETS_SCAN_VERBOSE = 1u << 2,
    XXWIDGETS_SCAN_AGGRESSIVE = 1u << 3,
    XXWIDGETS_SCAN_HIDE_UNKNOWN = 1u << 4,
    XXWIDGETS_SCAN_FORMAT = 1u << 5,
    XXWIDGETS_SCAN_DATABASE_EXTRA = 1u << 0,
    XXWIDGETS_SCAN_DATABASE_CUSTOM = 1u << 1
};

/* Complete scan UI: a file-type combo box, labeled checkbox combo boxes,
 * Scan/Report push buttons, and a hierarchical result tree. The application
 * supplies the scan engine. Bounds use text cells, require width >= 44 and
 * height >= 11. Children belong
 * to the owner WINDOW and events use its app callback. Destroy the panel before
 * its owner/app; borrowed control handles must not be destroyed separately.
 * BUTTON clicks are handled by the application; call show_report after polling.
 */
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_create(xxwidgets_widget *owner,
    xxwidgets_rect bounds, xxwidgets_scan_panel **out_panel);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_destroy(xxwidgets_scan_panel *panel);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_set_rect(xxwidgets_scan_panel *panel,
    xxwidgets_rect bounds);
/* Borrowed handles also allow an application to place controls using pixels.
 * FILE_TYPE is a normal COMBOBOX initially containing selected "Automatic"
 * with UINT64 value 0. Replace its records using xxwidgets_combobox_set_records
 * and handle its SELECT event in the application to rescan the current file.
 * Labels and values use the regular combo ownership and selection contract. */
XXWIDGETS_API xxwidgets_widget *xxwidgets_scan_panel_control(
    const xxwidgets_scan_panel *panel, xxwidgets_scan_panel_control_id control);

/* Flags use the six bits above. Database checks select Extra and Custom; the
 * main database is always used. Setters emit no input events. Defaults are
 * Deep/Heuristic/Verbose and both optional databases. Invalid bits are rejected.
 */
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_set_flags(xxwidgets_scan_panel *panel, unsigned int flags);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_get_flags(const xxwidgets_scan_panel *panel, unsigned int *flags);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_set_databases(xxwidgets_scan_panel *panel, unsigned int databases);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_get_databases(const xxwidgets_scan_panel *panel, unsigned int *databases);

/* Copies UTF-8 into a File -> detection -> Type/Name/Version/Info tree. NULL
 * fields/file/report mean empty strings. Replacement is transactional, resets
 * expansion/selection, and emits no input events. Complete report text is
 * copied separately so nested engine output need not be parsed or flattened.
 */
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_set_results(xxwidgets_scan_panel *panel,
    const char *file_label, const xxwidgets_scan_result *results, size_t count,
    const char *report);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_clear(xxwidgets_scan_panel *panel);
XXWIDGETS_API size_t xxwidgets_scan_panel_count(const xxwidgets_scan_panel *panel);
/* required includes NUL; short output remains valid UTF-8. */
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_get_report(const xxwidgets_scan_panel *panel,
    char *buffer, size_t capacity, size_t *required);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_show_report(xxwidgets_scan_panel *panel);

#ifdef __cplusplus
}
#endif
#endif
