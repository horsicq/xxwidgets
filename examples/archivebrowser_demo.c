#include "demo_common.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct browser_demo {
    xxwidgets_widget *browser, *status, *load, *clear, *advanced, *select, *quit;
} browser_demo;

#define KNOWN (XXWIDGETS_ARCHIVE_SIZE_KNOWN | XXWIDGETS_ARCHIVE_PACKED_SIZE_KNOWN)
static const xxwidgets_archive_property properties[] = {
    {"CRC32", "1234ABCD"}, {"Method", "Deflate"}, {"Comment", "Application-supplied metadata"}
};
static const xxwidgets_archive_browser_entry sample[] = {
    {"documents/", 0, 0, 1, 0, "2026-09-28 10:00", "D"},
    {"documents/readme.txt", 2048, 812, 0, KNOWN, "2026-09-28 09:12", "A", properties, 3},
    {"documents/Gr\xc3\xbc\xc3\x9f" "e.txt", 512, 214, 0, KNOWN, "2026-09-27 16:30", "A"},
    {"images/holiday/photo.jpg", UINT64_C(7340032), UINT64_C(7200114), 0,
        KNOWN, "2026-09-26 13:45", "A"},
    {"large-image.bin", UINT64_C(1099511627776), UINT64_C(542001239), 0,
        KNOWN, "2026-09-22 12:00", "A"},
    {"LICENSE", 1092, 0, 0, XXWIDGETS_ARCHIVE_SIZE_KNOWN, NULL, NULL}
};

static void update_status(browser_demo *demo)
{
    xxwidgets_archive_browser_entry entry;
    size_t source;
    char text[256];
    if (xxwidgets_archivebrowser_get_selection(demo->browser, &source, &entry) != XXWIDGETS_OK) return;
    if (!entry.path) snprintf(text, sizeof(text), "%zu members; %zu visible; no selection",
        xxwidgets_archivebrowser_count(demo->browser), xxwidgets_archivebrowser_visible_count(demo->browser));
    else snprintf(text, sizeof(text), "%zu selected; %s: %s",
        xxwidgets_archivebrowser_selection_count(demo->browser), entry.is_directory ? "Directory" : "File", entry.path);
    xxwidgets_widget_set_text(demo->status, text);
}

static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user_data)
{
    browser_demo *demo = (browser_demo *)user_data;
    xxwidgets_status status = XXWIDGETS_OK;
    if (event->type == XXWIDGETS_EVENT_CLOSE ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == demo->quit)) {
        xxwidgets_app_quit(app, 0); return;
    }
    if (event->widget == demo->browser) {
        if (event->type == XXWIDGETS_EVENT_ACTIVATE) {
            xxwidgets_archive_browser_entry entry;
            size_t source;
            char text[256];
            if (xxwidgets_archivebrowser_get_selection(demo->browser, &source, &entry) == XXWIDGETS_OK && entry.path) {
                snprintf(text, sizeof(text), "Activated: %s (member %zu)", entry.path, source);
                xxwidgets_widget_set_text(demo->status, text);
            }
        } else if (event->type == XXWIDGETS_EVENT_SELECT || event->type == XXWIDGETS_EVENT_CHANGE)
            update_status(demo);
        return;
    }
    if (event->widget == demo->advanced && (event->type == XXWIDGETS_EVENT_CHANGE || event->type == XXWIDGETS_EVENT_CLICK)) {
        int checked = 0;
        xxwidgets_widget_get_value(demo->advanced, &checked);
        status = xxwidgets_archivebrowser_set_advanced(demo->browser, checked);
        if (status != XXWIDGETS_OK) xxwidgets_widget_set_text(demo->status, xxwidgets_status_string(status));
        return;
    }
    if (event->type != XXWIDGETS_EVENT_CLICK) return;
    if (event->widget == demo->load)
        status = xxwidgets_archivebrowser_set_entries(demo->browser, sample, sizeof(sample) / sizeof(sample[0]));
    else if (event->widget == demo->clear)
        status = xxwidgets_archivebrowser_set_entries(demo->browser, NULL, 0);
    else if (event->widget == demo->select) {
        size_t count = xxwidgets_archivebrowser_visible_count(demo->browser), i;
        size_t *rows = count ? (size_t *)calloc(count, sizeof(*rows)) : NULL;
        if (count && !rows) status = XXWIDGETS_OUT_OF_MEMORY;
        else {
            for (i = 0; i < count; ++i) rows[i] = i;
            status = xxwidgets_archivebrowser_set_selection(demo->browser, rows, count);
        }
        free(rows);
    }
    if (status != XXWIDGETS_OK) xxwidgets_widget_set_text(demo->status, xxwidgets_status_string(status));
    else { update_status(demo); xxwidgets_widget_focus(demo->browser); }
}

static xxwidgets_widget *make(xxwidgets_app *app, xxwidgets_widget *parent,
    xxwidgets_kind kind, const char *text, int x, int y, int width, int height)
{
    xxwidgets_widget *widget = NULL;
    xxwidgets_rect rect = {x, y, width, height};
    xxwidgets_status status = xxwidgets_widget_create(app, parent, kind, text, rect, &widget);
    if (status != XXWIDGETS_OK) {
        fprintf(stderr, "xxwidgets: %s\n", xxwidgets_status_string(status));
        xxwidgets_app_destroy(app); exit(EXIT_FAILURE);
    }
    return widget;
}

int main(int argc, char **argv)
{
    browser_demo demo = {0};
    xxwidgets_config config = {XXWIDGETS_BACKEND_AUTO, on_event, &demo};
    xxwidgets_app *app = NULL;
    xxwidgets_widget *window;
    xxwidgets_status status;
    int i, result, smoke = 0;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--native")) config.backend = XXWIDGETS_BACKEND_NATIVE;
        else if (!strcmp(argv[i], "--tui")) config.backend = XXWIDGETS_BACKEND_TUI;
        else if (!strcmp(argv[i], "--smoke-test")) smoke = 1;
        else {
            fprintf(!strcmp(argv[i], "--help") ? stdout : stderr,
                "Usage: xxwidgets_archivebrowser_demo [--native|--tui] [--smoke-test]\n");
            return !strcmp(argv[i], "--help") ? 0 : 1;
        }
    }
    status = xxwidgets_app_create(&config, &app);
    if (status != XXWIDGETS_OK) { fprintf(stderr, "xxwidgets: %s\n", xxwidgets_status_string(status)); return 1; }
    window = make(app, NULL, XXWIDGETS_WINDOW, "ArchiveBrowser demo", 0, 0, 76, 22);
    if (smoke) xxwidgets_widget_set_visible(window, 0);
    demo.load = make(app, window, XXWIDGETS_BUTTON, "Load sample", 1, 1, 15, 1);
    demo.clear = make(app, window, XXWIDGETS_BUTTON, "Clear", 18, 1, 10, 1);
    demo.advanced = make(app, window, XXWIDGETS_CHECKBOX, "Advanced", 30, 1, 14, 1);
    demo.select = make(app, window, XXWIDGETS_BUTTON, "Select all", 46, 1, 14, 1);
    demo.quit = make(app, window, XXWIDGETS_BUTTON, "Quit", 63, 1, 11, 1);
    demo.browser = make(app, window, XXWIDGETS_ARCHIVEBROWSER, "Archive contents", 1, 3, 74, 15);
    demo.status = make(app, window, XXWIDGETS_LABEL, "", 1, 19, 74, 1);
    make(app, window, XXWIDGETS_LABEL,
        "Enter: open | Backspace: up | 1-5: sort | Ctrl/Shift: select", 1, 20, 74, 1);
    status = xxwidgets_archivebrowser_set_archive(demo.browser, "sample.zip");
    if (status == XXWIDGETS_OK)
        status = xxwidgets_archivebrowser_set_entries(demo.browser, sample, sizeof(sample) / sizeof(sample[0]));
    if (status != XXWIDGETS_OK) { fprintf(stderr, "xxwidgets: %s\n", xxwidgets_status_string(status)); xxwidgets_app_destroy(app); return 1; }
    update_status(&demo);
    if (smoke) {
        size_t required = 0;
        xxwidgets_event click = {XXWIDGETS_EVENT_CLICK, demo.select, 0, 0, 0};
        on_event(app, &click, &demo);
        result = !demo_check(xxwidgets_archivebrowser_set_advanced(demo.browser, 1)) ||
            !demo_check(xxwidgets_archivebrowser_selected_sources(demo.browser, NULL, 0, &required));
        if (required != sizeof(sample) / sizeof(sample[0]) || xxwidgets_archivebrowser_column_count(demo.browser) != 8 ||
            !demo_check(xxwidgets_archivebrowser_set_directory(demo.browser, "documents")) ||
            xxwidgets_archivebrowser_visible_count(demo.browser) != 2 || !demo_check(xxwidgets_app_poll(app, 0))) result = 1;
    } else {
        xxwidgets_widget_focus(demo.browser);
        result = xxwidgets_app_run(app);
    }
    xxwidgets_app_destroy(app);
    return result < 0 ? 1 : result;
}
