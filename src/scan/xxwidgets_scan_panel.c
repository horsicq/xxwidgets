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
#include "xxfclib/scan/xx_scan_color.h"
#include "xxwidgets/xxwidgets_scan_panel.h"
#include "xxwidgets/xxwidgets_combobox.h"
#include "xxwidgets_layout.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define SCAN_PANEL_OPTIONS (XXWIDGETS_SCAN_PANEL_ENGINE_ROW | XXWIDGETS_SCAN_PANEL_HIDE_FILE_TYPE | \
    XXWIDGETS_SCAN_PANEL_FIXED_CAPTIONS | XXWIDGETS_SCAN_PANEL_HIDE_TAG_FILTERS)

struct xxwidgets_scan_panel {
    xxwidgets_widget *owner;
    xxwidgets_widget *controls[XXWIDGETS_SCAN_PANEL_CONTROL_COUNT];
    char *report;
    size_t count;
    unsigned int options;
    int visible;
    /* Engine-row look (ENGINE_ROW), created hidden with the rest and NULL on
     * a backend without the kind: GROUP frames around the captioned boxes and
     * VALUE twins of the four file-info LABELs and of the elapsed time. */
    xxwidgets_widget *engine_frame, *info_frames[4], *info_values[4], *elapsed_value;
};

static const xxwidgets_scan_panel_control_id info_controls[4][2] = {
    {XXWIDGETS_SCAN_PANEL_ENDIANNESS_LABEL, XXWIDGETS_SCAN_PANEL_ENDIANNESS},
    {XXWIDGETS_SCAN_PANEL_MODE_LABEL, XXWIDGETS_SCAN_PANEL_MODE},
    {XXWIDGETS_SCAN_PANEL_ARCHITECTURE_LABEL, XXWIDGETS_SCAN_PANEL_ARCHITECTURE},
    {XXWIDGETS_SCAN_PANEL_TYPE_LABEL, XXWIDGETS_SCAN_PANEL_TYPE}};

static int panel_rect(xxwidgets_rect bounds)
{
    return bounds.x >= 0 && bounds.y >= 0 && bounds.width >= 44 && bounds.height >= 14 && bounds.x <= INT_MAX - bounds.width && bounds.y <= INT_MAX - bounds.height;
}

static int at_most(int value, int limit) { return value < limit ? value : limit; }
static int at_least(int value, int limit) { return value > limit ? value : limit; }

/* Detect It Easy's arrangement: three rows on top, four rows of buttons below
 * (the Scan button spans them), the tree in between. */
static void control_rects(xxwidgets_rect bounds, xxwidgets_rect *rects)
{
    int x = bounds.x, y = bounds.y, width = bounds.width, height = bounds.height;
    int field = at_least(at_most((width - 24) / 4 - 1, 14), 6), type = width - 4 * (field + 1);
    int left = width - 11, row_a = y + height - 4, row_b = y + height - 2;
    int signatures = at_most(12, left / 4), avail = left - signatures - 1;
    int flags = at_least(avail * 22 / 100, 4), tags = at_least((avail - 2 * flags - 3) / 2, 1);
    int directory = at_most(11, left / 4), log = at_most(7, left / 6), elapsed = at_most(10, left / 4), report = 5;
    static const xxwidgets_scan_panel_control_id info[4][2] = {
        {XXWIDGETS_SCAN_PANEL_ENDIANNESS_LABEL, XXWIDGETS_SCAN_PANEL_ENDIANNESS},
        {XXWIDGETS_SCAN_PANEL_MODE_LABEL, XXWIDGETS_SCAN_PANEL_MODE},
        {XXWIDGETS_SCAN_PANEL_ARCHITECTURE_LABEL, XXWIDGETS_SCAN_PANEL_ARCHITECTURE},
        {XXWIDGETS_SCAN_PANEL_TYPE_LABEL, XXWIDGETS_SCAN_PANEL_TYPE}};
    int i, at;
    rects[XXWIDGETS_SCAN_PANEL_FILE_TYPE_LABEL] = (xxwidgets_rect){x, y, type, 1};
    rects[XXWIDGETS_SCAN_PANEL_FILE_TYPE] = (xxwidgets_rect){x, y + 1, type, 2};
    for (i = 0; i < 4; ++i) {
        at = x + type + 1 + i * (field + 1);
        rects[info[i][0]] = (xxwidgets_rect){at, y, field, 1};
        rects[info[i][1]] = (xxwidgets_rect){at, y + 1, field, 2};
    }
    rects[XXWIDGETS_SCAN_PANEL_RESULTS] = (xxwidgets_rect){x, y + 3, width, height - 7};
    at = x;
    rects[XXWIDGETS_SCAN_PANEL_SIGNATURES] = (xxwidgets_rect){at, row_a, signatures, 2};
    at += signatures + 1;
    rects[XXWIDGETS_SCAN_PANEL_FLAGS] = (xxwidgets_rect){at, row_a, flags, 2};
    at += flags + 1;
    rects[XXWIDGETS_SCAN_PANEL_DATABASES] = (xxwidgets_rect){at, row_a, flags, 2};
    at += flags + 1;
    rects[XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS] = (xxwidgets_rect){at, row_a, tags, 2};
    at += tags + 1;
    rects[XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS] = (xxwidgets_rect){at, row_a, tags, 2};
    rects[XXWIDGETS_SCAN_PANEL_SCAN] = (xxwidgets_rect){x + width - 10, row_a, 10, 4};
    rects[XXWIDGETS_SCAN_PANEL_DIRECTORY] = (xxwidgets_rect){x, row_b, directory, 2};
    rects[XXWIDGETS_SCAN_PANEL_LOG] = (xxwidgets_rect){x + directory + 1, row_b, log, 2};
    rects[XXWIDGETS_SCAN_PANEL_ELAPSED] = (xxwidgets_rect){x + left - elapsed, row_b, elapsed, 2};
    rects[XXWIDGETS_SCAN_PANEL_REPORT] = (xxwidgets_rect){x + left - elapsed - report - 1, row_b, report, 2};
    /* Hidden: the combo boxes carry their own names, as in DIE. */
    rects[XXWIDGETS_SCAN_PANEL_FLAGS_LABEL] = rects[XXWIDGETS_SCAN_PANEL_DATABASES_LABEL] =
        rects[XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS_LABEL] = rects[XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS_LABEL] = (xxwidgets_rect){x, row_a, 1, 1};
    /* The engine row has no cell layout: set_bounds places it. */
    rects[XXWIDGETS_SCAN_PANEL_ENGINE] = rects[XXWIDGETS_SCAN_PANEL_ENGINE_LABEL] = (xxwidgets_rect){x, row_a, 1, 1};
}

static const xxwidgets_scan_panel_control_id hidden_labels[4] = {
    XXWIDGETS_SCAN_PANEL_FLAGS_LABEL, XXWIDGETS_SCAN_PANEL_DATABASES_LABEL,
    XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS_LABEL, XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS_LABEL};

/* With ENGINE_ROW the VALUE twins stand for the file-info LABELs and the
 * elapsed time. */
static xxwidgets_widget *shown_value(const xxwidgets_scan_panel *panel, xxwidgets_scan_panel_control_id control)
{
    size_t i;
    if (!(panel->options & XXWIDGETS_SCAN_PANEL_ENGINE_ROW)) return NULL;
    if (control == XXWIDGETS_SCAN_PANEL_ELAPSED) return panel->elapsed_value;
    for (i = 0; i < 4; ++i)
        if (info_controls[i][1] == control) return panel->info_values[i];
    return NULL;
}

xxwidgets_widget *xxwidgets_scan_panel_control(const xxwidgets_scan_panel *panel, xxwidgets_scan_panel_control_id control)
{
    xxwidgets_widget *value;
    if (!panel || control < 0 || control >= XXWIDGETS_SCAN_PANEL_CONTROL_COUNT) return NULL;
    value = shown_value(panel, control);
    return value ? value : panel->controls[control];
}

static xxwidgets_status fill_choices(xxwidgets_widget *widget, const char *const *names, size_t count)
{
    static const uint64_t values[] = {1, 2, 4, 8, 16, 32};
    return xxwidgets_translated_choices(widget, names, values, count);
}

static xxwidgets_status get_checks(const xxwidgets_widget *widget, unsigned int *mask, size_t count)
{
    unsigned int value = 0;
    size_t i;
    if (!widget || !mask || xxwidgets_combobox_count(widget) != count) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < count; ++i) {
        int checked;
        xxwidgets_status status = xxwidgets_checkcombobox_is_checked(widget, i, &checked);
        if (status != XXWIDGETS_OK) return status;
        if (checked) value |= 1u << i;
    }
    *mask = value;
    return XXWIDGETS_OK;
}

static xxwidgets_status set_checks(xxwidgets_widget *widget, unsigned int mask, size_t count)
{
    return xxwidgets_checkcombobox_set_mask(widget, mask, count);
}

/* Logical rollback is unconditional even if the native recovery also fails.
 * All setters suppress notifications during this best-effort recovery. */
static void recover_control(xxwidgets_widget *widget)
{
    ++widget->app->syncing;
    widget->app->ops->sync(widget);
    --widget->app->syncing;
}

static void recover_enabled(xxwidgets_widget *widget, int enabled)
{
    widget->enabled = enabled;
    recover_control(widget);
}

xxwidgets_status xxwidgets_scan_panel_set_flags(xxwidgets_scan_panel *panel, unsigned int flags)
{
    return panel ? set_checks(panel->controls[XXWIDGETS_SCAN_PANEL_FLAGS], flags, 6) : XXWIDGETS_INVALID_ARGUMENT;
}
xxwidgets_status xxwidgets_scan_panel_get_flags(const xxwidgets_scan_panel *panel, unsigned int *flags)
{
    return panel ? get_checks(panel->controls[XXWIDGETS_SCAN_PANEL_FLAGS], flags, 6) : XXWIDGETS_INVALID_ARGUMENT;
}
/* The Database combo has one item, Custom, which stands for the CUSTOM bit
 * (1u << 1): the check-combo mask itself only knows bit 0. Bit 0, the
 * deprecated EXTRA bit, is accepted and ignored. */
xxwidgets_status xxwidgets_scan_panel_set_databases(xxwidgets_scan_panel *panel, unsigned int databases)
{
    if (!panel || (databases & ~3u)) return XXWIDGETS_INVALID_ARGUMENT;
    return set_checks(panel->controls[XXWIDGETS_SCAN_PANEL_DATABASES], (databases & XXWIDGETS_SCAN_DATABASE_CUSTOM) ? 1u : 0u, 1);
}
xxwidgets_status xxwidgets_scan_panel_get_databases(const xxwidgets_scan_panel *panel, unsigned int *databases)
{
    unsigned int checks;
    xxwidgets_status status;
    if (!panel || !databases) return XXWIDGETS_INVALID_ARGUMENT;
    status = get_checks(panel->controls[XXWIDGETS_SCAN_PANEL_DATABASES], &checks, 1);
    if (status == XXWIDGETS_OK) *databases = (checks & 1u) ? XXWIDGETS_SCAN_DATABASE_CUSTOM : 0u;
    return status;
}

static int tag_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static uint32_t tag_utf8_codepoint(const unsigned char **position)
{
    const unsigned char *text = *position;
    uint32_t codepoint;
    if (text[0] < 0x80) {
        codepoint = text[0];
        *position = text + 1;
    } else if (text[0] < 0xe0) {
        codepoint = ((uint32_t)(text[0] & 0x1f) << 6) | (uint32_t)(text[1] & 0x3f);
        *position = text + 2;
    } else if (text[0] < 0xf0) {
        codepoint = ((uint32_t)(text[0] & 0x0f) << 12) | ((uint32_t)(text[1] & 0x3f) << 6) | (uint32_t)(text[2] & 0x3f);
        *position = text + 3;
    } else {
        codepoint = ((uint32_t)(text[0] & 0x07) << 18) | ((uint32_t)(text[1] & 0x3f) << 12) |
            ((uint32_t)(text[2] & 0x3f) << 6) | (uint32_t)(text[3] & 0x3f);
        *position = text + 4;
    }
    return codepoint;
}

/* The combo API uses wide display labels but xxwidgets intentionally does not
 * depend on xxfclib's string module. Input has already passed the widget
 * library's UTF-8 validator, so this local converter only owns malloc memory. */
static wchar_t *tag_utf8_to_wide(const char *text)
{
    const unsigned char *position = (const unsigned char *)text;
    size_t count = 0, index = 0;
    wchar_t *wide;
    while (*position) {
        uint32_t codepoint = tag_utf8_codepoint(&position);
        size_t units = (sizeof(wchar_t) == 2 && codepoint > 0xffff) ? 2u : 1u;
        if (count > SIZE_MAX - units) return NULL;
        count += units;
    }
    if (count > SIZE_MAX / sizeof(*wide) - 1) return NULL;
    wide = (wchar_t *)malloc((count + 1) * sizeof(*wide));
    if (!wide) return NULL;
    position = (const unsigned char *)text;
    while (*position) {
        uint32_t codepoint = tag_utf8_codepoint(&position);
        if (sizeof(wchar_t) == 2 && codepoint > 0xffff) {
            codepoint -= 0x10000;
            wide[index++] = (wchar_t)(0xd800 + (codepoint >> 10));
            wide[index++] = (wchar_t)(0xdc00 + (codepoint & 0x3ff));
        } else {
            wide[index++] = (wchar_t)codepoint;
        }
    }
    wide[index] = 0;
    return wide;
}

static void free_tag_records(xx_meta_string *records, xx_str_w_s *labels, char **texts, size_t count)
{
    size_t i;
    if (labels)
        for (i = 0; i < count; ++i) free(labels[i].data);
    if (texts)
        for (i = 0; i < count; ++i) free(texts[i]);
    free(records);
    free(labels);
    free(texts);
}

static xxwidgets_status create_tag_records(const char *tags, xx_meta_string **out_records,
    xx_str_w_s **out_labels, char ***out_texts, size_t *out_count)
{
    xx_meta_string *records = NULL;
    xx_str_w_s *labels = NULL;
    char **texts = NULL;
    const char *position;
    size_t count = 0, index = 0;
    if (!tags || !out_records || !out_labels || !out_texts || !out_count || !xxwidgets_valid_utf8(tags))
        return XXWIDGETS_INVALID_ARGUMENT;
    position = tags;
    while (*position) {
        const char *start, *end;
        while (*position == ';' || tag_space(*position)) ++position;
        start = position;
        while (*position && *position != ';') ++position;
        end = position;
        while (end > start && tag_space(end[-1])) --end;
        if (end > start) ++count;
        if (*position) ++position;
    }
    if (!count) {
        *out_records = NULL;
        *out_labels = NULL;
        *out_texts = NULL;
        *out_count = 0;
        return XXWIDGETS_OK;
    }
    if (count > INT_MAX || count > SIZE_MAX / sizeof(*records) ||
        count > SIZE_MAX / sizeof(*labels) || count > SIZE_MAX / sizeof(*texts))
        return XXWIDGETS_INVALID_ARGUMENT;
    records = (xx_meta_string *)calloc(count, sizeof(*records));
    labels = (xx_str_w_s *)calloc(count, sizeof(*labels));
    texts = (char **)calloc(count, sizeof(*texts));
    if (!records || !labels || !texts) goto out_of_memory;
    position = tags;
    while (*position) {
        const char *start, *end;
        size_t length;
        while (*position == ';' || tag_space(*position)) ++position;
        start = position;
        while (*position && *position != ';') ++position;
        end = position;
        while (end > start && tag_space(end[-1])) --end;
        if (end == start) {
            if (*position) ++position;
            continue;
        }
        length = (size_t)(end - start);
        texts[index] = (char *)malloc(length + 1);
        if (!texts[index]) goto out_of_memory;
        memcpy(texts[index], start, length);
        texts[index][length] = 0;
        labels[index].data = tag_utf8_to_wide(texts[index]);
        if (!labels[index].data) goto out_of_memory;
        labels[index].length = wcslen(labels[index].data);
        labels[index].capacity = labels[index].length + 1;
        labels[index].is_view = true;
        records[index].meta_string = labels + index;
        records[index].var.type = XX_VAR_TYPE_STRING_VIEW;
        records[index].var.val.str.ptr = texts[index];
        records[index].var.val.str.len = length;
        ++index;
        if (*position) ++position;
    }
    *out_records = records;
    *out_labels = labels;
    *out_texts = texts;
    *out_count = count;
    return XXWIDGETS_OK;
out_of_memory:
    free_tag_records(records, labels, texts, count);
    return XXWIDGETS_OUT_OF_MEMORY;
}

xxwidgets_status xxwidgets_scan_panel_set_available_tags(xxwidgets_scan_panel *panel, const char *tags)
{
    xx_meta_string *records;
    xx_str_w_s *labels;
    char **texts;
    size_t count;
    xxwidgets_status status;
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    if (!tags) tags = "";
    status = create_tag_records(tags, &records, &labels, &texts, &count);
    if (status != XXWIDGETS_OK) return status;
    status = xxwidgets_checkcombobox_set_shared_records(panel->controls[XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS],
        panel->controls[XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS], records, count, count != 0);
    free_tag_records(records, labels, texts, count);
    return status;
}

static xxwidgets_status get_selected_tags(const xxwidgets_widget *widget, char *buffer, size_t capacity, size_t *required)
{
    size_t i, length = 0, position = 0;
    if (!widget || (!buffer && capacity)) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < xxwidgets_combobox_count(widget); ++i) {
        const xx_meta_string *record;
        int checked;
        size_t tag_length, separator;
        if (xxwidgets_checkcombobox_is_checked(widget, i, &checked) != XXWIDGETS_OK) return XXWIDGETS_INVALID_ARGUMENT;
        if (!checked) continue;
        if (xxwidgets_combobox_get_record(widget, i, &record) != XXWIDGETS_OK ||
            (record->var.type != XX_VAR_TYPE_STRING && record->var.type != XX_VAR_TYPE_STRING_VIEW) ||
            (!record->var.val.str.ptr && record->var.val.str.len)) return XXWIDGETS_INVALID_ARGUMENT;
        tag_length = record->var.val.str.len;
        separator = length ? 1u : 0u;
        if (length > SIZE_MAX - 1u - separator ||
            tag_length > SIZE_MAX - 1u - separator - length) return XXWIDGETS_INVALID_ARGUMENT;
        length += tag_length + separator;
    }
    if (required) *required = length + 1;
    if (!buffer && !capacity) return XXWIDGETS_OK;
    if (!capacity) return XXWIDGETS_BUFFER_TOO_SMALL;
    for (i = 0; i < xxwidgets_combobox_count(widget) && position < capacity - 1; ++i) {
        const xx_meta_string *record;
        int checked;
        size_t copied;
        if (xxwidgets_checkcombobox_is_checked(widget, i, &checked) != XXWIDGETS_OK || !checked) continue;
        if (xxwidgets_combobox_get_record(widget, i, &record) != XXWIDGETS_OK) return XXWIDGETS_INVALID_ARGUMENT;
        if (position && position < capacity - 1) buffer[position++] = ';';
        copied = record->var.val.str.len;
        if (copied > capacity - 1 - position) copied = capacity - 1 - position;
        while (copied && ((unsigned char)record->var.val.str.ptr[copied] & 0xc0) == 0x80) --copied;
        memcpy(buffer + position, record->var.val.str.ptr, copied);
        position += copied;
    }
    buffer[position] = 0;
    return capacity > length ? XXWIDGETS_OK : XXWIDGETS_BUFFER_TOO_SMALL;
}

xxwidgets_status xxwidgets_scan_panel_get_include_tags(const xxwidgets_scan_panel *panel, char *buffer, size_t capacity, size_t *required)
{
    return panel ? get_selected_tags(panel->controls[XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS], buffer, capacity, required) : XXWIDGETS_INVALID_ARGUMENT;
}

xxwidgets_status xxwidgets_scan_panel_get_exclude_tags(const xxwidgets_scan_panel *panel, char *buffer, size_t capacity, size_t *required)
{
    return panel ? get_selected_tags(panel->controls[XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS], buffer, capacity, required) : XXWIDGETS_INVALID_ARGUMENT;
}

xxwidgets_status xxwidgets_scan_panel_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_scan_panel **out_panel)
{
    static const xxwidgets_kind kinds[XXWIDGETS_SCAN_PANEL_CONTROL_COUNT] = {
        XXWIDGETS_CHECKCOMBOBOX, XXWIDGETS_CHECKCOMBOBOX, XXWIDGETS_BUTTON,        XXWIDGETS_BUTTON, XXWIDGETS_TREEVIEW,
        XXWIDGETS_LABEL,         XXWIDGETS_LABEL,         XXWIDGETS_COMBOBOX,      XXWIDGETS_LABEL,  XXWIDGETS_CHECKCOMBOBOX,
        XXWIDGETS_CHECKCOMBOBOX, XXWIDGETS_LABEL,         XXWIDGETS_LABEL,
        XXWIDGETS_BUTTON, XXWIDGETS_BUTTON, XXWIDGETS_BUTTON, XXWIDGETS_LABEL,
        XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_LABEL, XXWIDGETS_LABEL,
        XXWIDGETS_COMBOBOX, XXWIDGETS_LABEL};
    static const char *const names[XXWIDGETS_SCAN_PANEL_CONTROL_COUNT] = {
        XXW_N_("No flags"), XXW_N_("Main database only"), XXW_N_("Scan"), ">", XXW_N_("Scan results"), XXW_N_("Flags"), XXW_N_("Databases"), XXW_N_("Automatic"), XXW_N_("Scan"),
        XXW_N_("All tags"), XXW_N_("No excluded tags"), XXW_N_("Include tags"), XXW_N_("Exclude tags"),
        XXW_N_("Signatures"), XXW_N_("Directory"), XXW_N_("Log"), "",
        XXW_N_("Endianness"), "", XXW_N_("Mode"), "", XXW_N_("Architecture"), "", XXW_N_("Type"), "",
        XXW_N_("Automatic"), XXW_N_("Scan")};
    /* Also the focus order: what DIE puts on top, the results, the buttons.
     * Values >= CONTROL_COUNT are the engine-row extras (extra_widget). */
    enum { ENGINE_FRAME = XXWIDGETS_SCAN_PANEL_CONTROL_COUNT, INFO_FRAME, INFO_VALUE = INFO_FRAME + 4, ELAPSED_VALUE = INFO_VALUE + 4, CREATED };
    static const int creation_order[CREATED] = {
        XXWIDGETS_SCAN_PANEL_FILE_TYPE_LABEL, XXWIDGETS_SCAN_PANEL_FILE_TYPE,
        XXWIDGETS_SCAN_PANEL_ENGINE_LABEL, ENGINE_FRAME, XXWIDGETS_SCAN_PANEL_ENGINE,
        XXWIDGETS_SCAN_PANEL_ENDIANNESS_LABEL, XXWIDGETS_SCAN_PANEL_ENDIANNESS, INFO_FRAME + 0, INFO_VALUE + 0,
        XXWIDGETS_SCAN_PANEL_MODE_LABEL, XXWIDGETS_SCAN_PANEL_MODE, INFO_FRAME + 1, INFO_VALUE + 1,
        XXWIDGETS_SCAN_PANEL_ARCHITECTURE_LABEL, XXWIDGETS_SCAN_PANEL_ARCHITECTURE, INFO_FRAME + 2, INFO_VALUE + 2,
        XXWIDGETS_SCAN_PANEL_TYPE_LABEL, XXWIDGETS_SCAN_PANEL_TYPE, INFO_FRAME + 3, INFO_VALUE + 3,
        XXWIDGETS_SCAN_PANEL_RESULTS, XXWIDGETS_SCAN_PANEL_SIGNATURES,
        XXWIDGETS_SCAN_PANEL_FLAGS_LABEL, XXWIDGETS_SCAN_PANEL_FLAGS, XXWIDGETS_SCAN_PANEL_DATABASES_LABEL, XXWIDGETS_SCAN_PANEL_DATABASES,
        XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS_LABEL, XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS, XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS_LABEL, XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS,
        XXWIDGETS_SCAN_PANEL_SCAN, XXWIDGETS_SCAN_PANEL_DIRECTORY, XXWIDGETS_SCAN_PANEL_LOG, XXWIDGETS_SCAN_PANEL_REPORT,
        XXWIDGETS_SCAN_PANEL_ELAPSED, ELAPSED_VALUE};
    static const char *const engine_names[2] = {XXW_N_("Automatic"), XXW_N_("Detect It Easy (DiE)")};
    static const uint64_t engine_values[2] = {0, 1};
    static const char *const flag_names[6] = {XXW_N_("Deep scan"), XXW_N_("Heuristic scan"), XXW_N_("Verbose results"), XXW_N_("Aggressive scan"), XXW_N_("Hide unknown results"), XXW_N_("Format text report")};
    static const char *const database_names[1] = {XXW_N_("Custom database")};
    static const uint64_t database_values[1] = {XXWIDGETS_SCAN_DATABASE_CUSTOM};
    xxwidgets_scan_panel *panel;
    xxwidgets_rect rects[XXWIDGETS_SCAN_PANEL_CONTROL_COUNT];
    xxwidgets_status status;
    size_t i;
    if (!out_panel) return XXWIDGETS_INVALID_ARGUMENT;
    *out_panel = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !panel_rect(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing) return XXWIDGETS_BUSY;
    panel = (xxwidgets_scan_panel *)calloc(1, sizeof(*panel));
    if (!panel) return XXWIDGETS_OUT_OF_MEMORY;
    panel->owner = owner;
    panel->visible = 1;
    control_rects(bounds, rects);
    for (i = 0; i < CREATED; ++i) {
        int control = creation_order[i];
        xxwidgets_kind kind;
        xxwidgets_widget **slot;
        if (control < XXWIDGETS_SCAN_PANEL_CONTROL_COUNT) {
            status = xxwidgets_widget_create(owner->app, owner, kinds[control], XXW_TR(owner->app, names[control]), rects[control], &panel->controls[control]);
            if (status != XXWIDGETS_OK) goto failed;
            continue;
        }
        if (control == ENGINE_FRAME) { kind = XXWIDGETS_GROUP; slot = &panel->engine_frame; }
        else if (control < INFO_VALUE) { kind = XXWIDGETS_GROUP; slot = &panel->info_frames[control - INFO_FRAME]; }
        else if (control < ELAPSED_VALUE) { kind = XXWIDGETS_VALUE; slot = &panel->info_values[control - INFO_VALUE]; }
        else { kind = XXWIDGETS_VALUE; slot = &panel->elapsed_value; }
        if (!xxwidgets_app_supports_kind(owner->app, kind)) continue;
        /* Hidden at once, in the hidden labels' cell: options 0 never show it. */
        status = xxwidgets_widget_create(owner->app, owner, kind, "", rects[XXWIDGETS_SCAN_PANEL_FLAGS_LABEL], slot);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_visible(*slot, 0);
        if (status != XXWIDGETS_OK) goto failed;
    }
    {
        static const char *const automatic[] = {XXW_N_("Automatic")};
        status = xxwidgets_translated_choices(panel->controls[XXWIDGETS_SCAN_PANEL_FILE_TYPE], automatic, NULL, 1);
        if (status != XXWIDGETS_OK) goto failed;
    }
    status = xxwidgets_translated_choices(panel->controls[XXWIDGETS_SCAN_PANEL_ENGINE], engine_names, engine_values, 2);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(panel->controls[XXWIDGETS_SCAN_PANEL_ENGINE], 0);
    if (status != XXWIDGETS_OK) goto failed;
    for (i = 0; i < 2; ++i) {
        static const xxwidgets_scan_panel_control_id engine_controls[2] = {XXWIDGETS_SCAN_PANEL_ENGINE, XXWIDGETS_SCAN_PANEL_ENGINE_LABEL};
        status = xxwidgets_widget_set_visible(panel->controls[engine_controls[i]], 0);
        if (status != XXWIDGETS_OK) goto failed;
    }
    status = fill_choices(panel->controls[XXWIDGETS_SCAN_PANEL_FLAGS], flag_names, 6);
    if (status != XXWIDGETS_OK) goto failed;
    /* Custom keeps its record value: the CUSTOM bit. */
    status = xxwidgets_translated_choices(panel->controls[XXWIDGETS_SCAN_PANEL_DATABASES], database_names, database_values, 1);
    if (status != XXWIDGETS_OK) goto failed;
    status = xxwidgets_scan_panel_set_flags(panel, XXWIDGETS_SCAN_DEEP | XXWIDGETS_SCAN_HEURISTIC | XXWIDGETS_SCAN_VERBOSE);
    if (status != XXWIDGETS_OK) goto failed;
    status = xxwidgets_scan_panel_set_databases(panel, XXWIDGETS_SCAN_DATABASE_CUSTOM);
    if (status != XXWIDGETS_OK) goto failed;
    status = xxwidgets_scan_panel_set_available_tags(panel, NULL);
    if (status != XXWIDGETS_OK) goto failed;
    status = xxwidgets_widget_set_enabled(panel->controls[XXWIDGETS_SCAN_PANEL_REPORT], 0);
    if (status != XXWIDGETS_OK) goto failed;
    for (i = 0; i < sizeof(hidden_labels) / sizeof(hidden_labels[0]); ++i) {
        status = xxwidgets_widget_set_visible(panel->controls[hidden_labels[i]], 0);
        if (status != XXWIDGETS_OK) goto failed;
    }
    *out_panel = panel;
    return XXWIDGETS_OK;
failed:
    xxwidgets_scan_panel_destroy(panel);
    return status;
}

xxwidgets_status xxwidgets_scan_panel_destroy(xxwidgets_scan_panel *panel)
{
    size_t i;
    xxwidgets_status status;
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    if (panel->owner->app->dispatch_depth || panel->owner->app->polling || panel->owner->app->syncing) return XXWIDGETS_BUSY;
    for (i = 0; i < XXWIDGETS_SCAN_PANEL_CONTROL_COUNT; ++i) {
        if (!panel->controls[i]) continue;
        status = xxwidgets_widget_destroy(panel->controls[i]);
        if (status != XXWIDGETS_OK) return status;
        panel->controls[i] = NULL;
    }
    {
        xxwidgets_widget **extras[10];
        extras[0] = &panel->engine_frame;
        extras[9] = &panel->elapsed_value;
        for (i = 0; i < 4; ++i) {
            extras[1 + i] = &panel->info_frames[i];
            extras[5 + i] = &panel->info_values[i];
        }
        for (i = 0; i < 10; ++i) {
            if (!*extras[i]) continue;
            status = xxwidgets_widget_destroy(*extras[i]);
            if (status != XXWIDGETS_OK) return status;
            *extras[i] = NULL;
        }
    }
    free(panel->report);
    free(panel);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_scan_panel_set_rect(xxwidgets_scan_panel *panel, xxwidgets_rect bounds)
{
    xxwidgets_rect rects[XXWIDGETS_SCAN_PANEL_CONTROL_COUNT], previous[XXWIDGETS_SCAN_PANEL_CONTROL_COUNT];
    xxwidgets_status status;
    size_t i;
    if (!panel || !panel_rect(bounds)) return XXWIDGETS_INVALID_ARGUMENT;
    control_rects(bounds, rects);
    for (i = 0; i < XXWIDGETS_SCAN_PANEL_CONTROL_COUNT; ++i) previous[i] = panel->controls[i]->rect;
    for (i = 0; i < XXWIDGETS_SCAN_PANEL_CONTROL_COUNT; ++i) {
        status = xxwidgets_widget_set_rect(panel->controls[i], rects[i]);
        if (status != XXWIDGETS_OK) {
            size_t j;
            for (j = 0; j < i; ++j) panel->controls[j]->rect = previous[j];
            for (j = 0; j < i; ++j) recover_control(panel->controls[j]);
            return status;
        }
    }
    return XXWIDGETS_OK;
}

/* Whether control i is shown for options while the panel is visible. */
static int control_shown(unsigned int options, size_t i, int has_value)
{
    size_t j;
    for (j = 0; j < sizeof(hidden_labels) / sizeof(hidden_labels[0]); ++j)
        if (hidden_labels[j] == (xxwidgets_scan_panel_control_id)i) return 0;
    switch (i) {
        case XXWIDGETS_SCAN_PANEL_FILE_TYPE:
        case XXWIDGETS_SCAN_PANEL_FILE_TYPE_LABEL: return !(options & XXWIDGETS_SCAN_PANEL_HIDE_FILE_TYPE);
        case XXWIDGETS_SCAN_PANEL_ENGINE:
        case XXWIDGETS_SCAN_PANEL_ENGINE_LABEL: return (options & XXWIDGETS_SCAN_PANEL_ENGINE_ROW) != 0;
        case XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS:
        case XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS: return !(options & XXWIDGETS_SCAN_PANEL_HIDE_TAG_FILTERS);
        case XXWIDGETS_SCAN_PANEL_ENDIANNESS:
        case XXWIDGETS_SCAN_PANEL_MODE:
        case XXWIDGETS_SCAN_PANEL_ARCHITECTURE:
        case XXWIDGETS_SCAN_PANEL_TYPE:
        case XXWIDGETS_SCAN_PANEL_ELAPSED: return !((options & XXWIDGETS_SCAN_PANEL_ENGINE_ROW) && has_value);
        default: return 1;
    }
}

/* Shows what visible and options call for; every control is set, so a
 * failure leaves the others right and the next call repairs it. */
static xxwidgets_status apply_visibility(xxwidgets_scan_panel *panel, int visible, unsigned int options)
{
    xxwidgets_status result = XXWIDGETS_OK, status;
    int engine_row = (options & XXWIDGETS_SCAN_PANEL_ENGINE_ROW) != 0;
    size_t i;
    for (i = 0; i < XXWIDGETS_SCAN_PANEL_CONTROL_COUNT; ++i) {
        xxwidgets_widget *value = NULL;
        size_t k;
        if (i == XXWIDGETS_SCAN_PANEL_ELAPSED) value = panel->elapsed_value;
        for (k = 0; k < 4; ++k)
            if (info_controls[k][1] == (xxwidgets_scan_panel_control_id)i) value = panel->info_values[k];
        status = xxwidgets_widget_set_visible(panel->controls[i], visible && control_shown(options, i, value != NULL));
        if (status != XXWIDGETS_OK && result == XXWIDGETS_OK) result = status;
        if (value) {
            status = xxwidgets_widget_set_visible(value, visible && engine_row);
            if (status != XXWIDGETS_OK && result == XXWIDGETS_OK) result = status;
        }
    }
    {
        xxwidgets_widget *frames[5] = {panel->engine_frame, panel->info_frames[0], panel->info_frames[1], panel->info_frames[2], panel->info_frames[3]};
        for (i = 0; i < 5; ++i) {
            if (!frames[i]) continue;
            status = xxwidgets_widget_set_visible(frames[i], visible && engine_row);
            if (status != XXWIDGETS_OK && result == XXWIDGETS_OK) result = status;
        }
    }
    return result;
}

xxwidgets_status xxwidgets_scan_panel_set_visible(xxwidgets_scan_panel *panel, int visible)
{
    xxwidgets_status status;
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    status = apply_visibility(panel, visible, panel->options);
    if (status == XXWIDGETS_OK) panel->visible = visible != 0;
    return status;
}

static char *join_text(const char *const *parts, size_t count)
{
    size_t i, size = 0, used = 0;
    char *text;
    for (i = 0; i < count; ++i) {
        size_t length = strlen(parts[i]);
        if (length >= SIZE_MAX - size) return NULL;
        size += length;
    }
    text = (char *)malloc(size + 1);
    if (!text) return NULL;
    for (i = 0; i < count; ++i) {
        size_t length = strlen(parts[i]);
        memcpy(text + used, parts[i], length);
        used += length;
    }
    text[used] = 0;
    return text;
}

static void free_nodes(xxwidgets_tree_node *nodes, size_t count)
{
    size_t i;
    if (nodes)
        for (i = 0; i < count; ++i) free((void *)nodes[i].text);
    free(nodes);
}

/* Presentation markers are added by the panel caller. All category rules and
 * palettes live in xxfclib/src/scan, ported from XScanEngine. */
xxwidgets_tree_color xxwidgets_scan_result_color(const char *type)
{
    xx_scan_color_record light, dark;
    if (type && !strncmp(type, "(Heur) ", 7)) type += 7;
    else if (type && !strncmp(type, "(A-Heur) ", 9)) type += 9;
    light = xx_scan_default_color(type, 0);
    dark = xx_scan_default_color(type, 1);
    return (xxwidgets_tree_color){light.foreground, light.background, dark.foreground, dark.background};
}

xxwidgets_status xxwidgets_scan_panel_set_tree(xxwidgets_scan_panel *panel, const xxwidgets_tree_node *nodes,
    const xxwidgets_tree_color *colors, size_t node_count, size_t detection_count, const char *report)
{
    char *copy;
    int previous_enabled;
    xxwidgets_status status;
    if (!report) report = "";
    if (!panel || !xxwidgets_valid_utf8(report)) return XXWIDGETS_INVALID_ARGUMENT;
    copy = xxwidgets_strdup(report);
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    previous_enabled = panel->controls[XXWIDGETS_SCAN_PANEL_REPORT]->enabled;
    status = xxwidgets_widget_set_enabled(panel->controls[XXWIDGETS_SCAN_PANEL_REPORT], report[0] != 0);
    if (status == XXWIDGETS_OK) {
        status = xxwidgets_treeview_set_colored_nodes(panel->controls[XXWIDGETS_SCAN_PANEL_RESULTS], nodes, colors, node_count);
        if (status != XXWIDGETS_OK) recover_enabled(panel->controls[XXWIDGETS_SCAN_PANEL_REPORT], previous_enabled);
    }
    if (status != XXWIDGETS_OK) { free(copy); return status; }
    free(panel->report);
    panel->report = copy;
    panel->count = detection_count;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_scan_panel_set_results(xxwidgets_scan_panel *panel, const char *file_label, const xxwidgets_scan_result *results, size_t count,
                                                  const char *report)
{
    xxwidgets_tree_node *nodes;
    xxwidgets_tree_color *colors;
    size_t i, j, node_count;
    xxwidgets_status status;
    if (!file_label) file_label = "";
    if (!panel || (!results && count) || count > INT_MAX - 1u || !xxwidgets_valid_utf8(file_label)) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < count; ++i) {
        const char *fields[4] = {results[i].type, results[i].name, results[i].version, results[i].info};
        for (j = 0; j < 4; ++j)
            if (fields[j] && !xxwidgets_valid_utf8(fields[j])) return XXWIDGETS_INVALID_ARGUMENT;
    }
    node_count = count + 1;
    if (node_count > SIZE_MAX / sizeof(*nodes)) return XXWIDGETS_INVALID_ARGUMENT;
    nodes = (xxwidgets_tree_node *)calloc(node_count, sizeof(*nodes));
    colors = (xxwidgets_tree_color *)malloc(node_count * sizeof(*colors));
    if (!nodes || !colors) { free(nodes); free(colors); return XXWIDGETS_OUT_OF_MEMORY; }
    nodes[0].text = xxwidgets_strdup(file_label[0] ? file_label : "Unknown");
    nodes[0].parent = SIZE_MAX;
    nodes[0].expanded = 1;
    colors[0] = xxwidgets_scan_result_color(NULL);
    for (i = 0; i < count; ++i) {
        const char *fields[4] = {results[i].type ? results[i].type : "", results[i].name ? results[i].name : "",
            results[i].version ? results[i].version : "", results[i].info ? results[i].info : ""};
        const char *parts[] = {fields[0][0] ? fields[0] : "Detection", ": ", fields[1][0] ? fields[1] : "Unknown",
            fields[2][0] ? " (" : "", fields[2], fields[2][0] ? ")" : "",
            fields[3][0] ? " [" : "", fields[3], fields[3][0] ? "]" : ""};
        nodes[i + 1].text = join_text(parts, 9);
        nodes[i + 1].parent = 0;
        colors[i + 1] = xxwidgets_scan_result_color(fields[0]);
    }
    for (i = 0; i < node_count; ++i)
        if (!nodes[i].text) { free_nodes(nodes, node_count); free(colors); return XXWIDGETS_OUT_OF_MEMORY; }
    status = xxwidgets_scan_panel_set_tree(panel, nodes, colors, node_count, count, report);
    free_nodes(nodes, node_count); free(colors);
    return status;
}

xxwidgets_status xxwidgets_scan_panel_set_file_info(xxwidgets_scan_panel *panel, const char *endianness,
    const char *mode, const char *architecture, const char *type)
{
    const char *texts[4] = {endianness, mode, architecture, type};
    static const xxwidgets_scan_panel_control_id fields[4] = {
        XXWIDGETS_SCAN_PANEL_ENDIANNESS, XXWIDGETS_SCAN_PANEL_MODE, XXWIDGETS_SCAN_PANEL_ARCHITECTURE, XXWIDGETS_SCAN_PANEL_TYPE};
    size_t i;
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < 4; ++i) if (texts[i] && !xxwidgets_valid_utf8(texts[i])) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < 4; ++i) {
        xxwidgets_status status = xxwidgets_widget_set_text(panel->controls[fields[i]], texts[i] ? texts[i] : "");
        if (status == XXWIDGETS_OK && panel->info_values[i]) status = xxwidgets_widget_set_text(panel->info_values[i], texts[i] ? texts[i] : "");
        if (status != XXWIDGETS_OK) return status;
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_scan_panel_set_elapsed(xxwidgets_scan_panel *panel, long long msec)
{
    char text[64];
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    if (msec < 0) text[0] = 0;
    else snprintf(text, sizeof(text), "%lld %s", msec, XXW_TR(panel->owner->app, "msec"));
    if (panel->elapsed_value) {
        xxwidgets_status status = xxwidgets_widget_set_text(panel->elapsed_value, text);
        if (status != XXWIDGETS_OK) return status;
    }
    return xxwidgets_widget_set_text(panel->controls[XXWIDGETS_SCAN_PANEL_ELAPSED], text);
}

xxwidgets_status xxwidgets_scan_panel_clear(xxwidgets_scan_panel *panel)
{
    xxwidgets_status status;
    int previous_enabled;
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    previous_enabled = panel->controls[XXWIDGETS_SCAN_PANEL_REPORT]->enabled;
    status = xxwidgets_widget_set_enabled(panel->controls[XXWIDGETS_SCAN_PANEL_REPORT], 0);
    if (status != XXWIDGETS_OK) return status;
    status = xxwidgets_treeview_clear(panel->controls[XXWIDGETS_SCAN_PANEL_RESULTS]);
    if (status != XXWIDGETS_OK) {
        recover_enabled(panel->controls[XXWIDGETS_SCAN_PANEL_REPORT], previous_enabled);
        return status;
    }
    free(panel->report);
    panel->report = NULL;
    panel->count = 0;
    (void)xxwidgets_scan_panel_set_file_info(panel, NULL, NULL, NULL, NULL);
    (void)xxwidgets_scan_panel_set_elapsed(panel, -1);
    return XXWIDGETS_OK;
}

size_t xxwidgets_scan_panel_count(const xxwidgets_scan_panel *panel)
{
    return panel ? panel->count : 0;
}

xxwidgets_status xxwidgets_scan_panel_get_report(const xxwidgets_scan_panel *panel, char *buffer, size_t capacity, size_t *required)
{
    const char *report;
    size_t length, copied;
    if (!panel || (!buffer && capacity)) return XXWIDGETS_INVALID_ARGUMENT;
    report = panel->report ? panel->report : "";
    length = strlen(report);
    if (required) *required = length + 1;
    if (!buffer && !capacity) return XXWIDGETS_OK;
    if (capacity) {
        copied = length < capacity - 1 ? length : capacity - 1;
        if (copied < length)
            while (copied && ((unsigned char)report[copied] & 0xc0) == 0x80) --copied;
        memcpy(buffer, report, copied);
        buffer[copied] = 0;
    }
    return capacity > length ? XXWIDGETS_OK : XXWIDGETS_BUFFER_TOO_SMALL;
}

xxwidgets_status xxwidgets_scan_panel_show_report(xxwidgets_scan_panel *panel)
{
    return panel ? xxwidgets_text_dialog(panel->owner, XXW_TR((panel->owner)->app, "Scan report"), panel->report ? panel->report : "") : XXWIDGETS_INVALID_ARGUMENT;
}

/* Engine row, fixed captions and the VALUE look (DIE_LOOK.md 3.3 (8)). */
static xxwidgets_status apply_look(xxwidgets_scan_panel *panel, unsigned int options)
{
    xxwidgets_app *app = panel->owner->app;
    int engine_row = (options & XXWIDGETS_SCAN_PANEL_ENGINE_ROW) != 0, fixed = (options & XXWIDGETS_SCAN_PANEL_FIXED_CAPTIONS) != 0;
    xxwidgets_status status;
    size_t i;
    status = xxwidgets_checkcombobox_set_fixed_caption(panel->controls[XXWIDGETS_SCAN_PANEL_FLAGS], fixed ? XXW_TR(app, "Flags") : NULL);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_checkcombobox_set_fixed_caption(panel->controls[XXWIDGETS_SCAN_PANEL_DATABASES], fixed ? XXW_TR(app, XXW_N_("Database")) : NULL);
    if (status != XXWIDGETS_OK) return status;
    /* Without VALUE the LABELs stand in, centred like the values they replace. */
    for (i = 0; i < 4; ++i) {
        if (panel->info_values[i]) continue;
        status = xxwidgets_widget_set_alignment(panel->controls[info_controls[i][1]], engine_row ? XXWIDGETS_ALIGN_CENTER : XXWIDGETS_ALIGN_LEFT);
        if (status != XXWIDGETS_OK) return status;
    }
    if (engine_row) {
        /* Tooltips are best effort: a backend may refuse one (WinAPI before
         * its tooltip control exists) and the row works without them. */
        (void)xxwidgets_widget_set_tooltip(panel->controls[XXWIDGETS_SCAN_PANEL_ENGINE], XXW_TR(app, XXW_N_("Scan engine")));
        for (i = 0; i < 4; ++i)
            if (panel->info_values[i])
                (void)xxwidgets_widget_set_tooltip(panel->info_values[i], panel->controls[info_controls[i][0]]->text);
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_scan_panel_set_options(xxwidgets_scan_panel *panel, unsigned int options)
{
    unsigned int previous;
    xxwidgets_status status;
    if (!panel || (options & ~(unsigned int)SCAN_PANEL_OPTIONS)) return XXWIDGETS_INVALID_ARGUMENT;
    /* Allowed from an event callback (a view toggle or a RESIZE relayout);
     * not from inside a backend call. */
    if (panel->owner->app->syncing) return XXWIDGETS_BUSY;
    previous = panel->options;
    status = apply_look(panel, options);
    if (status == XXWIDGETS_OK) status = apply_visibility(panel, panel->visible, options);
    if (status != XXWIDGETS_OK) {
        /* Best effort back to the previous look; the options stay as they were. */
        (void)apply_look(panel, previous);
        (void)apply_visibility(panel, panel->visible, previous);
        return status;
    }
    panel->options = options;
    return XXWIDGETS_OK;
}

unsigned int xxwidgets_scan_panel_get_options(const xxwidgets_scan_panel *panel)
{
    return panel ? panel->options : 0u;
}

/* Frame mode. Widths follow Qt DIE 3.11 at 96 DPI with Segoe UI 9, where a
 * text cell is 7 units: they are written in tenths of a cell so they scale
 * with the font, and never drop below what the text needs (measured). */
enum {
    FRAME_FILE_TYPE = 0, FRAME_ENGINE, FRAME_INFO, /* 4 info boxes */
    FRAME_BOXES = FRAME_INFO + 4
};
enum {
    BAR_SIGNATURES = 0, BAR_FLAGS, BAR_DATABASES, BAR_INCLUDE, BAR_EXCLUDE, BAR_SPACER, BAR_SCAN, BAR_ITEMS
};

typedef struct scan_frame_plan {
    xxwidgets_rect controls[XXWIDGETS_SCAN_PANEL_CONTROL_COUNT];
    int placed[XXWIDGETS_SCAN_PANEL_CONTROL_COUNT];
    xxwidgets_rect engine_frame, info_frames[4], info_values[4], elapsed_value;
    int minimum_width, minimum_height;
} scan_frame_plan;

static int font_units(const xxwidgets_metrics *metrics, int tenths)
{
    long long units = ((long long)metrics->cell_width * tenths + 5) / 10;
    /* Measured units: back to Qt's pixels (a 7-pixel cell), scaled by the
     * control height like the other main-window components (exact Qt
     * pixels on WinAPI at 96 DPI, where H is 23). Cells keep the cell rule. */
    if (!metrics->unit_is_cell && metrics->label_height > 0)
        /* By the text height (L is 15 where H is 23), as xxwidgets_component_qt. */
        units = (((long long)tenths * 7 + 5) / 10 * metrics->label_height + 7) / 15;
    else if (!metrics->unit_is_cell && metrics->control_height > 0)
        units = (((long long)tenths * 7 + 5) / 10 * metrics->control_height + 11) / 23;
    return units < 1 ? 1 : units > 32767 ? 32767 : (int)units;
}

static int preferred_width(xxwidgets_widget *widget, const char *text)
{
    int width = 1, height = 1;
    if (!widget) return 1;
    if (text) (void)xxwidgets_widget_measure_text(widget, text, &width, &height);
    else (void)xxwidgets_widget_get_preferred_size(widget, &width, &height);
    return width;
}

static int larger(int a, int b) { return a > b ? a : b; }

static void plan_box(xxwidgets_layout_item *item, int minimum, int preferred, int maximum, int stretch, int hidden)
{
    memset(item, 0, sizeof(*item));
    item->minimum = minimum;
    item->preferred = larger(minimum, preferred);
    item->maximum = maximum ? larger(maximum, item->preferred) : 0;
    item->stretch = stretch;
    item->hidden = hidden;
}

static xxwidgets_status scan_frame_layout(xxwidgets_scan_panel *panel, xxwidgets_rect units, scan_frame_plan *plan)
{
    static const char *const info_samples[4] = {"LE", "64-bit", "AMD64", "GUI"};
    /* Control widths: preferred, and the most a growing box takes (0: fixed). */
    static const int info_tenths[4][2] = {{100, 0}, {114, 0}, {130, 157}, {130, 157}};
    static const xxwidgets_scan_panel_control_id bar_controls[BAR_ITEMS] = {
        XXWIDGETS_SCAN_PANEL_SIGNATURES, XXWIDGETS_SCAN_PANEL_FLAGS, XXWIDGETS_SCAN_PANEL_DATABASES,
        XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS, XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS, XXWIDGETS_SCAN_PANEL_CONTROL_COUNT, XXWIDGETS_SCAN_PANEL_SCAN};
    xxwidgets_metrics m;
    xxwidgets_layout_item boxes[FRAME_BOXES], bar[BAR_ITEMS];
    xxwidgets_widget **c = panel->controls;
    xxwidgets_app *app = panel->owner->app;
    unsigned int options = panel->options;
    int engine_row = (options & XXWIDGETS_SCAN_PANEL_ENGINE_ROW) != 0;
    int hide_tags = (options & XXWIDGETS_SCAN_PANEL_HIDE_TAG_FILTERS) != 0;
    int inset2, box_height, bar_height, pad, tree_minimum, width, height, top_minimum, bar_minimum, row2_minimum;
    int directory, log, report, elapsed, tree_y, bar_y, row2_y, button_y, i;
    xxwidgets_status status = xxwidgets_app_get_metrics(app, &m);
    if (status != XXWIDGETS_OK) return status;
    memset(plan, 0, sizeof(*plan));
    inset2 = 2 * m.frame_inset;
    /* Row E: captioned boxes; each box is its control plus the frame insets
     * and at least as wide as its caption. */
    plan_box(&boxes[FRAME_FILE_TYPE],
             larger(preferred_width(c[XXWIDGETS_SCAN_PANEL_FILE_TYPE_LABEL], NULL), preferred_width(c[XXWIDGETS_SCAN_PANEL_FILE_TYPE], NULL)) + inset2,
             font_units(&m, 157) + inset2, 0, 0, (options & XXWIDGETS_SCAN_PANEL_HIDE_FILE_TYPE) != 0);
    plan_box(&boxes[FRAME_ENGINE],
             larger(preferred_width(c[XXWIDGETS_SCAN_PANEL_ENGINE_LABEL], NULL), preferred_width(c[XXWIDGETS_SCAN_PANEL_ENGINE], NULL)) + inset2,
             font_units(&m, 286) + inset2, 0, 1, !engine_row);
    for (i = 0; i < 4; ++i) {
        xxwidgets_widget *value = engine_row && panel->info_values[i] ? panel->info_values[i] : c[info_controls[i][1]];
        int minimum = larger(preferred_width(c[info_controls[i][0]], NULL), preferred_width(value, info_samples[i])) + inset2;
        plan_box(&boxes[FRAME_INFO + i], minimum, font_units(&m, info_tenths[i][0]) + inset2,
                 info_tenths[i][1] ? font_units(&m, info_tenths[i][1]) + inset2 : 0, info_tenths[i][1] ? 1 : 0, 0);
    }
    box_height = xxwidgets_layout_caption_box_height(&m, m.control_height);
    /* Row G: Signatures, Flags, Database, the tag filters, then the tall Scan
     * button at the right edge; below them Directory, Log, and right-aligned
     * the report button and the elapsed time, pad units taller than a button
     * (Qt: 60 = 23 + 6 + 31). */
    pad = m.frame_inset + m.panel_inset;
    bar_height = 2 * m.control_height + m.spacing + pad;
    plan_box(&bar[BAR_SIGNATURES], preferred_width(c[XXWIDGETS_SCAN_PANEL_SIGNATURES], NULL), font_units(&m, 114), 0, 0, 0);
    plan_box(&bar[BAR_FLAGS], preferred_width(c[XXWIDGETS_SCAN_PANEL_FLAGS], XXW_TR(app, "Flags")), font_units(&m, 214), 0, 0, 0);
    plan_box(&bar[BAR_DATABASES], preferred_width(c[XXWIDGETS_SCAN_PANEL_DATABASES], XXW_TR(app, XXW_N_("Database"))), font_units(&m, 214), 0, 0, 0);
    /* The tag combos show a summary ("All tags", "No excluded tags"), not
     * their label: at least their current caption fits. */
    plan_box(&bar[BAR_INCLUDE], larger(preferred_width(c[XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS], NULL),
                                       preferred_width(c[XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS], XXW_TR(app, "Include tags"))),
             font_units(&m, 214), 0, 0, hide_tags);
    plan_box(&bar[BAR_EXCLUDE], larger(preferred_width(c[XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS], NULL),
                                       preferred_width(c[XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS], XXW_TR(app, "Exclude tags"))),
             font_units(&m, 214), 0, 0, hide_tags);
    plan_box(&bar[BAR_SPACER], 0, 0, 0, 1, 0);
    bar[BAR_SPACER].spacer = 1;
    plan_box(&bar[BAR_SCAN], preferred_width(c[XXWIDGETS_SCAN_PANEL_SCAN], NULL), font_units(&m, 143), 0, 0, 0);
    directory = larger(preferred_width(c[XXWIDGETS_SCAN_PANEL_DIRECTORY], NULL), font_units(&m, 114));
    log = larger(preferred_width(c[XXWIDGETS_SCAN_PANEL_LOG], NULL), font_units(&m, 114));
    /* Report is square (H) in units; cells keep its "[>]". */
    report = m.unit_is_cell ? preferred_width(c[XXWIDGETS_SCAN_PANEL_REPORT], NULL) : m.control_height;
    elapsed = larger(preferred_width(engine_row && panel->elapsed_value ? panel->elapsed_value : c[XXWIDGETS_SCAN_PANEL_ELAPSED], "00000 msec"),
                     font_units(&m, 129));
    /* Minimum size: the widest row, and the rows stacked with the tree at
     * three control heights. */
    top_minimum = xxwidgets_layout_line_minimum(boxes, FRAME_BOXES, m.spacing);
    bar_minimum = xxwidgets_layout_line_minimum(bar, BAR_ITEMS, m.spacing);
    row2_minimum = directory + log + report + elapsed + bar[BAR_SCAN].minimum + 4 * m.spacing;
    tree_minimum = 3 * m.control_height;
    plan->minimum_width = larger(top_minimum, larger(bar_minimum, row2_minimum));
    plan->minimum_height = box_height + tree_minimum + bar_height + 2 * m.spacing;
    width = larger(units.width, plan->minimum_width);
    height = larger(units.height, plan->minimum_height);
    /* Row E. */
    (void)xxwidgets_layout_line(boxes, FRAME_BOXES, units.x, width, m.spacing);
    for (i = 0; i < FRAME_BOXES; ++i) {
        xxwidgets_layout_caption_box box;
        xxwidgets_rect area;
        if (boxes[i].hidden) continue;
        area = xxwidgets_layout_rect(boxes[i].position, units.y, boxes[i].size > 0 ? boxes[i].size : 1, box_height);
        xxwidgets_layout_caption_box_split(&m, area, m.control_height, &box);
        if (i == FRAME_FILE_TYPE) {
            plan->controls[XXWIDGETS_SCAN_PANEL_FILE_TYPE_LABEL] = box.caption;
            plan->controls[XXWIDGETS_SCAN_PANEL_FILE_TYPE] = box.control;
            plan->placed[XXWIDGETS_SCAN_PANEL_FILE_TYPE_LABEL] = plan->placed[XXWIDGETS_SCAN_PANEL_FILE_TYPE] = 1;
        } else if (i == FRAME_ENGINE) {
            plan->controls[XXWIDGETS_SCAN_PANEL_ENGINE_LABEL] = box.caption;
            plan->controls[XXWIDGETS_SCAN_PANEL_ENGINE] = box.control;
            plan->placed[XXWIDGETS_SCAN_PANEL_ENGINE_LABEL] = plan->placed[XXWIDGETS_SCAN_PANEL_ENGINE] = 1;
            plan->engine_frame = box.frame;
        } else {
            int k = i - FRAME_INFO;
            plan->controls[info_controls[k][0]] = box.caption;
            plan->controls[info_controls[k][1]] = box.control;
            plan->placed[info_controls[k][0]] = plan->placed[info_controls[k][1]] = 1;
            plan->info_frames[k] = box.frame;
            plan->info_values[k] = box.control;
        }
    }
    /* Row F takes every extra unit of height; row G sits at the bottom. */
    tree_y = units.y + box_height + m.spacing;
    bar_y = units.y + height - bar_height;
    plan->controls[XXWIDGETS_SCAN_PANEL_RESULTS] = xxwidgets_layout_rect(units.x, tree_y, width, bar_y - m.spacing - tree_y);
    plan->placed[XXWIDGETS_SCAN_PANEL_RESULTS] = 1;
    (void)xxwidgets_layout_line(bar, BAR_ITEMS, units.x, width, m.spacing);
    for (i = 0; i < BAR_ITEMS; ++i) {
        if (bar[i].hidden || bar[i].spacer) continue;
        plan->controls[bar_controls[i]] = xxwidgets_layout_rect(bar[i].position, bar_y, bar[i].size > 0 ? bar[i].size : 1,
                                                                 i == BAR_SCAN ? bar_height : m.control_height);
        plan->placed[bar_controls[i]] = 1;
    }
    /* The elapsed time ends S left of Scan and reaches the bottom; the
     * buttons of the second row are centred on it. */
    row2_y = bar_y + m.control_height + m.spacing;
    button_y = row2_y + pad / 2;
    plan->elapsed_value = xxwidgets_layout_rect(bar[BAR_SCAN].position - m.spacing - elapsed, row2_y, elapsed, m.control_height + pad);
    plan->controls[XXWIDGETS_SCAN_PANEL_ELAPSED] = plan->elapsed_value;
    plan->controls[XXWIDGETS_SCAN_PANEL_REPORT] = xxwidgets_layout_rect(plan->elapsed_value.x - m.spacing - report, button_y, report, m.control_height);
    plan->controls[XXWIDGETS_SCAN_PANEL_DIRECTORY] = xxwidgets_layout_rect(units.x, button_y, directory, m.control_height);
    plan->controls[XXWIDGETS_SCAN_PANEL_LOG] = xxwidgets_layout_rect(units.x + directory + m.spacing, button_y, log, m.control_height);
    plan->placed[XXWIDGETS_SCAN_PANEL_ELAPSED] = plan->placed[XXWIDGETS_SCAN_PANEL_REPORT] =
        plan->placed[XXWIDGETS_SCAN_PANEL_DIRECTORY] = plan->placed[XXWIDGETS_SCAN_PANEL_LOG] = 1;
    return XXWIDGETS_OK;
}

typedef struct scan_frame_backup {
    xxwidgets_widget *widget;
    xxwidgets_rect frame, rect;
    int has_frame;
} scan_frame_backup;

static xxwidgets_status place_backed_up(scan_frame_backup *backups, size_t *count, xxwidgets_widget *widget, xxwidgets_rect frame)
{
    if (!widget) return XXWIDGETS_OK;
    backups[*count].widget = widget;
    backups[*count].frame = widget->frame;
    backups[*count].rect = widget->rect;
    backups[*count].has_frame = widget->has_frame;
    ++*count;
    return xxwidgets_layout_place(widget, frame);
}

xxwidgets_status xxwidgets_scan_panel_set_bounds(xxwidgets_scan_panel *panel, xxwidgets_rect units)
{
    scan_frame_plan plan;
    scan_frame_backup backups[XXWIDGETS_SCAN_PANEL_CONTROL_COUNT + 10];
    size_t count = 0, i;
    xxwidgets_status status;
    if (!panel || units.x < 0 || units.y < 0 || units.width < 1 || units.height < 1 || units.x > 32767 - units.width ||
        units.y > 32767 - units.height)
        return XXWIDGETS_INVALID_ARGUMENT;
    /* Allowed from an event callback (a view toggle or a RESIZE relayout);
     * not from inside a backend call. */
    if (panel->owner->app->syncing) return XXWIDGETS_BUSY;
    status = scan_frame_layout(panel, units, &plan);
    if (status != XXWIDGETS_OK) return status;
    for (i = 0; i < XXWIDGETS_SCAN_PANEL_CONTROL_COUNT && status == XXWIDGETS_OK; ++i)
        if (plan.placed[i]) status = place_backed_up(backups, &count, panel->controls[i], plan.controls[i]);
    /* The engine-row twins and frames go where the controls they stand for
     * went, whether or not they show now: a later set_options needs no
     * second set_bounds for them. */
    if (status == XXWIDGETS_OK && plan.placed[XXWIDGETS_SCAN_PANEL_ENGINE])
        status = place_backed_up(backups, &count, panel->engine_frame, plan.engine_frame);
    for (i = 0; i < 4 && status == XXWIDGETS_OK; ++i) {
        status = place_backed_up(backups, &count, panel->info_frames[i], plan.info_frames[i]);
        if (status == XXWIDGETS_OK) status = place_backed_up(backups, &count, panel->info_values[i], plan.info_values[i]);
    }
    if (status == XXWIDGETS_OK) status = place_backed_up(backups, &count, panel->elapsed_value, plan.elapsed_value);
    if (status != XXWIDGETS_OK) {
        /* The failing widget restored itself; restore the earlier ones
         * logically and re-apply them best effort, as set_rect does. */
        for (i = 0; i + 1 < count; ++i) {
            xxwidgets_widget *widget = backups[i].widget;
            widget->frame = backups[i].frame;
            widget->rect = backups[i].rect;
            widget->has_frame = backups[i].has_frame;
            ++widget->app->syncing;
            if (widget->has_frame && widget->app->ops->place) (void)widget->app->ops->place(widget);
            else (void)widget->app->ops->sync(widget);
            --widget->app->syncing;
        }
    }
    return status;
}

xxwidgets_status xxwidgets_scan_panel_get_minimum_size(xxwidgets_scan_panel *panel, int *width, int *height)
{
    scan_frame_plan plan;
    xxwidgets_status status;
    if (!panel || !width || !height) return XXWIDGETS_INVALID_ARGUMENT;
    status = scan_frame_layout(panel, xxwidgets_layout_rect(0, 0, 1, 1), &plan);
    if (status != XXWIDGETS_OK) return status;
    *width = plan.minimum_width;
    *height = plan.minimum_height;
    return XXWIDGETS_OK;
}
