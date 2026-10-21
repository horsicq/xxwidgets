/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxfclib/codepages/xx_codepage.h"

#include <stdlib.h>
#include <string.h>

/* Bound clipboard input before calculating allocations. A Symbols paste can
 * expand UTF-8 into at most four output bytes per Unicode scalar. */
#define HEX_PASTE_TEXT_LIMIT ((size_t)16 * 1024 * 1024)

static int paste_space(unsigned char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\v' || value == '\f';
}

static int paste_digit(unsigned char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

/* The first pass validates everything and determines the exact allocation;
 * callers never observe a partial conversion. Whitespace may separate bytes,
 * but never the two nibbles belonging to a byte. */
static int paste_hex(const unsigned char *text, size_t size, unsigned char *bytes, size_t *length)
{
    size_t at = 0, count = 0;
    while (at < size) {
        int high, low;
        while (at < size && paste_space(text[at])) ++at;
        if (at == size) break;
        if (size - at < 2) return 0;
        high = paste_digit(text[at++]); low = paste_digit(text[at++]);
        if (high < 0 || low < 0) return 0;
        if (bytes) bytes[count] = (unsigned char)((high << 4) | low);
        ++count;
    }
    *length = count;
    return 1;
}

static int paste_symbols(const xx_codepage *encoding, const unsigned char *text, size_t size,
                         unsigned char *bytes, size_t *length)
{
    const xx_codepage *utf8 = xx_codepage_find("UTF-8");
    size_t at = 0, count = 0;
    unsigned char encoded[8];
    if (!encoding || !utf8 || encoding->max_bytes > sizeof(encoded)) return 0;
    while (at < size) {
        uint32_t scalar;
        size_t used = 0, written = 0;
        if (xx_codepage_decode(utf8, text + at, size - at, &used, &scalar) != XX_CODEPAGE_OK ||
            !used || used > size - at ||
            xx_codepage_encode(encoding, scalar, encoded, sizeof(encoded), &written) != XX_CODEPAGE_OK ||
            !written || written > sizeof(encoded) || count > SIZE_MAX - written) return 0;
        if (bytes) memcpy(bytes + count, encoded, written);
        count += written; at += used;
    }
    *length = count;
    return 1;
}

xxwidgets_status xxwidgets_hexview_decode_paste(const xxwidgets_widget *w, const char *text,
                                               unsigned char **bytes, size_t *length)
{
    const xx_codepage *encoding;
    const unsigned char *input = (const unsigned char *)text;
    unsigned char *output;
    size_t size = 0, count = 0, converted = 0;
    if (bytes) *bytes = NULL;
    if (length) *length = 0;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || !text || !bytes || !length) return XXWIDGETS_INVALID_ARGUMENT;
    if (w->hex_header != 1 && w->hex_header != 2) return XXWIDGETS_UNAVAILABLE;
    while (size <= HEX_PASTE_TEXT_LIMIT && text[size]) ++size;
    if (size > HEX_PASTE_TEXT_LIMIT) return XXWIDGETS_INVALID_ARGUMENT;
    encoding = w->hex_header == 2 ? xx_codepage_find(w->hex_display.encoding) : NULL;
    if (!(w->hex_header == 2 ? paste_symbols(encoding, input, size, NULL, &count)
                            : paste_hex(input, size, NULL, &count))) return XXWIDGETS_INVALID_ARGUMENT;
    if (!count) return XXWIDGETS_OK;
    output = (unsigned char *)malloc(count);
    if (!output) return XXWIDGETS_OUT_OF_MEMORY;
    if (!(w->hex_header == 2 ? paste_symbols(encoding, input, size, output, &converted)
                            : paste_hex(input, size, output, &converted)) || converted != count) {
        free(output);
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    *bytes = output; *length = count;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_hexview_paste_text(xxwidgets_widget *w, const char *text)
{
    unsigned char *bytes = NULL;
    size_t length = 0;
    uint64_t offset;
    xxwidgets_status status = xxwidgets_hexview_decode_paste(w, text, &bytes, &length);
    if (status != XXWIDGETS_OK || !length) return status;
    if (w->hex_source_read && w->hex_source_length) offset = w->hex_source_selection;
    else {
        size_t index = !w->hex_source_read && w->hex_selection_length ? w->hex_selection : w->hex_cursor;
        if ((uint64_t)index > UINT64_MAX - w->hex_base) {
            free(bytes);
            return XXWIDGETS_INVALID_ARGUMENT;
        }
        offset = w->hex_base + (uint64_t)index;
    }
    status = xxwidgets_hexview_overwrite_bytes(w, offset, bytes, length);
    free(bytes);
    return status;
}

int xxwidgets_hexview_can_paste_insert(const xxwidgets_widget *w)
{
    return w && w->kind == XXWIDGETS_HEXVIEW && !w->hex_cut_busy &&
           !w->hex_source_seeking && !w->app->syncing &&
           (w->hex_header == 1 || w->hex_header == 2) && xxwidgets_hexview_can_resize(w);
}

xxwidgets_status xxwidgets_hexview_paste_insert_text(xxwidgets_widget *w, const char *text)
{
    unsigned char *bytes = NULL;
    size_t length = 0;
    uint64_t offset, selected_length;
    xxwidgets_status status;
    if (!w || w->kind != XXWIDGETS_HEXVIEW || !text) return XXWIDGETS_INVALID_ARGUMENT;
    if (!xxwidgets_hexview_can_paste_insert(w)) return XXWIDGETS_UNAVAILABLE;
    status = xxwidgets_hexview_decode_paste(w, text, &bytes, &length);
    if (status != XXWIDGETS_OK || !length) return status;
    status = xxwidgets_hexview_get_source_selection(w, &offset, &selected_length);
    if (status != XXWIDGETS_OK) {
        free(bytes);
        return status;
    }
    if (!selected_length) {
        /* A seek to EOF has no byte cursor. Empty devices have the same
         * representation and must still accept their first insertion. */
        if (w->hex_cursor >= w->hex_size) offset = w->hex_source_size;
        else if ((uint64_t)w->hex_cursor > UINT64_MAX - w->hex_base) {
            free(bytes);
            return XXWIDGETS_INVALID_ARGUMENT;
        } else offset = w->hex_base + (uint64_t)w->hex_cursor;
    }
    status = xxwidgets_hexview_insert_bytes(w, offset, bytes, length);
    free(bytes);
    return status;
}
