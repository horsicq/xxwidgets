/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXWIDGET_SYSTEMPROCESSESVIEWER_MEMORY_H
#define XXWIDGET_SYSTEMPROCESSESVIEWER_MEMORY_H
#include "xxwidgets/xxwidget_systemprocessesviewer.h"
#include "xxfclib/io/xx_io.h"

typedef struct xxwidgets_process_memory {
    xx_io_device *device;
    uint64_t base, size;
    void *native;
} xxwidgets_process_memory;

/* Use a fresh zero-initialized state. On success the READ device borrows the
 * identity-checked native object held by native. The default range comes from
 * readable mapping metadata; later reads can still fail if memory changes.
 * Failure clears the fresh state. Close the device before its native object. */
xxwidgets_status xxwidgets_process_memory_open(const xxwidgets_process_info *, xxwidgets_process_memory *);
void xxwidgets_process_memory_close(xxwidgets_process_memory *);
#endif
