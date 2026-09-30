#include "xxwidgets_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef XXWIDGETS_WITH_SETTINGS
#include "xxwidgets/xxwidgets_settings.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#endif

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "shortcuts line %d: %s\n", __LINE__, #expr); exit(1); } } while (0)

static int received, last_action, replace_in_callback;
static void shortcut_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    (void)app;
    CHECK(event->type == XXWIDGETS_EVENT_SHORTCUT && event->widget == user && !event->x && !event->y);
    ++received; last_action = event->value;
    if (replace_in_callback) {
        const xxwidgets_shortcut replacement = {"F5", 99};
        CHECK(xxwidgets_window_set_shortcuts(event->widget, &replacement, 1) == XXWIDGETS_OK);
    }
}

#ifdef XXWIDGETS_WITH_SETTINGS
static void write_ini(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    CHECK(file && fwrite(text, 1, strlen(text), file) == strlen(text));
    CHECK(fclose(file) == 0);
}

static void check_ini(xxwidgets_widget *window)
{
    char path[96];
    char default_name[] = "open", default_sequence[] = "Ctrl+O";
    xx_shortcut defaults[] = {{default_name, default_sequence}, {"refresh", "F5"}, {"extract", "Ctrl+E"}};
    const xx_shortcut duplicates[] = {{"open", "Ctrl+O"}, {"open", "Ctrl+N"}};
    const xxwidgets_shortcut_action actions[] = {{"open", 10}, {"refresh", 11}, {"extract", 12}};
    xx_shortcuts *list = NULL;
#ifdef _WIN32
    unsigned long pid = GetCurrentProcessId();
#else
    unsigned long pid = (unsigned long)getpid();
#endif
    snprintf(path, sizeof(path), "xxwidgets-shortcuts-%lu.ini", pid);
    CHECK(xx_shortcuts_load(path, defaults, 3, &list) == XXFC_OK && xx_shortcuts_count(list) == 3);
    default_name[0] = 'x'; default_sequence[0] = 'x';
    CHECK(!strcmp(xx_shortcuts_at(list, 0)->action, "open") && !strcmp(xx_shortcuts_at(list, 0)->sequence, "Ctrl+O"));
    CHECK(xx_shortcuts_at(list, 3) == NULL);
    xx_shortcuts_destroy(list); list = NULL;
    default_name[0] = 'o'; default_sequence[0] = 'C';
    write_ini(path, "\357\273\277; overrides\n[shortcuts]\nopen=Alt+F4\nextract=\nunknown=unrecognised key\n[unrelated]\nvalue=ignored\n");
    CHECK(xx_shortcuts_load(path, defaults, 3, &list) == XXFC_OK && xx_shortcuts_count(list) == 4);
    CHECK(!strcmp(xx_shortcuts_at(list, 0)->sequence, "Alt+F4") && !strcmp(xx_shortcuts_at(list, 1)->sequence, "F5"));
    CHECK(!strcmp(xx_shortcuts_at(list, 2)->sequence, ""));
    CHECK(xxwidgets_settings_install_shortcuts(window, list, actions, 3) == XXWIDGETS_OK);
    CHECK(xxwidgets_window_shortcut_count(window) == 2);
    xx_shortcuts_destroy(list); list = NULL;
    CHECK(xxwidgets_shortcut_dispatch(window, XXWIDGETS_KEY_F1 + 3, XXWIDGETS_MOD_ALT) && last_action == 10);
    CHECK(xxwidgets_shortcut_dispatch(window, XXWIDGETS_KEY_F1 + 4, 0) && last_action == 11);
    CHECK(!xxwidgets_shortcut_dispatch(window, 'E', XXWIDGETS_MOD_CTRL));
    write_ini(path, "[shortcuts]\nopen=F5\n");
    CHECK(xx_shortcuts_load(path, defaults, 3, &list) == XXFC_OK);
    CHECK(xxwidgets_settings_install_shortcuts(window, list, actions, 3) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_window_shortcut_count(window) == 2); /* Previous bindings survive conflicts. */
    xx_shortcuts_destroy(list); list = NULL;
    {
        const char *bad[] = {"[shortcuts\nopen=Ctrl+O\n", "[shortcuts]\nopen=@XXBool(true)\n", "[shortcuts]\nopen=Ctrl\\0O\n"};
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            write_ini(path, bad[i]);
            CHECK(xx_shortcuts_load(path, defaults, 3, &list) != XXFC_OK && !list);
        }
    }
    CHECK(remove(path) == 0);
    CHECK(xx_shortcuts_load(path, duplicates, 2, &list) == XXFC_ERR_INVALID_ARG && !list);
    CHECK(xx_shortcuts_load(path, NULL, 1, &list) == XXFC_ERR_INVALID_ARG && !list);
    CHECK(xx_shortcuts_load(path, NULL, 0, NULL) == XXFC_ERR_NULL_PARAM);
    CHECK(xx_shortcuts_count(NULL) == 0 && !xx_shortcuts_at(NULL, 0));
    xx_shortcuts_destroy(NULL);
}
#endif

void xxwidgets_test_shortcuts(xxwidgets_app *app, xxwidgets_widget *window, xxwidgets_widget *edit)
{
    xxwidgets_event_fn callback = app->on_event;
    void *user = app->user_data;
    char sequence[] = " Ctrl + Shift + o ";
    xxwidgets_shortcut bindings[] = {{sequence, 41}, {"F24", 42}, {"", 43}};
    const xxwidgets_shortcut conflict[] = {{"Ctrl+O", 1}, {"Control+o", 2}};
    const char *bad[] = {"Ctrl+", "Ctrl+Ctrl+O", "Something+O", "F25", "F0", "Ctrl+O+P"};
    int visible = window->visible;
    app->on_event = shortcut_event; app->user_data = window;
    CHECK(xxwidgets_widget_set_visible(window, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_window_set_shortcuts(NULL, bindings, 3) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_window_set_shortcuts(edit, bindings, 3) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_window_set_shortcuts(window, bindings, 3) == XXWIDGETS_OK && xxwidgets_window_shortcut_count(window) == 2);
    sequence[1] = 'x';
    CHECK(xxwidgets_shortcut_dispatch(window, 'O', XXWIDGETS_MOD_CTRL | XXWIDGETS_MOD_SHIFT));
    CHECK(received == 1 && last_action == 41);
    CHECK(!xxwidgets_shortcut_dispatch(window, 'O', XXWIDGETS_MOD_CTRL));
    CHECK(xxwidgets_shortcut_dispatch(window, XXWIDGETS_KEY_F1 + 23, 0) && last_action == 42);
    CHECK(xxwidgets_window_set_shortcuts(window, conflict, 2) == XXWIDGETS_INVALID_ARGUMENT);
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        xxwidgets_shortcut invalid = {bad[i], 0};
        CHECK(xxwidgets_window_set_shortcuts(window, &invalid, 1) == XXWIDGETS_INVALID_ARGUMENT);
        CHECK(xxwidgets_window_shortcut_count(window) == 2);
    }
    CHECK(xxwidgets_widget_set_enabled(window, 0) == XXWIDGETS_OK);
    CHECK(!xxwidgets_shortcut_dispatch(window, XXWIDGETS_KEY_F1 + 23, 0));
    CHECK(xxwidgets_widget_set_enabled(window, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_set_visible(window, 0) == XXWIDGETS_OK);
    CHECK(!xxwidgets_shortcut_dispatch(window, XXWIDGETS_KEY_F1 + 23, 0));
    CHECK(xxwidgets_widget_set_visible(window, 1) == XXWIDGETS_OK);
    app->modal_window = edit;
    CHECK(!xxwidgets_shortcut_dispatch(window, XXWIDGETS_KEY_F1 + 23, 0));
    app->modal_window = NULL;
    replace_in_callback = 1;
    CHECK(xxwidgets_shortcut_dispatch(window, XXWIDGETS_KEY_F1 + 23, 0));
    replace_in_callback = 0;
    CHECK(xxwidgets_window_shortcut_count(window) == 1);
    CHECK(xxwidgets_shortcut_dispatch(window, XXWIDGETS_KEY_F1 + 4, 0) && last_action == 99);
#ifdef XXWIDGETS_WITH_SETTINGS
    check_ini(window);
#endif
    CHECK(xxwidgets_window_set_shortcuts(window, NULL, 0) == XXWIDGETS_OK && !xxwidgets_window_shortcut_count(window));
    CHECK(xxwidgets_widget_set_visible(window, visible) == XXWIDGETS_OK);
    app->on_event = callback; app->user_data = user;
}
