/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0600
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#if defined(__FreeBSD__)
/* This native provider needs the public BSD sysctl/kinfo ABI. Older FreeBSD
 * headers hide it under a strict POSIX profile; newer headers use _BSD_SOURCE. */
#undef _POSIX_C_SOURCE
#undef _POSIX_SOURCE
#undef _XOPEN_SOURCE
#ifndef _BSD_SOURCE
#define _BSD_SOURCE 1
#endif
#endif
#include "xxwidgets/xxwidget_systemprocessesviewer.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <wchar.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <libproc.h>
#include <sys/proc_info.h>
#elif defined(__linux__)
#include <dirent.h>
#include <sys/syscall.h>
/* Strict POSIX feature profiles hide this Linux extension's declaration. */
extern long syscall(long number, ...);
#elif defined(__FreeBSD__)
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#endif
#endif

#define SYSTEMPROCESSESVIEWER_MAX_PROCESSES ((size_t)1048576)
#define SYSTEMPROCESSESVIEWER_MAX_STRING ((size_t)1048576)

static size_t systemprocessesviewer_length(const char *text)
{
    size_t length;
    for (length = 0; length <= SYSTEMPROCESSESVIEWER_MAX_STRING; ++length) if (!text[length]) return length;
    return SIZE_MAX;
}

static char *systemprocessesviewer_copy(const char *text)
{
    size_t length = systemprocessesviewer_length(text);
    char *copy;
    if (length > SYSTEMPROCESSESVIEWER_MAX_STRING) return NULL;
    copy = (char *)malloc(length + 1);
    if (copy) memcpy(copy, text, length + 1);
    return copy;
}

#ifndef _WIN32
/* Unix process names may contain arbitrary bytes. Replace invalid UTF-8 in
 * names. A filename that cannot be represented by this API is unavailable. */
static size_t systemprocessesviewer_utf8_sequence(const unsigned char *s, size_t remaining)
{
    size_t length, i;
    if (s[0] < 0x80) return 1;
    if (s[0] >= 0xc2 && s[0] <= 0xdf) length = 2;
    else if (s[0] >= 0xe0 && s[0] <= 0xef) length = 3;
    else if (s[0] >= 0xf0 && s[0] <= 0xf4) length = 4;
    else return 0;
    if (remaining < length) return 0;
    for (i = 1; i < length; ++i) if ((s[i] & 0xc0) != 0x80) return 0;
    if ((s[0] == 0xe0 && s[1] < 0xa0) || (s[0] == 0xed && s[1] >= 0xa0) ||
        (s[0] == 0xf0 && s[1] < 0x90) || (s[0] == 0xf4 && s[1] >= 0x90)) return 0;
    return length;
}

static char *systemprocessesviewer_utf8_copy(const char *text, int filename)
{
    size_t length = systemprocessesviewer_length(text), i = 0, used = 0;
    char *copy;
    if (!length) return systemprocessesviewer_copy("");
    if (length > SYSTEMPROCESSESVIEWER_MAX_STRING || length > (SIZE_MAX - 1) / 3) return NULL;
    copy = (char *)malloc(length * 3 + 1);
    if (!copy) return NULL;
    while (i < length) {
        size_t sequence = systemprocessesviewer_utf8_sequence((const unsigned char *)text + i, length - i);
        if (!sequence) {
            if (filename) { copy[0] = '\0'; return copy; }
            memcpy(copy + used, "\xef\xbf\xbd", 3);
            used += 3;
            ++i;
        } else {
            memcpy(copy + used, text + i, sequence);
            used += sequence;
            i += sequence;
        }
    }
    copy[used] = '\0';
    return copy;
}
#endif

void xxwidget_systemprocessesviewer_free_processes(xxwidgets_process_info *processes, size_t count)
{
    size_t i;
    if (!processes) return;
    for (i = 0; i < count; ++i) {
        free((void *)processes[i].name);
        free((void *)processes[i].path);
    }
    free(processes);
}

static xxwidgets_status systemprocessesviewer_append(xxwidgets_process_info **items, size_t *count, size_t *capacity,
                                             xxwidgets_process_info *item)
{
    if (!item->name || !item->path) return XXWIDGETS_OUT_OF_MEMORY;
    if (*count == *capacity) {
        size_t next = *capacity ? *capacity * 2 : 256;
        xxwidgets_process_info *expanded;
        if (next > SYSTEMPROCESSESVIEWER_MAX_PROCESSES) return XXWIDGETS_UNAVAILABLE;
        expanded = (xxwidgets_process_info *)realloc(*items, next * sizeof(**items));
        if (!expanded) return XXWIDGETS_OUT_OF_MEMORY;
        *items = expanded;
        *capacity = next;
    }
    (*items)[(*count)++] = *item;
    memset(item, 0, sizeof(*item));
    return XXWIDGETS_OK;
}

#ifdef _WIN32
static char *systemprocessesviewer_from_wide(const wchar_t *text)
{
    int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    char *copy;
    if (length <= 0 || (size_t)length > SYSTEMPROCESSESVIEWER_MAX_STRING) return NULL;
    copy = (char *)malloc((size_t)length);
    if (copy && !WideCharToMultiByte(CP_UTF8, 0, text, -1, copy, length, NULL, NULL)) { free(copy); return NULL; }
    return copy;
}

static uint64_t systemprocessesviewer_windows_start(HANDLE process)
{
    FILETIME creation, exit_time, kernel, user;
    if (!GetProcessTimes(process, &creation, &exit_time, &kernel, &user)) return 0;
    return ((uint64_t)creation.dwHighDateTime << 32) | creation.dwLowDateTime;
}

xxwidgets_status xxwidget_systemprocessesviewer_enumerate(xxwidgets_process_info **out_processes, size_t *out_count)
{
    HANDLE snapshot;
    PROCESSENTRY32W entry;
    xxwidgets_process_info *items = NULL, item = {0};
    size_t count = 0, capacity = 0;
    xxwidgets_status status = XXWIDGETS_OK;
    BOOL more;
    if (!out_processes || !out_count) return XXWIDGETS_INVALID_ARGUMENT;
    *out_processes = NULL;
    *out_count = 0;
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return XXWIDGETS_PLATFORM_ERROR;
    memset(&entry, 0, sizeof(entry));
    entry.dwSize = sizeof(entry);
    more = Process32FirstW(snapshot, &entry);
    while (more) {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        wchar_t path[32768];
        DWORD length = (DWORD)(sizeof(path) / sizeof(path[0]));
        item.pid = entry.th32ProcessID;
        item.name = systemprocessesviewer_from_wide(entry.szExeFile);
        if (process) {
            item.start_time = systemprocessesviewer_windows_start(process);
            if (QueryFullProcessImageNameW(process, 0, path, &length) && length < 32768) {
                path[length] = L'\0';
                item.path = systemprocessesviewer_from_wide(path);
            }
            CloseHandle(process);
        }
        if (!item.path) item.path = systemprocessesviewer_copy("");
        status = systemprocessesviewer_append(&items, &count, &capacity, &item);
        if (status != XXWIDGETS_OK) break;
        more = Process32NextW(snapshot, &entry);
    }
    if (status == XXWIDGETS_OK && !more && GetLastError() != ERROR_NO_MORE_FILES) status = XXWIDGETS_PLATFORM_ERROR;
    CloseHandle(snapshot);
    if (status != XXWIDGETS_OK) {
        free((void *)item.name);
        free((void *)item.path);
        xxwidget_systemprocessesviewer_free_processes(items, count);
        return status;
    }
    *out_processes = items;
    *out_count = count;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidget_systemprocessesviewer_kill_process(const xxwidgets_process_info *process, int force)
{
    HANDLE handle;
    xxwidgets_status status;
    (void)force; /* Windows has no general SIGTERM equivalent. */
    if (!process || process->pid <= 1 || process->pid > MAXDWORD || process->pid == GetCurrentProcessId()) return XXWIDGETS_INVALID_ARGUMENT;
    if (!process->start_time) return XXWIDGETS_UNAVAILABLE;
    handle = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)process->pid);
    if (!handle) return GetLastError() == ERROR_ACCESS_DENIED || GetLastError() == ERROR_INVALID_PARAMETER ? XXWIDGETS_UNAVAILABLE : XXWIDGETS_PLATFORM_ERROR;
    if (systemprocessesviewer_windows_start(handle) != process->start_time) status = XXWIDGETS_UNAVAILABLE;
    else if (TerminateProcess(handle, 1)) status = XXWIDGETS_OK;
    else status = GetLastError() == ERROR_ACCESS_DENIED ? XXWIDGETS_UNAVAILABLE : XXWIDGETS_PLATFORM_ERROR;
    CloseHandle(handle);
    return status;
}

xxwidgets_status xxwidget_systemprocessesviewer_open_process_path(const xxwidgets_process_info *process)
{
    wchar_t explorer[MAX_PATH + 16], *path, *parameters;
    UINT windows_length;
    int length;
    SHELLEXECUTEINFOW info;
    if (!process) return XXWIDGETS_INVALID_ARGUMENT;
    if (!process->path || !process->path[0]) return XXWIDGETS_UNAVAILABLE;
    if (systemprocessesviewer_length(process->path) > SYSTEMPROCESSESVIEWER_MAX_STRING) return XXWIDGETS_INVALID_ARGUMENT;
    length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, process->path, -1, NULL, 0);
    if (length <= 0 || length > 32768 || strchr(process->path, '"')) return XXWIDGETS_INVALID_ARGUMENT;
    path = (wchar_t *)malloc((size_t)length * sizeof(*path));
    parameters = (wchar_t *)malloc(((size_t)length + 16) * sizeof(*parameters));
    if (!path || !parameters) { free(path); free(parameters); return XXWIDGETS_OUT_OF_MEMORY; }
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, process->path, -1, path, length);
    if (!(length > 3 && ((path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) ||
                         (path[0] == L'\\' && path[1] == L'\\')))) {
        free(path); free(parameters); return XXWIDGETS_INVALID_ARGUMENT;
    }
    windows_length = GetWindowsDirectoryW(explorer, MAX_PATH);
    if (!windows_length || windows_length >= MAX_PATH) { free(path); free(parameters); return XXWIDGETS_PLATFORM_ERROR; }
    wcscat(explorer, L"\\explorer.exe");
    wcscpy(parameters, L"/select,\"");
    wcscat(parameters, path);
    wcscat(parameters, L"\"");
    free(path);
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"open";
    info.lpFile = explorer;
    info.lpParameters = parameters;
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) { free(parameters); return XXWIDGETS_PLATFORM_ERROR; }
    if (info.hProcess) CloseHandle(info.hProcess);
    free(parameters);
    return XXWIDGETS_OK;
}

#else
static xxwidgets_status systemprocessesviewer_errno_status(int error)
{
    return error == EPERM || error == EACCES || error == ESRCH || error == ENOENT ? XXWIDGETS_UNAVAILABLE : XXWIDGETS_PLATFORM_ERROR;
}

#if defined(__linux__)
static int systemprocessesviewer_linux_stat(pid_t pid, char **name, uint64_t *start)
{
    char filename[64], buffer[65536], *left, *right, *cursor, *end;
    size_t used = 0;
    int fd, field, error;
    ssize_t got;
    *start = 0;
    if (name) *name = NULL;
    snprintf(filename, sizeof(filename), "/proc/%ld/stat", (long)pid);
    fd = open(filename, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    while (used < sizeof(buffer) - 1) {
        got = read(fd, buffer + used, sizeof(buffer) - 1 - used);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) { error = errno; close(fd); errno = error; return -1; }
        if (!got) break;
        used += (size_t)got;
    }
    close(fd);
    if (used == sizeof(buffer) - 1) { errno = EOVERFLOW; return -1; }
    buffer[used] = '\0';
    left = strchr(buffer, '(');
    right = strrchr(buffer, ')');
    if (!left || !right || right <= left || right[1] != ' ') { errno = EINVAL; return -1; }
    cursor = right + 2;
    for (field = 3; field < 22; ++field) {
        while (*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != '\n') ++cursor;
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (!*cursor || *cursor == '\n') { errno = EINVAL; return -1; }
    }
    errno = 0;
    *start = (uint64_t)strtoull(cursor, &end, 10);
    if (errno || end == cursor || (*end && *end != ' ' && *end != '\n' && *end != '\t') || !*start) {
        *start = 0; errno = EINVAL; return -1;
    }
    if (name) {
        *right = '\0';
        *name = systemprocessesviewer_utf8_copy(left + 1, 0);
        if (!*name) { errno = ENOMEM; return -1; }
    }
    return 0;
}

static char *systemprocessesviewer_linux_path(pid_t pid)
{
    char filename[64];
    size_t capacity = 256;
    snprintf(filename, sizeof(filename), "/proc/%ld/exe", (long)pid);
    while (capacity <= SYSTEMPROCESSESVIEWER_MAX_STRING) {
        char *buffer = (char *)malloc(capacity + 1), *path;
        ssize_t length;
        if (!buffer) return NULL;
        length = readlink(filename, buffer, capacity);
        if (length < 0) { free(buffer); return systemprocessesviewer_copy(""); }
        if ((size_t)length < capacity) {
            buffer[length] = '\0';
            path = systemprocessesviewer_utf8_copy(buffer, 1);
            free(buffer);
            return path;
        }
        free(buffer);
        capacity *= 2;
    }
    return systemprocessesviewer_copy("");
}
#elif defined(__APPLE__)
static uint64_t systemprocessesviewer_macos_start(pid_t pid, struct proc_bsdinfo *out_info)
{
    struct proc_bsdinfo info;
    int size = proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info));
    if (size != (int)sizeof(info) || info.pbi_start_tvsec > (UINT64_MAX - info.pbi_start_tvusec) / 1000000) return 0;
    if (out_info) *out_info = info;
    return (uint64_t)info.pbi_start_tvsec * 1000000 + info.pbi_start_tvusec;
}
#elif defined(__FreeBSD__)
/* Public ABI references: FreeBSD sys/sys/user.h, sys/sys/sysctl.h and
 * lib/libutil/kinfo_getallproc.c. Query results are native caller ABI records. */
static uint64_t systemprocessesviewer_freebsd_token(const struct kinfo_proc *info)
{
    if (info->ki_start.tv_sec <= 0 || info->ki_start.tv_usec < 0 || info->ki_start.tv_usec >= 1000000 ||
        (uint64_t)info->ki_start.tv_sec > (UINT64_MAX - (uint64_t)info->ki_start.tv_usec) / 1000000) return 0;
    return (uint64_t)info->ki_start.tv_sec * 1000000 + (uint64_t)info->ki_start.tv_usec;
}

static uint64_t systemprocessesviewer_freebsd_start(pid_t pid)
{
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
    struct kinfo_proc info;
    size_t length = sizeof(info);
    if (sysctl(mib, 4, &info, &length, NULL, 0) || length != sizeof(info) ||
        info.ki_structsize != (int)sizeof(info) || info.ki_pid != pid) return 0;
    return systemprocessesviewer_freebsd_token(&info);
}

static char *systemprocessesviewer_freebsd_path(pid_t pid)
{
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, pid};
    char filename[PATH_MAX];
    size_t length = sizeof(filename);
    if (sysctl(mib, 4, filename, &length, NULL, 0) || !length || length > sizeof(filename))
        return systemprocessesviewer_copy("");
    filename[length - 1] = '\0';
    return systemprocessesviewer_utf8_copy(filename, 1);
}
#endif

xxwidgets_status xxwidget_systemprocessesviewer_enumerate(xxwidgets_process_info **out_processes, size_t *out_count)
{
    xxwidgets_process_info *items = NULL, item = {0};
    size_t count = 0, capacity = 0;
    xxwidgets_status status = XXWIDGETS_OK;
    if (!out_processes || !out_count) return XXWIDGETS_INVALID_ARGUMENT;
    *out_processes = NULL;
    *out_count = 0;
#if defined(__linux__)
    {
        DIR *directory = opendir("/proc");
        struct dirent *entry;
        if (!directory) return systemprocessesviewer_errno_status(errno);
        for (;;) {
            char *end, *name;
            unsigned long value;
            errno = 0;
            entry = readdir(directory);
            if (!entry) { if (errno) status = XXWIDGETS_PLATFORM_ERROR; break; }
            if (entry->d_name[0] < '1' || entry->d_name[0] > '9') continue;
            errno = 0;
            value = strtoul(entry->d_name, &end, 10);
            if (errno || *end || value > INT_MAX) continue;
            item.pid = value;
            if (systemprocessesviewer_linux_stat((pid_t)value, &name, &item.start_time)) {
                if (errno == ENOMEM) { status = XXWIDGETS_OUT_OF_MEMORY; break; }
                continue; /* Exited or inaccessible between readdir and stat. */
            }
            item.name = name;
            item.path = systemprocessesviewer_linux_path((pid_t)value);
            status = systemprocessesviewer_append(&items, &count, &capacity, &item);
            if (status != XXWIDGETS_OK) break;
        }
        closedir(directory);
    }
#elif defined(__APPLE__)
    {
        pid_t *pids = NULL;
        int desired = proc_listallpids(NULL, 0), found = 0, attempt, i;
        if (desired <= 0) return XXWIDGETS_PLATFORM_ERROR;
        for (attempt = 0; attempt < 4; ++attempt) {
            size_t allocated = (size_t)desired + 256;
            if (allocated > SYSTEMPROCESSESVIEWER_MAX_PROCESSES) { status = XXWIDGETS_UNAVAILABLE; break; }
            free(pids);
            pids = (pid_t *)malloc(allocated * sizeof(*pids));
            if (!pids) { status = XXWIDGETS_OUT_OF_MEMORY; break; }
            found = proc_listallpids(pids, (int)(allocated * sizeof(*pids)));
            if (found <= 0) { status = XXWIDGETS_PLATFORM_ERROR; break; }
            if ((size_t)found < allocated) break;
            desired = found + 256;
        }
        if (status == XXWIDGETS_OK) for (i = 0; i < found; ++i) {
            struct proc_bsdinfo info;
            char path[PROC_PIDPATHINFO_MAXSIZE], name[sizeof(info.pbi_name) + 1];
            if (pids[i] <= 0) continue;
            memset(&info, 0, sizeof(info));
            item.pid = (uint64_t)pids[i];
            item.start_time = systemprocessesviewer_macos_start(pids[i], &info);
            memset(name, 0, sizeof(name));
            if (item.start_time) {
                const char *source = info.pbi_name[0] ? info.pbi_name : info.pbi_comm;
                size_t maximum = info.pbi_name[0] ? sizeof(info.pbi_name) : sizeof(info.pbi_comm);
                memcpy(name, source, maximum < sizeof(name) - 1 ? maximum : sizeof(name) - 1);
            } else if (proc_name(pids[i], name, (uint32_t)sizeof(name)) <= 0) continue;
            name[sizeof(name) - 1] = '\0';
            memset(path, 0, sizeof(path));
            if (proc_pidpath(pids[i], path, sizeof(path)) <= 0) path[0] = '\0';
            path[sizeof(path) - 1] = '\0';
            item.name = systemprocessesviewer_utf8_copy(name, 0);
            item.path = systemprocessesviewer_utf8_copy(path, 1);
            status = systemprocessesviewer_append(&items, &count, &capacity, &item);
            if (status != XXWIDGETS_OK) break;
        }
        free(pids);
    }
#elif defined(__FreeBSD__)
    {
        int mib[3] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC};
        struct kinfo_proc *records = NULL;
        size_t length = 0;
        unsigned attempt;
        int complete = 0;
        for (attempt = 0; attempt < 8; ++attempt) {
            size_t wanted = 0, maximum = SYSTEMPROCESSESVIEWER_MAX_PROCESSES;
            if (sysctl(mib, 3, NULL, &wanted, NULL, 0)) { status = systemprocessesviewer_errno_status(errno); break; }
            if (maximum > SIZE_MAX / sizeof(*records)) maximum = SIZE_MAX / sizeof(*records);
            if (wanted / sizeof(*records) > maximum || wanted > SIZE_MAX - 16 * sizeof(*records)) {
                status = XXWIDGETS_UNAVAILABLE; break;
            }
            size_t bytes = wanted + 16 * sizeof(*records);
            if (bytes / sizeof(*records) > maximum) bytes = maximum * sizeof(*records);
            struct kinfo_proc *grown = (struct kinfo_proc *)realloc(records, bytes);
            if (!grown) { status = XXWIDGETS_OUT_OF_MEMORY; break; }
            records = grown; length = bytes;
            if (!sysctl(mib, 3, records, &length, NULL, 0)) {
                if (length > bytes || length % sizeof(*records)) status = XXWIDGETS_PLATFORM_ERROR;
                else complete = 1;
                break;
            }
            if (errno != ENOMEM) { status = systemprocessesviewer_errno_status(errno); break; }
        }
        if (status == XXWIDGETS_OK && !complete) status = XXWIDGETS_UNAVAILABLE;
        if (status == XXWIDGETS_OK) for (size_t i = 0; i < length / sizeof(*records); ++i) {
            const struct kinfo_proc *info = records + i;
            char name[sizeof(info->ki_comm) + 1];
            if (info->ki_structsize != (int)sizeof(*info)) { status = XXWIDGETS_UNAVAILABLE; break; }
            if (info->ki_pid <= 0) continue;
            item.pid = (uint64_t)info->ki_pid;
            item.start_time = systemprocessesviewer_freebsd_token(info);
            if (!item.start_time) continue;
            memcpy(name, info->ki_comm, sizeof(info->ki_comm));
            name[sizeof(info->ki_comm)] = '\0';
            item.name = systemprocessesviewer_utf8_copy(name, 0);
            item.path = systemprocessesviewer_freebsd_path(info->ki_pid);
            status = systemprocessesviewer_append(&items, &count, &capacity, &item);
            if (status != XXWIDGETS_OK) break;
        }
        free(records);
    }
#else
    status = XXWIDGETS_UNAVAILABLE;
#endif
    if (status != XXWIDGETS_OK) {
        free((void *)item.name);
        free((void *)item.path);
        xxwidget_systemprocessesviewer_free_processes(items, count);
        return status;
    }
    *out_processes = items;
    *out_count = count;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidget_systemprocessesviewer_kill_process(const xxwidgets_process_info *process, int force)
{
    uint64_t start = 0;
    int signal_number = force ? SIGKILL : SIGTERM;
    if (!process || process->pid <= 1 || process->pid > INT_MAX || process->pid == (uint64_t)getpid()) return XXWIDGETS_INVALID_ARGUMENT;
    if (!process->start_time) return XXWIDGETS_UNAVAILABLE;
#if defined(__linux__)
    {
        int pidfd = -1;
#if defined(SYS_pidfd_open) && defined(SYS_pidfd_send_signal)
        pidfd = (int)syscall(SYS_pidfd_open, (pid_t)process->pid, 0);
        if (pidfd < 0 && errno != ENOSYS && errno != EINVAL) return systemprocessesviewer_errno_status(errno);
#endif
        if (systemprocessesviewer_linux_stat((pid_t)process->pid, NULL, &start) || start != process->start_time) {
            if (pidfd >= 0) close(pidfd);
            return XXWIDGETS_UNAVAILABLE;
        }
#if defined(SYS_pidfd_open) && defined(SYS_pidfd_send_signal)
        if (pidfd >= 0) {
            int result = (int)syscall(SYS_pidfd_send_signal, pidfd, signal_number, NULL, 0);
            int error = errno;
            close(pidfd);
            return result ? systemprocessesviewer_errno_status(error) : XXWIDGETS_OK;
        }
#endif
    }
#elif defined(__APPLE__)
    start = systemprocessesviewer_macos_start((pid_t)process->pid, NULL);
    if (!start || start != process->start_time) return XXWIDGETS_UNAVAILABLE;
#elif defined(__FreeBSD__)
    start = systemprocessesviewer_freebsd_start((pid_t)process->pid);
    if (!start || start != process->start_time) return XXWIDGETS_UNAVAILABLE;
#else
    (void)start;
    return XXWIDGETS_UNAVAILABLE;
#endif
    return kill((pid_t)process->pid, signal_number) ? systemprocessesviewer_errno_status(errno) : XXWIDGETS_OK;
}

/* Reap the short-lived intermediary. The desktop utility is adopted by the
 * system reaper; the close-on-exec pipe reports its exec failures. */
static xxwidgets_status systemprocessesviewer_launch(char *const argv[])
{
    int descriptors[2], child_status, exec_error = 0, result;
    pid_t child, waited;
    ssize_t received;
    if (pipe(descriptors)) return XXWIDGETS_PLATFORM_ERROR;
    if (fcntl(descriptors[0], F_SETFD, FD_CLOEXEC) < 0 || fcntl(descriptors[1], F_SETFD, FD_CLOEXEC) < 0) {
        close(descriptors[0]); close(descriptors[1]); return XXWIDGETS_PLATFORM_ERROR;
    }
    child = fork();
    if (child < 0) { close(descriptors[0]); close(descriptors[1]); return XXWIDGETS_PLATFORM_ERROR; }
    if (!child) {
        pid_t detached;
        close(descriptors[0]);
        detached = fork();
        if (!detached) {
            execvp(argv[0], argv);
            exec_error = errno;
        } else if (detached < 0) exec_error = errno;
        else _exit(0);
        do { result = (int)write(descriptors[1], &exec_error, sizeof(exec_error)); } while (result < 0 && errno == EINTR);
        _exit(127);
    }
    close(descriptors[1]);
    do { waited = waitpid(child, &child_status, 0); } while (waited < 0 && errno == EINTR);
    do { received = read(descriptors[0], &exec_error, sizeof(exec_error)); } while (received < 0 && errno == EINTR);
    close(descriptors[0]);
    if (received > 0) return systemprocessesviewer_errno_status(exec_error);
    if (received < 0 || waited < 0 || !WIFEXITED(child_status) || WEXITSTATUS(child_status)) return XXWIDGETS_PLATFORM_ERROR;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidget_systemprocessesviewer_open_process_path(const xxwidgets_process_info *process)
{
    xxwidgets_status status;
    if (!process) return XXWIDGETS_INVALID_ARGUMENT;
    if (!process->path || !process->path[0]) return XXWIDGETS_UNAVAILABLE;
    if (process->path[0] != '/' || systemprocessesviewer_length(process->path) > SYSTEMPROCESSESVIEWER_MAX_STRING) return XXWIDGETS_INVALID_ARGUMENT;
#if defined(__APPLE__)
    {
        char *argv[] = {"/usr/bin/open", "-R", (char *)process->path, NULL};
        status = systemprocessesviewer_launch(argv);
    }
#elif defined(__linux__) || defined(__FreeBSD__)
    {
        char *directory = systemprocessesviewer_copy(process->path), *slash;
        char *argv[] = {"xdg-open", directory, NULL};
        if (!directory) return XXWIDGETS_OUT_OF_MEMORY;
        slash = strrchr(directory, '/');
        if (slash == directory) slash[1] = '\0';
        else *slash = '\0';
        status = systemprocessesviewer_launch(argv);
        free(directory);
    }
#else
    status = XXWIDGETS_UNAVAILABLE;
#endif
    return status;
}
#endif
