/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "xxwidget_systemprocessesviewer_memory.h"
#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>

static xxwidgets_status process_memory_windows_error(DWORD error)
{
    switch (error) {
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY: return XXWIDGETS_OUT_OF_MEMORY;
        case ERROR_ACCESS_DENIED:
        case ERROR_INVALID_PARAMETER:
        case ERROR_PARTIAL_COPY:
        case ERROR_NOT_SUPPORTED:
        case ERROR_NO_MORE_FILES: return XXWIDGETS_UNAVAILABLE;
        default: return XXWIDGETS_PLATFORM_ERROR;
    }
}

static int process_memory_windows_identity(HANDLE process, uint64_t start)
{
    FILETIME creation, exit_time, kernel, user;
    if (!GetProcessTimes(process, &creation, &exit_time, &kernel, &user)) return 0;
    return (((uint64_t)creation.dwHighDateTime << 32) | creation.dwLowDateTime) == start;
}

#ifndef _WIN64
static int process_memory_windows_pointer_width(HANDLE process)
{
    typedef BOOL (WINAPI *process_memory_is_wow64_process2_fn)(HANDLE, USHORT *, USHORT *);
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    process_memory_is_wow64_process2_fn query = kernel ?
        (process_memory_is_wow64_process2_fn)(void (*)(void))GetProcAddress(kernel, "IsWow64Process2") : NULL;
    if (query) {
        USHORT machine = 0, native_machine = 0;
        if (!query(process, &machine, &native_machine)) return 0;
        if (!machine) machine = native_machine;
        /* PE machine identifiers for i386, ARM, Thumb and ARMNT are 32-bit.
         * Other or unknown targets cannot be described by this host pointer. */
        return machine == 0x014cU || machine == 0x01c0U || machine == 0x01c2U || machine == 0x01c4U;
    } else {
        SYSTEM_INFO system;
        BOOL wow64 = FALSE;
        GetNativeSystemInfo(&system);
        /* PROCESSOR_ARCHITECTURE_ARM64 is 12, including with older SDKs. */
        if (system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ||
            system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64 || system.wProcessorArchitecture == 12U)
            return IsWow64Process(process, &wow64) && wow64;
        return 1;
    }
}
#endif

static int process_memory_windows_readable(const MEMORY_BASIC_INFORMATION *region)
{
    if (region->State != MEM_COMMIT || (region->Protect & PAGE_GUARD)) return 0;
    switch (region->Protect & 0xffU) {
        case PAGE_READONLY:
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY: return 1;
        default: return 0;
    }
}

/* Start at the requested point within a readable region, so a main module's
 * header remains the first byte even if the query groups preceding pages. */
static int process_memory_windows_region(const MEMORY_BASIC_INFORMATION *region, uint64_t address,
    uint64_t *base, uint64_t *size)
{
    uint64_t start = (uint64_t)(uintptr_t)region->BaseAddress;
    uint64_t length = (uint64_t)region->RegionSize;
    if (!process_memory_windows_readable(region) || !length || address < start || address - start >= length ||
        start > (uint64_t)INT64_MAX || length > (uint64_t)INT64_MAX - start) return 0;
    *base = address;
    *size = length - (address - start);
    return 1;
}

static uint64_t process_memory_windows_main_module(DWORD pid)
{
    HANDLE snapshot = INVALID_HANDLE_VALUE;
    MODULEENTRY32W module = {0};
    uint64_t base = 0;
    unsigned attempt;
    DWORD flags = TH32CS_SNAPMODULE;
#ifdef _WIN64
    flags |= TH32CS_SNAPMODULE32;
#endif
    for (attempt = 0; attempt < 8; ++attempt) {
        snapshot = CreateToolhelp32Snapshot(flags, pid);
        if (snapshot != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH) break;
    }
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    module.dwSize = (DWORD)sizeof(module);
    if (Module32FirstW(snapshot, &module)) base = (uint64_t)(uintptr_t)module.modBaseAddr;
    (void)CloseHandle(snapshot);
    return base;
}

static int process_memory_windows_initial_region(HANDLE process, DWORD pid, uint64_t *base, uint64_t *size)
{
    MEMORY_BASIC_INFORMATION region;
    SYSTEM_INFO system;
    uint64_t address = process_memory_windows_main_module(pid), maximum;
    if (address && VirtualQueryEx(process, (LPCVOID)(uintptr_t)address, &region, sizeof(region)) == sizeof(region) &&
        process_memory_windows_region(&region, address, base, size)) return 1;
    /* A module snapshot can be unavailable for a process that still permits
     * memory queries. Find its first committed, readable, non-guard range. */
    GetSystemInfo(&system);
    address = (uint64_t)(uintptr_t)system.lpMinimumApplicationAddress;
    maximum = (uint64_t)(uintptr_t)system.lpMaximumApplicationAddress;
    while (address <= maximum) {
        uint64_t region_base, length, next;
        if (VirtualQueryEx(process, (LPCVOID)(uintptr_t)address, &region, sizeof(region)) != sizeof(region)) break;
        if (process_memory_windows_region(&region, address, base, size)) return 1;
        region_base = (uint64_t)(uintptr_t)region.BaseAddress;
        length = (uint64_t)region.RegionSize;
        if (!length || length > UINT64_MAX - region_base) break;
        next = region_base + length;
        if (next <= address || next > maximum) break;
        address = next;
    }
    return 0;
}

xxwidgets_status xxwidgets_process_memory_open(const xxwidgets_process_info *info, xxwidgets_process_memory *memory)
{
    HANDLE process;
    xx_io_device *device = NULL;
    uint64_t base = 0, size = 0;
    xxwidgets_status status = XXWIDGETS_UNAVAILABLE;
    DWORD error;
    if (!memory) return XXWIDGETS_INVALID_ARGUMENT;
    memory->device = NULL;
    memory->base = memory->size = 0;
    memory->native = NULL;
    if (!info || info->pid <= 1 || info->pid > MAXDWORD) return XXWIDGETS_INVALID_ARGUMENT;
    if (!info->start_time) return XXWIDGETS_UNAVAILABLE;
    process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, (DWORD)info->pid);
    if (!process) return process_memory_windows_error(GetLastError());
    if (!process_memory_windows_identity(process, info->start_time)) goto done;
#ifndef _WIN64
    /* Self always has the host's pointer width. A 64-bit host can inspect
     * either width; a 32-bit host rejects an incompatible target. */
    if (info->pid != (uint64_t)GetCurrentProcessId() && !process_memory_windows_pointer_width(process)) goto done;
#endif
    if (!process_memory_windows_initial_region(process, (DWORD)info->pid, &base, &size)) goto done;
    if (!process_memory_windows_identity(process, info->start_time)) goto done;
    device = xx_io_process_open_handle_ex((void *)process, XX_IO_PROCESS_READ);
    if (!device) {
        status = process_memory_windows_error(GetLastError());
        goto done;
    }
    if (!process_memory_windows_identity(process, info->start_time)) goto done;
    memory->device = device;
    memory->base = base;
    memory->size = size;
    memory->native = (void *)process;
    return XXWIDGETS_OK;
done:
    error = GetLastError();
    if (device) (void)xx_io_process_close(device);
    (void)CloseHandle(process);
    SetLastError(error);
    return status;
}

void xxwidgets_process_memory_close(xxwidgets_process_memory *memory)
{
    DWORD error;
    if (!memory) return;
    error = GetLastError();
    if (memory->device) (void)xx_io_process_close(memory->device);
    if (memory->native && (HANDLE)memory->native != INVALID_HANDLE_VALUE) (void)CloseHandle((HANDLE)memory->native);
    memory->device = NULL;
    memory->base = memory->size = 0;
    memory->native = NULL;
    SetLastError(error);
}
#endif /* _WIN32 */
