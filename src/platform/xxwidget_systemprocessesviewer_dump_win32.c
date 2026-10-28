/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include "xxwidgets/xxwidget_systemprocessesviewer.h"
#include "xxfclib/io/xx_io.h"
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif

#define XX_DUMP_MAX_PATH 32767u
#define XX_DUMP_CHUNK 65536u

typedef BOOL (WINAPI *xx_dump_write_fn)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
    PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
typedef BOOL (WINAPI *xx_dump_wow64_fn)(HANDLE, USHORT *, USHORT *);
typedef BOOL (WINAPI *xx_dump_process_info_fn)(HANDLE, int, void *, DWORD);

/* This protects this library's DbgHelp calls. Applications using DbgHelp
 * elsewhere must also obey its process-wide serialization requirement. */
static SRWLOCK xx_dump_dbghelp_lock = SRWLOCK_INIT;
static volatile LONG xx_dump_stage_serial;

static xxwidgets_status dump_error(DWORD error)
{
    /* MiniDumpWriteDump reports an HRESULT through GetLastError. */
    if ((error & 0xffff0000u) == 0x80070000u) error &= 0xffffu;
    if (error == ERROR_NOT_ENOUGH_MEMORY || error == ERROR_OUTOFMEMORY) return XXWIDGETS_OUT_OF_MEMORY;
    if (error == ERROR_INVALID_NAME || error == ERROR_BAD_PATHNAME || error == ERROR_INVALID_DRIVE)
        return XXWIDGETS_INVALID_ARGUMENT;
    if (error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD || error == ERROR_INVALID_PARAMETER ||
        error == ERROR_NOT_SUPPORTED || error == ERROR_CALL_NOT_IMPLEMENTED || error == ERROR_PARTIAL_COPY ||
        error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_NOT_FOUND ||
        error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS || error == ERROR_PROCESS_ABORTED)
        return XXWIDGETS_UNAVAILABLE;
    return XXWIDGETS_PLATFORM_ERROR;
}

static xxwidgets_status dump_identity(HANDLE handle, uint64_t expected)
{
    FILETIME created, exited, kernel, user;
    DWORD exit_code;
    uint64_t actual;
    if (!GetProcessTimes(handle, &created, &exited, &kernel, &user)) return dump_error(GetLastError());
    actual = ((uint64_t)created.dwHighDateTime << 32) | created.dwLowDateTime;
    if (!expected || actual != expected) return XXWIDGETS_UNAVAILABLE;
    if (!GetExitCodeProcess(handle, &exit_code)) return dump_error(GetLastError());
    return exit_code == STILL_ACTIVE ? XXWIDGETS_OK : XXWIDGETS_UNAVAILABLE;
}

static USHORT dump_writer_machine(void)
{
#if defined(_M_X64) || defined(__x86_64__)
    return IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_IX86) || defined(__i386__)
    return IMAGE_FILE_MACHINE_I386;
#elif defined(_M_ARM64) || defined(__aarch64__)
    return IMAGE_FILE_MACHINE_ARM64;
#elif defined(_M_ARM) || defined(__arm__)
    return IMAGE_FILE_MACHINE_ARMNT;
#else
    return IMAGE_FILE_MACHINE_UNKNOWN;
#endif
}

static xxwidgets_status dump_same_machine(HANDLE process)
{
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    xx_dump_wow64_fn query = kernel ? (xx_dump_wow64_fn)GetProcAddress(kernel, "IsWow64Process2") : NULL;
    xx_dump_process_info_fn query_information = kernel ?
        (xx_dump_process_info_fn)GetProcAddress(kernel, "GetProcessInformation") : NULL;
    USHORT machine, native_machine, writer = dump_writer_machine();
    if (writer == IMAGE_FILE_MACHINE_UNKNOWN) return XXWIDGETS_UNAVAILABLE;
    if (query_information) {
        /* Windows 11 ProcessMachineTypeInfo (class 9) disambiguates native
         * ARM64 and emulated x64. Keep its ABI private for older SDK builds. */
        struct { USHORT machine, reserved; DWORD attributes; } information;
        memset(&information, 0, sizeof(information));
        if (query_information(process, 9, &information, sizeof(information)))
            return information.machine == writer ? XXWIDGETS_OK : XXWIDGETS_UNAVAILABLE;
    }
    if (query) {
        if (!query(process, &machine, &native_machine)) return dump_error(GetLastError());
        if (machine == IMAGE_FILE_MACHINE_UNKNOWN) {
            /* Older ARM64 emulation can report UNKNOWN for both native
             * ARM64 and emulated x64. Refuse an ambiguous architecture. */
            if (native_machine == 0xaa64u) return XXWIDGETS_UNAVAILABLE;
            machine = native_machine;
        }
    } else {
        SYSTEM_INFO system;
        BOOL wow64;
        if (!IsWow64Process(process, &wow64)) return dump_error(GetLastError());
        GetNativeSystemInfo(&system);
        if (system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) machine = IMAGE_FILE_MACHINE_I386;
        else if (system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
            machine = wow64 ? IMAGE_FILE_MACHINE_I386 : IMAGE_FILE_MACHINE_AMD64;
        else return XXWIDGETS_UNAVAILABLE; /* Old WOW64 APIs cannot distinguish ARM emulation reliably. */
    }
    return machine == writer ? XXWIDGETS_OK : XXWIDGETS_UNAVAILABLE;
}

/* Canonicalize before staging so changing the working directory cannot move
 * the destination between file creation and publication. Extended paths also
 * keep a long adjacent staging filename within Win32's supported path form. */
static xxwidgets_status dump_destination(const char *text, wchar_t **out)
{
    wchar_t *input = NULL, *absolute = NULL, *full = NULL, *last;
    DWORD needed, actual;
    int length;
    size_t i, count, prefix;
    xxwidgets_status status = XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!text || !*text) return status;
    for (i = 0; i <= 1048576u; ++i) if (!text[i]) break;
    if (i > 1048576u) return status;
    length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (length <= 1 || length > (int)XX_DUMP_MAX_PATH) return status;
    input = (wchar_t *)malloc((size_t)length * sizeof(*input));
    if (!input) return XXWIDGETS_OUT_OF_MEMORY;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, input, length)) goto done;
    for (i = 0; input[i]; ++i) if (input[i] == L'/') input[i] = L'\\';
    if (!wcsncmp(input, L"\\\\.\\", 4)) goto done; /* Device/pipe destinations are not dump files. */
    needed = GetFullPathNameW(input, 0, NULL, NULL);
    if (!needed || needed > XX_DUMP_MAX_PATH) goto done;
    absolute = (wchar_t *)malloc(((size_t)needed + 1) * sizeof(*absolute));
    if (!absolute) { status = XXWIDGETS_OUT_OF_MEMORY; goto done; }
    actual = GetFullPathNameW(input, needed + 1, absolute, NULL);
    if (!actual || actual > needed) goto done;
    count = wcslen(absolute);
    if (!wcsncmp(absolute, L"\\\\?\\", 4)) {
        if (!wcsncmp(absolute, L"\\\\?\\UNC\\", 8)) prefix = 8;
        else if (count >= 7 && absolute[5] == L':' && absolute[6] == L'\\') prefix = 6;
        else goto done;
        full = absolute; absolute = NULL;
    } else if (count >= 2 && absolute[0] == L'\\' && absolute[1] == L'\\') {
        if (count > XX_DUMP_MAX_PATH - 7) goto done;
        full = (wchar_t *)malloc((count + 7) * sizeof(*full));
        if (!full) { status = XXWIDGETS_OUT_OF_MEMORY; goto done; }
        memcpy(full, L"\\\\?\\UNC\\", 8 * sizeof(*full));
        memcpy(full + 8, absolute + 2, (count - 1) * sizeof(*full));
        prefix = 8;
    } else {
        if (count < 3 || absolute[1] != L':' || absolute[2] != L'\\' || count > XX_DUMP_MAX_PATH - 5) goto done;
        full = (wchar_t *)malloc((count + 5) * sizeof(*full));
        if (!full) { status = XXWIDGETS_OUT_OF_MEMORY; goto done; }
        memcpy(full, L"\\\\?\\", 4 * sizeof(*full));
        memcpy(full + 4, absolute, (count + 1) * sizeof(*full));
        prefix = 6;
    }
    /* Reject alternate streams; publication must target an ordinary file. */
    for (i = prefix; full[i]; ++i) if (full[i] == L':') goto done;
    last = wcsrchr(full, L'\\');
    if (!last || !last[1] || !wcscmp(last + 1, L".") || !wcscmp(last + 1, L"..")) goto done;
    *out = full; full = NULL; status = XXWIDGETS_OK;
done:
    free(input); free(absolute); free(full);
    return status;
}

static int dump_disposition(HANDLE file, int remove)
{
    FILE_DISPOSITION_INFO disposition;
    disposition.DeleteFile = remove ? TRUE : FALSE;
    return SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition)) != 0;
}

/* When a filesystem rejects FileDispositionInfo, fall back to the separate
 * DELETE_ON_CLOSE open mechanism. Reopen by path only to locate the recorded
 * file identity, then ReOpenFile acts on that verified object, not its name.
 * If the path has been replaced, the replacement is never removed. */
static void dump_close_stage(HANDLE file, const wchar_t *stage)
{
    BY_HANDLE_FILE_INFORMATION expected, actual;
    HANDLE reopened, deleting;
    DWORD sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    int identified = GetFileInformationByHandle(file, &expected) != 0;
    if (dump_disposition(file, 1)) { CloseHandle(file); return; }
    CloseHandle(file);
    if (!identified || !stage) return;
    reopened = CreateFileW(stage, DELETE | FILE_READ_ATTRIBUTES, sharing, NULL, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (reopened == INVALID_HANDLE_VALUE) return;
    if (GetFileInformationByHandle(reopened, &actual) &&
        actual.dwVolumeSerialNumber == expected.dwVolumeSerialNumber &&
        actual.nFileIndexHigh == expected.nFileIndexHigh && actual.nFileIndexLow == expected.nFileIndexLow &&
        actual.ftCreationTime.dwHighDateTime == expected.ftCreationTime.dwHighDateTime &&
        actual.ftCreationTime.dwLowDateTime == expected.ftCreationTime.dwLowDateTime) {
        deleting = ReOpenFile(reopened, DELETE, sharing, FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OPEN_REPARSE_POINT);
        if (deleting != INVALID_HANDLE_VALUE) CloseHandle(deleting);
        else (void)dump_disposition(reopened, 1);
    }
    CloseHandle(reopened);
}

static xxwidgets_status dump_stage(const wchar_t *destination, HANDLE *file, wchar_t **stage_path)
{
    const wchar_t *name = wcsrchr(destination, L'\\');
    size_t directory = (size_t)(name - destination) + 1, capacity = directory + 96;
    wchar_t *stage;
    int attempt;
    DWORD error = ERROR_FILE_EXISTS;
    *file = INVALID_HANDLE_VALUE;
    *stage_path = NULL;
    if (capacity > XX_DUMP_MAX_PATH) return XXWIDGETS_INVALID_ARGUMENT;
    stage = (wchar_t *)malloc(capacity * sizeof(*stage));
    if (!stage) return XXWIDGETS_OUT_OF_MEMORY;
    memcpy(stage, destination, directory * sizeof(*stage));
    for (attempt = 0; attempt < 128; ++attempt) {
        DWORD serial = (DWORD)InterlockedIncrement(&xx_dump_stage_serial);
        if (swprintf(stage + directory, capacity - directory, L".xxwidgets-dump-%lu-%08lX-%08lX.part",
                     (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount(), (unsigned long)serial) < 0) {
            error = ERROR_INVALID_NAME; break;
        }
        if (!_wcsicmp(stage, destination)) continue;
        /* DELETE is needed for safe handle-based cleanup and publication. */
        *file = CreateFileW(stage, GENERIC_WRITE | DELETE | FILE_READ_ATTRIBUTES, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (*file != INVALID_HANDLE_VALUE) break;
        error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) break;
    }
    if (*file == INVALID_HANDLE_VALUE) { free(stage); return dump_error(error); }
    *stage_path = stage;
    /* Unlike FILE_FLAG_DELETE_ON_CLOSE, this disposition can be cleared for
     * commit. Failure and process termination otherwise remove the stage. */
    if (!dump_disposition(*file, 1)) return dump_error(GetLastError());
    return XXWIDGETS_OK;
}

static xxwidgets_status dump_publish(HANDLE file, const wchar_t *destination, int overwrite)
{
    int unc = !wcsncmp(destination, L"\\\\?\\UNC\\", 8);
    const wchar_t *name = destination + (unc ? 8 : 4);
    size_t characters = wcslen(name) + (unc ? 2 : 0);
    size_t bytes = characters * sizeof(wchar_t);
    size_t size = offsetof(FILE_RENAME_INFO, FileName) + bytes + sizeof(wchar_t);
    FILE_RENAME_INFO *rename = (FILE_RENAME_INFO *)calloc(1, size);
    DWORD error;
    if (!rename) return XXWIDGETS_OUT_OF_MEMORY;
    rename->ReplaceIfExists = overwrite != 0;
    rename->RootDirectory = NULL;
    rename->FileNameLength = (DWORD)bytes;
    /* FileRenameInfo documents an absolute drive/UNC path. Remove the Win32
     * extended-path prefix used for CreateFile, retaining explicit byte size. */
    if (unc) {
        rename->FileName[0] = rename->FileName[1] = L'\\';
        memcpy(rename->FileName + 2, name, (characters - 1) * sizeof(wchar_t));
    } else memcpy(rename->FileName, name, bytes + sizeof(wchar_t));
    if (!FlushFileBuffers(file)) { error = GetLastError(); free(rename); return dump_error(error); }
    if (!dump_disposition(file, 0)) { error = GetLastError(); free(rename); return dump_error(error); }
    if (!SetFileInformationByHandle(file, FileRenameInfo, rename, (DWORD)size)) {
        error = GetLastError();
        (void)dump_disposition(file, 1);
        free(rename);
        return dump_error(error);
    }
    free(rename);
    return XXWIDGETS_OK;
}

static BOOL CALLBACK dump_memory_callback(PVOID parameter, const PMINIDUMP_CALLBACK_INPUT input, PMINIDUMP_CALLBACK_OUTPUT output)
{
    xx_pd_struct *pd = parameter;
    if (input && output && input->CallbackType == CancelCallback) {
        output->CheckCancel = TRUE;
        output->Cancel = xx_pd_is_stopped(pd);
    }
    return TRUE;
}

static xxwidgets_status dump_memory(HANDLE process, DWORD pid, HANDLE file, xx_pd_struct *pd)
{
    wchar_t library_path[MAX_PATH + 16];
    HMODULE library = NULL;
    xx_dump_write_fn write_dump;
    UINT length;
    xxwidgets_status status = XXWIDGETS_UNAVAILABLE;
    MINIDUMP_CALLBACK_INFORMATION callback = {dump_memory_callback, pd};
    int level = xx_pd_enter_level(pd, 0, "Capturing process memory");
    while (!TryAcquireSRWLockExclusive(&xx_dump_dbghelp_lock)) {
        if (xx_pd_is_stopped(pd)) { xx_pd_leave_level(pd, level); return XXWIDGETS_UNAVAILABLE; }
        Sleep(20);
    }
    length = GetSystemDirectoryW(library_path, MAX_PATH);
    if (!length || length >= MAX_PATH) goto done;
    wcscat(library_path, L"\\dbghelp.dll");
    library = LoadLibraryExW(library_path, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library && GetLastError() == ERROR_INVALID_PARAMETER)
        library = LoadLibraryExW(library_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!library) goto done;
    write_dump = (xx_dump_write_fn)GetProcAddress(library, "MiniDumpWriteDump");
    if (!write_dump) goto done;
    if (!write_dump(process, pid, file, (MINIDUMP_TYPE)(MiniDumpWithFullMemory | MiniDumpWithFullMemoryInfo |
                    MiniDumpWithThreadInfo | MiniDumpIgnoreInaccessibleMemory), NULL, NULL, &callback))
        status = dump_error(GetLastError());
    else status = XXWIDGETS_OK;
done:
    if (library) FreeLibrary(library);
    ReleaseSRWLockExclusive(&xx_dump_dbghelp_lock);
    xx_pd_leave_level(pd, level);
    return status;
}

static xxwidgets_status dump_read(xx_io_device *memory, uintptr_t address, void *buffer, SIZE_T size)
{
    return xx_io_process_read_exact_at(memory, (uint64_t)address, buffer, size) ? XXWIDGETS_OK : dump_error(GetLastError());
}

static xxwidgets_status dump_main_image(xx_io_device *memory, DWORD pid, uintptr_t *base, DWORD *image_size)
{
    MODULEENTRY32W module;
    IMAGE_DOS_HEADER dos;
    struct { DWORD signature; IMAGE_FILE_HEADER file; } nt;
    union { IMAGE_OPTIONAL_HEADER32 pe32; IMAGE_OPTIONAL_HEADER64 pe64; } optional;
    HANDLE snapshot = INVALID_HANDLE_VALUE;
    WORD magic;
    size_t nt_offset, optional_offset, optional_size;
    DWORD headers_size, size;
    xxwidgets_status status = XXWIDGETS_UNAVAILABLE;
    int attempt;
    for (attempt = 0; attempt < 32; ++attempt) {
        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
        if (snapshot != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_BAD_LENGTH) return dump_error(GetLastError());
    }
    if (snapshot == INVALID_HANDLE_VALUE) return XXWIDGETS_UNAVAILABLE;
    memset(&module, 0, sizeof(module)); module.dwSize = sizeof(module);
    if (!Module32FirstW(snapshot, &module)) { status = dump_error(GetLastError()); CloseHandle(snapshot); return status; }
    CloseHandle(snapshot);
    /* The loader's initial module is the executable; validate its PE headers
     * rather than accepting a DLL or trusting the snapshot's image size. */
    *base = (uintptr_t)module.modBaseAddr;
    if (!*base || module.modBaseSize < sizeof(dos) || module.modBaseSize > UINTPTR_MAX - *base) return status;
    status = dump_read(memory, *base, &dos, sizeof(dos));
    if (status != XXWIDGETS_OK) return status;
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < (LONG)sizeof(dos)) return XXWIDGETS_UNAVAILABLE;
    nt_offset = (size_t)dos.e_lfanew;
    if (nt_offset > module.modBaseSize || sizeof(nt) > module.modBaseSize - nt_offset) return XXWIDGETS_UNAVAILABLE;
    status = dump_read(memory, *base + nt_offset, &nt, sizeof(nt));
    if (status != XXWIDGETS_OK) return status;
    if (nt.signature != IMAGE_NT_SIGNATURE || !(nt.file.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) ||
        (nt.file.Characteristics & IMAGE_FILE_DLL)) return XXWIDGETS_UNAVAILABLE;
    optional_offset = nt_offset + sizeof(nt);
    if (optional_offset > module.modBaseSize || sizeof(magic) > module.modBaseSize - optional_offset) return XXWIDGETS_UNAVAILABLE;
    status = dump_read(memory, *base + optional_offset, &magic, sizeof(magic));
    if (status != XXWIDGETS_OK) return status;
    if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) optional_size = sizeof(optional.pe32);
    else if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) optional_size = sizeof(optional.pe64);
    else return XXWIDGETS_UNAVAILABLE;
    if (nt.file.SizeOfOptionalHeader < optional_size || optional_size > module.modBaseSize - optional_offset) return XXWIDGETS_UNAVAILABLE;
    status = dump_read(memory, *base + optional_offset, &optional, optional_size);
    if (status != XXWIDGETS_OK) return status;
    size = magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC ? optional.pe32.SizeOfImage : optional.pe64.SizeOfImage;
    headers_size = magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC ? optional.pe32.SizeOfHeaders : optional.pe64.SizeOfHeaders;
    if (!size || !headers_size || size > module.modBaseSize || headers_size > size ||
        optional_offset + optional_size > headers_size || size > UINTPTR_MAX - *base) return XXWIDGETS_UNAVAILABLE;
    *image_size = size;
    return XXWIDGETS_OK;
}

static xxwidgets_status dump_write(HANDLE file, const unsigned char *buffer, DWORD size)
{
    DWORD offset = 0;
    while (offset < size) {
        DWORD written = 0;
        if (!WriteFile(file, buffer + offset, size - offset, &written, NULL)) return dump_error(GetLastError());
        if (!written) return XXWIDGETS_PLATFORM_ERROR;
        offset += written;
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status dump_image(HANDLE process, xx_io_device *memory_device, DWORD pid, HANDLE file, xx_pd_struct *pd)
{
    uintptr_t base, cursor;
    DWORD image_size;
    size_t offset = 0;
    unsigned char *buffer;
    xxwidgets_status status = dump_main_image(memory_device, pid, &base, &image_size);
    int level;
    if (status != XXWIDGETS_OK) return status;
    level = xx_pd_enter_level(pd, image_size, "Dumping executable image");
    buffer = (unsigned char *)malloc(XX_DUMP_CHUNK);
    if (!buffer) { xx_pd_leave_level(pd, level); return XXWIDGETS_OUT_OF_MEMORY; }
    while (offset < image_size) {
        MEMORY_BASIC_INFORMATION memory;
        uintptr_t region, end;
        size_t chunk, remaining = (size_t)image_size - offset;
        int zero;
        if (xx_pd_is_stopped(pd)) { status = XXWIDGETS_UNAVAILABLE; break; }
        cursor = base + offset;
        if (VirtualQueryEx(process, (LPCVOID)cursor, &memory, sizeof(memory)) != sizeof(memory)) {
            status = dump_error(GetLastError()); break;
        }
        region = (uintptr_t)memory.BaseAddress;
        if (!memory.RegionSize || region > cursor || memory.RegionSize > UINTPTR_MAX - region) {
            status = XXWIDGETS_UNAVAILABLE; break;
        }
        end = region + memory.RegionSize;
        if (end <= cursor) { status = XXWIDGETS_UNAVAILABLE; break; }
        chunk = end - cursor;
        if (chunk > remaining) chunk = remaining;
        if (chunk > XX_DUMP_CHUNK) chunk = XX_DUMP_CHUNK;
        zero = memory.State == MEM_FREE || memory.State == MEM_RESERVE ||
               (memory.State == MEM_COMMIT && (memory.Protect & 0xffu) == PAGE_NOACCESS);
        if (zero) memset(buffer, 0, chunk);
        else {
            /* A guard page is committed but cannot be read without affecting
             * the target. Do not change protection or silently replace it. */
            if (memory.State != MEM_COMMIT || (memory.Protect & PAGE_GUARD) || (uintptr_t)memory.AllocationBase != base) {
                status = XXWIDGETS_UNAVAILABLE; break;
            }
            status = dump_read(memory_device, cursor, buffer, chunk);
            if (status != XXWIDGETS_OK) break;
        }
        status = dump_write(file, buffer, (DWORD)chunk);
        if (status != XXWIDGETS_OK) break;
        offset += chunk;
        xx_pd_set_current(pd, level, offset);
    }
    free(buffer);
    xx_pd_leave_level(pd, level);
    return status;
}

xxwidgets_status xxwidget_systemprocessesviewer_dump_process_ex(const xxwidgets_process_info *process,
    const char *destination, xxwidgets_process_dump_type type, int overwrite, xx_pd_struct *pd)
{
    HANDLE handle = NULL, file = INVALID_HANDLE_VALUE;
    xx_io_device *memory = NULL;
    wchar_t *path = NULL, *stage_path = NULL;
    xxwidgets_status status;
    int published = 0;
    if (!process || !destination || !*destination || process->pid <= 1 || process->pid > MAXDWORD ||
        process->pid == GetCurrentProcessId() || (type != XXWIDGETS_PROCESS_DUMP_MEMORY && type != XXWIDGETS_PROCESS_DUMP_IMAGE))
        return XXWIDGETS_INVALID_ARGUMENT;
    if (!process->start_time) return XXWIDGETS_UNAVAILABLE;
    if (xx_pd_is_stopped(pd)) return XXWIDGETS_UNAVAILABLE;
    status = dump_destination(destination, &path);
    if (status != XXWIDGETS_OK) goto done;
    if (!overwrite) {
        DWORD attributes = GetFileAttributesW(path), error;
        if (attributes != INVALID_FILE_ATTRIBUTES) { status = XXWIDGETS_UNAVAILABLE; goto done; }
        error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) { status = dump_error(error); goto done; }
    }
    handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, (DWORD)process->pid);
    if (!handle) { status = dump_error(GetLastError()); goto done; }
    status = dump_identity(handle, process->start_time);
    if (status != XXWIDGETS_OK) goto done;
    status = dump_same_machine(handle);
    if (status != XXWIDGETS_OK) goto done;
    if (type == XXWIDGETS_PROCESS_DUMP_IMAGE) {
        memory = xx_io_process_open_handle_ex(handle, XX_IO_PROCESS_READ);
        if (!memory) { status = dump_error(GetLastError()); goto done; }
    }
    status = dump_stage(path, &file, &stage_path);
    if (status != XXWIDGETS_OK) goto done;
    status = type == XXWIDGETS_PROCESS_DUMP_MEMORY ? dump_memory(handle, (DWORD)process->pid, file, pd) :
                                                                 dump_image(handle, memory, (DWORD)process->pid, file, pd);
    if (status != XXWIDGETS_OK) goto done;
    if (xx_pd_is_stopped(pd)) { status = XXWIDGETS_UNAVAILABLE; goto done; }
    status = dump_identity(handle, process->start_time);
    if (status != XXWIDGETS_OK) goto done;
    status = dump_publish(file, path, overwrite);
    published = status == XXWIDGETS_OK;
done:
    if (file != INVALID_HANDLE_VALUE) {
        if (!published) dump_close_stage(file, stage_path);
        else CloseHandle(file);
    }
    if (memory) (void)xx_io_process_close(memory);
    if (handle) CloseHandle(handle);
    free(path); free(stage_path);
    return status;
}
xxwidgets_status xxwidget_systemprocessesviewer_dump_process(const xxwidgets_process_info *process,
    const char *destination, xxwidgets_process_dump_type type, int overwrite)
{
    return xxwidget_systemprocessesviewer_dump_process_ex(process, destination, type, overwrite, NULL);
}
#endif /* _WIN32 */
