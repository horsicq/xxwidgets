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
    XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS,
    XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS,
    XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS_LABEL,
    XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS_LABEL,
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
 * height >= 14. Children belong
 * to the owner WINDOW and events use its app callback. Destroy the panel before
 * its owner/app; borrowed control handles must not be destroyed separately.
 * BUTTON clicks are handled by the application; call show_report after polling.
 */
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_scan_panel **out_panel);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_destroy(xxwidgets_scan_panel *panel);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_set_rect(xxwidgets_scan_panel *panel, xxwidgets_rect bounds);
/* Borrowed handles also allow an application to place controls using pixels.
 * FILE_TYPE is a normal COMBOBOX initially containing selected "Automatic"
 * with UINT64 value 0. Replace its records using xxwidgets_combobox_set_records
 * and handle its SELECT event in the application to rescan the current file.
 * Labels and values use the regular combo ownership and selection contract. */
XXWIDGETS_API xxwidgets_widget *xxwidgets_scan_panel_control(const xxwidgets_scan_panel *panel, xxwidgets_scan_panel_control_id control);

/* Flags use the six bits above. Database checks select Extra and Custom; the
 * main database is always used. Setters emit no input events. Defaults are
 * Deep/Heuristic/Verbose and both optional databases. Invalid bits are rejected.
 */
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_set_flags(xxwidgets_scan_panel *panel, unsigned int flags);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_get_flags(const xxwidgets_scan_panel *panel, unsigned int *flags);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_set_databases(xxwidgets_scan_panel *panel, unsigned int databases);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_get_databases(const xxwidgets_scan_panel *panel, unsigned int *databases);

/* Populates the include/exclude checkbox combo boxes from a semicolon-separated
 * UTF-8 tag catalog. A successful replacement updates both controls atomically
 * and clears both selections. An empty catalog disables both controls; a
 * nonempty catalog enables them. The getters join checked tags with semicolons;
 * required includes the terminating NUL. A NULL buffer with zero capacity
 * queries the required size. */
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_set_available_tags(xxwidgets_scan_panel *panel, const char *tags);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_get_include_tags(const xxwidgets_scan_panel *panel, char *buffer, size_t capacity, size_t *required);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_get_exclude_tags(const xxwidgets_scan_panel *panel, char *buffer, size_t capacity, size_t *required);

/* Compact format root -> Type: Name (Version) [Info] leaves. file_label is
 * the detected format, e.g. PE32. NULL fields are empty. For nested scans use
 * set_tree with explicit parent indexes; the full text report stays separate. */
/* Uses xxfclib's XScanEngine color rules; accepts presentation heuristic prefixes. */
XXWIDGETS_API xxwidgets_tree_color xxwidgets_scan_result_color(const char *type);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_set_tree(xxwidgets_scan_panel *panel, const xxwidgets_tree_node *nodes,
    const xxwidgets_tree_color *colors, size_t node_count, size_t detection_count, const char *report);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_set_results(xxwidgets_scan_panel *panel, const char *file_label, const xxwidgets_scan_result *results, size_t count,
                                                                const char *report);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_clear(xxwidgets_scan_panel *panel);
XXWIDGETS_API size_t xxwidgets_scan_panel_count(const xxwidgets_scan_panel *panel);
/* required includes NUL; short output remains valid UTF-8. */
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_get_report(const xxwidgets_scan_panel *panel, char *buffer, size_t capacity, size_t *required);
XXWIDGETS_API xxwidgets_status xxwidgets_scan_panel_show_report(xxwidgets_scan_panel *panel);

#ifdef __cplusplus
}
#endif
#endif
