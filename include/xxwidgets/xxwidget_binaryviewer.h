/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXWIDGET_BINARYVIEWER_H
#define XXWIDGET_BINARYVIEWER_H
#include "xxwidgets/xxwidgets.h"
#include "xxfclib/formats/xx_format_streams.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Native/TUI metadata tables. Create the corresponding XXWIDGET_*VIEWER kind
 * with xxwidgets_widget_create. Setters deep-copy records and their strings,
 * escape control characters for display, select the first row (or -1 when
 * empty), and emit no input events. Failure leaves the previous model intact.
 * Record getters borrow strings until replacement/clear/destruction. Empty
 * selection succeeds with index SIZE_MAX and a zeroed record. Metadata offsets
 * and addresses preserve xxfclib's conventions: -1 / UINT64_MAX are unknown.
 * All APIs belong to the UI thread. */
#define XXWIDGET_DECLARE_BINARYVIEWER(name, record) \
    XXWIDGETS_API xxwidgets_status xxwidget_##name##_set_records(xxwidgets_widget *, const record *, size_t count); \
    XXWIDGETS_API xxwidgets_status xxwidget_##name##_clear(xxwidgets_widget *); \
    XXWIDGETS_API size_t xxwidget_##name##_count(const xxwidgets_widget *); \
    XXWIDGETS_API xxwidgets_status xxwidget_##name##_get_record(const xxwidgets_widget *, size_t index, record *); \
    XXWIDGETS_API xxwidgets_status xxwidget_##name##_get_selection(xxwidgets_widget *, size_t *index, record *); \
    xxwidgets_status xxwidget_##name##_load_file(xxwidgets_widget *, const char *path)
XXWIDGET_DECLARE_BINARYVIEWER(resourcesviewer, xx_resource_record);
XXWIDGET_DECLARE_BINARYVIEWER(importsviewer, xx_import_record);
XXWIDGET_DECLARE_BINARYVIEWER(exportsviewer, xx_export_record);
XXWIDGET_DECLARE_BINARYVIEWER(symbolsviewer, xx_symbol_record);
#undef XXWIDGET_DECLARE_BINARYVIEWER

/* File loaders are provided by xxwidgets::inspect (xxfclib native readers),
 * separately from the dependency-light widget model. Supported executables:
 * PE, ELF, Mach-O and the first architecture of a universal Mach-O file.
 * Unrecognized formats return UNAVAILABLE; missing tables load an empty table.
 * Failed reads/parses preserve the previous table. Call outside callbacks for
 * large inputs; these reads are synchronous. No external parsing executable,
 * Python runtime or LIEF dependency is used. */
xxwidgets_status xxwidget_binaryviewer_load_file(xxwidgets_widget *, const char *path);

#ifdef __cplusplus
}
#endif
#endif
