#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_context_options.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlobj.h>
#include <wchar.h>

typedef struct registry_target {
    HKEY hive;
    wchar_t *key, *caption, *command, *icon;
} registry_target;
typedef struct registry_value {
    BYTE *data;
    DWORD size, type;
    int present;
} registry_value;

static xxwidgets_status registry_error(LONG error)
{
    SetLastError((DWORD)error);
    return error == ERROR_NOT_ENOUGH_MEMORY || error == ERROR_OUTOFMEMORY
        ? XXWIDGETS_OUT_OF_MEMORY : XXWIDGETS_PLATFORM_ERROR;
}

static int valid_component(const char *text, int file_class)
{
    size_t i;
    if (!text || !text[0]) return 0;
    if (file_class && !strcmp(text, "*")) return 1;
    if (!strcmp(text, ".") || !strcmp(text, "..")) return 0;
    for (i = 0; text[i]; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (i >= 128 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return 0;
    }
    return 1;
}

static wchar_t *wide_utf8(const char *text)
{
    int count;
    wchar_t *wide;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (count <= 0 || (size_t)count > SIZE_MAX / sizeof(*wide)) return NULL;
    wide = (wchar_t *)malloc((size_t)count * sizeof(*wide));
    if (wide && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, count)) {
        free(wide); return NULL;
    }
    return wide;
}

static void free_target(registry_target *target)
{
    free(target->key); free(target->caption); free(target->command); free(target->icon);
    memset(target, 0, sizeof(*target));
}

static xxwidgets_status make_target(const xxwidgets_context_config *config, registry_target *target)
{
    static const wchar_t prefix[] = L"Software\\Classes\\";
    static const wchar_t shell[] = L"\\shell\\";
    const size_t prefix_length = sizeof(prefix) / sizeof(prefix[0]) - 1;
    const size_t shell_length = sizeof(shell) / sizeof(shell[0]) - 1;
    const char *file_class;
    wchar_t *id = NULL, *class_name = NULL, *input = NULL, *absolute = NULL;
    size_t i, length, key_length, class_length, id_length, offset;
    DWORD required, actual, attributes;
    xxwidgets_status status = XXWIDGETS_INVALID_ARGUMENT;
    memset(target, 0, sizeof(*target));
    if (!config) return status;
    file_class = config->file_class && config->file_class[0] ? config->file_class : "*";
    if (!valid_component(config->application_id, 0) || !valid_component(file_class, 1) ||
        !config->caption || !config->caption[0] || !xxwidgets_valid_utf8(config->caption) ||
        !config->executable || !xxwidgets_valid_utf8(config->executable) ||
        (config->scope != XXWIDGETS_CONTEXT_CURRENT_USER && config->scope != XXWIDGETS_CONTEXT_ALL_USERS)) return status;
    for (i = 0; config->caption[i]; ++i)
        if ((unsigned char)config->caption[i] < 32 || config->caption[i] == 127) return status;
    for (i = 0; config->executable[i]; ++i)
        if ((unsigned char)config->executable[i] < 32 || config->executable[i] == 127 ||
            config->executable[i] == '"' || config->executable[i] == '%') return status;
    input = wide_utf8(config->executable);
    id = wide_utf8(config->application_id); class_name = wide_utf8(file_class);
    target->caption = wide_utf8(config->caption);
    if (!input || !id || !class_name || !target->caption) { status = XXWIDGETS_OUT_OF_MEMORY; goto done; }
    for (i = 0; input[i]; ++i) if (input[i] == L'/') input[i] = L'\\';
    length = wcslen(input);
    if (!((length >= 3 && ((input[0] >= L'A' && input[0] <= L'Z') ||
        (input[0] >= L'a' && input[0] <= L'z')) && input[1] == L':' && input[2] == L'\\') ||
        (length >= 5 && input[0] == L'\\' && input[1] == L'\\' &&
        input[2] != L'?' && input[2] != L'.'))) goto done;
    required = GetFullPathNameW(input, 0, NULL, NULL);
    if (!required || required > 32768) goto done;
    absolute = (wchar_t *)malloc((size_t)required * sizeof(*absolute));
    if (!absolute) { status = XXWIDGETS_OUT_OF_MEMORY; goto done; }
    actual = GetFullPathNameW(input, required, absolute, NULL);
    if (!actual || actual >= required) goto done;
    length = wcslen(absolute);
    if (length < 4 || _wcsicmp(absolute + length - 4, L".exe")) goto done;
    attributes = GetFileAttributesW(absolute);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) goto done;
    class_length = wcslen(class_name); id_length = wcslen(id);
    key_length = prefix_length + class_length + shell_length + id_length;
    target->key = (wchar_t *)malloc((key_length + 1) * sizeof(*target->key));
    target->icon = (wchar_t *)malloc((length + 3) * sizeof(*target->icon));
    target->command = (wchar_t *)malloc((length + 8) * sizeof(*target->command));
    if (!target->key || !target->icon || !target->command) { status = XXWIDGETS_OUT_OF_MEMORY; goto done; }
    memcpy(target->key, prefix, prefix_length * sizeof(*target->key));
    offset = prefix_length;
    memcpy(target->key + offset, class_name, class_length * sizeof(*target->key));
    offset += class_length;
    memcpy(target->key + offset, shell, shell_length * sizeof(*target->key));
    offset += shell_length;
    memcpy(target->key + offset, id, (id_length + 1) * sizeof(*target->key));
    target->icon[0] = L'"'; memcpy(target->icon + 1, absolute, length * sizeof(*absolute));
    target->icon[length + 1] = L'"'; target->icon[length + 2] = 0;
    memcpy(target->command, target->icon, (length + 2) * sizeof(*absolute));
    memcpy(target->command + length + 2, L" \"%1\"", 6 * sizeof(*absolute));
    target->hive = config->scope == XXWIDGETS_CONTEXT_CURRENT_USER ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    status = XXWIDGETS_OK;
done:
    free(id); free(class_name); free(input); free(absolute);
    if (status != XXWIDGETS_OK) {
        free_target(target);
        SetLastError(status == XXWIDGETS_OUT_OF_MEMORY ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER);
    }
    return status;
}

static LONG read_value(HKEY key, const wchar_t *name, registry_value *value)
{
    LONG error;
    memset(value, 0, sizeof(*value));
    error = RegQueryValueExW(key, name, NULL, &value->type, NULL, &value->size);
    if (error == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
    if (error != ERROR_SUCCESS) return error;
    if ((size_t)value->size > SIZE_MAX - sizeof(wchar_t)) return ERROR_NOT_ENOUGH_MEMORY;
    value->data = (BYTE *)malloc((size_t)value->size + sizeof(wchar_t));
    if (!value->data) return ERROR_NOT_ENOUGH_MEMORY;
    memset(value->data, 0, (size_t)value->size + sizeof(wchar_t));
    error = RegQueryValueExW(key, name, NULL, &value->type, value->data, &value->size);
    if (error != ERROR_SUCCESS) { free(value->data); value->data = NULL; return error; }
    value->present = 1; return ERROR_SUCCESS;
}

static LONG restore_value(HKEY key, const wchar_t *name, const registry_value *value)
{
    LONG error = value->present ? RegSetValueExW(key, name, 0, value->type, value->data, value->size)
        : RegDeleteValueW(key, name);
    return error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : error;
}

static LONG write_text(HKEY key, const wchar_t *name, const wchar_t *text)
{
    size_t size = (wcslen(text) + 1) * sizeof(*text);
    if (size > MAXDWORD) return ERROR_INVALID_PARAMETER;
    return RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)text, (DWORD)size);
}

/* Preflight every owned descendant before recursive deletion. Failed access
 * must not remove a preceding, accessible branch before reaching the denial. */
static LONG can_delete_tree(HKEY key, unsigned int depth)
{
    DWORD index = 0;
    LONG error;
    if (depth >= 64) return ERROR_BADKEY;
    for (;;) {
        wchar_t name[256]; DWORD count = 256;
        HKEY child;
        error = RegEnumKeyExW(key, index++, name, &count, NULL, NULL, NULL, NULL);
        if (error == ERROR_NO_MORE_ITEMS) return ERROR_SUCCESS;
        if (error != ERROR_SUCCESS) return error;
        error = RegOpenKeyExW(key, name, 0, KEY_READ | KEY_WRITE | DELETE, &child);
        if (error != ERROR_SUCCESS) return error;
        error = can_delete_tree(child, depth + 1);
        RegCloseKey(child);
        if (error != ERROR_SUCCESS) return error;
    }
}
#endif

xxwidgets_status xxwidgets_context_is_registered(const xxwidgets_context_config *config, int *registered)
{
    if (!registered) return XXWIDGETS_INVALID_ARGUMENT;
    *registered = 0;
#ifdef _WIN32
    {
        registry_target target;
        registry_value value = {0};
        HKEY key, command;
        LONG error;
        xxwidgets_status status = make_target(config, &target);
        if (status != XXWIDGETS_OK) return status;
        error = RegOpenKeyExW(target.hive, target.key, 0, KEY_READ, &key);
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) { free_target(&target); return XXWIDGETS_OK; }
        if (error != ERROR_SUCCESS) { free_target(&target); return registry_error(error); }
        error = RegOpenKeyExW(key, L"command", 0, KEY_READ, &command);
        if (error == ERROR_SUCCESS) {
            error = read_value(command, NULL, &value);
            if (error == ERROR_SUCCESS && value.present && value.type == REG_SZ &&
                value.size % sizeof(wchar_t) == 0)
                *registered = !_wcsicmp((const wchar_t *)value.data, target.command);
            free(value.data); RegCloseKey(command);
        } else if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) error = ERROR_SUCCESS;
        RegCloseKey(key); free_target(&target);
        return error == ERROR_SUCCESS ? XXWIDGETS_OK : registry_error(error);
    }
#else
    (void)config;
    return XXWIDGETS_UNAVAILABLE;
#endif
}

xxwidgets_status xxwidgets_context_set_enabled(const xxwidgets_context_config *config, int enabled)
{
    if (enabled != 0 && enabled != 1) return XXWIDGETS_INVALID_ARGUMENT;
#ifdef _WIN32
    {
        registry_target target;
        registry_value caption = {0}, icon = {0}, value = {0};
        HKEY key = NULL, command = NULL;
        DWORD key_disposition = 0, command_disposition = 0;
        int captured = 0;
        LONG error;
        xxwidgets_status status = make_target(config, &target);
        if (status != XXWIDGETS_OK) return status;
        if (!enabled) {
            error = RegOpenKeyExW(target.hive, target.key, 0, KEY_READ | KEY_WRITE | DELETE, &key);
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) error = ERROR_SUCCESS;
            else if (error == ERROR_SUCCESS) {
                error = can_delete_tree(key, 0);
                RegCloseKey(key); key = NULL;
                if (error == ERROR_SUCCESS) error = RegDeleteTreeW(target.hive, target.key);
            }
            goto done;
        }
        error = RegCreateKeyExW(target.hive, target.key, 0, NULL, 0,
            KEY_READ | KEY_WRITE | DELETE, NULL, &key, &key_disposition);
        if (error != ERROR_SUCCESS) goto done;
        error = RegCreateKeyExW(key, L"command", 0, NULL, 0,
            KEY_READ | KEY_WRITE | DELETE, NULL, &command, &command_disposition);
        if (error != ERROR_SUCCESS) goto rollback;
        error = read_value(key, NULL, &caption);
        if (error == ERROR_SUCCESS) error = read_value(key, L"Icon", &icon);
        if (error == ERROR_SUCCESS) error = read_value(command, NULL, &value);
        if (error != ERROR_SUCCESS) goto rollback;
        captured = 1;
        error = write_text(key, NULL, target.caption);
        if (error == ERROR_SUCCESS) error = write_text(key, L"Icon", target.icon);
        if (error == ERROR_SUCCESS) error = write_text(command, NULL, target.command);
        if (error == ERROR_SUCCESS) goto done;
rollback:
        if (key_disposition == REG_CREATED_NEW_KEY) {
            LONG cleanup_error;
            if (command) { RegCloseKey(command); command = NULL; }
            RegCloseKey(key); key = NULL;
            cleanup_error = RegDeleteTreeW(target.hive, target.key);
            if (cleanup_error != ERROR_SUCCESS && cleanup_error != ERROR_FILE_NOT_FOUND)
                error = cleanup_error;
        } else {
            /* All prior values were captured before the first write. */
            if (captured) {
                LONG rollback_error = restore_value(key, NULL, &caption);
                LONG next = restore_value(key, L"Icon", &icon);
                if (rollback_error == ERROR_SUCCESS) rollback_error = next;
                next = restore_value(command, NULL, &value);
                if (rollback_error == ERROR_SUCCESS) rollback_error = next;
                if (rollback_error != ERROR_SUCCESS) error = rollback_error;
            }
            if (command_disposition == REG_CREATED_NEW_KEY) {
                LONG cleanup_error;
                if (command) { RegCloseKey(command); command = NULL; }
                cleanup_error = RegDeleteTreeW(key, L"command");
                if (cleanup_error != ERROR_SUCCESS && cleanup_error != ERROR_FILE_NOT_FOUND)
                    error = cleanup_error;
            }
        }
done:
        if (command) RegCloseKey(command);
        if (key) RegCloseKey(key);
        free(caption.data); free(icon.data); free(value.data); free_target(&target);
        if (error != ERROR_SUCCESS) return registry_error(error);
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
        return XXWIDGETS_OK;
    }
#else
    (void)config;
    return XXWIDGETS_UNAVAILABLE;
#endif
}

xxwidgets_status xxwidgets_context_register(const xxwidgets_context_config *config)
{ return xxwidgets_context_set_enabled(config, 1); }

struct xxwidgets_context_options {
    xxwidgets_widget *owner;
    xxwidgets_widget *controls[XXWIDGETS_CONTEXT_OPTIONS_CONTROL_COUNT];
    xxwidgets_context_config config;
    char *strings[4];
};

xxwidgets_widget *xxwidgets_context_options_control(const xxwidgets_context_options *options,
    xxwidgets_context_options_control_id control)
{
    return options && control >= 0 && control < XXWIDGETS_CONTEXT_OPTIONS_CONTROL_COUNT
        ? options->controls[control] : NULL;
}

xxwidgets_status xxwidgets_context_options_refresh(xxwidgets_context_options *options)
{
    int registered;
    xxwidgets_status status;
    if (!options) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_context_is_registered(&options->config, &registered);
    if (status != XXWIDGETS_OK) return status;
    status = xxwidgets_widget_set_value(options->controls[XXWIDGETS_CONTEXT_OPTIONS_ENABLE], registered);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_set_text(options->controls[XXWIDGETS_CONTEXT_OPTIONS_STATUS],
            registered ? "Explorer context menu is enabled." : "Explorer context menu is disabled.");
    return status;
}

xxwidgets_status xxwidgets_context_options_apply(xxwidgets_context_options *options)
{
    int enabled;
    xxwidgets_status status;
    if (!options) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_widget_get_value(options->controls[XXWIDGETS_CONTEXT_OPTIONS_ENABLE], &enabled);
    if (status == XXWIDGETS_OK) status = xxwidgets_context_set_enabled(&options->config, enabled);
    if (status != XXWIDGETS_OK) {
        xxwidgets_context_options_refresh(options);
        xxwidgets_widget_set_text(options->controls[XXWIDGETS_CONTEXT_OPTIONS_STATUS],
            "Cannot change the context menu. Check permissions and the application path.");
        return status;
    }
    return xxwidgets_context_options_refresh(options);
}

xxwidgets_status xxwidgets_context_options_create(xxwidgets_widget *owner, xxwidgets_rect bounds,
    const xxwidgets_context_config *config, xxwidgets_context_options **out_options)
{
    static const xxwidgets_kind kinds[3] = {XXWIDGETS_CHECKBOX, XXWIDGETS_BUTTON, XXWIDGETS_LABEL};
    xxwidgets_context_options *options;
    xxwidgets_rect rects[3];
    const char *strings[4];
    const char *names[3] = {"Enable Explorer context menu", "Apply", ""};
    int registered;
    size_t i;
    xxwidgets_status status;
    if (!out_options) return XXWIDGETS_INVALID_ARGUMENT;
    *out_options = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !config || bounds.x < 0 || bounds.y < 0 ||
        bounds.width < 36 || bounds.height < 6 || bounds.x > INT_MAX - bounds.width ||
        bounds.y > INT_MAX - bounds.height) return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing) return XXWIDGETS_BUSY;
    status = xxwidgets_context_is_registered(config, &registered);
    if (status != XXWIDGETS_OK) return status;
    options = (xxwidgets_context_options *)calloc(1, sizeof(*options));
    if (!options) return XXWIDGETS_OUT_OF_MEMORY;
    options->owner = owner; options->config.scope = config->scope;
    strings[0] = config->application_id; strings[1] = config->caption;
    strings[2] = config->file_class && config->file_class[0] ? config->file_class : "*";
    strings[3] = config->executable;
    for (i = 0; i < 4; ++i) {
        options->strings[i] = xxwidgets_strdup(strings[i]);
        if (!options->strings[i]) { status = XXWIDGETS_OUT_OF_MEMORY; goto failed; }
    }
    options->config.application_id = options->strings[0]; options->config.caption = options->strings[1];
    options->config.file_class = options->strings[2]; options->config.executable = options->strings[3];
    rects[0] = (xxwidgets_rect){bounds.x, bounds.y, bounds.width, 2};
    rects[1] = (xxwidgets_rect){bounds.x, bounds.y + 2, 10, 2};
    rects[2] = (xxwidgets_rect){bounds.x, bounds.y + 4, bounds.width, bounds.height - 4};
    for (i = 0; i < 3; ++i) {
        status = xxwidgets_widget_create(owner->app, owner, kinds[i], names[i], rects[i], &options->controls[i]);
        if (status != XXWIDGETS_OK) goto failed;
    }
    status = xxwidgets_context_options_refresh(options);
    if (status != XXWIDGETS_OK) goto failed;
    *out_options = options; return XXWIDGETS_OK;
failed:
    xxwidgets_context_options_destroy(options); return status;
}

xxwidgets_status xxwidgets_context_options_destroy(xxwidgets_context_options *options)
{
    size_t i;
    xxwidgets_status status;
    if (!options) return XXWIDGETS_INVALID_ARGUMENT;
    if (options->owner->app->dispatch_depth || options->owner->app->polling || options->owner->app->syncing)
        return XXWIDGETS_BUSY;
    for (i = 0; i < XXWIDGETS_CONTEXT_OPTIONS_CONTROL_COUNT; ++i) {
        if (!options->controls[i]) continue;
        status = xxwidgets_widget_destroy(options->controls[i]);
        if (status != XXWIDGETS_OK) return status;
        options->controls[i] = NULL;
    }
    for (i = 0; i < 4; ++i) free(options->strings[i]);
    free(options); return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_context_options_dialog(xxwidgets_widget *owner, const char *title,
    const xxwidgets_context_config *config, int *accepted)
{
    xxwidgets_option option = {"Enable Explorer context menu", 0};
    int changed = 0;
    xxwidgets_status status;
    if (!accepted) return XXWIDGETS_INVALID_ARGUMENT;
    *accepted = 0;
    status = xxwidgets_context_is_registered(config, &option.value);
    if (status != XXWIDGETS_OK) return status;
    status = xxwidgets_options_dialog(owner, title, &option, 1, &changed);
    if (status != XXWIDGETS_OK || !changed) return status;
    status = xxwidgets_context_set_enabled(config, option.value);
    if (status == XXWIDGETS_OK) *accepted = 1;
    return status;
}
