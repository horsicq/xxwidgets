#include "demo_common.h"

typedef struct about_demo {
    xxwidgets_widget *window, *name, *open, *status, *quit;
    xxwidgets_about_dialog *about;
    int running, pending;
} about_demo;

static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    about_demo *demo = (about_demo *)user;
    if (event->type == XXWIDGETS_EVENT_CLOSE ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == demo->quit)) {
        demo->running = 0; xxwidgets_app_quit(app, 0);
    } else if (event->type == XXWIDGETS_EVENT_CLICK && event->widget == demo->open) demo->pending = 1;
}

static int configure(xxwidgets_about_dialog *about)
{
    const char *fields[] = {
        "xxwidgets About demo", "Widget demonstration", "Version 1.0",
        "A reusable modal About dialog.\n\nThe application supplies every text field and a copied RGBA image. "
        "Try changing the application name, then reopen the same dialog.\n\n"
        "The native backend displays selectable, scrollable text and scales the image. "
        "The terminal backend displays wrapped, scrollable text.",
        "Example copyright line", "example.org", "Example license text: MIT",
        "Application-provided credits\nxxwidgets native and terminal backends", "Done"
    };
    unsigned char image[32 * 32 * 4];
    size_t field, x, y;
    for (field = 0; field < sizeof(fields) / sizeof(fields[0]); ++field)
        if (!demo_check(xxwidgets_about_dialog_set_text(about, (xxwidgets_about_text)field, fields[field]))) return 0;
    for (y = 0; y < 32; ++y) for (x = 0; x < 32; ++x) {
        size_t offset = (y * 32 + x) * 4;
        int light = ((x / 8) + (y / 8)) % 2;
        image[offset] = light ? 230 : 35; image[offset + 1] = light ? 240 : 105;
        image[offset + 2] = light ? 255 : 190; image[offset + 3] = 255;
    }
    return demo_check(xxwidgets_about_dialog_set_image(about, image, 32, 32, 32 * 4));
}

int main(int argc, char **argv)
{
    about_demo demo = {0};
    demo_arguments arguments;
    xxwidgets_app *app = NULL;
    xxwidgets_config config = {XXWIDGETS_BACKEND_AUTO, on_event, &demo};
    char text[512];
    int result = 0, parsed = demo_parse(argc, argv, "xxwidgets_about_demo", &arguments);
    if (parsed <= 0) return parsed < 0;
    if (!demo_check(xxwidgets_about_dialog_create(&demo.about))) return 1;
    if (!configure(demo.about)) { xxwidgets_about_dialog_destroy(demo.about); return 1; }
    config.backend = arguments.backend;
    if (!demo_check(xxwidgets_app_create(&config, &app))) { xxwidgets_about_dialog_destroy(demo.about); return 1; }
    demo.window = demo_widget(app, NULL, XXWIDGETS_WINDOW, "xxwidgets About demo", 1, 1, 76, 16);
    if (!demo.window) { result = 1; goto finish; }
    if (arguments.smoke) xxwidgets_widget_set_visible(demo.window, 0);
    if (!demo_widget(app, demo.window, XXWIDGETS_LABEL, "Application name (edit and reopen the dialog):", 2, 2, 70, 1)) { result = 1; goto finish; }
    demo.name = demo_widget(app, demo.window, XXWIDGETS_EDIT, "Widget demonstration", 2, 4, 68, 1);
    demo.open = demo_widget(app, demo.window, XXWIDGETS_BUTTON, "About...", 2, 7, 16, 2);
    demo.quit = demo_widget(app, demo.window, XXWIDGETS_BUTTON, "Quit", 22, 7, 12, 2);
    demo.status = demo_widget(app, demo.window, XXWIDGETS_LABEL, "Text, image, version, license, credits and a custom Close label.", 2, 11, 70, 1);
    if (!demo.name || !demo.open || !demo.quit || !demo.status) { result = 1; goto finish; }
    if (arguments.smoke) {
        if (!demo_check(xxwidgets_about_dialog_get_text(demo.about, XXWIDGETS_ABOUT_VERSION, text, sizeof(text), NULL)) ||
            strcmp(text, "Version 1.0") || !demo_modal_smoke_begin(app, demo.window, 0)) { result = 1; goto finish; }
        result = !demo_check(xxwidgets_about_dialog_show(demo.about, demo.window));
        if (!demo_modal_smoke_finish()) result = 1;
    } else {
        demo.running = 1;
        xxwidgets_widget_focus(demo.open);
        while (demo.running) {
            if (!demo_check(xxwidgets_app_poll(app, 30))) { result = 1; break; }
            if (demo.pending && demo.running) {
                demo.pending = 0;
                if (!demo_check(xxwidgets_widget_get_text(demo.name, text, sizeof(text), NULL)) ||
                    !demo_check(xxwidgets_about_dialog_set_text(demo.about, XXWIDGETS_ABOUT_PROGRAM_NAME, text)) ||
                    !demo_check(xxwidgets_about_dialog_show(demo.about, demo.window)))
                    xxwidgets_widget_set_text(demo.status, "Cannot show About. The name must fit within 511 UTF-8 bytes.");
                else xxwidgets_widget_set_text(demo.status, "Dialog closed. Edit the name and reopen it to reuse the same dialog.");
            }
        }
    }
finish:
    if (!demo_check(xxwidgets_about_dialog_destroy(demo.about))) result = 1;
    return demo_finish(app, result);
}
