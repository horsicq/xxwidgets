#include "xxwidgets_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct xxwidgets_tree_projection {
    char **rows;
    size_t *visible, *row_of;
    size_t count, columns;
} xxwidgets_tree_projection;

struct xxwidgets_tree_state {
    xxwidgets_tree_node *nodes;
    char **display;
    size_t *first_child, *next_sibling, *depth;
    size_t first_root, count;
    xxwidgets_tree_projection projection;
};

static int tree_widget(const xxwidgets_widget *widget)
{
    return widget && widget->kind == XXWIDGETS_TREEVIEW;
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
        if (tree->nodes) free((void *)tree->nodes[i].text);
        if (tree->display) free(tree->display[i]);
    }
    free(tree->nodes);
    free(tree->display);
    free(tree->first_child);
    free(tree->next_sibling);
    free(tree->depth);
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
        memcpy(line + indent, tree->first_child[index] == SIZE_MAX ? "    " : tree->nodes[index].expanded ? "[-] " : "[+] ", 4);
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

xxwidgets_status xxwidgets_treeview_set_nodes(xxwidgets_widget *widget, const xxwidgets_tree_node *nodes, size_t count)
{
    struct xxwidgets_tree_state *tree, *previous;
    size_t *last_child = NULL, last_root = SIZE_MAX, i;
    int previous_value;
    uint64_t previous_revision, previous_content_revision;
    xxwidgets_status status = XXWIDGETS_OK;
    if (!tree_widget(widget) || (!nodes && count) || count > INT_MAX || count > SIZE_MAX / sizeof(xxwidgets_tree_node) || count > SIZE_MAX / sizeof(size_t))
        return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < count; ++i)
        if ((nodes[i].parent != SIZE_MAX && nodes[i].parent >= i) || !xxwidgets_valid_utf8(nodes[i].text) || (nodes[i].expanded != 0 && nodes[i].expanded != 1))
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
    previous = widget->tree;
    previous_value = widget->value;
    previous_revision = widget->tree_revision;
    previous_content_revision = widget->tree_content_revision;
    widget->tree = tree;
    widget->items = tree->projection.rows;
    widget->item_count = tree->projection.count;
    widget->archive_columns = tree->projection.columns;
    widget->value = count ? (int)tree->first_root : -1;
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
        goto failed;
    }
    free_tree(previous, 1);
    return XXWIDGETS_OK;
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

xxwidgets_status xxwidgets_treeview_select(xxwidgets_widget *widget, int index)
{
    if (!tree_widget(widget) || index < -1 || (index >= 0 && (size_t)index >= xxwidgets_treeview_count(widget))) return XXWIDGETS_INVALID_ARGUMENT;
    return change_tree(widget, SIZE_MAX, 0, index, 1);
}

xxwidgets_status xxwidgets_treeview_set_expanded(xxwidgets_widget *widget, size_t index, int expanded)
{
    if (!tree_widget(widget) || index >= xxwidgets_treeview_count(widget) || (expanded != 0 && expanded != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->tree->nodes[index].expanded == expanded) return XXWIDGETS_OK;
    return change_tree(widget, index, expanded, widget->value, 1);
}

xxwidgets_status xxwidgets_treeview_expansion_input(xxwidgets_widget *widget, size_t index, int expanded)
{
    if (!tree_widget(widget) || index >= xxwidgets_treeview_count(widget) || (expanded != 0 && expanded != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->tree->nodes[index].expanded == expanded) return XXWIDGETS_OK;
    return change_tree(widget, index, expanded, widget->value, 0);
}

xxwidgets_status xxwidgets_treeview_user_expand(xxwidgets_widget *widget, size_t index, int expanded)
{
    int changed;
    xxwidgets_status status;
    if (!tree_widget(widget) || index >= xxwidgets_treeview_count(widget)) return XXWIDGETS_INVALID_ARGUMENT;
    changed = widget->tree->nodes[index].expanded != expanded;
    status = xxwidgets_treeview_set_expanded(widget, index, expanded);
    if (status == XXWIDGETS_OK && changed) xxwidgets_emit(widget, XXWIDGETS_EVENT_CHANGE, (int)index);
    return status;
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
    return tree_widget(widget) && index < xxwidgets_treeview_count(widget) ? widget->tree->projection.row_of[index] : SIZE_MAX;
}
size_t xxwidgets_treeview_depth(const xxwidgets_widget *widget, size_t index)
{
    return tree_widget(widget) && index < xxwidgets_treeview_count(widget) ? widget->tree->depth[index] : 0;
}
int xxwidgets_treeview_has_children(const xxwidgets_widget *widget, size_t index)
{
    return tree_widget(widget) && index < xxwidgets_treeview_count(widget) && widget->tree->first_child[index] != SIZE_MAX;
}
const char *xxwidgets_treeview_display_text(const xxwidgets_widget *widget, size_t index)
{
    return tree_widget(widget) && index < xxwidgets_treeview_count(widget) ? widget->tree->display[index] : "";
}
