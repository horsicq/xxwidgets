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

#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_hex_options.h"
#include "xxwidgets/xxwidgets_combobox.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

struct xxwidgets_hex_options_widget {
    xxwidgets_widget *owner;
    xxwidgets_widget *controls[XXWIDGETS_HEX_OPTIONS_CONTROL_COUNT];
    xxwidgets_font font;
};

static int valid_options(const xxwidgets_hex_options *options)
{
    return options &&
        (options->bytes_per_row == 8 || options->bytes_per_row == 16 || options->bytes_per_row == 24 || options->bytes_per_row == 32 || options->bytes_per_row == 48 || options->bytes_per_row == 64) &&
        (options->editable == 0 || options->editable == 1) &&
        (options->bold_nonzero == 0 || options->bold_nonzero == 1) && xxwidgets_font_valid(&options->font);
}

static int valid_bounds(xxwidgets_rect bounds)
{
    return bounds.x >= 0 && bounds.y >= 0 && bounds.width >= 44 && bounds.height >= 5 &&
        bounds.x <= INT_MAX - bounds.width && bounds.y <= INT_MAX - bounds.height;
}

static void option_rects(xxwidgets_rect bounds, xxwidgets_rect rects[XXWIDGETS_HEX_OPTIONS_CONTROL_COUNT])
{
    rects[XXWIDGETS_HEX_OPTIONS_BYTES_PER_ROW_LABEL] = (xxwidgets_rect){bounds.x, bounds.y, 14, 1};
    rects[XXWIDGETS_HEX_OPTIONS_BYTES_PER_ROW] = (xxwidgets_rect){bounds.x + 15, bounds.y, 13, 2};
    rects[XXWIDGETS_HEX_OPTIONS_EDITABLE] = (xxwidgets_rect){bounds.x + 30, bounds.y, bounds.width - 30, 1};
    rects[XXWIDGETS_HEX_OPTIONS_BOLD_NONZERO] = (xxwidgets_rect){bounds.x + 30, bounds.y + 1, bounds.width - 30, 1};
    rects[XXWIDGETS_HEX_OPTIONS_FONT_FAMILY_LABEL] = (xxwidgets_rect){bounds.x, bounds.y + 2, 14, 1};
    rects[XXWIDGETS_HEX_OPTIONS_FONT_FAMILY] = (xxwidgets_rect){bounds.x + 15, bounds.y + 2, bounds.width - 15, 1};
    rects[XXWIDGETS_HEX_OPTIONS_FONT_SIZE_LABEL] = (xxwidgets_rect){bounds.x, bounds.y + 4, 14, 1};
    rects[XXWIDGETS_HEX_OPTIONS_FONT_SIZE] = (xxwidgets_rect){bounds.x + 15, bounds.y + 4, 6, 1};
    rects[XXWIDGETS_HEX_OPTIONS_FONT_SIZE_HINT] = (xxwidgets_rect){bounds.x + 23, bounds.y + 4, bounds.width - 23, 1};
}

static void recover_control(xxwidgets_widget *control)
{
    ++control->app->syncing;
    control->app->ops->sync(control);
    --control->app->syncing;
}

static xxwidgets_status set_choices(xxwidgets_widget *control)
{
    static const char *const names[] = {XXW_N_("8 bytes"), XXW_N_("16 bytes"), XXW_N_("24 bytes"), XXW_N_("32 bytes"), XXW_N_("48 bytes"), XXW_N_("64 bytes")};
    static const uint64_t values[] = {8, 16, 24, 32, 48, 64};
    return xxwidgets_translated_choices(control, names, values, 6);
}

xxwidgets_status xxwidgets_hex_options_init(xxwidgets_hex_options *options)
{
    if (!options) return XXWIDGETS_INVALID_ARGUMENT;
    memset(options, 0, sizeof(*options));
    options->bytes_per_row = 16;
    strcpy(options->font.family, "monospace");
    return XXWIDGETS_OK;
}

xxwidgets_widget *xxwidgets_hex_options_widget_control(const xxwidgets_hex_options_widget *widget,
                                                        xxwidgets_hex_options_control_id control)
{
    return widget && control >= 0 && control < XXWIDGETS_HEX_OPTIONS_CONTROL_COUNT ? widget->controls[control] : NULL;
}

xxwidgets_status xxwidgets_hex_options_widget_get(const xxwidgets_hex_options_widget *widget, xxwidgets_hex_options *options)
{
    xxwidgets_hex_options result;
    xx_var value = {0};
    char size_text[16];
    unsigned int points = 0;
    xxwidgets_status status;
    if (!widget || !options) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_combobox_get_current(widget->controls[XXWIDGETS_HEX_OPTIONS_BYTES_PER_ROW], &value);
    if (status == XXWIDGETS_OK && (value.type != XX_VAR_TYPE_UINT64 || value.val.u64 > UINT_MAX)) status = XXWIDGETS_INVALID_ARGUMENT;
    if (status == XXWIDGETS_OK) result.bytes_per_row = (unsigned int)value.val.u64;
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(widget->controls[XXWIDGETS_HEX_OPTIONS_EDITABLE], &result.editable);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(widget->controls[XXWIDGETS_HEX_OPTIONS_BOLD_NONZERO], &result.bold_nonzero);
    result.font = widget->font;
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_text(widget->controls[XXWIDGETS_HEX_OPTIONS_FONT_FAMILY], result.font.family, sizeof(result.font.family), NULL);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_text(widget->controls[XXWIDGETS_HEX_OPTIONS_FONT_SIZE], size_text, sizeof(size_text), NULL);
    if (status != XXWIDGETS_OK) return status;
    if (!size_text[0]) return XXWIDGETS_INVALID_ARGUMENT;
    for (const char *p = size_text; *p; ++p) {
        if (*p < '0' || *p > '9' || points > 96) return XXWIDGETS_INVALID_ARGUMENT;
        points = points * 10 + (unsigned int)(*p - '0');
    }
    result.font.point_size = points;
    if (!valid_options(&result)) return XXWIDGETS_INVALID_ARGUMENT;
    *options = result;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_hex_options_widget_set(xxwidgets_hex_options_widget *widget, const xxwidgets_hex_options *options)
{
    xxwidgets_widget *columns, *editable, *bold, *family, *size;
    char *previous_family, *previous_size;
    char size_text[16];
    int previous_columns, previous_editable, previous_bold, selected;
    xxwidgets_status status;
    if (!widget || !valid_options(options)) return XXWIDGETS_INVALID_ARGUMENT;
    columns = widget->controls[XXWIDGETS_HEX_OPTIONS_BYTES_PER_ROW];
    editable = widget->controls[XXWIDGETS_HEX_OPTIONS_EDITABLE];
    bold = widget->controls[XXWIDGETS_HEX_OPTIONS_BOLD_NONZERO];
    family = widget->controls[XXWIDGETS_HEX_OPTIONS_FONT_FAMILY];
    size = widget->controls[XXWIDGETS_HEX_OPTIONS_FONT_SIZE];
    previous_family = xxwidgets_strdup(family->text);
    previous_size = xxwidgets_strdup(size->text);
    if (!previous_family || !previous_size) { free(previous_family); free(previous_size); return XXWIDGETS_OUT_OF_MEMORY; }
    previous_columns = columns->value;
    previous_editable = editable->value;
    previous_bold = bold->value;
    selected = options->bytes_per_row == 8 ? 0 : options->bytes_per_row == 16 ? 1 : options->bytes_per_row == 24 ? 2 : options->bytes_per_row == 32 ? 3 : options->bytes_per_row == 48 ? 4 : 5;
    status = xxwidgets_widget_set_value(columns, selected);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(editable, options->editable);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(bold, options->bold_nonzero);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(family, options->font.family);
    snprintf(size_text, sizeof(size_text), "%u", options->font.point_size);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(size, size_text);
    if (status != XXWIDGETS_OK && columns->value != previous_columns) {
        columns->value = previous_columns;
        recover_control(columns);
    }
    if (status != XXWIDGETS_OK && editable->value != previous_editable) {
        editable->value = previous_editable;
        recover_control(editable);
    }
    if (status != XXWIDGETS_OK && bold->value != previous_bold) {
        bold->value = previous_bold;
        recover_control(bold);
    }
    if (status != XXWIDGETS_OK && strcmp(family->text, previous_family)) {
        free(family->text); family->text = previous_family; previous_family = NULL;
        recover_control(family);
    }
    if (status != XXWIDGETS_OK && strcmp(size->text, previous_size)) {
        free(size->text); size->text = previous_size; previous_size = NULL;
        recover_control(size);
    }
    if (status == XXWIDGETS_OK) widget->font = options->font;
    free(previous_family); free(previous_size);
    return status;
}

xxwidgets_status xxwidgets_hex_options_widget_create(xxwidgets_widget *owner, xxwidgets_rect bounds,
                                                      xxwidgets_hex_options_widget **out_widget)
{
    static const char *const labels[XXWIDGETS_HEX_OPTIONS_CONTROL_COUNT] = {
        XXW_N_("Bytes per row"), XXW_N_("Editable"), XXW_N_("Bytes per row"), XXW_N_("Bold non-zero"), "", "0", XXW_N_("Hex font"), XXW_N_("Point size"), XXW_N_("0 = automatic; 4..96 pt")};
    static const xxwidgets_kind kinds[XXWIDGETS_HEX_OPTIONS_CONTROL_COUNT] = {
        XXWIDGETS_COMBOBOX, XXWIDGETS_CHECKBOX, XXWIDGETS_LABEL, XXWIDGETS_CHECKBOX,
        XXWIDGETS_EDIT, XXWIDGETS_EDIT, XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_LABEL};
    xxwidgets_hex_options_widget *widget;
    xxwidgets_hex_options defaults;
    xxwidgets_rect rects[XXWIDGETS_HEX_OPTIONS_CONTROL_COUNT];
    xxwidgets_status status;
    size_t i;
    if (!out_widget) return XXWIDGETS_INVALID_ARGUMENT;
    *out_widget = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !valid_bounds(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing) return XXWIDGETS_BUSY;
    widget = (xxwidgets_hex_options_widget *)calloc(1, sizeof(*widget));
    if (!widget) return XXWIDGETS_OUT_OF_MEMORY;
    widget->owner = owner;
    option_rects(bounds, rects);
    for (i = 0; i < XXWIDGETS_HEX_OPTIONS_CONTROL_COUNT; ++i) {
        status = xxwidgets_widget_create(owner->app, owner, kinds[i], XXW_TR(owner->app, labels[i]), rects[i], &widget->controls[i]);
        if (status != XXWIDGETS_OK) goto failed;
    }
    status = set_choices(widget->controls[XXWIDGETS_HEX_OPTIONS_BYTES_PER_ROW]);
    if (status != XXWIDGETS_OK) goto failed;
    xxwidgets_hex_options_init(&defaults);
    status = xxwidgets_hex_options_widget_set(widget, &defaults);
    if (status != XXWIDGETS_OK) goto failed;
    *out_widget = widget;
    return XXWIDGETS_OK;
failed:
    xxwidgets_hex_options_widget_destroy(widget);
    return status;
}

xxwidgets_status xxwidgets_hex_options_widget_destroy(xxwidgets_hex_options_widget *widget)
{
    size_t i;
    if (!widget) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->owner->app->dispatch_depth || widget->owner->app->polling || widget->owner->app->syncing) return XXWIDGETS_BUSY;
    for (i = 0; i < XXWIDGETS_HEX_OPTIONS_CONTROL_COUNT; ++i) {
        xxwidgets_status status;
        if (!widget->controls[i]) continue;
        status = xxwidgets_widget_destroy(widget->controls[i]);
        if (status != XXWIDGETS_OK) return status;
        widget->controls[i] = NULL;
    }
    free(widget);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_hex_options_widget_set_rect(xxwidgets_hex_options_widget *widget, xxwidgets_rect bounds)
{
    xxwidgets_rect rects[XXWIDGETS_HEX_OPTIONS_CONTROL_COUNT], previous[XXWIDGETS_HEX_OPTIONS_CONTROL_COUNT];
    size_t i;
    if (!widget || !valid_bounds(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    option_rects(bounds, rects);
    for (i = 0; i < XXWIDGETS_HEX_OPTIONS_CONTROL_COUNT; ++i) previous[i] = widget->controls[i]->rect;
    for (i = 0; i < XXWIDGETS_HEX_OPTIONS_CONTROL_COUNT; ++i) {
        xxwidgets_status status = xxwidgets_widget_set_rect(widget->controls[i], rects[i]);
        if (status != XXWIDGETS_OK) {
            size_t j;
            for (j = 0; j < i; ++j) widget->controls[j]->rect = previous[j];
            for (j = 0; j < i; ++j) recover_control(widget->controls[j]);
            return status;
        }
    }
    return XXWIDGETS_OK;
}
