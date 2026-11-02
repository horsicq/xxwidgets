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

#include "xxwidgets_layout.h"

static int item_minimum(const xxwidgets_layout_item *item)
{
    return item->minimum > 0 ? item->minimum : 0;
}

static int item_preferred(const xxwidgets_layout_item *item)
{
    int minimum = item_minimum(item);
    return item->preferred > minimum ? item->preferred : minimum;
}

static int can_grow(const xxwidgets_layout_item *item)
{
    return !item->hidden && item->stretch > 0 && (item->maximum <= 0 || item->size < item->maximum);
}

static int line_length(const xxwidgets_layout_item *items, size_t count, int spacing, int minimum)
{
    long long total = 0;
    size_t i;
    const xxwidgets_layout_item *previous = NULL;
    if (!items) return 0;
    if (spacing < 0) spacing = 0;
    for (i = 0; i < count; ++i) {
        if (items[i].hidden) continue;
        if (previous && !previous->spacer) total += spacing;
        total += minimum ? item_minimum(&items[i]) : item_preferred(&items[i]);
        previous = &items[i];
    }
    return total > 0x3fffffff ? 0x3fffffff : (int)total;
}

int xxwidgets_layout_line_minimum(const xxwidgets_layout_item *items, size_t count, int spacing)
{
    return line_length(items, count, spacing, 1);
}

int xxwidgets_layout_line_preferred(const xxwidgets_layout_item *items, size_t count, int spacing)
{
    return line_length(items, count, spacing, 0);
}

static void grow(xxwidgets_layout_item *items, size_t count, int extra)
{
    size_t i;
    while (extra > 0) {
        long long weight = 0;
        int given = 0;
        for (i = 0; i < count; ++i)
            if (can_grow(&items[i])) weight += items[i].stretch;
        if (!weight) return;
        for (i = 0; i < count; ++i) {
            long long share;
            if (!can_grow(&items[i])) continue;
            share = (long long)extra * items[i].stretch / weight;
            if (items[i].maximum > 0 && items[i].size + share > items[i].maximum) share = items[i].maximum - items[i].size;
            items[i].size += (int)share;
            given += (int)share;
        }
        if (!given) {
            /* Less extra than total weight: one unit each, in order. */
            for (i = 0; i < count && extra > 0; ++i)
                if (can_grow(&items[i])) {
                    ++items[i].size;
                    --extra;
                    ++given;
                }
            if (!given) return;
            continue;
        }
        extra -= given;
    }
}

static void shrink(xxwidgets_layout_item *items, size_t count, int deficit)
{
    long long capacity = 0;
    int taken = 0;
    size_t i;
    for (i = 0; i < count; ++i)
        if (!items[i].hidden) capacity += items[i].size - item_minimum(&items[i]);
    if (capacity <= deficit) {
        for (i = 0; i < count; ++i)
            if (!items[i].hidden) items[i].size = item_minimum(&items[i]);
        return;
    }
    for (i = 0; i < count; ++i) {
        int take;
        if (items[i].hidden) continue;
        take = (int)((long long)deficit * (items[i].size - item_minimum(&items[i])) / capacity);
        items[i].size -= take;
        taken += take;
    }
    deficit -= taken;
    while (deficit > 0) {
        int progress = 0;
        for (i = 0; i < count && deficit > 0; ++i)
            if (!items[i].hidden && items[i].size > item_minimum(&items[i])) {
                --items[i].size;
                --deficit;
                progress = 1;
            }
        if (!progress) return;
    }
}

int xxwidgets_layout_line(xxwidgets_layout_item *items, size_t count, int start, int length, int spacing)
{
    const xxwidgets_layout_item *previous = NULL;
    int preferred, position = start;
    size_t i;
    if (!items || !count) return start;
    if (spacing < 0) spacing = 0;
    if (length < 0) length = 0;
    for (i = 0; i < count; ++i) items[i].size = items[i].hidden ? 0 : item_preferred(&items[i]);
    preferred = xxwidgets_layout_line_preferred(items, count, spacing);
    if (length >= preferred) grow(items, count, length - preferred);
    else shrink(items, count, preferred - length);
    for (i = 0; i < count; ++i) {
        if (items[i].hidden) {
            items[i].position = position;
            continue;
        }
        if (previous && !previous->spacer) position += spacing;
        items[i].position = position;
        position += items[i].size;
        previous = &items[i];
    }
    return position;
}

int xxwidgets_layout_max(const int *values, size_t count)
{
    int widest = 0;
    size_t i;
    for (i = 0; values && i < count; ++i)
        if (values[i] > widest) widest = values[i];
    return widest;
}

int xxwidgets_layout_caption_box_height(const xxwidgets_metrics *metrics, int control_height)
{
    return metrics->label_height + metrics->caption_gap + 2 * metrics->frame_inset + control_height;
}

int xxwidgets_layout_caption_box_width(const xxwidgets_metrics *metrics, int control_width)
{
    return control_width + 2 * metrics->frame_inset;
}

void xxwidgets_layout_caption_box_split(const xxwidgets_metrics *metrics, xxwidgets_rect box, int control_height,
                                        xxwidgets_layout_caption_box *out)
{
    int top = metrics->label_height + metrics->caption_gap, inset = metrics->frame_inset;
    out->caption = xxwidgets_layout_rect(box.x, box.y, box.width, metrics->label_height);
    out->frame = xxwidgets_layout_rect(box.x, box.y + top, box.width, box.height - top > 1 ? box.height - top : 1);
    out->control = xxwidgets_layout_rect(box.x + inset, out->frame.y + inset, box.width - 2 * inset > 1 ? box.width - 2 * inset : 1,
                                         control_height > 1 ? control_height : 1);
}

xxwidgets_status xxwidgets_layout_place(xxwidgets_widget *widget, xxwidgets_rect rect)
{
    if (!widget) return XXWIDGETS_OK;
    if (rect.x < 0) rect.x = 0;
    if (rect.y < 0) rect.y = 0;
    if (rect.x > 32767) rect.x = 32767;
    if (rect.y > 32767) rect.y = 32767;
    if (rect.width < 1) rect.width = 1;
    if (rect.height < 1) rect.height = 1;
    if (rect.width > 32767) rect.width = 32767;
    if (rect.height > 32767) rect.height = 32767;
    return xxwidgets_widget_set_frame(widget, rect);
}
