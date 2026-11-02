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

#ifndef XXWIDGETS_DIE_MAIN_PANEL_H
#define XXWIDGETS_DIE_MAIN_PANEL_H

#include "xxwidgets/xxwidgets.h"
#include "xxwidgets/xxwidgets_file_header.h"
#include "xxwidgets/xxwidgets_file_name_bar.h"
#include "xxwidgets/xxwidgets_scan_panel.h"
#include "xxwidgets/xxwidgets_side_panel.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parts that arrive in Phase 2 (xxwidgets_button_grid.h,
 * xxwidgets_format_panel.h declare their typedefs). */
struct xxwidgets_button_grid;
struct xxwidgets_format_panel;

/* The whole content of the Detect It Easy main window, in its Simple and
 * Advanced views, laid out in measured units:
 *   A  file name bar (full width)
 *   B  file header: File type, File size | Base address, Entry point (Advanced)
 *   C  tools grid, 2 x 7 (Advanced)          D  type area, per format page (Advanced)
 *   E  engine row: Scan, Endianness, Mode, Architecture, Type
 *   F  result tree (takes every extra unit of height)
 *   G  scan bar: Signatures, Flags, Database [, tag filters], Directory, Log,
 *      ">", elapsed time, tall Scan
 *   right column, from row B to the bottom: Advanced | Demangle ...
 *      Update database, Shortcuts, Options, About, Exit
 * Every control is created once, in that order (the Tab order); a view
 * change only shows and hides. The Advanced checkbox is handled here: it
 * applies the view, then returns ADVANCED_CHANGED. The fixed "Flags" and
 * "Database" captions are kept here too. Everything else is returned as an
 * action for the application to carry out. Result tree events (the S and ?
 * marks) still belong to the scan panel's tree. Conventions: see
 * xxwidgets_value_field.h. */
typedef struct xxwidgets_die_main_panel xxwidgets_die_main_panel;

/* The tools grid, row-major (row 0 then row 1). */
typedef enum xxwidgets_die_tool {
    XXWIDGETS_DIE_TOOL_FILE_INFO = 0, /* "File info", Info icon */
    XXWIDGETS_DIE_TOOL_MEMORY_MAP,    /* "Memory map", MemoryMap */
    XXWIDGETS_DIE_TOOL_DISASM,        /* "Disasm", Disasm */
    XXWIDGETS_DIE_TOOL_HEX,           /* "Hex", Binary */
    XXWIDGETS_DIE_TOOL_STRINGS,       /* "Strings", String */
    XXWIDGETS_DIE_TOOL_SIGNATURES,    /* "Signatures" (byte-signature search), Signature */
    XXWIDGETS_DIE_TOOL_VIRUSTOTAL,    /* "VirusTotal", Virustotal */
    XXWIDGETS_DIE_TOOL_MIME,          /* "MIME", Mime */
    XXWIDGETS_DIE_TOOL_VISUALIZATION, /* "Visualization", Image */
    XXWIDGETS_DIE_TOOL_SEARCH,        /* "Search", Search */
    XXWIDGETS_DIE_TOOL_HASH,          /* "Hash", Hash */
    XXWIDGETS_DIE_TOOL_ENTROPY,       /* "Entropy", Entropy */
    XXWIDGETS_DIE_TOOL_EXTRACTOR,     /* "Extractor", Extract */
    XXWIDGETS_DIE_TOOL_YARA,          /* "YARA", Yara: hidden, its cell stays empty */
    XXWIDGETS_DIE_TOOL_COUNT
} xxwidgets_die_tool;

/* Item ids of the right column (xxwidgets_side_panel_control etc.). */
typedef enum xxwidgets_die_side_item {
    XXWIDGETS_DIE_SIDE_ADVANCED = 0, /* checkbox, top */
    XXWIDGETS_DIE_SIDE_DEMANGLE,     /* top; shown with SHOW_DEMANGLE in the Advanced view */
    XXWIDGETS_DIE_SIDE_UPDATE,       /* "Update database", above Shortcuts; shown with SHOW_UPDATE */
    XXWIDGETS_DIE_SIDE_SHORTCUTS,
    XXWIDGETS_DIE_SIDE_OPTIONS,
    XXWIDGETS_DIE_SIDE_ABOUT,
    XXWIDGETS_DIE_SIDE_EXIT,
    XXWIDGETS_DIE_SIDE_COUNT
} xxwidgets_die_side_item;

typedef enum xxwidgets_die_action_id {
    XXWIDGETS_DIE_ACTION_NONE = 0,
    XXWIDGETS_DIE_ACTION_RECENT,
    XXWIDGETS_DIE_ACTION_BROWSE,
    XXWIDGETS_DIE_ACTION_OPEN_PATH,        /* Return in the path field */
    XXWIDGETS_DIE_ACTION_PATH_EDITED,
    XXWIDGETS_DIE_ACTION_FILE_TYPE,        /* value: record index; type_id: the UINT64 type id */
    XXWIDGETS_DIE_ACTION_ENGINE,           /* value: engine record index */
    XXWIDGETS_DIE_ACTION_ADVANCED_CHANGED, /* value: 0 or 1; the view is already applied */
    XXWIDGETS_DIE_ACTION_ENTRY_POINT,
    XXWIDGETS_DIE_ACTION_TOOL,             /* value: xxwidgets_die_tool */
    XXWIDGETS_DIE_ACTION_FORMAT,           /* value: xxwidgets_format_item (Phase 2) */
    XXWIDGETS_DIE_ACTION_FORMAT_ARROW,     /* value: xxwidgets_format_item (Phase 2) */
    XXWIDGETS_DIE_ACTION_SCAN,
    XXWIDGETS_DIE_ACTION_REPORT,
    XXWIDGETS_DIE_ACTION_SIGNATURES,       /* the scan bar's DiE script editor */
    XXWIDGETS_DIE_ACTION_DIRECTORY,
    XXWIDGETS_DIE_ACTION_LOG,
    XXWIDGETS_DIE_ACTION_DEMANGLE,
    XXWIDGETS_DIE_ACTION_UPDATE,
    XXWIDGETS_DIE_ACTION_SHORTCUTS,
    XXWIDGETS_DIE_ACTION_OPTIONS,
    XXWIDGETS_DIE_ACTION_ABOUT,
    XXWIDGETS_DIE_ACTION_EXIT
} xxwidgets_die_action_id;

typedef struct xxwidgets_die_action {
    xxwidgets_die_action_id id;
    int value;
    uint64_t type_id; /* FILE_TYPE only */
} xxwidgets_die_action;

enum {
    /* Shows "Update database" in the right column above Shortcuts. Enable it
     * (xxwidgets_side_panel_set_item_enabled, XXWIDGETS_DIE_SIDE_UPDATE)
     * only when an updater is available; it starts disabled. */
    XXWIDGETS_DIE_MAIN_PANEL_SHOW_UPDATE = 1u << 0,
    /* Shows Demangle under Advanced in the Advanced view. */
    XXWIDGETS_DIE_MAIN_PANEL_SHOW_DEMANGLE = 1u << 1,
    /* Keeps the include/exclude tag filters in the scan bar (wider than Qt). */
    XXWIDGETS_DIE_MAIN_PANEL_TAG_FILTERS = 1u << 2
};

/* Fills window (a WINDOW without other controls the panel would overlap).
 * Starts in the Simple view with no file loaded. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_main_panel_create(xxwidgets_widget *window, unsigned int flags, xxwidgets_die_main_panel **out);
XXWIDGETS_API xxwidgets_status xxwidgets_die_main_panel_destroy(xxwidgets_die_main_panel *panel);
/* Lays the parts out over the window's current client area; call on RESIZE. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_main_panel_layout(xxwidgets_die_main_panel *panel);
/* Simple (0) or Advanced (1) view: inside one window update, shows or hides
 * the Advanced parts, moves the focus off a hidden control, lays out again
 * and raises the window minimum (growing the window if needed, never
 * shrinking it). Emits no event (use it at startup with the stored setting). */
XXWIDGETS_API xxwidgets_status xxwidgets_die_main_panel_set_advanced(xxwidgets_die_main_panel *panel, int advanced);
XXWIDGETS_API int xxwidgets_die_main_panel_get_advanced(const xxwidgets_die_main_panel *panel);
/* 0: no file (File type, File size, Base address, Entry point, the tools,
 * the type area, the engine row and the whole scan bar disabled; the file
 * name bar and the right column stay enabled); 1: a file is loaded. Items
 * the application disabled and unavailable tools stay disabled. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_main_panel_set_loaded(xxwidgets_die_main_panel *panel, int loaded);
/* Client size in units for a view (advanced 0 or 1). */
XXWIDGETS_API xxwidgets_status xxwidgets_die_main_panel_get_minimum_size(const xxwidgets_die_main_panel *panel, int advanced, int *width,
                                                                       int *height);
/* Qt's 720 x 500 at 96 DPI (in units), or the minimum when that is larger. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_main_panel_get_default_size(const xxwidgets_die_main_panel *panel, int advanced, int *width,
                                                                       int *height);
/* 1 when event belongs to the panel; *out receives the action (NONE when the
 * panel handled it completely). */
XXWIDGETS_API int xxwidgets_die_main_panel_handle_event(xxwidgets_die_main_panel *panel, const xxwidgets_event *event, xxwidgets_die_action *out);
/* Re-applies every caption, label and tooltip from the current catalog and
 * lays out again. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_main_panel_retranslate(xxwidgets_die_main_panel *panel);
/* Parts, borrowed. tools and format_panel are NULL until Phase 2. */
XXWIDGETS_API xxwidgets_file_name_bar *xxwidgets_die_main_panel_file_name_bar(const xxwidgets_die_main_panel *panel);
XXWIDGETS_API xxwidgets_file_header *xxwidgets_die_main_panel_file_header(const xxwidgets_die_main_panel *panel);
XXWIDGETS_API struct xxwidgets_button_grid *xxwidgets_die_main_panel_tools(const xxwidgets_die_main_panel *panel);
XXWIDGETS_API struct xxwidgets_format_panel *xxwidgets_die_main_panel_format_panel(const xxwidgets_die_main_panel *panel);
XXWIDGETS_API xxwidgets_scan_panel *xxwidgets_die_main_panel_scan_panel(const xxwidgets_die_main_panel *panel);
XXWIDGETS_API xxwidgets_side_panel *xxwidgets_die_main_panel_side_panel(const xxwidgets_die_main_panel *panel);

#ifdef __cplusplus
}
#endif
#endif
