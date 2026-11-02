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

#ifndef XXWIDGETS_VALUE_FIELD_H
#define XXWIDGETS_VALUE_FIELD_H

#include "xxwidgets/xxwidgets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A captioned, framed value box in the Detect It Easy look: a caption LABEL
 * on top, below it a GROUP frame holding the value (a VALUE, a COMBOBOX, or
 * a row of buttons the application supplies), and optionally an icon-only
 * Entry arrow BUTTON S units right of the value, whose tooltip is the
 * caption. Used for File type, File size, Base address, Entry point, Scan,
 * Endianness, Mode, Architecture, Type, Sections, Time date stamp, Size of
 * image, Programs, Commands, Segments, Libraries and PE "Resources".
 *
 * Conventions shared by the main-window components: children belong to the
 * owner WINDOW and are created once, in screen order (the Tab order); events
 * arrive through the app callback and handle_event says whether one belongs
 * to the component; setters emit no events; captions and tooltips are
 * untranslated strings shown through the app catalog, and retranslate
 * re-applies them; bounds are layout units (xxwidgets_widget_set_frame).
 * Destroy the component before its owner. On a backend without VALUE or
 * GROUP (xxwidgets_app_supports_kind) the value is a LABEL and there is no
 * frame: CONTROL(FRAME) is then NULL. */
typedef struct xxwidgets_value_field xxwidgets_value_field;

typedef enum xxwidgets_value_field_control_id {
    XXWIDGETS_VALUE_FIELD_CAPTION = 0, /* LABEL */
    XXWIDGETS_VALUE_FIELD_FRAME,       /* GROUP */
    XXWIDGETS_VALUE_FIELD_VALUE,       /* VALUE, or COMBOBOX with COMBO; NULL with BUTTONS */
    XXWIDGETS_VALUE_FIELD_ARROW,       /* BUTTON with WITH_ARROW, else NULL */
    XXWIDGETS_VALUE_FIELD_CONTROL_COUNT
} xxwidgets_value_field_control_id;

enum {
    XXWIDGETS_VALUE_FIELD_WITH_ARROW = 1u << 0, /* icon-only Entry button after the value */
    XXWIDGETS_VALUE_FIELD_COMBO = 1u << 1,      /* the value is a COMBOBOX (File type, Scan) */
    XXWIDGETS_VALUE_FIELD_BUTTONS = 1u << 2     /* a row of buttons instead of a value (PE "Resources") */
};

/* caption is untranslated (an XXW_N_ string); it is copied. */
XXWIDGETS_API xxwidgets_status xxwidgets_value_field_create(xxwidgets_widget *owner, const char *caption, unsigned int flags,
                                                          xxwidgets_value_field **out);
XXWIDGETS_API xxwidgets_status xxwidgets_value_field_destroy(xxwidgets_value_field *field);
/* The whole box: caption, frame and (WITH_ARROW) the arrow inside the frame. */
XXWIDGETS_API xxwidgets_status xxwidgets_value_field_set_bounds(xxwidgets_value_field *field, xxwidgets_rect units);
/* The box size that fits the caption and value_sample, the text the value
 * must show in full (e.g. "0000000000000000", measured bold); NULL: the
 * current value (combo boxes: their widest record). Height is the captioned
 * box height (xxwidgets_metrics: L + caption_gap + 2 * frame_inset + H). */
XXWIDGETS_API xxwidgets_status xxwidgets_value_field_get_preferred_size(xxwidgets_value_field *field, const char *value_sample, int *width,
                                                                      int *height);
/* The value as plain text, not bold. */
XXWIDGETS_API xxwidgets_status xxwidgets_value_field_set_text(xxwidgets_value_field *field, const char *utf8);
/* Lower-case hexadecimal (as Qt DIE) zero-padded to digits (1..16: 2, 4, 8 or 16 for 8,
 * 16, 32 or 64 bits); a wider value shows all its digits. Bold when
 * value != 0. COMBO and BUTTONS fields: INVALID_ARGUMENT (also set_text,
 * set_seg_off). */
XXWIDGETS_API xxwidgets_status xxwidgets_value_field_set_hex(xxwidgets_value_field *field, uint64_t value, unsigned int digits);
/* "SSSS:OOOO", e.g. "1000:0000"; bold when either part is nonzero. */
XXWIDGETS_API xxwidgets_status xxwidgets_value_field_set_seg_off(xxwidgets_value_field *field, uint16_t segment, uint16_t offset);
/* Untranslated caption (copied); also the arrow's tooltip. */
XXWIDGETS_API xxwidgets_status xxwidgets_value_field_set_caption(xxwidgets_value_field *field, const char *caption);
/* Untranslated tooltip of the value control (copied); NULL: none. */
XXWIDGETS_API xxwidgets_status xxwidgets_value_field_set_tooltip(xxwidgets_value_field *field, const char *tooltip);
/* BUTTONS only: the borrowed buttons shown left to right inside the frame,
 * each at its preferred width; the caller keeps them and destroys them
 * after the field. NULL/0 removes them. */
XXWIDGETS_API xxwidgets_status xxwidgets_value_field_set_buttons(xxwidgets_value_field *field, xxwidgets_widget *const *buttons, size_t count);
/* Caption, value and arrow together. */
XXWIDGETS_API xxwidgets_status xxwidgets_value_field_set_enabled(xxwidgets_value_field *field, int enabled);
XXWIDGETS_API xxwidgets_status xxwidgets_value_field_set_visible(xxwidgets_value_field *field, int visible);
XXWIDGETS_API xxwidgets_status xxwidgets_value_field_retranslate(xxwidgets_value_field *field);
/* Borrowed; NULL for an id the field does not have. */
XXWIDGETS_API xxwidgets_widget *xxwidgets_value_field_control(const xxwidgets_value_field *field, xxwidgets_value_field_control_id control);
/* 1 when event belongs to the field: CLICK on ARROW, SELECT on a COMBO value
 * (event->value is the record index). *control receives the part. */
XXWIDGETS_API int xxwidgets_value_field_handle_event(xxwidgets_value_field *field, const xxwidgets_event *event,
                                                   xxwidgets_value_field_control_id *control);

#ifdef __cplusplus
}
#endif
#endif
