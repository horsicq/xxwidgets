/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#include "xxwidget_systemprocessesviewer_memory.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/sysmacros.h>
#elif defined(__APPLE__)
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_region.h>
#include <sys/proc_info.h>
#endif

typedef struct process_memory_native {
#if defined(__linux__)
    int proc, memory;
#elif defined(__APPLE__)
    task_t task;
#else
    int unused;
#endif
} process_memory_native;

static xxwidgets_status process_memory_errno(int error)
{
    if (error == ENOMEM) return XXWIDGETS_OUT_OF_MEMORY;
    if (error == EACCES || error == EPERM || error == EBADF || error == ESRCH || error == ENOENT || error == ENOTSUP)
        return XXWIDGETS_UNAVAILABLE;
    return XXWIDGETS_PLATFORM_ERROR;
}

#if defined(__linux__)
static uint64_t process_memory_start(int proc)
{
    char bytes[65536], *last, *cursor, *end;
    size_t used = 0;
    uint64_t start;
    int saved = 0, field, fd = openat(proc, "stat", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    while (used < sizeof(bytes) - 1) {
        ssize_t got = read(fd, bytes + used, sizeof(bytes) - 1 - used);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) { saved = errno; break; }
        if (!got) break;
        used += (size_t)got;
    }
    close(fd);
    if (saved) { errno = saved; return 0; }
    if (!used || used == sizeof(bytes) - 1) return 0;
    bytes[used] = '\0';
    last = strrchr(bytes, ')');
    if (!last || last[1] != ' ') return 0;
    cursor = last + 2;
    for (field = 3; field < 22; ++field) {
        while (*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != '\n') ++cursor;
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (!*cursor || *cursor == '\n') return 0;
    }
    if (*cursor < '0' || *cursor > '9') return 0;
    errno = 0;
    start = (uint64_t)strtoull(cursor, &end, 10);
    return errno || end == cursor || (*end && *end != ' ' && *end != '\t' && *end != '\n') ? 0 : start;
}

static xxwidgets_status process_memory_region(int proc, const struct stat *executable, uint64_t *base, uint64_t *size)
{
    char line[16384], permissions[5];
    uint64_t first_base = 0, first_size = 0;
    size_t lines = 0;
    int fd = openat(proc, "maps", O_RDONLY | O_CLOEXEC);
    FILE *stream;
    xxwidgets_status status = XXWIDGETS_UNAVAILABLE;
    if (fd < 0) return process_memory_errno(errno);
    stream = fdopen(fd, "r");
    if (!stream) { int error = errno; close(fd); return process_memory_errno(error); }
    while (fgets(line, sizeof(line), stream)) {
        unsigned long long start, end, offset, inode;
        unsigned device_major, device_minor;
        size_t length = strlen(line);
        if (++lines > 1048576 || ((!length || line[length - 1] != '\n') && !feof(stream))) break;
        if (sscanf(line, "%llx-%llx %4s %llx %x:%x %llu", &start, &end, permissions, &offset,
                   &device_major, &device_minor, &inode) != 7) { status = XXWIDGETS_PLATFORM_ERROR; break; }
        (void)offset;
        if (permissions[0] != 'r' || start >= end || end - 1U > (unsigned long long)INT64_MAX) continue;
        if (!first_size) { first_base = (uint64_t)start; first_size = (uint64_t)(end - start); }
        if (executable && (uint64_t)inode == (uint64_t)executable->st_ino && device_major == major(executable->st_dev) && device_minor == minor(executable->st_dev)) {
            *base = (uint64_t)start;
            *size = (uint64_t)(end - start);
            status = XXWIDGETS_OK;
            break;
        }
    }
    if (ferror(stream)) status = XXWIDGETS_PLATFORM_ERROR;
    else if (status == XXWIDGETS_UNAVAILABLE && first_size) { *base = first_base; *size = first_size; status = XXWIDGETS_OK; }
    fclose(stream);
    return status;
}

#elif defined(__APPLE__)
static uint64_t process_memory_start(pid_t pid)
{
    struct proc_bsdinfo info;
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != (int)sizeof(info) ||
        info.pbi_start_tvsec > (UINT64_MAX - info.pbi_start_tvusec) / 1000000) return 0;
    return (uint64_t)info.pbi_start_tvsec * 1000000 + info.pbi_start_tvusec;
}

static xxwidgets_status process_memory_mach_error(kern_return_t error)
{
    if (error == KERN_NO_SPACE || error == KERN_RESOURCE_SHORTAGE) return XXWIDGETS_OUT_OF_MEMORY;
    return XXWIDGETS_UNAVAILABLE;
}

static xxwidgets_status process_memory_region(task_t task, uint64_t *base, uint64_t *size)
{
    mach_vm_address_t address = 0;
    natural_t depth = 0;
    size_t regions;
    for (regions = 0; regions < 1048576; ++regions) {
        vm_region_submap_info_data_64_t info = {0};
        mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;
        mach_vm_size_t length = 0;
        kern_return_t result = mach_vm_region_recurse(task, &address, &length, &depth,
                                                     (vm_region_recurse_info_t)&info, &count);
        if (result != KERN_SUCCESS) return process_memory_mach_error(result);
        if (count < VM_REGION_SUBMAP_INFO_V0_COUNT_64) return XXWIDGETS_UNAVAILABLE;
        if (info.is_submap) {
            if (depth >= 64) return XXWIDGETS_UNAVAILABLE;
            ++depth;
            continue;
        }
        if (!length || length - 1 > UINT64_MAX - address) return XXWIDGETS_UNAVAILABLE;
        if (info.protection & VM_PROT_READ) {
            /* __PAGEZERO is inaccessible and is skipped by this test. Only
             * metadata is inspected; HexView performs later reads via xx_io. */
            *base = (uint64_t)address;
            *size = (uint64_t)length;
            return XXWIDGETS_OK;
        }
        if (length > UINT64_MAX - address) return XXWIDGETS_UNAVAILABLE;
        address += length;
    }
    return XXWIDGETS_UNAVAILABLE;
}
#endif

void xxwidgets_process_memory_close(xxwidgets_process_memory *memory)
{
    int saved = errno;
    process_memory_native *native;
    if (!memory) return;
    native = (process_memory_native *)memory->native;
    if (memory->device) (void)xx_io_process_close(memory->device);
    if (native) {
#if defined(__linux__)
        if (native->memory >= 0) close(native->memory);
        if (native->proc >= 0) close(native->proc);
#elif defined(__APPLE__)
        if (native->task != MACH_PORT_NULL) (void)mach_port_deallocate(mach_task_self(), native->task);
#endif
        free(native);
    }
    memset(memory, 0, sizeof(*memory));
    errno = saved;
}

xxwidgets_status xxwidgets_process_memory_open(const xxwidgets_process_info *process, xxwidgets_process_memory *memory)
{
    process_memory_native *native;
    xxwidgets_status status = XXWIDGETS_UNAVAILABLE;
    if (!memory) return XXWIDGETS_INVALID_ARGUMENT;
    memset(memory, 0, sizeof(*memory));
    if (!process || process->pid <= 1 || process->pid > INT_MAX) return XXWIDGETS_INVALID_ARGUMENT;
    if (!process->start_time) return XXWIDGETS_UNAVAILABLE;
    native = (process_memory_native *)calloc(1, sizeof(*native));
    if (!native) return XXWIDGETS_OUT_OF_MEMORY;
    memory->native = native;
#if defined(__linux__)
    {
        char path[64];
        struct stat executable, after;
        int has_executable;
        native->proc = native->memory = -1;
        snprintf(path, sizeof(path), "/proc/%" PRIu64, process->pid);
        native->proc = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (native->proc < 0) { status = process_memory_errno(errno); goto fail; }
        if (process_memory_start(native->proc) != process->start_time) goto fail;
        has_executable = fstatat(native->proc, "exe", &executable, 0) == 0;
        native->memory = openat(native->proc, "mem", O_RDONLY | O_CLOEXEC);
        if (native->memory < 0) { status = process_memory_errno(errno); goto fail; }
        if (process_memory_start(native->proc) != process->start_time) goto fail;
        status = process_memory_region(native->proc, has_executable ? &executable : NULL, &memory->base, &memory->size);
        if (status != XXWIDGETS_OK) goto fail;
        if (process_memory_start(native->proc) != process->start_time ||
            (has_executable && (fstatat(native->proc, "exe", &after, 0) || after.st_dev != executable.st_dev || after.st_ino != executable.st_ino))) {
            status = XXWIDGETS_UNAVAILABLE; goto fail;
        }
        memory->device = xx_io_process_open_handle_ex((void *)(intptr_t)native->memory, XX_IO_PROCESS_READ);
    }
#elif defined(__APPLE__)
    {
        kern_return_t result;
        if (process_memory_start((pid_t)process->pid) != process->start_time) goto fail;
        if (process->pid == (uint64_t)getpid()) {
            native->task = mach_task_self();
            result = mach_port_mod_refs(mach_task_self(), native->task, MACH_PORT_RIGHT_SEND, 1);
            if (result != KERN_SUCCESS) { native->task = MACH_PORT_NULL; status = process_memory_mach_error(result); goto fail; }
        } else {
            result = task_for_pid(mach_task_self(), (pid_t)process->pid, &native->task);
            if (result != KERN_SUCCESS) { native->task = MACH_PORT_NULL; status = process_memory_mach_error(result); goto fail; }
        }
        if (process_memory_start((pid_t)process->pid) != process->start_time) goto fail;
        status = process_memory_region(native->task, &memory->base, &memory->size);
        if (status != XXWIDGETS_OK) goto fail;
        if (process_memory_start((pid_t)process->pid) != process->start_time) { status = XXWIDGETS_UNAVAILABLE; goto fail; }
        memory->device = xx_io_process_open_handle_ex((void *)(uintptr_t)native->task, XX_IO_PROCESS_READ);
    }
#else
    goto fail;
#endif
    if (!memory->device) { status = process_memory_errno(errno); goto fail; }
    return XXWIDGETS_OK;
fail:
    xxwidgets_process_memory_close(memory);
    return status;
}
#endif
