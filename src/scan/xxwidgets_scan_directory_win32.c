/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_scan_directory_platform.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
struct xxwidgets_directory_iterator {
    HANDLE handle;
    WIN32_FIND_DATAW entry;
    int first;
};
static wchar_t *wide_path(const char *path)
{
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *wide = count > 0 ? malloc((size_t)count * sizeof(*wide)) : NULL;
    if (wide && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, count)) {
        free(wide);
        return NULL;
    }
    return wide;
}
xxwidgets_status xxwidgets_directory_open(const char *path, xxwidgets_directory_iterator **out)
{
    wchar_t *wide = wide_path(path), *pattern;
    xxwidgets_directory_iterator *iterator;
    DWORD attributes, error;
    size_t length;
    *out = NULL;
    if (!wide)
        return XXWIDGETS_OUT_OF_MEMORY;
    attributes = GetFileAttributesW(wide);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        free(wide);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    length = wcslen(wide);
    if (length > SIZE_MAX / sizeof(*pattern) - 3) {
        free(wide);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    pattern = malloc((length + 3) * sizeof(*pattern));
    iterator = calloc(1, sizeof(*iterator));
    if (!pattern || !iterator) {
        free(pattern);
        free(iterator);
        free(wide);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    memcpy(pattern, wide, length * sizeof(*pattern));
    if (length && wide[length - 1] != L'/' && wide[length - 1] != L'\\' && !(length == 2 && wide[1] == L':'))
        pattern[length++] = L'\\';
    pattern[length++] = L'*';
    pattern[length] = 0;
    iterator->handle = FindFirstFileW(pattern, &iterator->entry);
    error = GetLastError();
    free(pattern);
    free(wide);
    if (iterator->handle == INVALID_HANDLE_VALUE && error != ERROR_FILE_NOT_FOUND) {
        free(iterator);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    iterator->first = 1;
    *out = iterator;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_directory_open_child(
    xxwidgets_directory_iterator *parent, const char *name, const char *path, xxwidgets_directory_iterator **out)
{
    wchar_t *wide = wide_path(path);
    DWORD attributes;
    (void)parent;
    (void)name;
    *out = NULL;
    if (!wide)
        return XXWIDGETS_OUT_OF_MEMORY;
    attributes = GetFileAttributesW(wide);
    free(wide);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return XXWIDGETS_PLATFORM_ERROR;
    return xxwidgets_directory_open(path, out);
}
xxwidgets_status xxwidgets_directory_next(xxwidgets_directory_iterator *iterator, char **name, int *kind)
{
    int count;
    *name = NULL;
    *kind = XXW_DIRECTORY_END;
    if (iterator->handle == INVALID_HANDLE_VALUE)
        return XXWIDGETS_OK;
    for (;;) {
        if (!iterator->first && !FindNextFileW(iterator->handle, &iterator->entry))
            return GetLastError() == ERROR_NO_MORE_FILES ? XXWIDGETS_OK : XXWIDGETS_PLATFORM_ERROR;
        iterator->first = 0;
        if (wcscmp(iterator->entry.cFileName, L".") && wcscmp(iterator->entry.cFileName, L".."))
            break;
    }
    count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, iterator->entry.cFileName, -1, NULL, 0, NULL, NULL);
    if (count <= 0) {
        *kind = XXW_DIRECTORY_SKIP;
        return XXWIDGETS_OK;
    }
    *name = malloc((size_t)count);
    if (!*name)
        return XXWIDGETS_OUT_OF_MEMORY;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, iterator->entry.cFileName, -1, *name, count, NULL, NULL)) {
        free(*name);
        *name = NULL;
        *kind = XXW_DIRECTORY_SKIP;
        return XXWIDGETS_OK;
    }
    *kind = iterator->entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ? XXW_DIRECTORY_SKIP
        : iterator->entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY       ? XXW_DIRECTORY_SUBDIR
        : iterator->entry.dwFileAttributes & FILE_ATTRIBUTE_DEVICE          ? XXW_DIRECTORY_SKIP
                                                                            : XXW_DIRECTORY_FILE;
    return XXWIDGETS_OK;
}
void xxwidgets_directory_close(xxwidgets_directory_iterator *iterator)
{
    if (iterator) {
        if (iterator->handle != INVALID_HANDLE_VALUE)
            FindClose(iterator->handle);
        free(iterator);
    }
}
