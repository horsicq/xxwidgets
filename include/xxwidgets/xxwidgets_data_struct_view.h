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

#ifndef XXWIDGETS_DATA_STRUCT_VIEW_H
#define XXWIDGETS_DATA_STRUCT_VIEW_H

#include "xxwidgets/xxwidgets_data_struct_table.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_data_struct_view xxwidgets_data_struct_view;

typedef struct xxwidgets_data_struct_item {
    const char *name; /* UTF-8 display name. */
    xx_data_struct data_struct;
    const xxwidgets_data_struct_field *fields;
    size_t field_count;
} xxwidgets_data_struct_item;

typedef enum xxwidgets_data_struct_view_control_id {
    XXWIDGETS_DATA_STRUCT_VIEW_LIST = 0,
    XXWIDGETS_DATA_STRUCT_VIEW_TABLE,
    XXWIDGETS_DATA_STRUCT_VIEW_DETAILS,
    XXWIDGETS_DATA_STRUCT_VIEW_CONTROL_COUNT
} xxwidgets_data_struct_view_control_id;

/* Two-panel data-structure browser. Bounds use text cells and require width >=
 * 48 and height >= 4. The left list owns selection. Pass its SELECT event to
 * xxwidgets_data_struct_view_handle_event to update the right panel. Typed
 * items with fields use the nine-column table, including ENTRY records.
 * Raw descriptors and items without fields use a metadata list. Children belong to the
 * owner WINDOW. Destroy the view before its owner/app and do not destroy
 * borrowed control handles separately. */
XXWIDGETS_API xxwidgets_status xxwidgets_data_struct_view_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_data_struct_view **out_view);
XXWIDGETS_API xxwidgets_status xxwidgets_data_struct_view_destroy(xxwidgets_data_struct_view *view);
XXWIDGETS_API xxwidgets_status xxwidgets_data_struct_view_set_rect(xxwidgets_data_struct_view *view, xxwidgets_rect bounds);
XXWIDGETS_API xxwidgets_widget *xxwidgets_data_struct_view_control(const xxwidgets_data_struct_view *view, xxwidgets_data_struct_view_control_id control);
/* Call from the application's event callback. Unrelated events are accepted
 * and ignored, making it safe to pass every event received by the owner. */
XXWIDGETS_API xxwidgets_status xxwidgets_data_struct_view_handle_event(xxwidgets_data_struct_view *view, const xxwidgets_event *event);

/* Input is deep-copied. Replacement is transactional, selects and shows the
 * first item, or clears both panels for an empty list. Programmatic selection
 * is transactional and emits no event. get_selection returns an item whose
 * name, fields array, and field strings are borrowed until the next set_items,
 * clear, or view destruction. */
XXWIDGETS_API xxwidgets_status xxwidgets_data_struct_view_set_items(xxwidgets_data_struct_view *view, const xxwidgets_data_struct_item *items, size_t count);
XXWIDGETS_API xxwidgets_status xxwidgets_data_struct_view_clear(xxwidgets_data_struct_view *view);
XXWIDGETS_API size_t xxwidgets_data_struct_view_count(const xxwidgets_data_struct_view *view);
XXWIDGETS_API xxwidgets_status xxwidgets_data_struct_view_select(xxwidgets_data_struct_view *view, size_t index);
XXWIDGETS_API xxwidgets_status xxwidgets_data_struct_view_get_selection(const xxwidgets_data_struct_view *view, size_t *index,
                                                                        xxwidgets_data_struct_item *item);

#ifdef __cplusplus
}
#endif
#endif
