/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets/xxwidgets_memory_map.h"
#include "xxwidgets/xxwidgets_disk_write.h"
#include "xxwidgets_internal.h"
#include "xxfclib/io/xx_io.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <wchar.h>
#else
#include <sys/stat.h>
#endif

/* Files are created exclusively, including the manifest. A failed operation
 * removes only files that this operation successfully created. */
static xxwidgets_status export_ready(xxwidgets_memory_map *view, xxwidgets_widget **table)
{
    *table = xxwidgets_memory_map_control(view, XXWIDGETS_MEMORY_MAP_TABLE);
    if (!*table) return XXWIDGETS_INVALID_ARGUMENT;
    if ((*table)->app->dispatch_depth || (*table)->app->polling || (*table)->app->syncing) return XXWIDGETS_BUSY;
    return XXWIDGETS_OK;
}

static int valid_path(const char *path)
{
    return path && path[0] && xxwidgets_valid_utf8(path);
}

#ifdef _WIN32
static wchar_t *wide_path(const char *path)
{
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *wide = count ? malloc((size_t)count * sizeof(*wide)) : NULL;
    if (wide) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, count);
    return wide;
}

static wchar_t *absolute_path(const char *path)
{
    wchar_t *wide = wide_path(path), *absolute = NULL;
    DWORD count;
    if (!wide) return NULL;
    count = GetFullPathNameW(wide, 0, NULL, NULL);
    if (count) {
        absolute = malloc((size_t)count * sizeof(*absolute));
        if (absolute) {
            DWORD length = GetFullPathNameW(wide, count, absolute, NULL);
            if (!length || length >= count) { free(absolute); absolute = NULL; }
        }
    }
    free(wide);
    return absolute;
}
#endif

static int same_path(const char *a, const char *b)
{
    if (!valid_path(a) || !valid_path(b)) return 0;
#ifdef _WIN32
    {
        wchar_t *left = absolute_path(a), *right = absolute_path(b);
        int same = left && right ? !_wcsicmp(left, right) : !strcmp(a, b);
        free(left); free(right);
        return same;
    }
#else
    /* Existing destinations cannot be opened with wbx, including aliases of
     * the source. Comparing inode identities also gives a useful early error. */
    {
        struct stat left, right;
        return !strcmp(a, b) || (!stat(a, &left) && !stat(b, &right) && left.st_dev == right.st_dev && left.st_ino == right.st_ino);
    }
#endif
}

static int source_path(xxwidgets_memory_map *view, xx_io_device *source, const char *path)
{
    xxwidgets_widget *label = xxwidgets_memory_map_control(view, XXWIDGETS_MEMORY_MAP_PATH);
    return same_path(source ? xx_io_source_path(source) : NULL, path) || same_path(label ? label->text : NULL, path);
}

static int directory_exists(const char *path)
{
#ifdef _WIN32
    wchar_t *wide = wide_path(path);
    DWORD attributes = wide ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
    free(wide);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat info;
    return !stat(path, &info) && S_ISDIR(info.st_mode);
#endif
}

static char *join_path(const char *directory, const char *name)
{
    size_t a = strlen(directory), b = strlen(name);
    char *path;
    if (a > SIZE_MAX - b - 2) return NULL;
    path = malloc(a + b + 2);
    if (!path) return NULL;
    memcpy(path, directory, a);
    if (a && directory[a - 1] != '/' && directory[a - 1] != '\\') path[a++] = '/';
    memcpy(path + a, name, b + 1);
    return path;
}

typedef struct map_export_row {
    xxwidgets_memory_map_record record;
    size_t model;
    char name[513];
} map_export_row;
typedef struct map_export_job {
    map_export_row *rows;
    size_t count;
    xx_io_device *source;
    const char *path;
    char *source_name, *label_name;
    uint64_t bytes, total, files, file_total;
    int bytes_known;
    xx_pd_struct *progress;
    xxwidgets_disk_write_reporter *reporter;
} map_export_job;

static void free_snapshot(map_export_job *job)
{
    free(job->rows); free(job->source_name); free(job->label_name);
}

/* Snapshot table/filter state on the UI thread. Workers receive no widget
 * pointer and borrow only the source, held exclusively by the modal host. */
static xxwidgets_status snapshot(map_export_job *job, xxwidgets_memory_map *view, xxwidgets_widget *table, size_t index)
{
    size_t count = index == SIZE_MAX ? xxwidgets_tabular_rows(table) : 1;
    xxwidgets_widget *label = xxwidgets_memory_map_control(view, XXWIDGETS_MEMORY_MAP_PATH);
    const char *input = job->source ? xx_io_source_path(job->source) : NULL;
    job->rows = calloc(count ? count : 1, sizeof(*job->rows));
    job->source_name = xxwidgets_strdup(input ? input : "");
    job->label_name = xxwidgets_strdup(label && label->text ? label->text : "");
    if (!job->rows || !job->source_name || !job->label_name) return XXWIDGETS_OUT_OF_MEMORY;
    for (size_t shown = 0; shown < count; ++shown) {
        map_export_row *row = job->rows + shown;
        xxwidgets_status status;
        row->model = index == SIZE_MAX ? xxwidgets_tabular_model_row(table, shown) : index;
        status = xxwidgets_memory_map_table_get_row(table, row->model, &row->record);
        if (status != XXWIDGETS_OK) return status;
        snprintf(row->name, sizeof(row->name), "%s", row->record.name ? row->record.name : "");
        row->record.name = row->name;
        row->record.type = NULL;
        ++job->count;
    }
    return XXWIDGETS_OK;
}

static int job_source_path(const map_export_job *job, const char *path)
{
    return same_path(job->source_name, path) || same_path(job->label_name, path);
}

static int write_bytes(map_export_job *job, xx_io_device *destination, const void *bytes, size_t length)
{
    const unsigned char *data = bytes;
    while (length) {
        size_t chunk = length > 65536 ? 65536 : length;
        ssize_t written;
        if (xx_pd_is_stopped(job->progress)) return 0;
        written = xx_io_write(destination, data, chunk);
        if (written <= 0 || (size_t)written > chunk || (uint64_t)written > UINT64_MAX - job->bytes) return 0;
        job->bytes += (uint64_t)written;
        (void)xxwidgets_disk_write_set_bytes(job->reporter, job->bytes, job->total, job->bytes_known);
        data += written;
        length -= (size_t)written;
    }
    return 1;
}

static int write_text(map_export_job *job, xx_io_device *destination, const char *text)
{
    return write_bytes(job, destination, text, strlen(text));
}

static int write_escaped_cell(map_export_job *job, xx_io_device *destination, const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        char escaped[5];
        const char *chunk = escaped;
        size_t length = 1;
        if (*p == '\t') { chunk = "\\t"; length = 2; }
        else if (*p == '\r') { chunk = "\\r"; length = 2; }
        else if (*p == '\n') { chunk = "\\n"; length = 2; }
        else if (*p == '\\') { chunk = "\\\\"; length = 2; }
        else if (*p < 32 || *p == 127) { snprintf(escaped, sizeof(escaped), "\\x%02x", *p); length = 4; }
        else escaped[0] = (char)*p;
        if (!write_bytes(job, destination, chunk, length)) return 0;
        ++p;
    }
    return 1;
}

static void hex_number(char *out, uint64_t value, int absent)
{
    if (absent) out[0] = 0;
    else snprintf(out, 32, value <= UINT32_MAX ? "%08" PRIx64 : "%016" PRIx64, value);
}

static xxwidgets_status save_worker(void *user, xx_pd_struct *progress, xxwidgets_disk_write_reporter *reporter)
{
    map_export_job *job = user;
    xx_io_device *destination;
    xxwidgets_status status = XXWIDGETS_OK;
    job->progress = progress; job->reporter = reporter;
    if (xx_pd_is_stopped(progress)) return XXWIDGETS_UNAVAILABLE;
    (void)xxwidgets_disk_write_set_file(reporter, job->path, 0, 1, 1);
    destination = xx_io_file_open(job->path, "wbx");
    if (!destination) return XXWIDGETS_PLATFORM_ERROR;
    if (!write_text(job, destination, "Offset\tAddress\tSize\tName\n")) status = XXWIDGETS_PLATFORM_ERROR;
    for (size_t shown = 0; status == XXWIDGETS_OK && shown < job->count; ++shown) {
        xxwidgets_memory_map_record row = job->rows[shown].record;
        char offset[32], address[32], size[32], numbers[100];
        hex_number(offset, (uint64_t)row.offset, row.offset < 0 || (row.flags & XXWIDGETS_MEMORY_MAP_VIRTUAL));
        hex_number(address, row.address, row.address == XXWIDGETS_MEMORY_MAP_INVALID_ADDRESS);
        hex_number(size, row.size, 0);
        snprintf(numbers, sizeof(numbers), "%s\t%s\t%s\t", offset, address, size);
        if (!write_text(job, destination, numbers) || !write_escaped_cell(job, destination, row.name) || !write_text(job, destination, "\n"))
            status = XXWIDGETS_PLATFORM_ERROR;
    }
    if (xx_io_close(destination)) status = XXWIDGETS_PLATFORM_ERROR;
    if (xx_pd_is_stopped(progress)) status = XXWIDGETS_UNAVAILABLE;
    if (status != XXWIDGETS_OK) (void)xx_io_file_remove_a(job->path);
    else {
        (void)xxwidgets_disk_write_set_bytes(reporter, job->bytes, job->bytes, 1);
        (void)xxwidgets_disk_write_set_file(reporter, job->path, 1, 1, 1);
    }
    return status;
}

xxwidgets_status xxwidgets_memory_map_save(xxwidgets_memory_map *view, const char *path)
{
    xxwidgets_widget *table;
    map_export_job job = {0};
    xxwidgets_status status = export_ready(view, &table);
    if (status != XXWIDGETS_OK) return status;
    if (!valid_path(path) || source_path(view, NULL, path)) return XXWIDGETS_INVALID_ARGUMENT;
    job.path = path;
    status = snapshot(&job, view, table, SIZE_MAX);
    if (status == XXWIDGETS_OK) status = xxwidgets_disk_write_run(table->parent, "Saving Memory Map", path, save_worker, &job, NULL);
    free_snapshot(&job);
    return status;
}

static xxwidgets_status dump_range(map_export_job *job, const xxwidgets_memory_map_record *row, const char *path)
{
    xx_io_device *destination;
    unsigned char buffer[65536];
    uint64_t position = 0;
    xxwidgets_status status = XXWIDGETS_OK;
    if (row->offset < 0 || (row->flags & XXWIDGETS_MEMORY_MAP_VIRTUAL)) return XXWIDGETS_UNAVAILABLE;
    if (row->size > INT64_MAX || !xx_io_check(job->source, row->offset, (int64_t)row->size)) return XXWIDGETS_UNAVAILABLE;
    if (xx_pd_is_stopped(job->progress)) return XXWIDGETS_UNAVAILABLE;
    destination = xx_io_file_open(path, "wbx");
    if (!destination) return XXWIDGETS_PLATFORM_ERROR;
    while (position < row->size) {
        size_t length = row->size - position < sizeof(buffer) ? (size_t)(row->size - position) : sizeof(buffer);
        if (xx_pd_is_stopped(job->progress) || !xx_io_read_at(job->source, row->offset + (int64_t)position, buffer, length) || !write_bytes(job, destination, buffer, length)) {
            status = XXWIDGETS_PLATFORM_ERROR;
            break;
        }
        position += length;
    }
    if (xx_io_close(destination)) status = XXWIDGETS_PLATFORM_ERROR;
    if (xx_pd_is_stopped(job->progress)) status = XXWIDGETS_UNAVAILABLE;
    if (status != XXWIDGETS_OK) (void)xx_io_file_remove_a(path);
    return status;
}

static xxwidgets_status dump_worker(void *user, xx_pd_struct *progress, xxwidgets_disk_write_reporter *reporter)
{
    map_export_job *job = user;
    xxwidgets_status status;
    job->progress = progress; job->reporter = reporter;
    job->total = job->rows[0].record.size; job->bytes_known = 1;
    (void)xxwidgets_disk_write_set_bytes(reporter, 0, job->total, 1);
    (void)xxwidgets_disk_write_set_file(reporter, job->path, 0, 1, 1);
    status = dump_range(job, &job->rows[0].record, job->path);
    if (status == XXWIDGETS_OK) (void)xxwidgets_disk_write_set_file(reporter, job->path, 1, 1, 1);
    return status;
}

xxwidgets_status xxwidgets_memory_map_dump(xxwidgets_memory_map *view, xx_io_device *source, size_t row_index, const char *path)
{
    xxwidgets_widget *table;
    map_export_job job = {0};
    xxwidgets_status status = export_ready(view, &table);
    if (status != XXWIDGETS_OK) return status;
    if (!source || !valid_path(path) || source_path(view, source, path)) return XXWIDGETS_INVALID_ARGUMENT;
    /* Selections and public table getters use model indexes. */
    if (xxwidgets_tabular_display_row(table, row_index) == SIZE_MAX) return XXWIDGETS_UNAVAILABLE;
    job.source = source; job.path = path;
    status = snapshot(&job, view, table, row_index);
    if (status == XXWIDGETS_OK) status = xxwidgets_disk_write_run(table->parent, "Dumping memory region", path, dump_worker, &job, NULL);
    free_snapshot(&job);
    return status;
}

static void safe_name(char *destination, const char *name)
{
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p && used < 80; ++p)
        destination[used++] = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
                              *p == '-' || *p == '_' || *p == '.' ? (char)*p : '_';
    while (used && destination[used - 1] == '.') --used;
    if (!used) { memcpy(destination, "region", 6); used = 6; }
    destination[used] = 0;
    /* Every leaf also has a numeric index prefix, so CON/AUX/COM1 etc. can
     * never become a reserved device leaf; separators/ADS are replaced above. */
}

static xxwidgets_status dump_all_worker(void *user, xx_pd_struct *progress, xxwidgets_disk_write_reporter *reporter)
{
    map_export_job *job = user;
    const char *directory = job->path;
    xx_io_device *manifest = NULL;
    char *manifest_path = NULL, **created = NULL;
    size_t created_count = 0, count;
    xxwidgets_status status = XXWIDGETS_OK;
    job->progress = progress; job->reporter = reporter;
    if (!directory_exists(directory)) return XXWIDGETS_INVALID_ARGUMENT;
    if (xx_pd_is_stopped(progress)) return XXWIDGETS_UNAVAILABLE;
    count = job->count;
    job->file_total = 1;
    for (size_t i = 0; i < count; ++i)
        if (job->rows[i].record.offset >= 0 && !(job->rows[i].record.flags & XXWIDGETS_MEMORY_MAP_VIRTUAL)) ++job->file_total;
    created = calloc(count ? count : 1, sizeof(*created));
    manifest_path = join_path(directory, "memory-map.patch.json");
    if (!created || !manifest_path) { status = XXWIDGETS_OUT_OF_MEMORY; goto finish; }
    if (job_source_path(job, manifest_path)) { status = XXWIDGETS_INVALID_ARGUMENT; goto finish; }
    (void)xxwidgets_disk_write_set_file(reporter, manifest_path, 0, job->file_total, 1);
    manifest = xx_io_file_open(manifest_path, "wbx");
    if (!manifest) { status = XXWIDGETS_PLATFORM_ERROR; goto finish; }
    if (!write_text(job, manifest, "{\n  \"version\": 1,\n  \"regions\": [\n")) { status = XXWIDGETS_PLATFORM_ERROR; goto finish; }
    for (size_t shown = 0; shown < count; ++shown) {
        xxwidgets_memory_map_record row = job->rows[shown].record;
        size_t model = job->rows[shown].model;
        char name[81], leaf[200], json[400];
        char *path = NULL;
        unsigned suffix;
        if (xx_pd_is_stopped(progress)) { status = XXWIDGETS_UNAVAILABLE; goto finish; }
        if (row.offset < 0 || (row.flags & XXWIDGETS_MEMORY_MAP_VIRTUAL)) continue;
        safe_name(name, row.name);
        for (suffix = 0; suffix < 10000; ++suffix) {
            if (suffix) snprintf(leaf, sizeof(leaf), "%04zu_%016" PRIx64 "_%016" PRIx64 "_%s_%u.bin", model, (uint64_t)row.offset, row.size, name, suffix);
            else snprintf(leaf, sizeof(leaf), "%04zu_%016" PRIx64 "_%016" PRIx64 "_%s.bin", model, (uint64_t)row.offset, row.size, name);
            path = join_path(directory, leaf);
            if (!path) { status = XXWIDGETS_OUT_OF_MEMORY; goto finish; }
            if (!xx_io_file_exists_a(path) && !job_source_path(job, path)) break;
            free(path); path = NULL;
        }
        if (!path) { status = XXWIDGETS_PLATFORM_ERROR; goto finish; }
        (void)xxwidgets_disk_write_set_file(reporter, path, job->files, job->file_total, 1);
        status = dump_range(job, &row, path);
        if (status != XXWIDGETS_OK) { free(path); goto finish; }
        created[created_count++] = path;
        ++job->files;
        (void)xxwidgets_disk_write_set_file(reporter, path, job->files, job->file_total, 1);
        snprintf(json, sizeof(json), "%s    {\"index\": %zu, \"offset\": \"%016" PRIx64 "\", \"size\": \"%016" PRIx64 "\", \"file\": \"%s\"}",
                 created_count > 1 ? ",\n" : "", model, (uint64_t)row.offset, row.size, leaf);
        if (!write_text(job, manifest, json)) { status = XXWIDGETS_PLATFORM_ERROR; goto finish; }
    }
    (void)xxwidgets_disk_write_set_file(reporter, manifest_path, job->files, job->file_total, 1);
    if (!write_text(job, manifest, "\n  ]\n}\n")) status = XXWIDGETS_PLATFORM_ERROR;
finish:
    if (manifest) {
        if (xx_io_close(manifest)) status = XXWIDGETS_PLATFORM_ERROR;
        if (xx_pd_is_stopped(progress)) status = XXWIDGETS_UNAVAILABLE;
        if (status != XXWIDGETS_OK) (void)xx_io_file_remove_a(manifest_path);
    }
    if (status == XXWIDGETS_OK) {
        (void)xxwidgets_disk_write_set_bytes(reporter, job->bytes, job->bytes, 1);
        (void)xxwidgets_disk_write_set_file(reporter, manifest_path, job->file_total, job->file_total, 1);
    }
    for (size_t i = 0; i < created_count; ++i) {
        if (status != XXWIDGETS_OK) (void)xx_io_file_remove_a(created[i]);
        free(created[i]);
    }
    free(created); free(manifest_path);
    return status;
}

xxwidgets_status xxwidgets_memory_map_dump_all(xxwidgets_memory_map *view, xx_io_device *source, const char *directory)
{
    xxwidgets_widget *table;
    map_export_job job = {0};
    xxwidgets_status status = export_ready(view, &table);
    if (status != XXWIDGETS_OK) return status;
    if (!source || !valid_path(directory)) return XXWIDGETS_INVALID_ARGUMENT;
    job.source = source; job.path = directory;
    status = snapshot(&job, view, table, SIZE_MAX);
    if (status == XXWIDGETS_OK) status = xxwidgets_disk_write_run(table->parent, "Dumping memory regions", directory, dump_all_worker, &job, NULL);
    free_snapshot(&job);
    return status;
}

xxwidgets_status xxwidgets_memory_map_choose_directory(xxwidgets_widget *owner, char **path, int *accepted)
{
    if (!path || !accepted) return XXWIDGETS_INVALID_ARGUMENT;
    *path = NULL; *accepted = 0;
    if (!owner || owner->kind != XXWIDGETS_WINDOW) return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing ||
        (owner->app->modal_window && owner->app->modal_window != owner)) return XXWIDGETS_BUSY;
#ifdef _WIN32
    if (owner->app->backend != XXWIDGETS_BACKEND_TUI && xxwidgets_widget_native_handle(owner)) {
        BROWSEINFOW browse;
        PIDLIST_ABSOLUTE item;
        wchar_t selected[MAX_PATH];
        HRESULT initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        memset(&browse, 0, sizeof(browse));
        browse.hwndOwner = (HWND)xxwidgets_widget_native_handle(owner);
        browse.lpszTitle = L"Dump all memory map regions";
        browse.ulFlags = BIF_RETURNONLYFSDIRS | (SUCCEEDED(initialized) ? BIF_NEWDIALOGSTYLE : 0);
        item = SHBrowseForFolderW(&browse);
        if (item) {
            if (SHGetPathFromIDListW(item, selected)) {
                int length = WideCharToMultiByte(CP_UTF8, 0, selected, -1, NULL, 0, NULL, NULL);
                *path = length ? malloc((size_t)length) : NULL;
                if (*path) { WideCharToMultiByte(CP_UTF8, 0, selected, -1, *path, length, NULL, NULL); *accepted = 1; }
            }
            CoTaskMemFree(item);
            if (SUCCEEDED(initialized)) CoUninitialize();
            return *accepted ? XXWIDGETS_OK : XXWIDGETS_PLATFORM_ERROR;
        }
        if (SUCCEEDED(initialized)) CoUninitialize();
        return XXWIDGETS_OK;
    }
#endif
    {
        char *chosen = NULL, *slash, *forward;
        xxwidgets_status status = xxwidgets_file_dialog(owner, XXWIDGETS_FILE_DIALOG_SAVE, "Choose dump folder using manifest location", "memory-map.patch.json", &chosen, accepted);
        if (status != XXWIDGETS_OK || !*accepted) { free(chosen); return status; }
        slash = strrchr(chosen, '\\'); forward = strrchr(chosen, '/');
        if (forward && (!slash || forward > slash)) slash = forward;
        if (slash) {
            if (slash == chosen || (slash == chosen + 2 && chosen[1] == ':')) slash[1] = 0;
            else *slash = 0;
            *path = chosen;
        } else { free(chosen); *path = xxwidgets_strdup("."); }
        if (!*path) { *accepted = 0; return XXWIDGETS_OUT_OF_MEMORY; }
        return XXWIDGETS_OK;
    }
}
