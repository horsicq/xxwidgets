/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxfclib/io/xx_io.h"

#include <stdlib.h>
#include <string.h>

int xxwidgets_hexview_can_cut(const xxwidgets_widget *w)
{
    uint64_t offset, length;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || w->hex_cut_busy ||
        w->hex_source_seeking || w->app->syncing || !w->app->ops->copy_text ||
        (w->hex_header != 1 && w->hex_header != 2) || !xxwidgets_hexview_can_resize(w)) return 0;
    return xxwidgets_hexview_get_source_selection(w, &offset, &length) == XXWIDGETS_OK && length &&
           offset <= w->hex_source_size && length <= w->hex_source_size - offset;
}

/* A clipboard provider is application/platform code. It can pump events or
 * call back into the widget, so a successful copy alone never authorizes a
 * deletion against a changed source, view, selection, or encoding. */
static int cut_view_unchanged(const xxwidgets_widget *w, const xxwidgets_widget *before, xx_io_device *device)
{
    return xxwidgets_hexview_get_device(w) == device &&
           w->hex_io == before->hex_io && w->hex_source_read == before->hex_source_read &&
           w->hex_source_user == before->hex_source_user && w->hex_source_size == before->hex_source_size &&
           w->hex_revision == before->hex_revision && w->hex_base == before->hex_base &&
           w->hex_size == before->hex_size && w->hex_header == before->hex_header &&
           w->hex_cursor == before->hex_cursor && w->hex_nibble == before->hex_nibble &&
           w->hex_anchor == before->hex_anchor && w->hex_anchor_bytes == before->hex_anchor_bytes &&
           w->hex_selection == before->hex_selection && w->hex_selection_length == before->hex_selection_length &&
           w->hex_source_anchor == before->hex_source_anchor &&
           w->hex_source_anchor_bytes == before->hex_source_anchor_bytes &&
           w->hex_source_selection == before->hex_source_selection && w->hex_source_length == before->hex_source_length &&
           w->hex_display.encoding == before->hex_display.encoding &&
           w->hex_display.mode == before->hex_display.mode && w->hex_display.radix == before->hex_display.radix &&
           w->hex_display.offsets == before->hex_display.offsets && w->hex_columns == before->hex_columns;
}

static void cut_restore_selection(xxwidgets_widget *w, const xxwidgets_widget *before, xx_io_device *device,
                                  uint64_t expected_revision)
{
    uint64_t cursor;
    if (xxwidgets_hexview_get_device(w) != device || w->hex_source_size != before->hex_source_size ||
        w->hex_revision != expected_revision || xx_io_total_size(device) != (int64_t)before->hex_source_size) return;
    if (w->hex_base != before->hex_base && xxwidgets_hexview_seek_device(w, before->hex_base) != XXWIDGETS_OK) return;
    if (before->hex_source_length) {
        cursor = before->hex_cursor < before->hex_size ? before->hex_base + before->hex_cursor : before->hex_source_selection;
        if (xxwidgets_hexview_set_source_range(w, cursor, before->hex_source_selection, before->hex_source_length) != XXWIDGETS_OK) return;
    } else if (xxwidgets_hexview_set_selection(w, SIZE_MAX, 0) != XXWIDGETS_OK) return;
    /* Restore direction and caret metadata as well as the visible range. */
    w->hex_source_anchor = before->hex_source_anchor;
    w->hex_source_anchor_bytes = before->hex_source_anchor_bytes;
    w->hex_anchor = before->hex_anchor; w->hex_anchor_bytes = before->hex_anchor_bytes;
    w->hex_nibble = before->hex_nibble;
    w->hex_vertical_active = before->hex_vertical_active;
    w->hex_vertical_column = before->hex_vertical_column; w->hex_vertical_row = before->hex_vertical_row;
    w->hex_source_vertical_row = before->hex_source_vertical_row;
}

xxwidgets_status xxwidgets_hexview_cut_range(xxwidgets_widget *w, uint64_t offset, uint64_t length,
                                            unsigned int column, const char *encoding, int interactive)
{
    xxwidgets_widget before;
    xx_io_device *device;
    unsigned char *snapshot = NULL, *verify = NULL;
    char *text = NULL;
    uint64_t at, selected_revision;
    xxwidgets_status status;
    int selected = 0;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || !encoding) return XXWIDGETS_INVALID_ARGUMENT;
    if (w->hex_cut_busy || w->hex_edit_busy || w->hex_source_seeking || w->app->syncing) return XXWIDGETS_BUSY;
    if ((column != 1 && column != 2) || !length || !w->app->ops->copy_text ||
        !xxwidgets_hexview_can_resize(w)) return XXWIDGETS_UNAVAILABLE;
    if (offset > w->hex_source_size || length > w->hex_source_size - offset) return XXWIDGETS_INVALID_ARGUMENT;
    device = xxwidgets_hexview_get_device(w);
    if (xx_io_total_size(device) != (int64_t)w->hex_source_size) return XXWIDGETS_BUSY;
    before = *w;
    w->hex_cut_busy = 1;
    status = xxwidgets_hexview_copy_range_text(w, offset, length,
             column == 2 ? XXWIDGETS_HEX_COPY_TEXT : XXWIDGETS_HEX_COPY_HEX, encoding, &text, &snapshot);
    if (status != XXWIDGETS_OK) goto finish;
    if (!cut_view_unchanged(w, &before, device)) { status = XXWIDGETS_BUSY; goto finish; }
    /* Bypass an application's possibly deferred copy handler: deletion needs
     * confirmed clipboard success during this operation. */
    status = w->app->ops->copy_text(w->parent, text);
    if (status != XXWIDGETS_OK) goto finish;
    if (!cut_view_unchanged(w, &before, device) || !xxwidgets_hexview_can_resize(w) ||
        xx_io_total_size(device) != (int64_t)before.hex_source_size) { status = XXWIDGETS_BUSY; goto finish; }
    verify = malloc(length < 65536 ? (size_t)length : 65536);
    if (!verify) { status = XXWIDGETS_OUT_OF_MEMORY; goto finish; }
    for (at = 0; at < length;) {
        size_t count = length - at < 65536 ? (size_t)(length - at) : 65536;
        status = before.hex_source_read(before.hex_source_user, offset + at, verify, count);
        if (status != XXWIDGETS_OK) goto finish;
        if (!cut_view_unchanged(w, &before, device) ||
            xx_io_total_size(device) != (int64_t)before.hex_source_size ||
            memcmp(snapshot + (size_t)at, verify, count)) { status = XXWIDGETS_BUSY; goto finish; }
        at += count;
    }
    /* Queued Ctrl+X captures the selected range before later keyboard input.
     * History should restore that cut range, rather than the later caret. */
    if (before.hex_source_selection != offset || before.hex_source_length != length) {
        status = xxwidgets_hexview_set_source_selection(w, offset, length);
        if (status != XXWIDGETS_OK) goto finish;
        selected = 1;
    }
    selected_revision = w->hex_revision;
    status = interactive ? xxwidgets_hexview_edit_interactive(w, offset, NULL, 0, length)
                         : xxwidgets_hexview_delete_bytes(w, offset, length);
    if (status != XXWIDGETS_OK && selected) cut_restore_selection(w, &before, device, selected_revision);
finish:
    w->hex_cut_busy = 0;
    free(verify); free(snapshot); free(text);
    return status;
}

xxwidgets_status xxwidgets_hexview_cut_selection(xxwidgets_widget *w)
{
    uint64_t offset, length;
    if (!w || w->kind != XXWIDGETS_HEXVIEW) return XXWIDGETS_INVALID_ARGUMENT;
    if (w->hex_cut_busy || w->hex_edit_busy || w->hex_source_seeking || w->app->syncing) return XXWIDGETS_BUSY;
    if (!xxwidgets_hexview_can_cut(w)) return XXWIDGETS_UNAVAILABLE;
    (void)xxwidgets_hexview_get_source_selection(w, &offset, &length);
    return xxwidgets_hexview_cut_range(w, offset, length, w->hex_header, w->hex_display.encoding, 0);
}
