/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidget_symbolsviewer.h"

xxwidgets_status xxwidget_symbolsviewer_set_records(xxwidgets_widget *widget, const xx_symbol_record *records, size_t count)
{ return xxwidgets_binaryviewer_set_records(widget, XXWIDGET_SYMBOLSVIEWER, records, count); }

xxwidgets_status xxwidget_symbolsviewer_clear(xxwidgets_widget *widget)
{ return xxwidget_symbolsviewer_set_records(widget, NULL, 0); }

size_t xxwidget_symbolsviewer_count(const xxwidgets_widget *widget)
{ return widget && widget->kind == XXWIDGET_SYMBOLSVIEWER ? widget->item_count : 0; }

xxwidgets_status xxwidget_symbolsviewer_get_record(const xxwidgets_widget *widget, size_t index, xx_symbol_record *record)
{ return xxwidgets_binaryviewer_get_record(widget, XXWIDGET_SYMBOLSVIEWER, index, record); }

xxwidgets_status xxwidget_symbolsviewer_get_selection(xxwidgets_widget *widget, size_t *index, xx_symbol_record *record)
{ return xxwidgets_binaryviewer_get_selection(widget, XXWIDGET_SYMBOLSVIEWER, index, record); }
