/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_SCAN_DIRECTORY_PLATFORM_H
#define XXWIDGETS_SCAN_DIRECTORY_PLATFORM_H
#include "xxwidgets/xxwidgets.h"
typedef struct xxwidgets_directory_iterator xxwidgets_directory_iterator;
enum { XXW_DIRECTORY_END, XXW_DIRECTORY_FILE, XXW_DIRECTORY_SUBDIR, XXW_DIRECTORY_SKIP, XXW_DIRECTORY_ERROR };
xxwidgets_status xxwidgets_directory_open(const char *path, xxwidgets_directory_iterator **out);
xxwidgets_status xxwidgets_directory_open_child(
    xxwidgets_directory_iterator *parent, const char *name, const char *path, xxwidgets_directory_iterator **out);
/* name is malloc-owned; END has no name. ERROR describes one metadata failure. */
xxwidgets_status xxwidgets_directory_next(xxwidgets_directory_iterator *iterator, char **name, int *kind);
void xxwidgets_directory_close(xxwidgets_directory_iterator *iterator);
#endif
