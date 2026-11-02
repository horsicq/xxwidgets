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

/* Small private helpers shared by the main-window components (value field,
 * file name bar, file header, side panel). Not part of the public API. */

#ifndef XXWIDGETS_COMPONENTS_INTERNAL_H
#define XXWIDGETS_COMPONENTS_INTERNAL_H

#include "xxwidgets_internal.h"
#include "xxwidgets_layout.h"

/* A Qt Detect It Easy width (pixels at 96 DPI, where the caption height L
 * is 15 and H is 23) in layout units: scaled by this backend's text height,
 * so the proportions follow the font; a theme with taller controls (GTK's
 * Adwaita H is about 34 for a 17 px line) does not widen the window. Without
 * a label height, by the control height. In cells, about one cell per 8 Qt
 * pixels. */
static inline int xxwidgets_component_qt(const xxwidgets_metrics *metrics, int qt_pixels)
{
    if (metrics->unit_is_cell) return (qt_pixels + 4) / 8 > 0 ? (qt_pixels + 4) / 8 : 1;
    if (metrics->label_height > 0) return (int)(((long long)qt_pixels * metrics->label_height + 7) / 15);
    return (int)(((long long)qt_pixels * metrics->control_height + 11) / 23);
}

/* The side of an icon-only button: the icon plus a frame, at least the
 * button's preferred size. */
static inline int xxwidgets_component_icon_button(const xxwidgets_metrics *metrics, int preferred)
{
    int side = metrics->small_icon > 0 ? metrics->small_icon + 8 : 0;
    return preferred > side ? preferred : side;
}

/* The control receives user input in its current state. */
static inline int xxwidgets_component_live(const xxwidgets_widget *widget)
{
    return widget && widget->visible && widget->enabled;
}

/* Components are created and destroyed outside callbacks, like the scan
 * panel. */
static inline int xxwidgets_component_busy(const xxwidgets_app *app)
{
    return app->dispatch_depth || app->polling || app->syncing;
}

struct xxwidgets_file_header;
/* xxwidgets_file_header.c: the row's size in either view, whatever the
 * current one (xxwidgets_die_main_panel sizes both views). */
xxwidgets_status xxwidgets_file_header_view_size(struct xxwidgets_file_header *header, int advanced, int minimum, int *width, int *height);

static inline int xxwidgets_component_max(int a, int b)
{
    return a > b ? a : b;
}

#endif
