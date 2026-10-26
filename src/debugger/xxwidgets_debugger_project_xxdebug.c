/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets/xxwidgets_debugger_xxdebug.h"
#include "xxfclib/fs/xx_fs.h"
#include "xxfclib/io/xx_io.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static uint16_t read16(const unsigned char *p, int little)
{
    return little ? (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8) :
        (uint16_t)((uint16_t)p[0] << 8 | (uint16_t)p[1]);
}

static uint32_t read32(const unsigned char *p, int little)
{
    return little ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24 :
        (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static uint64_t read64(const unsigned char *p, int little)
{
    return little ? (uint64_t)read32(p, 1) | (uint64_t)read32(p + 4, 1) << 32 :
        (uint64_t)read32(p, 0) << 32 | read32(p + 4, 0);
}

static int com_suffix(const char *path)
{
    size_t length = strlen(path);
    if (length < 4) return 0;
    path += length - 4;
    return path[0] == '.' && (path[1] == 'c' || path[1] == 'C') &&
        (path[2] == 'o' || path[2] == 'O') && (path[3] == 'm' || path[3] == 'M');
}

#if defined(__linux__) || defined(_WIN32)
static int native_x86(int bits)
{
#if defined(__x86_64__) || defined(_M_X64)
    return bits == 32 || bits == 64;
#elif defined(__i386__) || defined(_M_IX86)
    return bits == 32;
#else
    (void)bits;
    return 0;
#endif
}
#endif

static void native_label(xxwidgets_debugger_project_info *info)
{
#if defined(_WIN32)
    wcscpy(info->native_label, L"Real debugger (Windows)");
#elif defined(__linux__)
    wcscpy(info->native_label, L"Real debugger (Linux)");
#elif defined(__APPLE__)
    wcscpy(info->native_label, L"Real debugger (macOS)");
#else
    wcscpy(info->native_label, L"Real debugger (host)");
#endif
    wcscpy(info->emulator_label, L"Emulator");
}

static void probe_elf(const unsigned char *header, size_t got, int native_available,
    int emulator_available, xxwidgets_debugger_project_info *info)
{
    int bits = got > 4 && header[4] == 1 ? 32 : got > 4 && header[4] == 2 ? 64 : 0;
    int little = got > 5 && header[5] == 1;
    unsigned machine, type, abi;
    int x86, linux_abi;
    wcscpy(info->emulator_label, L"Emulator (Linux)");
    snprintf(info->target, sizeof(info->target), "ELF executable");
    if (!bits || got < (size_t)(bits == 64 ? 64 : 52) ||
        (header[5] != 1 && header[5] != 2) || header[6] != 1) {
        snprintf(info->message, sizeof(info->message), "The ELF header is incomplete or unsupported.");
        return;
    }
    machine = read16(header + 18, little);
    type = read16(header + 16, little);
    abi = header[7];
    x86 = (bits == 32 && machine == 3) || (bits == 64 && machine == 62);
    linux_abi = abi == 0 || abi == 3;
    info->format = bits == 64 ? XXWIDGETS_DEBUGGER_FORMAT_ELF64 : XXWIDGETS_DEBUGGER_FORMAT_ELF32;
    if (linux_abi)
        snprintf(info->target, sizeof(info->target), "ELF%d / %s / Linux%s", bits,
            x86 ? (bits == 64 ? "x86-64" : "x86") : machine == 183 ? "ARM64" : machine == 40 ? "ARM" : "other CPU",
            type == 3 ? " (PIE/shared image)" : "");
    else
        snprintf(info->target, sizeof(info->target), "ELF%d / OS ABI %u / machine %u", bits, abi, machine);
#if defined(__linux__)
    if (native_available && linux_abi && little && x86 && native_x86(bits) && (type == 2 || type == 3))
        info->backends |= XXWIDGETS_DEBUGGER_PROJECT_NATIVE;
#else
    (void)native_available;
#endif
    if (emulator_available && linux_abi && little && x86 && type == 2)
        info->backends |= XXWIDGETS_DEBUGGER_PROJECT_EMULATOR;
    if (!linux_abi)
        snprintf(info->message, sizeof(info->message), "This ELF OS ABI is not supported by the Linux debugging backends.");
    else if (!little || !x86)
        snprintf(info->message, sizeof(info->message), "This debugger supports little-endian x86 ELF targets only.");
    else if (type == 3)
        snprintf(info->message, sizeof(info->message), "ELF emulation requires ET_EXEC; PIE/shared images (ET_DYN) need real debugging.");
    else if (type != 2)
        snprintf(info->message, sizeof(info->message), "Choose an ELF executable; relocatable objects cannot be launched.");
}

static void probe_mz(xx_io_device *device, int64_t size, const unsigned char *header, size_t got,
    int native_available, int emulator_available, xxwidgets_debugger_project_info *info)
{
    unsigned char pe[26];
    uint32_t offset = got >= 64 ? read32(header + 60, 1) : 0;
    if (offset && (uint64_t)offset + sizeof(pe) <= (uint64_t)size && xx_io_read_at(device, offset, pe, sizeof(pe)) &&
        !memcmp(pe, "PE\0\0", 4)) {
        unsigned machine = read16(pe + 4, 1), magic = read16(pe + 24, 1);
        int bits = magic == 0x10b ? 32 : magic == 0x20b ? 64 : 0;
        int x86 = (bits == 32 && machine == 0x14c) || (bits == 64 && machine == 0x8664);
        wcscpy(info->emulator_label, L"Emulator (Windows)");
        snprintf(info->target, sizeof(info->target), "PE%d / %s / Windows", bits,
            x86 ? (bits == 64 ? "x86-64" : "x86") : machine == 0xaa64 ? "ARM64" : "other CPU");
        if (bits) info->format = bits == 64 ? XXWIDGETS_DEBUGGER_FORMAT_PE64 : XXWIDGETS_DEBUGGER_FORMAT_PE32;
#if defined(_WIN32)
        if (native_available && x86 && native_x86(bits)) info->backends |= XXWIDGETS_DEBUGGER_PROJECT_NATIVE;
#else
        (void)native_available;
#endif
        if (emulator_available && x86) info->backends |= XXWIDGETS_DEBUGGER_PROJECT_EMULATOR;
        if (!x86) snprintf(info->message, sizeof(info->message), "This debugger supports x86 PE32 and PE64 targets only.");
        return;
    }
    info->format = XXWIDGETS_DEBUGGER_FORMAT_MZ;
    wcscpy(info->emulator_label, L"Emulator (DOS)");
    snprintf(info->target, sizeof(info->target), "MZ / x86 / DOS");
    if (got < 28) snprintf(info->message, sizeof(info->message), "The DOS executable header is incomplete.");
    else if (emulator_available) info->backends |= XXWIDGETS_DEBUGGER_PROJECT_EMULATOR;
}

static int macho_arm64(const unsigned char *header, size_t got)
{
    return got >= 32 && read32(header, 1) == UINT32_C(0xfeedfacf) &&
        read32(header + 4, 1) == UINT32_C(0x0100000c) && read32(header + 12, 1) == 2;
}

static void probe_macho(xx_io_device *device, int64_t size, const unsigned char *header, size_t got,
    int native_available, xxwidgets_debugger_project_info *info)
{
    uint32_t magic = got >= 4 ? read32(header, 0) : 0;
    int arm64 = macho_arm64(header, got);
    int fat = magic == UINT32_C(0xcafebabe) || magic == UINT32_C(0xbebafeca) ||
        magic == UINT32_C(0xcafebabf) || magic == UINT32_C(0xbfbafeca);
    if (fat && got >= 8) {
        int little = magic == UINT32_C(0xbebafeca) || magic == UINT32_C(0xbfbafeca);
        int wide = magic == UINT32_C(0xcafebabf) || magic == UINT32_C(0xbfbafeca);
        unsigned count = read32(header + 4, little), stride = wide ? 32u : 20u;
        /* Bound work even for a malformed universal header. */
        if (count <= 64 && 8u + (uint64_t)count * stride <= (uint64_t)size) {
            for (unsigned i = 0; i < count && !arm64; ++i) {
                unsigned char slice[32], image[32];
                if (!xx_io_read_at(device, 8 + (int64_t)i * stride, slice, stride)) break;
                if (read32(slice, little) != UINT32_C(0x0100000c)) continue;
                uint64_t offset = wide ? read64(slice + 8, little) : read32(slice + 8, little);
                uint64_t length = wide ? read64(slice + 16, little) : read32(slice + 12, little);
                if (length >= sizeof(image) && offset <= (uint64_t)size && length <= (uint64_t)size - offset &&
                    xx_io_read_at(device, (int64_t)offset, image, sizeof(image))) arm64 = macho_arm64(image, sizeof(image));
            }
        }
    }
    snprintf(info->target, sizeof(info->target), "%s / %s / macOS", fat ? "Universal Mach-O" : "Mach-O",
        arm64 ? "ARM64" : "other CPU");
#if defined(__APPLE__) && (defined(__arm64__) || defined(__aarch64__))
    if (native_available && arm64) info->backends |= XXWIDGETS_DEBUGGER_PROJECT_NATIVE;
#else
    (void)native_available;
#endif
    snprintf(info->message, sizeof(info->message), "Mach-O needs the real ARM64 debugger on macOS; this adapter cannot emulate it.");
}

xxwidgets_status xxwidgets_debugger_xxdebug_probe_project(const char *program,
    xxwidgets_debugger_project_info *info, void *user_data)
{
    unsigned char header[64];
    xx_io_device *device;
    int64_t size;
    size_t got;
    int native_available = xxdebug_backend_native() != NULL;
    int emulator_available = xxdebug_backend_emul() != NULL;
    uint32_t magic;
    (void)user_data;
    if (!program || !info) return XXWIDGETS_INVALID_ARGUMENT;
    memset(info, 0, sizeof(*info));
    native_label(info);
    if (!program[0]) {
        snprintf(info->message, sizeof(info->message), "Choose a program to detect debugging options.");
        return XXWIDGETS_OK;
    }
    /* Do not open directories, FIFOs, devices or other non-file paths. */
    if (!xx_fs_is_file(program) || !(device = xx_io_file_open(program, "rb"))) {
        snprintf(info->message, sizeof(info->message), "Choose a readable regular program file.");
        return XXWIDGETS_OK;
    }
    size = xx_io_total_size(device);
    got = size > 0 && size < (int64_t)sizeof(header) ? (size_t)size : sizeof(header);
    if (size <= 0 || !xx_io_read_at(device, 0, header, got)) {
        xx_io_close(device);
        snprintf(info->message, sizeof(info->message), "The program file is empty or could not be read.");
        return XXWIDGETS_OK;
    }
    magic = got >= 4 ? read32(header, 0) : 0;
    if (got >= 4 && !memcmp(header, "\177ELF", 4))
        probe_elf(header, got, native_available, emulator_available, info);
    else if (got >= 2 && ((!memcmp(header, "MZ", 2)) || !memcmp(header, "ZM", 2)))
        probe_mz(device, size, header, got, native_available, emulator_available, info);
    else if (magic == UINT32_C(0xfeedface) || magic == UINT32_C(0xcefaedfe) ||
        magic == UINT32_C(0xfeedfacf) || magic == UINT32_C(0xcffaedfe) ||
        magic == UINT32_C(0xcafebabe) || magic == UINT32_C(0xbebafeca) ||
        magic == UINT32_C(0xcafebabf) || magic == UINT32_C(0xbfbafeca))
        probe_macho(device, size, header, got, native_available, info);
    else if (com_suffix(program)) {
        info->format = XXWIDGETS_DEBUGGER_FORMAT_COM;
        wcscpy(info->emulator_label, L"Emulator (DOS)");
        snprintf(info->target, sizeof(info->target), "COM / x86 / DOS");
        if (emulator_available) info->backends |= XXWIDGETS_DEBUGGER_PROJECT_EMULATOR;
    } else {
        snprintf(info->target, sizeof(info->target), "%s", got >= 2 && !memcmp(header, "#!", 2) ? "Host script" : "Unknown executable format");
        if (native_available) info->backends |= XXWIDGETS_DEBUGGER_PROJECT_NATIVE;
        snprintf(info->message, sizeof(info->message), "The host loader will validate this file; no emulator format was detected.");
    }
    xx_io_close(device);
    if (!info->backends && !info->message[0])
        snprintf(info->message, sizeof(info->message), "No compiled debugger backend supports this target on this host.");
    return XXWIDGETS_OK;
}
