#include "xxwidgets_internal.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct browser_source {
    xxwidgets_archive_browser_entry entry;
    char *normalized;
} browser_source;

typedef struct browser_storage {
    size_t references, count;
    browser_source *sources;
    char **titles;
    size_t property_count;
} browser_storage;

typedef struct browser_row {
    xxwidgets_archive_browser_entry entry;
    size_t source_index;
    char *name;
    char *normalized;
    char **cells;
    int selected;
} browser_row;

struct xxwidgets_archive_browser_state {
    browser_storage *storage;
    char *archive;
    char *directory;
    browser_row *rows;
    size_t row_count, name_columns, column_count;
    xxwidgets_archive_column column;
    int descending, advanced, selection_ready;
};
typedef struct xxwidgets_archive_browser_state browser_state;

static int browser_widget(const xxwidgets_widget *widget)
{
    return widget && widget->kind == XXWIDGETS_ARCHIVEBROWSER;
}

static int utf8_valid(const char *text)
{
    const unsigned char *s = (const unsigned char *)text;
    if (!s) return 0;
    while (*s) {
        unsigned int cp, minimum;
        int remaining;
        if (*s < 0x80) {
            ++s;
            continue;
        }
        if (*s >= 0xc2 && *s <= 0xdf) {
            cp = *s & 31;
            minimum = 0x80;
            remaining = 1;
        } else if (*s >= 0xe0 && *s <= 0xef) {
            cp = *s & 15;
            minimum = 0x800;
            remaining = 2;
        } else if (*s >= 0xf0 && *s <= 0xf4) {
            cp = *s & 7;
            minimum = 0x10000;
            remaining = 3;
        } else return 0;
        ++s;
        while (remaining--) {
            if ((*s & 0xc0) != 0x80) return 0;
            cp = (cp << 6) | (*s++ & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
    }
    return 1;
}

static char *copy_range(const char *text, size_t length)
{
    char *copy = (char *)malloc(length + 1);
    if (copy) {
        memcpy(copy, text, length);
        copy[length] = 0;
    }
    return copy;
}

/* The model retains the original member path separately from this normalized
 * display path. It never interprets display paths as filesystem destinations. */
static xxwidgets_status normalize_path(const char *path, int directory, char **out)
{
    const char *start, *cursor;
    size_t length, used = 0;
    char *result;
    *out = NULL;
    if (!utf8_valid(path)) return XXWIDGETS_INVALID_ARGUMENT;
    length = strlen(path);
    if (length > SIZE_MAX - 2) return XXWIDGETS_INVALID_ARGUMENT;
    result = (char *)malloc(length + 2);
    if (!result) return XXWIDGETS_OUT_OF_MEMORY;
    cursor = path;
    while (*cursor) {
        size_t segment;
        while (*cursor == '/' || *cursor == '\\') ++cursor;
        start = cursor;
        while (*cursor && *cursor != '/' && *cursor != '\\') ++cursor;
        segment = (size_t)(cursor - start);
        if (!segment || (segment == 1 && start[0] == '.')) continue;
        if (segment == 2 && start[0] == '.' && start[1] == '.') {
            free(result);
            return XXWIDGETS_INVALID_ARGUMENT;
        }
        if (used) result[used++] = '/';
        memcpy(result + used, start, segment);
        used += segment;
    }
    if (directory && used) result[used++] = '/';
    result[used] = 0;
    *out = result;
    return XXWIDGETS_OK;
}

static void storage_release(browser_storage *storage)
{
    size_t i;
    if (!storage || --storage->references) return;
    for (i = 0; i < storage->count; ++i) {
        size_t j;
        xxwidgets_archive_property *properties = (xxwidgets_archive_property *)storage->sources[i].entry.properties;
        for (j = 0; j < storage->sources[i].entry.property_count; ++j) {
            free((char *)properties[j].name);
            free((char *)properties[j].value);
        }
        free(properties);
        free((void *)storage->sources[i].entry.path);
        free((void *)storage->sources[i].entry.modified);
        free((void *)storage->sources[i].entry.attributes);
        free(storage->sources[i].normalized);
    }
    free(storage->sources);
    for (i = 0; i < storage->property_count; ++i) free(storage->titles[i]);
    free(storage->titles);
    free(storage);
}

static xxwidgets_status storage_create(const xxwidgets_archive_browser_entry *entries, size_t count, browser_storage **out)
{
    browser_storage *storage;
    size_t i;
    xxwidgets_status status;
    *out = NULL;
    if ((!entries && count) || count > INT_MAX || count > SIZE_MAX / sizeof(browser_source)) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < count; ++i) {
        size_t j;
        if (!utf8_valid(entries[i].path) || !entries[i].path[0] || (entries[i].is_directory != 0 && entries[i].is_directory != 1) ||
            (entries[i].flags & ~(XXWIDGETS_ARCHIVE_SIZE_KNOWN | XXWIDGETS_ARCHIVE_PACKED_SIZE_KNOWN)) || (entries[i].modified && !utf8_valid(entries[i].modified)) ||
            (entries[i].attributes && !utf8_valid(entries[i].attributes)))
            return XXWIDGETS_INVALID_ARGUMENT;
        if ((!entries[i].properties && entries[i].property_count) || entries[i].property_count > SIZE_MAX / sizeof(xxwidgets_archive_property))
            return XXWIDGETS_INVALID_ARGUMENT;
        for (j = 0; j < entries[i].property_count; ++j)
            if (!utf8_valid(entries[i].properties[j].name) || !entries[i].properties[j].name[0] || !utf8_valid(entries[i].properties[j].value))
                return XXWIDGETS_INVALID_ARGUMENT;
    }
    storage = (browser_storage *)calloc(1, sizeof(*storage));
    if (!storage) return XXWIDGETS_OUT_OF_MEMORY;
    storage->references = 1;
    if (count) {
        storage->sources = (browser_source *)calloc(count, sizeof(*storage->sources));
        if (!storage->sources) {
            storage_release(storage);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
    }
    storage->count = count;
    for (i = 0; i < count; ++i) {
        browser_source *source = &storage->sources[i];
        source->entry = entries[i];
        /* Install only owned pointers, so cleanup is safe after any failure. */
        source->entry.path = source->entry.modified = source->entry.attributes = NULL;
        source->entry.properties = NULL;
        source->entry.property_count = 0;
        source->entry.path = xxwidgets_strdup(entries[i].path);
        source->entry.modified = xxwidgets_strdup(entries[i].modified ? entries[i].modified : "");
        source->entry.attributes = xxwidgets_strdup(entries[i].attributes ? entries[i].attributes : "");
        if (!source->entry.path || !source->entry.modified || !source->entry.attributes) {
            storage_release(storage);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        status = normalize_path(entries[i].path, entries[i].is_directory, &source->normalized);
        if (status != XXWIDGETS_OK) {
            storage_release(storage);
            return status;
        }
        if (!source->normalized[0] && !source->entry.is_directory) {
            storage_release(storage);
            return XXWIDGETS_INVALID_ARGUMENT;
        }
        if (entries[i].property_count) {
            size_t j;
            xxwidgets_archive_property *properties = (xxwidgets_archive_property *)calloc(entries[i].property_count, sizeof(*properties));
            if (!properties) {
                storage_release(storage);
                return XXWIDGETS_OUT_OF_MEMORY;
            }
            source->entry.properties = properties;
            source->entry.property_count = entries[i].property_count;
            for (j = 0; j < entries[i].property_count; ++j) {
                size_t k;
                properties[j].name = xxwidgets_strdup(entries[i].properties[j].name);
                properties[j].value = xxwidgets_strdup(entries[i].properties[j].value);
                if (!properties[j].name || !properties[j].value) {
                    storage_release(storage);
                    return XXWIDGETS_OUT_OF_MEMORY;
                }
                for (k = 0; k < storage->property_count; ++k)
                    if (!strcmp(properties[j].name, storage->titles[k])) break;
                if (k == storage->property_count) {
                    char **grown;
                    char *title;
                    if (k >= INT_MAX - 5 || k >= SIZE_MAX / sizeof(*grown) - 1) {
                        storage_release(storage);
                        return XXWIDGETS_INVALID_ARGUMENT;
                    }
                    title = xxwidgets_strdup(properties[j].name);
                    grown = (char **)realloc(storage->titles, (k + 1) * sizeof(*grown));
                    if (!title || !grown) {
                        free(title);
                        if (grown) storage->titles = grown;
                        storage_release(storage);
                        return XXWIDGETS_OUT_OF_MEMORY;
                    }
                    storage->titles = grown;
                    storage->titles[storage->property_count++] = title;
                }
            }
        }
    }
    *out = storage;
    return XXWIDGETS_OK;
}

static void state_free(browser_state *state)
{
    size_t i;
    if (!state) return;
    for (i = 0; i < state->row_count; ++i) {
        size_t column;
        free(state->rows[i].name);
        free(state->rows[i].normalized);
        if (state->rows[i].cells)
            for (column = 0; column < state->column_count; ++column) free(state->rows[i].cells[column]);
        free(state->rows[i].cells);
    }
    free(state->rows);
    storage_release(state->storage);
    free(state->archive);
    free(state->directory);
    free(state);
}

void xxwidgets_archivebrowser_dispose(xxwidgets_widget *widget)
{
    state_free(widget->browser);
    widget->browser = NULL;
}

static browser_state *state_copy(const browser_state *previous)
{
    browser_state *state = (browser_state *)calloc(1, sizeof(*state));
    if (!state) return NULL;
    state->archive = xxwidgets_strdup(previous ? previous->archive : "");
    state->directory = xxwidgets_strdup(previous ? previous->directory : "");
    if (!state->archive || !state->directory) {
        state_free(state);
        return NULL;
    }
    if (previous) {
        state->storage = previous->storage;
        if (state->storage) ++state->storage->references;
        state->column = previous->column;
        state->descending = previous->descending;
        state->advanced = previous->advanced;
    }
    return state;
}

static size_t name_hash(const char *name, size_t length)
{
    size_t result = (size_t)2166136261u, i;
    for (i = 0; i < length; ++i) result = (result ^ (unsigned char)name[i]) * (size_t)16777619u;
    return result;
}

static int name_compare(const char *a, const char *b)
{
    const unsigned char *left = (const unsigned char *)a, *right = (const unsigned char *)b;
    while (*left && *right) {
        unsigned int x = *left++, y = *right++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return x < y ? -1 : 1;
    }
    if (*left != *right) return *left ? 1 : -1;
    return strcmp(a, b);
}

static const char *property_value(const xxwidgets_archive_browser_entry *entry, const char *name)
{
    size_t i;
    for (i = 0; i < entry->property_count; ++i)
        if (!strcmp(entry->properties[i].name, name)) return entry->properties[i].value;
    return "";
}

static int row_compare(const browser_row *a, const browser_row *b, const browser_state *state)
{
    int result = 0;
    unsigned int flag = 0;
    uint64_t left = 0, right = 0;
    if (a->entry.is_directory != b->entry.is_directory) return a->entry.is_directory ? -1 : 1;
    if (state->column == XXWIDGETS_ARCHIVE_COLUMN_SIZE) {
        flag = XXWIDGETS_ARCHIVE_SIZE_KNOWN;
        left = a->entry.size;
        right = b->entry.size;
    } else if (state->column == XXWIDGETS_ARCHIVE_COLUMN_PACKED_SIZE) {
        flag = XXWIDGETS_ARCHIVE_PACKED_SIZE_KNOWN;
        left = a->entry.packed_size;
        right = b->entry.packed_size;
    }
    if (flag) {
        int left_known = (a->entry.flags & flag) != 0, right_known = (b->entry.flags & flag) != 0;
        if (left_known != right_known) return left_known ? -1 : 1;
        if (left_known) result = left < right ? -1 : left > right ? 1 : 0;
    } else if (state->column == XXWIDGETS_ARCHIVE_COLUMN_MODIFIED) result = strcmp(a->entry.modified, b->entry.modified);
    else if (state->column == XXWIDGETS_ARCHIVE_COLUMN_ATTRIBUTES) result = strcmp(a->entry.attributes, b->entry.attributes);
    else if ((int)state->column >= 5 && state->storage && (size_t)state->column - 5 < state->storage->property_count)
        result = strcmp(property_value(&a->entry, state->storage->titles[(size_t)state->column - 5]),
                        property_value(&b->entry, state->storage->titles[(size_t)state->column - 5]));
    else result = name_compare(a->name, b->name);
    if (!result) result = name_compare(a->name, b->name);
    if (!result) result = a->source_index < b->source_index ? -1 : a->source_index > b->source_index ? 1 : 0;
    if (state->descending) result = result < 0 ? 1 : result > 0 ? -1 : 0;
    return result;
}

/* Context-aware stable merge sort keeps this model reentrant without qsort's
 * global comparison state and avoids quadratic insertion sorting. */
static xxwidgets_status sort_rows(browser_state *state)
{
    browser_row *temporary;
    size_t width, n = state->row_count;
    if (n < 2) return XXWIDGETS_OK;
    temporary = (browser_row *)malloc(n * sizeof(*temporary));
    if (!temporary) return XXWIDGETS_OUT_OF_MEMORY;
    for (width = 1; width < n; width *= 2) {
        size_t start;
        for (start = 0; start < n; start += width * 2) {
            size_t left = start, middle = start + width < n ? start + width : n;
            size_t right = middle, end = middle + width < n ? middle + width : n, out = start;
            while (left < middle && right < end)
                temporary[out++] = row_compare(&state->rows[left], &state->rows[right], state) <= 0 ? state->rows[left++] : state->rows[right++];
            while (left < middle) temporary[out++] = state->rows[left++];
            while (right < end) temporary[out++] = state->rows[right++];
        }
        memcpy(state->rows, temporary, n * sizeof(*temporary));
    }
    free(temporary);
    return XXWIDGETS_OK;
}

static xxwidgets_status build_view(browser_state *state)
{
    size_t count = state->storage ? state->storage->count : 0;
    size_t prefix = strlen(state->directory), capacity = 16, i;
    size_t *slots = NULL;
    if (!count) return XXWIDGETS_OK;
    while (capacity < count * 2) {
        if (capacity > SIZE_MAX / 2) return XXWIDGETS_OUT_OF_MEMORY;
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(*slots)) return XXWIDGETS_OUT_OF_MEMORY;
    state->rows = (browser_row *)calloc(count, sizeof(*state->rows));
    slots = (size_t *)malloc(capacity * sizeof(*slots));
    if (!state->rows || !slots) {
        free(slots);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    memset(slots, 0xff, capacity * sizeof(*slots));
    for (i = 0; i < count; ++i) {
        browser_source *source = &state->storage->sources[i];
        const char *relative, *slash;
        size_t length, slot = 0, index;
        int directory, explicit_directory;
        browser_row *row;
        if (strncmp(source->normalized, state->directory, prefix) != 0) continue;
        relative = source->normalized + prefix;
        if (!relative[0]) continue;
        slash = strchr(relative, '/');
        directory = slash != NULL;
        length = slash ? (size_t)(slash - relative) : strlen(relative);
        explicit_directory = directory && !slash[1] && source->entry.is_directory;
        if (directory) {
            slot = name_hash(relative, length) & (capacity - 1);
            while (slots[slot] != SIZE_MAX) {
                row = &state->rows[slots[slot]];
                if (strlen(row->name) == length && memcmp(row->name, relative, length) == 0) break;
                slot = (slot + 1) & (capacity - 1);
            }
            if (slots[slot] != SIZE_MAX) {
                row = &state->rows[slots[slot]];
                if (explicit_directory && row->source_index == SIZE_MAX) {
                    row->entry = source->entry;
                    row->source_index = i;
                }
                continue;
            }
        }
        index = state->row_count++;
        row = &state->rows[index];
        row->name = copy_range(relative, length);
        row->normalized = copy_range(source->normalized, prefix + length + (directory ? 1 : 0));
        if (!row->name || !row->normalized) {
            free(slots);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        if (directory && !explicit_directory) {
            row->source_index = SIZE_MAX;
            row->entry.path = row->normalized;
            row->entry.is_directory = 1;
            row->entry.modified = "";
            row->entry.attributes = "D";
        } else {
            row->entry = source->entry;
            row->source_index = i;
        }
        if (directory) slots[slot] = index;
    }
    free(slots);
    return sort_rows(state);
}

static char *escaped_text(const char *text, size_t *columns)
{
    static const char digits[] = "0123456789ABCDEF";
    const unsigned char *cursor = (const unsigned char *)text;
    size_t length = 0, width = 0, position = 0;
    char *copy;
    while (*cursor) {
        size_t added = (*cursor < 32 || *cursor == 127) ? 4 : 1;
        if (length > SIZE_MAX - added - 1) return NULL;
        length += added;
        if (added == 4) width += 4;
        else if ((*cursor & 0xc0) != 0x80) ++width;
        ++cursor;
    }
    copy = (char *)malloc(length + 1);
    if (!copy) return NULL;
    cursor = (const unsigned char *)text;
    while (*cursor) {
        if (*cursor < 32 || *cursor == 127) {
            copy[position++] = '\\';
            copy[position++] = 'x';
            copy[position++] = digits[*cursor >> 4];
            copy[position++] = digits[*cursor & 15];
        } else copy[position++] = (char)*cursor;
        ++cursor;
    }
    copy[position] = 0;
    *columns = width;
    return copy;
}

static void free_items(char **items, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) free(items[i]);
    free(items);
}

static xxwidgets_status format_rows(browser_state *state, char ***out_items, size_t *out_columns)
{
    char **items = NULL;
    size_t i, max_columns = 0;
    state->name_columns = 32;
    state->column_count = 5 + (state->advanced && state->storage ? state->storage->property_count : 0);
    if ((size_t)state->column >= state->column_count) state->column = XXWIDGETS_ARCHIVE_COLUMN_NAME;
    *out_items = NULL;
    *out_columns = 0;
    if (state->row_count) {
        items = (char **)calloc(state->row_count, sizeof(*items));
        if (!items) return XXWIDGETS_OUT_OF_MEMORY;
    }
    for (i = 0; i < state->row_count; ++i) {
        browser_row *row = &state->rows[i];
        char size[24] = "", packed[24] = "";
        size_t columns;
        size_t column;
        row->cells = (char **)calloc(state->column_count, sizeof(*row->cells));
        if (!row->cells) {
            free_items(items, state->row_count);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        row->cells[0] = escaped_text(row->name, &columns);
        if (!row->cells[0]) {
            free_items(items, state->row_count);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        if (columns > state->name_columns) state->name_columns = columns;
        if (row->entry.flags & XXWIDGETS_ARCHIVE_SIZE_KNOWN) snprintf(size, sizeof(size), "%" PRIu64, row->entry.size);
        if (row->entry.flags & XXWIDGETS_ARCHIVE_PACKED_SIZE_KNOWN) snprintf(packed, sizeof(packed), "%" PRIu64, row->entry.packed_size);
        row->cells[1] = xxwidgets_strdup(size);
        row->cells[2] = xxwidgets_strdup(packed);
        row->cells[3] = escaped_text(row->entry.modified, &columns);
        row->cells[4] = escaped_text(row->entry.attributes, &columns);
        if (!row->cells[1] || !row->cells[2] || !row->cells[3] || !row->cells[4]) {
            free_items(items, state->row_count);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        for (column = 5; column < state->column_count; ++column) {
            row->cells[column] = escaped_text(property_value(&row->entry, state->storage->titles[column - 5]), &columns);
            if (!row->cells[column]) {
                free_items(items, state->row_count);
                return XXWIDGETS_OUT_OF_MEMORY;
            }
        }
    }
    for (i = 0; i < state->row_count; ++i) {
        browser_row *row = &state->rows[i];
        const char *modified = row->cells[3], *attributes = row->cells[4];
        char *line;
        size_t name_columns = 0, modified_columns, attributes_columns, byte_length, pad, pos, j;
        size_t column;
        const unsigned char *cursor = (const unsigned char *)row->cells[0];
        while (*cursor) {
            if ((*cursor++ & 0xc0) != 0x80) ++name_columns;
        }
        modified_columns = attributes_columns = 0;
        cursor = (const unsigned char *)modified;
        while (*cursor) {
            if ((*cursor++ & 0xc0) != 0x80) ++modified_columns;
        }
        cursor = (const unsigned char *)attributes;
        while (*cursor) {
            if ((*cursor++ & 0xc0) != 0x80) ++attributes_columns;
        }
        pad = state->name_columns - name_columns;
        byte_length = strlen(row->cells[0]);
        {
            size_t additions[] = {pad, strlen(modified), strlen(attributes), 52, modified_columns < 19 ? 19 - modified_columns : 0};
            size_t addition;
            for (addition = 0; addition < sizeof(additions) / sizeof(additions[0]); ++addition) {
                if (byte_length > SIZE_MAX - additions[addition] - 1) {
                    free_items(items, state->row_count);
                    return XXWIDGETS_INVALID_ARGUMENT;
                }
                byte_length += additions[addition];
            }
        }
        for (column = 5; column < state->column_count; ++column) {
            size_t addition = strlen(state->storage->titles[column - 5]) + strlen(row->cells[column]) + 5;
            if (byte_length > SIZE_MAX - addition - 1) {
                free_items(items, state->row_count);
                return XXWIDGETS_INVALID_ARGUMENT;
            }
            byte_length += addition;
        }
        line = (char *)malloc(byte_length + 1);
        if (!line) {
            free_items(items, state->row_count);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        memcpy(line, row->entry.is_directory ? "[D] " : "    ", 4);
        pos = 4;
        j = strlen(row->cells[0]);
        memcpy(line + pos, row->cells[0], j);
        pos += j;
        memset(line + pos, ' ', pad);
        pos += pad;
        pos += (size_t)snprintf(line + pos, byte_length + 1 - pos, "  %20s  %20s  ", row->cells[1], row->cells[2]);
        j = strlen(modified);
        memcpy(line + pos, modified, j);
        pos += j;
        if (modified_columns < 19) {
            memset(line + pos, ' ', 19 - modified_columns);
            pos += 19 - modified_columns;
        }
        line[pos++] = ' ';
        line[pos++] = ' ';
        j = strlen(attributes);
        memcpy(line + pos, attributes, j);
        pos += j;
        for (column = 5; column < state->column_count; ++column)
            pos += (size_t)snprintf(line + pos, byte_length + 1 - pos, "  %s: %s", state->storage->titles[column - 5], row->cells[column]);
        line[pos] = 0;
        items[i] = line;
        j = 4 + state->name_columns + 48 + (modified_columns > 19 ? modified_columns : 19) + attributes_columns;
        for (column = 5; column < state->column_count; ++column) {
            const unsigned char *cp = (const unsigned char *)state->storage->titles[column - 5];
            j += 4;
            while (*cp)
                if ((*cp++ & 0xc0) != 0x80) ++j;
            cp = (const unsigned char *)row->cells[column];
            while (*cp)
                if ((*cp++ & 0xc0) != 0x80) ++j;
        }
        if (j > max_columns) max_columns = j;
    }
    *out_items = items;
    *out_columns = max_columns;
    return XXWIDGETS_OK;
}

static xxwidgets_status sync_browser(xxwidgets_widget *widget)
{
    xxwidgets_status status;
    ++widget->app->syncing;
    status = widget->app->ops->sync(widget);
    --widget->app->syncing;
    return status;
}

static xxwidgets_status install_state(xxwidgets_widget *widget, browser_state *state, int value)
{
    browser_state *previous = widget->browser;
    char **items = NULL, **previous_items = widget->items;
    size_t columns = 0, previous_count = widget->item_count, previous_columns = widget->archive_columns;
    uint64_t revision = widget->browser_revision;
    int previous_value = widget->value;
    xxwidgets_status status = state->rows ? XXWIDGETS_OK : build_view(state);
    if (status == XXWIDGETS_OK && previous && state->storage == previous->storage && !strcmp(state->directory, previous->directory)) {
        size_t i, capacity = 16, selected_count = 0;
        size_t *slots;
        for (i = 0; i < previous->row_count; ++i)
            if (previous->rows[i].selected) ++selected_count;
        while (capacity < selected_count * 2) capacity *= 2;
        slots = (size_t *)malloc(capacity * sizeof(*slots));
        if (!slots) {
            state_free(state);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        memset(slots, 0xff, capacity * sizeof(*slots));
        for (i = 0; i < previous->row_count; ++i)
            if (previous->rows[i].selected) {
                browser_row *row = &previous->rows[i];
                size_t slot = (name_hash(row->normalized, strlen(row->normalized)) ^ row->source_index) & (capacity - 1);
                while (slots[slot] != SIZE_MAX) slot = (slot + 1) & (capacity - 1);
                slots[slot] = i;
            }
        for (i = 0; i < state->row_count; ++i) {
            browser_row *row = &state->rows[i];
            size_t slot = (name_hash(row->normalized, strlen(row->normalized)) ^ row->source_index) & (capacity - 1);
            while (slots[slot] != SIZE_MAX) {
                const browser_row *old = &previous->rows[slots[slot]];
                if (old->source_index == row->source_index && !strcmp(old->normalized, row->normalized)) {
                    row->selected = 1;
                    break;
                }
                slot = (slot + 1) & (capacity - 1);
            }
        }
        free(slots);
        state->selection_ready = 1;
    }
    if (status == XXWIDGETS_OK) status = format_rows(state, &items, &columns);
    if (status != XXWIDGETS_OK) {
        state_free(state);
        return status;
    }
    widget->browser = state;
    widget->items = items;
    widget->item_count = state->row_count;
    widget->archive_columns = columns;
    widget->browser_revision = revision + 1;
    widget->value = value == -1 ? -1 : value >= 0 && (size_t)value < state->row_count ? value : state->row_count ? 0 : -1;
    if (!state->selection_ready && widget->value >= 0) state->rows[widget->value].selected = 1;
    state->selection_ready = 1;
    status = sync_browser(widget);
    if (status != XXWIDGETS_OK) {
        widget->browser = previous;
        widget->items = previous_items;
        widget->item_count = previous_count;
        widget->archive_columns = previous_columns;
        widget->browser_revision = revision;
        widget->value = previous_value;
        sync_browser(widget);
        free_items(items, state->row_count);
        state_free(state);
    } else {
        state_free(previous);
        free_items(previous_items, previous_count);
    }
    return status;
}

xxwidgets_status xxwidgets_archivebrowser_set_entries(xxwidgets_widget *widget, const xxwidgets_archive_browser_entry *entries, size_t count)
{
    browser_storage *storage;
    browser_state *state;
    xxwidgets_status status;
    if (!browser_widget(widget)) return XXWIDGETS_INVALID_ARGUMENT;
    status = storage_create(entries, count, &storage);
    if (status != XXWIDGETS_OK) return status;
    state = state_copy(widget->browser);
    if (!state) {
        storage_release(storage);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    storage_release(state->storage);
    state->storage = storage;
    state->directory[0] = 0;
    if ((size_t)state->column >= 5) state->column = XXWIDGETS_ARCHIVE_COLUMN_NAME;
    return install_state(widget, state, 0);
}

xxwidgets_status xxwidgets_archivebrowser_set_advanced(xxwidgets_widget *widget, int enabled)
{
    browser_state *state;
    if (!browser_widget(widget) || (enabled != 0 && enabled != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    state = state_copy(widget->browser);
    if (!state) return XXWIDGETS_OUT_OF_MEMORY;
    state->advanced = enabled;
    if (!enabled && (int)state->column >= 5) state->column = XXWIDGETS_ARCHIVE_COLUMN_NAME;
    /* Sort may change when hiding columns. Reuse sort's selection restoration. */
    {
        size_t source = SIZE_MAX, i;
        xxwidgets_archive_browser_entry selected;
        int selection = -1;
        char *path = NULL;
        xxwidgets_status status = xxwidgets_archivebrowser_get_selection(widget, &source, &selected);
        if (status != XXWIDGETS_OK) {
            state_free(state);
            return status;
        }
        if (selected.path) {
            path = xxwidgets_strdup(selected.path);
            if (!path) {
                state_free(state);
                return XXWIDGETS_OUT_OF_MEMORY;
            }
        }
        status = build_view(state);
        if (status != XXWIDGETS_OK) {
            free(path);
            state_free(state);
            return status;
        }
        for (i = 0; path && i < state->row_count; ++i)
            if (state->rows[i].source_index == source && !strcmp(state->rows[i].entry.path, path)) {
                selection = (int)i;
                break;
            }
        free(path);
        return install_state(widget, state, selection);
    }
}

size_t xxwidgets_archivebrowser_column_count(const xxwidgets_widget *widget)
{
    return browser_widget(widget) && widget->browser ? widget->browser->column_count : 5;
}

const char *xxwidgets_archivebrowser_column_title(const xxwidgets_widget *widget, size_t column)
{
    static const char *titles[] = {"Name", "Size", "Packed Size", "Modified", "Attributes"};
    if (!browser_widget(widget) || column >= xxwidgets_archivebrowser_column_count(widget)) return "";
    if (column < 5) return titles[column];
    return widget->browser->storage->titles[column - 5];
}

xxwidgets_status xxwidgets_archivebrowser_set_archive(xxwidgets_widget *widget, const char *archive)
{
    browser_state *state;
    char *copy;
    if (!browser_widget(widget) || !utf8_valid(archive)) return XXWIDGETS_INVALID_ARGUMENT;
    copy = xxwidgets_strdup(archive);
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    state = state_copy(widget->browser);
    if (!state) {
        free(copy);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    free(state->archive);
    state->archive = copy;
    return install_state(widget, state, widget->value);
}

const char *xxwidgets_archivebrowser_archive(const xxwidgets_widget *widget)
{
    return browser_widget(widget) && widget->browser ? widget->browser->archive : "";
}

static int directory_exists(const browser_state *state, const char *directory)
{
    size_t i, length = strlen(directory);
    if (!length) return 1;
    if (!state || !state->storage) return 0;
    for (i = 0; i < state->storage->count; ++i)
        if (strncmp(state->storage->sources[i].normalized, directory, length) == 0) return 1;
    return 0;
}

/* select: a normalized folder path ("a/b/") whose row becomes the current one,
 * or NULL for the first row. */
static xxwidgets_status change_directory(xxwidgets_widget *widget, const char *directory, const char *select)
{
    char *normalized;
    browser_state *state;
    xxwidgets_status status;
    size_t row;
    int value = 0;
    if (!browser_widget(widget)) return XXWIDGETS_INVALID_ARGUMENT;
    status = normalize_path(directory, 1, &normalized);
    if (status != XXWIDGETS_OK) return status;
    if (!directory_exists(widget->browser, normalized)) {
        free(normalized);
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    state = state_copy(widget->browser);
    if (!state) {
        free(normalized);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    free(state->directory);
    state->directory = normalized;
    if (select) {
        /* Chosen before the first sync, so a backend that starts its keyboard
         * cursor on the current row starts it here too. */
        status = build_view(state);
        if (status != XXWIDGETS_OK) {
            state_free(state);
            return status;
        }
        for (row = 0; row < state->row_count; ++row)
            if (state->rows[row].entry.is_directory && !strcmp(state->rows[row].normalized, select)) {
                value = (int)row;
                break;
            }
    }
    return install_state(widget, state, value);
}

xxwidgets_status xxwidgets_archivebrowser_set_directory(xxwidgets_widget *widget, const char *directory)
{
    return change_directory(widget, directory, NULL);
}

const char *xxwidgets_archivebrowser_directory(const xxwidgets_widget *widget)
{
    return browser_widget(widget) && widget->browser ? widget->browser->directory : "";
}

xxwidgets_status xxwidgets_archivebrowser_up(xxwidgets_widget *widget)
{
    char *parent, *slash, *left;
    xxwidgets_status status;
    if (!browser_widget(widget)) return XXWIDGETS_INVALID_ARGUMENT;
    parent = xxwidgets_strdup(xxwidgets_archivebrowser_directory(widget));
    if (!parent) return XXWIDGETS_OUT_OF_MEMORY;
    if (!parent[0]) {
        free(parent);
        return XXWIDGETS_OK;
    }
    left = xxwidgets_strdup(parent);
    if (!left) {
        free(parent);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    parent[strlen(parent) - 1] = 0;
    slash = strrchr(parent, '/');
    if (slash) slash[1] = 0;
    else parent[0] = 0;
    /* Select the folder just left, as file managers do, so Enter goes back in.
     * The directory is normalized, so it matches the row's normalized path
     * whatever form ("./", backslashes) the archive stores. */
    status = change_directory(widget, parent, left);
    free(left);
    free(parent);
    return status;
}

size_t xxwidgets_archivebrowser_count(const xxwidgets_widget *widget)
{
    return browser_widget(widget) && widget->browser && widget->browser->storage ? widget->browser->storage->count : 0;
}

size_t xxwidgets_archivebrowser_visible_count(const xxwidgets_widget *widget)
{
    return browser_widget(widget) ? widget->item_count : 0;
}

xxwidgets_status xxwidgets_archivebrowser_get_entry(const xxwidgets_widget *widget, size_t row, size_t *source_index, xxwidgets_archive_browser_entry *entry)
{
    if (!browser_widget(widget) || !widget->browser || !source_index || !entry || row >= widget->item_count) return XXWIDGETS_INVALID_ARGUMENT;
    *source_index = widget->browser->rows[row].source_index;
    *entry = widget->browser->rows[row].entry;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_archivebrowser_get_selection(xxwidgets_widget *widget, size_t *source_index, xxwidgets_archive_browser_entry *entry)
{
    int value;
    xxwidgets_status status;
    if (!browser_widget(widget) || !source_index || !entry) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_widget_get_value(widget, &value);
    if (status != XXWIDGETS_OK) return status;
    if (value < 0 || (size_t)value >= widget->item_count) {
        *source_index = SIZE_MAX;
        memset(entry, 0, sizeof(*entry));
        return XXWIDGETS_OK;
    }
    return xxwidgets_archivebrowser_get_entry(widget, (size_t)value, source_index, entry);
}

int xxwidgets_archivebrowser_row_selected(const xxwidgets_widget *widget, size_t row)
{
    return browser_widget(widget) && widget->browser && row < widget->browser->row_count && widget->browser->rows[row].selected;
}

void xxwidgets_archivebrowser_selection_clear(xxwidgets_widget *widget)
{
    size_t i;
    if (!browser_widget(widget) || !widget->browser) return;
    for (i = 0; i < widget->browser->row_count; ++i) widget->browser->rows[i].selected = 0;
}

void xxwidgets_archivebrowser_selection_input(xxwidgets_widget *widget, size_t row, int selected)
{
    if (browser_widget(widget) && widget->browser && row < widget->browser->row_count) widget->browser->rows[row].selected = selected != 0;
}

size_t xxwidgets_archivebrowser_selection_count(const xxwidgets_widget *widget)
{
    size_t i, count = 0;
    if (browser_widget(widget) && widget->browser)
        for (i = 0; i < widget->browser->row_count; ++i) count += widget->browser->rows[i].selected != 0;
    return count;
}

xxwidgets_status xxwidgets_archivebrowser_set_selection(xxwidgets_widget *widget, const size_t *rows, size_t count)
{
    size_t i, n;
    unsigned char *previous;
    int value;
    xxwidgets_status status;
    if (!browser_widget(widget) || (!rows && count)) return XXWIDGETS_INVALID_ARGUMENT;
    n = widget->item_count;
    for (i = 0; i < count; ++i)
        if (rows[i] >= n) return XXWIDGETS_INVALID_ARGUMENT;
    previous = n ? (unsigned char *)malloc(n) : NULL;
    if (n && !previous) return XXWIDGETS_OUT_OF_MEMORY;
    value = widget->value;
    for (i = 0; i < n; ++i) {
        previous[i] = (unsigned char)widget->browser->rows[i].selected;
        widget->browser->rows[i].selected = 0;
    }
    for (i = 0; i < count; ++i) widget->browser->rows[rows[i]].selected = 1;
    if (value < 0 || (size_t)value >= n || !xxwidgets_archivebrowser_row_selected(widget, (size_t)value)) widget->value = count ? (int)rows[0] : -1;
    status = sync_browser(widget);
    if (status != XXWIDGETS_OK) {
        widget->value = value;
        for (i = 0; i < n; ++i) widget->browser->rows[i].selected = previous[i];
        sync_browser(widget);
    }
    free(previous);
    return status;
}

xxwidgets_status xxwidgets_archivebrowser_select_all(xxwidgets_widget *widget)
{
    size_t i, count = xxwidgets_archivebrowser_visible_count(widget);
    size_t *rows = count ? (size_t *)malloc(count * sizeof(*rows)) : NULL;
    xxwidgets_status status;
    if (count && !rows) return XXWIDGETS_OUT_OF_MEMORY;
    for (i = 0; i < count; ++i) rows[i] = i;
    status = xxwidgets_archivebrowser_set_selection(widget, rows, count);
    free(rows);
    return status;
}

xxwidgets_status xxwidgets_archivebrowser_selected_sources(const xxwidgets_widget *widget, size_t *indexes, size_t capacity, size_t *required)
{
    const browser_state *state;
    size_t i, row, found = 0;
    unsigned char *included;
    if (!browser_widget(widget) || !required || (!indexes && capacity)) return XXWIDGETS_INVALID_ARGUMENT;
    *required = 0;
    state = widget->browser;
    if (!state || !state->storage) return XXWIDGETS_OK;
    included = (unsigned char *)calloc(state->storage->count ? state->storage->count : 1, 1);
    if (!included) return XXWIDGETS_OUT_OF_MEMORY;
    for (row = 0; row < state->row_count; ++row)
        if (state->rows[row].selected) {
            const browser_row *selected = &state->rows[row];
            if (selected->source_index != SIZE_MAX) included[selected->source_index] = 1;
            if (selected->entry.is_directory) {
                size_t length = strlen(selected->normalized);
                for (i = 0; i < state->storage->count; ++i)
                    if (!strncmp(state->storage->sources[i].normalized, selected->normalized, length)) included[i] = 1;
            }
        }
    for (i = 0; i < state->storage->count; ++i)
        if (included[i]) {
            if (indexes && found < capacity) indexes[found] = i;
            ++found;
        }
    free(included);
    *required = found;
    return indexes && capacity < found ? XXWIDGETS_BUFFER_TOO_SMALL : XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_archivebrowser_sort(xxwidgets_widget *widget, xxwidgets_archive_column column, int descending)
{
    browser_state *state;
    xxwidgets_status status;
    size_t index = SIZE_MAX, i;
    char *selected_path = NULL;
    int selection = -1;
    xxwidgets_archive_browser_entry entry;
    if (!browser_widget(widget) || column < XXWIDGETS_ARCHIVE_COLUMN_NAME || (size_t)column >= xxwidgets_archivebrowser_column_count(widget) ||
        (descending != 0 && descending != 1))
        return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_archivebrowser_get_selection(widget, &index, &entry);
    if (status != XXWIDGETS_OK) return status;
    if (entry.path) {
        selected_path = xxwidgets_strdup(entry.path);
        if (!selected_path) return XXWIDGETS_OUT_OF_MEMORY;
    }
    state = state_copy(widget->browser);
    if (!state) {
        free(selected_path);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    state->column = column;
    state->descending = descending;
    /* Find the selected member in the sorted view before the backend sync. */
    status = build_view(state);
    if (status != XXWIDGETS_OK) {
        state_free(state);
        free(selected_path);
        return status;
    }
    if (selected_path)
        for (i = 0; i < state->row_count; ++i)
            if (state->rows[i].source_index == index && strcmp(state->rows[i].entry.path, selected_path) == 0) {
                selection = (int)i;
                break;
            }
    free(selected_path);
    return install_state(widget, state, selection);
}

const char *xxwidgets_archivebrowser_name(const xxwidgets_widget *widget, size_t row)
{
    return xxwidgets_archivebrowser_cell(widget, row, XXWIDGETS_ARCHIVE_COLUMN_NAME);
}

const char *xxwidgets_archivebrowser_cell(const xxwidgets_widget *widget, size_t row, xxwidgets_archive_column column)
{
    return browser_widget(widget) && widget->browser && row < widget->item_count && column >= XXWIDGETS_ARCHIVE_COLUMN_NAME &&
                   (size_t)column < widget->browser->column_count
               ? widget->browser->rows[row].cells[column]
               : "";
}

size_t xxwidgets_archivebrowser_name_columns(const xxwidgets_widget *widget)
{
    return browser_widget(widget) && widget->browser ? widget->browser->name_columns : 32;
}

xxwidgets_archive_column xxwidgets_archivebrowser_sort_column(const xxwidgets_widget *widget)
{
    return browser_widget(widget) && widget->browser ? widget->browser->column : XXWIDGETS_ARCHIVE_COLUMN_NAME;
}

int xxwidgets_archivebrowser_sort_descending(const xxwidgets_widget *widget)
{
    return browser_widget(widget) && widget->browser ? widget->browser->descending : 0;
}

xxwidgets_status xxwidgets_archivebrowser_user_activate(xxwidgets_widget *widget, size_t row)
{
    xxwidgets_status status;
    if (!browser_widget(widget) || !widget->browser || row >= widget->item_count) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->browser->rows[row].entry.is_directory) {
        status = xxwidgets_archivebrowser_set_directory(widget, widget->browser->rows[row].normalized);
        if (status == XXWIDGETS_OK) xxwidgets_emit(widget, XXWIDGETS_EVENT_CHANGE, widget->value);
    } else {
        if (widget->value != (int)row) {
            status = xxwidgets_widget_set_value(widget, (int)row);
            if (status != XXWIDGETS_OK) return status;
        }
        xxwidgets_emit(widget, XXWIDGETS_EVENT_ACTIVATE, (int)row);
        status = XXWIDGETS_OK;
    }
    return status;
}

xxwidgets_status xxwidgets_archivebrowser_user_up(xxwidgets_widget *widget)
{
    xxwidgets_status status;
    int changed;
    if (!browser_widget(widget)) return XXWIDGETS_INVALID_ARGUMENT;
    changed = xxwidgets_archivebrowser_directory(widget)[0] != 0;
    status = xxwidgets_archivebrowser_up(widget);
    if (status == XXWIDGETS_OK && changed) xxwidgets_emit(widget, XXWIDGETS_EVENT_CHANGE, widget->value);
    return status;
}

xxwidgets_status xxwidgets_archivebrowser_user_sort(xxwidgets_widget *widget, xxwidgets_archive_column column)
{
    int descending;
    xxwidgets_status status;
    if (!browser_widget(widget)) return XXWIDGETS_INVALID_ARGUMENT;
    descending = xxwidgets_archivebrowser_sort_column(widget) == column ? !xxwidgets_archivebrowser_sort_descending(widget) : 0;
    status = xxwidgets_archivebrowser_sort(widget, column, descending);
    if (status == XXWIDGETS_OK) xxwidgets_emit(widget, XXWIDGETS_EVENT_CHANGE, widget->value);
    return status;
}
