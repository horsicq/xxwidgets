/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets/debugger/xxwidgets_debugger_arguments.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int argument_space(unsigned char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\v' || ch == '\f';
}

static xxwidgets_status argument_error(xxwidgets_status status, char *error, size_t capacity, const char *message)
{
    if (error && capacity) (void)snprintf(error, capacity, "%s", message);
    return status;
}

void xxwidgets_debugger_arguments_free(int argc, char **argv)
{
    int i;
    if (!argv) return;
    for (i = 0; i < argc; ++i) free(argv[i]);
    free(argv);
}

static int argument_append(char ***values, int *count, size_t *capacity, const char *text, size_t length)
{
    char *copy;
    if (*count == INT_MAX || length == SIZE_MAX) return 0;
    if ((size_t)*count + 1 >= *capacity) {
        size_t next = *capacity ? *capacity * 2 : 8;
        char **grown;
        if (next < *capacity || next > SIZE_MAX / sizeof(*grown)) return 0;
        grown = realloc(*values, next * sizeof(*grown));
        if (!grown) return 0;
        *values = grown;
        *capacity = next;
    }
    copy = malloc(length + 1);
    if (!copy) return 0;
    memcpy(copy, text, length);
    copy[length] = '\0';
    (*values)[(*count)++] = copy;
    (*values)[*count] = NULL;
    return 1;
}

xxwidgets_status xxwidgets_debugger_arguments_parse(
    const char *program, const char *arguments, int *argc, char ***argv,
    char *error, size_t error_capacity)
{
    const unsigned char *cursor;
    char **values = NULL, *word = NULL;
    size_t capacity = 0, input_length;
    int count = 0;
    xxwidgets_status status = XXWIDGETS_OK;
    if (argc) *argc = 0;
    if (argv) *argv = NULL;
    if (error && error_capacity) error[0] = '\0';
    if (!program || !*program || !argc || !argv)
        return argument_error(XXWIDGETS_INVALID_ARGUMENT, error, error_capacity, "A program path and argument outputs are required.");
    if (!arguments) arguments = "";
    input_length = strlen(arguments);
    if (input_length == SIZE_MAX || !argument_append(&values, &count, &capacity, program, strlen(program))) goto memory_failure;
    word = malloc(input_length + 1);
    if (!word) goto memory_failure;
    cursor = (const unsigned char *)arguments;
    while (*cursor) {
        unsigned char quote = 0;
        size_t length = 0;
        while (argument_space(*cursor)) ++cursor;
        if (!*cursor) break;
        while (*cursor) {
            unsigned char ch = *cursor;
            if (!quote && argument_space(ch)) break;
            if (ch == '\\') {
                unsigned char next = cursor[1];
                if (!next) {
                    status = argument_error(XXWIDGETS_INVALID_ARGUMENT, error, error_capacity, "Arguments end with an incomplete backslash escape.");
                    goto finish;
                }
                if (next == '\\' || (quote && next == quote) ||
                    (!quote && (next == '\'' || next == '"' || argument_space(next)))) {
                    word[length++] = (char)next;
                    cursor += 2;
                } else {
                    word[length++] = (char)ch;
                    ++cursor;
                }
            } else if (quote && ch == quote) {
                quote = 0;
                ++cursor;
            } else if (!quote && (ch == '\'' || ch == '"')) {
                quote = ch;
                ++cursor;
            } else {
                word[length++] = (char)ch;
                ++cursor;
            }
        }
        if (quote) {
            status = argument_error(XXWIDGETS_INVALID_ARGUMENT, error, error_capacity,
                                    quote == '\'' ? "Arguments contain an unterminated single quote." : "Arguments contain an unterminated double quote.");
            goto finish;
        }
        if (!argument_append(&values, &count, &capacity, word, length)) goto memory_failure;
    }
    *argc = count;
    *argv = values;
    values = NULL;
    goto finish;
memory_failure:
    status = argument_error(XXWIDGETS_OUT_OF_MEMORY, error, error_capacity, "Cannot allocate program arguments.");
finish:
    free(word);
    xxwidgets_debugger_arguments_free(count, values);
    return status;
}
