/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidget_resourcesviewer.h"

xxwidgets_status xxwidget_resourcesviewer_set_records(xxwidgets_widget *widget, const xx_resource_record *records, size_t count)
{ return xxwidgets_binaryviewer_set_records(widget, XXWIDGET_RESOURCESVIEWER, records, count); }

xxwidgets_status xxwidget_resourcesviewer_clear(xxwidgets_widget *widget)
{ return xxwidget_resourcesviewer_set_records(widget, NULL, 0); }

size_t xxwidget_resourcesviewer_count(const xxwidgets_widget *widget)
{ return widget && widget->kind == XXWIDGET_RESOURCESVIEWER ? widget->item_count : 0; }

xxwidgets_status xxwidget_resourcesviewer_get_record(const xxwidgets_widget *widget, size_t index, xx_resource_record *record)
{ return xxwidgets_binaryviewer_get_record(widget, XXWIDGET_RESOURCESVIEWER, index, record); }

xxwidgets_status xxwidget_resourcesviewer_get_selection(xxwidgets_widget *widget, size_t *index, xx_resource_record *record)
{ return xxwidgets_binaryviewer_get_selection(widget, XXWIDGET_RESOURCESVIEWER, index, record); }
