#include "demo_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct demo {
    xxwidgets_widget *window, *edit, *checkbox, *list, *progress, *status, *apply, *quit;
} demo;

static xxwidgets_app *cleanup_app;

static void require(xxwidgets_status status)
{
    if (status != XXWIDGETS_OK) {
        if (cleanup_app) xxwidgets_app_destroy(cleanup_app);
        fprintf(stderr, "xxwidgets: %s\n", xxwidgets_status_string(status));
        exit(EXIT_FAILURE);
    }
}

static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user_data)
{
    demo *d = (demo *)user_data;
    if (event->type == XXWIDGETS_EVENT_CLOSE ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == d->quit)) {
        xxwidgets_app_quit(app, 0);
    } else if (event->type == XXWIDGETS_EVENT_CLICK && event->widget == d->apply) {
        char name[128], message[256];
        int checked = 0;
        xxwidgets_widget_get_text(d->edit, name, sizeof(name), NULL);
        xxwidgets_widget_get_value(d->checkbox, &checked);
        snprintf(message, sizeof(message), "Hello, %s! Option: %s", name, checked ? "on" : "off");
        xxwidgets_widget_set_text(d->status, message);
        xxwidgets_widget_set_value(d->progress, 100);
    } else if (event->type == XXWIDGETS_EVENT_SELECT) {
        char message[64];
        snprintf(message, sizeof(message), "Selected item %d", event->value + 1);
        xxwidgets_widget_set_text(d->status, message);
        xxwidgets_widget_set_value(d->progress, (event->value + 1) * 25);
    }
}

static xxwidgets_widget *create(xxwidgets_app *app, xxwidgets_widget *parent,
    xxwidgets_kind kind, const char *text, int x, int y, int width, int height)
{
    xxwidgets_widget *widget = NULL;
    xxwidgets_rect rect = { x, y, width, height };
    require(xxwidgets_widget_create(app, parent, kind, text, rect, &widget));
    return widget;
}

int main(int argc, char **argv)
{
    xxwidgets_app *app = NULL;
    xxwidgets_config config = { XXWIDGETS_BACKEND_AUTO, on_event, NULL };
    demo d = {0};
    xxwidgets_status status;
    int exit_code;
    demo_arguments arguments;
    int parsed = demo_parse(argc, argv, "xxwidgets_demo", &arguments);
    if (parsed <= 0) return parsed < 0;
    config.backend = arguments.backend;
    config.user_data = &d;
    status = xxwidgets_app_create(&config, &app);
    if (status != XXWIDGETS_OK) {
        fprintf(stderr, "xxwidgets initialization: %s\n", xxwidgets_status_string(status));
        return EXIT_FAILURE;
    }
    cleanup_app = app;
    d.window = create(app, NULL, XXWIDGETS_WINDOW, "xxwidgets - native C controls + TUI", 1, 1, 68, 22);
    if (arguments.smoke) require(xxwidgets_widget_set_visible(d.window, 0));
    create(app, d.window, XXWIDGETS_LABEL, "Same C API on every backend", 2, 1, 60, 1);
    create(app, d.window, XXWIDGETS_LABEL, "Name:", 2, 3, 10, 1);
    d.edit = create(app, d.window, XXWIDGETS_EDIT, "World", 13, 3, 40, 1);
    d.checkbox = create(app, d.window, XXWIDGETS_CHECKBOX, "Enable option", 2, 5, 30, 1);
    d.list = create(app, d.window, XXWIDGETS_LISTBOX, "", 2, 7, 30, 6);
    require(xxwidgets_listbox_add(d.list, "Windows / WinAPI"));
    require(xxwidgets_listbox_add(d.list, "Linux / GTK"));
    require(xxwidgets_listbox_add(d.list, "macOS / AppKit"));
    require(xxwidgets_listbox_add(d.list, "Terminal / TUI"));
    d.progress = create(app, d.window, XXWIDGETS_PROGRESS, "", 35, 9, 25, 1);
    d.apply = create(app, d.window, XXWIDGETS_BUTTON, "Apply", 2, 15, 12, 2);
    d.quit = create(app, d.window, XXWIDGETS_BUTTON, "Quit", 17, 15, 12, 2);
    d.status = create(app, d.window, XXWIDGETS_LABEL, "Ready. Tab to navigate, Enter to activate.", 2, 18, 62, 1);
    if (arguments.smoke) {
        xxwidgets_event apply = {XXWIDGETS_EVENT_CLICK, d.apply, 0, 0, 0};
        int progress = 0;
        require(xxwidgets_widget_set_value(d.checkbox, 1));
        on_event(app, &apply, &d);
        require(xxwidgets_widget_get_value(d.progress, &progress));
        require(xxwidgets_app_poll(app, 0));
        exit_code = progress != 100 || xxwidgets_listbox_count(d.list) != 4;
    } else {
        require(xxwidgets_widget_focus(d.edit));
        exit_code = xxwidgets_app_run(app);
    }
    cleanup_app = NULL;
    require(xxwidgets_app_destroy(app));
    return exit_code;
}
