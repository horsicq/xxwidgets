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

xxwidgets_status xxwidgets_window_set_accept_file_drops(xxwidgets_widget *window, int accept)
{
    xxwidgets_status status;
    if (!window || window->kind != XXWIDGETS_WINDOW || (accept != 0 && accept != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    if (window->accept_file_drops == accept) return XXWIDGETS_OK;
    if (!window->app->ops->accept_file_drops) return XXWIDGETS_UNAVAILABLE;
    ++window->app->syncing;
    status = window->app->ops->accept_file_drops(window, accept);
    --window->app->syncing;
    if (status == XXWIDGETS_OK) window->accept_file_drops = accept;
    return status;
}

static xxwidgets_status sync_window(xxwidgets_widget *widget)
{
    xxwidgets_status status;
    ++widget->app->syncing;
    status = widget->app->ops->sync(widget);
    --widget->app->syncing;
    return status;
}

xxwidgets_status xxwidgets_window_set_minimum_size(xxwidgets_widget *window, int columns, int rows)
{
    int previous_columns, previous_rows, previous_set;
    xxwidgets_status status;
    if (!window || window->kind != XXWIDGETS_WINDOW || columns < 0 || rows < 0 || columns > 32767 || rows > 32767) return XXWIDGETS_INVALID_ARGUMENT;
    previous_columns = window->min_columns;
    previous_rows = window->min_rows;
    previous_set = window->has_minimum;
    window->min_columns = columns;
    window->min_rows = rows;
    window->has_minimum = 1;
    status = sync_window(window);
    if (status != XXWIDGETS_OK) {
        window->min_columns = previous_columns;
        window->min_rows = previous_rows;
        window->has_minimum = previous_set;
        sync_window(window);
    }
    return status;
}
