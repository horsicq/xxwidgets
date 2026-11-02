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

#include "xxwidgets_components_internal.h"
#include "xxwidgets/xxwidgets_value_field.h"
#include "xxwidgets/xxwidgets_icons.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct xxwidgets_value_field {
    xxwidgets_widget *owner;
    xxwidgets_widget *controls[XXWIDGETS_VALUE_FIELD_CONTROL_COUNT];
    unsigned int flags;
    char *caption; /* untranslated */
    char *tooltip; /* untranslated, or NULL */
    xxwidgets_widget **buttons;
    size_t button_count;
    xxwidgets_rect bounds;
    int has_bounds;
};

#define FIELD_FLAGS (XXWIDGETS_VALUE_FIELD_WITH_ARROW | XXWIDGETS_VALUE_FIELD_COMBO | XXWIDGETS_VALUE_FIELD_BUTTONS)

static int text_value(const xxwidgets_value_field *field)
{
    return !(field->flags & (XXWIDGETS_VALUE_FIELD_COMBO | XXWIDGETS_VALUE_FIELD_BUTTONS));
}

/* Icon-only arrow: without icons (cells, or a backend without them) it
 * shows Qt's ">" so it stays small; the caption stays its tooltip. */
static const char *arrow_text(const xxwidgets_value_field *field, const xxwidgets_metrics *metrics)
{
    return metrics->small_icon > 0 ? XXW_TR(field->owner->app, field->caption) : ">";
}

static xxwidgets_status apply_texts(xxwidgets_value_field *field)
{
    xxwidgets_app *app = field->owner->app;
    xxwidgets_widget *value = field->controls[XXWIDGETS_VALUE_FIELD_VALUE];
    xxwidgets_widget *arrow = field->controls[XXWIDGETS_VALUE_FIELD_ARROW];
    xxwidgets_metrics metrics;
    xxwidgets_status status = xxwidgets_app_get_metrics(app, &metrics);
    if (status != XXWIDGETS_OK) return status;
    status = xxwidgets_widget_set_text(field->controls[XXWIDGETS_VALUE_FIELD_CAPTION], XXW_TR(app, field->caption));
    if (status == XXWIDGETS_OK && value)
        status = xxwidgets_widget_set_tooltip(value, field->tooltip ? XXW_TR(app, field->tooltip) : NULL);
    if (status == XXWIDGETS_OK && arrow) status = xxwidgets_widget_set_text(arrow, arrow_text(field, &metrics));
    if (status == XXWIDGETS_OK && arrow) status = xxwidgets_widget_set_tooltip(arrow, XXW_TR(app, field->caption));
    return status;
}

xxwidgets_status xxwidgets_value_field_create(xxwidgets_widget *owner, const char *caption, unsigned int flags, xxwidgets_value_field **out)
{
    static const xxwidgets_rect start = {0, 0, 1, 1};
    xxwidgets_value_field *field;
    xxwidgets_app *app;
    xxwidgets_kind value_kind;
    xxwidgets_status status;
    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !caption || !xxwidgets_valid_utf8(caption) || (flags & ~(unsigned int)FIELD_FLAGS) ||
        ((flags & XXWIDGETS_VALUE_FIELD_COMBO) && (flags & XXWIDGETS_VALUE_FIELD_BUTTONS)))
        return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (xxwidgets_component_busy(app)) return XXWIDGETS_BUSY;
    field = (xxwidgets_value_field *)calloc(1, sizeof(*field));
    if (!field) return XXWIDGETS_OUT_OF_MEMORY;
    field->owner = owner;
    field->flags = flags;
    field->caption = xxwidgets_strdup(caption);
    if (!field->caption) {
        free(field);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    /* Creation order is the Tab order: caption, frame, value, arrow. */
    status = xxwidgets_widget_create(app, owner, XXWIDGETS_LABEL, XXW_TR(app, caption), start, &field->controls[XXWIDGETS_VALUE_FIELD_CAPTION]);
    if (status == XXWIDGETS_OK && xxwidgets_app_supports_kind(app, XXWIDGETS_GROUP))
        status = xxwidgets_widget_create(app, owner, XXWIDGETS_GROUP, "", start, &field->controls[XXWIDGETS_VALUE_FIELD_FRAME]);
    if (status == XXWIDGETS_OK && !(flags & XXWIDGETS_VALUE_FIELD_BUTTONS)) {
        if (flags & XXWIDGETS_VALUE_FIELD_COMBO) value_kind = XXWIDGETS_COMBOBOX;
        else value_kind = xxwidgets_app_supports_kind(app, XXWIDGETS_VALUE) ? XXWIDGETS_VALUE : XXWIDGETS_LABEL;
        status = xxwidgets_widget_create(app, owner, value_kind, "", start, &field->controls[XXWIDGETS_VALUE_FIELD_VALUE]);
        if (status == XXWIDGETS_OK && value_kind == XXWIDGETS_LABEL)
            status = xxwidgets_widget_set_alignment(field->controls[XXWIDGETS_VALUE_FIELD_VALUE], XXWIDGETS_ALIGN_CENTER);
    }
    if (status == XXWIDGETS_OK && (flags & XXWIDGETS_VALUE_FIELD_WITH_ARROW)) {
        status = xxwidgets_widget_create(app, owner, XXWIDGETS_BUTTON, XXW_TR(app, caption), start, &field->controls[XXWIDGETS_VALUE_FIELD_ARROW]);
        if (status == XXWIDGETS_OK)
            status = xxwidgets_button_set_icon_id(field->controls[XXWIDGETS_VALUE_FIELD_ARROW], XXWIDGETS_ICON_ENTRY, XXWIDGETS_ICON_ONLY);
    }
    if (status == XXWIDGETS_OK) status = apply_texts(field);
    if (status != XXWIDGETS_OK) {
        xxwidgets_value_field_destroy(field);
        return status;
    }
    *out = field;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_value_field_destroy(xxwidgets_value_field *field)
{
    size_t i;
    if (!field) return XXWIDGETS_INVALID_ARGUMENT;
    if (xxwidgets_component_busy(field->owner->app)) return XXWIDGETS_BUSY;
    for (i = XXWIDGETS_VALUE_FIELD_CONTROL_COUNT; i-- > 0;) {
        xxwidgets_status status;
        if (!field->controls[i]) continue;
        status = xxwidgets_widget_destroy(field->controls[i]);
        if (status != XXWIDGETS_OK) return status;
        field->controls[i] = NULL;
    }
    free(field->buttons);
    free(field->caption);
    free(field->tooltip);
    free(field);
    return XXWIDGETS_OK;
}

xxwidgets_widget *xxwidgets_value_field_control(const xxwidgets_value_field *field, xxwidgets_value_field_control_id control)
{
    return field && control >= 0 && control < XXWIDGETS_VALUE_FIELD_CONTROL_COUNT ? field->controls[control] : NULL;
}

static xxwidgets_status arrow_width(const xxwidgets_value_field *field, const xxwidgets_metrics *metrics, int *width)
{
    int w, h;
    xxwidgets_status status;
    *width = 0;
    if (!field->controls[XXWIDGETS_VALUE_FIELD_ARROW]) return XXWIDGETS_OK;
    status = xxwidgets_widget_get_preferred_size(field->controls[XXWIDGETS_VALUE_FIELD_ARROW], &w, &h);
    if (status != XXWIDGETS_OK) return status;
    *width = metrics->unit_is_cell ? w : xxwidgets_component_icon_button(metrics, w);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_value_field_set_bounds(xxwidgets_value_field *field, xxwidgets_rect units)
{
    xxwidgets_metrics metrics;
    xxwidgets_layout_caption_box box;
    xxwidgets_status status;
    int arrow = 0, value_width, x;
    size_t i;
    if (!field || units.x < 0 || units.y < 0 || units.width < 1 || units.height < 1) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_app_get_metrics(field->owner->app, &metrics);
    if (status == XXWIDGETS_OK) status = arrow_width(field, &metrics, &arrow);
    if (status != XXWIDGETS_OK) return status;
    xxwidgets_layout_caption_box_split(&metrics, units, metrics.control_height, &box);
    status = xxwidgets_layout_place(field->controls[XXWIDGETS_VALUE_FIELD_CAPTION], box.caption);
    if (status == XXWIDGETS_OK) status = xxwidgets_layout_place(field->controls[XXWIDGETS_VALUE_FIELD_FRAME], box.frame);
    value_width = box.control.width - (arrow ? arrow + metrics.spacing : 0);
    if (value_width < 1) value_width = 1;
    if (status == XXWIDGETS_OK)
        status = xxwidgets_layout_place(field->controls[XXWIDGETS_VALUE_FIELD_VALUE],
                                        xxwidgets_layout_rect(box.control.x, box.control.y, value_width, box.control.height));
    if (status == XXWIDGETS_OK && arrow)
        status = xxwidgets_layout_place(field->controls[XXWIDGETS_VALUE_FIELD_ARROW],
                                        xxwidgets_layout_rect(box.control.x + value_width + metrics.spacing, box.control.y, arrow, box.control.height));
    /* BUTTONS: left to right at their preferred widths. */
    x = box.control.x;
    for (i = 0; status == XXWIDGETS_OK && i < field->button_count; ++i) {
        int w, h;
        if (!field->buttons[i]->visible) continue;
        status = xxwidgets_widget_get_preferred_size(field->buttons[i], &w, &h);
        if (status == XXWIDGETS_OK) status = xxwidgets_layout_place(field->buttons[i], xxwidgets_layout_rect(x, box.control.y, w, box.control.height));
        x += w + metrics.spacing;
    }
    if (status == XXWIDGETS_OK) {
        field->bounds = units;
        field->has_bounds = 1;
    }
    return status;
}

xxwidgets_status xxwidgets_value_field_get_preferred_size(xxwidgets_value_field *field, const char *value_sample, int *width, int *height)
{
    xxwidgets_metrics metrics;
    xxwidgets_widget *value;
    xxwidgets_status status;
    int content = 0, w, h, arrow = 0, caption_width;
    size_t i;
    if (!field || !width || !height || (value_sample && !xxwidgets_valid_utf8(value_sample))) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_app_get_metrics(field->owner->app, &metrics);
    if (status == XXWIDGETS_OK) status = arrow_width(field, &metrics, &arrow);
    if (status != XXWIDGETS_OK) return status;
    value = field->controls[XXWIDGETS_VALUE_FIELD_VALUE];
    if (value) {
        if (value_sample && text_value(field)) {
            /* Samples are hex numbers that show bold when nonzero. */
            int bold = value->bold;
            status = bold ? XXWIDGETS_OK : xxwidgets_widget_set_bold(value, 1);
            if (status == XXWIDGETS_OK) status = xxwidgets_widget_measure_text(value, value_sample, &w, &h);
            if (!bold) {
                xxwidgets_status restore = xxwidgets_widget_set_bold(value, 0);
                if (status == XXWIDGETS_OK) status = restore;
            }
        } else
            status = xxwidgets_widget_get_preferred_size(value, &w, &h);
        if (status != XXWIDGETS_OK) return status;
        content = w;
    }
    for (i = 0; i < field->button_count; ++i) {
        if (!field->buttons[i]->visible) continue;
        status = xxwidgets_widget_get_preferred_size(field->buttons[i], &w, &h);
        if (status != XXWIDGETS_OK) return status;
        content += (content ? metrics.spacing : 0) + w;
    }
    if (arrow) content += metrics.spacing + arrow;
    status = xxwidgets_widget_get_preferred_size(field->controls[XXWIDGETS_VALUE_FIELD_CAPTION], &caption_width, &h);
    if (status != XXWIDGETS_OK) return status;
    *width = xxwidgets_component_max(xxwidgets_layout_caption_box_width(&metrics, content), caption_width);
    *height = xxwidgets_layout_caption_box_height(&metrics, metrics.control_height);
    return XXWIDGETS_OK;
}

static xxwidgets_status set_value(xxwidgets_value_field *field, const char *text, int bold)
{
    xxwidgets_widget *value;
    xxwidgets_status status;
    if (!field || !text || !text_value(field) || !xxwidgets_valid_utf8(text)) return XXWIDGETS_INVALID_ARGUMENT;
    value = field->controls[XXWIDGETS_VALUE_FIELD_VALUE];
    status = xxwidgets_widget_set_text(value, text);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_bold(value, bold);
    return status;
}

xxwidgets_status xxwidgets_value_field_set_text(xxwidgets_value_field *field, const char *utf8)
{
    return set_value(field, utf8 ? utf8 : "", 0);
}

xxwidgets_status xxwidgets_value_field_set_hex(xxwidgets_value_field *field, uint64_t value, unsigned int digits)
{
    char text[24];
    if (digits < 1 || digits > 16) return XXWIDGETS_INVALID_ARGUMENT;
    /* At least digits digits; a wider value shows in full. */
    snprintf(text, sizeof(text), "%0*llx", (int)digits, (unsigned long long)value);
    return set_value(field, text, value != 0);
}

xxwidgets_status xxwidgets_value_field_set_seg_off(xxwidgets_value_field *field, uint16_t segment, uint16_t offset)
{
    char text[16];
    snprintf(text, sizeof(text), "%04X:%04X", (unsigned int)segment, (unsigned int)offset);
    return set_value(field, text, segment != 0 || offset != 0);
}

static xxwidgets_status replace_string(char **slot, const char *text)
{
    char *copy = NULL;
    if (text) {
        if (!xxwidgets_valid_utf8(text)) return XXWIDGETS_INVALID_ARGUMENT;
        copy = xxwidgets_strdup(text);
        if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    }
    free(*slot);
    *slot = copy;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_value_field_set_caption(xxwidgets_value_field *field, const char *caption)
{
    xxwidgets_status status;
    if (!field || !caption) return XXWIDGETS_INVALID_ARGUMENT;
    status = replace_string(&field->caption, caption);
    return status == XXWIDGETS_OK ? apply_texts(field) : status;
}

xxwidgets_status xxwidgets_value_field_set_tooltip(xxwidgets_value_field *field, const char *tooltip)
{
    xxwidgets_status status;
    if (!field) return XXWIDGETS_INVALID_ARGUMENT;
    status = replace_string(&field->tooltip, tooltip && *tooltip ? tooltip : NULL);
    return status == XXWIDGETS_OK ? apply_texts(field) : status;
}

xxwidgets_status xxwidgets_value_field_set_buttons(xxwidgets_value_field *field, xxwidgets_widget *const *buttons, size_t count)
{
    xxwidgets_widget **copy = NULL;
    size_t i;
    if (!field || !(field->flags & XXWIDGETS_VALUE_FIELD_BUTTONS) || (!buttons && count) || count > 64) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < count; ++i)
        if (!buttons[i] || buttons[i]->parent != field->owner) return XXWIDGETS_INVALID_ARGUMENT;
    if (count) {
        copy = (xxwidgets_widget **)malloc(count * sizeof(*copy));
        if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
        memcpy(copy, buttons, count * sizeof(*copy));
    }
    free(field->buttons);
    field->buttons = copy;
    field->button_count = count;
    return field->has_bounds ? xxwidgets_value_field_set_bounds(field, field->bounds) : XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_value_field_set_enabled(xxwidgets_value_field *field, int enabled)
{
    static const xxwidgets_value_field_control_id parts[] = {XXWIDGETS_VALUE_FIELD_CAPTION, XXWIDGETS_VALUE_FIELD_VALUE, XXWIDGETS_VALUE_FIELD_ARROW};
    size_t i;
    if (!field || (enabled != 0 && enabled != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
        xxwidgets_status status;
        if (!field->controls[parts[i]]) continue;
        status = xxwidgets_widget_set_enabled(field->controls[parts[i]], enabled);
        if (status != XXWIDGETS_OK) return status;
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_value_field_set_visible(xxwidgets_value_field *field, int visible)
{
    size_t i;
    if (!field || (visible != 0 && visible != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < XXWIDGETS_VALUE_FIELD_CONTROL_COUNT; ++i) {
        xxwidgets_status status;
        if (!field->controls[i]) continue;
        status = xxwidgets_widget_set_visible(field->controls[i], visible);
        if (status != XXWIDGETS_OK) return status;
    }
    /* The borrowed buttons sit inside the frame: they go with it. */
    for (i = 0; i < field->button_count; ++i) {
        xxwidgets_status status = xxwidgets_widget_set_visible(field->buttons[i], visible);
        if (status != XXWIDGETS_OK) return status;
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_value_field_retranslate(xxwidgets_value_field *field)
{
    xxwidgets_status status;
    if (!field) return XXWIDGETS_INVALID_ARGUMENT;
    status = apply_texts(field);
    if (status == XXWIDGETS_OK && field->has_bounds) status = xxwidgets_value_field_set_bounds(field, field->bounds);
    return status;
}

int xxwidgets_value_field_handle_event(xxwidgets_value_field *field, const xxwidgets_event *event, xxwidgets_value_field_control_id *control)
{
    xxwidgets_widget *arrow, *value;
    if (!field || !event || !event->widget) return 0;
    arrow = field->controls[XXWIDGETS_VALUE_FIELD_ARROW];
    value = field->controls[XXWIDGETS_VALUE_FIELD_VALUE];
    if (arrow && event->widget == arrow && event->type == XXWIDGETS_EVENT_CLICK && xxwidgets_component_live(arrow)) {
        if (control) *control = XXWIDGETS_VALUE_FIELD_ARROW;
        return 1;
    }
    if (value && event->widget == value && (field->flags & XXWIDGETS_VALUE_FIELD_COMBO) && event->type == XXWIDGETS_EVENT_SELECT &&
        xxwidgets_component_live(value)) {
        if (control) *control = XXWIDGETS_VALUE_FIELD_VALUE;
        return 1;
    }
    return 0;
}
