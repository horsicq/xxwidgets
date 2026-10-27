/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "xxwidgets_scan_directory_platform.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
struct xxwidgets_directory_iterator {
    DIR *directory;
};
static xxwidgets_status from_fd(int fd, xxwidgets_directory_iterator **out)
{
    xxwidgets_directory_iterator *iterator;
    *out = NULL;
    if (fd < 0)
        return XXWIDGETS_PLATFORM_ERROR;
    iterator = malloc(sizeof(*iterator));
    if (!iterator) {
        close(fd);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    iterator->directory = fdopendir(fd);
    if (!iterator->directory) {
        close(fd);
        free(iterator);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    *out = iterator;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_directory_open(const char *path, xxwidgets_directory_iterator **out)
{
    return from_fd(open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC), out);
}
xxwidgets_status xxwidgets_directory_open_child(
    xxwidgets_directory_iterator *parent, const char *name, const char *path, xxwidgets_directory_iterator **out)
{
    (void)path;
    return from_fd(openat(dirfd(parent->directory), name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW), out);
}
xxwidgets_status xxwidgets_directory_next(xxwidgets_directory_iterator *iterator, char **name, int *kind)
{
    struct dirent *entry;
    struct stat info;
    *name = NULL;
    *kind = XXW_DIRECTORY_END;
    for (;;) {
        errno = 0;
        entry = readdir(iterator->directory);
        if (!entry)
            return errno ? XXWIDGETS_PLATFORM_ERROR : XXWIDGETS_OK;
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
            break;
    }
    *name = malloc(strlen(entry->d_name) + 1);
    if (!*name)
        return XXWIDGETS_OUT_OF_MEMORY;
    strcpy(*name, entry->d_name);
    if (fstatat(dirfd(iterator->directory), entry->d_name, &info, AT_SYMLINK_NOFOLLOW))
        *kind = XXW_DIRECTORY_ERROR;
    else if (S_ISDIR(info.st_mode))
        *kind = XXW_DIRECTORY_SUBDIR;
    else if (S_ISREG(info.st_mode))
        *kind = XXW_DIRECTORY_FILE;
    else
        *kind = XXW_DIRECTORY_SKIP;
    return XXWIDGETS_OK;
}
void xxwidgets_directory_close(xxwidgets_directory_iterator *iterator)
{
    if (iterator) {
        closedir(iterator->directory);
        free(iterator);
    }
}
