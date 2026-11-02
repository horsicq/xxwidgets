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

/* The built-in icon set: lookups over the generated table. */

#include "xxwidgets_icons_internal.h"
#include "xxwidgets_internal.h"

static int valid_id(xxwidgets_icon_id id)
{
    return id > XXWIDGETS_ICON_NONE && id < XXWIDGETS_ICON_COUNT;
}

xxwidgets_status xxwidgets_icon_get(xxwidgets_icon_id id, xxwidgets_icon *icon)
{
    const xxwidgets_icon_entry *entry;
    if (!icon || !valid_id(id)) return XXWIDGETS_INVALID_ARGUMENT;
    entry = &xxwidgets_icon_entries[id];
    if (!entry->rgba) return XXWIDGETS_UNAVAILABLE;
    icon->rgba = entry->rgba;
    icon->width = entry->width;
    icon->height = entry->height;
    icon->stride = (size_t)entry->width * 4u;
    return XXWIDGETS_OK;
}

const char *xxwidgets_icon_name(xxwidgets_icon_id id)
{
    return valid_id(id) ? xxwidgets_icon_entries[id].name : NULL;
}

static int ascii_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

xxwidgets_icon_id xxwidgets_icon_find(const char *name)
{
    int id;
    for (id = XXWIDGETS_ICON_NONE + 1; name && id < XXWIDGETS_ICON_COUNT; ++id) {
        const char *a = name, *b = xxwidgets_icon_entries[id].name;
        while (*a && *b && ascii_lower((unsigned char)*a) == ascii_lower((unsigned char)*b)) ++a, ++b;
        if (!*a && !*b) return (xxwidgets_icon_id)id;
    }
    return XXWIDGETS_ICON_NONE;
}

xxwidgets_status xxwidgets_button_set_icon_id(xxwidgets_widget *button, xxwidgets_icon_id id, unsigned int flags)
{
    xxwidgets_icon icon;
    xxwidgets_status status;
    if (id == XXWIDGETS_ICON_NONE) return xxwidgets_button_set_icon(button, NULL, 0, 0, 0, 0);
    status = xxwidgets_icon_get(id, &icon);
    if (status != XXWIDGETS_OK) return status;
    return xxwidgets_button_set_icon(button, icon.rgba, icon.width, icon.height, icon.stride, flags);
}
