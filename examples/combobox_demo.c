#include "xxwidgets/xxwidgets_combobox.h"
#include "demo_common.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef struct combo_demo {
    xxwidgets_widget *combo, *checks, *status, *reset, *quit;
} combo_demo;

static void update(combo_demo *demo)
{
    xx_var value = {0};
    const xx_meta_string *checked[4];
    size_t count, i, used;
    char text[256];
    if (xxwidgets_combobox_get_current(demo->combo, &value) != XXWIDGETS_OK ||
        xxwidgets_checkcombobox_get_checked(demo->checks, checked, 4, &count) != XXWIDGETS_OK) return;
    used = (size_t)snprintf(text, sizeof(text), "Current value: %" PRIu64 " | Checked values:", value.val.u64);
    for (i = 0; i < count && used < sizeof(text); ++i)
        used += (size_t)snprintf(text + used, sizeof(text) - used, "%s%" PRIu64, i ? ", " : " ", checked[i]->var.val.u64);
    xxwidgets_widget_set_text(demo->status, text);
}

static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    combo_demo *demo = user;
    if (event->type == XXWIDGETS_EVENT_CLOSE || (event->type == XXWIDGETS_EVENT_CLICK && event->widget == demo->quit))
        xxwidgets_app_quit(app, 0);
    else if ((event->widget == demo->combo && event->type == XXWIDGETS_EVENT_SELECT) ||
             (event->widget == demo->checks && event->type == XXWIDGETS_EVENT_CHANGE)) update(demo);
    else if (event->type == XXWIDGETS_EVENT_CLICK && event->widget == demo->reset) {
        size_t i;
        xxwidgets_widget_set_value(demo->combo, 0);
        for (i = 0; i < xxwidgets_combobox_count(demo->checks); ++i)
            xxwidgets_checkcombobox_set_checked(demo->checks, i, 0);
        update(demo);
    }
}

static xxwidgets_widget *make(xxwidgets_app *app, xxwidgets_widget *parent, xxwidgets_kind kind,
    const char *text, int x, int y, int width, int height)
{
    return demo_widget(app, parent, kind, text, x, y, width, height);
}

int main(int argc, char **argv)
{
    combo_demo demo = {0};
    xxwidgets_config config = {XXWIDGETS_BACKEND_AUTO, on_event, &demo};
    xxwidgets_app *app = NULL;
    xxwidgets_widget *window;
    xx_str_w_s labels[4] = {0};
    xx_meta_string records[4] = {0};
    wchar_t *names[] = {L"Stored", L"Deflate", L"BZip2", L"Unicode \x03bb"};
    uint64_t values[] = {0, 8, 12, UINT64_C(4294967297)};
    int i, result;
    demo_arguments arguments;
    int parsed = demo_parse(argc, argv, "xxwidgets_combobox_demo", &arguments);
    if (parsed <= 0) return parsed < 0;
    config.backend = arguments.backend;
    if (xxwidgets_app_create(&config, &app) != XXWIDGETS_OK) return 1;
    window = make(app, NULL, XXWIDGETS_WINDOW, "Combobox demo", 1, 1, 76, 16);
    if (window && arguments.smoke) xxwidgets_widget_set_visible(window, 0);
    demo.combo = window ? make(app, window, XXWIDGETS_COMBOBOX, "Choose one", 2, 2, 32, 2) : NULL;
    demo.checks = window ? make(app, window, XXWIDGETS_CHECKCOMBOBOX, "Choose several", 38, 2, 32, 2) : NULL;
    demo.status = window ? make(app, window, XXWIDGETS_LABEL, "", 2, 8, 70, 2) : NULL;
    demo.reset = window ? make(app, window, XXWIDGETS_BUTTON, "Reset", 2, 12, 12, 2) : NULL;
    demo.quit = window ? make(app, window, XXWIDGETS_BUTTON, "Quit", 60, 12, 12, 2) : NULL;
    if (!demo.combo || !demo.checks || !demo.status || !demo.reset || !demo.quit) { xxwidgets_app_destroy(app); return 1; }
    if (!make(app, window, XXWIDGETS_LABEL, "Single choice", 2, 0, 32, 1) ||
        !make(app, window, XXWIDGETS_LABEL, "Checkbox choices", 38, 0, 32, 1)) return demo_finish(app, 1);
    make(app, window, XXWIDGETS_LABEL, "Terminal: Enter opens/closes; arrows move; Space checks; Esc closes the list.", 2, 6, 72, 1);
    for (i = 0; i < 4; ++i) {
        labels[i].data = names[i]; labels[i].length = wcslen(names[i]); labels[i].capacity = labels[i].length + 1; labels[i].is_view = true;
        records[i].meta_string = labels + i; records[i].var.type = XX_VAR_TYPE_UINT64; records[i].var.val.u64 = values[i];
    }
    if (xxwidgets_combobox_set_records(demo.combo, records, 4) != XXWIDGETS_OK ||
        xxwidgets_combobox_set_records(demo.checks, records, 4) != XXWIDGETS_OK) { xxwidgets_app_destroy(app); return 1; }
    update(&demo);
    if (arguments.smoke) {
        xx_var current = {0};
        const xx_meta_string *checked[4];
        size_t count = 0;
        result = !demo_check(xxwidgets_widget_set_value(demo.combo, 1)) ||
            !demo_check(xxwidgets_checkcombobox_set_checked(demo.checks, 1, 1)) ||
            !demo_check(xxwidgets_checkcombobox_set_checked(demo.checks, 3, 1)) ||
            !demo_check(xxwidgets_combobox_get_current(demo.combo, &current)) ||
            !demo_check(xxwidgets_checkcombobox_get_checked(demo.checks, checked, 4, &count));
        if (result || current.type != XX_VAR_TYPE_UINT64 || current.val.u64 != 8 || count != 2 ||
            checked[0]->var.val.u64 != 8 || checked[1]->var.val.u64 != UINT64_C(4294967297)) result = 1;
        update(&demo);
        if (!demo_check(xxwidgets_app_poll(app, 0))) result = 1;
    } else {
        xxwidgets_widget_focus(demo.combo);
        result = xxwidgets_app_run(app);
    }
    if (xxwidgets_app_destroy(app) != XXWIDGETS_OK) return 1;
    return result;
}
