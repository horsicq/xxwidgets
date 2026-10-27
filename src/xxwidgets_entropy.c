/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * XEntropyWidget-style analysis, shared by all widget backends.
 */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_entropy.h"
#include "xxwidgets/xxwidgets_combobox.h"
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct entropy_model {
    unsigned char *data;
    uint64_t size;
    size_t region_count, sample_count, requested_count;
    int snapshot;
    uint64_t base, counts[256];
    double total;
    char *type;
    xxwidgets_entropy_region *regions;
    double *region_entropy;
    xxwidgets_entropy_sample *samples;
} entropy_model;
struct xxwidgets_entropy {
    xxwidgets_widget *owner, *controls[XXWIDGETS_ENTROPY_CONTROL_COUNT];
    xxwidgets_rect bounds;
    entropy_model *model;
    size_t *visible_regions, selected_region;
    int bytes, whole, grid;
};
#define CONTROL(v, id) ((v)->controls[XXWIDGETS_ENTROPY_##id])
static const char *packed(double entropy) { return entropy >= 6.5 ? "packed" : "not packed"; }
static xxwidgets_status sync_control(xxwidgets_widget *widget)
{
    xxwidgets_status status;
    ++widget->app->syncing;
    status = widget->app->ops->sync(widget);
    --widget->app->syncing;
    return status;
}
static double counts_entropy(const uint64_t counts[256], uint64_t size)
{
    double entropy = 0;
    if (size)
        for (size_t i = 0; i < 256; ++i)
            if (counts[i]) {
                double p = (double)counts[i] / (double)size;
                entropy -= p * (log(p) / log(2.0));
            }
    return entropy < 0 ? 0 : entropy > 8 ? 8 : entropy;
}
static double data_entropy(const unsigned char *data, size_t size)
{
    uint64_t counts[256] = {0};
    for (size_t i = 0; i < size; ++i)
        ++counts[data[i]];
    return counts_entropy(counts, size);
}
static void free_model(entropy_model *model)
{
    if (!model)
        return;
    for (size_t i = 0; i < model->region_count; ++i)
        free((void *)model->regions[i].name);
    free(model->regions);
    free(model->region_entropy);
    free(model->samples);
    free(model->data);
    free(model->type);
    free(model);
}
static xxwidgets_status make_samples(entropy_model *m, size_t requested)
{
    size_t count = requested < m->size ? requested : m->size;
    xxwidgets_entropy_sample *samples = count ? calloc(count, sizeof(*samples)) : NULL;
    if (count && !samples)
        return XXWIDGETS_OUT_OF_MEMORY;
    for (size_t i = 0; i < count; ++i) {
        size_t offset = i * (m->size / count);
        size_t size = i + 1 == count ? m->size - offset : m->size / count;
        samples[i] = (xxwidgets_entropy_sample){m->base + offset, size, data_entropy(m->data + offset, size)};
    }
    free(m->samples);
    m->samples = samples;
    m->sample_count = count;
    m->requested_count = requested;
    return XXWIDGETS_OK;
}
static xxwidgets_status choice(xxwidgets_widget *widget, const char *const *names, size_t count)
{
    xx_meta_string records[2] = {0};
    xx_str_w_s labels[2] = {0};
    xxwidgets_status status = XXWIDGETS_OK;
    if (count > 2)
        return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i) {
        const unsigned char *p = (const unsigned char *)names[i];
        wchar_t *text = calloc(strlen(names[i]) + 1, sizeof(wchar_t));
        size_t n = 0;
        if (!text) {
            status = XXWIDGETS_OUT_OF_MEMORY;
            break;
        }
        while (*p) {
            uint32_t c = *p++;
            if (c >= 0xc0) {
                int following = c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3;
                c &= (1u << (6 - following)) - 1;
                while (following--)
                    c = (c << 6) | (*p++ & 63);
            }
            if (sizeof(wchar_t) == 2 && c > 0xffff) {
                c -= 0x10000;
                text[n++] = (wchar_t)(0xd800 + (c >> 10));
                text[n++] = (wchar_t)(0xdc00 + (c & 1023));
            } else
                text[n++] = (wchar_t)c;
        }
        labels[i].data = text;
        labels[i].length = n;
        labels[i].capacity = n + 1;
        labels[i].is_view = true;
        records[i].meta_string = labels + i;
        records[i].var.type = XX_VAR_TYPE_UINT64;
        records[i].var.val.u64 = i;
    }
    if (status == XXWIDGETS_OK)
        status = xxwidgets_combobox_set_records(widget, records, count);
    for (size_t i = 0; i < count; ++i)
        free(labels[i].data);
    return status;
}
static void free_rows(char **rows, size_t count)
{
    if (rows)
        for (size_t i = 0; i < count; ++i)
            free(rows[i]);
    free(rows);
}
void xxwidgets_entropy_table_dispose(xxwidgets_widget *widget)
{
    if (xxwidgets_entropy_table(widget))
        free_rows(widget->entropy_cells, widget->item_count * xxwidgets_tabular_column_count(widget));
    widget->entropy_cells = NULL;
    free(widget->entropy_plot_samples);
    widget->entropy_plot_samples = NULL;
    widget->entropy_plot_count = 0;
}
const char *xxwidgets_entropy_cell(const xxwidgets_widget *widget, size_t row, size_t col)
{
    size_t columns = xxwidgets_tabular_column_count(widget);
    return widget->entropy_cells && row < widget->item_count && col < columns ? widget->entropy_cells[row * columns + col] : "";
}
static xxwidgets_status replace_table(xxwidgets_widget *widget, char **cells, char **rows, size_t count)
{
    char **old_cells = widget->entropy_cells, **old_rows = widget->items;
    size_t old_count = widget->item_count;
    int old_value = widget->value;
    widget->entropy_cells = cells;
    widget->items = rows;
    widget->item_count = count;
    widget->value = -1;
    ++widget->entropy_revision;
    xxwidgets_status status = sync_control(widget);
    if (status != XXWIDGETS_OK) {
        widget->entropy_cells = old_cells;
        widget->items = old_rows;
        widget->item_count = old_count;
        widget->value = old_value;
        ++widget->entropy_revision;
        sync_control(widget);
    } else {
        free_rows(old_cells, old_count * xxwidgets_tabular_column_count(widget));
        free_rows(old_rows, old_count);
    }
    return status;
}
static int match_filter(const char *name, const char *filter)
{
    size_t n = strlen(filter);
    if (!n)
        return 1;
    for (; *name; ++name) {
        size_t i;
        for (i = 0; i < n && name[i]; ++i) {
            unsigned char a = (unsigned char)name[i], b = (unsigned char)filter[i];
            if (a >= 'A' && a <= 'Z')
                a += 'a' - 'A';
            if (b >= 'A' && b <= 'Z')
                b += 'a' - 'A';
            if (a != b)
                break;
        }
        if (i == n)
            return 1;
    }
    return 0;
}
static xxwidgets_status refresh_table(xxwidgets_entropy *v, int bytes)
{
    entropy_model *m = v->model;
    xxwidgets_widget *table = bytes ? CONTROL(v, BYTES_TABLE) : CONTROL(v, REGIONS_TABLE);
    size_t columns = bytes ? 3 : 5, capacity = !m ? 0 : bytes ? 256 : v->whole ? 1 : m->region_count, count = 0;
    char **cells = capacity ? calloc(capacity * columns, sizeof(*cells)) : NULL;
    char **rows = capacity ? calloc(capacity, sizeof(*rows)) : NULL;
    size_t *mapping = !bytes && capacity ? calloc(capacity, sizeof(*mapping)) : NULL;
    xxwidgets_status status = XXWIDGETS_OK;
    if (capacity && (!cells || !rows || (!bytes && !mapping))) {
        status = XXWIDGETS_OUT_OF_MEMORY;
        goto done;
    }
    for (size_t i = 0; i < capacity; ++i) {
        char numbers[5][64];
        const char *values[5] = {numbers[0], numbers[1], numbers[2], numbers[3], numbers[4]};
        size_t length = 1;
        if (bytes) {
            snprintf(numbers[0], sizeof(numbers[0]), "0x%02X", (unsigned)i);
            snprintf(numbers[1], sizeof(numbers[1]), "%" PRIu64, m->counts[i]);
            snprintf(numbers[2], sizeof(numbers[2]), "%.4f", m->size ? 100.0 * (double)m->counts[i] / (double)m->size : 0);
        } else {
            uint64_t offset = v->whole ? 0 : m->regions[i].offset, size = v->whole ? m->size : m->regions[i].size;
            double e = v->whole ? m->total : m->region_entropy[i];
            values[4] = v->whole ? "Whole file" : m->regions[i].name;
            snprintf(numbers[0], sizeof(numbers[0]), "%08" PRIX64, m->base + offset);
            snprintf(numbers[1], sizeof(numbers[1]), "%08" PRIX64, size);
            snprintf(numbers[2], sizeof(numbers[2]), "%.5f", e);
            values[3] = packed(e);
            if (!match_filter(values[4], CONTROL(v, FILTER)->text))
                continue;
            mapping[count] = v->whole ? SIZE_MAX : i;
        }
        for (size_t col = 0; col < columns; ++col) {
            char *cell = xxwidgets_strdup(values[col]);
            if (!cell) {
                status = XXWIDGETS_OUT_OF_MEMORY;
                goto done;
            }
            for (char *p = cell; *p; ++p)
                if ((unsigned char)*p < 32 || *p == 127)
                    *p = ' ';
            cells[count * columns + col] = cell;
            size_t width = xxwidgets_tabular_column_width(table, col), n = strlen(cell);
            if (n < width)
                n = width;
            if (n > SIZE_MAX - length - 2) {
                status = XXWIDGETS_OUT_OF_MEMORY;
                goto done;
            }
            length += n + 2;
        }
        rows[count] = calloc(length, 1);
        if (!rows[count]) {
            status = XXWIDGETS_OUT_OF_MEMORY;
            goto done;
        }
        size_t pos = 0;
        for (size_t col = 0; col < columns; ++col) {
            int n = snprintf(rows[count] + pos, length - pos, "%-*s%s", (int)xxwidgets_tabular_column_width(table, col), cells[count * columns + col],
                             col + 1 < columns ? "  " : "");
            if (n < 0) {
                status = XXWIDGETS_PLATFORM_ERROR;
                goto done;
            }
            pos += (size_t)n;
        }
        ++count;
    }
    status = replace_table(table, cells, rows, count);
    if (status == XXWIDGETS_OK) {
        cells = rows = NULL;
        if (!bytes) {
            free(v->visible_regions);
            v->visible_regions = mapping;
            mapping = NULL;
            v->selected_region = SIZE_MAX;
        }
    }
done:
    free_rows(cells, capacity * columns);
    free_rows(rows, capacity);
    free(mapping);
    return status;
}
static xxwidgets_status refresh(xxwidgets_entropy *v)
{
    char text[128];
    entropy_model *m = v->model;
    xxwidgets_status status;
#define TRY(call)                                                                                                                                    \
    do {                                                                                                                                             \
        status = (call);                                                                                                                             \
        if (status != XXWIDGETS_OK)                                                                                                                  \
            return status;                                                                                                                           \
    } while (0)
    const char *type[] = {m ? m->type : "Binary"};
    TRY(choice(CONTROL(v, TYPE), type, 1));
    snprintf(text, sizeof(text), "%08" PRIX64, m ? m->base : 0);
    TRY(xxwidgets_widget_set_text(CONTROL(v, OFFSET), text));
    snprintf(text, sizeof(text), "%08" PRIX64, (uint64_t)(m ? m->size : 0));
    TRY(xxwidgets_widget_set_text(CONTROL(v, SIZE), text));
    snprintf(text, sizeof(text), "%zu", m ? m->requested_count : 1);
    TRY(xxwidgets_widget_set_text(CONTROL(v, COUNT), text));
    snprintf(text, sizeof(text), "Block: %08" PRIX64, (uint64_t)(m && m->sample_count ? m->size / m->sample_count : 0));
    TRY(xxwidgets_widget_set_text(CONTROL(v, PART_SIZE), text));
    snprintf(text, sizeof(text), "%.5f", m ? m->total : 0);
    TRY(xxwidgets_widget_set_text(CONTROL(v, TOTAL), text));
    TRY(xxwidgets_widget_set_value(CONTROL(v, PROGRESS), m ? (int)(m->total * 100 / 8) : 0));
    snprintf(text, sizeof(text), "%s (%d%%)", packed(m ? m->total : 0), m ? (int)(m->total * 100 / 8) : 0);
    TRY(xxwidgets_widget_set_text(CONTROL(v, STATUS), text));
    TRY(xxwidgets_widget_set_enabled(CONTROL(v, COUNT), !m || !m->snapshot));
    TRY(xxwidgets_widget_set_enabled(CONTROL(v, RELOAD), !m || !m->snapshot));
    TRY(refresh_table(v, 0));
    TRY(refresh_table(v, 1));
    TRY(xxwidgets_widget_set_text(CONTROL(v, TRACKER), "Select a region or click the diagram"));
    TRY(sync_control(CONTROL(v, GRAPH)));
    return XXWIDGETS_OK;
#undef TRY
}
xxwidgets_widget *xxwidgets_entropy_control(const xxwidgets_entropy *v, xxwidgets_entropy_control_id id)
{
    return v && id >= 0 && id < XXWIDGETS_ENTROPY_CONTROL_COUNT ? v->controls[id] : NULL;
}
xxwidgets_status xxwidgets_entropy_set_rect(xxwidgets_entropy *v, xxwidgets_rect b)
{
    xxwidgets_rect r[XXWIDGETS_ENTROPY_CONTROL_COUNT];
    if (!v || b.x < 0 || b.y < 0 || b.width < 88 || b.height < 27 || b.x > 32767 - b.width || b.y > 32767 - b.height)
        return XXWIDGETS_INVALID_ARGUMENT;
    int table_height = (b.height - 10) / 3;
#define R(id, dx, dy, w, h) r[XXWIDGETS_ENTROPY_##id] = (xxwidgets_rect){b.x + (dx), b.y + (dy), (w), (h)}
    R(TYPE, 0, 0, 13, 2);
    R(MAP, 14, 0, 16, 2);
    R(OFFSET, 31, 0, 12, 2);
    R(SIZE, 44, 0, 12, 2);
    R(COUNT_LABEL, b.width - 30, 0, 6, 2);
    R(COUNT, b.width - 24, 0, 5, 2);
    R(PART_SIZE, b.width - 18, 0, 18, 2);
    R(TOTAL, 0, 3, 10, 2);
    R(PROGRESS, 11, 3, b.width - 62, 2);
    R(STATUS, b.width - 50, 3, 20, 2);
    R(SAVE, b.width - 29, 3, 9, 2);
    R(SAVE_DIAGRAM, b.width - 19, 3, 19, 2);
    R(RELOAD, b.width - 12, 6, 12, 2);
    R(TAB, 0, 6, 12, 2);
    R(BYTES_TAB, 13, 6, 10, 2);
    R(FILTER_LABEL, 25, 6, 6, 2);
    R(FILTER, 32, 6, b.width - 46, 2);
    R(REGIONS_TABLE, 0, 9, b.width, table_height);
    R(BYTES_TABLE, 0, 9, b.width, table_height);
    R(GRID, 0, 10 + table_height, 10, 1);
    R(TRACKER, 12, 10 + table_height, b.width - 12, 1);
    R(GRAPH, 0, 12 + table_height, b.width, b.height - 12 - table_height);
#undef R
    xxwidgets_rect previous[XXWIDGETS_ENTROPY_CONTROL_COUNT];
    for (size_t i = 0; i < XXWIDGETS_ENTROPY_CONTROL_COUNT; ++i)
        previous[i] = v->controls[i]->rect;
    for (size_t i = 0; i < XXWIDGETS_ENTROPY_CONTROL_COUNT; ++i) {
        xxwidgets_status status = xxwidgets_widget_set_rect(v->controls[i], r[i]);
        if (status != XXWIDGETS_OK) {
            for (size_t j = 0; j < i; ++j)
                xxwidgets_widget_set_rect(v->controls[j], previous[j]);
            return status;
        }
    }
    v->bounds = b;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_entropy_create(xxwidgets_widget *owner, xxwidgets_rect bounds, xxwidgets_entropy **out)
{
    if (!out)
        return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling)
        return XXWIDGETS_BUSY;
    xxwidgets_entropy *v = calloc(1, sizeof(*v));
    if (!v)
        return XXWIDGETS_OUT_OF_MEMORY;
    v->owner = owner;
    v->selected_region = SIZE_MAX;
    static const xxwidgets_kind kinds[] = {
        XXWIDGETS_COMBOBOX,      XXWIDGETS_COMBOBOX, XXWIDGETS_LABEL,  XXWIDGETS_LABEL,       XXWIDGETS_LABEL, XXWIDGETS_EDIT,
        XXWIDGETS_LABEL,         XXWIDGETS_BUTTON,   XXWIDGETS_LABEL,  XXWIDGETS_PROGRESS,    XXWIDGETS_LABEL, XXWIDGETS_BUTTON,
        XXWIDGETS_BUTTON,        XXWIDGETS_BUTTON,   XXWIDGETS_BUTTON, XXWIDGETS_LABEL,       XXWIDGETS_EDIT,  XXWIDGETS_ENTROPY_REGIONS,
        XXWIDGETS_ENTROPY_BYTES, XXWIDGETS_CHECKBOX, XXWIDGETS_LABEL,  XXWIDGETS_ENTROPY_PLOT};
    static const char *names[] = {XXW_N_("Type"), XXW_N_("Map"),          XXW_N_("Offset"),  XXW_N_("Size"),  XXW_N_("Count"),  "1", XXW_N_("Block"),   XXW_N_("Reload"), "0.00000", "", XXW_N_("not packed (0%)"),
                                  XXW_N_("Save"), XXW_N_("Save diagram"), XXW_N_("Entropy"), XXW_N_("Bytes"), XXW_N_("Filter"), "",  XXW_N_("Regions"), XXW_N_("Bytes"),  XXW_N_("Grid"),    "", XXW_N_("Entropy diagram")};
    xxwidgets_status status = XXWIDGETS_OK;
    for (size_t i = 0; i < XXWIDGETS_ENTROPY_CONTROL_COUNT; ++i) {
        status = xxwidgets_widget_create(owner->app, owner, kinds[i], XXW_TR(owner->app, names[i]), (xxwidgets_rect){0, 0, 1, 1}, &v->controls[i]);
        if (status != XXWIDGETS_OK)
            goto fail;
        v->controls[i]->entropy = v;
    }
    status = xxwidgets_entropy_set_rect(v, bounds);
    if (status != XXWIDGETS_OK)
        goto fail;
    const char *maps[] = {XXW_TR(owner->app, "Regions"), XXW_TR(owner->app, "Whole file")};
    status = choice(CONTROL(v, MAP), maps, 2);
    if (status != XXWIDGETS_OK)
        goto fail;
    status = xxwidgets_widget_set_visible(CONTROL(v, BYTES_TABLE), 0);
    if (status != XXWIDGETS_OK)
        goto fail;
    status = refresh(v);
    if (status != XXWIDGETS_OK)
        goto fail;
    xxwidgets_widget_set_enabled(CONTROL(v, TAB), 0);
    *out = v;
    return XXWIDGETS_OK;
fail:
    xxwidgets_entropy_destroy(v);
    return status;
}
xxwidgets_status xxwidgets_entropy_destroy(xxwidgets_entropy *v)
{
    if (!v)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (v->owner->app->dispatch_depth || v->owner->app->polling)
        return XXWIDGETS_BUSY;
    for (size_t i = 0; i < XXWIDGETS_ENTROPY_CONTROL_COUNT; ++i)
        if (v->controls[i])
            xxwidgets_widget_destroy(v->controls[i]);
    free_model(v->model);
    free(v->visible_regions);
    free(v);
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_entropy_set_data(xxwidgets_entropy *v, const void *data, size_t size, uint64_t base, const char *type,
                                            const xxwidgets_entropy_region *regions, size_t count)
{
    if (!v || (!data && size) || size > UINT64_MAX - base || !xxwidgets_valid_utf8(type ? type : "Binary") || (!regions && count) ||
        count > INT_MAX || count > SIZE_MAX / sizeof(*regions))
        return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i)
        if (regions[i].offset > size || regions[i].size > size - regions[i].offset || !xxwidgets_valid_utf8(regions[i].name ? regions[i].name : ""))
            return XXWIDGETS_INVALID_ARGUMENT;
    entropy_model *m = calloc(1, sizeof(*m));
    if (!m)
        return XXWIDGETS_OUT_OF_MEMORY;
    m->size = size;
    m->base = base;
    m->type = xxwidgets_strdup(type ? type : "Binary");
    m->data = size ? malloc(size) : NULL;
    m->region_count = count ? count : 1;
    m->regions = calloc(m->region_count, sizeof(*m->regions));
    m->region_entropy = calloc(m->region_count, sizeof(double));
    if (!m->type || (size && !m->data) || !m->regions || !m->region_entropy) {
        m->region_count = 0;
        free_model(m);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    if (size)
        memcpy(m->data, data, size);
    for (size_t i = 0; i < size; ++i)
        ++m->counts[m->data[i]];
    m->total = counts_entropy(m->counts, size);
    for (size_t i = 0; i < m->region_count; ++i) {
        m->regions[i] = count ? regions[i] : (xxwidgets_entropy_region){0, size, "Whole file"};
        m->regions[i].name = xxwidgets_strdup(count ? (regions[i].name ? regions[i].name : "") : "Whole file");
        if (!m->regions[i].name) {
            free_model(m);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        m->region_entropy[i] = data_entropy(m->data ? m->data + m->regions[i].offset : NULL, (size_t)m->regions[i].size);
    }
    size_t samples = size / 512;
    if (samples < 1)
        samples = 1;
    if (samples > 100)
        samples = 100;
    xxwidgets_status status = make_samples(m, samples);
    if (status != XXWIDGETS_OK) {
        free_model(m);
        return status;
    }
    entropy_model *old = v->model;
    v->model = m;
    status = refresh(v);
    if (status != XXWIDGETS_OK) {
        v->model = old;
        refresh(v);
        free_model(m);
    } else
        free_model(old);
    return status;
}
xxwidgets_status xxwidgets_entropy_set_statistics(xxwidgets_entropy *v, const xxwidgets_entropy_statistics *s)
{
    uint64_t sum = 0, end;
    entropy_model *m, *old;
    xxwidgets_status status;
    if (!v || !s || !s->counts || s->size > UINT64_MAX - s->base ||
        !xxwidgets_valid_utf8(s->file_type ? s->file_type : "Binary") ||
        !isfinite(s->total) || s->total < 0 || s->total > 8 ||
        s->region_count > 1024 || (s->region_count && (!s->regions || !s->region_entropy)) ||
        s->sample_count > 4096 || (s->sample_count && !s->samples) ||
        (!s->size != !s->sample_count)) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < 256; ++i) {
        if (s->counts[i] > s->size - sum) return XXWIDGETS_INVALID_ARGUMENT;
        sum += s->counts[i];
    }
    if (sum != s->size || fabs(counts_entropy(s->counts, s->size) - s->total) > 1e-8) return XXWIDGETS_INVALID_ARGUMENT;
    end = s->base;
    for (size_t i = 0; i < s->sample_count; ++i) {
        const xxwidgets_entropy_sample *sample = s->samples + i;
        if (sample->offset != end || !sample->size || sample->size > s->size - (end - s->base) ||
            !isfinite(sample->entropy) || sample->entropy < 0 || sample->entropy > 8) return XXWIDGETS_INVALID_ARGUMENT;
        end += sample->size;
    }
    if (end != s->base + s->size) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < s->region_count; ++i)
        if (s->regions[i].offset > s->size || s->regions[i].size > s->size - s->regions[i].offset ||
            !xxwidgets_valid_utf8(s->regions[i].name ? s->regions[i].name : "") ||
            !isfinite(s->region_entropy[i]) || s->region_entropy[i] < 0 || s->region_entropy[i] > 8)
            return XXWIDGETS_INVALID_ARGUMENT;
    m = calloc(1, sizeof(*m));
    if (!m) return XXWIDGETS_OUT_OF_MEMORY;
    m->snapshot = 1; m->size = s->size; m->base = s->base; m->total = s->total;
    m->requested_count = s->sample_count; m->sample_count = s->sample_count;
    m->type = xxwidgets_strdup(s->file_type ? s->file_type : "Binary");
    m->regions = calloc(s->region_count ? s->region_count : 1, sizeof(*m->regions));
    m->region_entropy = calloc(s->region_count ? s->region_count : 1, sizeof(*m->region_entropy));
    m->samples = s->sample_count ? malloc(s->sample_count * sizeof(*m->samples)) : NULL;
    if (!m->type || !m->regions || !m->region_entropy || (s->sample_count && !m->samples)) {
        free_model(m); return XXWIDGETS_OUT_OF_MEMORY;
    }
    memcpy(m->counts, s->counts, sizeof(m->counts));
    if (s->sample_count) memcpy(m->samples, s->samples, s->sample_count * sizeof(*m->samples));
    for (size_t i = 0; i < s->region_count; ++i) {
        m->regions[i] = s->regions[i];
        m->regions[i].name = xxwidgets_strdup(s->regions[i].name ? s->regions[i].name : "");
        m->region_count = i + 1;
        if (!m->regions[i].name) { free_model(m); return XXWIDGETS_OUT_OF_MEMORY; }
        m->region_entropy[i] = s->region_entropy[i];
    }
    old = v->model; v->model = m;
    status = refresh(v);
    if (status != XXWIDGETS_OK) { v->model = old; refresh(v); free_model(m); }
    else free_model(old);
    return status;
}
xxwidgets_status xxwidgets_entropy_set_count(xxwidgets_entropy *v, size_t count)
{
    if (!v || !count || count > 1000)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (!v->model)
        return XXWIDGETS_OK;
    entropy_model *m = v->model;
    if (m->snapshot) return XXWIDGETS_UNAVAILABLE;
    xxwidgets_entropy_sample *old = m->samples;
    size_t old_count = m->sample_count, old_requested = m->requested_count;
    m->samples = NULL;
    xxwidgets_status status = make_samples(m, count);
    if (status == XXWIDGETS_OK)
        status = refresh(v);
    if (status != XXWIDGETS_OK) {
        free(m->samples);
        m->samples = old;
        m->sample_count = old_count;
        m->requested_count = old_requested;
        refresh(v);
    } else
        free(old);
    return status;
}
double xxwidgets_entropy_total(const xxwidgets_entropy *v) { return v && v->model ? v->model->total : 0; }
uint64_t xxwidgets_entropy_byte_count(const xxwidgets_entropy *v, unsigned int byte)
{
    return v && v->model && byte < 256 ? v->model->counts[byte] : 0;
}
size_t xxwidgets_entropy_sample_count(const xxwidgets_entropy *v) { return v && v->model ? v->model->sample_count : 0; }
size_t xxwidgets_entropy_region_count(const xxwidgets_entropy *v) { return v && v->model ? v->model->region_count : 0; }
xxwidgets_status xxwidgets_entropy_get_sample(const xxwidgets_entropy *v, size_t i, xxwidgets_entropy_sample *sample)
{
    if (!v || !v->model || !sample || i >= v->model->sample_count)
        return XXWIDGETS_INVALID_ARGUMENT;
    *sample = v->model->samples[i];
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_entropy_get_region(const xxwidgets_entropy *v, size_t i, xxwidgets_entropy_region *region, double *entropy)
{
    if (!v || !v->model || !region || !entropy || i >= v->model->region_count)
        return XXWIDGETS_INVALID_ARGUMENT;
    *region = v->model->regions[i];
    *entropy = v->model->region_entropy[i];
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_entropy_handle_event(xxwidgets_entropy *v, const xxwidgets_event *event)
{
    if (!v || !event)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (event->type == XXWIDGETS_EVENT_CLICK && (event->widget == CONTROL(v, TAB) || event->widget == CONTROL(v, BYTES_TAB))) {
        v->bytes = event->widget == CONTROL(v, BYTES_TAB);
        xxwidgets_status status = xxwidgets_widget_set_visible(CONTROL(v, REGIONS_TABLE), !v->bytes);
        if (status != XXWIDGETS_OK)
            return status;
        status = xxwidgets_widget_set_visible(CONTROL(v, BYTES_TABLE), v->bytes);
        if (status != XXWIDGETS_OK)
            return status;
        xxwidgets_widget_set_enabled(CONTROL(v, FILTER), !v->bytes);
        xxwidgets_widget_set_enabled(CONTROL(v, TAB), v->bytes);
        xxwidgets_widget_set_enabled(CONTROL(v, BYTES_TAB), !v->bytes);
        return sync_control(CONTROL(v, GRAPH));
    }
    if ((event->type == XXWIDGETS_EVENT_CLICK && event->widget == CONTROL(v, RELOAD)) ||
        (event->type == XXWIDGETS_EVENT_ACTIVATE && event->widget == CONTROL(v, COUNT))) {
        char text[32], *end = NULL;
        unsigned long count;
        if (xxwidgets_widget_get_text(CONTROL(v, COUNT), text, sizeof(text), NULL) != XXWIDGETS_OK)
            return XXWIDGETS_INVALID_ARGUMENT;
        count = strtoul(text, &end, 10);
        if (!text[0] || *end || count < 1 || count > 1000) {
            xxwidgets_widget_set_text(CONTROL(v, TRACKER), "Count must be 1..1000");
            return XXWIDGETS_INVALID_ARGUMENT;
        }
        return xxwidgets_entropy_set_count(v, (size_t)count);
    }
    if (event->type == XXWIDGETS_EVENT_CHANGE && event->widget == CONTROL(v, FILTER))
        return refresh_table(v, 0);
    if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == CONTROL(v, MAP)) {
        v->whole = event->value == 1;
        xxwidgets_status status = refresh_table(v, 0);
        return status == XXWIDGETS_OK ? sync_control(CONTROL(v, GRAPH)) : status;
    }
    if ((event->type == XXWIDGETS_EVENT_CLICK || event->type == XXWIDGETS_EVENT_CHANGE) && event->widget == CONTROL(v, GRID)) {
        v->grid = CONTROL(v, GRID)->value;
        return sync_control(CONTROL(v, GRAPH));
    }
    if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == CONTROL(v, REGIONS_TABLE)) {
        v->selected_region = event->value >= 0 && (size_t)event->value < event->widget->item_count ? v->visible_regions[event->value] : SIZE_MAX;
        return sync_control(CONTROL(v, GRAPH));
    }
    if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == CONTROL(v, GRAPH) && v->model) {
        char text[128];
        size_t index = (size_t)event->value;
        if (v->bytes && index < 256)
            snprintf(text, sizeof(text), "Byte 0x%02X: %" PRIu64 " (%.4f%%)", (unsigned)index, v->model->counts[index],
                     v->model->size ? 100.0 * v->model->counts[index] / v->model->size : 0);
        else if (index < v->model->sample_count) {
            xxwidgets_entropy_sample s = v->model->samples[index];
            snprintf(text, sizeof(text), "Offset 0x%" PRIX64 " | Size 0x%" PRIX64 " | Entropy %.5f", s.offset, s.size, s.entropy);
        } else
            return XXWIDGETS_OK;
        return xxwidgets_widget_set_text(CONTROL(v, TRACKER), text);
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_entropy_plot_set_samples(xxwidgets_widget *plot,
    const xxwidgets_entropy_sample *samples, size_t count, uint64_t base, uint64_t size)
{
    xxwidgets_entropy_sample *copy = NULL, *old;
    size_t old_count;
    uint64_t old_base, old_size, end = base;
    xxwidgets_status status;
    if (!plot || plot->kind != XXWIDGETS_ENTROPY_PLOT || plot->entropy ||
        (!samples && count) || count > 4096 || size > UINT64_MAX - base || (count && !size)) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i) {
        if (!isfinite(samples[i].entropy) || samples[i].entropy < 0 || samples[i].entropy > 8 ||
            !samples[i].size || samples[i].offset < end || samples[i].offset < base ||
            samples[i].offset - base > size || samples[i].size > size - (samples[i].offset - base)) return XXWIDGETS_INVALID_ARGUMENT;
        end = samples[i].offset + samples[i].size;
    }
    if (count) {
        copy = malloc(count * sizeof(*copy));
        if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
        memcpy(copy, samples, count * sizeof(*copy));
    }
    old = plot->entropy_plot_samples; old_count = plot->entropy_plot_count;
    old_base = plot->entropy_plot_base; old_size = plot->entropy_plot_size;
    plot->entropy_plot_samples = copy; plot->entropy_plot_count = count;
    plot->entropy_plot_base = base; plot->entropy_plot_size = size;
    ++plot->entropy_revision;
    status = sync_control(plot);
    if (status != XXWIDGETS_OK) {
        plot->entropy_plot_samples = old; plot->entropy_plot_count = old_count;
        plot->entropy_plot_base = old_base; plot->entropy_plot_size = old_size;
        ++plot->entropy_revision; (void)sync_control(plot); free(copy);
    } else free(old);
    return status;
}

void xxwidgets_entropy_draw(const xxwidgets_widget *widget, double width, double height, double cx, double cy, xxwidgets_plot_draw_fn draw,
                            void *user)
{
    xxwidgets_entropy *v = widget ? widget->entropy : NULL;
    entropy_model *m = v ? v->model : NULL;
    double left = 6 * cx, top = cy, right = width - cx, bottom = height - 2 * cy, maximum = 8;
    char label[64];
    if (!draw || width <= 0 || height <= 0)
        return;
#define DRAW(k, x, y, x2, y2, c, t) draw(user, k, x, y, x2, y2, c, t)
    DRAW(1, 0, 0, width, height, 0xf8f8f8, NULL);
    if (right <= left || bottom <= top)
        return;
    if (v && v->bytes) {
        maximum = 1;
        if (m)
            for (size_t i = 0; i < 256; ++i)
                if ((double)m->counts[i] > maximum)
                    maximum = (double)m->counts[i];
    }
    if (v && !v->bytes && m && v->selected_region < m->region_count && m->size) {
        xxwidgets_entropy_region region = m->regions[v->selected_region];
        double x = left + (right - left) * (double)region.offset / (double)m->size;
        double end = left + (right - left) * (double)(region.offset + region.size) / (double)m->size;
        DRAW(1, x, top, end, bottom, 0xdce7f5, NULL);
    }
    if (!v && widget && widget->value >= 0 && (size_t)widget->value < widget->entropy_plot_count && widget->entropy_plot_size) {
        xxwidgets_entropy_sample selected = widget->entropy_plot_samples[widget->value];
        double x = left + (right - left) * (double)(selected.offset - widget->entropy_plot_base) / (double)widget->entropy_plot_size;
        double end = left + (right - left) * (double)(selected.offset - widget->entropy_plot_base + selected.size) / (double)widget->entropy_plot_size;
        DRAW(1, x, top, end, bottom, 0xdce7f5, NULL);
    }
    for (int i = 0; i <= 8; ++i) {
        double y = bottom - (bottom - top) * i / 8.0;
        if (v && v->grid)
            DRAW(0, left, y, right, y, 0xd8d8d8, NULL);
        if (!v || !v->bytes)
            snprintf(label, sizeof(label), "%d", i);
        else
            snprintf(label, sizeof(label), "%.0f", maximum * i / 8.0);
        if (!v || !v->bytes || i % 2 == 0)
            DRAW(2, 0, y - cy / 2, 0, 0, 0x333333, label);
    }
    DRAW(0, left, top, left, bottom, 0x555555, NULL);
    DRAW(0, left, bottom, right, bottom, 0x555555, NULL);
    for (int i = 0; i <= 4; ++i) {
        double x = left + (right - left) * i / 4.0;
        if (v && v->grid)
            DRAW(0, x, top, x, bottom, 0xd8d8d8, NULL);
        if (v && v->bytes)
            snprintf(label, sizeof(label), "%d", i * 64);
        else {
            uint64_t base = m ? m->base : widget ? widget->entropy_plot_base : 0;
            uint64_t size = m ? m->size : widget ? widget->entropy_plot_size : 0;
            snprintf(label, sizeof(label), "%" PRIu64, base + (size / 4) * i + (size % 4) * i / 4);
        }
        double tx = x - (double)strlen(label) * cx / 2;
        if (tx < 0)
            tx = 0;
        if (tx + (double)strlen(label) * cx > width)
            tx = width - (double)strlen(label) * cx;
        DRAW(2, tx, bottom + cy / 2, 0, 0, 0x333333, label);
    }
    if (m && v->bytes) {
        for (size_t i = 0; i < 256; ++i) {
            double x = left + (right - left) * (double)i / 256, end = left + (right - left) * (double)(i + 1) / 256;
            double y = bottom - (bottom - top) * (double)m->counts[i] / maximum;
            if (m->counts[i])
                DRAW(1, x, y, end, bottom, 0x2778c4, NULL);
        }
    } else if ((m && m->sample_count && m->size) || (widget && widget->entropy_plot_count && widget->entropy_plot_size)) {
        const xxwidgets_entropy_sample *samples = m ? m->samples : widget->entropy_plot_samples;
        size_t count = m ? m->sample_count : widget->entropy_plot_count;
        uint64_t base = m ? m->base : widget->entropy_plot_base, size = m ? m->size : widget->entropy_plot_size;
        for (size_t i = 0; i < count; ++i) {
            xxwidgets_entropy_sample a = samples[i], b = i + 1 < count ? samples[i + 1] : a;
            double x = left + (right - left) * (double)(a.offset - base) / (double)size;
            double end = i + 1 < count ? left + (right - left) * (double)(b.offset - base) / (double)size : right;
            DRAW(0, x, bottom - (bottom - top) * a.entropy / 8, end, bottom - (bottom - top) * b.entropy / 8, 0xd62222, NULL);
        }
    }
#undef DRAW
}
void xxwidgets_entropy_plot_input(xxwidgets_widget *widget, double x, double width, double cell_x)
{
    if (!widget || !widget->enabled || !widget->parent || !widget->parent->enabled || widget->app->syncing)
        return;
    xxwidgets_entropy *v = widget->entropy;
    entropy_model *m = v ? v->model : NULL;
    const xxwidgets_entropy_sample *samples = m ? m->samples : widget->entropy_plot_samples;
    size_t count = m ? m->sample_count : widget->entropy_plot_count;
    uint64_t size = m ? m->size : widget->entropy_plot_size, base = m ? m->base : widget->entropy_plot_base;
    if (!size || (!count && !(v && v->bytes)) || width <= 7 * cell_x) return;
    double fraction = (x - 6 * cell_x) / (width - 7 * cell_x);
    if (fraction < 0)
        fraction = 0;
    if (fraction > 1)
        fraction = 1;
    size_t index = 0;
    if (v && v->bytes) {
        index = (size_t)(fraction * 256);
        if (index > 255)
            index = 255;
    } else {
        uint64_t offset = fraction >= 1 ? size : (uint64_t)(fraction * (double)size);
        while (index + 1 < count && samples[index + 1].offset - base <= offset)
            ++index;
    }
    xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, (int)index);
}

typedef struct output_text {
    char *data;
    size_t length, capacity;
    int failed;
} output_text;
static void append(output_text *out, const char *text)
{
    size_t n = strlen(text);
    if (out->failed)
        return;
    if (n >= SIZE_MAX - out->length) {
        out->failed = 1;
        return;
    }
    if (out->length + n + 1 > out->capacity) {
        size_t cap = out->length + n + 1;
        if (cap < 4096)
            cap = 4096;
        if (cap <= SIZE_MAX / 2)
            cap *= 2;
        char *data = realloc(out->data, cap);
        if (!data) {
            out->failed = 1;
            return;
        }
        out->data = data;
        out->capacity = cap;
    }
    memcpy(out->data + out->length, text, n + 1);
    out->length += n;
}
static void print(output_text *out, const char *format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof(text)) {
        out->failed = 1;
        return;
    }
    append(out, text);
}
static xxwidgets_status output_result(output_text *out, char *buffer, size_t capacity, size_t *required)
{
    xxwidgets_status status = XXWIDGETS_OK;
    if (out->failed) {
        free(out->data);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    if (required)
        *required = out->length + 1;
    if (buffer && capacity) {
        size_t n = out->length < capacity - 1 ? out->length : capacity - 1;
        if (n)
            memcpy(buffer, out->data, n);
        buffer[n] = 0;
    }
    if (buffer && capacity < out->length + 1)
        status = XXWIDGETS_BUFFER_TOO_SMALL;
    free(out->data);
    return status;
}
static void csv_cell(output_text *out, const char *cell)
{
    append(out, "\"");
    for (; *cell; ++cell) {
        char c[] = {*cell, 0};
        if (*cell == '"')
            append(out, "\"");
        append(out, c);
    }
    append(out, "\"");
}
xxwidgets_status xxwidgets_entropy_export_csv(const xxwidgets_entropy *v, char *buffer, size_t capacity, size_t *required)
{
    if (!v || (!buffer && capacity))
        return XXWIDGETS_INVALID_ARGUMENT;
    output_text out = {0};
    xxwidgets_widget *table = v->bytes ? CONTROL(v, BYTES_TABLE) : CONTROL(v, REGIONS_TABLE);
    size_t columns = xxwidgets_tabular_column_count(table);
    for (size_t row = 0; row <= table->item_count; ++row) {
        for (size_t col = 0; col < columns; ++col) {
            if (col)
                append(&out, ",");
            csv_cell(&out, row ? xxwidgets_entropy_cell(table, row - 1, col) : xxwidgets_tabular_column_title(table, col));
        }
        append(&out, "\r\n");
    }
    return output_result(&out, buffer, capacity, required);
}
/* Serialize SVG numbers independently of the host application's numeric locale. */
static void svg_number(char text[64], double value)
{
    snprintf(text, 64, "%.2f", value);
    const char *decimal = localeconv()->decimal_point;
    size_t length = strlen(decimal);
    if (length && strcmp(decimal, ".")) {
        char *point = strstr(text, decimal);
        if (point) {
            *point = '.';
            if (length > 1)
                memmove(point + 1, point + length, strlen(point + length) + 1);
        }
    }
}
static void svg_draw(void *user, int kind, double x, double y, double x2, double y2, uint32_t rgb, const char *text)
{
    output_text *out = user;
    char a[64], b[64], c[64], d[64];
    svg_number(a, x);
    svg_number(b, kind == 2 ? y + 12 : y);
    svg_number(c, kind == 1 ? x2 - x : x2);
    svg_number(d, kind == 1 ? y2 - y : y2);
    if (kind == 0)
        print(out, "<line x1=\"%s\" y1=\"%s\" x2=\"%s\" y2=\"%s\" stroke=\"#%06x\"/>\n", a, b, c, d, (unsigned)rgb);
    else if (kind == 1)
        print(out, "<rect x=\"%s\" y=\"%s\" width=\"%s\" height=\"%s\" fill=\"#%06x\"/>\n", a, b, c, d, (unsigned)rgb);
    else
        print(out, "<text x=\"%s\" y=\"%s\" fill=\"#%06x\" font-family=\"monospace\" font-size=\"12\">%s</text>\n", a, b, (unsigned)rgb, text);
}
xxwidgets_status xxwidgets_entropy_export_svg(const xxwidgets_entropy *v, int width, int height, char *buffer, size_t capacity, size_t *required)
{
    if (!v || (!buffer && capacity) || width < 160 || width > 16384 || height < 100 || height > 16384)
        return XXWIDGETS_INVALID_ARGUMENT;
    output_text out = {0};
    print(&out, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\">\n", width, height, width, height);
    /* Plot text is numeric ASCII generated above, never a caller-supplied name. */
    xxwidgets_entropy_draw(CONTROL(v, GRAPH), width, height, 7, 14, svg_draw, &out);
    append(&out, "</svg>\n");
    return output_result(&out, buffer, capacity, required);
}
