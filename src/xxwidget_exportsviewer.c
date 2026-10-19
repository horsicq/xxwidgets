/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidget_exportsviewer.h"

xxwidgets_status xxwidget_exportsviewer_set_records(xxwidgets_widget *widget, const xx_export_record *records, size_t count)
{ return xxwidgets_binaryviewer_set_records(widget, XXWIDGET_EXPORTSVIEWER, records, count); }

xxwidgets_status xxwidget_exportsviewer_clear(xxwidgets_widget *widget)
{ return xxwidget_exportsviewer_set_records(widget, NULL, 0); }

size_t xxwidget_exportsviewer_count(const xxwidgets_widget *widget)
{ return widget && widget->kind == XXWIDGET_EXPORTSVIEWER ? widget->item_count : 0; }

xxwidgets_status xxwidget_exportsviewer_get_record(const xxwidgets_widget *widget, size_t index, xx_export_record *record)
{ return xxwidgets_binaryviewer_get_record(widget, XXWIDGET_EXPORTSVIEWER, index, record); }

xxwidgets_status xxwidget_exportsviewer_get_selection(xxwidgets_widget *widget, size_t *index, xx_export_record *record)
{ return xxwidgets_binaryviewer_get_selection(widget, XXWIDGET_EXPORTSVIEWER, index, record); }
