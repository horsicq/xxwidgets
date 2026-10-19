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
#include <stdlib.h>

static xxwidgets_status sync_listbox(xxwidgets_widget *widget)
{
    xxwidgets_status status;
    ++widget->app->syncing;
    status = widget->app->ops->sync(widget);
    --widget->app->syncing;
    return status;
}

xxwidgets_status xxwidgets_listbox_add(xxwidgets_widget *widget, const char *text)
{
    char *copy, **items;
    xxwidgets_status status;
    if (!widget || widget->kind != XXWIDGETS_LISTBOX || !xxwidgets_valid_utf8(text) || widget->item_count >= INT_MAX || widget->item_count >= (size_t)-1 / sizeof(char *) - 1)
        return XXWIDGETS_INVALID_ARGUMENT;
    copy = xxwidgets_strdup(text);
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    items = (char **)realloc(widget->items, (widget->item_count + 1) * sizeof(*items));
    if (!items) {
        free(copy);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    widget->items = items;
    items[widget->item_count++] = copy;
    status = sync_listbox(widget);
    if (status != XXWIDGETS_OK) {
        --widget->item_count;
        sync_listbox(widget);
        free(copy);
    }
    return status;
}

xxwidgets_status xxwidgets_listbox_clear(xxwidgets_widget *widget)
{
    char **items;
    size_t count, i;
    int value;
    xxwidgets_status status;
    if (!widget || widget->kind != XXWIDGETS_LISTBOX) return XXWIDGETS_INVALID_ARGUMENT;
    items = widget->items;
    count = widget->item_count;
    value = widget->value;
    widget->items = NULL;
    widget->item_count = 0;
    widget->value = -1;
    status = sync_listbox(widget);
    if (status != XXWIDGETS_OK) {
        widget->items = items;
        widget->item_count = count;
        widget->value = value;
        sync_listbox(widget);
    } else {
        for (i = 0; i < count; ++i) free(items[i]);
        free(items);
    }
    return status;
}

size_t xxwidgets_listbox_count(const xxwidgets_widget *widget)
{
    return widget && widget->kind == XXWIDGETS_LISTBOX ? widget->item_count : 0;
}
