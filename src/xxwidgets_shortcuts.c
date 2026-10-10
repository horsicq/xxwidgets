#include "xxwidgets_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct xxwidgets_shortcut_binding {
    unsigned int key, modifiers;
    int action;
} shortcut_binding;

static unsigned int upper(unsigned int c)
{
    return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c;
}
static int word(const char *text, const char *name)
{
    while (*text && *name && upper((unsigned char)*text) == upper((unsigned char)*name)) {
        ++text;
        ++name;
    }
    return !*text && !*name;
}
static char *trim(char *text)
{
    size_t length;
    while (*text == ' ' || *text == '\t') ++text;
    length = strlen(text);
    while (length && (text[length - 1] == ' ' || text[length - 1] == '\t')) text[--length] = 0;
    return text;
}
static unsigned int key_name(const char *name)
{
    static const struct {
        const char *name;
        unsigned int key;
    } names[] = {{"Escape", XXWIDGETS_KEY_ESCAPE},
                 {"Esc", XXWIDGETS_KEY_ESCAPE},
                 {"Enter", XXWIDGETS_KEY_ENTER},
                 {"Return", XXWIDGETS_KEY_ENTER},
                 {"Tab", XXWIDGETS_KEY_TAB},
                 {"Backspace", XXWIDGETS_KEY_BACKSPACE},
                 {"Delete", XXWIDGETS_KEY_DELETE},
                 {"Del", XXWIDGETS_KEY_DELETE},
                 {"Insert", XXWIDGETS_KEY_INSERT},
                 {"Ins", XXWIDGETS_KEY_INSERT},
                 {"Home", XXWIDGETS_KEY_HOME},
                 {"End", XXWIDGETS_KEY_END},
                 {"PageUp", XXWIDGETS_KEY_PAGEUP},
                 {"PgUp", XXWIDGETS_KEY_PAGEUP},
                 {"PageDown", XXWIDGETS_KEY_PAGEDOWN},
                 {"PgDown", XXWIDGETS_KEY_PAGEDOWN},
                 {"Up", XXWIDGETS_KEY_UP},
                 {"Down", XXWIDGETS_KEY_DOWN},
                 {"Left", XXWIDGETS_KEY_LEFT},
                 {"Right", XXWIDGETS_KEY_RIGHT},
                 {"Space", ' '},
                 {"Plus", '+'},
                 {"Minus", '-'},
                 {"Comma", ','},
                 {"Period", '.'},
                 {"Slash", '/'},
                 {"Backslash", '\\'},
                 {"Equal", '='},
                 {"Semicolon", ';'},
                 {"Apostrophe", '\''},
                 {"BracketLeft", '['},
                 {"BracketRight", ']'}};
    if (name[0] && !name[1]) {
        unsigned int c = upper((unsigned char)name[0]);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("-.,/\\=;'[]", (int)c)) return c;
    }
    if (upper((unsigned char)name[0]) == 'F' && name[1]) {
        unsigned int number = 0;
        const char *digit = name + 1;
        for (; *digit; ++digit) {
            if (*digit < '0' || *digit > '9' || number > 24) return 0;
            number = number * 10 + (unsigned int)(*digit - '0');
        }
        if (number >= 1 && number <= 24) return XXWIDGETS_KEY_F1 + number - 1;
    }
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (word(name, names[i].name)) return names[i].key;
    return 0;
}

static xxwidgets_status parse(const char *sequence, shortcut_binding *binding)
{
    char buffer[128], *part, *next;
    if (!sequence || strlen(sequence) >= sizeof(buffer)) return XXWIDGETS_INVALID_ARGUMENT;
    memcpy(buffer, sequence, strlen(sequence) + 1);
    binding->key = binding->modifiers = 0;
    part = trim(buffer);
    if (!part[0]) return XXWIDGETS_OK;
    while ((next = strchr(part, '+')) != NULL) {
        unsigned int modifier = 0;
        *next++ = 0;
        part = trim(part);
        if (word(part, "Ctrl") || word(part, "Control")) modifier = XXWIDGETS_MOD_CTRL;
        else if (word(part, "Alt") || word(part, "Option")) modifier = XXWIDGETS_MOD_ALT;
        else if (word(part, "Shift")) modifier = XXWIDGETS_MOD_SHIFT;
        else if (word(part, "Meta") || word(part, "Cmd") || word(part, "Command") || word(part, "Win")) modifier = XXWIDGETS_MOD_META;
        if (!modifier || (binding->modifiers & modifier)) return XXWIDGETS_INVALID_ARGUMENT;
        binding->modifiers |= modifier;
        part = trim(next);
    }
    binding->key = key_name(trim(part));
    return binding->key ? XXWIDGETS_OK : XXWIDGETS_INVALID_ARGUMENT;
}

xxwidgets_status xxwidgets_window_set_shortcuts(xxwidgets_widget *window, const xxwidgets_shortcut *shortcuts, size_t count)
{
    shortcut_binding *bindings;
    size_t used = 0;
    if (!window || window->kind != XXWIDGETS_WINDOW || (!shortcuts && count) || count > SIZE_MAX / sizeof(*bindings)) return XXWIDGETS_INVALID_ARGUMENT;
    bindings = count ? (shortcut_binding *)malloc(count * sizeof(*bindings)) : NULL;
    if (count && !bindings) return XXWIDGETS_OUT_OF_MEMORY;
    for (size_t i = 0; i < count; ++i) {
        shortcut_binding binding;
        xxwidgets_status status = parse(shortcuts[i].sequence, &binding);
        if (status != XXWIDGETS_OK) {
            free(bindings);
            return status;
        }
        if (!binding.key) continue;
        for (size_t j = 0; j < used; ++j)
            if (bindings[j].key == binding.key && bindings[j].modifiers == binding.modifiers) {
                free(bindings);
                return XXWIDGETS_INVALID_ARGUMENT;
            }
        binding.action = shortcuts[i].action;
        bindings[used++] = binding;
    }
    free(window->shortcuts);
    window->shortcuts = bindings;
    window->shortcut_count = used;
    return XXWIDGETS_OK;
}

size_t xxwidgets_window_shortcut_count(const xxwidgets_widget *window)
{
    return window && window->kind == XXWIDGETS_WINDOW ? window->shortcut_count : 0;
}

int xxwidgets_shortcut_dispatch(xxwidgets_widget *window, unsigned int key, unsigned int modifiers)
{
    if (!window || window->kind != XXWIDGETS_WINDOW || !window->visible || !window->enabled || window->app->quit || window->app->syncing ||
        (window->app->modal_window && window->app->modal_window != window))
        return 0;
    key = upper(key);
    for (size_t i = 0; i < window->shortcut_count; ++i) {
        const shortcut_binding *binding = &window->shortcuts[i];
        if (binding->key == key && binding->modifiers == modifiers) {
            int action = binding->action;
            xxwidgets_emit(window, XXWIDGETS_EVENT_SHORTCUT, action);
            return 1;
        }
    }
    return 0;
}
