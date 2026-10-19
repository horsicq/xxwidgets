/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidget_importsviewer.h"

xxwidgets_status xxwidget_importsviewer_set_records(xxwidgets_widget *widget, const xx_import_record *records, size_t count)
{ return xxwidgets_binaryviewer_set_records(widget, XXWIDGET_IMPORTSVIEWER, records, count); }

xxwidgets_status xxwidget_importsviewer_clear(xxwidgets_widget *widget)
{ return xxwidget_importsviewer_set_records(widget, NULL, 0); }

size_t xxwidget_importsviewer_count(const xxwidgets_widget *widget)
{ return widget && widget->kind == XXWIDGET_IMPORTSVIEWER ? widget->item_count : 0; }

xxwidgets_status xxwidget_importsviewer_get_record(const xxwidgets_widget *widget, size_t index, xx_import_record *record)
{ return xxwidgets_binaryviewer_get_record(widget, XXWIDGET_IMPORTSVIEWER, index, record); }

xxwidgets_status xxwidget_importsviewer_get_selection(xxwidgets_widget *widget, size_t *index, xx_import_record *record)
{ return xxwidgets_binaryviewer_get_selection(widget, XXWIDGET_IMPORTSVIEWER, index, record); }
