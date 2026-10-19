/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxwidgets/xxwidget_binaryviewer.h"
#include "xxfclib/formats/executable/pe/xx_pe.h"
#include "xxfclib/formats/executable/elf/xx_elf.h"
#include "xxfclib/formats/executable/macho/xx_macho.h"
#include "xxfclib/io/xx_io.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define BINARY_RECORD_LIMIT ((size_t)1048576)
#define BINARY_STRING_LIMIT ((size_t)1048576)
#define BINARY_STRING_BUDGET ((size_t)67108864)

typedef union binary_record {
    xx_resource_record resource;
    xx_import_record import;
    xx_export_record export;
    xx_symbol_record symbol;
} binary_record;

typedef struct binary_records {
    xxwidgets_kind kind;
    void *items;
    size_t count, capacity, stride, string_bytes;
    int64_t slice_offset;
} binary_records;

static size_t binary_length(const char *text)
{
    size_t length;
    for (length = 0; length <= BINARY_STRING_LIMIT; ++length) if (!text[length]) return length;
    return SIZE_MAX;
}

static int binary_is_kind(xxwidgets_kind kind)
{
    return kind == XXWIDGET_RESOURCESVIEWER || kind == XXWIDGET_IMPORTSVIEWER ||
           kind == XXWIDGET_EXPORTSVIEWER || kind == XXWIDGET_SYMBOLSVIEWER;
}

static xxwidgets_status binary_parse_error(const xx_pd_struct *pd)
{
    return pd && pd->last_error == XXFC_ERR_OUT_OF_MEMORY ? XXWIDGETS_OUT_OF_MEMORY : XXWIDGETS_PLATFORM_ERROR;
}

static char *binary_copy_string(binary_records *records, const char *text)
{
    size_t length;
    char *copy;
    if (!text) return NULL;
    length = binary_length(text);
    if (length > BINARY_STRING_LIMIT || length + 1 > BINARY_STRING_BUDGET - records->string_bytes) return NULL;
    copy = (char *)malloc(length + 1);
    if (copy) {
        memcpy(copy, text, length + 1);
        records->string_bytes += length + 1;
    }
    return copy;
}

static void binary_free_record(xxwidgets_kind kind, void *record)
{
    switch (kind) {
        case XXWIDGET_RESOURCESVIEWER: {
            xx_resource_record *r = (xx_resource_record *)record;
            free(r->type_name); free(r->name); free(r->language_name);
            break;
        }
        case XXWIDGET_IMPORTSVIEWER: {
            xx_import_record *r = (xx_import_record *)record;
            free(r->library_name); free(r->name);
            break;
        }
        case XXWIDGET_EXPORTSVIEWER: {
            xx_export_record *r = (xx_export_record *)record;
            free(r->name); free(r->forwarder);
            break;
        }
        case XXWIDGET_SYMBOLSVIEWER: {
            xx_symbol_record *r = (xx_symbol_record *)record;
            free(r->name); free(r->file_name);
            break;
        }
        default: break;
    }
}

static void binary_records_cleanup(binary_records *records)
{
    size_t i;
    for (i = 0; i < records->count; ++i) binary_free_record(records->kind, (char *)records->items + i * records->stride);
    free(records->items);
    records->items = NULL;
    records->count = records->capacity = 0;
}

static int binary_rebase_offset(int64_t *offset, int64_t base)
{
    if (*offset < 0) return 1;
    if (*offset > INT64_MAX - base) return 0;
    *offset += base;
    return 1;
}

static xxwidgets_status binary_append(binary_records *records, const void *source)
{
    binary_record copy = {0};
    int okay = 1;
    switch (records->kind) {
        case XXWIDGET_RESOURCESVIEWER: {
            const xx_resource_record *r = (const xx_resource_record *)source;
            copy.resource = *r;
            copy.resource.type_name = binary_copy_string(records, r->type_name);
            copy.resource.name = binary_copy_string(records, r->name);
            copy.resource.language_name = binary_copy_string(records, r->language_name);
            okay = (!r->type_name || copy.resource.type_name) && (!r->name || copy.resource.name) && (!r->language_name || copy.resource.language_name);
            if (!binary_rebase_offset(&copy.resource.offset, records->slice_offset)) okay = 0;
            break;
        }
        case XXWIDGET_IMPORTSVIEWER: {
            const xx_import_record *r = (const xx_import_record *)source;
            copy.import = *r;
            copy.import.library_name = binary_copy_string(records, r->library_name);
            copy.import.name = binary_copy_string(records, r->name);
            okay = (!r->library_name || copy.import.library_name) && (!r->name || copy.import.name);
            if (!binary_rebase_offset(&copy.import.offset, records->slice_offset)) okay = 0;
            break;
        }
        case XXWIDGET_EXPORTSVIEWER: {
            const xx_export_record *r = (const xx_export_record *)source;
            copy.export = *r;
            copy.export.name = binary_copy_string(records, r->name);
            copy.export.forwarder = binary_copy_string(records, r->forwarder);
            okay = (!r->name || copy.export.name) && (!r->forwarder || copy.export.forwarder);
            if (!binary_rebase_offset(&copy.export.offset, records->slice_offset)) okay = 0;
            break;
        }
        case XXWIDGET_SYMBOLSVIEWER: {
            const xx_symbol_record *r = (const xx_symbol_record *)source;
            copy.symbol = *r;
            copy.symbol.name = binary_copy_string(records, r->name);
            copy.symbol.file_name = binary_copy_string(records, r->file_name);
            okay = (!r->name || copy.symbol.name) && (!r->file_name || copy.symbol.file_name);
            if (!binary_rebase_offset(&copy.symbol.offset, records->slice_offset) ||
                !binary_rebase_offset(&copy.symbol.record_offset, records->slice_offset)) okay = 0;
            break;
        }
        default: return XXWIDGETS_INVALID_ARGUMENT;
    }
    if (!okay) { binary_free_record(records->kind, &copy); return XXWIDGETS_OUT_OF_MEMORY; }
    if (records->count == records->capacity) {
        size_t next = records->capacity ? records->capacity * 2 : 128;
        void *expanded;
        if (next > BINARY_RECORD_LIMIT || next > SIZE_MAX / records->stride) {
            binary_free_record(records->kind, &copy);
            return XXWIDGETS_UNAVAILABLE;
        }
        expanded = realloc(records->items, next * records->stride);
        if (!expanded) { binary_free_record(records->kind, &copy); return XXWIDGETS_OUT_OF_MEMORY; }
        records->items = expanded;
        records->capacity = next;
    }
    memcpy((char *)records->items + records->count * records->stride, &copy, records->stride);
    ++records->count;
    return XXWIDGETS_OK;
}

#define BINARY_COLLECT_STREAM(kind, plural) \
    static xxwidgets_status binary_collect_##plural(Abstractformat *format, binary_records *records, xx_pd_struct *pd) \
    { \
        xx_##kind##_state *state = xx_format_create_##plural##_reading(format, pd); \
        const xx_##kind##_record *record; \
        xxwidgets_status status = XXWIDGETS_OK; \
        if (!state) return binary_parse_error(pd); \
        while ((record = xx_format_get_current_##kind(format, state)) != NULL) { \
            status = binary_append(records, record); \
            if (status != XXWIDGETS_OK) break; \
            if (!xx_format_##kind##_move_to_next(format, state, pd)) break; \
        } \
        if (status == XXWIDGETS_OK && state->failed) status = binary_parse_error(pd); \
        xx_format_free_##plural##_reading(format, state); \
        return status; \
    }
BINARY_COLLECT_STREAM(resource, resources)
BINARY_COLLECT_STREAM(import, imports)
BINARY_COLLECT_STREAM(export, exports)
BINARY_COLLECT_STREAM(symbol, symbols)
#undef BINARY_COLLECT_STREAM

static xxwidgets_status binary_read_pe(xx_io_device *device, binary_records *records, xx_pd_struct *pd)
{
    xx_pe reader;
    xxwidgets_status status;
    xx_pe_init(&reader, device, 0);
    if (!xx_pe_check_is_valid(&reader.format, pd) || !xx_pe_handle_base_info(&reader.format, pd)) status = binary_parse_error(pd);
    else switch (records->kind) {
        case XXWIDGET_RESOURCESVIEWER: status = binary_collect_resources(&reader.format, records, pd); break;
        case XXWIDGET_IMPORTSVIEWER: status = binary_collect_imports(&reader.format, records, pd); break;
        case XXWIDGET_EXPORTSVIEWER: status = binary_collect_exports(&reader.format, records, pd); break;
        default: status = binary_collect_symbols(&reader.format, records, pd); break;
    }
    xx_pe_destroy(&reader);
    return status;
}

/* st_other is not included in xx_symbol_record. Read only that field at the
 * validated native cursor's record offset; table locations come from xxfclib. */
static int binary_elf_export_visible(const xx_elf *elf, const xx_symbol_record *symbol)
{
    uint8_t other;
    int64_t delta = elf->elf_class == XX_ELF_CLASS_64 ? 5 : 13;
    if (symbol->record_offset < 0 || symbol->record_offset > INT64_MAX - delta ||
        !xx_io_read_at(elf->format.device, symbol->record_offset + delta, &other, 1)) return -1;
    other &= 3U;
    return other == 0 || other == 3; /* DEFAULT and PROTECTED */
}

static int binary_elf_dynamic_symbol(const xx_elf *elf, const xx_symbol_record *symbol)
{
    uint64_t i, offset;
    if (symbol->record_offset < elf->format.base_address) return 0;
    offset = (uint64_t)(symbol->record_offset - elf->format.base_address);
    for (i = 0; i < elf->section_header_count; ++i) {
        const xx_elf_section_header *section = &elf->section_headers[i];
        if (section->type == XX_ELF_SECTION_DYNSYM && offset >= section->offset && offset - section->offset < section->size) return 1;
    }
    return 0;
}

static xxwidgets_status binary_read_elf(xx_io_device *device, binary_records *records, xx_pd_struct *pd)
{
    xx_elf reader;
    xx_symbol_state *state = NULL;
    const xx_symbol_record *symbol;
    xxwidgets_status status = XXWIDGETS_OK;
    xx_elf_init(&reader, device, 0);
    if (!xx_elf_check_is_valid(&reader.format, pd) || !xx_elf_handle_base_info(&reader.format, pd)) { status = binary_parse_error(pd); goto done; }
    if (records->kind == XXWIDGET_RESOURCESVIEWER) { status = binary_collect_resources(&reader.format, records, pd); goto done; }
    if (records->kind == XXWIDGET_SYMBOLSVIEWER) { status = binary_collect_symbols(&reader.format, records, pd); goto done; }
    state = xx_elf_create_symbols_reading(&reader.format, pd);
    if (!state) { status = binary_parse_error(pd); goto done; }
    while ((symbol = xx_elf_get_current_symbol(&reader.format, state)) != NULL) {
        if (symbol->name && symbol->name[0] &&
            (reader.type == XX_ELF_TYPE_REL || binary_elf_dynamic_symbol(&reader, symbol))) {
            if (records->kind == XXWIDGET_IMPORTSVIEWER && symbol->is_undefined) {
                xx_import_record imported = {0};
                imported.name = symbol->name;
                imported.offset = -1;
                imported.address = UINT64_MAX;
                status = binary_append(records, &imported);
            } else if (records->kind == XXWIDGET_EXPORTSVIEWER && !symbol->is_undefined &&
                       (symbol->binding == XX_SYMBOL_BINDING_GLOBAL || symbol->binding == XX_SYMBOL_BINDING_WEAK) &&
                       symbol->kind != XX_SYMBOL_KIND_FILE && symbol->kind != XX_SYMBOL_KIND_SECTION) {
                int visible = binary_elf_export_visible(&reader, symbol);
                if (visible < 0) status = XXWIDGETS_PLATFORM_ERROR;
                else if (visible) {
                    xx_export_record exported = {0};
                    exported.name = symbol->name;
                    exported.address = symbol->address;
                    exported.offset = symbol->offset;
                    status = binary_append(records, &exported);
                }
            }
        }
        if (status != XXWIDGETS_OK || !xx_elf_symbol_move_to_next(&reader.format, state, pd)) break;
    }
    if (status == XXWIDGETS_OK && state->failed) status = binary_parse_error(pd);
done:
    if (state) xx_elf_free_symbols_reading(&reader.format, state);
    xx_elf_destroy(&reader);
    return status;
}

static int64_t binary_macho_offset(const xx_macho *reader, uint64_t address)
{
    uint32_t i;
    if (address == UINT64_MAX) return -1;
    for (i = 0; i < reader->segment_count; ++i) {
        const xx_macho_segment *segment = &reader->segments[i];
        uint64_t delta;
        if (address < segment->virtual_address) continue;
        delta = address - segment->virtual_address;
        if (delta < segment->file_size && segment->file_offset <= (uint64_t)INT64_MAX - delta)
            return (int64_t)(segment->file_offset + delta);
    }
    return -1;
}

static xxwidgets_status binary_read_macho(xx_io_device *device, binary_records *records, xx_pd_struct *pd)
{
    xx_macho reader;
    xx_macho_inspection inspection = {0};
    xxwidgets_status status = XXWIDGETS_OK;
    int i;
    xx_macho_init(&reader, device, 0);
    if (!xx_macho_check_is_valid(&reader.format, pd) || !xx_macho_handle_base_info(&reader.format, pd) ||
        !xx_macho_inspect_parse(&reader, &inspection, pd)) { status = binary_parse_error(pd); goto done; }
    for (i = 0; i < inspection.nSymbolCount; ++i) {
        const xx_macho_inspect_symbol *symbol = &inspection.pSymbols[i];
        if (records->kind == XXWIDGET_SYMBOLSVIEWER) {
            xx_symbol_record record = {0};
            record.name = symbol->pName;
            record.value = symbol->nValue;
            record.address = symbol->bFromSymbolTable && !symbol->bUndefined &&
                             ((symbol->nType & 0x0eU) == 0x0eU || (symbol->nType & 0x0eU) == 2U) ?
                             symbol->nValue : symbol->nExportAddress;
            record.is_debug = (symbol->nType & 0xe0U) != 0;
            if (record.is_debug) record.address = UINT64_MAX;
            record.offset = record.is_debug || (symbol->nType & 0x0eU) == 2U ? -1 : binary_macho_offset(&reader, record.address);
            record.record_offset = symbol->nRecordOffset;
            record.table_index = symbol->nTableIndex;
            record.section_number = symbol->nSection;
            record.native_type = symbol->nType;
            record.native_storage_class = symbol->nDescription; /* Native n_desc encoding. */
            record.is_undefined = symbol->bUndefined != 0;
            record.is_absolute = !record.is_debug && (symbol->nType & 0x0eU) == 2U;
            record.binding = symbol->nDescription & 0xc0U ? XX_SYMBOL_BINDING_WEAK :
                             (symbol->bExternal ? XX_SYMBOL_BINDING_GLOBAL : XX_SYMBOL_BINDING_LOCAL);
            if (record.is_debug && (symbol->nType == 0x64U || symbol->nType == 0x66U)) record.kind = XX_SYMBOL_KIND_FILE;
            status = binary_append(records, &record);
        } else if (records->kind == XXWIDGET_IMPORTSVIEWER && symbol->bFromSymbolTable && symbol->bExternal &&
                   symbol->bUndefined && (!symbol->nValue || (symbol->nType & 0x0eU) == 0x0cU) && symbol->pName && symbol->pName[0]) {
            xx_import_record record = {0};
            unsigned ordinal = symbol->nDescription >> 8;
            record.name = symbol->pName;
            if (ordinal > 0 && ordinal < 0xfeU && ordinal <= (unsigned)inspection.nLibraryCount)
                record.library_name = inspection.pLibraries[ordinal - 1].pName;
            record.offset = -1;
            record.address = UINT64_MAX;
            status = binary_append(records, &record);
        } else if (records->kind == XXWIDGET_EXPORTSVIEWER && symbol->bExported && symbol->pName && symbol->pName[0]) {
            xx_export_record record = {0};
            record.name = symbol->pName;
            record.forwarder = symbol->pForwarder;
            record.address = symbol->nExportAddress;
            record.offset = (symbol->nExportFlags & 3U) == 2U ? -1 : binary_macho_offset(&reader, record.address);
            status = binary_append(records, &record);
        }
        if (status != XXWIDGETS_OK) break;
    }
done:
    xx_macho_inspect_free(&inspection);
    xx_macho_destroy(&reader);
    return status;
}

static uint64_t binary_integer(const unsigned char *bytes, size_t width, int big_endian)
{
    uint64_t value = 0;
    size_t i;
    for (i = 0; i < width; ++i) value = (value << 8) | bytes[big_endian ? i : width - i - 1];
    return value;
}

static int binary_is_macho(const unsigned char *magic)
{
    uint64_t value = binary_integer(magic, 4, 1);
    return value == 0xfeedfaceU || value == 0xcefaedfeU || value == 0xfeedfacfU || value == 0xcffaedfeU;
}

/* Only the universal container header is interpreted here. The selected slice
 * is bounded by an xxfclib subdevice and decoded by the native Mach-O reader. */
static xxwidgets_status binary_select_fat(xx_io_device *parent, xx_io_device **out_slice, int64_t *out_offset)
{
    unsigned char header[8], first[32], magic[4];
    uint64_t tag, count, stride, table_end, offset, size, alignment;
    int is64, big_endian;
    int64_t total = xx_io_size(parent);
    *out_slice = NULL;
    *out_offset = 0;
    if (total < 8 || !xx_io_read_at(parent, 0, header, sizeof(header))) return XXWIDGETS_PLATFORM_ERROR;
    tag = binary_integer(header, 4, 1);
    is64 = tag == 0xcafebabfU || tag == 0xbfbafecaU;
    big_endian = tag == 0xcafebabeU || tag == 0xcafebabfU;
    count = binary_integer(header + 4, 4, big_endian);
    stride = is64 ? 32 : 20;
    if (!count || count > 65536 || count > ((uint64_t)total - 8) / stride) return XXWIDGETS_PLATFORM_ERROR;
    table_end = 8 + count * stride;
    if (!xx_io_read_at(parent, 8, first, (size_t)stride)) return XXWIDGETS_PLATFORM_ERROR;
    offset = binary_integer(first + 8, is64 ? 8 : 4, big_endian);
    size = binary_integer(first + (is64 ? 16 : 12), is64 ? 8 : 4, big_endian);
    alignment = binary_integer(first + (is64 ? 24 : 16), 4, big_endian);
    if (offset < table_end || size < 28 || offset > (uint64_t)total || size > (uint64_t)total - offset ||
        offset > INT64_MAX || size > INT64_MAX || alignment > 63 || (offset & ((UINT64_C(1) << alignment) - 1)) ||
        !xx_io_read_at(parent, (int64_t)offset, magic, sizeof(magic)) || !binary_is_macho(magic)) return XXWIDGETS_PLATFORM_ERROR;
    *out_slice = xx_io_sub_open_ro(parent, (int64_t)offset, (int64_t)size);
    if (!*out_slice) return XXWIDGETS_OUT_OF_MEMORY;
    *out_offset = (int64_t)offset;
    return XXWIDGETS_OK;
}

static xxwidgets_status binary_commit(xxwidgets_widget *widget, const binary_records *records)
{
    switch (records->kind) {
        case XXWIDGET_RESOURCESVIEWER: return xxwidget_resourcesviewer_set_records(widget, (const xx_resource_record *)records->items, records->count);
        case XXWIDGET_IMPORTSVIEWER: return xxwidget_importsviewer_set_records(widget, (const xx_import_record *)records->items, records->count);
        case XXWIDGET_EXPORTSVIEWER: return xxwidget_exportsviewer_set_records(widget, (const xx_export_record *)records->items, records->count);
        case XXWIDGET_SYMBOLSVIEWER: return xxwidget_symbolsviewer_set_records(widget, (const xx_symbol_record *)records->items, records->count);
        default: return XXWIDGETS_INVALID_ARGUMENT;
    }
}

xxwidgets_status xxwidget_binaryviewer_load_file(xxwidgets_widget *widget, const char *path)
{
    binary_records records = {0};
    xx_pd_struct pd = {0};
    xx_io_device *device, *slice = NULL;
    unsigned char magic[4];
    uint64_t tag;
    int64_t size;
    xxwidgets_status status;
    if (!widget || !path || !path[0] || binary_length(path) > BINARY_STRING_LIMIT) return XXWIDGETS_INVALID_ARGUMENT;
    records.kind = xxwidgets_widget_kind(widget);
    if (!binary_is_kind(records.kind)) return XXWIDGETS_INVALID_ARGUMENT;
    switch (records.kind) {
        case XXWIDGET_RESOURCESVIEWER: records.stride = sizeof(xx_resource_record); break;
        case XXWIDGET_IMPORTSVIEWER: records.stride = sizeof(xx_import_record); break;
        case XXWIDGET_EXPORTSVIEWER: records.stride = sizeof(xx_export_record); break;
        default: records.stride = sizeof(xx_symbol_record); break;
    }
    device = xx_io_file_open(path, "rb");
    if (!device) return errno == ENOMEM ? XXWIDGETS_OUT_OF_MEMORY : XXWIDGETS_PLATFORM_ERROR;
    size = xx_io_size(device);
    if (size < 0) status = XXWIDGETS_PLATFORM_ERROR;
    else if (size < 4) status = XXWIDGETS_UNAVAILABLE;
    else if (!xx_io_read_at(device, 0, magic, sizeof(magic))) status = XXWIDGETS_PLATFORM_ERROR;
    else {
        tag = binary_integer(magic, 4, 1);
        if (magic[0] == 'M' && magic[1] == 'Z') status = binary_read_pe(device, &records, &pd);
        else if (tag == 0x7f454c46U) status = binary_read_elf(device, &records, &pd);
        else if (binary_is_macho(magic)) status = binary_read_macho(device, &records, &pd);
        else if (tag == 0xcafebabeU || tag == 0xbebafecaU || tag == 0xcafebabfU || tag == 0xbfbafecaU) {
            status = binary_select_fat(device, &slice, &records.slice_offset);
            if (status == XXWIDGETS_OK) status = binary_read_macho(slice, &records, &pd);
        } else status = XXWIDGETS_UNAVAILABLE;
    }
    if (slice) xx_io_close(slice);
    xx_io_close(device);
    if (status == XXWIDGETS_OK) status = binary_commit(widget, &records);
    binary_records_cleanup(&records);
    return status;
}

#define BINARY_LOAD_WRAPPER(name, expected) \
    xxwidgets_status xxwidget_##name##_load_file(xxwidgets_widget *widget, const char *path) \
    { \
        if (!widget || xxwidgets_widget_kind(widget) != expected) return XXWIDGETS_INVALID_ARGUMENT; \
        return xxwidget_binaryviewer_load_file(widget, path); \
    }
BINARY_LOAD_WRAPPER(resourcesviewer, XXWIDGET_RESOURCESVIEWER)
BINARY_LOAD_WRAPPER(importsviewer, XXWIDGET_IMPORTSVIEWER)
BINARY_LOAD_WRAPPER(exportsviewer, XXWIDGET_EXPORTSVIEWER)
BINARY_LOAD_WRAPPER(symbolsviewer, XXWIDGET_SYMBOLSVIEWER)
#undef BINARY_LOAD_WRAPPER
