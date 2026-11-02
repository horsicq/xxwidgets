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

#ifndef XXWIDGETS_FILE_NAME_BAR_H
#define XXWIDGETS_FILE_NAME_BAR_H

#include "xxwidgets/xxwidgets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The "File name" row of the Detect It Easy window, usable by any program
 * that opens a file: a "File name" caption over a frame holding an icon-only
 * Recent files button (List icon), the path EDIT, which stretches, and an
 * icon-only Browse button (Open icon), right-aligned. The frame keeps
 * panel_inset around the row (xxwidgets_metrics), so the bar is
 * L + caption_gap + 2 * panel_inset + H units high.
 * Conventions: see xxwidgets_value_field.h. */
typedef struct xxwidgets_file_name_bar xxwidgets_file_name_bar;

typedef enum xxwidgets_file_name_bar_control_id {
    XXWIDGETS_FILE_NAME_BAR_CAPTION = 0, /* LABEL "File name" */
    XXWIDGETS_FILE_NAME_BAR_RECENT,      /* BUTTON, icon only, tooltip "Recent files"; disabled while the list is empty */
    XXWIDGETS_FILE_NAME_BAR_PATH,        /* EDIT, tooltip "File name", then the full path once a file is loaded */
    XXWIDGETS_FILE_NAME_BAR_BROWSE,      /* BUTTON, icon only, tooltip "Open file"; at least 80 units wide on WinAPI */
    XXWIDGETS_FILE_NAME_BAR_FRAME,       /* GROUP around the row; NULL on a backend without GROUP */
    XXWIDGETS_FILE_NAME_BAR_CONTROL_COUNT
} xxwidgets_file_name_bar_control_id;

typedef enum xxwidgets_file_name_bar_action {
    XXWIDGETS_FILE_NAME_BAR_ACTION_NONE = 0,
    XXWIDGETS_FILE_NAME_BAR_ACTION_RECENT, /* Recent files clicked */
    XXWIDGETS_FILE_NAME_BAR_ACTION_BROWSE, /* Browse clicked */
    XXWIDGETS_FILE_NAME_BAR_ACTION_OPEN,   /* Return in PATH: open its text as a path */
    XXWIDGETS_FILE_NAME_BAR_ACTION_EDITED  /* the user changed PATH; no file is unloaded */
} xxwidgets_file_name_bar_action;

/* flags: 0 (reserved). */
XXWIDGETS_API xxwidgets_status xxwidgets_file_name_bar_create(xxwidgets_widget *owner, unsigned int flags, xxwidgets_file_name_bar **out);
XXWIDGETS_API xxwidgets_status xxwidgets_file_name_bar_destroy(xxwidgets_file_name_bar *bar);
XXWIDGETS_API xxwidgets_status xxwidgets_file_name_bar_set_bounds(xxwidgets_file_name_bar *bar, xxwidgets_rect units);
XXWIDGETS_API xxwidgets_status xxwidgets_file_name_bar_get_preferred_height(xxwidgets_file_name_bar *bar, int *height);
/* Caption, both buttons and a short path. */
XXWIDGETS_API xxwidgets_status xxwidgets_file_name_bar_get_minimum_width(xxwidgets_file_name_bar *bar, int *width);
/* The PATH text (copied UTF-8). Does not change the tooltip. */
XXWIDGETS_API xxwidgets_status xxwidgets_file_name_bar_set_path(xxwidgets_file_name_bar *bar, const char *path);
/* widget_get_text conventions: required includes the NUL; NULL/0 queries. */
XXWIDGETS_API xxwidgets_status xxwidgets_file_name_bar_get_path(xxwidgets_file_name_bar *bar, char *buffer, size_t capacity, size_t *required);
/* A file was opened: PATH shows path and its tooltip becomes path. NULL or
 * "" clears the text and restores the "File name" tooltip. */
XXWIDGETS_API xxwidgets_status xxwidgets_file_name_bar_set_loaded_path(xxwidgets_file_name_bar *bar, const char *path);
/* Enables Recent files (1) or disables it (0: the recent list is empty). */
XXWIDGETS_API xxwidgets_status xxwidgets_file_name_bar_set_recent_available(xxwidgets_file_name_bar *bar, int available);
XXWIDGETS_API xxwidgets_status xxwidgets_file_name_bar_set_visible(xxwidgets_file_name_bar *bar, int visible);
XXWIDGETS_API xxwidgets_status xxwidgets_file_name_bar_retranslate(xxwidgets_file_name_bar *bar);
/* Borrowed; NULL for an id the bar does not have. */
XXWIDGETS_API xxwidgets_widget *xxwidgets_file_name_bar_control(const xxwidgets_file_name_bar *bar, xxwidgets_file_name_bar_control_id control);
/* 1 when event belongs to the bar; *action says what happened (NONE for an
 * event of the bar that needs nothing). */
XXWIDGETS_API int xxwidgets_file_name_bar_handle_event(xxwidgets_file_name_bar *bar, const xxwidgets_event *event,
                                                     xxwidgets_file_name_bar_action *action);

#ifdef __cplusplus
}
#endif
#endif
