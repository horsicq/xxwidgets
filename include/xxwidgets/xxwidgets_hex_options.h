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

#ifndef XXWIDGETS_HEX_OPTIONS_H
#define XXWIDGETS_HEX_OPTIONS_H

#include "xxwidgets/xxwidgets.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_hex_options {
    unsigned int bytes_per_row;
    int editable;
} xxwidgets_hex_options;

typedef struct xxwidgets_hex_options_widget xxwidgets_hex_options_widget;

typedef enum xxwidgets_hex_options_control_id {
    XXWIDGETS_HEX_OPTIONS_BYTES_PER_ROW = 0,
    XXWIDGETS_HEX_OPTIONS_EDITABLE,
    XXWIDGETS_HEX_OPTIONS_BYTES_PER_ROW_LABEL,
    XXWIDGETS_HEX_OPTIONS_CONTROL_COUNT
} xxwidgets_hex_options_control_id;

/* Defaults match a newly created HexView: 16 bytes per row and read-only. */
XXWIDGETS_API xxwidgets_status xxwidgets_hex_options_init(xxwidgets_hex_options *options);

/* Embedded HexView options form with an 8/16/24/32/48/64-byte row selector and an
 * Editable checkbox. Bounds use text cells and require at least 44 x 2.
 * Children belong to the owner WINDOW and use its app callback. Destroy the
 * form before its owner/app; borrowed controls must not be destroyed alone. */
XXWIDGETS_API xxwidgets_status xxwidgets_hex_options_widget_create(xxwidgets_widget *owner, xxwidgets_rect bounds,
                                                                   xxwidgets_hex_options_widget **out_widget);
XXWIDGETS_API xxwidgets_status xxwidgets_hex_options_widget_destroy(xxwidgets_hex_options_widget *widget);
XXWIDGETS_API xxwidgets_widget *xxwidgets_hex_options_widget_control(const xxwidgets_hex_options_widget *widget,
                                                                     xxwidgets_hex_options_control_id control);
XXWIDGETS_API xxwidgets_status xxwidgets_hex_options_widget_get(const xxwidgets_hex_options_widget *widget,
                                                                 xxwidgets_hex_options *options);
/* bytes_per_row must be 8, 16, 24, 32, 48 or 64 and editable must be 0 or 1.
 * Programmatic updates are transactional and emit no input events. */
XXWIDGETS_API xxwidgets_status xxwidgets_hex_options_widget_set(xxwidgets_hex_options_widget *widget,
                                                                 const xxwidgets_hex_options *options);
XXWIDGETS_API xxwidgets_status xxwidgets_hex_options_widget_set_rect(xxwidgets_hex_options_widget *widget, xxwidgets_rect bounds);

#ifdef __cplusplus
}
#endif
#endif
