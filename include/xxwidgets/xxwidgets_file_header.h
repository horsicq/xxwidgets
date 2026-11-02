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

#ifndef XXWIDGETS_FILE_HEADER_H
#define XXWIDGETS_FILE_HEADER_H

#include "xxwidgets/xxwidgets.h"
#include "xxwidgets/xxwidgets_value_field.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Row B of the Detect It Easy window: File type (a COMBO value field) and
 * File size on the left; in the Advanced view also Base address and Entry
 * point (with its Entry arrow), right-aligned after an expanding gap. Base
 * address and Entry point exist from creation and stay hidden until
 * set_advanced(1). Conventions: see xxwidgets_value_field.h. */
typedef struct xxwidgets_file_header xxwidgets_file_header;

typedef enum xxwidgets_file_header_field_id {
    XXWIDGETS_FILE_HEADER_FILE_TYPE = 0, /* COMBO; tooltip "Type" */
    XXWIDGETS_FILE_HEADER_FILE_SIZE,     /* VALUE, e.g. "31.46 MiB"; tooltip "File size" */
    XXWIDGETS_FILE_HEADER_BASE_ADDRESS,  /* VALUE, hex; Advanced only */
    XXWIDGETS_FILE_HEADER_ENTRY_POINT,   /* VALUE, hex, WITH_ARROW (tooltip "Entry point"); Advanced only */
    XXWIDGETS_FILE_HEADER_FIELD_COUNT
} xxwidgets_file_header_field_id;

typedef enum xxwidgets_file_header_action_id {
    XXWIDGETS_FILE_HEADER_ACTION_NONE = 0,
    XXWIDGETS_FILE_HEADER_ACTION_FILE_TYPE_SELECTED, /* the user chose another file type */
    XXWIDGETS_FILE_HEADER_ACTION_ENTRY_POINT_ARROW   /* the Entry point arrow was clicked */
} xxwidgets_file_header_action_id;

typedef struct xxwidgets_file_header_action {
    xxwidgets_file_header_action_id id;
    uint64_t type_id; /* FILE_TYPE_SELECTED: the chosen record's UINT64 value */
    size_t index;     /* FILE_TYPE_SELECTED: the chosen record's index */
} xxwidgets_file_header_action;

/* flags: 0 (reserved). Starts in the Simple view with no file loaded. */
XXWIDGETS_API xxwidgets_status xxwidgets_file_header_create(xxwidgets_widget *owner, unsigned int flags, xxwidgets_file_header **out);
XXWIDGETS_API xxwidgets_status xxwidgets_file_header_destroy(xxwidgets_file_header *header);
XXWIDGETS_API xxwidgets_status xxwidgets_file_header_set_bounds(xxwidgets_file_header *header, xxwidgets_rect units);
/* For the current view: the natural and the smallest size of the row. */
XXWIDGETS_API xxwidgets_status xxwidgets_file_header_get_preferred_size(xxwidgets_file_header *header, int *width, int *height);
XXWIDGETS_API xxwidgets_status xxwidgets_file_header_get_minimum_size(xxwidgets_file_header *header, int *width, int *height);
/* Borrowed field of an id (NULL past the end). */
XXWIDGETS_API xxwidgets_value_field *xxwidgets_file_header_field(const xxwidgets_file_header *header, xxwidgets_file_header_field_id field);
/* The file's concrete types (xxwidgets_combobox_set_records; values are
 * UINT64 type ids), selecting record selected (the most specific type). */
XXWIDGETS_API xxwidgets_status xxwidgets_file_header_set_file_types(xxwidgets_file_header *header, const xx_meta_string *records, size_t count,
                                                                  size_t selected);
/* "0 Bytes", "1023 Bytes", "1.00 KiB", "31.46 MiB" (1024-based), not bold. */
XXWIDGETS_API xxwidgets_status xxwidgets_file_header_set_file_size(xxwidgets_file_header *header, uint64_t bytes);
/* BASE_ADDRESS or ENTRY_POINT as hex padded to bits / 4 digits (bits 8, 16,
 * 32 or 64), bold when nonzero; or as "SSSS:OOOO" (MSDOS). Other ids:
 * INVALID_ARGUMENT. */
XXWIDGETS_API xxwidgets_status xxwidgets_file_header_set_address(xxwidgets_file_header *header, xxwidgets_file_header_field_id field,
                                                               uint64_t value, unsigned int bits);
XXWIDGETS_API xxwidgets_status xxwidgets_file_header_set_address_seg_off(xxwidgets_file_header *header, xxwidgets_file_header_field_id field,
                                                                       uint16_t segment, uint16_t offset);
/* Shows (1) or hides (0) Base address and Entry point and lays out again. */
XXWIDGETS_API xxwidgets_status xxwidgets_file_header_set_advanced(xxwidgets_file_header *header, int advanced);
/* 0: no file, every field disabled; 1: enabled. */
XXWIDGETS_API xxwidgets_status xxwidgets_file_header_set_loaded(xxwidgets_file_header *header, int loaded);
/* Empties every value and the type list. */
XXWIDGETS_API xxwidgets_status xxwidgets_file_header_clear(xxwidgets_file_header *header);
XXWIDGETS_API xxwidgets_status xxwidgets_file_header_set_visible(xxwidgets_file_header *header, int visible);
XXWIDGETS_API xxwidgets_status xxwidgets_file_header_retranslate(xxwidgets_file_header *header);
XXWIDGETS_API int xxwidgets_file_header_handle_event(xxwidgets_file_header *header, const xxwidgets_event *event,
                                                   xxwidgets_file_header_action *action);

#ifdef __cplusplus
}
#endif
#endif
