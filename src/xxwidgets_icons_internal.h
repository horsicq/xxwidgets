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

#ifndef XXWIDGETS_ICONS_INTERNAL_H
#define XXWIDGETS_ICONS_INTERNAL_H

#include "xxwidgets/xxwidgets_icons.h"

/* One row of the generated table (src/xxwidgets_icons_data.c): the XStyles
 * name and 16 x 16 straight-alpha RGBA8 pixels, or NULL pixels for a retired
 * icon that keeps its id. Row 0 is XXWIDGETS_ICON_NONE. */
typedef struct xxwidgets_icon_entry {
    const char *name;
    const unsigned char *rgba;
    unsigned short width, height;
} xxwidgets_icon_entry;

extern const xxwidgets_icon_entry xxwidgets_icon_entries[XXWIDGETS_ICON_COUNT];

#endif
