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

#ifndef XXWIDGETS_DATA_STRUCT_TABLE_H
#define XXWIDGETS_DATA_STRUCT_TABLE_H

#include "xxfclib/formats/xx_format.h"
#include "xxwidgets/xxwidgets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Presentation record for one field in a binary structure. All strings are
 * UTF-8. A setter copies them; NULL strings mean empty cells. Offsets and size
 * use -1 when unknown. Role text is derived from the xxfclib property flags;
 * property 0 is displayed as "data". Raw is the hexadecimal representation
 * supplied by the data-structure provider. */
typedef struct xxwidgets_data_struct_field {
    const char *name;
    const char *type;
    int64_t relative_offset;
    int64_t file_offset;
    int64_t size;
    xx_data_struct_record_property_t property;
    const char *raw;
    const char *value;
    const char *comment;
} xxwidgets_data_struct_field;

/* DATA_STRUCT_TABLE is a read-only nine-column table implemented by every
 * xxwidgets backend. Create it with xxwidgets_widget_create, using a WINDOW as
 * parent. Replacement is transactional, selects the first field, and emits no
 * events. Returned strings are borrowed until replacement/destruction. */
XXWIDGETS_API xxwidgets_status xxwidgets_data_struct_table_set_fields(xxwidgets_widget *widget, const xxwidgets_data_struct_field *fields, size_t count);
XXWIDGETS_API xxwidgets_status xxwidgets_data_struct_table_clear(xxwidgets_widget *widget);
XXWIDGETS_API size_t xxwidgets_data_struct_table_count(const xxwidgets_widget *widget);
XXWIDGETS_API xxwidgets_status xxwidgets_data_struct_table_get_field(const xxwidgets_widget *widget, size_t index, xxwidgets_data_struct_field *field);
XXWIDGETS_API xxwidgets_status xxwidgets_data_struct_table_get_selection(xxwidgets_widget *widget, size_t *index, xxwidgets_data_struct_field *field);

#ifdef __cplusplus
}
#endif
#endif
