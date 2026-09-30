#include "demo_common.h"
#include "xxwidgets/xxwidgets_settings.h"

typedef struct settings_demo {
    xxwidgets_widget *window, *open, *status, *quit;
    xx_settings *settings;
    int running, pending;
} settings_demo;

static const xxwidgets_setting_option options[] = {
    {"Show details", "Demo/Details", 0}, {"Show operation log", "Demo/Log", 1}
};

static void describe(settings_demo *demo)
{
    char text[160];
    snprintf(text, sizeof(text), "Saved preferences | Details: %s | Log: %s",
        xxwidgets_settings_get_bool(demo->settings, options[0].key, options[0].default_value) ? "on" : "off",
        xxwidgets_settings_get_bool(demo->settings, options[1].key, options[1].default_value) ? "on" : "off");
    xxwidgets_widget_set_text(demo->status, text);
}

static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    settings_demo *demo = (settings_demo *)user;
    if (event->type == XXWIDGETS_EVENT_CLOSE ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == demo->quit)) {
        demo->running = 0; xxwidgets_app_quit(app, 0);
    } else if (event->type == XXWIDGETS_EVENT_CLICK && event->widget == demo->open) demo->pending = 1;
}

int main(int argc, char **argv)
{
    settings_demo demo = {0};
    demo_arguments arguments;
    xxwidgets_app *app = NULL;
    xxwidgets_config config = {XXWIDGETS_BACKEND_AUTO, on_event, &demo};
    int accepted = 0, result = 0, parsed = demo_parse(argc, argv, "xxwidgets_settings_demo", &arguments);
    if (parsed <= 0) return parsed < 0;
    demo.settings = arguments.smoke ? xx_settings_create_memory() : xx_settings_create_native("xxwidgets", "SettingsDemo");
    if (!demo.settings) { fprintf(stderr, "Cannot create settings.\n"); return 1; }
    if (xx_settings_load(demo.settings) != XXFC_OK) {
        fprintf(stderr, "Cannot load settings.\n"); xx_settings_destroy(demo.settings); return 1;
    }
    config.backend = arguments.backend;
    if (!demo_check(xxwidgets_app_create(&config, &app))) { xx_settings_destroy(demo.settings); return 1; }
    demo.window = demo_widget(app, NULL, XXWIDGETS_WINDOW, "xxwidgets Persistent settings demo", 1, 1, 76, 16);
    if (!demo.window) { result = 1; goto finish; }
    if (arguments.smoke) xxwidgets_widget_set_visible(demo.window, 0);
    if (!demo_widget(app, demo.window, XXWIDGETS_LABEL, "Options use xxfclib's typed xx_settings store.", 2, 2, 70, 1) ||
        !demo_widget(app, demo.window, XXWIDGETS_LABEL, "Choose OK, close this demo, then reopen it to see saved preferences.", 2, 4, 70, 1) ||
        !demo_widget(app, demo.window, XXWIDGETS_LABEL, "Per-user store: xxwidgets / SettingsDemo. Cancel leaves it unchanged.", 2, 5, 70, 1)) { result = 1; goto finish; }
    demo.open = demo_widget(app, demo.window, XXWIDGETS_BUTTON, "Options...", 2, 7, 16, 2);
    demo.quit = demo_widget(app, demo.window, XXWIDGETS_BUTTON, "Quit", 22, 7, 12, 2);
    demo.status = demo_widget(app, demo.window, XXWIDGETS_LABEL, "", 2, 11, 70, 1);
    if (!demo.open || !demo.quit || !demo.status) { result = 1; goto finish; }
    describe(&demo);
    if (arguments.smoke) {
        if (!demo_modal_smoke_begin(app, demo.window, 1)) { result = 1; goto finish; }
        result = !demo_check(xxwidgets_settings_options_dialog(demo.window, "Persistent demo options", demo.settings, options, 2, &accepted));
        if (!demo_modal_smoke_finish() || !accepted || !xxwidgets_settings_get_bool(demo.settings, "Demo/Details", 0) ||
            !xxwidgets_settings_get_bool(demo.settings, "Demo/Log", 0)) result = 1;
    } else {
        demo.running = 1;
        xxwidgets_widget_focus(demo.open);
        while (demo.running) {
            if (!demo_check(xxwidgets_app_poll(app, 30))) { result = 1; break; }
            if (demo.pending && demo.running) {
                demo.pending = 0;
                if (!demo_check(xxwidgets_settings_options_dialog(demo.window, "Persistent demo options", demo.settings, options, 2, &accepted)))
                    xxwidgets_widget_set_text(demo.status, "Cannot open or save Options.");
                else describe(&demo);
            }
        }
    }
finish:
    xx_settings_destroy(demo.settings);
    return demo_finish(app, result);
}
