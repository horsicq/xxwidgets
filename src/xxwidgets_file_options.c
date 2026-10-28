/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

xxwidgets_status xxwidgets_app_set_save_backup(xxwidgets_app *app, int enabled)
{
    if (!app || (enabled != 0 && enabled != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    app->save_backup = enabled;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_app_get_save_backup(const xxwidgets_app *app, int *enabled)
{
    if (!app || !enabled) return XXWIDGETS_INVALID_ARGUMENT;
    *enabled = app->save_backup;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_backup_destination(const xxwidgets_app *app, const char *path)
{
    if (!app || !path || !*path || !xxwidgets_valid_utf8(path)) return XXWIDGETS_INVALID_ARGUMENT;
    if (!app->save_backup) return XXWIDGETS_OK;
#ifdef _WIN32
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *wide, *backup, *temporary;
    DWORD attributes;
    xxwidgets_status status;
    if (!count || (size_t)count > SIZE_MAX / sizeof(wchar_t) - 80) return XXWIDGETS_INVALID_ARGUMENT;
    wide = malloc((size_t)count * sizeof(*wide));
    if (!wide) return XXWIDGETS_OUT_OF_MEMORY;
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, count);
    attributes = GetFileAttributesW(wide);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        free(wide);
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? XXWIDGETS_OK : XXWIDGETS_PLATFORM_ERROR;
    }
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) { free(wide); return XXWIDGETS_PLATFORM_ERROR; }
    backup = malloc(((size_t)count + 4) * sizeof(*backup));
    if (!backup) { free(wide); return XXWIDGETS_OUT_OF_MEMORY; }
    memcpy(backup, wide, ((size_t)count - 1) * sizeof(*backup));
    memcpy(backup + count - 1, L".bak", 5 * sizeof(*backup));
    temporary = malloc(((size_t)count + 80) * sizeof(*temporary));
    if (!temporary) { free(backup); free(wide); return XXWIDGETS_OUT_OF_MEMORY; }
    status = XXWIDGETS_PLATFORM_ERROR;
    for (unsigned int attempt = 0; attempt < 128; ++attempt) {
        swprintf(temporary, (size_t)count + 80, L"%ls.tmp-%lu-%lu-%u", backup,
            (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount(), attempt);
        if (CopyFileW(wide, temporary, TRUE)) {
            if (MoveFileExW(temporary, backup, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) status = XXWIDGETS_OK;
            else {
                DWORD attrs = GetFileAttributesW(temporary);
                if (attrs != INVALID_FILE_ATTRIBUTES) SetFileAttributesW(temporary, attrs & ~FILE_ATTRIBUTE_READONLY);
                DeleteFileW(temporary);
            }
            break;
        }
        DWORD error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) break;
    }
    free(temporary); free(backup); free(wide);
    return status;
#else
    struct stat info;
    FILE *source, *backup;
    char *name, *temporary;
    unsigned char buffer[16384];
    size_t count, length = strlen(path);
    int ok = 1, descriptor;
    if (stat(path, &info)) return errno == ENOENT ? XXWIDGETS_OK : XXWIDGETS_PLATFORM_ERROR;
    if (!S_ISREG(info.st_mode) || length > SIZE_MAX - 20) return XXWIDGETS_PLATFORM_ERROR;
    source = fopen(path, "rb");
    if (!source) return XXWIDGETS_PLATFORM_ERROR;
    name = malloc(length + 5);
    temporary = malloc(length + 20);
    if (!name || !temporary) { free(name); free(temporary); fclose(source); return XXWIDGETS_OUT_OF_MEMORY; }
    memcpy(name, path, length); memcpy(name + length, ".bak", 5);
    memcpy(temporary, name, length + 4); memcpy(temporary + length + 4, ".tmp-XXXXXX", 12);
    descriptor = mkstemp(temporary);
    backup = descriptor >= 0 ? fdopen(descriptor, "wb") : NULL;
    if (!backup) {
        if (descriptor >= 0) { close(descriptor); remove(temporary); }
        free(name); free(temporary); fclose(source); return XXWIDGETS_PLATFORM_ERROR;
    }
    while ((count = fread(buffer, 1, sizeof(buffer), source)) != 0)
        if (fwrite(buffer, 1, count, backup) != count) { ok = 0; break; }
    if (ferror(source)) ok = 0;
    if (fclose(source)) ok = 0;
    if (fclose(backup)) ok = 0;
    /* Replacing the directory entry avoids following a stale .bak symlink
     * or truncating a hard link to the destination we are protecting. */
    if (ok && rename(temporary, name)) ok = 0;
    if (!ok) remove(temporary);
    free(name); free(temporary);
    return ok ? XXWIDGETS_OK : XXWIDGETS_PLATFORM_ERROR;
#endif
}
