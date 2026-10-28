/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_SIGNATURE_SAVE_H
#define XXWIDGETS_SIGNATURE_SAVE_H
#include "xxwidgets/xxwidgets_disk_write.h"
#include "xxfclib/data/xx_pd.h"
/* Internal worker operation. source/destination are validated immutable UTF-8
 * snapshots owned by the caller; no widget is accessed. NULL reporter skips
 * disk-dialog counters while preserving xx_pd progress/cancellation. */
xxwidgets_status xxwidgets_signature_save_snapshot(const char *destination, const char *source, uint64_t sequence,
    xx_pd_struct *progress, xxwidgets_disk_write_reporter *reporter);
#endif
