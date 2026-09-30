#include "demo_common.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct archive_demo {
    xxwidgets_widget *archive, *status, *load, *clear, *quit;
} archive_demo;

static const xxwidgets_archive_entry sample[] = {
    {"documents/", 0, 1},
    {"documents/readme.txt", 2048, 0},
    {"documents/Gr\xc3\xbc\xc3\x9f" "e.txt", 512, 0},
    {"images/", 0, 1},
    {"images/holiday/photo.jpg", UINT64_C(7340032), 0},
    {"large-image.bin", UINT64_C(1099511627776), 0},
    {"line\nbreak.txt", 7, 0}
};

static void update_status(archive_demo *demo)
{
    xxwidgets_archive_entry entry;
    size_t index;
    char text[160];
    if (xxwidgets_archiveview_get_selection(demo->archive, &index, &entry) != XXWIDGETS_OK) return;
    if (index == SIZE_MAX) snprintf(text, sizeof(text), "%zu entries; no selection", xxwidgets_archiveview_count(demo->archive));
    else snprintf(text, sizeof(text), "Entry %zu of %zu: %s, %" PRIu64 " bytes",
        index + 1, xxwidgets_archiveview_count(demo->archive), entry.is_directory ? "directory" : "file", entry.size);
    xxwidgets_widget_set_text(demo->status, text);
}

static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user_data)
{
    archive_demo *demo = (archive_demo *)user_data;
    xxwidgets_status status = XXWIDGETS_OK;
    if (event->type == XXWIDGETS_EVENT_CLOSE ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == demo->quit)) {
        xxwidgets_app_quit(app, 0);
        return;
    }
    if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == demo->archive) {
        update_status(demo);
        return;
    }
    if (event->type != XXWIDGETS_EVENT_CLICK) return;
    if (event->widget == demo->load)
        status = xxwidgets_archiveview_set_entries(demo->archive, sample, sizeof(sample) / sizeof(sample[0]));
    else if (event->widget == demo->clear) status = xxwidgets_archiveview_clear(demo->archive);
    if (status != XXWIDGETS_OK) xxwidgets_widget_set_text(demo->status, xxwidgets_status_string(status));
    else { update_status(demo); xxwidgets_widget_focus(demo->archive); }
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
    archive_demo demo = {0};
    xxwidgets_config config = {XXWIDGETS_BACKEND_AUTO, on_event, &demo};
    xxwidgets_app *app = NULL;
    xxwidgets_widget *window;
    xxwidgets_status status;
    int i, result, smoke = 0;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--native") == 0) config.backend = XXWIDGETS_BACKEND_NATIVE;
        else if (strcmp(argv[i], "--tui") == 0) config.backend = XXWIDGETS_BACKEND_TUI;
        else if (strcmp(argv[i], "--smoke-test") == 0) smoke = 1;
        else {
            fprintf(strcmp(argv[i], "--help") == 0 ? stdout : stderr,
                "Usage: xxwidgets_archiveview_demo [--native|--tui] [--smoke-test]\n");
            return strcmp(argv[i], "--help") == 0 ? 0 : 1;
        }
    }
    status = xxwidgets_app_create(&config, &app);
    if (status != XXWIDGETS_OK) { fprintf(stderr, "xxwidgets: %s\n", xxwidgets_status_string(status)); return 1; }
    window = make(app, NULL, XXWIDGETS_WINDOW, "ArchiveView demo", 0, 0, 76, 18);
    if (smoke) xxwidgets_widget_set_visible(window, 0);
    make(app, window, XXWIDGETS_LABEL, "Type                  Bytes  Path", 1, 1, 72, 1);
    demo.archive = make(app, window, XXWIDGETS_ARCHIVEVIEW, "Archive contents", 1, 3, 72, 9);
    demo.status = make(app, window, XXWIDGETS_LABEL, "", 1, 13, 72, 1);
    demo.load = make(app, window, XXWIDGETS_BUTTON, "Load sample", 1, 15, 15, 1);
    demo.clear = make(app, window, XXWIDGETS_BUTTON, "Clear", 18, 15, 10, 1);
    demo.quit = make(app, window, XXWIDGETS_BUTTON, "Quit", 30, 15, 10, 1);
    status = xxwidgets_archiveview_set_entries(demo.archive, sample, sizeof(sample) / sizeof(sample[0]));
    if (status != XXWIDGETS_OK) { fprintf(stderr, "xxwidgets: %s\n", xxwidgets_status_string(status)); xxwidgets_app_destroy(app); return 1; }
    update_status(&demo);
    if (smoke) {
        xxwidgets_event click = {XXWIDGETS_EVENT_CLICK, demo.clear, 0, 0, 0};
        on_event(app, &click, &demo);
        result = xxwidgets_archiveview_count(demo.archive) != 0;
        click.widget = demo.load; on_event(app, &click, &demo);
        if (xxwidgets_archiveview_count(demo.archive) != sizeof(sample) / sizeof(sample[0]) ||
            !demo_check(xxwidgets_app_poll(app, 0))) result = 1;
    } else {
        xxwidgets_widget_focus(demo.archive);
        result = xxwidgets_app_run(app);
    }
    xxwidgets_app_destroy(app);
    return result < 0 ? 1 : result;
}
