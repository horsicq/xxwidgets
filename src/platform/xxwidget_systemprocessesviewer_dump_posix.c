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
#include "xxwidgets/xxwidget_systemprocessesviewer.h"
#include "xxfclib/io/xx_io.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#include <sys/sysmacros.h>
extern long syscall(long number, ...);
#elif defined(__APPLE__)
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/task_info.h>
#include <sys/proc_info.h>
#include <sys/stdio.h>
#endif

/* Raw image offsets are virtual-address offsets, not executable file offsets.
 * Gaps and inaccessible mappings are sparse zeroes. Refuse corrupt or wildly
 * fragmented layouts rather than allocating an unbounded staging file. */
#define PROCESS_DUMP_PATH_LIMIT ((size_t)1048576)
#define PROCESS_DUMP_MAP_LIMIT ((size_t)1048576)
#define PROCESS_DUMP_IMAGE_LIMIT (UINT64_C(1) << 40)
#define PROCESS_DUMP_CHUNK ((size_t)65536)

typedef struct dump_stage {
    int parent, directory;
    char name[64];
    char *destination;
    dev_t device;
    ino_t inode;
} dump_stage;

typedef struct dump_mapping {
    uint64_t start, end;
    uint64_t file_offset; /* UINT64_MAX for anonymous ELF BSS. */
    int readable;
} dump_mapping;

typedef struct dump_mappings {
    dump_mapping *items;
    size_t count, capacity;
    uint64_t base, end;
} dump_mappings;

static xxwidgets_status dump_errno(int error)
{
    if (error == ENOMEM) return XXWIDGETS_OUT_OF_MEMORY;
    if (error == EEXIST) return XXWIDGETS_BUSY;
    if (error == EACCES || error == EPERM || error == ESRCH || error == ENOENT || error == ENOTSUP || error == ENOSYS)
        return XXWIDGETS_UNAVAILABLE;
    return XXWIDGETS_PLATFORM_ERROR;
}

static size_t dump_length(const char *text)
{
    size_t i;
    for (i = 0; i <= PROCESS_DUMP_PATH_LIMIT; ++i) if (!text[i]) return i;
    return SIZE_MAX;
}

static uint64_t dump_integer(const unsigned char *bytes, size_t width, int big_endian)
{
    size_t i;
    uint64_t value = 0;
    for (i = 0; i < width; ++i) value = (value << 8) | bytes[big_endian ? i : width - i - 1];
    return value;
}

static int dump_read_at(int fd, uint64_t offset, void *buffer, size_t length)
{
    size_t done = 0;
    if (offset > INT64_MAX || length > (uint64_t)INT64_MAX - offset) { errno = EOVERFLOW; return 0; }
    while (done < length) {
        ssize_t got = pread(fd, (char *)buffer + done, length - done, (off_t)(offset + done));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) { if (!got) errno = EIO; return 0; }
        done += (size_t)got;
    }
    return 1;
}

static int dump_write_at(int fd, uint64_t offset, const void *buffer, size_t length)
{
    size_t done = 0;
    if (offset > INT64_MAX || length > (uint64_t)INT64_MAX - offset) { errno = EOVERFLOW; return 0; }
    while (done < length) {
        ssize_t put = pwrite(fd, (const char *)buffer + done, length - done, (off_t)(offset + done));
        if (put < 0 && errno == EINTR) continue;
        if (put <= 0) { if (!put) errno = EIO; return 0; }
        done += (size_t)put;
    }
    return 1;
}

#if defined(__linux__)
/* A proc directory descriptor pins the PID object. Opening stat/mem through
 * it cannot silently switch to a new process that later reuses the PID. */
static uint64_t dump_linux_start_at(int proc)
{
    char bytes[65536], *cursor, *last, *end;
    size_t used = 0;
    uint64_t value;
    int fd = openat(proc, "stat", O_RDONLY | O_CLOEXEC), field;
    if (fd < 0) return 0;
    while (used < sizeof(bytes) - 1) {
        ssize_t got = read(fd, bytes + used, sizeof(bytes) - 1 - used);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        used += (size_t)got;
    }
    close(fd);
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
    errno = 0;
    value = (uint64_t)strtoull(cursor, &end, 10);
    return errno || end == cursor || (*end && *end != ' ' && *end != '\n' && *end != '\t') ? 0 : value;
}
#endif

static int dump_identity(const xxwidgets_process_info *process)
{
#if defined(__linux__)
    char path[64];
    int proc;
    uint64_t start;
    snprintf(path, sizeof(path), "/proc/%" PRIu64, process->pid);
    proc = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (proc < 0) return 0;
    start = dump_linux_start_at(proc);
    close(proc);
    return start && start == process->start_time;
#elif defined(__APPLE__)
    struct proc_bsdinfo info;
    uint64_t start;
    if (proc_pidinfo((pid_t)process->pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != (int)sizeof(info) ||
        info.pbi_start_tvsec > (UINT64_MAX - info.pbi_start_tvusec) / 1000000) return 0;
    start = (uint64_t)info.pbi_start_tvsec * 1000000 + info.pbi_start_tvusec;
    return start && start == process->start_time;
#else
    (void)process;
    return 0;
#endif
}

static int dump_random(unsigned char bytes[16])
{
    size_t done = 0;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    while (done < 16) {
        ssize_t got = read(fd, bytes + done, 16 - done);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) { close(fd); return 0; }
        done += (size_t)got;
    }
    close(fd);
    return 1;
}

static void dump_stage_cleanup(dump_stage *stage, const char *output)
{
    struct stat current;
    if (stage->directory >= 0) {
        if (output) (void)unlinkat(stage->directory, output, 0);
        (void)unlinkat(stage->directory, "debugger.log", 0);
        close(stage->directory);
        stage->directory = -1;
    }
    /* Remove only the private directory we created, even if a parent entry
     * was renamed by another actor while the dump was running. */
    if (stage->parent >= 0) {
        if (stage->name[0] && fstatat(stage->parent, stage->name, &current, AT_SYMLINK_NOFOLLOW) == 0 &&
            current.st_dev == stage->device && current.st_ino == stage->inode)
            (void)unlinkat(stage->parent, stage->name, AT_REMOVEDIR);
        close(stage->parent);
        stage->parent = -1;
    }
    free(stage->destination);
    stage->destination = NULL;
}

static xxwidgets_status dump_stage_create(dump_stage *stage, const char *destination)
{
    char *path, *slash, *directory, *name;
    size_t length = dump_length(destination), i;
    unsigned attempt;
    struct stat info;
    stage->parent = stage->directory = -1;
    if (!length || length > PROCESS_DUMP_PATH_LIMIT) return XXWIDGETS_INVALID_ARGUMENT;
    path = (char *)malloc(length + 1);
    if (!path) return XXWIDGETS_OUT_OF_MEMORY;
    memcpy(path, destination, length + 1);
    slash = strrchr(path, '/');
    if (!slash) { directory = "."; name = path; }
    else {
        name = slash + 1;
        if (slash == path) directory = "/";
        else { *slash = '\0'; directory = path; }
    }
    if (!*name || !strcmp(name, ".") || !strcmp(name, "..")) { free(path); return XXWIDGETS_INVALID_ARGUMENT; }
    stage->destination = (char *)malloc(strlen(name) + 1);
    if (!stage->destination) { free(path); return XXWIDGETS_OUT_OF_MEMORY; }
    strcpy(stage->destination, name);
    stage->parent = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    free(path);
    if (stage->parent < 0) return dump_errno(errno);
    for (attempt = 0; attempt < 32; ++attempt) {
        unsigned char random[16];
        if (!dump_random(random)) return XXWIDGETS_PLATFORM_ERROR;
        memcpy(stage->name, ".xxwidgets-dump-", 16);
        for (i = 0; i < 16; ++i) snprintf(stage->name + 16 + i * 2, 3, "%02x", random[i]);
        if (mkdirat(stage->parent, stage->name, 0700) == 0) break;
        if (errno != EEXIST) { stage->name[0] = '\0'; return dump_errno(errno); }
        stage->name[0] = '\0';
    }
    if (attempt == 32) return XXWIDGETS_BUSY;
    if (fstatat(stage->parent, stage->name, &info, AT_SYMLINK_NOFOLLOW) || !S_ISDIR(info.st_mode) || info.st_uid != geteuid())
        return XXWIDGETS_PLATFORM_ERROR;
    stage->device = info.st_dev;
    stage->inode = info.st_ino;
    stage->directory = openat(stage->parent, stage->name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (stage->directory < 0) return dump_errno(errno);
    if (fstat(stage->directory, &info) || !S_ISDIR(info.st_mode) || info.st_uid != geteuid() ||
        stage->device != info.st_dev || stage->inode != info.st_ino) return XXWIDGETS_PLATFORM_ERROR;
    if (fchmod(stage->directory, 0700)) return dump_errno(errno);
    return XXWIDGETS_OK;
}

static xxwidgets_status dump_publish(dump_stage *stage, const char *output, int overwrite)
{
    int result;
    if (overwrite) result = renameat(stage->directory, output, stage->parent, stage->destination);
    else {
#if defined(__linux__) && defined(SYS_renameat2)
        result = (int)syscall(SYS_renameat2, stage->directory, output, stage->parent, stage->destination, 1U /* RENAME_NOREPLACE */);
        if (result && (errno == ENOSYS || errno == EINVAL || errno == ENOTSUP))
#elif defined(__APPLE__) && defined(RENAME_EXCL)
        result = renameatx_np(stage->directory, output, stage->parent, stage->destination, RENAME_EXCL);
        if (result && (errno == ENOSYS || errno == EINVAL || errno == ENOTSUP))
#endif
        {
            result = linkat(stage->directory, output, stage->parent, stage->destination, 0);
            if (!result) (void)unlinkat(stage->directory, output, 0);
        }
    }
    if (result) return dump_errno(errno);
    /* Rename/link is the commit point. Directory fsync is best effort because
     * some supported filesystems do not implement it. */
    (void)fsync(stage->parent);
    return XXWIDGETS_OK;
}

static int dump_range(uint64_t total, uint64_t offset, uint64_t size)
{
    return offset <= total && size <= total - offset;
}

static int dump_validate_core(int fd)
{
    struct stat info;
    unsigned char header[64], command[72];
    uint64_t total, tag, count, offset, stride, i;
    int big_endian;
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size < 28 || !dump_read_at(fd, 0, header, 28)) return 0;
    total = (uint64_t)info.st_size;
    tag = dump_integer(header, 4, 1);
#if defined(__linux__)
    if (tag != UINT64_C(0x7f454c46) || (header[4] != 1 && header[4] != 2) || (header[5] != 1 && header[5] != 2) || header[6] != 1) return 0;
    big_endian = header[5] == 2;
    if (dump_integer(header + 16, 2, big_endian) != 4 ||
        !dump_read_at(fd, 0, header, header[4] == 2 ? 64 : 52)) return 0;
    if (header[4] == 2) {
        offset = dump_integer(header + 32, 8, big_endian);
        stride = dump_integer(header + 54, 2, big_endian);
        count = dump_integer(header + 56, 2, big_endian);
    } else {
        offset = dump_integer(header + 28, 4, big_endian);
        stride = dump_integer(header + 42, 2, big_endian);
        count = dump_integer(header + 44, 2, big_endian);
    }
    if (count == 65535) {
        uint64_t section = dump_integer(header + (header[4] == 2 ? 40 : 32), header[4] == 2 ? 8 : 4, big_endian);
        size_t size = header[4] == 2 ? 64 : 40;
        if (!dump_range(total, section, size) || !dump_read_at(fd, section, command, size)) return 0;
        count = dump_integer(command + (header[4] == 2 ? 44 : 28), 4, big_endian);
    }
    if (!count || count > PROCESS_DUMP_MAP_LIMIT || stride < (header[4] == 2 ? 56U : 32U) ||
        offset > total || count > (total - offset) / stride) return 0;
    for (i = 0; i < count; ++i) {
        uint64_t data_offset, data_size;
        if (!dump_read_at(fd, offset + i * stride, command, header[4] == 2 ? 56 : 32)) return 0;
        data_offset = dump_integer(command + (header[4] == 2 ? 8 : 4), header[4] == 2 ? 8 : 4, big_endian);
        data_size = dump_integer(command + (header[4] == 2 ? 32 : 16), header[4] == 2 ? 8 : 4, big_endian);
        if (!dump_range(total, data_offset, data_size)) return 0;
    }
    return 1;
#elif defined(__APPLE__)
    if (tag != UINT64_C(0xfeedface) && tag != UINT64_C(0xcefaedfe) && tag != UINT64_C(0xfeedfacf) && tag != UINT64_C(0xcffaedfe)) return 0;
    big_endian = tag == UINT64_C(0xfeedface) || tag == UINT64_C(0xfeedfacf);
    if (dump_integer(header + 12, 4, big_endian) != 4) return 0;
    count = dump_integer(header + 16, 4, big_endian);
    stride = dump_integer(header + 20, 4, big_endian);
    offset = tag == UINT64_C(0xfeedfacf) || tag == UINT64_C(0xcffaedfe) ? 32 : 28;
    if (!count || count > PROCESS_DUMP_MAP_LIMIT || !dump_range(total, offset, stride)) return 0;
    {
    uint64_t command_end = offset + stride;
    for (i = 0; i < count; ++i) {
        uint64_t size, kind;
        if (!dump_range(command_end, offset, 8) || !dump_read_at(fd, offset, command, 8)) return 0;
        kind = dump_integer(command, 4, big_endian);
        size = dump_integer(command + 4, 4, big_endian);
        if (size < 8 || size % 4 || !dump_range(command_end, offset, size)) return 0;
        if (kind == 1 || kind == 0x19 || kind == 0x31) {
            size_t minimum = kind == 1 ? 56 : (kind == 0x19 ? 72 : 40);
            uint64_t data_offset, data_size;
            if (size < minimum || !dump_read_at(fd, offset, command, minimum)) return 0;
            if (kind == 0x31) {
                data_offset = dump_integer(command + 24, 8, big_endian);
                data_size = dump_integer(command + 32, 8, big_endian);
            } else {
                data_offset = dump_integer(command + (kind == 0x19 ? 40 : 32), kind == 0x19 ? 8 : 4, big_endian);
                data_size = dump_integer(command + (kind == 0x19 ? 48 : 36), kind == 0x19 ? 8 : 4, big_endian);
            }
            if (!dump_range(total, data_offset, data_size)) return 0;
        }
        offset += size;
    }
    return offset == command_end;
    }
#else
    (void)tag; (void)count; (void)offset; (void)stride; (void)i; (void)big_endian; (void)command;
    return 0;
#endif
}

/* gcore is invoked directly with a private cwd and a fixed basename. The
 * user's destination never enters a debugger command or shell expression. */
static xxwidgets_status dump_core(const xxwidgets_process_info *process, dump_stage *stage, char output[64], xx_pd_struct *pd)
{
    char pid[32];
    int log, null_input, errors[2], child_status = 0, exec_error = 0;
    ssize_t received;
    pid_t child, waited;
    xxwidgets_status status;
    snprintf(pid, sizeof(pid), "%" PRIu64, process->pid);
#if defined(__APPLE__)
    strcpy(output, "core");
#else
    snprintf(output, 64, "core.%" PRIu64, process->pid);
#endif
    if (xx_pd_is_stopped(pd) || !dump_identity(process)) return XXWIDGETS_UNAVAILABLE;
    log = openat(stage->directory, "debugger.log", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (log < 0) return dump_errno(errno);
    null_input = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (null_input < 0) { close(log); return dump_errno(errno); }
    if (pipe(errors)) { close(log); close(null_input); return XXWIDGETS_PLATFORM_ERROR; }
    if (fcntl(errors[0], F_SETFD, FD_CLOEXEC) || fcntl(errors[1], F_SETFD, FD_CLOEXEC)) {
        close(errors[0]); close(errors[1]); close(log); close(null_input);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    child = fork();
    if (child < 0) {
        status = dump_errno(errno);
        close(errors[0]); close(errors[1]); close(log); close(null_input);
        return status;
    }
    if (!child) {
        int put;
        close(errors[0]);
        /* A private process group lets cancellation stop the debugger and its
         * helper children, without signalling the inspected process. */
        if (setpgid(0, 0) || fchdir(stage->directory) || dup2(null_input, STDIN_FILENO) < 0 ||
            dup2(log, STDOUT_FILENO) < 0 || dup2(log, STDERR_FILENO) < 0) exec_error = errno;
        else {
#if defined(__APPLE__)
            char *const arguments[] = {"/usr/bin/gcore", "-s", "-x", "full", "-o", "core", pid, NULL};
            execv(arguments[0], arguments);
#else
            char *const arguments[] = {"gcore", "-a", "-o", "core", pid, NULL};
            execvp(arguments[0], arguments);
#endif
            exec_error = errno;
        }
        do { put = (int)write(errors[1], &exec_error, sizeof(exec_error)); } while (put < 0 && errno == EINTR);
        _exit(127);
    }
    close(errors[1]); close(log); close(null_input);
    (void)setpgid(child, child);
    {
        int level = xx_pd_enter_level(pd, 0, "Capturing process memory");
        int cancelling = 0, elapsed = 0;
        struct timespec delay = {0, 20000000};
        for (;;) {
            waited = waitpid(child, &child_status, WNOHANG);
            if (waited == child || (waited < 0 && errno != EINTR)) break;
            if (xx_pd_is_stopped(pd)) {
                if (!cancelling) { (void)kill(-child, SIGTERM); cancelling = 1; }
                /* A debugger normally detaches during termination. Reap even
                 * an unresponsive helper before cleaning its staging folder. */
                if (++elapsed == 100) (void)kill(-child, SIGKILL);
            }
            (void)nanosleep(&delay, NULL);
        }
        xx_pd_leave_level(pd, level);
    }
    do { received = read(errors[0], &exec_error, sizeof(exec_error)); } while (received < 0 && errno == EINTR);
    close(errors[0]);
    if (xx_pd_is_stopped(pd)) return XXWIDGETS_UNAVAILABLE;
    if (received > 0) return dump_errno(exec_error);
    if (received < 0 || waited < 0 || !WIFEXITED(child_status) || WEXITSTATUS(child_status)) return XXWIDGETS_UNAVAILABLE;
    return dump_identity(process) ? XXWIDGETS_OK : XXWIDGETS_UNAVAILABLE;
}

static xxwidgets_status dump_add_mapping(dump_mappings *maps, uint64_t start, uint64_t end, int readable)
{
    if (start >= end || end > INT64_MAX) return XXWIDGETS_UNAVAILABLE;
    if (maps->count == maps->capacity) {
        size_t next = maps->capacity ? maps->capacity * 2 : 16;
        dump_mapping *items;
        if (next > PROCESS_DUMP_MAP_LIMIT) return XXWIDGETS_UNAVAILABLE;
        items = (dump_mapping *)realloc(maps->items, next * sizeof(*items));
        if (!items) return XXWIDGETS_OUT_OF_MEMORY;
        maps->items = items;
        maps->capacity = next;
    }
    maps->items[maps->count++] = (dump_mapping){start, end, 0, readable};
    if (start < maps->base) maps->base = start;
    if (end > maps->end) maps->end = end;
    return maps->end - maps->base <= PROCESS_DUMP_IMAGE_LIMIT ? XXWIDGETS_OK : XXWIDGETS_UNAVAILABLE;
}

#if defined(__linux__)
static xxwidgets_status dump_linux_maps(int proc, const struct stat *executable, dump_mappings *maps)
{
    char line[16384], permissions[5];
    int fd = openat(proc, "maps", O_RDONLY | O_CLOEXEC);
    FILE *stream;
    xxwidgets_status status = XXWIDGETS_OK;
    maps->base = UINT64_MAX;
    if (fd < 0) return dump_errno(errno);
    stream = fdopen(fd, "r");
    if (!stream) { close(fd); return dump_errno(errno); }
    while (fgets(line, sizeof(line), stream)) {
        unsigned long long start, end, file_offset, inode;
        unsigned device_major, device_minor;
        size_t length = strlen(line);
        if ((!length || line[length - 1] != '\n') && !feof(stream)) { status = XXWIDGETS_UNAVAILABLE; break; }
        if (sscanf(line, "%llx-%llx %4s %llx %x:%x %llu", &start, &end, permissions, &file_offset,
                   &device_major, &device_minor, &inode) != 7) { status = XXWIDGETS_PLATFORM_ERROR; break; }
        if ((uint64_t)inode != (uint64_t)executable->st_ino || device_major != major(executable->st_dev) || device_minor != minor(executable->st_dev)) continue;
        status = dump_add_mapping(maps, (uint64_t)start, (uint64_t)end, permissions[0] == 'r');
        if (status != XXWIDGETS_OK) break;
        maps->items[maps->count - 1].file_offset = (uint64_t)file_offset;
    }
    if (ferror(stream) && status == XXWIDGETS_OK) status = XXWIDGETS_PLATFORM_ERROR;
    fclose(stream);
    if (status == XXWIDGETS_OK && !maps->count) status = XXWIDGETS_UNAVAILABLE;
    return status;
}

static int dump_mapping_order(const void *left, const void *right)
{
    const dump_mapping *a = (const dump_mapping *)left, *b = (const dump_mapping *)right;
    if (a->start != b->start) return a->start < b->start ? -1 : 1;
    return a->end == b->end ? 0 : (a->end < b->end ? -1 : 1);
}

static int dump_linux_file_address(const dump_mappings *maps, uint64_t offset, uint64_t size, uint64_t *address)
{
    size_t i;
    for (i = 0; i < maps->count; ++i) {
        const dump_mapping *map = &maps->items[i];
        uint64_t delta;
        if (map->file_offset == UINT64_MAX || offset < map->file_offset) continue;
        delta = offset - map->file_offset;
        if (delta <= map->end - map->start && size <= map->end - map->start - delta) {
            *address = map->start + delta;
            return 1;
        }
    }
    return 0;
}

/* Determine only the main ELF's anonymous zero-fill extents from its live
 * headers. The executable on disk is never opened for byte copying. Include
 * only actual anonymous VM mappings intersecting those PT_LOAD extents. */
static xxwidgets_status dump_linux_add_bss(int proc, xx_io_device *memory, dump_mappings *maps)
{
    unsigned char header[64], *programs = NULL;
    uint64_t address, table, stride, count, image = 0, preferred = UINT64_MAX;
    dump_mappings bss = {0};
    xxwidgets_status status = XXWIDGETS_UNAVAILABLE;
    size_t i, used;
    int wide, big_endian, fd = -1;
    FILE *stream = NULL;
    bss.base = UINT64_MAX;
    if (!dump_linux_file_address(maps, 0, sizeof(header), &address)) return XXWIDGETS_OK;
    image = address;
    if (!xx_io_process_read_exact_at(memory, image, header, sizeof(header))) return dump_errno(errno);
    if (memcmp(header, "\177ELF", 4)) return XXWIDGETS_OK; /* Non-ELF: inode-backed maps only. */
    if ((header[4] != 1 && header[4] != 2) || (header[5] != 1 && header[5] != 2)) return XXWIDGETS_UNAVAILABLE;
    wide = header[4] == 2;
    big_endian = header[5] == 2;
    if (dump_integer(header + 16, 2, big_endian) != 2 && dump_integer(header + 16, 2, big_endian) != 3) return XXWIDGETS_UNAVAILABLE;
    table = dump_integer(header + (wide ? 32 : 28), wide ? 8 : 4, big_endian);
    stride = dump_integer(header + (wide ? 54 : 42), 2, big_endian);
    count = dump_integer(header + (wide ? 56 : 44), 2, big_endian);
    if (!count || count == 65535 || stride < (wide ? 56U : 32U) || count > UINT64_C(16777216) / stride ||
        !dump_linux_file_address(maps, table, count * stride, &address)) return XXWIDGETS_UNAVAILABLE;
    programs = (unsigned char *)malloc((size_t)(count * stride));
    if (!programs) return XXWIDGETS_OUT_OF_MEMORY;
    if (!xx_io_process_read_exact_at(memory, address, programs, (size_t)(count * stride))) { status = dump_errno(errno); goto done; }
    for (i = 0; i < count; ++i) {
        const unsigned char *program = programs + i * stride;
        uint64_t offset, virtual_address, file_size;
        if (dump_integer(program, 4, big_endian) != 1) continue;
        offset = dump_integer(program + (wide ? 8 : 4), wide ? 8 : 4, big_endian);
        virtual_address = dump_integer(program + (wide ? 16 : 8), wide ? 8 : 4, big_endian);
        file_size = dump_integer(program + (wide ? 32 : 16), wide ? 8 : 4, big_endian);
        if (!offset && file_size && virtual_address < preferred) preferred = virtual_address;
    }
    if (preferred == UINT64_MAX) goto done;
    for (i = 0; i < count; ++i) {
        const unsigned char *program = programs + i * stride;
        uint64_t virtual_address, file_size, memory_size, runtime;
        if (dump_integer(program, 4, big_endian) != 1) continue;
        virtual_address = dump_integer(program + (wide ? 16 : 8), wide ? 8 : 4, big_endian);
        file_size = dump_integer(program + (wide ? 32 : 16), wide ? 8 : 4, big_endian);
        memory_size = dump_integer(program + (wide ? 40 : 20), wide ? 8 : 4, big_endian);
        if (file_size > memory_size || virtual_address < preferred || virtual_address - preferred > UINT64_MAX - image) {
            status = XXWIDGETS_UNAVAILABLE; goto done;
        }
        runtime = image + (virtual_address - preferred);
        if (memory_size > UINT64_MAX - runtime) { status = XXWIDGETS_UNAVAILABLE; goto done; }
        if (memory_size > file_size) {
            status = dump_add_mapping(&bss, runtime + file_size, runtime + memory_size, 1);
            if (status != XXWIDGETS_OK) goto done;
        }
    }
    if (!bss.count) { status = XXWIDGETS_OK; goto done; }
    qsort(bss.items, bss.count, sizeof(*bss.items), dump_mapping_order);
    used = 0;
    for (i = 0; i < bss.count; ++i) {
        if (used && bss.items[i].start <= bss.items[used - 1].end) {
            if (bss.items[i].end > bss.items[used - 1].end) bss.items[used - 1].end = bss.items[i].end;
        } else bss.items[used++] = bss.items[i];
    }
    bss.count = used;
    fd = openat(proc, "maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0) { status = dump_errno(errno); goto done; }
    stream = fdopen(fd, "r");
    if (!stream) { status = dump_errno(errno); goto done; }
    fd = -1; /* Owned by stream. */
    status = XXWIDGETS_OK;
    {
        char line[16384], permissions[5];
        while (fgets(line, sizeof(line), stream)) {
            unsigned long long start, end, offset, inode;
            unsigned device_major, device_minor;
            size_t length = strlen(line);
            if ((!length || line[length - 1] != '\n') && !feof(stream)) { status = XXWIDGETS_UNAVAILABLE; break; }
            if (sscanf(line, "%llx-%llx %4s %llx %x:%x %llu", &start, &end, permissions, &offset,
                       &device_major, &device_minor, &inode) != 7) { status = XXWIDGETS_PLATFORM_ERROR; break; }
            (void)offset; (void)device_major; (void)device_minor;
            if (inode || start >= end) continue;
            for (i = 0; i < bss.count; ++i) {
                uint64_t first = (uint64_t)start > bss.items[i].start ? (uint64_t)start : bss.items[i].start;
                uint64_t last = (uint64_t)end < bss.items[i].end ? (uint64_t)end : bss.items[i].end;
                if (first >= last) continue;
                status = dump_add_mapping(maps, first, last, permissions[0] == 'r');
                if (status != XXWIDGETS_OK) break;
                maps->items[maps->count - 1].file_offset = UINT64_MAX;
            }
            if (status != XXWIDGETS_OK) break;
        }
        if (ferror(stream) && status == XXWIDGETS_OK) status = XXWIDGETS_PLATFORM_ERROR;
    }
    if (status == XXWIDGETS_OK) {
        qsort(maps->items, maps->count, sizeof(*maps->items), dump_mapping_order);
        for (i = 1; i < maps->count; ++i) if (maps->items[i - 1].end > maps->items[i].start) { status = XXWIDGETS_BUSY; break; }
    }
done:
    if (stream) fclose(stream);
    if (fd >= 0) close(fd);
    free(programs); free(bss.items);
    return status;
}

static xxwidgets_status dump_image(const xxwidgets_process_info *process, int output, xx_pd_struct *pd)
{
    char path[64];
    int proc = -1, memory = -1;
    xx_io_device *memory_io = NULL;
    struct stat executable, after;
    dump_mappings maps = {0}, verified = {0};
    unsigned char *buffer = NULL;
    xxwidgets_status status = XXWIDGETS_UNAVAILABLE;
    size_t i;
    int level = -1;
    snprintf(path, sizeof(path), "/proc/%" PRIu64, process->pid);
    proc = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (proc < 0) return dump_errno(errno);
    if (dump_linux_start_at(proc) != process->start_time) goto done;
    if (fstatat(proc, "exe", &executable, 0)) { status = dump_errno(errno); goto done; }
    memory = openat(proc, "mem", O_RDONLY | O_CLOEXEC);
    if (memory < 0) { status = dump_errno(errno); goto done; }
    memory_io = xx_io_process_open_handle_ex((void *)(intptr_t)memory, XX_IO_PROCESS_READ);
    if (!memory_io) { status = dump_errno(errno); goto done; }
    if (dump_linux_start_at(proc) != process->start_time) goto done;
    status = dump_linux_maps(proc, &executable, &maps);
    if (status != XXWIDGETS_OK) goto done;
    status = dump_linux_add_bss(proc, memory_io, &maps);
    if (status != XXWIDGETS_OK) goto done;
    if (ftruncate(output, (off_t)(maps.end - maps.base))) { status = dump_errno(errno); goto done; }
    level = xx_pd_enter_level(pd, maps.end - maps.base, "Dumping executable image");
    buffer = (unsigned char *)malloc(PROCESS_DUMP_CHUNK);
    if (!buffer) { status = XXWIDGETS_OUT_OF_MEMORY; goto done; }
    for (i = 0; i < maps.count; ++i) if (maps.items[i].readable) {
        uint64_t address = maps.items[i].start;
        while (address < maps.items[i].end) {
            size_t amount = maps.items[i].end - address < PROCESS_DUMP_CHUNK ? (size_t)(maps.items[i].end - address) : PROCESS_DUMP_CHUNK;
            if (xx_pd_is_stopped(pd)) { status = XXWIDGETS_UNAVAILABLE; goto done; }
            if (!xx_io_process_read_exact_at(memory_io, address, buffer, amount)) { status = dump_errno(errno); goto done; }
            if (!dump_write_at(output, address - maps.base, buffer, amount)) { status = dump_errno(errno); goto done; }
            address += amount;
            xx_pd_set_current(pd, level, address - maps.base);
        }
    }
    if (fstatat(proc, "exe", &after, 0) || after.st_dev != executable.st_dev || after.st_ino != executable.st_ino ||
        dump_linux_start_at(proc) != process->start_time) { status = XXWIDGETS_UNAVAILABLE; goto done; }
    status = dump_linux_maps(proc, &after, &verified);
    if (status != XXWIDGETS_OK) goto done;
    status = dump_linux_add_bss(proc, memory_io, &verified);
    if (status != XXWIDGETS_OK) goto done;
    if (verified.count != maps.count) { status = XXWIDGETS_BUSY; goto done; }
    for (i = 0; i < maps.count; ++i) {
        if (maps.items[i].start != verified.items[i].start || maps.items[i].end != verified.items[i].end ||
            maps.items[i].readable != verified.items[i].readable || maps.items[i].file_offset != verified.items[i].file_offset) {
            status = XXWIDGETS_BUSY;
            break;
        }
    }
done:
    xx_pd_leave_level(pd, level);
    free(buffer); free(maps.items); free(verified.items);
    if (memory_io) (void)xx_io_process_close(memory_io);
    if (memory >= 0) close(memory);
    close(proc);
    return status;
}

#elif defined(__APPLE__)
static int dump_mach_read(xx_io_device *memory, uint64_t address, void *output, size_t length)
{
    return xx_io_process_read_exact_at(memory, address, output, length);
}

static xxwidgets_status dump_mach_main(task_t task, xx_io_device *memory, uint64_t *base)
{
    task_dyld_info_data_t dyld;
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
    unsigned char prefix[16], entry[24], header[32];
    uint64_t array, images, i;
    size_t pointer, stride;
    if (task_info(task, TASK_DYLD_INFO, (task_info_t)&dyld, &count) != KERN_SUCCESS) return XXWIDGETS_UNAVAILABLE;
    if (dyld.all_image_info_format == TASK_DYLD_ALL_IMAGE_INFO_64) pointer = 8;
    else if (dyld.all_image_info_format == TASK_DYLD_ALL_IMAGE_INFO_32) pointer = 4;
    else return XXWIDGETS_UNAVAILABLE;
    if (!dump_mach_read(memory, dyld.all_image_info_addr, prefix, 8 + pointer)) return XXWIDGETS_UNAVAILABLE;
    images = dump_integer(prefix + 4, 4, 0);
    array = dump_integer(prefix + 8, pointer, 0);
    stride = pointer * 3;
    if (!array) return XXWIDGETS_BUSY; /* dyld was updating its image array. */
    if (!images || images > 65536 || images > (UINT64_MAX - array) / stride) return XXWIDGETS_UNAVAILABLE;
    for (i = 0; i < images; ++i) {
        uint64_t address, magic;
        if (!dump_mach_read(memory, array + i * stride, entry, stride)) return XXWIDGETS_UNAVAILABLE;
        address = dump_integer(entry, pointer, 0);
        if (!address || !dump_mach_read(memory, address, header, sizeof(header))) continue;
        magic = dump_integer(header, 4, 0);
        if ((magic == UINT64_C(0xfeedface) || magic == UINT64_C(0xfeedfacf)) && dump_integer(header + 12, 4, 0) == 2) {
            *base = address;
            return XXWIDGETS_OK;
        }
    }
    return XXWIDGETS_UNAVAILABLE;
}

static xxwidgets_status dump_image(const xxwidgets_process_info *process, int output, xx_pd_struct *pd)
{
    task_t task = MACH_PORT_NULL;
    xx_io_device *memory = NULL;
    task_suspension_token_t token = MACH_PORT_NULL;
    unsigned char header[32], *commands = NULL, *buffer = NULL;
    dump_mappings maps = {0};
    uint64_t image = 0, count, command_bytes, cursor, preferred = UINT64_MAX;
    size_t i, header_size;
    xxwidgets_status status = XXWIDGETS_UNAVAILABLE;
    int level = -1;
    if (task_for_pid(mach_task_self(), (pid_t)process->pid, &task) != KERN_SUCCESS) return XXWIDGETS_UNAVAILABLE;
    if (!dump_identity(process)) goto done;
    if (task_suspend2(task, &token) != KERN_SUCCESS) goto done;
    if (!dump_identity(process)) goto done;
    memory = xx_io_process_open_handle_ex((void *)(uintptr_t)task, XX_IO_PROCESS_READ);
    if (!memory) { status = dump_errno(errno); goto done; }
    status = dump_mach_main(task, memory, &image);
    if (status != XXWIDGETS_OK) goto done;
    if (!dump_mach_read(memory, image, header, sizeof(header))) { status = XXWIDGETS_UNAVAILABLE; goto done; }
    header_size = dump_integer(header, 4, 0) == UINT64_C(0xfeedfacf) ? 32 : 28;
    count = dump_integer(header + 16, 4, 0);
    command_bytes = dump_integer(header + 20, 4, 0);
    if (!count || count > 65536 || command_bytes > UINT64_C(16777216) || count > command_bytes / 8 ||
        image > UINT64_MAX - header_size || command_bytes > UINT64_MAX - image - header_size) { status = XXWIDGETS_UNAVAILABLE; goto done; }
    commands = (unsigned char *)malloc((size_t)command_bytes);
    if (!commands) { status = XXWIDGETS_OUT_OF_MEMORY; goto done; }
    if (!dump_mach_read(memory, image + header_size, commands, (size_t)command_bytes)) { status = XXWIDGETS_UNAVAILABLE; goto done; }
    maps.base = UINT64_MAX;
    cursor = 0;
    for (i = 0; i < count; ++i) {
        uint64_t command, size;
        if (cursor > command_bytes || command_bytes - cursor < 8) { status = XXWIDGETS_UNAVAILABLE; goto done; }
        command = dump_integer(commands + cursor, 4, 0);
        size = dump_integer(commands + cursor + 4, 4, 0);
        if (size < 8 || size > command_bytes - cursor) { status = XXWIDGETS_UNAVAILABLE; goto done; }
        if (command == 1 || command == 0x19) {
            int wide = command == 0x19;
            uint64_t address, length, file_offset, file_size, protection;
            if (size < (wide ? 72U : 56U)) { status = XXWIDGETS_UNAVAILABLE; goto done; }
            address = dump_integer(commands + cursor + 24, wide ? 8 : 4, 0);
            length = dump_integer(commands + cursor + (wide ? 32 : 28), wide ? 8 : 4, 0);
            file_offset = dump_integer(commands + cursor + (wide ? 40 : 32), wide ? 8 : 4, 0);
            file_size = dump_integer(commands + cursor + (wide ? 48 : 36), wide ? 8 : 4, 0);
            protection = dump_integer(commands + cursor + (wide ? 60 : 44), 4, 0);
            if (!file_offset && file_size >= header_size && address < preferred) preferred = address;
            if (length && memcmp(commands + cursor + 8, "__PAGEZERO", 10)) {
                if (address > UINT64_MAX - length) { status = XXWIDGETS_UNAVAILABLE; goto done; }
                status = dump_add_mapping(&maps, address, address + length, (protection & VM_PROT_READ) != 0);
                if (status != XXWIDGETS_OK) goto done;
            }
        }
        cursor += size;
    }
    if (cursor != command_bytes || !maps.count || preferred == UINT64_MAX || maps.base < preferred) { status = XXWIDGETS_UNAVAILABLE; goto done; }
    if (ftruncate(output, (off_t)(maps.end - maps.base))) { status = dump_errno(errno); goto done; }
    level = xx_pd_enter_level(pd, maps.end - maps.base, "Dumping executable image");
    buffer = (unsigned char *)malloc(PROCESS_DUMP_CHUNK);
    if (!buffer) { status = XXWIDGETS_OUT_OF_MEMORY; goto done; }
    for (i = 0; i < maps.count; ++i) if (maps.items[i].readable) {
        uint64_t address = maps.items[i].start;
        if (address - preferred > UINT64_MAX - image || maps.items[i].end - preferred > UINT64_MAX - image) {
            status = XXWIDGETS_UNAVAILABLE; goto done;
        }
        while (address < maps.items[i].end) {
            size_t amount = maps.items[i].end - address < PROCESS_DUMP_CHUNK ? (size_t)(maps.items[i].end - address) : PROCESS_DUMP_CHUNK;
            if (xx_pd_is_stopped(pd)) { status = XXWIDGETS_UNAVAILABLE; goto done; }
            if (!dump_mach_read(memory, image + (address - preferred), buffer, amount)) { status = XXWIDGETS_UNAVAILABLE; goto done; }
            if (!dump_write_at(output, address - maps.base, buffer, amount)) { status = dump_errno(errno); goto done; }
            address += amount;
            xx_pd_set_current(pd, level, address - maps.base);
        }
    }
    status = XXWIDGETS_OK;
done:
    xx_pd_leave_level(pd, level);
    free(commands); free(buffer); free(maps.items);
    if (memory) (void)xx_io_process_close(memory);
    if (token != MACH_PORT_NULL) {
        /* task_resume2 moves/consumes this send-once token. Do not deallocate
         * its old port name afterward: it may already name a reused port. */
        kern_return_t resumed = task_resume2(token);
        if (resumed != KERN_SUCCESS && status == XXWIDGETS_OK) status = XXWIDGETS_PLATFORM_ERROR;
    }
    if (task != MACH_PORT_NULL) (void)mach_port_deallocate(mach_task_self(), task);
    return status;
}
#else
static xxwidgets_status dump_image(const xxwidgets_process_info *process, int output, xx_pd_struct *pd)
{
    (void)process; (void)output; (void)pd;
    return XXWIDGETS_UNAVAILABLE;
}
#endif

xxwidgets_status xxwidget_systemprocessesviewer_dump_process_ex(const xxwidgets_process_info *process, const char *destination,
                                                           xxwidgets_process_dump_type type, int overwrite, xx_pd_struct *pd)
{
    dump_stage stage = {-1, -1, {0}, NULL, 0, 0};
    char output[64] = "image.bin";
    int fd = -1;
    xxwidgets_status status;
    struct stat existing;
    if (!process || !destination || process->pid <= 1 || process->pid > INT_MAX || process->pid == (uint64_t)getpid() ||
        (type != XXWIDGETS_PROCESS_DUMP_MEMORY && type != XXWIDGETS_PROCESS_DUMP_IMAGE)) return XXWIDGETS_INVALID_ARGUMENT;
    if (xx_pd_is_stopped(pd) || !process->start_time || !dump_identity(process)) return XXWIDGETS_UNAVAILABLE;
    status = dump_stage_create(&stage, destination);
    if (status != XXWIDGETS_OK) goto done;
    if (!overwrite) {
        if (!fstatat(stage.parent, stage.destination, &existing, AT_SYMLINK_NOFOLLOW)) { status = XXWIDGETS_BUSY; goto done; }
        if (errno != ENOENT) { status = dump_errno(errno); goto done; }
    }
    if (type == XXWIDGETS_PROCESS_DUMP_MEMORY) {
        status = dump_core(process, &stage, output, pd);
        if (status != XXWIDGETS_OK) goto done;
        fd = openat(stage.directory, output, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) { status = dump_errno(errno); goto done; }
        if (!dump_validate_core(fd)) { status = XXWIDGETS_PLATFORM_ERROR; goto done; }
    } else {
        fd = openat(stage.directory, output, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0) { status = dump_errno(errno); goto done; }
        status = dump_image(process, fd, pd);
        if (status != XXWIDGETS_OK) goto done;
    }
    if (xx_pd_is_stopped(pd) || !dump_identity(process)) { status = XXWIDGETS_UNAVAILABLE; goto done; }
    if (fchmod(fd, 0600) || fsync(fd)) { status = dump_errno(errno); goto done; }
    if (close(fd)) { fd = -1; status = dump_errno(errno); goto done; }
    fd = -1;
    status = dump_publish(&stage, output, overwrite != 0);
done:
    if (fd >= 0) close(fd);
    dump_stage_cleanup(&stage, output);
    return status;
}
xxwidgets_status xxwidget_systemprocessesviewer_dump_process(const xxwidgets_process_info *process, const char *destination,
                                                           xxwidgets_process_dump_type type, int overwrite)
{
    return xxwidget_systemprocessesviewer_dump_process_ex(process, destination, type, overwrite, NULL);
}
#endif
