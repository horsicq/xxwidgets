/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_DEBUGGER_H
#define XXWIDGETS_DEBUGGER_H

#include "xxwidgets/xxwidgets_debugger_tables.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_debugger xxwidgets_debugger;

typedef enum xxwidgets_debugger_control_id {
    XXWIDGETS_DEBUGGER_CONTROL_DISASM,
    XXWIDGETS_DEBUGGER_CONTROL_REGISTERS,
    XXWIDGETS_DEBUGGER_CONTROL_HEX,
    XXWIDGETS_DEBUGGER_CONTROL_STACK,
    XXWIDGETS_DEBUGGER_LABEL_DISASM,
    XXWIDGETS_DEBUGGER_LABEL_REGISTERS,
    XXWIDGETS_DEBUGGER_LABEL_HEX,
    XXWIDGETS_DEBUGGER_LABEL_STACK,
    XXWIDGETS_DEBUGGER_SPLIT_VERTICAL,
    XXWIDGETS_DEBUGGER_SPLIT_HORIZONTAL,
    XXWIDGETS_DEBUGGER_CONTROL_COUNT
} xxwidgets_debugger_control_id;

/* Classic debugger view, using native controls or the TUI backend:
 *     Disassembly | Registers
 *     ------------+----------
 *     Hex         | Stack
 * Bounds are owner-window text cells, at least 65 x 13. Both splitters are
 * draggable; keyboard arrows resize a focused splitter in the TUI/native UI.
 * Forward events to handle_event; on owner RESIZE call set_rect explicitly.
 * All calls belong to the UI thread. Destroy before owner/app destruction.
 * Borrowed controls must not be destroyed separately. The default Disasm,
 * Registers and Stack panes are independent debugger table widgets; Hex is
 * the same architecture-independent HEXVIEW used elsewhere in the library.
 * Every pane may be hidden or replaced with a caller-created widget. */
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_debugger **out);
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_destroy(xxwidgets_debugger *view);
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_set_rect(xxwidgets_debugger *view, xxwidgets_rect bounds);
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_get_rect(const xxwidgets_debugger *view, xxwidgets_rect *bounds);
/* Requested left/top percentages are 1..99 (defaults 65/60). Actual positions
 * are clamped to pane minimums; resizing preserves the requested ratios.
 * Dragging stores finer ratios; get_split rounds these to whole percentages. */
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_set_split(xxwidgets_debugger *view, unsigned left_percent, unsigned top_percent);
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_get_split(const xxwidgets_debugger *view, unsigned *left_percent, unsigned *top_percent);
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_set_visible(xxwidgets_debugger *view, int visible);
XXWIDGETS_API xxwidgets_widget *xxwidgets_debugger_control(const xxwidgets_debugger *view, xxwidgets_debugger_control_id control);
/* Pane IDs are CONTROL_DISASM through CONTROL_STACK. Hidden panes give their
 * row's space to the other pane; an entirely hidden row collapses. Preferences
 * survive hiding the entire composite. */
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_set_panel_visible(xxwidgets_debugger *view, xxwidgets_debugger_control_id pane, int visible);
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_get_panel_visible(const xxwidgets_debugger *view, xxwidgets_debugger_control_id pane, int *visible);
/* replacement is borrowed, must be a child of the same owner WINDOW, and must
 * not already occupy another pane. The composite manages its rect/visibility
 * while installed; removal or composite destruction restores both. Do not
 * destroy or share it with another composite while installed. NULL restores
 * the retained default widget. This does not destroy either widget. Configure
 * independent ARM/x86 pane widgets with their own rows and swap them here;
 * the hex pane can remain the same. Custom widget kinds are accepted, with
 * their model/event logic supplied by the host. */
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_set_panel(xxwidgets_debugger *view, xxwidgets_debugger_control_id pane, xxwidgets_widget *replacement);
/* Ignores unrelated events. Selection/activation events on borrowed
 * panes remain available to the host for navigation and debugger actions. */
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_handle_event(xxwidgets_debugger *view, const xxwidgets_event *event);

/* Each setter copies its pane's snapshot transactionally and emits no input
 * events. Rows use the table API's ownership and selection conventions.
 * No target process or debugger session is owned or resumed by the widget.
 * A pane setter returns UNAVAILABLE when its replacement is a different
 * widget kind; configure such replacements with their own APIs. */
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_set_disassembly(xxwidgets_debugger *view, const xxwidgets_debugger_instruction *rows, size_t count);
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_set_registers(xxwidgets_debugger *view, const xxwidgets_debugger_register *rows, size_t count);
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_set_stack(xxwidgets_debugger *view, const xxwidgets_debugger_stack_entry *rows, size_t count);
/* Atomically replaces copied bytes and their base address. The hex pane is
 * read-only initially. This helper rejects a caller-attached live source or
 * IO device; use that source's own refresh API instead. NULL/0 clears bytes. */
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_set_memory(xxwidgets_debugger *view, uint64_t address, const void *bytes, size_t size);
/* Replaces a pane heading with its name and optional UTF-8 status, e.g.
 * "Hex - unreadable". control must be one of the first four pane controls. */
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_set_pane_status(xxwidgets_debugger *view, xxwidgets_debugger_control_id control, const char *status);
/* Clears panes in display order. Each pane update is transactional; a backend
 * failure stops the operation and is returned to the caller. */
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_clear(xxwidgets_debugger *view);

#ifdef __cplusplus
}
#endif
#endif
