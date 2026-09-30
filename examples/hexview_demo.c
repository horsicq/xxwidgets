#include "demo_common.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct hex_demo {
    xxwidgets_widget *hex, *status, *columns[3], *quit;
    uint64_t base;
} hex_demo;

static xxwidgets_app *cleanup_app;
static void require(xxwidgets_status status)
{
    if (status == XXWIDGETS_OK) return;
    if (cleanup_app) xxwidgets_app_destroy(cleanup_app);
    fprintf(stderr, "xxwidgets: %s\n", xxwidgets_status_string(status));
    exit(EXIT_FAILURE);
}

static void update_status(hex_demo *demo)
{
    char text[128];
    size_t offset, length;
    if (xxwidgets_hexview_get_selection(demo->hex, &offset, &length) != XXWIDGETS_OK) return;
    if (offset == SIZE_MAX) snprintf(text, sizeof(text), "%zu bytes; no selection", xxwidgets_hexview_size(demo->hex));
    else snprintf(text, sizeof(text), "%zu bytes | Address %016" PRIX64 " | Row: %zu bytes",
        xxwidgets_hexview_size(demo->hex), demo->base + (uint64_t)offset, length);
    xxwidgets_widget_set_text(demo->status, text);
}

static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user_data)
{
    hex_demo *demo = (hex_demo *)user_data;
    unsigned int i;
    if (event->type == XXWIDGETS_EVENT_CLOSE ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == demo->quit)) {
        xxwidgets_app_quit(app, 0);
    } else if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == demo->hex) {
        update_status(demo);
    } else if (event->type == XXWIDGETS_EVENT_CLICK) {
        for (i = 0; i < 3; ++i) {
            if (event->widget == demo->columns[i]) {
                xxwidgets_status status = xxwidgets_hexview_set_layout(demo->hex, demo->base, 8u << i);
                if (status != XXWIDGETS_OK) xxwidgets_widget_set_text(demo->status, xxwidgets_status_string(status));
                else { update_status(demo); xxwidgets_widget_focus(demo->hex); }
            }
        }
    }
}

static xxwidgets_widget *create(xxwidgets_app *app, xxwidgets_widget *parent,
    xxwidgets_kind kind, const char *text, int x, int y, int width, int height)
{
    xxwidgets_widget *widget = NULL;
    xxwidgets_rect rect = {x, y, width, height};
    require(xxwidgets_widget_create(app, parent, kind, text, rect, &widget));
    return widget;
}

static int load_file(const char *path, unsigned char **data, size_t *size)
{
    const long limit = 16L * 1024 * 1024;
    FILE *file = fopen(path, "rb");
    long length;
    if (!file) { fprintf(stderr, "Cannot open %s: %s\n", path, strerror(errno)); return 0; }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 || length > limit ||
        fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Cannot read %s (demo limit: 16 MiB).\n", path);
        fclose(file); return 0;
    }
    *data = length ? (unsigned char *)malloc((size_t)length) : NULL;
    if (length && !*data) { fprintf(stderr, "Out of memory.\n"); fclose(file); return 0; }
    if (length && fread(*data, 1, (size_t)length, file) != (size_t)length) {
        fprintf(stderr, "Cannot read %s.\n", path); free(*data); *data = NULL; fclose(file); return 0;
    }
    fclose(file); *size = (size_t)length;
    return 1;
}

int main(int argc, char **argv)
{
    xxwidgets_config config = {XXWIDGETS_BACKEND_AUTO, on_event, NULL};
    xxwidgets_app *app = NULL;
    xxwidgets_widget *window;
    hex_demo demo = {0};
    unsigned char sample[515], *data = sample;
    size_t size = sizeof(sample), i;
    unsigned int columns = 0;
    const char *path = NULL;
    int width, exit_code, arg, smoke = 0, help = 0;
    for (arg = 1; arg < argc; ++arg) {
        if (strcmp(argv[arg], "--native") == 0) config.backend = XXWIDGETS_BACKEND_NATIVE;
        else if (strcmp(argv[arg], "--tui") == 0) config.backend = XXWIDGETS_BACKEND_TUI;
        else if (strcmp(argv[arg], "--smoke-test") == 0) smoke = 1;
        else if (strcmp(argv[arg], "--help") == 0) { help = 1; goto usage; }
        else if (strcmp(argv[arg], "--cols") == 0 && arg + 1 < argc) {
            const char *value = argv[++arg];
            if (!strcmp(value, "8")) columns = 8;
            else if (!strcmp(value, "16")) columns = 16;
            else if (!strcmp(value, "32")) columns = 32;
            else goto usage;
        } else if (strcmp(argv[arg], "--base") == 0 && arg + 1 < argc) {
            char *end;
            const char *value = argv[++arg];
            errno = 0;
            if (!*value || *value == '-') goto usage;
            demo.base = strtoull(value, &end, 0);
            if (errno || *end) goto usage;
        } else if (argv[arg][0] == '-' || path) goto usage;
        else path = argv[arg];
    }
    if (path) { if (!load_file(path, &data, &size)) return EXIT_FAILURE; }
    else {
        for (i = 0; i < sizeof(sample); ++i) sample[i] = (unsigned char)i;
        memcpy(sample, "xxwidgets HexView: native controls and TUI", 42);
    }
    if (size && (uintmax_t)(size - 1) > UINT64_MAX - demo.base) {
        fprintf(stderr, "Base address plus file size overflows 64 bits.\n");
        if (path) free(data);
        return EXIT_FAILURE;
    }
    config.user_data = &demo;
    {
        xxwidgets_status status = xxwidgets_app_create(&config, &app);
        if (status != XXWIDGETS_OK) {
            fprintf(stderr, "xxwidgets initialization: %s\n", xxwidgets_status_string(status));
            if (path) free(data);
            return EXIT_FAILURE;
        }
    }
    cleanup_app = app;
    width = xxwidgets_app_backend(app) == XXWIDGETS_BACKEND_TUI ? 72 : 100;
    if (!columns) columns = xxwidgets_app_backend(app) == XXWIDGETS_BACKEND_TUI ? 8 : 16;
    window = create(app, NULL, XXWIDGETS_WINDOW, "xxwidgets HexView", 1, 0, width, 23);
    if (smoke) require(xxwidgets_widget_set_visible(window, 0));
    create(app, window, XXWIDGETS_LABEL, path ? path : "Sample bytes: text, 00-FF, and a partial final row", 1, 0, width - 2, 1);
    create(app, window, XXWIDGETS_LABEL, "Address | Hexadecimal bytes | Printable ASCII", 1, 1, width - 2, 1);
    demo.hex = create(app, window, XXWIDGETS_HEXVIEW, "Binary data", 1, 2, width - 2, 14);
    require(xxwidgets_hexview_set_layout(demo.hex, demo.base, columns));
    require(xxwidgets_hexview_set_data(demo.hex, data, size));
    if (path) free(data);
    demo.status = create(app, window, XXWIDGETS_LABEL, "", 1, 17, width - 2, 1);
    demo.columns[0] = create(app, window, XXWIDGETS_BUTTON, "8 bytes", 1, 18, 11, 2);
    demo.columns[1] = create(app, window, XXWIDGETS_BUTTON, "16 bytes", 14, 18, 11, 2);
    demo.columns[2] = create(app, window, XXWIDGETS_BUTTON, "32 bytes", 27, 18, 11, 2);
    demo.quit = create(app, window, XXWIDGETS_BUTTON, "Quit", width - 13, 18, 11, 2);
    create(app, window, XXWIDGETS_LABEL, "Arrows/PgUp/PgDn: rows. Tab: controls. TUI Left/Right: pan.", 1, 21, width - 2, 1);
    update_status(&demo);
    if (smoke) {
        size_t offset, length;
        require(xxwidgets_hexview_set_layout(demo.hex, demo.base, 8));
        require(xxwidgets_hexview_get_selection(demo.hex, &offset, &length));
        require(xxwidgets_app_poll(app, 0));
        exit_code = xxwidgets_hexview_size(demo.hex) != size ||
            (size ? offset != 0 || length != (size < 8 ? size : 8) : offset != SIZE_MAX || length != 0);
    } else {
        require(xxwidgets_widget_focus(demo.hex));
        exit_code = xxwidgets_app_run(app);
    }
    cleanup_app = NULL;
    require(xxwidgets_app_destroy(app));
    return exit_code;
usage:
    fprintf(help ? stdout : stderr, "Usage: xxwidgets_hexview_demo [--native|--tui] [--smoke-test] [--cols 8|16|32] [--base address] [file]\n");
    return help ? 0 : EXIT_FAILURE;
}
