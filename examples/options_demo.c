#include "demo_common.h"

typedef struct options_demo {
    xxwidgets_widget *window, *open, *status, *quit;
    xxwidgets_option options[2];
    int running, pending;
} options_demo;

static void describe(options_demo *demo, int accepted)
{
    char text[160];
    snprintf(text, sizeof(text), "%s | Details: %s | Log: %s", accepted ? "Applied" : "Current values",
        demo->options[0].value ? "on" : "off", demo->options[1].value ? "on" : "off");
    xxwidgets_widget_set_text(demo->status, text);
}

static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    options_demo *demo = (options_demo *)user;
    if (event->type == XXWIDGETS_EVENT_CLOSE ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == demo->quit)) {
        demo->running = 0; xxwidgets_app_quit(app, 0);
    } else if (event->type == XXWIDGETS_EVENT_CLICK && event->widget == demo->open) demo->pending = 1;
}

int main(int argc, char **argv)
{
    options_demo demo = {0};
    demo_arguments arguments;
    xxwidgets_app *app = NULL;
    xxwidgets_config config = {XXWIDGETS_BACKEND_AUTO, on_event, &demo};
    int accepted = 0, result = 0, parsed = demo_parse(argc, argv, "xxwidgets_options_demo", &arguments);
    if (parsed <= 0) return parsed < 0;
    demo.options[0].label = "Show details";
    demo.options[1].label = "Show operation log"; demo.options[1].value = 1;
    config.backend = arguments.backend;
    if (!demo_check(xxwidgets_app_create(&config, &app))) return 1;
    demo.window = demo_widget(app, NULL, XXWIDGETS_WINDOW, "xxwidgets Options demo", 1, 1, 76, 16);
    if (!demo.window) return demo_finish(app, 1);
    if (arguments.smoke) xxwidgets_widget_set_visible(demo.window, 0);
    if (!demo_widget(app, demo.window, XXWIDGETS_LABEL, "Open the modal form, change checkboxes, then choose OK or Cancel.", 2, 2, 70, 1) ||
        !demo_widget(app, demo.window, XXWIDGETS_LABEL, "OK applies values; Cancel, Escape and close retain the previous values.", 2, 4, 70, 1))
        return demo_finish(app, 1);
    demo.open = demo_widget(app, demo.window, XXWIDGETS_BUTTON, "Options...", 2, 7, 16, 2);
    demo.quit = demo_widget(app, demo.window, XXWIDGETS_BUTTON, "Quit", 22, 7, 12, 2);
    demo.status = demo_widget(app, demo.window, XXWIDGETS_LABEL, "", 2, 11, 70, 1);
    if (!demo.open || !demo.quit || !demo.status) return demo_finish(app, 1);
    describe(&demo, 0);
    if (arguments.smoke) {
        if (!demo_modal_smoke_begin(app, demo.window, 1)) return demo_finish(app, 1);
        result = !demo_check(xxwidgets_options_dialog(demo.window, "Demo options", demo.options, 2, &accepted));
        if (!demo_modal_smoke_finish() || !accepted || !demo.options[0].value || !demo.options[1].value) result = 1;
    } else {
        demo.running = 1;
        xxwidgets_widget_focus(demo.open);
        while (demo.running) {
            if (!demo_check(xxwidgets_app_poll(app, 30))) { result = 1; break; }
            if (demo.pending && demo.running) {
                demo.pending = 0;
                /* Modal loops start after returning from the input callback. */
                if (!demo_check(xxwidgets_options_dialog(demo.window, "Demo options", demo.options, 2, &accepted))) {
                    xxwidgets_widget_set_text(demo.status, "Cannot show Options."); continue;
                }
                describe(&demo, accepted);
            }
        }
    }
    return demo_finish(app, result);
}
