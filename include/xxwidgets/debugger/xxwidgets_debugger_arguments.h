/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_DEBUGGER_ARGUMENTS_H
#define XXWIDGETS_DEBUGGER_ARGUMENTS_H

#include "xxwidgets/xxwidgets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parse a debugger's arguments field without a shell. program is copied into
 * argv[0]; arguments contains only the remaining arguments and may be NULL.
 * On success argv[argc] is NULL. Release the returned array with the matching
 * free function. On failure argc is 0 and argv is NULL; error, when supplied,
 * receives a diagnostic. Program must be nonempty. UTF-8 bytes are preserved.
 *
 * ASCII whitespace separates arguments outside matching single/double quotes.
 * Quoted and unquoted segments concatenate; an empty quoted argument survives.
 * Backslash escapes a backslash or the active quote; outside quotes it also
 * escapes either quote or whitespace. Before any other character it remains
 * literal, so ordinary Windows paths retain their backslashes. A trailing
 * backslash is an incomplete escape: use two backslashes for a final literal
 * one, including before a closing quote. No expansion, command substitution,
 * wildcard processing, or environment lookup occurs. */
XXWIDGETS_API xxwidgets_status xxwidgets_debugger_arguments_parse(
    const char *program, const char *arguments, int *argc, char ***argv,
    char *error, size_t error_capacity);
XXWIDGETS_API void xxwidgets_debugger_arguments_free(int argc, char **argv);

#ifdef __cplusplus
}
#endif
#endif
