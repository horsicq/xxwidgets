#include "demo_common.h"

#ifndef XXWIDGETS_DEMO_KIND
#define XXWIDGETS_DEMO_KIND XXWIDGETS_BUTTON
#endif

static xxwidgets_kind control_kind(void) { return XXWIDGETS_DEMO_KIND; }

typedef struct control_demo {
    xxwidgets_widget *window, *control, *status, *primary, *reset, *enabled, *visible, *quit;
    int clicks, resized;
} control_demo;

static const char *control_name(void)
{
    switch (control_kind()) {
    case XXWIDGETS_WINDOW: return "Window";
    case XXWIDGETS_LABEL: return "Label";
    case XXWIDGETS_BUTTON: return "Button";
    case XXWIDGETS_EDIT: return "Edit";
    case XXWIDGETS_CHECKBOX: return "Checkbox";
    case XXWIDGETS_LISTBOX: return "ListBox";
    default: return "Progress";
    }
}

static int fill_list(xxwidgets_widget *list)
{
    const char *items[] = {"Windows / WinAPI", "Linux / GTK", "macOS / AppKit", "Terminal / TUI", "Unicode: \xce\xbb"};
    size_t i;
    if (!demo_check(xxwidgets_listbox_clear(list))) return 0;
    for (i = 0; i < sizeof(items) / sizeof(items[0]); ++i)
        if (!demo_check(xxwidgets_listbox_add(list, items[i]))) return 0;
    return demo_check(xxwidgets_widget_set_value(list, 0));
}

static void describe(control_demo *demo)
{
    char text[1200], value_text[1024];
    int value = 0;
    switch (control_kind()) {
    case XXWIDGETS_EDIT:
        if (xxwidgets_widget_get_text(demo->control, value_text, sizeof(value_text), NULL) == XXWIDGETS_OK)
            snprintf(text, sizeof(text), "Text: %s", value_text);
        else strcpy(text, "Text exceeds the demo's 1023-byte display limit.");
        break;
    case XXWIDGETS_CHECKBOX:
        xxwidgets_widget_get_value(demo->control, &value);
        snprintf(text, sizeof(text), "Checkbox is %s.", value ? "checked" : "unchecked"); break;
    case XXWIDGETS_LISTBOX:
        xxwidgets_widget_get_value(demo->control, &value);
        snprintf(text, sizeof(text), "%zu items; selected index: %d", xxwidgets_listbox_count(demo->control), value); break;
    case XXWIDGETS_PROGRESS:
        xxwidgets_widget_get_value(demo->control, &value);
        snprintf(text, sizeof(text), "Progress: %d%%", value); break;
    default: snprintf(text, sizeof(text), "%s action invoked %d time(s).", control_name(), demo->clicks); break;
    }
    xxwidgets_widget_set_text(demo->status, text);
}

static void action(control_demo *demo, int reset)
{
    xxwidgets_status status = XXWIDGETS_OK;
    int value = 0;
    if (reset) demo->clicks = 0; else ++demo->clicks;
    switch (control_kind()) {
    case XXWIDGETS_WINDOW: {
        xxwidgets_rect bounds = {1, 1, 70, 22};
        demo->resized = reset ? 0 : !demo->resized;
        if (demo->resized) { bounds.width = 64; bounds.height = 20; }
        status = xxwidgets_widget_set_rect(demo->window, bounds); break;
    }
    case XXWIDGETS_LABEL:
        status = xxwidgets_widget_set_text(demo->control, reset ? "A label displays UTF-8 text." :
            demo->clicks % 2 ? "Hello, world! Unicode: \xce\xbb" : "Change text without recreating the label."); break;
    case XXWIDGETS_BUTTON: break;
    case XXWIDGETS_EDIT:
        if (reset) status = xxwidgets_widget_set_text(demo->control, "Edit me: Unicode \xce\xbb");
        break;
    case XXWIDGETS_CHECKBOX:
        if (!reset) xxwidgets_widget_get_value(demo->control, &value);
        status = xxwidgets_widget_set_value(demo->control, reset ? 0 : !value); break;
    case XXWIDGETS_LISTBOX:
        if (reset) { if (!fill_list(demo->control)) return; }
        else {
            char text[64];
            snprintf(text, sizeof(text), "Added item %d", demo->clicks);
            status = xxwidgets_listbox_add(demo->control, text);
        }
        break;
    case XXWIDGETS_PROGRESS:
        xxwidgets_widget_get_value(demo->control, &value);
        status = xxwidgets_widget_set_value(demo->control, reset ? 0 : (value >= 100 ? 0 : value + 10)); break;
    default: break;
    }
    if (status != XXWIDGETS_OK) xxwidgets_widget_set_text(demo->status, xxwidgets_status_string(status));
    else describe(demo);
}

static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    control_demo *demo = (control_demo *)user;
    int value = 0;
    if (event->type == XXWIDGETS_EVENT_CLOSE ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == demo->quit) ||
        (event->type == XXWIDGETS_EVENT_SHORTCUT && event->value == 2)) xxwidgets_app_quit(app, 0);
    else if (event->type == XXWIDGETS_EVENT_SHORTCUT && event->value == 1) action(demo, 0);
    else if (event->type == XXWIDGETS_EVENT_CLICK && event->widget == demo->primary) action(demo, 0);
    else if (event->type == XXWIDGETS_EVENT_CLICK && event->widget == demo->reset) action(demo, 1);
    else if ((event->type == XXWIDGETS_EVENT_CHANGE || event->type == XXWIDGETS_EVENT_CLICK) && event->widget == demo->enabled) {
        xxwidgets_widget_get_value(demo->enabled, &value); xxwidgets_widget_set_enabled(demo->control, value);
    } else if ((event->type == XXWIDGETS_EVENT_CHANGE || event->type == XXWIDGETS_EVENT_CLICK) && event->widget == demo->visible) {
        xxwidgets_widget_get_value(demo->visible, &value); xxwidgets_widget_set_visible(demo->control, value);
    } else if (event->widget == demo->control) {
        if (control_kind() == XXWIDGETS_BUTTON && event->type == XXWIDGETS_EVENT_CLICK) action(demo, 0);
        else if (event->type == XXWIDGETS_EVENT_CHANGE || event->type == XXWIDGETS_EVENT_SELECT) describe(demo);
    }
}

int main(int argc, char **argv)
{
    control_demo demo = {0};
    demo_arguments arguments;
    xxwidgets_app *app = NULL;
    xxwidgets_config config = {XXWIDGETS_BACKEND_AUTO, on_event, &demo};
    const xxwidgets_shortcut shortcuts[] = {{"Ctrl+R", 1}, {"Ctrl+Q", 2}};
    char title[96];
    int result, parsed = demo_parse(argc, argv, argv[0], &arguments);
    if (parsed <= 0) return parsed < 0;
    config.backend = arguments.backend;
    if (!demo_check(xxwidgets_app_create(&config, &app))) return 1;
    snprintf(title, sizeof(title), "xxwidgets %s demo", control_name());
    demo.window = demo_widget(app, NULL, XXWIDGETS_WINDOW, title, 1, 1, 70, 22);
    if (!demo.window) return demo_finish(app, 1);
    if (arguments.smoke) xxwidgets_widget_set_visible(demo.window, 0);
    if (!demo_widget(app, demo.window, XXWIDGETS_LABEL, title, 2, 1, 60, 1)) return demo_finish(app, 1);
    if (control_kind() == XXWIDGETS_WINDOW) demo.control = demo.window;
    else demo.control = demo_widget(app, demo.window, control_kind(),
        control_kind() == XXWIDGETS_EDIT ? "Edit me: Unicode \xce\xbb" :
        control_kind() == XXWIDGETS_LABEL ? "A label displays UTF-8 text." : "Try this control",
        2, 4, 58, control_kind() == XXWIDGETS_LISTBOX ? 8 : 2);
    demo.primary = demo_widget(app, demo.window, XXWIDGETS_BUTTON,
        control_kind() == XXWIDGETS_WINDOW ? "Resize" : control_kind() == XXWIDGETS_EDIT ? "Read text" :
        control_kind() == XXWIDGETS_LISTBOX ? "Add item" : control_kind() == XXWIDGETS_PROGRESS ? "+10%" : "Change",
        2, 13, 14, 2);
    demo.reset = demo_widget(app, demo.window, XXWIDGETS_BUTTON, "Reset", 18, 13, 12, 2);
    demo.quit = demo_widget(app, demo.window, XXWIDGETS_BUTTON, "Quit", 46, 13, 12, 2);
    demo.status = demo_widget(app, demo.window, XXWIDGETS_LABEL, "", 2, 17, 60, 1);
    if (!demo.control || !demo.primary || !demo.reset || !demo.quit || !demo.status) return demo_finish(app, 1);
    if (control_kind() != XXWIDGETS_WINDOW) {
        demo.enabled = demo_widget(app, demo.window, XXWIDGETS_CHECKBOX, "Enabled", 2, 2, 18, 1);
        demo.visible = demo_widget(app, demo.window, XXWIDGETS_CHECKBOX, "Visible", 22, 2, 18, 1);
        if (!demo.enabled || !demo.visible || !demo_check(xxwidgets_widget_set_value(demo.enabled, 1)) ||
            !demo_check(xxwidgets_widget_set_value(demo.visible, 1))) return demo_finish(app, 1);
    }
    if (!demo_widget(app, demo.window, XXWIDGETS_LABEL, "Tab: controls | Ctrl+R: action | Ctrl+Q: quit", 2, 18, 60, 1) ||
        !demo_check(xxwidgets_window_set_shortcuts(demo.window, shortcuts, 2))) return demo_finish(app, 1);
    if (control_kind() == XXWIDGETS_LISTBOX && !fill_list(demo.control)) return demo_finish(app, 1);
    describe(&demo);
    if (arguments.smoke) {
        action(&demo, 0); action(&demo, 1);
        result = !demo_check(xxwidgets_app_poll(app, 0));
    } else {
        if (!demo_check(xxwidgets_widget_focus(demo.primary))) return demo_finish(app, 1);
        result = xxwidgets_app_run(app);
    }
    return demo_finish(app, result);
}
