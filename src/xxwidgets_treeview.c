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

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct xxwidgets_tree_projection {
    char **rows;
    size_t *visible, *row_of;
    size_t count, columns;
    size_t nodes; /* Nodes row_of covers: a lazy tree may append nodes before its next projection. */
} xxwidgets_tree_projection;

/* Lazy node flags: PENDING, children not fetched yet; MORE, a "... N more" item. */
enum {
    TREE_PENDING = 1u,
    TREE_MORE = 2u
};

/* One page fetched into a shown tree: content revision from -> from + 1
 * appended nodes [first, ...) and rewrote node changed (SIZE_MAX: none). */
typedef struct xxwidgets_tree_delta {
    uint64_t from;
    size_t first, changed;
} xxwidgets_tree_delta;
#define TREE_DELTA_LOG 1024

typedef struct xxwidgets_tree_more {
    size_t node;   /* The "... N more" child. */
    size_t offset; /* Children of its parent loaded before it. */
    size_t total;  /* Children of its parent, as the source reported. */
} xxwidgets_tree_more;

struct xxwidgets_tree_state {
    xxwidgets_tree_node *nodes;
    char **display;
    xxwidgets_tree_color *colors;
    size_t *first_child, *next_sibling, *depth;
    size_t first_root, count;
    xxwidgets_tree_projection projection;
    /* Optional trailing buttons: labels per column, and a bit set per node. */
    char *mark_labels[XXWIDGETS_TREE_MARKS_MAX];
    size_t mark_count;
    unsigned int *marks;
    /* Lazy source (xxwidgets_treeview_set_source): the per-node arrays grow
     * to capacity; ids, TREE_* flags and last children (for appending) are
     * kept per node, plus the "more" items of partly loaded parents. A plain
     * node's display text may share its original text (no escapes). */
    int lazy, fetching;
    xxwidgets_tree_source source;
    xxwidgets_app *app;
    uint64_t *ids;
    unsigned char *flags;
    size_t *last_child, last_root, capacity;
    xxwidgets_tree_more *more;
    size_t more_count, more_capacity;
    /* The pages fetched since the tree was shown, for
     * xxwidgets_treeview_source_delta (the oldest are dropped), and the
     * changed nodes it returns. */
    xxwidgets_tree_delta *deltas;
    size_t delta_count, delta_capacity;
    size_t *delta_changed;
    size_t delta_changed_capacity;
    /* xxwidgets_treeview_child_at cursor. */
    size_t cursor_parent, cursor_position, cursor_node;
    int cursor_valid;
};

static int tree_widget(const xxwidgets_widget *widget)
{
    return widget && widget->kind == XXWIDGETS_TREEVIEW;
}

/* A source callback runs for this widget's tree: the tree must stay. */
static int tree_busy(const xxwidgets_widget *widget)
{
    return widget->tree_loading || (widget->tree && widget->tree->fetching);
}

static int lazy_tree(const xxwidgets_widget *widget)
{
    return tree_widget(widget) && widget->tree && widget->tree->lazy;
}

static int node_has_children(const struct xxwidgets_tree_state *tree, size_t index)
{
    return tree->first_child[index] != SIZE_MAX || (tree->lazy && (tree->flags[index] & (TREE_PENDING | TREE_MORE)));
}

static void free_pair(char *original, char *display)
{
    if (display != original) free(display);
    free(original);
}

static void free_projection(xxwidgets_tree_projection *projection)
{
    size_t i;
    if (projection->rows)
        for (i = 0; i < projection->count; ++i) free(projection->rows[i]);
    free(projection->rows);
    free(projection->visible);
    free(projection->row_of);
    memset(projection, 0, sizeof(*projection));
}

static void free_tree(struct xxwidgets_tree_state *tree, int free_rows)
{
    size_t i;
    if (!tree) return;
    for (i = 0; i < tree->count; ++i) {
        char *text = tree->nodes ? (char *)tree->nodes[i].text : NULL;
        if (tree->display) free_pair(text, tree->display[i]);
        else free(text);
    }
    free(tree->nodes);
    free(tree->colors);
    free(tree->display);
    free(tree->first_child);
    free(tree->next_sibling);
    free(tree->depth);
    free(tree->ids);
    free(tree->flags);
    free(tree->last_child);
    free(tree->more);
    free(tree->deltas);
    free(tree->delta_changed);
    for (i = 0; i < tree->mark_count; ++i) free(tree->mark_labels[i]);
    free(tree->marks);
    if (free_rows) free_projection(&tree->projection);
    else {
        free(tree->projection.visible);
        free(tree->projection.row_of);
    }
    free(tree);
}

void xxwidgets_treeview_dispose(xxwidgets_widget *widget)
{
    /* The generic destructor owns widget->items, the projected row strings. */
    free_tree(widget->tree, 0);
    widget->tree = NULL;
}

static xxwidgets_status escape_text(const char *text, char **out)
{
    static const char digits[] = "0123456789ABCDEF";
    const unsigned char *s = (const unsigned char *)text;
    size_t length = 0, pos = 0;
    char *copy;
    while (*s) {
        size_t added = *s < 32 || *s == 127 ? 4 : 1;
        if (added >= SIZE_MAX - length) return XXWIDGETS_INVALID_ARGUMENT;
        length += added;
        ++s;
    }
    copy = (char *)malloc(length + 1);
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    s = (const unsigned char *)text;
    while (*s) {
        if (*s < 32 || *s == 127) {
            copy[pos++] = '\\';
            copy[pos++] = 'x';
            copy[pos++] = digits[*s >> 4];
            copy[pos++] = digits[*s & 15];
        } else copy[pos++] = (char)*s;
        ++s;
    }
    copy[pos] = 0;
    *out = copy;
    return XXWIDGETS_OK;
}

/* Length of the valid UTF-8 sequence at s (1..4), or 0. */
static size_t utf8_sequence(const unsigned char *s)
{
    unsigned int c = s[0];
    if (c < 0x80) return 1;
    if (c >= 0xc2 && c <= 0xdf) return (s[1] & 0xc0) == 0x80 ? 2 : 0;
    if (c >= 0xe0 && c <= 0xef) {
        if ((s[1] & 0xc0) != 0x80 || (s[2] & 0xc0) != 0x80) return 0;
        if ((c == 0xe0 && s[1] < 0xa0) || (c == 0xed && s[1] >= 0xa0)) return 0;
        return 3;
    }
    if (c >= 0xf0 && c <= 0xf4) {
        if ((s[1] & 0xc0) != 0x80 || (s[2] & 0xc0) != 0x80 || (s[3] & 0xc0) != 0x80) return 0;
        if ((c == 0xf0 && s[1] < 0x90) || (c == 0xf4 && s[1] >= 0x90)) return 0;
        return 4;
    }
    return 0;
}

/* Source text: the display escapes control characters and invalid bytes as
 * \xNN. The original keeps valid text as given; invalid text is kept escaped.
 * Text needing no escapes is stored once (original == display). */
static xxwidgets_status source_text(const char *text, char **original, char **display)
{
    static const char digits[] = "0123456789ABCDEF";
    const unsigned char *s;
    size_t length = 0, pos = 0, step;
    int plain = 1, valid = 1;
    char *copy;
    if (!text) text = "";
    for (s = (const unsigned char *)text; *s; s += step) {
        step = utf8_sequence(s);
        if (!step || *s < 32 || *s == 127) {
            if (!step) valid = 0;
            if (length > SIZE_MAX - 8) return XXWIDGETS_INVALID_ARGUMENT;
            length += 4;
            step = 1;
            plain = 0;
        } else {
            if (length > SIZE_MAX - 8) return XXWIDGETS_INVALID_ARGUMENT;
            length += step;
        }
    }
    copy = (char *)malloc(length + 1);
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    for (s = (const unsigned char *)text; *s; s += step) {
        step = utf8_sequence(s);
        if (!step || *s < 32 || *s == 127) {
            copy[pos++] = '\\';
            copy[pos++] = 'x';
            copy[pos++] = digits[*s >> 4];
            copy[pos++] = digits[*s & 15];
            step = 1;
        } else {
            memcpy(copy + pos, s, step);
            pos += step;
        }
    }
    copy[pos] = 0;
    if (plain || !valid) {
        *original = *display = copy;
        return XXWIDGETS_OK;
    }
    *original = xxwidgets_strdup(text);
    if (!*original) {
        free(copy);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    *display = copy;
    return XXWIDGETS_OK;
}

static xxwidgets_status build_projection(const struct xxwidgets_tree_state *tree, xxwidgets_tree_projection *out)
{
    size_t index, row = 0, i;
    memset(out, 0, sizeof(*out));
    if (!tree->count) return XXWIDGETS_OK;
    out->rows = (char **)calloc(tree->count, sizeof(*out->rows));
    out->visible = (size_t *)malloc(tree->count * sizeof(*out->visible));
    out->row_of = (size_t *)malloc(tree->count * sizeof(*out->row_of));
    if (!out->rows || !out->visible || !out->row_of) {
        free_projection(out);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    out->nodes = tree->count;
    for (i = 0; i < tree->count; ++i) out->row_of[i] = SIZE_MAX;
    index = tree->first_root;
    while (index != SIZE_MAX) {
        const unsigned char *s = (const unsigned char *)tree->display[index];
        size_t bytes = strlen(tree->display[index]), indent, columns;
        char *line;
        if (tree->depth[index] > (SIZE_MAX - 5) / 2) {
            free_projection(out);
            return XXWIDGETS_INVALID_ARGUMENT;
        }
        indent = tree->depth[index] * 2;
        if (bytes > SIZE_MAX - indent - 5) {
            free_projection(out);
            return XXWIDGETS_INVALID_ARGUMENT;
        }
        line = (char *)malloc(indent + 4 + bytes + 1);
        if (!line) {
            free_projection(out);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        memset(line, ' ', indent);
        memcpy(line + indent, !node_has_children(tree, index) ? "    " : tree->nodes[index].expanded ? "[-] " : "[+] ", 4);
        memcpy(line + indent + 4, tree->display[index], bytes + 1);
        out->rows[row] = line;
        out->visible[row] = index;
        out->row_of[index] = row;
        out->count = ++row;
        columns = indent + 4;
        while (*s)
            if ((*s++ & 0xc0) != 0x80) ++columns;
        if (columns > out->columns) out->columns = columns;
        if (tree->nodes[index].expanded && tree->first_child[index] != SIZE_MAX) {
            index = tree->first_child[index];
        } else {
            while (index != SIZE_MAX && tree->next_sibling[index] == SIZE_MAX) index = tree->nodes[index].parent;
            if (index != SIZE_MAX) index = tree->next_sibling[index];
        }
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status sync_tree(xxwidgets_widget *widget)
{
    xxwidgets_status status;
    ++widget->app->syncing;
    status = widget->app->ops->sync(widget);
    --widget->app->syncing;
    return status;
}

/* Shows a complete new tree state, selecting value; frees the previous one,
 * or on a backend failure restores it and frees tree. */
static xxwidgets_status install_tree(xxwidgets_widget *widget, struct xxwidgets_tree_state *tree, int value)
{
    struct xxwidgets_tree_state *previous = widget->tree;
    int previous_value = widget->value;
    uint64_t previous_revision = widget->tree_revision, previous_content_revision = widget->tree_content_revision;
    xxwidgets_status status;
    widget->tree = tree;
    widget->items = tree->projection.rows;
    widget->item_count = tree->projection.count;
    widget->archive_columns = tree->projection.columns;
    widget->value = value;
    widget->tree_revision = previous_revision + 1;
    widget->tree_content_revision = previous_content_revision + 1;
    status = sync_tree(widget);
    if (status != XXWIDGETS_OK) {
        widget->tree = previous;
        widget->items = previous ? previous->projection.rows : NULL;
        widget->item_count = previous ? previous->projection.count : 0;
        widget->archive_columns = previous ? previous->projection.columns : 0;
        widget->value = previous_value;
        widget->tree_revision = previous_revision;
        widget->tree_content_revision = previous_content_revision;
        sync_tree(widget);
        free_tree(tree, 1);
        return status;
    }
    free_tree(previous, 1);
    return XXWIDGETS_OK;
}

/* Rebuilds the visible rows after loaded nodes changed, without syncing. */
static xxwidgets_status relayout(xxwidgets_widget *widget)
{
    struct xxwidgets_tree_state *tree = widget->tree;
    xxwidgets_tree_projection projection, previous;
    xxwidgets_status status = build_projection(tree, &projection);
    if (status != XXWIDGETS_OK) return status;
    previous = tree->projection;
    tree->projection = projection;
    widget->items = projection.rows;
    widget->item_count = projection.count;
    widget->archive_columns = projection.columns;
    ++widget->tree_revision;
    free_projection(&previous);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_treeview_set_nodes(xxwidgets_widget *widget, const xxwidgets_tree_node *nodes, size_t count)
{
    return xxwidgets_treeview_set_colored_nodes(widget, nodes, NULL, count);
}

xxwidgets_tree_color xxwidgets_treeview_node_color(const xxwidgets_widget *widget, size_t index)
{
    return tree_widget(widget) && widget->tree && index < widget->tree->count && widget->tree->colors
               ? widget->tree->colors[index]
               : (xxwidgets_tree_color){UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
}

xxwidgets_status xxwidgets_treeview_set_colored_nodes(xxwidgets_widget *widget, const xxwidgets_tree_node *nodes, const xxwidgets_tree_color *colors, size_t count)
{
    struct xxwidgets_tree_state *tree;
    size_t *last_child = NULL, last_root = SIZE_MAX, i;
    xxwidgets_status status = XXWIDGETS_OK;
    if (!tree_widget(widget) || (!nodes && count) || count > INT_MAX || count > SIZE_MAX / sizeof(xxwidgets_tree_node) || count > SIZE_MAX / sizeof(size_t))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (tree_busy(widget)) return XXWIDGETS_BUSY;
    for (i = 0; i < count; ++i)
        if ((nodes[i].parent != SIZE_MAX && nodes[i].parent >= i) || !xxwidgets_valid_utf8(nodes[i].text) || (nodes[i].expanded != 0 && nodes[i].expanded != 1))
            return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; colors && i < count; ++i)
        if ((colors[i].foreground != UINT32_MAX && colors[i].foreground > 0xffffffu) || (colors[i].background != UINT32_MAX && colors[i].background > 0xffffffu) ||
            (colors[i].dark_foreground != UINT32_MAX && colors[i].dark_foreground > 0xffffffu) ||
            (colors[i].dark_background != UINT32_MAX && colors[i].dark_background > 0xffffffu))
            return XXWIDGETS_INVALID_ARGUMENT;
    tree = (struct xxwidgets_tree_state *)calloc(1, sizeof(*tree));
    if (!tree) return XXWIDGETS_OUT_OF_MEMORY;
    tree->count = count;
    tree->first_root = SIZE_MAX;
    if (count) {
        tree->nodes = (xxwidgets_tree_node *)calloc(count, sizeof(*tree->nodes));
        tree->display = (char **)calloc(count, sizeof(*tree->display));
        tree->first_child = (size_t *)malloc(count * sizeof(size_t));
        tree->next_sibling = (size_t *)malloc(count * sizeof(size_t));
        tree->depth = (size_t *)malloc(count * sizeof(size_t));
        last_child = (size_t *)malloc(count * sizeof(size_t));
        if (!tree->nodes || !tree->display || !tree->first_child || !tree->next_sibling || !tree->depth || !last_child) {
            status = XXWIDGETS_OUT_OF_MEMORY;
            goto failed;
        }
    }
    if (colors && count) {
        tree->colors = (xxwidgets_tree_color *)malloc(count * sizeof(*colors));
        if (!tree->colors) {
            status = XXWIDGETS_OUT_OF_MEMORY;
            goto failed;
        }
        memcpy(tree->colors, colors, count * sizeof(*colors));
    }
    for (i = 0; i < count; ++i) {
        size_t parent = nodes[i].parent;
        tree->nodes[i] = nodes[i];
        tree->nodes[i].text = xxwidgets_strdup(nodes[i].text);
        if (!tree->nodes[i].text) {
            status = XXWIDGETS_OUT_OF_MEMORY;
            goto failed;
        }
        status = escape_text(nodes[i].text, &tree->display[i]);
        if (status != XXWIDGETS_OK) goto failed;
        tree->first_child[i] = tree->next_sibling[i] = last_child[i] = SIZE_MAX;
        tree->depth[i] = parent == SIZE_MAX ? 0 : tree->depth[parent] + 1;
        if (parent == SIZE_MAX) {
            if (last_root == SIZE_MAX) tree->first_root = i;
            else tree->next_sibling[last_root] = i;
            last_root = i;
        } else {
            if (last_child[parent] == SIZE_MAX) tree->first_child[parent] = i;
            else tree->next_sibling[last_child[parent]] = i;
            last_child[parent] = i;
        }
    }
    free(last_child);
    last_child = NULL;
    status = build_projection(tree, &tree->projection);
    if (status != XXWIDGETS_OK) goto failed;
    return install_tree(widget, tree, count ? (int)tree->first_root : -1);
failed:
    free(last_child);
    free_tree(tree, 1);
    return status;
}

static xxwidgets_status change_tree(xxwidgets_widget *widget, size_t index, int expanded, int selection, int sync)
{
    struct xxwidgets_tree_state *tree = widget->tree;
    xxwidgets_tree_projection projection, previous;
    int *old_expanded = NULL, old_value = widget->value;
    uint64_t old_revision = widget->tree_revision;
    size_t i, ancestor;
    xxwidgets_status status;
    if (!tree) {
        if (selection != -1) return XXWIDGETS_INVALID_ARGUMENT;
        widget->value = -1;
        return sync ? sync_tree(widget) : XXWIDGETS_OK;
    }
    if (tree->count) {
        old_expanded = (int *)malloc(tree->count * sizeof(*old_expanded));
        if (!old_expanded) return XXWIDGETS_OUT_OF_MEMORY;
        for (i = 0; i < tree->count; ++i) old_expanded[i] = tree->nodes[i].expanded;
    }
    if (index != SIZE_MAX) {
        tree->nodes[index].expanded = expanded;
        if (!expanded && widget->value >= 0) {
            ancestor = (size_t)widget->value;
            while (ancestor != SIZE_MAX && ancestor != index) ancestor = tree->nodes[ancestor].parent;
            if (ancestor == index) widget->value = (int)index;
        }
    } else {
        widget->value = selection;
        ancestor = selection < 0 ? SIZE_MAX : tree->nodes[selection].parent;
        while (ancestor != SIZE_MAX) {
            tree->nodes[ancestor].expanded = 1;
            ancestor = tree->nodes[ancestor].parent;
        }
    }
    status = build_projection(tree, &projection);
    if (status != XXWIDGETS_OK) goto rollback_flags;
    previous = tree->projection;
    tree->projection = projection;
    widget->items = projection.rows;
    widget->item_count = projection.count;
    widget->archive_columns = projection.columns;
    widget->tree_revision = old_revision + 1;
    status = sync ? sync_tree(widget) : XXWIDGETS_OK;
    if (status != XXWIDGETS_OK) {
        tree->projection = previous;
        widget->items = previous.rows;
        widget->item_count = previous.count;
        widget->archive_columns = previous.columns;
        widget->tree_revision = old_revision;
        free_projection(&projection);
        for (i = 0; i < tree->count; ++i) tree->nodes[i].expanded = old_expanded[i];
        widget->value = old_value;
        sync_tree(widget);
        free(old_expanded);
        return status;
    }
    free_projection(&previous);
    free(old_expanded);
    return XXWIDGETS_OK;
rollback_flags:
    for (i = 0; i < tree->count; ++i) tree->nodes[i].expanded = old_expanded[i];
    widget->value = old_value;
    free(old_expanded);
    return status;
}

/* Lazy source internals. */

static struct xxwidgets_tree_state *lazy_state(xxwidgets_app *app, const xxwidgets_tree_source *source)
{
    struct xxwidgets_tree_state *tree = (struct xxwidgets_tree_state *)calloc(1, sizeof(*tree));
    if (!tree) return NULL;
    tree->lazy = 1;
    tree->source = *source;
    if (!tree->source.page_size) tree->source.page_size = XXWIDGETS_TREE_PAGE_DEFAULT;
    tree->app = app;
    tree->first_root = tree->last_root = SIZE_MAX;
    return tree;
}

/* Room for extra more nodes in every per-node array. */
static xxwidgets_status tree_reserve(struct xxwidgets_tree_state *tree, size_t extra)
{
    size_t needed, capacity;
    void *grown;
    if (extra > (size_t)INT_MAX - tree->count) return XXWIDGETS_INVALID_ARGUMENT;
    needed = tree->count + extra;
    if (needed <= tree->capacity) return XXWIDGETS_OK;
    capacity = tree->capacity ? tree->capacity : 64;
    while (capacity < needed) capacity = capacity > (size_t)INT_MAX / 2 ? (size_t)INT_MAX : capacity * 2;
    if (capacity > SIZE_MAX / sizeof(xxwidgets_tree_node) || capacity > SIZE_MAX / sizeof(xxwidgets_tree_color)) return XXWIDGETS_OUT_OF_MEMORY;
    /* An array that grew before a later one failed is only larger than needed. */
    grown = realloc(tree->nodes, capacity * sizeof(*tree->nodes));
    if (!grown) return XXWIDGETS_OUT_OF_MEMORY;
    tree->nodes = (xxwidgets_tree_node *)grown;
    grown = realloc(tree->display, capacity * sizeof(*tree->display));
    if (!grown) return XXWIDGETS_OUT_OF_MEMORY;
    tree->display = (char **)grown;
    grown = realloc(tree->colors, capacity * sizeof(*tree->colors));
    if (!grown) return XXWIDGETS_OUT_OF_MEMORY;
    tree->colors = (xxwidgets_tree_color *)grown;
    grown = realloc(tree->first_child, capacity * sizeof(*tree->first_child));
    if (!grown) return XXWIDGETS_OUT_OF_MEMORY;
    tree->first_child = (size_t *)grown;
    grown = realloc(tree->next_sibling, capacity * sizeof(*tree->next_sibling));
    if (!grown) return XXWIDGETS_OUT_OF_MEMORY;
    tree->next_sibling = (size_t *)grown;
    grown = realloc(tree->depth, capacity * sizeof(*tree->depth));
    if (!grown) return XXWIDGETS_OUT_OF_MEMORY;
    tree->depth = (size_t *)grown;
    grown = realloc(tree->ids, capacity * sizeof(*tree->ids));
    if (!grown) return XXWIDGETS_OUT_OF_MEMORY;
    tree->ids = (uint64_t *)grown;
    grown = realloc(tree->flags, capacity * sizeof(*tree->flags));
    if (!grown) return XXWIDGETS_OUT_OF_MEMORY;
    tree->flags = (unsigned char *)grown;
    grown = realloc(tree->last_child, capacity * sizeof(*tree->last_child));
    if (!grown) return XXWIDGETS_OUT_OF_MEMORY;
    tree->last_child = (size_t *)grown;
    tree->capacity = capacity;
    return XXWIDGETS_OK;
}

/* Appends a node as the last child of parent (SIZE_MAX: a root); reserved. */
static void link_node(struct xxwidgets_tree_state *tree, size_t parent, char *text, char *display, const xxwidgets_tree_color *color, uint64_t id, unsigned char flags)
{
    size_t i = tree->count++;
    tree->nodes[i].parent = parent;
    tree->nodes[i].text = text;
    tree->nodes[i].expanded = 0;
    tree->display[i] = display;
    tree->colors[i] = *color;
    tree->ids[i] = id;
    tree->flags[i] = flags;
    tree->first_child[i] = tree->next_sibling[i] = tree->last_child[i] = SIZE_MAX;
    tree->depth[i] = parent == SIZE_MAX ? 0 : tree->depth[parent] + 1;
    if (parent == SIZE_MAX) {
        if (tree->last_root == SIZE_MAX) tree->first_root = i;
        else tree->next_sibling[tree->last_root] = i;
        tree->last_root = i;
    } else {
        if (tree->last_child[parent] == SIZE_MAX) tree->first_child[parent] = i;
        else tree->next_sibling[tree->last_child[parent]] = i;
        tree->last_child[parent] = i;
    }
}

static uint32_t color_part(uint32_t value)
{
    return value > 0xffffffu ? UINT32_MAX : value;
}

static size_t more_slot(const struct xxwidgets_tree_state *tree, size_t node)
{
    size_t i;
    for (i = 0; i < tree->more_count; ++i)
        if (tree->more[i].node == node) return i;
    return SIZE_MAX;
}

static size_t more_of_parent(const struct xxwidgets_tree_state *tree, size_t parent)
{
    size_t i;
    for (i = 0; i < tree->more_count; ++i)
        if (tree->nodes[tree->more[i].node].parent == parent) return i;
    return SIZE_MAX;
}

/* Fetches the next children of parent (SIZE_MAX: the roots): the first page
 * when placeholder is SIZE_MAX, else the page behind that "more" item, which
 * becomes the page's first child. live is the widget showing the tree: its
 * content revision records the change for source_delta (NULL for a state not
 * shown yet). Failures leave the tree unchanged. */
static xxwidgets_status tree_page(struct xxwidgets_tree_state *tree, size_t parent, size_t placeholder, xxwidgets_widget *live)
{
    static const xxwidgets_tree_color plain_color = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
    size_t page = tree->source.page_size, offset = 0, total = 0, n, i, k = 0, slot = SIZE_MAX;
    size_t first = tree->count, changed = SIZE_MAX, extra;
    xxwidgets_tree_item *items;
    char **texts = NULL, **displays = NULL, *more_original = NULL, *more_display = NULL;
    int more_needed;
    xxwidgets_status status = XXWIDGETS_OK;
    if (tree->fetching) return XXWIDGETS_BUSY;
    if (placeholder != SIZE_MAX) {
        slot = more_slot(tree, placeholder);
        if (slot == SIZE_MAX) return XXWIDGETS_INVALID_ARGUMENT;
        offset = tree->more[slot].offset;
    }
    items = (xxwidgets_tree_item *)calloc(page, sizeof(*items));
    if (!items) return XXWIDGETS_OUT_OF_MEMORY;
    /* As in an event callback, the callback cannot destroy widgets (this
     * tree, its window) or the app, nor poll: those return BUSY. */
    tree->fetching = 1;
    ++tree->app->dispatch_depth;
    n = tree->source.children(tree->source.user, parent == SIZE_MAX ? 0 : tree->ids[parent], offset, items, page, &total);
    --tree->app->dispatch_depth;
    tree->fetching = 0;
    if (n == SIZE_MAX) {
        free(items);
        return XXWIDGETS_UNAVAILABLE;
    }
    if (n > page) n = page;
    for (i = 0; i < n; ++i)
        if (!items[i].id) {
            free(items);
            return XXWIDGETS_INVALID_ARGUMENT;
        }
    if (total < offset || total - offset < n) total = offset + n;
    more_needed = n && total - offset > n;
    extra = n + (more_needed ? 1u : 0u) - (placeholder != SIZE_MAX && n ? 1u : 0u);
    if (n) {
        texts = (char **)calloc(n, sizeof(*texts));
        displays = (char **)calloc(n, sizeof(*displays));
        if (!texts || !displays) {
            status = XXWIDGETS_OUT_OF_MEMORY;
            goto done;
        }
        for (i = 0; i < n; ++i) {
            status = source_text(items[i].text, &texts[i], &displays[i]);
            if (status != XXWIDGETS_OK) goto done;
        }
    }
    if (more_needed || (placeholder != SIZE_MAX && !n)) {
        char label[160];
        if (more_needed) snprintf(label, sizeof(label), "... %zu %s", total - offset - n, tree->app ? XXW_TR(tree->app, "more") : "more");
        else snprintf(label, sizeof(label), "...");
        status = source_text(label, &more_original, &more_display);
        if (status != XXWIDGETS_OK) goto done;
    }
    status = tree_reserve(tree, extra);
    if (status != XXWIDGETS_OK) goto done;
    if (live && tree->delta_count == tree->delta_capacity) {
        if (tree->delta_capacity >= TREE_DELTA_LOG) {
            /* Keep the newer half; a backend further behind rebuilds. */
            memmove(tree->deltas, tree->deltas + TREE_DELTA_LOG / 2, (TREE_DELTA_LOG / 2) * sizeof(*tree->deltas));
            tree->delta_count = TREE_DELTA_LOG / 2;
        } else {
            size_t capacity = tree->delta_capacity ? tree->delta_capacity * 2 : 16;
            xxwidgets_tree_delta *grown = (xxwidgets_tree_delta *)realloc(tree->deltas, capacity * sizeof(*grown));
            size_t *changed_list = (size_t *)realloc(tree->delta_changed, capacity * sizeof(*changed_list));
            if (grown) tree->deltas = grown;
            if (changed_list) tree->delta_changed = changed_list;
            if (!grown || !changed_list) {
                status = XXWIDGETS_OUT_OF_MEMORY;
                goto done;
            }
            tree->delta_capacity = capacity;
        }
    }
    if (more_needed && placeholder == SIZE_MAX && tree->more_count == tree->more_capacity) {
        size_t capacity = tree->more_capacity ? tree->more_capacity * 2 : 8;
        xxwidgets_tree_more *grown;
        if (capacity > SIZE_MAX / sizeof(*grown)) {
            status = XXWIDGETS_OUT_OF_MEMORY;
            goto done;
        }
        grown = (xxwidgets_tree_more *)realloc(tree->more, capacity * sizeof(*grown));
        if (!grown) {
            status = XXWIDGETS_OUT_OF_MEMORY;
            goto done;
        }
        tree->more = grown;
        tree->more_capacity = capacity;
    }
    /* Commit: nothing below fails. */
    tree->cursor_valid = 0;
    if (placeholder != SIZE_MAX) {
        free_pair((char *)tree->nodes[placeholder].text, tree->display[placeholder]);
        if (n) {
            tree->nodes[placeholder].text = texts[0];
            tree->display[placeholder] = displays[0];
            texts[0] = displays[0] = NULL;
            tree->colors[placeholder].foreground = color_part(items[0].color.foreground);
            tree->colors[placeholder].background = color_part(items[0].color.background);
            tree->colors[placeholder].dark_foreground = color_part(items[0].color.dark_foreground);
            tree->colors[placeholder].dark_background = color_part(items[0].color.dark_background);
            tree->ids[placeholder] = items[0].id;
            tree->flags[placeholder] = (unsigned char)(items[0].has_children ? TREE_PENDING : 0);
            k = 1;
        } else {
            /* The source had no more after all: an inert end marker. */
            tree->nodes[placeholder].text = more_original;
            tree->display[placeholder] = more_display;
            more_original = more_display = NULL;
            tree->colors[placeholder] = plain_color;
            tree->ids[placeholder] = 0;
            tree->flags[placeholder] = 0;
        }
        changed = placeholder;
    } else if (parent != SIZE_MAX) {
        tree->flags[parent] = (unsigned char)(tree->flags[parent] & ~TREE_PENDING);
        changed = parent;
    }
    for (i = k; i < n; ++i) {
        xxwidgets_tree_color color;
        color.foreground = color_part(items[i].color.foreground);
        color.background = color_part(items[i].color.background);
        color.dark_foreground = color_part(items[i].color.dark_foreground);
        color.dark_background = color_part(items[i].color.dark_background);
        link_node(tree, parent, texts[i], displays[i], &color, items[i].id, (unsigned char)(items[i].has_children ? TREE_PENDING : 0));
        texts[i] = displays[i] = NULL;
    }
    if (more_needed) {
        link_node(tree, parent, more_original, more_display, &plain_color, 0, (unsigned char)TREE_MORE);
        more_original = more_display = NULL;
        /* A replaced item's record moves to the new one. */
        if (placeholder == SIZE_MAX) slot = tree->more_count++;
        tree->more[slot].node = tree->count - 1;
        tree->more[slot].offset = offset + n;
        tree->more[slot].total = total;
    } else if (placeholder != SIZE_MAX) {
        tree->more[slot] = tree->more[--tree->more_count];
    }
    if (live) {
        xxwidgets_tree_delta *delta = &tree->deltas[tree->delta_count++];
        delta->from = live->tree_content_revision;
        delta->first = first;
        delta->changed = changed;
        ++live->tree_content_revision;
    }
done:
    for (i = 0; i < n && texts && displays; ++i) free_pair(texts[i], displays[i]);
    free(texts);
    free(displays);
    free_pair(more_original, more_display);
    free(items);
    return status;
}

static size_t loaded_children(const struct xxwidgets_tree_state *tree, size_t parent)
{
    size_t node = parent == SIZE_MAX ? tree->first_root : tree->first_child[parent], count = 0;
    for (; node != SIZE_MAX; node = tree->next_sibling[node])
        if (!(tree->flags[node] & TREE_MORE)) ++count;
    return count;
}

static size_t find_id(const struct xxwidgets_tree_state *tree, uint64_t id)
{
    size_t i;
    if (!id) return SIZE_MAX;
    for (i = 0; i < tree->count; ++i)
        if (tree->ids[i] == id) return i;
    return SIZE_MAX;
}

static size_t child_with_id(const struct xxwidgets_tree_state *tree, size_t parent, uint64_t id)
{
    size_t node = parent == SIZE_MAX ? tree->first_root : tree->first_child[parent];
    for (; node != SIZE_MAX; node = tree->next_sibling[node])
        if (tree->ids[node] == id) return node;
    return SIZE_MAX;
}

static xxwidgets_status load_more(xxwidgets_widget *widget, size_t index)
{
    xxwidgets_status status = tree_page(widget->tree, widget->tree->nodes[index].parent, index, widget);
    return status == XXWIDGETS_OK ? change_tree(widget, SIZE_MAX, 0, widget->value, 1) : status;
}

xxwidgets_status xxwidgets_treeview_select(xxwidgets_widget *widget, int index)
{
    if (!tree_widget(widget) || index < -1 || (index >= 0 && (size_t)index >= xxwidgets_treeview_count(widget))) return XXWIDGETS_INVALID_ARGUMENT;
    if (tree_busy(widget)) return XXWIDGETS_BUSY;
    return change_tree(widget, SIZE_MAX, 0, index, 1);
}

xxwidgets_status xxwidgets_treeview_set_expanded(xxwidgets_widget *widget, size_t index, int expanded)
{
    if (!tree_widget(widget) || index >= xxwidgets_treeview_count(widget) || (expanded != 0 && expanded != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    if (tree_busy(widget)) return XXWIDGETS_BUSY;
    if (widget->tree->lazy && expanded) {
        struct xxwidgets_tree_state *tree = widget->tree;
        if (tree->flags[index] & TREE_MORE) return load_more(widget, index);
        if (tree->flags[index] & TREE_PENDING) {
            xxwidgets_status status = tree_page(tree, index, SIZE_MAX, widget);
            if (status != XXWIDGETS_OK) return status;
            /* No children after all: shown again without its expander. */
            if (tree->first_child[index] == SIZE_MAX) return change_tree(widget, SIZE_MAX, 0, widget->value, 1);
        }
    }
    if (widget->tree->nodes[index].expanded == expanded) return XXWIDGETS_OK;
    return change_tree(widget, index, expanded, widget->value, 1);
}

xxwidgets_status xxwidgets_treeview_expansion_input(xxwidgets_widget *widget, size_t index, int expanded)
{
    if (!tree_widget(widget) || index >= xxwidgets_treeview_count(widget) || (expanded != 0 && expanded != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    if (tree_busy(widget)) return XXWIDGETS_BUSY;
    if (widget->tree->lazy && expanded && (widget->tree->flags[index] & (TREE_PENDING | TREE_MORE))) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->tree->nodes[index].expanded == expanded) return XXWIDGETS_OK;
    return change_tree(widget, index, expanded, widget->value, 0);
}

xxwidgets_status xxwidgets_treeview_user_expand(xxwidgets_widget *widget, size_t index, int expanded)
{
    int changed;
    xxwidgets_status status;
    if (!tree_widget(widget) || index >= xxwidgets_treeview_count(widget)) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->tree->lazy && (widget->tree->flags[index] & TREE_MORE)) {
        size_t parent = widget->tree->nodes[index].parent;
        status = xxwidgets_treeview_set_expanded(widget, index, 1);
        if (status == XXWIDGETS_OK) xxwidgets_emit(widget, XXWIDGETS_EVENT_CHANGE, parent == SIZE_MAX ? -1 : (int)parent);
        return status;
    }
    changed = widget->tree->nodes[index].expanded != expanded;
    status = xxwidgets_treeview_set_expanded(widget, index, expanded);
    if (status == XXWIDGETS_OK && changed && widget->tree->nodes[index].expanded == expanded) xxwidgets_emit(widget, XXWIDGETS_EVENT_CHANGE, (int)index);
    return status;
}

void xxwidgets_treeview_user_activate(xxwidgets_widget *widget, size_t index)
{
    if (lazy_tree(widget) && index < widget->tree->count && (widget->tree->flags[index] & TREE_MORE)) (void)xxwidgets_treeview_user_expand(widget, index, 1);
    else xxwidgets_emit(widget, XXWIDGETS_EVENT_ACTIVATE, (int)index);
}

xxwidgets_status xxwidgets_treeview_set_source(xxwidgets_widget *widget, const xxwidgets_tree_source *source)
{
    struct xxwidgets_tree_state *tree;
    xxwidgets_status status;
    if (!tree_widget(widget)) return XXWIDGETS_INVALID_ARGUMENT;
    if (tree_busy(widget)) return XXWIDGETS_BUSY;
    if (!source) return xxwidgets_treeview_set_colored_nodes(widget, NULL, NULL, 0);
    if (!source->children || source->page_size > XXWIDGETS_TREE_PAGE_MAX) return XXWIDGETS_INVALID_ARGUMENT;
    tree = lazy_state(widget->app, source);
    if (!tree) return XXWIDGETS_OUT_OF_MEMORY;
    widget->tree_loading = 1; /* The roots' callback must not replace the tree. */
    status = tree_page(tree, SIZE_MAX, SIZE_MAX, NULL);
    widget->tree_loading = 0;
    if (status == XXWIDGETS_OK) status = build_projection(tree, &tree->projection);
    if (status != XXWIDGETS_OK) {
        free_tree(tree, 1);
        return status;
    }
    return install_tree(widget, tree, tree->count ? (int)tree->first_root : -1);
}

int xxwidgets_treeview_has_source(const xxwidgets_widget *widget)
{
    return lazy_tree(widget);
}

typedef struct tree_keep {
    uint64_t id;
    size_t loaded; /* Selection chain: children its parent had loaded. */
} tree_keep;

static int id_compare(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

/* Further pages of parent until a child has id, at most until bound children
 * are loaded (as far as the old tree reached). Only new nodes are searched. */
static xxwidgets_status tree_load_until(struct xxwidgets_tree_state *tree, size_t parent, uint64_t id, size_t bound)
{
    size_t slot, i, first, converted;
    xxwidgets_status status;
    if (!id || child_with_id(tree, parent, id) != SIZE_MAX) return XXWIDGETS_OK;
    while ((slot = more_of_parent(tree, parent)) != SIZE_MAX && tree->more[slot].offset < bound) {
        converted = tree->more[slot].node;
        first = tree->count;
        status = tree_page(tree, parent, converted, NULL);
        if (status != XXWIDGETS_OK) return status;
        if (tree->ids[converted] == id) return XXWIDGETS_OK;
        for (i = first; i < tree->count; ++i)
            if (tree->nodes[i].parent == parent && tree->ids[i] == id) return XXWIDGETS_OK;
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_treeview_source_refresh(xxwidgets_widget *widget)
{
    struct xxwidgets_tree_state *old, *tree = NULL;
    uint64_t *keep = NULL;
    tree_keep *chain = NULL;
    size_t keep_count = 0, chain_count = 0, i, k, node, selected = SIZE_MAX;
    xxwidgets_status status = XXWIDGETS_OK;
    if (!lazy_tree(widget)) return XXWIDGETS_INVALID_ARGUMENT;
    old = widget->tree;
    if (tree_busy(widget)) return XXWIDGETS_BUSY;
    /* What to restore: the expanded ids, and the selected id followed by its
     * ancestors' ids, each with how many children its parent had loaded. */
    for (i = 0; i < old->count; ++i)
        if (old->nodes[i].expanded) ++keep_count;
    if (keep_count) {
        keep = (uint64_t *)malloc(keep_count * sizeof(*keep));
        if (!keep) return XXWIDGETS_OUT_OF_MEMORY;
    }
    keep_count = 0;
    for (i = 0; i < old->count; ++i)
        if (old->nodes[i].expanded && old->ids[i]) keep[keep_count++] = old->ids[i];
    if (keep_count > 1) qsort(keep, keep_count, sizeof(*keep), id_compare);
    if (widget->value >= 0 && (size_t)widget->value < old->count) {
        for (node = (size_t)widget->value; node != SIZE_MAX; node = old->nodes[node].parent) ++chain_count;
        chain = (tree_keep *)malloc(chain_count * sizeof(*chain));
        if (!chain) {
            status = XXWIDGETS_OUT_OF_MEMORY;
            goto done;
        }
        chain_count = 0;
        for (node = (size_t)widget->value; node != SIZE_MAX; node = old->nodes[node].parent) {
            chain[chain_count].id = old->ids[node];
            chain[chain_count].loaded = loaded_children(old, old->nodes[node].parent);
            ++chain_count;
        }
    }
    tree = lazy_state(widget->app, &old->source);
    if (!tree) {
        status = XXWIDGETS_OUT_OF_MEMORY;
        goto done;
    }
    /* The first page of the roots and of every node expanded before; more
     * pages only on the way to the selected node, so a refresh costs about
     * one page per open node, however far pages had been loaded. */
    widget->tree_loading = 1; /* The callbacks must not replace the tree meanwhile. */
    status = tree_page(tree, SIZE_MAX, SIZE_MAX, NULL);
    if (status == XXWIDGETS_OK && chain_count) status = tree_load_until(tree, SIZE_MAX, chain[chain_count - 1].id, chain[chain_count - 1].loaded);
    /* Nodes appended while loading are visited too, so expanded descendants
     * are restored below their restored ancestors. */
    for (i = 0; status == XXWIDGETS_OK && keep_count && i < tree->count; ++i) {
        if (!(tree->flags[i] & TREE_PENDING) || !bsearch(&tree->ids[i], keep, keep_count, sizeof(*keep), id_compare)) continue;
        status = tree_page(tree, i, SIZE_MAX, NULL);
        for (k = 1; status == XXWIDGETS_OK && k < chain_count; ++k)
            if (chain[k].id == tree->ids[i]) status = tree_load_until(tree, i, chain[k - 1].id, chain[k - 1].loaded);
        if (status == XXWIDGETS_OK && tree->first_child[i] != SIZE_MAX) tree->nodes[i].expanded = 1;
    }
    widget->tree_loading = 0;
    if (status != XXWIDGETS_OK) goto done;
    for (i = 0; selected == SIZE_MAX && i < chain_count; ++i) selected = find_id(tree, chain[i].id);
    if (selected == SIZE_MAX && tree->count) selected = tree->first_root;
    for (node = selected == SIZE_MAX ? SIZE_MAX : tree->nodes[selected].parent; node != SIZE_MAX; node = tree->nodes[node].parent) tree->nodes[node].expanded = 1;
    status = build_projection(tree, &tree->projection);
    if (status != XXWIDGETS_OK) goto done;
    status = install_tree(widget, tree, selected == SIZE_MAX ? -1 : (int)selected);
    tree = NULL; /* Installed, or freed by install_tree. */
done:
    free_tree(tree, 1);
    free(keep);
    free(chain);
    return status;
}

xxwidgets_status xxwidgets_treeview_source_selection(xxwidgets_widget *widget, uint64_t *id)
{
    int value;
    xxwidgets_status status;
    if (id) *id = 0;
    if (!lazy_tree(widget) || !id) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_widget_get_value(widget, &value);
    if (status != XXWIDGETS_OK) return status;
    if (value >= 0 && (size_t)value < widget->tree->count) *id = widget->tree->ids[value];
    return XXWIDGETS_OK;
}

uint64_t xxwidgets_treeview_source_id(const xxwidgets_widget *widget, size_t index)
{
    return lazy_tree(widget) && index < widget->tree->count ? widget->tree->ids[index] : 0;
}

size_t xxwidgets_treeview_source_find(const xxwidgets_widget *widget, uint64_t id)
{
    return lazy_tree(widget) ? find_id(widget->tree, id) : SIZE_MAX;
}

size_t xxwidgets_treeview_source_more_count(const xxwidgets_widget *widget, size_t index)
{
    size_t slot;
    if (!lazy_tree(widget) || index >= widget->tree->count || !(widget->tree->flags[index] & TREE_MORE)) return 0;
    slot = more_slot(widget->tree, index);
    return slot == SIZE_MAX ? 0 : widget->tree->more[slot].total - widget->tree->more[slot].offset;
}

xxwidgets_status xxwidgets_treeview_source_select(xxwidgets_widget *widget, const uint64_t *path, size_t count)
{
    struct xxwidgets_tree_state *tree;
    size_t level, parent = SIZE_MAX, node = SIZE_MAX, slot;
    uint64_t before;
    xxwidgets_status status = XXWIDGETS_OK, shown;
    if (!lazy_tree(widget) || !path || !count) return XXWIDGETS_INVALID_ARGUMENT;
    tree = widget->tree;
    if (tree_busy(widget)) return XXWIDGETS_BUSY;
    for (level = 0; level < count; ++level)
        if (!path[level]) return XXWIDGETS_INVALID_ARGUMENT;
    before = widget->tree_content_revision;
    for (level = 0; level < count && status == XXWIDGETS_OK; ++level) {
        uint64_t id = path[level];
        if (parent != SIZE_MAX && (tree->flags[parent] & TREE_PENDING)) status = tree_page(tree, parent, SIZE_MAX, widget);
        node = status == XXWIDGETS_OK ? child_with_id(tree, parent, id) : SIZE_MAX;
        /* Further pages: only the nodes each one adds are searched. */
        while (status == XXWIDGETS_OK && node == SIZE_MAX && (slot = more_of_parent(tree, parent)) != SIZE_MAX) {
            size_t converted = tree->more[slot].node, first = tree->count, i;
            status = tree_page(tree, parent, converted, widget);
            if (status != XXWIDGETS_OK) break;
            if (tree->ids[converted] == id) node = converted;
            for (i = first; node == SIZE_MAX && i < tree->count; ++i)
                if (tree->nodes[i].parent == parent && tree->ids[i] == id) node = i;
        }
        if (status == XXWIDGETS_OK && node == SIZE_MAX) status = XXWIDGETS_UNAVAILABLE;
        parent = node;
    }
    if (status != XXWIDGETS_OK && widget->tree_content_revision == before) return status;
    /* Show the pages fetched, and the target with its ancestors expanded. */
    shown = change_tree(widget, SIZE_MAX, 0, status == XXWIDGETS_OK ? (int)node : widget->value, 1);
    return status == XXWIDGETS_OK ? shown : status;
}

xxwidgets_status xxwidgets_treeview_source_fetch(xxwidgets_widget *widget, size_t index)
{
    xxwidgets_status status;
    if (!tree_widget(widget) || index >= xxwidgets_treeview_count(widget)) return XXWIDGETS_INVALID_ARGUMENT;
    if (tree_busy(widget)) return XXWIDGETS_BUSY;
    if (!widget->tree->lazy || !(widget->tree->flags[index] & TREE_PENDING)) return XXWIDGETS_OK;
    status = tree_page(widget->tree, index, SIZE_MAX, widget);
    return status == XXWIDGETS_OK ? relayout(widget) : status;
}

int xxwidgets_treeview_source_delta(const xxwidgets_widget *widget, uint64_t applied, size_t *first, const size_t **changed, size_t *changed_count)
{
    struct xxwidgets_tree_state *tree = lazy_tree(widget) ? widget->tree : NULL;
    size_t start, k, count = 0;
    if (!tree || !first || !changed || !changed_count || !tree->delta_count || applied >= widget->tree_content_revision) return 0;
    /* The pages since applied, one per revision up to the current one. */
    if (widget->tree_content_revision - applied > tree->delta_count) return 0;
    start = tree->delta_count - (size_t)(widget->tree_content_revision - applied);
    for (k = start; k < tree->delta_count; ++k)
        if (tree->deltas[k].from != applied + (k - start)) return 0;
    *first = tree->deltas[start].first;
    /* Nodes appended by the later pages are inserted as they are now. */
    for (k = start; k < tree->delta_count; ++k)
        if (tree->deltas[k].changed != SIZE_MAX && tree->deltas[k].changed < *first) tree->delta_changed[count++] = tree->deltas[k].changed;
    *changed = tree->delta_changed;
    *changed_count = count;
    return 1;
}

int xxwidgets_treeview_pending(const xxwidgets_widget *widget, size_t index)
{
    return lazy_tree(widget) && index < widget->tree->count && (widget->tree->flags[index] & (TREE_PENDING | TREE_MORE));
}

int xxwidgets_treeview_is_more(const xxwidgets_widget *widget, size_t index)
{
    return lazy_tree(widget) && index < widget->tree->count && (widget->tree->flags[index] & TREE_MORE);
}

int xxwidgets_treeview_children_loaded(const xxwidgets_widget *widget, size_t index)
{
    return tree_widget(widget) && index < xxwidgets_treeview_count(widget) && widget->tree->first_child[index] != SIZE_MAX;
}

size_t xxwidgets_treeview_child_count(xxwidgets_widget *widget, size_t parent)
{
    struct xxwidgets_tree_state *tree = tree_widget(widget) ? widget->tree : NULL;
    size_t node, count = 0;
    if (!tree || (parent != SIZE_MAX && parent >= tree->count)) return 0;
    for (node = parent == SIZE_MAX ? tree->first_root : tree->first_child[parent]; node != SIZE_MAX; node = tree->next_sibling[node]) ++count;
    return count;
}

size_t xxwidgets_treeview_child_at(xxwidgets_widget *widget, size_t parent, size_t position)
{
    struct xxwidgets_tree_state *tree = tree_widget(widget) ? widget->tree : NULL;
    size_t node, at = 0;
    if (!tree || (parent != SIZE_MAX && parent >= tree->count)) return SIZE_MAX;
    if (tree->cursor_valid && tree->cursor_parent == parent && tree->cursor_position <= position) {
        node = tree->cursor_node;
        at = tree->cursor_position;
    } else node = parent == SIZE_MAX ? tree->first_root : tree->first_child[parent];
    while (node != SIZE_MAX && at < position) {
        node = tree->next_sibling[node];
        ++at;
    }
    if (node == SIZE_MAX) return SIZE_MAX;
    tree->cursor_valid = 1;
    tree->cursor_parent = parent;
    tree->cursor_position = position;
    tree->cursor_node = node;
    return node;
}

xxwidgets_status xxwidgets_treeview_set_marks(xxwidgets_widget *widget, const char *const *labels, size_t label_count, const unsigned int *masks, size_t count)
{
    struct xxwidgets_tree_state *tree;
    char *copies[XXWIDGETS_TREE_MARKS_MAX] = {0};
    unsigned int *bits = NULL, *previous_bits;
    char *previous_labels[XXWIDGETS_TREE_MARKS_MAX];
    size_t previous_count, i;
    xxwidgets_status status;
    if (!tree_widget(widget) || !widget->tree) return XXWIDGETS_INVALID_ARGUMENT;
    if (tree_busy(widget)) return XXWIDGETS_BUSY;
    tree = widget->tree;
    /* Lazy trees grow; a mask per node cannot follow them. */
    if (tree->lazy) return labels ? XXWIDGETS_UNAVAILABLE : XXWIDGETS_OK;
    if (labels) {
        if (!label_count || label_count > XXWIDGETS_TREE_MARKS_MAX || count != tree->count || (!masks && count)) return XXWIDGETS_INVALID_ARGUMENT;
        for (i = 0; i < label_count; ++i) {
            const unsigned char *s = (const unsigned char *)labels[i];
            if (!s || !*s || !xxwidgets_valid_utf8(labels[i]) || strlen(labels[i]) > 16) return XXWIDGETS_INVALID_ARGUMENT;
            for (; *s; ++s)
                if (*s < 32 || *s == 127) return XXWIDGETS_INVALID_ARGUMENT;
        }
        for (i = 0; i < count; ++i)
            if (masks[i] >> label_count) return XXWIDGETS_INVALID_ARGUMENT;
        if (count) {
            bits = (unsigned int *)malloc(count * sizeof(*bits));
            if (!bits) return XXWIDGETS_OUT_OF_MEMORY;
            memcpy(bits, masks, count * sizeof(*bits));
        }
        for (i = 0; i < label_count; ++i) {
            copies[i] = xxwidgets_strdup(labels[i]);
            if (!copies[i]) {
                while (i) free(copies[--i]);
                free(bits);
                return XXWIDGETS_OUT_OF_MEMORY;
            }
        }
    } else label_count = 0;
    previous_count = tree->mark_count;
    previous_bits = tree->marks;
    memcpy(previous_labels, tree->mark_labels, sizeof(previous_labels));
    memcpy(tree->mark_labels, copies, sizeof(copies));
    tree->mark_count = label_count;
    tree->marks = bits;
    status = sync_tree(widget);
    if (status != XXWIDGETS_OK) {
        memcpy(tree->mark_labels, previous_labels, sizeof(previous_labels));
        tree->mark_count = previous_count;
        tree->marks = previous_bits;
        sync_tree(widget);
        for (i = 0; i < label_count; ++i) free(copies[i]);
        free(bits);
        return status;
    }
    for (i = 0; i < previous_count; ++i) free(previous_labels[i]);
    free(previous_bits);
    return XXWIDGETS_OK;
}

unsigned int xxwidgets_treeview_node_marks(const xxwidgets_widget *widget, size_t index)
{
    return tree_widget(widget) && widget->tree && widget->tree->marks && index < widget->tree->count ? widget->tree->marks[index] : 0;
}

size_t xxwidgets_treeview_mark_count(const xxwidgets_widget *widget)
{
    return tree_widget(widget) && widget->tree ? widget->tree->mark_count : 0;
}

const char *xxwidgets_treeview_mark_label(const xxwidgets_widget *widget, size_t mark)
{
    return mark < xxwidgets_treeview_mark_count(widget) ? widget->tree->mark_labels[mark] : NULL;
}

void xxwidgets_treeview_mark_click(xxwidgets_widget *widget, size_t index, size_t mark)
{
    if (mark < XXWIDGETS_TREE_MARKS_MAX && (xxwidgets_treeview_node_marks(widget, index) >> mark & 1u))
        xxwidgets_emit_point(widget, XXWIDGETS_EVENT_CLICK, (int)index, (int)mark, 0);
}

int xxwidgets_treeview_mark_key(xxwidgets_widget *widget, unsigned int key)
{
    size_t mark, count = xxwidgets_treeview_mark_count(widget);
    if (!count || widget->value < 0 || key < 32 || key >= 127) return 0;
    if (key >= 'A' && key <= 'Z') key += 'a' - 'A';
    for (mark = 0; mark < count; ++mark) {
        unsigned int first = (unsigned char)widget->tree->mark_labels[mark][0];
        if (first >= 'A' && first <= 'Z') first += 'a' - 'A';
        if (first == key && (xxwidgets_treeview_node_marks(widget, (size_t)widget->value) >> mark & 1u)) {
            xxwidgets_treeview_mark_click(widget, (size_t)widget->value, mark);
            return 1;
        }
    }
    return 0;
}

xxwidgets_status xxwidgets_treeview_clear(xxwidgets_widget *widget)
{
    return xxwidgets_treeview_set_nodes(widget, NULL, 0);
}
size_t xxwidgets_treeview_count(const xxwidgets_widget *widget)
{
    return tree_widget(widget) && widget->tree ? widget->tree->count : 0;
}
size_t xxwidgets_treeview_visible_count(const xxwidgets_widget *widget)
{
    return tree_widget(widget) ? widget->item_count : 0;
}

xxwidgets_status xxwidgets_treeview_get_node(const xxwidgets_widget *widget, size_t index, xxwidgets_tree_node *node)
{
    if (!tree_widget(widget) || !node || index >= xxwidgets_treeview_count(widget)) return XXWIDGETS_INVALID_ARGUMENT;
    *node = widget->tree->nodes[index];
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_treeview_get_selection(xxwidgets_widget *widget, size_t *index, xxwidgets_tree_node *node)
{
    int value;
    xxwidgets_status status;
    if (!tree_widget(widget) || !index || !node) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_widget_get_value(widget, &value);
    if (status != XXWIDGETS_OK) return status;
    if (value < 0 || (size_t)value >= xxwidgets_treeview_count(widget)) {
        *index = SIZE_MAX;
        memset(node, 0, sizeof(*node));
    } else {
        *index = (size_t)value;
        *node = widget->tree->nodes[value];
    }
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_treeview_get_expanded(const xxwidgets_widget *widget, size_t index, int *expanded)
{
    if (!tree_widget(widget) || !expanded || index >= xxwidgets_treeview_count(widget)) return XXWIDGETS_INVALID_ARGUMENT;
    *expanded = widget->tree->nodes[index].expanded;
    return XXWIDGETS_OK;
}

size_t xxwidgets_treeview_node_at_row(const xxwidgets_widget *widget, size_t row)
{
    return tree_widget(widget) && row < widget->item_count ? widget->tree->projection.visible[row] : SIZE_MAX;
}
size_t xxwidgets_treeview_row_of_node(const xxwidgets_widget *widget, size_t index)
{
    return tree_widget(widget) && index < xxwidgets_treeview_count(widget) && index < widget->tree->projection.nodes ? widget->tree->projection.row_of[index] : SIZE_MAX;
}
size_t xxwidgets_treeview_depth(const xxwidgets_widget *widget, size_t index)
{
    return tree_widget(widget) && index < xxwidgets_treeview_count(widget) ? widget->tree->depth[index] : 0;
}
int xxwidgets_treeview_has_children(const xxwidgets_widget *widget, size_t index)
{
    return tree_widget(widget) && index < xxwidgets_treeview_count(widget) && node_has_children(widget->tree, index);
}
const char *xxwidgets_treeview_display_text(const xxwidgets_widget *widget, size_t index)
{
    return tree_widget(widget) && index < xxwidgets_treeview_count(widget) ? widget->tree->display[index] : "";
}
