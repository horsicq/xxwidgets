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
#include "xxwidgets/xxwidgets_file_header.h"
#include "xxwidgets/xxwidgets_combobox.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct xxwidgets_file_header {
    xxwidgets_widget *owner;
    xxwidgets_value_field *fields[XXWIDGETS_FILE_HEADER_FIELD_COUNT];
    int advanced, loaded, visible;
    xxwidgets_rect bounds;
    int has_bounds;
};

/* Caption, value tooltip, value flags, the Qt box width and the sample the
 * value must show in full. */
static const struct {
    const char *caption, *tooltip;
    unsigned int flags;
    int qt_box, qt_value;
    const char *sample;
} field_specs[XXWIDGETS_FILE_HEADER_FIELD_COUNT] = {
    {XXW_N_("File type"), XXW_N_("Type"), XXWIDGETS_VALUE_FIELD_COMBO, 120, 110, NULL},
    {XXW_N_("File size"), XXW_N_("File size"), 0, 130, 120, "9999.99 MiB"},
    {XXW_N_("Base address"), XXW_N_("Base address"), 0, 140, 130, "0000000000000000"},
    {XXW_N_("Entry point"), XXW_N_("Entry point"), XXWIDGETS_VALUE_FIELD_WITH_ARROW, 180, 140, "0000000000000000"}};

static int advanced_field(size_t field)
{
    return field == XXWIDGETS_FILE_HEADER_BASE_ADDRESS || field == XXWIDGETS_FILE_HEADER_ENTRY_POINT;
}

xxwidgets_value_field *xxwidgets_file_header_field(const xxwidgets_file_header *header, xxwidgets_file_header_field_id field)
{
    return header && field >= 0 && field < XXWIDGETS_FILE_HEADER_FIELD_COUNT ? header->fields[field] : NULL;
}

static xxwidgets_status apply_state(xxwidgets_file_header *header)
{
    size_t i;
    for (i = 0; i < XXWIDGETS_FILE_HEADER_FIELD_COUNT; ++i) {
        xxwidgets_status status = xxwidgets_value_field_set_visible(header->fields[i], header->visible && (!advanced_field(i) || header->advanced));
        if (status == XXWIDGETS_OK) status = xxwidgets_value_field_set_enabled(header->fields[i], header->loaded);
        if (status != XXWIDGETS_OK) return status;
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_file_header_create(xxwidgets_widget *owner, unsigned int flags, xxwidgets_file_header **out)
{
    xxwidgets_file_header *header;
    xxwidgets_status status = XXWIDGETS_OK;
    size_t i;
    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || flags) return XXWIDGETS_INVALID_ARGUMENT;
    if (xxwidgets_component_busy(owner->app)) return XXWIDGETS_BUSY;
    header = (xxwidgets_file_header *)calloc(1, sizeof(*header));
    if (!header) return XXWIDGETS_OUT_OF_MEMORY;
    header->owner = owner;
    header->visible = 1;
    for (i = 0; status == XXWIDGETS_OK && i < XXWIDGETS_FILE_HEADER_FIELD_COUNT; ++i) {
        status = xxwidgets_value_field_create(owner, field_specs[i].caption, field_specs[i].flags, &header->fields[i]);
        if (status == XXWIDGETS_OK) status = xxwidgets_value_field_set_tooltip(header->fields[i], field_specs[i].tooltip);
    }
    if (status == XXWIDGETS_OK) status = apply_state(header);
    if (status != XXWIDGETS_OK) {
        xxwidgets_file_header_destroy(header);
        return status;
    }
    *out = header;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_file_header_destroy(xxwidgets_file_header *header)
{
    size_t i;
    if (!header) return XXWIDGETS_INVALID_ARGUMENT;
    if (xxwidgets_component_busy(header->owner->app)) return XXWIDGETS_BUSY;
    for (i = XXWIDGETS_FILE_HEADER_FIELD_COUNT; i-- > 0;) {
        xxwidgets_status status;
        if (!header->fields[i]) continue;
        status = xxwidgets_value_field_destroy(header->fields[i]);
        if (status != XXWIDGETS_OK) return status;
        header->fields[i] = NULL;
    }
    free(header);
    return XXWIDGETS_OK;
}

/* Box widths: at least Qt's, wider when the platform's text needs it. */
static xxwidgets_status box_widths(xxwidgets_file_header *header, int *widths)
{
    xxwidgets_metrics metrics;
    xxwidgets_status status = xxwidgets_app_get_metrics(header->owner->app, &metrics);
    size_t i;
    if (status != XXWIDGETS_OK) return status;
    for (i = 0; i < XXWIDGETS_FILE_HEADER_FIELD_COUNT; ++i) {
        int width, height, qt_box = xxwidgets_component_qt(&metrics, field_specs[i].qt_box);
        status = xxwidgets_value_field_get_preferred_size(header->fields[i], field_specs[i].sample, &width, &height);
        if (status != XXWIDGETS_OK) return status;
        widths[i] = xxwidgets_component_max(width, qt_box);
    }
    return XXWIDGETS_OK;
}

/* Row: File type, File size, an expanding gap, Base address, Entry point. */
static xxwidgets_status row_items(xxwidgets_file_header *header, int advanced, xxwidgets_layout_item *items)
{
    int widths[XXWIDGETS_FILE_HEADER_FIELD_COUNT];
    xxwidgets_status status = box_widths(header, widths);
    if (status != XXWIDGETS_OK) return status;
    memset(items, 0, 5 * sizeof(*items));
    items[0].minimum = items[0].preferred = widths[XXWIDGETS_FILE_HEADER_FILE_TYPE];
    items[1].minimum = items[1].preferred = widths[XXWIDGETS_FILE_HEADER_FILE_SIZE];
    items[2].spacer = 1;
    items[2].stretch = 1;
    items[3].minimum = items[3].preferred = widths[XXWIDGETS_FILE_HEADER_BASE_ADDRESS];
    items[4].minimum = items[4].preferred = widths[XXWIDGETS_FILE_HEADER_ENTRY_POINT];
    /* Simple view: no right group, so no gap either. */
    items[2].hidden = items[3].hidden = items[4].hidden = !advanced;
    return XXWIDGETS_OK;
}

static xxwidgets_status row_size(xxwidgets_file_header *header, int advanced, int minimum, int *width, int *height)
{
    xxwidgets_layout_item items[5];
    xxwidgets_metrics metrics;
    xxwidgets_status status;
    if (!header || !width || !height) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_app_get_metrics(header->owner->app, &metrics);
    if (status == XXWIDGETS_OK) status = row_items(header, advanced, items);
    if (status != XXWIDGETS_OK) return status;
    /* File size, S, the spacer (0 or more), Base address: at least S apart. */
    *width = minimum ? xxwidgets_layout_line_minimum(items, 5, metrics.spacing) : xxwidgets_layout_line_preferred(items, 5, metrics.spacing);
    *height = xxwidgets_layout_caption_box_height(&metrics, metrics.control_height);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_file_header_get_preferred_size(xxwidgets_file_header *header, int *width, int *height)
{
    return header ? row_size(header, header->advanced, 0, width, height) : XXWIDGETS_INVALID_ARGUMENT;
}

xxwidgets_status xxwidgets_file_header_get_minimum_size(xxwidgets_file_header *header, int *width, int *height)
{
    return header ? row_size(header, header->advanced, 1, width, height) : XXWIDGETS_INVALID_ARGUMENT;
}

xxwidgets_status xxwidgets_file_header_view_size(xxwidgets_file_header *header, int advanced, int minimum, int *width, int *height)
{
    if (!header || (advanced != 0 && advanced != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    return row_size(header, advanced, minimum ? 1 : 0, width, height);
}

xxwidgets_status xxwidgets_file_header_set_bounds(xxwidgets_file_header *header, xxwidgets_rect units)
{
    static const xxwidgets_file_header_field_id order[5] = {XXWIDGETS_FILE_HEADER_FILE_TYPE, XXWIDGETS_FILE_HEADER_FILE_SIZE,
                                                            XXWIDGETS_FILE_HEADER_FIELD_COUNT, XXWIDGETS_FILE_HEADER_BASE_ADDRESS,
                                                            XXWIDGETS_FILE_HEADER_ENTRY_POINT};
    xxwidgets_layout_item items[5];
    xxwidgets_metrics metrics;
    xxwidgets_status status;
    size_t i;
    if (!header || units.x < 0 || units.y < 0 || units.width < 1 || units.height < 1) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_app_get_metrics(header->owner->app, &metrics);
    if (status == XXWIDGETS_OK) status = row_items(header, header->advanced, items);
    if (status != XXWIDGETS_OK) return status;
    /* The right group is right-aligned: the spacer takes the slack. */
    xxwidgets_layout_line(items, 5, units.x, units.width, metrics.spacing);
    for (i = 0; i < 5; ++i) {
        xxwidgets_rect box;
        if (order[i] == XXWIDGETS_FILE_HEADER_FIELD_COUNT || items[i].hidden) continue;
        box = xxwidgets_layout_rect(items[i].position, units.y, items[i].size, units.height);
        status = xxwidgets_value_field_set_bounds(header->fields[order[i]], box);
        if (status != XXWIDGETS_OK) return status;
    }
    header->bounds = units;
    header->has_bounds = 1;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_file_header_set_file_types(xxwidgets_file_header *header, const xx_meta_string *records, size_t count, size_t selected)
{
    xxwidgets_widget *combo;
    xxwidgets_status status;
    if (!header || (!records && count) || (count && selected >= count) || count > 0x7fffffff) return XXWIDGETS_INVALID_ARGUMENT;
    combo = xxwidgets_value_field_control(header->fields[XXWIDGETS_FILE_HEADER_FILE_TYPE], XXWIDGETS_VALUE_FIELD_VALUE);
    status = xxwidgets_combobox_set_records(combo, records, count);
    if (status == XXWIDGETS_OK && count) status = xxwidgets_widget_set_value(combo, (int)selected);
    /* The combo may have grown: lay the row out again. */
    if (status == XXWIDGETS_OK && header->has_bounds) status = xxwidgets_file_header_set_bounds(header, header->bounds);
    return status;
}

/* "1023 Bytes", "1.00 KiB", "31.46 MiB": xx_str_bytes_count_to_string with
 * base 1024, in integer arithmetic so no locale changes the separator. */
static void format_size(uint64_t bytes, char *text, size_t capacity)
{
    static const char *const units[] = {"KiB", "MiB", "GiB", "TiB"};
    uint64_t divisor = 1024;
    uint64_t whole, hundredths;
    size_t unit = 0;
    if (bytes < 1024) {
        snprintf(text, capacity, "%llu Bytes", (unsigned long long)bytes);
        return;
    }
    while (unit < 3 && bytes / divisor >= 1024) {
        divisor *= 1024;
        ++unit;
    }
    whole = bytes / divisor;
    hundredths = ((bytes % divisor) * 100 + divisor / 2) / divisor;
    if (hundredths >= 100) {
        ++whole;
        hundredths -= 100;
    }
    snprintf(text, capacity, "%llu.%02u %s", (unsigned long long)whole, (unsigned int)hundredths, units[unit]);
}

xxwidgets_status xxwidgets_file_header_set_file_size(xxwidgets_file_header *header, uint64_t bytes)
{
    char text[48];
    if (!header) return XXWIDGETS_INVALID_ARGUMENT;
    format_size(bytes, text, sizeof(text));
    return xxwidgets_value_field_set_text(header->fields[XXWIDGETS_FILE_HEADER_FILE_SIZE], text);
}

xxwidgets_status xxwidgets_file_header_set_address(xxwidgets_file_header *header, xxwidgets_file_header_field_id field, uint64_t value,
                                                   unsigned int bits)
{
    if (!header || !advanced_field((size_t)field) || (bits != 8 && bits != 16 && bits != 32 && bits != 64)) return XXWIDGETS_INVALID_ARGUMENT;
    return xxwidgets_value_field_set_hex(header->fields[field], value, bits / 4);
}

xxwidgets_status xxwidgets_file_header_set_address_seg_off(xxwidgets_file_header *header, xxwidgets_file_header_field_id field, uint16_t segment,
                                                           uint16_t offset)
{
    if (!header || !advanced_field((size_t)field)) return XXWIDGETS_INVALID_ARGUMENT;
    return xxwidgets_value_field_set_seg_off(header->fields[field], segment, offset);
}

xxwidgets_status xxwidgets_file_header_set_advanced(xxwidgets_file_header *header, int advanced)
{
    xxwidgets_status status;
    if (!header || (advanced != 0 && advanced != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    header->advanced = advanced;
    status = apply_state(header);
    if (status == XXWIDGETS_OK && header->has_bounds) status = xxwidgets_file_header_set_bounds(header, header->bounds);
    return status;
}

xxwidgets_status xxwidgets_file_header_set_loaded(xxwidgets_file_header *header, int loaded)
{
    if (!header || (loaded != 0 && loaded != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    header->loaded = loaded;
    return apply_state(header);
}

xxwidgets_status xxwidgets_file_header_clear(xxwidgets_file_header *header)
{
    xxwidgets_status status;
    size_t i;
    if (!header) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_combobox_set_records(xxwidgets_value_field_control(header->fields[XXWIDGETS_FILE_HEADER_FILE_TYPE], XXWIDGETS_VALUE_FIELD_VALUE),
                                            NULL, 0);
    for (i = XXWIDGETS_FILE_HEADER_FILE_SIZE; status == XXWIDGETS_OK && i < XXWIDGETS_FILE_HEADER_FIELD_COUNT; ++i)
        status = xxwidgets_value_field_set_text(header->fields[i], "");
    return status;
}

xxwidgets_status xxwidgets_file_header_set_visible(xxwidgets_file_header *header, int visible)
{
    if (!header || (visible != 0 && visible != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    header->visible = visible;
    return apply_state(header);
}

xxwidgets_status xxwidgets_file_header_retranslate(xxwidgets_file_header *header)
{
    size_t i;
    if (!header) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < XXWIDGETS_FILE_HEADER_FIELD_COUNT; ++i) {
        xxwidgets_status status = xxwidgets_value_field_retranslate(header->fields[i]);
        if (status != XXWIDGETS_OK) return status;
    }
    return header->has_bounds ? xxwidgets_file_header_set_bounds(header, header->bounds) : XXWIDGETS_OK;
}

int xxwidgets_file_header_handle_event(xxwidgets_file_header *header, const xxwidgets_event *event, xxwidgets_file_header_action *action)
{
    xxwidgets_file_header_action result;
    xxwidgets_value_field_control_id part;
    size_t i;
    if (!header || !event || !event->widget) return 0;
    memset(&result, 0, sizeof(result));
    if (xxwidgets_value_field_handle_event(header->fields[XXWIDGETS_FILE_HEADER_ENTRY_POINT], event, &part) && part == XXWIDGETS_VALUE_FIELD_ARROW) {
        result.id = XXWIDGETS_FILE_HEADER_ACTION_ENTRY_POINT_ARROW;
    } else if (xxwidgets_value_field_handle_event(header->fields[XXWIDGETS_FILE_HEADER_FILE_TYPE], event, &part) && part == XXWIDGETS_VALUE_FIELD_VALUE) {
        const xx_meta_string *record;
        xxwidgets_widget *combo = event->widget;
        if (event->value < 0 || xxwidgets_combobox_get_record(combo, (size_t)event->value, &record) != XXWIDGETS_OK) {
            if (action) *action = result;
            return 1;
        }
        result.id = XXWIDGETS_FILE_HEADER_ACTION_FILE_TYPE_SELECTED;
        result.index = (size_t)event->value;
        result.type_id = record->var.type == XX_VAR_TYPE_UINT64 ? record->var.val.u64 : 0;
    } else {
        /* Any other event of one of the header's own controls is consumed. */
        int ours = 0;
        for (i = 0; i < XXWIDGETS_FILE_HEADER_FIELD_COUNT && !ours; ++i) {
            int id;
            for (id = 0; id < XXWIDGETS_VALUE_FIELD_CONTROL_COUNT; ++id)
                if (xxwidgets_value_field_control(header->fields[i], (xxwidgets_value_field_control_id)id) == event->widget) ours = 1;
        }
        if (!ours) return 0;
    }
    if (action) *action = result;
    return 1;
}
