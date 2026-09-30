#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_combobox.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef XXWIDGETS_WITH_SETTINGS
#include "xxwidgets/xxwidgets_settings.h"
#endif

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "line %d: %s\n", __LINE__, #expr); exit(1); } } while (0)

static int creates, destroys, shutdowns, fail_sync, events, init_failure, create_failure;
void xxwidgets_test_shortcuts(xxwidgets_app *app, xxwidgets_widget *window, xxwidgets_widget *edit);
void xxwidgets_test_process(xxwidgets_app *app, xxwidgets_widget *window);
static xxwidgets_widget *event_widget;
static int dialog_mode, dialog_polls;
static int about_mode, about_polls;
static int text_mode, text_polls, text_copies;
static const char *text_expected = "Formats \xce\xbb\nZIP\nTAR.GZ\n";
static xxwidgets_about_dialog *current_about;
static int dialog_check_values, dialog_expected[16];
static xxwidgets_status mock_init(xxwidgets_app *app) { (void)app; return init_failure ? XXWIDGETS_UNAVAILABLE : XXWIDGETS_OK; }
static void mock_shutdown(xxwidgets_app *app) { (void)app; ++shutdowns; }
static xxwidgets_status mock_create(xxwidgets_widget *widget) { (void)widget; ++creates; return create_failure ? XXWIDGETS_PLATFORM_ERROR : XXWIDGETS_OK; }
static void mock_destroy(xxwidgets_widget *widget)
{
    xxwidgets_widget *other;
    CHECK(widget->app->syncing);
    for (other = widget->app->widgets; other; other = other->next) CHECK(other->parent != widget);
    ++destroys;
}
static xxwidgets_status mock_sync(xxwidgets_widget *widget)
{
    CHECK(widget->app->syncing);
    xxwidgets_emit(widget, XXWIDGETS_EVENT_CHANGE, widget->value);
    if (fail_sync) { fail_sync = 0; return XXWIDGETS_PLATFORM_ERROR; }
    return XXWIDGETS_OK;
}
static xxwidgets_status mock_read(xxwidgets_widget *widget) { (void)widget; return XXWIDGETS_OK; }
static xxwidgets_status mock_focus(xxwidgets_widget *widget) { (void)widget; return XXWIDGETS_OK; }
static xxwidgets_status mock_copy_text(xxwidgets_widget *window, const char *text)
{
    CHECK(window == window->app->modal_window && !strcmp(text, text_expected));
    ++text_copies;
    return text_mode == 3 ? XXWIDGETS_PLATFORM_ERROR : XXWIDGETS_OK;
}
static xxwidgets_status mock_poll(xxwidgets_app *app, int timeout)
{
    (void)app; (void)timeout;
    if (text_mode && app->modal_window) {
        xxwidgets_widget *widget, *copy = NULL, *list = NULL;
        ++text_polls;
        CHECK(xxwidgets_text_dialog(app->modal_window, "Nested", "Text") == XXWIDGETS_BUSY);
        for (widget = app->widgets; widget; widget = widget->next) {
            if (widget->kind == XXWIDGETS_WINDOW && widget != app->modal_window) CHECK(!widget->enabled);
            if (widget->parent != app->modal_window) continue;
            if (widget->kind == XXWIDGETS_LISTBOX) list = widget;
            if (widget->kind == XXWIDGETS_BUTTON && widget != app->modal_default) copy = widget;
        }
        CHECK(copy && list && list->item_count == 3 && !strcmp(list->items[1], "ZIP"));
        if (text_mode == 4) return XXWIDGETS_PLATFORM_ERROR;
        if (text_polls == 1) xxwidgets_emit(copy, XXWIDGETS_EVENT_CLICK, 0);
        else {
            CHECK(!strcmp(copy->text, text_mode == 3 ? "Copy failed" : "Copied"));
            if (text_mode == 2) xxwidgets_emit(app->modal_window, XXWIDGETS_EVENT_CLOSE, 0);
            else xxwidgets_emit(app->modal_default, XXWIDGETS_EVENT_CLICK, 0);
        }
        return XXWIDGETS_OK;
    }
    if (about_mode && app->modal_window) {
        xxwidgets_widget *widget, *list = NULL;
        int accepted = 1;
        ++about_polls;
        CHECK(current_about->showing && app->modal_default);
        CHECK(xxwidgets_about_dialog_show(current_about, app->modal_window) == XXWIDGETS_BUSY);
        CHECK(xxwidgets_about_dialog_set_text(current_about, XXWIDGETS_ABOUT_VERSION, "busy") == XXWIDGETS_BUSY);
        CHECK(xxwidgets_about_dialog_set_image(current_about, NULL, 0, 0, 0) == XXWIDGETS_BUSY);
        CHECK(xxwidgets_about_dialog_destroy(current_about) == XXWIDGETS_BUSY);
        CHECK(xxwidgets_options_dialog(app->modal_window, "nested", NULL, 0, &accepted) == XXWIDGETS_BUSY && !accepted);
        for (widget = app->widgets; widget; widget = widget->next) {
            if (widget->kind == XXWIDGETS_WINDOW && widget != app->modal_window) CHECK(!widget->enabled);
            if (widget->parent == app->modal_window && widget->kind == XXWIDGETS_LISTBOX) list = widget;
        }
        CHECK(list && list->item_count >= 7);
        CHECK(!strcmp(list->items[0], "Example \xce\xbb"));
        CHECK(!strcmp(list->items[2], "Version 2"));
        CHECK(!strcmp(list->items[4], "First line") && !strcmp(list->items[5], "Second line"));
        CHECK(xxwidgets_valid_utf8(list->items[list->item_count - 1]));
        if (about_mode == 3) return XXWIDGETS_PLATFORM_ERROR;
        if (about_mode == 2) xxwidgets_emit(app->modal_window, XXWIDGETS_EVENT_CLOSE, 0);
        else xxwidgets_emit(app->modal_default, XXWIDGETS_EVENT_CLICK, 0);
        return XXWIDGETS_OK;
    }
    if (dialog_mode && app->modal_window) {
        xxwidgets_widget *widget, *first = NULL, *button = NULL;
        size_t option_index = 0;
        int accepted = 1;
        ++dialog_polls;
        CHECK(xxwidgets_options_dialog(app->modal_window, "nested", NULL, 0, &accepted) == XXWIDGETS_BUSY);
        CHECK(accepted == 0);
        for (widget = app->widgets; widget; widget = widget->next) {
            if (widget->kind == XXWIDGETS_WINDOW && widget != app->modal_window) CHECK(!widget->enabled);
            if (widget->parent != app->modal_window) continue;
            if (widget->kind == XXWIDGETS_CHECKBOX) {
                if (dialog_check_values) CHECK(widget->value == dialog_expected[option_index]);
                ++option_index;
                if (!first) first = widget;
            }
            if (widget->kind == XXWIDGETS_BUTTON && !strcmp(widget->text, dialog_mode == 1 ? "OK" : "Cancel")) button = widget;
        }
        if (first) CHECK(xxwidgets_widget_set_value(first, !first->value) == XXWIDGETS_OK);
        if (dialog_mode == 4) return XXWIDGETS_PLATFORM_ERROR;
        if (dialog_mode == 3) xxwidgets_emit(app->modal_window, XXWIDGETS_EVENT_CLOSE, 0);
        else { CHECK(button); xxwidgets_emit(button, XXWIDGETS_EVENT_CLICK, 0); }
        return XXWIDGETS_OK;
    }
    if (event_widget) xxwidgets_emit(event_widget, XXWIDGETS_EVENT_CLICK, 0);
    return XXWIDGETS_OK;
}
const xxwidgets_backend_ops xxwidgets_tui_ops = {
    "mock", mock_init, mock_shutdown, mock_poll, mock_create, mock_destroy,
    mock_sync, mock_read, mock_read, mock_focus, NULL, NULL, mock_copy_text
};

static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user_data)
{
    xxwidgets_widget *unused = NULL;
    xxwidgets_rect rect = {0, 0, 10, 1};
    CHECK(user_data == &events);
    ++events;
    CHECK(xxwidgets_widget_destroy(event->widget) == XXWIDGETS_BUSY);
    CHECK(xxwidgets_app_destroy(app) == XXWIDGETS_BUSY);
    CHECK(xxwidgets_app_poll(app, 0) == XXWIDGETS_BUSY);
    CHECK(xxwidgets_app_run(app) == -1);
    {
        int accepted = 1;
        CHECK(xxwidgets_options_dialog(event->widget->parent, "busy", NULL, 0, &accepted) == XXWIDGETS_BUSY);
        CHECK(accepted == 0);
    }
    CHECK(xxwidgets_widget_create(app, event->widget->parent, XXWIDGETS_LABEL, "", rect, &unused) == XXWIDGETS_BUSY);
    CHECK(xxwidgets_widget_set_text(event->widget, "changed inside callback") == XXWIDGETS_OK);
    xxwidgets_app_quit(app, 42);
}

static void check_combos(xxwidgets_app *app, xxwidgets_widget *window, xxwidgets_widget *edit)
{
    xxwidgets_widget *combo = NULL, *check = NULL;
    xxwidgets_rect bounds = {1, 1, 24, 1};
    wchar_t name[] = L"Choice \x03bb", second[] = L"Choice \x03bb", bytes_name[] = L"Bytes";
    xx_str_w_s labels[] = {{name, 8, 9, true}, {second, 8, 9, true}, {bytes_name, 5, 6, true}};
    xx_meta_string records[3] = {0};
    const xx_meta_string *checked[3] = {0}, *record;
    char payload[] = "value";
    unsigned char bytes[] = {0, 0x80, 0xff};
    xx_var value = {0};
    size_t count;
    int flag;
    records[0].meta_string = &labels[0]; records[0].var.type = XX_VAR_TYPE_UINT64; records[0].var.val.u64 = UINT64_MAX;
    records[1].meta_string = &labels[1]; records[1].var.type = XX_VAR_TYPE_STRING_VIEW;
    records[1].var.val.str.ptr = payload; records[1].var.val.str.len = 5;
    records[2].meta_string = &labels[2]; records[2].var.type = XX_VAR_TYPE_BYTES_VIEW;
    records[2].var.val.bytes.data = bytes; records[2].var.val.bytes.size = sizeof(bytes);
    CHECK(xxwidgets_widget_create(app, window, XXWIDGETS_COMBOBOX, "Choose", bounds, &combo) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_create(app, window, XXWIDGETS_CHECKCOMBOBOX, "Choose several", bounds, &check) == XXWIDGETS_OK);
    CHECK(xxwidgets_combobox_get_current(combo, &value) == XXWIDGETS_OK && value.type == XX_VAR_TYPE_NONE);
    CHECK(xxwidgets_combobox_set_records(edit, records, 3) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_combobox_set_records(combo, records, 3) == XXWIDGETS_OK && combo->value == 0);
    CHECK(xxwidgets_combobox_set_records(check, records, 3) == XXWIDGETS_OK);
    name[0] = 'x'; payload[0] = 'x'; bytes[0] = 77;
    CHECK(xxwidgets_combobox_get_record(combo, 0, &record) == XXWIDGETS_OK && record->meta_string->data[0] == 'C');
    CHECK(!strcmp(combo->items[0], "Choice \xce\xbb"));
    CHECK(xxwidgets_combobox_count(combo) == 3 && xxwidgets_combobox_count(edit) == 0);
    CHECK(xxwidgets_combobox_get_current(combo, &value) == XXWIDGETS_OK && value.type == XX_VAR_TYPE_UINT64 && value.val.u64 == UINT64_MAX);
    CHECK(xxwidgets_widget_set_value(combo, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_combobox_get_current(combo, &value) == XXWIDGETS_OK && !value.is_allocated && !strcmp(value.val.str.ptr, "value"));
    CHECK(xxwidgets_widget_set_value(combo, 2) == XXWIDGETS_OK);
    CHECK(xxwidgets_combobox_get_current(combo, &value) == XXWIDGETS_OK && value.val.bytes.data[0] == 0 && value.val.bytes.size == 3);
    CHECK(xxwidgets_combobox_get_current(check, &value) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_checkcombobox_set_checked(combo, 0, 1) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_checkcombobox_set_checked(check, 3, 1) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_checkcombobox_set_checked(check, 0, 2) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_checkcombobox_set_checked(check, 2, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_checkcombobox_set_checked(check, 0, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_checkcombobox_get_checked(check, NULL, 0, &count) == XXWIDGETS_OK && count == 2);
    CHECK(xxwidgets_checkcombobox_get_checked(check, checked, 1, &count) == XXWIDGETS_BUFFER_TOO_SMALL && !checked[0]);
    CHECK(xxwidgets_checkcombobox_get_checked(check, checked, 3, &count) == XXWIDGETS_OK && count == 2);
    CHECK(checked[0]->var.val.u64 == UINT64_MAX && checked[1]->var.val.bytes.data[0] == 0);
    CHECK(!strcmp(xxwidgets_combobox_caption(check), "2 selected"));
    fail_sync = 1;
    CHECK(xxwidgets_checkcombobox_set_checked(check, 0, 0) == XXWIDGETS_PLATFORM_ERROR);
    CHECK(xxwidgets_checkcombobox_is_checked(check, 0, &flag) == XXWIDGETS_OK && flag);
    records[0].meta_string = NULL;
    CHECK(xxwidgets_combobox_set_records(combo, records, 3) == XXWIDGETS_INVALID_ARGUMENT && combo->value == 2);
    records[0].meta_string = &labels[0];
    records[1].var.type = XX_VAR_TYPE_WSTRING_VIEW; records[1].var.val.wstr.ptr = second; records[1].var.val.wstr.len = SIZE_MAX;
    CHECK(xxwidgets_combobox_set_records(combo, records, 3) == XXWIDGETS_INVALID_ARGUMENT);
    records[1].var.val.wstr.len = 8;
    fail_sync = 1;
    CHECK(xxwidgets_combobox_set_records(combo, records, 3) == XXWIDGETS_PLATFORM_ERROR && combo->value == 2 && combo->item_count == 3);
    CHECK(xxwidgets_combobox_set_records(combo, records, 3) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_set_value(combo, 1) == XXWIDGETS_OK);
    second[0] = 'y';
    CHECK(xxwidgets_combobox_get_current(combo, &value) == XXWIDGETS_OK && value.val.wstr.ptr[0] == 'C');
    CHECK(xxwidgets_combobox_set_records(check, NULL, 0) == XXWIDGETS_OK && check->value == -1);
    CHECK(xxwidgets_checkcombobox_get_checked(check, NULL, 0, &count) == XXWIDGETS_OK && !count);
    CHECK(xxwidgets_widget_set_value(combo, -1) == XXWIDGETS_OK);
    CHECK(xxwidgets_combobox_get_current(combo, &value) == XXWIDGETS_OK && value.type == XX_VAR_TYPE_NONE);
    {
        wchar_t slice[] = {L'V', L'\x03bb', L'X'};
        xx_str_w_s view = {slice, 2, 2, true}, empty = {0};
        xx_meta_string views[2] = {{&view, {0}}, {&empty, {0}}};
        CHECK(xxwidgets_combobox_set_records(combo, views, 2) == XXWIDGETS_OK);
        CHECK(!strcmp(combo->items[0], "V\xce\xbb") && !combo->items[1][0]);
        CHECK(xxwidgets_combobox_get_record(combo, 0, &record) == XXWIDGETS_OK &&
            record->meta_string->length == 2 && record->meta_string->data[2] == L'\0');
        slice[1] = L'\xd800';
        CHECK(xxwidgets_combobox_set_records(combo, views, 2) == XXWIDGETS_INVALID_ARGUMENT);
        CHECK(!strcmp(combo->items[0], "V\xce\xbb"));
    }
    CHECK(xxwidgets_widget_destroy(combo) == XXWIDGETS_OK && xxwidgets_widget_destroy(check) == XXWIDGETS_OK);
}

static void check_about(xxwidgets_app *app, xxwidgets_app *other, xxwidgets_widget *window, xxwidgets_widget *edit)
{
    xxwidgets_about_dialog *about = NULL;
    xxwidgets_widget *other_window = NULL;
    xxwidgets_rect bounds = {0, 0, 78, 23};
    xxwidgets_event_fn callback = app->on_event;
    void *user = app->user_data;
    unsigned char pixels[] = {255, 0, 0, 255, 0, 255, 0, 128, 9, 9, 9, 9,
                              0, 0, 255, 255, 255, 255, 255, 0};
    char name[] = "Example \xce\xbb", buffer[64], long_text[260];
    size_t required;
    int mode;
    CHECK(xxwidgets_about_dialog_create(NULL) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_about_dialog_create(&about) == XXWIDGETS_OK);
    CHECK(xxwidgets_about_dialog_get_text(about, XXWIDGETS_ABOUT_TITLE, buffer, sizeof(buffer), &required) == XXWIDGETS_OK);
    CHECK(!strcmp(buffer, "About") && required == 6);
    CHECK(xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_PROGRAM_NAME, name) == XXWIDGETS_OK);
    name[0] = 'x';
    CHECK(xxwidgets_about_dialog_get_text(about, XXWIDGETS_ABOUT_PROGRAM_NAME, buffer, sizeof(buffer), NULL) == XXWIDGETS_OK);
    CHECK(!strcmp(buffer, "Example \xce\xbb"));
    CHECK(xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_VERSION, "Version 2") == XXWIDGETS_OK);
    CHECK(xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_DESCRIPTION, "First line\nSecond line") == XXWIDGETS_OK);
    memset(long_text, 'x', 63); memcpy(long_text + 63, "\xce\xbb", 2);
    memset(long_text + 65, 'y', 194); long_text[259] = 0;
    CHECK(xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_CREDITS, long_text) == XXWIDGETS_OK);
    CHECK(xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_CLOSE_LABEL, "Dismiss") == XXWIDGETS_OK);
    CHECK(xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_CLOSE_LABEL, "") == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_PROGRAM_NAME, "\xed\xa0\x80") == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_about_dialog_set_text(about, (xxwidgets_about_text)99, "invalid") == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_about_dialog_get_text(about, XXWIDGETS_ABOUT_PROGRAM_NAME, NULL, 0, &required) == XXWIDGETS_OK && required == 11);
    CHECK(xxwidgets_about_dialog_get_text(about, XXWIDGETS_ABOUT_PROGRAM_NAME, buffer, 10, NULL) == XXWIDGETS_BUFFER_TOO_SMALL);
    CHECK(!strcmp(buffer, "Example ") && xxwidgets_valid_utf8(buffer));
    CHECK(xxwidgets_about_dialog_set_image(about, pixels, 2, 2, 12) == XXWIDGETS_OK);
    pixels[0] = 0;
    CHECK(about->image[0] == 255 && about->image[8] == 0 && about->image[10] == 255 && about->image[15] == 0);
    CHECK(about->image_width == 2 && about->image_height == 2);
    CHECK(xxwidgets_about_dialog_set_image(about, pixels, 2, 2, 7) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_about_dialog_set_image(about, pixels, 2, 2, SIZE_MAX) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_about_dialog_set_image(about, pixels, 32768, 1, SIZE_MAX) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(about->image[0] == 255);
    CHECK(xxwidgets_about_dialog_show(about, edit) == XXWIDGETS_INVALID_ARGUMENT);
    app->dispatch_depth = 1;
    CHECK(xxwidgets_about_dialog_show(about, window) == XXWIDGETS_BUSY);
    app->dispatch_depth = 0;
    create_failure = 1;
    CHECK(xxwidgets_about_dialog_show(about, window) == XXWIDGETS_PLATFORM_ERROR);
    create_failure = 0;
    CHECK(!about->showing && window->enabled && !app->modal_window && !app->modal_default);
    fail_sync = 1;
    CHECK(xxwidgets_about_dialog_show(about, window) == XXWIDGETS_PLATFORM_ERROR);
    CHECK(!about->showing && window->enabled && app->on_event == callback && app->user_data == user);
    current_about = about;
    for (mode = 1; mode <= 3; ++mode) {
        about_mode = mode;
        CHECK(xxwidgets_about_dialog_show(about, window) == (mode == 3 ? XXWIDGETS_PLATFORM_ERROR : XXWIDGETS_OK));
        CHECK(window->enabled && !app->quit && !app->modal_window && !app->modal_default && !about->showing);
        CHECK(app->on_event == callback && app->user_data == user);
    }
    CHECK(xxwidgets_about_dialog_set_image(about, NULL, 0, 0, 0) == XXWIDGETS_OK && !about->image && !about->image_width);
    CHECK(xxwidgets_widget_create(other, NULL, XXWIDGETS_WINDOW, "second owner", bounds, &other_window) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_set_enabled(other_window, 0) == XXWIDGETS_OK);
    about_mode = 1;
    CHECK(xxwidgets_about_dialog_show(about, other_window) == XXWIDGETS_OK && !other_window->enabled);
    CHECK(!other->on_event && !other->user_data && !other->quit && about_polls == 4);
    CHECK(xxwidgets_widget_destroy(other_window) == XXWIDGETS_OK);
    about_mode = 0; current_about = NULL;
    CHECK(xxwidgets_about_dialog_destroy(about) == XXWIDGETS_OK);
}

static xxwidgets_widget *make(xxwidgets_app *app, xxwidgets_widget *parent, xxwidgets_kind kind, const char *text)
{
    xxwidgets_widget *widget = NULL;
    xxwidgets_rect rect = {1, 1, 20, 2};
    CHECK(xxwidgets_widget_create(app, parent, kind, text, rect, &widget) == XXWIDGETS_OK);
    return widget;
}

static void check_archiveview(xxwidgets_app *app, xxwidgets_widget *window, xxwidgets_widget *edit)
{
    char path[] = "folder/Gr\xc3\xbc\xc3\x9f" "e.txt";
    xxwidgets_archive_entry entries[] = {
        {"folder/", 42, 1}, {path, UINT64_MAX, 0}, {"line\n\t\x7f.txt", 0, 0}
    };
    xxwidgets_archive_entry invalid[] = {{"", 0, 0}}, entry;
    xxwidgets_widget *archive = make(app, window, XXWIDGETS_ARCHIVEVIEW, "archive");
    size_t index;
    uint64_t revision;
    int value;
    CHECK(xxwidgets_archiveview_count(NULL) == 0 && xxwidgets_archiveview_count(edit) == 0);
    CHECK(xxwidgets_archiveview_get_selection(archive, &index, &entry) == XXWIDGETS_OK);
    CHECK(index == SIZE_MAX && !entry.path && !entry.size && !entry.is_directory);
    CHECK(xxwidgets_archiveview_set_entries(edit, entries, 3) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_archiveview_set_entries(NULL, entries, 3) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_archiveview_set_entries(archive, NULL, 1) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_archiveview_set_entries(archive, entries, SIZE_MAX) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_archiveview_set_entries(archive, invalid, 1) == XXWIDGETS_INVALID_ARGUMENT);
    invalid[0].path = NULL;
    CHECK(xxwidgets_archiveview_set_entries(archive, invalid, 1) == XXWIDGETS_INVALID_ARGUMENT);
    invalid[0].path = "\xed\xa0\x80";
    CHECK(xxwidgets_archiveview_set_entries(archive, invalid, 1) == XXWIDGETS_INVALID_ARGUMENT);
    invalid[0].path = "valid"; invalid[0].is_directory = 2;
    CHECK(xxwidgets_archiveview_set_entries(archive, invalid, 1) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_archiveview_set_entries(archive, entries, 3) == XXWIDGETS_OK);
    CHECK(xxwidgets_archiveview_count(archive) == 3);
    CHECK(xxwidgets_archiveview_get_selection(archive, &index, &entry) == XXWIDGETS_OK);
    CHECK(index == 0 && entry.is_directory == 1 && entry.size == 42 && strcmp(entry.path, "folder/") == 0);
    CHECK(strncmp(archive->items[0], "<DIR>", 5) == 0);
    CHECK(strcmp(archive->items[0] + 28, "folder/") == 0);
    CHECK(strstr(archive->items[1], "18446744073709551615") != NULL);
    CHECK(strcmp(archive->items[1] + 28, path) == 0);
    CHECK(strcmp(archive->items[2] + 28, "line\\x0A\\x09\\x7F.txt") == 0);
    CHECK(archive->archive_columns == 48);
    path[0] = 'X'; entries[1].size = 7;
    CHECK(xxwidgets_archiveview_get_entry(archive, 1, &entry) == XXWIDGETS_OK);
    CHECK(entry.path[0] == 'f' && entry.path != path && entry.size == UINT64_MAX);
    CHECK(xxwidgets_archiveview_get_entry(archive, 3, &entry) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_archiveview_get_entry(archive, 0, NULL) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_archiveview_get_entry(edit, 0, &entry) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_archiveview_get_selection(archive, NULL, &entry) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_archiveview_get_selection(archive, &index, NULL) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_widget_set_value(archive, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_get_value(archive, &value) == XXWIDGETS_OK && value == 1);
    CHECK(xxwidgets_archiveview_get_selection(archive, &index, &entry) == XXWIDGETS_OK);
    CHECK(index == 1 && entry.size == UINT64_MAX && !entry.is_directory);
    CHECK(xxwidgets_widget_set_value(archive, 3) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_listbox_add(archive, "invalid") == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_listbox_clear(archive) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_widget_focus(archive) == XXWIDGETS_OK);
    revision = archive->archive_revision;
    fail_sync = 1;
    CHECK(xxwidgets_archiveview_set_entries(archive, entries, 3) == XXWIDGETS_PLATFORM_ERROR);
    CHECK(archive->archive_revision == revision && archive->value == 1);
    CHECK(xxwidgets_archiveview_get_entry(archive, 1, &entry) == XXWIDGETS_OK && entry.size == UINT64_MAX);
    fail_sync = 1;
    CHECK(xxwidgets_archiveview_clear(archive) == XXWIDGETS_PLATFORM_ERROR);
    CHECK(xxwidgets_archiveview_count(archive) == 3 && archive->value == 1);
    CHECK(xxwidgets_archiveview_set_entries(archive, entries, 3) == XXWIDGETS_OK);
    CHECK(archive->archive_revision != revision && archive->value == 0);
    CHECK(strcmp(archive->items[1] + 28, path) == 0);
    /* A borrowed entry can be supplied back to the widget safely. */
    CHECK(xxwidgets_archiveview_get_entry(archive, 1, &entry) == XXWIDGETS_OK);
    CHECK(xxwidgets_archiveview_set_entries(archive, &entry, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_archiveview_count(archive) == 1 && archive->value == 0);
    CHECK(xxwidgets_widget_set_value(archive, -1) == XXWIDGETS_OK);
    CHECK(xxwidgets_archiveview_get_selection(archive, &index, &entry) == XXWIDGETS_OK && index == SIZE_MAX && !entry.path);
    CHECK(xxwidgets_archiveview_clear(archive) == XXWIDGETS_OK);
    CHECK(xxwidgets_archiveview_count(archive) == 0 && archive->value == -1 && !archive->archive_columns);
    CHECK(xxwidgets_archiveview_clear(edit) == XXWIDGETS_INVALID_ARGUMENT);
}

static int browser_events;
static xxwidgets_event_type browser_event_type;
static int browser_event_value;
static void browser_event(xxwidgets_app *app, const xxwidgets_event *event, void *user_data)
{
    (void)app; (void)user_data;
    ++browser_events;
    browser_event_type = event->type;
    browser_event_value = event->value;
}

static void check_archivebrowser(xxwidgets_app *app, xxwidgets_widget *window, xxwidgets_widget *edit)
{
    char path[] = "folder/Gr\xc3\xbc\xc3\x9f" "e.txt";
    char modified[] = "2026-09-28 12:00";
    char attributes[] = "A";
    xxwidgets_archive_browser_entry entries[] = {
        {"folder/sub/one.bin", 100, 0, 0, XXWIDGETS_ARCHIVE_SIZE_KNOWN, "", ""},
        {"folder\\two.txt", 20, 12, 0, XXWIDGETS_ARCHIVE_SIZE_KNOWN | XXWIDGETS_ARCHIVE_PACKED_SIZE_KNOWN, "", "A"},
        {"folder/", 0, 0, 1, 0, "2026-09-27", "D"},
        {"loose.txt", UINT64_MAX, UINT64_MAX - 1, 0, 3, "", ""},
        {"empty/", 0, 0, 1, 0, "", "D"},
        {"./folder//sub/./three.txt", 3, 0, 0, 1, "", ""},
        {"z-unknown", 888, 999, 0, 0, NULL, NULL},
        {path, 5, 2, 0, 3, modified, attributes},
        {"\xd0\x9f\xd0\xb0\xd0\xbf\xd0\xba\xd0\xb0/child", 44, 0, 0, 1, "", ""}
    };
    xxwidgets_archive_browser_entry entry, invalid = {"../bad", 0, 0, 0, 0, NULL, NULL};
    xxwidgets_widget *browser = make(app, window, XXWIDGETS_ARCHIVEBROWSER, "browser");
    size_t source, row;
    int value;
    uint64_t revision;
    xxwidgets_event_fn saved = app->on_event;
    app->on_event = browser_event;
    CHECK(xxwidgets_archivebrowser_count(NULL) == 0 && xxwidgets_archivebrowser_visible_count(edit) == 0);
    CHECK(xxwidgets_archivebrowser_get_selection(browser, &source, &entry) == XXWIDGETS_OK && source == SIZE_MAX && !entry.path);
    CHECK(xxwidgets_archivebrowser_set_entries(edit, entries, 9) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_archivebrowser_set_entries(browser, NULL, 1) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_archivebrowser_set_entries(browser, entries, SIZE_MAX) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_archivebrowser_set_entries(browser, &invalid, 1) == XXWIDGETS_INVALID_ARGUMENT);
    invalid.path = "bad\xed\xa0\x80";
    CHECK(xxwidgets_archivebrowser_set_entries(browser, &invalid, 1) == XXWIDGETS_INVALID_ARGUMENT);
    invalid.path = "valid"; invalid.modified = "\xff";
    CHECK(xxwidgets_archivebrowser_set_entries(browser, &invalid, 1) == XXWIDGETS_INVALID_ARGUMENT);
    invalid.modified = NULL; invalid.flags = 128;
    CHECK(xxwidgets_archivebrowser_set_entries(browser, &invalid, 1) == XXWIDGETS_INVALID_ARGUMENT);
    invalid.flags = 0; invalid.is_directory = 2;
    CHECK(xxwidgets_archivebrowser_set_entries(browser, &invalid, 1) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_archivebrowser_set_archive(browser, "archive.zip") == XXWIDGETS_OK);
    CHECK(strcmp(xxwidgets_archivebrowser_archive(browser), "archive.zip") == 0);
    CHECK(xxwidgets_archivebrowser_set_entries(browser, entries, 9) == XXWIDGETS_OK);
    path[0] = 'X'; modified[0] = 'X'; attributes[0] = 'X';
    CHECK(xxwidgets_archivebrowser_count(browser) == 9 && xxwidgets_archivebrowser_visible_count(browser) == 5);
    CHECK(browser->item_count == 5 && browser->value == 0 && browser->archive_columns > 90);
    CHECK(strcmp(xxwidgets_archivebrowser_name(browser, 0), "empty") == 0);
    CHECK(xxwidgets_archivebrowser_get_entry(browser, 1, &source, &entry) == XXWIDGETS_OK && source == 2);
    CHECK(entry.is_directory && strcmp(entry.path, "folder/") == 0 && strcmp(entry.modified, "2026-09-27") == 0);
    CHECK(xxwidgets_archivebrowser_get_entry(browser, 2, &source, &entry) == XXWIDGETS_OK && source == SIZE_MAX && entry.is_directory);
    CHECK(strcmp(entry.path, "\xd0\x9f\xd0\xb0\xd0\xbf\xd0\xba\xd0\xb0/") == 0);
    CHECK(strstr(browser->items[3], "18446744073709551615") != NULL);
    CHECK(strstr(browser->items[4], "888") == NULL && strstr(browser->items[4], "999") == NULL);
    CHECK(xxwidgets_archivebrowser_get_entry(browser, 5, &source, &entry) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_archivebrowser_get_entry(browser, 0, NULL, &entry) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_archivebrowser_get_selection(browser, &source, NULL) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_archivebrowser_set_directory(browser, "missing") == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_archivebrowser_set_directory(browser, "folder/../") == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_archivebrowser_set_directory(browser, "./folder\\") == XXWIDGETS_OK);
    CHECK(strcmp(xxwidgets_archivebrowser_directory(browser), "folder/") == 0 && browser->item_count == 3);
    CHECK(xxwidgets_archivebrowser_get_entry(browser, 0, &source, &entry) == XXWIDGETS_OK && source == SIZE_MAX && entry.is_directory);
    CHECK(strcmp(entry.path, "folder/sub/") == 0);
    CHECK(xxwidgets_archivebrowser_get_entry(browser, 1, &source, &entry) == XXWIDGETS_OK && source == 7);
    CHECK(entry.path[0] == 'f' && entry.modified[0] == '2' && entry.attributes[0] == 'A');
    CHECK(strcmp(xxwidgets_archivebrowser_name(browser, 1), "Gr\xc3\xbc\xc3\x9f" "e.txt") == 0);
    CHECK(xxwidgets_widget_set_value(browser, 2) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_get_selection(browser, &source, &entry) == XXWIDGETS_OK && source == 1);
    CHECK(strcmp(entry.path, "folder\\two.txt") == 0);
    CHECK(xxwidgets_archivebrowser_sort(browser, XXWIDGETS_ARCHIVE_COLUMN_SIZE, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_get_selection(browser, &source, &entry) == XXWIDGETS_OK && source == 1 && browser->value == 1);
    CHECK(xxwidgets_archivebrowser_sort_column(browser) == XXWIDGETS_ARCHIVE_COLUMN_SIZE && xxwidgets_archivebrowser_sort_descending(browser) == 1);
    CHECK(xxwidgets_archivebrowser_user_activate(browser, 0) == XXWIDGETS_OK);
    CHECK(browser_events == 1 && browser_event_type == XXWIDGETS_EVENT_CHANGE);
    CHECK(strcmp(xxwidgets_archivebrowser_directory(browser), "folder/sub/") == 0 && browser->item_count == 2);
    CHECK(xxwidgets_archivebrowser_get_entry(browser, 0, &source, &entry) == XXWIDGETS_OK && source == 0 && entry.size == 100);
    CHECK(xxwidgets_archivebrowser_user_activate(browser, 1) == XXWIDGETS_OK);
    CHECK(browser_events == 2 && browser_event_type == XXWIDGETS_EVENT_ACTIVATE && browser_event_value == 1);
    CHECK(xxwidgets_archivebrowser_get_selection(browser, &source, &entry) == XXWIDGETS_OK && source == 5);
    revision = browser->browser_revision;
    fail_sync = 1;
    CHECK(xxwidgets_archivebrowser_set_entries(browser, entries, 9) == XXWIDGETS_PLATFORM_ERROR);
    CHECK(browser->browser_revision == revision && browser->item_count == 2 && browser->value == 1);
    CHECK(strcmp(xxwidgets_archivebrowser_directory(browser), "folder/sub/") == 0);
    fail_sync = 1;
    CHECK(xxwidgets_archivebrowser_set_archive(browser, "failed.zip") == XXWIDGETS_PLATFORM_ERROR);
    CHECK(strcmp(xxwidgets_archivebrowser_archive(browser), "archive.zip") == 0 && browser->browser_revision == revision);
    fail_sync = 1;
    CHECK(xxwidgets_archivebrowser_set_directory(browser, "") == XXWIDGETS_PLATFORM_ERROR);
    CHECK(strcmp(xxwidgets_archivebrowser_directory(browser), "folder/sub/") == 0 && browser->value == 1);
    fail_sync = 1;
    CHECK(xxwidgets_archivebrowser_sort(browser, XXWIDGETS_ARCHIVE_COLUMN_NAME, 0) == XXWIDGETS_PLATFORM_ERROR);
    CHECK(xxwidgets_archivebrowser_sort_column(browser) == XXWIDGETS_ARCHIVE_COLUMN_SIZE && browser->value == 1);
    fail_sync = 1;
    CHECK(xxwidgets_archivebrowser_user_up(browser) == XXWIDGETS_PLATFORM_ERROR && browser_events == 2);
    CHECK(xxwidgets_archivebrowser_user_up(browser) == XXWIDGETS_OK && browser_events == 3);
    CHECK(strcmp(xxwidgets_archivebrowser_directory(browser), "folder/") == 0);
    CHECK(xxwidgets_archivebrowser_user_up(browser) == XXWIDGETS_OK && browser_events == 4);
    CHECK(xxwidgets_archivebrowser_directory(browser)[0] == 0);
    CHECK(xxwidgets_archivebrowser_user_up(browser) == XXWIDGETS_OK && browser_events == 4);
    CHECK(xxwidgets_archivebrowser_sort(browser, XXWIDGETS_ARCHIVE_COLUMN_SIZE, 0) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_get_entry(browser, 4, &source, &entry) == XXWIDGETS_OK && source == 6);
    CHECK(xxwidgets_archivebrowser_sort(browser, XXWIDGETS_ARCHIVE_COLUMN_SIZE, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_get_entry(browser, 4, &source, &entry) == XXWIDGETS_OK && source == 6);
    CHECK(xxwidgets_archivebrowser_user_sort(browser, XXWIDGETS_ARCHIVE_COLUMN_SIZE) == XXWIDGETS_OK && browser_events == 5);
    CHECK(xxwidgets_archivebrowser_sort_descending(browser) == 0);
    CHECK(xxwidgets_widget_set_value(browser, -1) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_sort(browser, XXWIDGETS_ARCHIVE_COLUMN_NAME, 0) == XXWIDGETS_OK && browser->value == -1);
    CHECK(xxwidgets_archivebrowser_get_selection(browser, &source, &entry) == XXWIDGETS_OK && source == SIZE_MAX && !entry.path);
    CHECK(xxwidgets_widget_get_value(browser, &value) == XXWIDGETS_OK && value == -1);
    CHECK(xxwidgets_archivebrowser_set_directory(browser, "empty") == XXWIDGETS_OK && browser->item_count == 0 && browser->value == -1);
    CHECK(xxwidgets_archivebrowser_up(browser) == XXWIDGETS_OK && browser->item_count == 5 && browser_events == 5);
    /* Both borrowed member metadata and a borrowed synthetic directory can be
     * safely supplied as the next dataset before old state is released. */
    CHECK(xxwidgets_archivebrowser_get_entry(browser, 3, &source, &entry) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_set_entries(browser, &entry, 1) == XXWIDGETS_OK && browser->item_count == 1);
    CHECK(xxwidgets_archivebrowser_set_entries(browser, NULL, 0) == XXWIDGETS_OK && browser->item_count == 0 && browser->value == -1);
    CHECK(xxwidgets_archivebrowser_count(browser) == 0 && strcmp(xxwidgets_archivebrowser_archive(browser), "archive.zip") == 0);
    {
        xxwidgets_archive_browser_entry nested = {"implicit/deep/file", 0, 0, 0, 0, NULL, NULL};
        CHECK(xxwidgets_archivebrowser_set_entries(browser, &nested, 1) == XXWIDGETS_OK);
        CHECK(xxwidgets_archivebrowser_get_entry(browser, 0, &source, &entry) == XXWIDGETS_OK && source == SIZE_MAX && entry.is_directory);
        CHECK(xxwidgets_archivebrowser_set_entries(browser, &entry, 1) == XXWIDGETS_OK);
        CHECK(xxwidgets_archivebrowser_get_entry(browser, 0, &source, &entry) == XXWIDGETS_OK && source == 0 && entry.is_directory);
        CHECK(strcmp(entry.path, "implicit/") == 0);
        CHECK(xxwidgets_archivebrowser_set_directory(browser, "implicit") == XXWIDGETS_OK && browser->item_count == 0);
    }
    {
        xxwidgets_archive_browser_entry controls = {"line\n\t.txt", 0, 0, 0, 1, "date\n", "\x7f"};
        CHECK(xxwidgets_archivebrowser_set_entries(browser, &controls, 1) == XXWIDGETS_OK);
        CHECK(strstr(browser->items[0], "line\\x0A\\x09.txt") && strstr(browser->items[0], "date\\x0A") && strstr(browser->items[0], "\\x7F"));
        CHECK(strcmp(xxwidgets_archivebrowser_cell(browser, 0, XXWIDGETS_ARCHIVE_COLUMN_NAME), "line\\x0A\\x09.txt") == 0);
        CHECK(strcmp(xxwidgets_archivebrowser_cell(browser, 0, XXWIDGETS_ARCHIVE_COLUMN_MODIFIED), "date\\x0A") == 0);
        CHECK(strcmp(xxwidgets_archivebrowser_cell(browser, 0, XXWIDGETS_ARCHIVE_COLUMN_ATTRIBUTES), "\\x7F") == 0);
    }
    for (row = 0; row < browser->item_count; ++row) CHECK(strlen(browser->items[row]) <= browser->archive_columns + 8);
    {
        char title[] = "CRC32", crc[] = "75BCC38E";
        xxwidgets_archive_property first[] = {{title, crc}, {"Comment", "hello\nworld"}};
        xxwidgets_archive_property second[] = {{"CRC32", "00000001"}, {"Method", "Deflate"}};
        xxwidgets_archive_browser_entry detailed[3] = {0};
        detailed[0].path = "word/document.xml"; detailed[0].properties = first; detailed[0].property_count = 2;
        detailed[1].path = "word/fontTable.xml"; detailed[1].properties = second; detailed[1].property_count = 2;
        detailed[2].path = "word/theme/theme1.xml";
        CHECK(xxwidgets_archivebrowser_set_entries(browser, detailed, 3) == XXWIDGETS_OK);
        title[0] = 'X'; crc[0] = 'X';
        CHECK(xxwidgets_archivebrowser_column_count(browser) == 5);
        CHECK(xxwidgets_archivebrowser_set_directory(browser, "word") == XXWIDGETS_OK);
        CHECK(xxwidgets_widget_set_value(browser, 1) == XXWIDGETS_OK);
        CHECK(xxwidgets_archivebrowser_get_selection(browser, &source, &entry) == XXWIDGETS_OK && source == 0);
        CHECK(xxwidgets_archivebrowser_set_advanced(browser, 1) == XXWIDGETS_OK);
        CHECK(xxwidgets_archivebrowser_column_count(browser) == 8);
        CHECK(!strcmp(xxwidgets_archivebrowser_column_title(browser, 5), "CRC32"));
        CHECK(!strcmp(xxwidgets_archivebrowser_cell(browser, 0, (xxwidgets_archive_column)5), ""));
        CHECK(!strcmp(xxwidgets_archivebrowser_cell(browser, 1, (xxwidgets_archive_column)5), "75BCC38E"));
        CHECK(!strcmp(xxwidgets_archivebrowser_cell(browser, 1, (xxwidgets_archive_column)6), "hello\\x0Aworld"));
        CHECK(xxwidgets_archivebrowser_get_selection(browser, &source, &entry) == XXWIDGETS_OK && source == 0);
        CHECK(xxwidgets_archivebrowser_sort(browser, (xxwidgets_archive_column)5, 0) == XXWIDGETS_OK);
        CHECK(xxwidgets_archivebrowser_get_selection(browser, &source, &entry) == XXWIDGETS_OK && source == 0 && browser->value == 2);
        revision = browser->browser_revision;
        fail_sync = 1;
        CHECK(xxwidgets_archivebrowser_set_advanced(browser, 0) == XXWIDGETS_PLATFORM_ERROR);
        CHECK(browser->browser_revision == revision && xxwidgets_archivebrowser_column_count(browser) == 8);
        CHECK(xxwidgets_archivebrowser_set_advanced(browser, 0) == XXWIDGETS_OK);
        CHECK(xxwidgets_archivebrowser_column_count(browser) == 5);
        CHECK(!strcmp(xxwidgets_archivebrowser_directory(browser), "word/"));
        CHECK(xxwidgets_archivebrowser_get_selection(browser, &source, &entry) == XXWIDGETS_OK && source == 0);
        CHECK(xxwidgets_archivebrowser_set_advanced(browser, 2) == XXWIDGETS_INVALID_ARGUMENT);
        CHECK(xxwidgets_archivebrowser_set_advanced(browser, 1) == XXWIDGETS_OK);
        CHECK(xxwidgets_archivebrowser_set_entries(browser, NULL, 0) == XXWIDGETS_OK);
        CHECK(xxwidgets_archivebrowser_column_count(browser) == 5 && browser->item_count == 0);
    }
    CHECK(browser_events == 5);
    app->on_event = saved;
}

#ifdef XXWIDGETS_WITH_SETTINGS
static void check_settings_options(xxwidgets_widget *window)
{
    const char *path = "xxwidgets-settings-test.ini";
    xxwidgets_setting_option options[] = {{"Advanced", "UI/Advanced", 0}, {"Log", "UI/Log", 1}};
    xxwidgets_setting_option invalid[] = {{"Advanced", "UI/Advanced", 0}, {"Log", "/invalid", 1}};
    xx_settings_value text = {0};
    const xx_settings_value *value;
    xx_settings *settings = xx_settings_create_memory(), *reopened;
    int accepted = 1, polls;
    CHECK(settings);
    CHECK(xxwidgets_settings_get_bool(NULL, "UI/Advanced", 1) == 1);
    CHECK(xxwidgets_settings_set_bool(settings, "UI/Advanced", 2) == XXWIDGETS_INVALID_ARGUMENT);
    dialog_mode = 2; dialog_check_values = 1;
    dialog_expected[0] = 0; dialog_expected[1] = 1;
    CHECK(xxwidgets_settings_options_dialog(window, "Options", settings, options, 2, &accepted) == XXWIDGETS_OK);
    CHECK(!accepted && xx_settings_count(settings) == 0);
    polls = dialog_polls;
    CHECK(xxwidgets_settings_options_dialog(window, "Options", settings, invalid, 2, &accepted) == XXWIDGETS_INVALID_ARGUMENT);
    invalid[1].key = "UI/Advanced";
    CHECK(xxwidgets_settings_options_dialog(window, "Options", settings, invalid, 2, &accepted) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(dialog_polls == polls && xx_settings_count(settings) == 0);
    dialog_mode = 1;
    CHECK(xxwidgets_settings_options_dialog(window, "Options", settings, options, 2, &accepted) == XXWIDGETS_OK);
    CHECK(accepted && xxwidgets_settings_get_bool(settings, "UI/Advanced", 0) == 1 &&
        xxwidgets_settings_get_bool(settings, "UI/Log", 0) == 1);
    CHECK(xxwidgets_settings_set_bool(settings, "UI/Advanced", 0) == XXWIDGETS_OK);
    CHECK(xxwidgets_settings_get_bool(settings, "UI/Advanced", 1) == 0);
    xx_settings_destroy(settings);

    remove(path);
    settings = xx_settings_create_ini(path);
    CHECK(settings);
    text.type = XX_SETTINGS_VALUE_STRING;
    text.data.buffer.data = "preserve"; text.data.buffer.size = 8;
    CHECK(xx_settings_set(settings, "Other/Text", &text) == XXFC_OK);
    CHECK(xx_settings_set(settings, "UI/Log", &text) == XXFC_OK);
    CHECK(xxwidgets_settings_set_bool(settings, "UI/Advanced", 0) == XXWIDGETS_OK);
    for (dialog_mode = 2; dialog_mode <= 4; ++dialog_mode) {
        xxwidgets_status status = xxwidgets_settings_options_dialog(window, "Options", settings, options, 2, &accepted);
        CHECK(status == (dialog_mode == 4 ? XXWIDGETS_PLATFORM_ERROR : XXWIDGETS_OK));
        CHECK(!accepted && xxwidgets_settings_get_bool(settings, "UI/Advanced", 1) == 0);
        value = xx_settings_get(settings, "UI/Log");
        CHECK(value && value->type == XX_SETTINGS_VALUE_STRING);
        reopened = xx_settings_create_ini(path);
        CHECK(reopened && xx_settings_load(reopened) == XXFC_OK);
        value = xx_settings_get(reopened, "UI/Log");
        CHECK(value && value->type == XX_SETTINGS_VALUE_STRING);
        xx_settings_destroy(reopened);
    }
    dialog_mode = 1;
    CHECK(xxwidgets_settings_options_dialog(window, "Options", settings, options, 2, &accepted) == XXWIDGETS_OK && accepted);
    xx_settings_destroy(settings);
    settings = xx_settings_create_ini(path);
    CHECK(settings && xx_settings_load(settings) == XXFC_OK);
    CHECK(xxwidgets_settings_get_bool(settings, "UI/Advanced", 0) == 1);
    CHECK(xxwidgets_settings_get_bool(settings, "UI/Log", 0) == 1);
    value = xx_settings_get(settings, "Other/Text");
    CHECK(value && value->type == XX_SETTINGS_VALUE_STRING && value->data.buffer.size == 8 &&
        !memcmp(value->data.buffer.data, "preserve", 8));
    xx_settings_destroy(settings);

    /* A regular file cannot be the parent of another settings file. */
    settings = xx_settings_create_ini("xxwidgets-settings-test.ini/child.ini");
    CHECK(settings && xx_settings_set(settings, "UI/Advanced", &text) == XXFC_OK);
    dialog_expected[0] = 0;
    CHECK(xxwidgets_settings_options_dialog(window, "Options", settings, options, 2, &accepted) == XXWIDGETS_PLATFORM_ERROR);
    CHECK(!accepted && !xx_settings_get(settings, "UI/Log"));
    value = xx_settings_get(settings, "UI/Advanced");
    CHECK(value && value->type == XX_SETTINGS_VALUE_STRING && value->data.buffer.size == 8 &&
        !memcmp(value->data.buffer.data, "preserve", 8));
    CHECK(xxwidgets_settings_set_bool(settings, "UI/New", 1) == XXWIDGETS_PLATFORM_ERROR);
    CHECK(!xx_settings_get(settings, "UI/New"));
    xx_settings_destroy(settings);
    CHECK(remove(path) == 0);
    dialog_mode = dialog_check_values = 0;
}
#endif

static void check_browser_multiselect(xxwidgets_app *app, xxwidgets_widget *window)
{
    xxwidgets_archive_browser_entry entries[6] = {0};
    const char *paths[] = {"chosen/a.txt", "chosen/deep/b.txt", "chosen-other/c.txt", "root.txt", "empty/", "root.txt"};
    xxwidgets_widget *browser = make(app, window, XXWIDGETS_ARCHIVEBROWSER, "multiple selection");
    size_t i, count, indexes[6], rows[] = {0, 3, 4, 0};
    for (i = 0; i < 6; ++i) entries[i].path = paths[i];
    entries[4].is_directory = 1;
    CHECK(xxwidgets_archivebrowser_set_entries(browser, entries, 6) == XXWIDGETS_OK);
    /* Root rows: chosen, chosen-other, empty, root.txt (twice). */
    CHECK(xxwidgets_archivebrowser_set_selection(browser, rows, 4) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_selection_count(browser) == 3);
    CHECK(xxwidgets_archivebrowser_selected_sources(browser, indexes, 6, &count) == XXWIDGETS_OK);
    CHECK(count == 4 && indexes[0] == 0 && indexes[1] == 1 && indexes[2] == 3 && indexes[3] == 5);
    CHECK(xxwidgets_archivebrowser_selected_sources(browser, indexes, 1, &count) == XXWIDGETS_BUFFER_TOO_SMALL && count == 4);
    CHECK(xxwidgets_archivebrowser_sort(browser, XXWIDGETS_ARCHIVE_COLUMN_NAME, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_set_advanced(browser, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_set_archive(browser, "multiple.zip") == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_selection_count(browser) == 3);
    CHECK(xxwidgets_archivebrowser_selected_sources(browser, indexes, 6, &count) == XXWIDGETS_OK);
    CHECK(count == 4 && indexes[0] == 0 && indexes[1] == 1 && indexes[2] == 3 && indexes[3] == 5);
    rows[0] = 99;
    CHECK(xxwidgets_archivebrowser_set_selection(browser, rows, 1) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_archivebrowser_selection_count(browser) == 3);
    fail_sync = 1;
    CHECK(xxwidgets_archivebrowser_set_selection(browser, NULL, 0) == XXWIDGETS_PLATFORM_ERROR);
    CHECK(xxwidgets_archivebrowser_selection_count(browser) == 3);
    CHECK(xxwidgets_archivebrowser_set_selection(browser, NULL, 0) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_selected_sources(browser, NULL, 0, &count) == XXWIDGETS_OK && count == 0);
    CHECK(xxwidgets_archivebrowser_set_directory(browser, "chosen") == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_selection_count(browser) == 1);
    CHECK(xxwidgets_archivebrowser_select_all(browser) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_selected_sources(browser, indexes, 6, &count) == XXWIDGETS_OK && count == 2);
    CHECK(indexes[0] == 0 && indexes[1] == 1);
    CHECK(xxwidgets_widget_set_value(browser, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_selection_count(browser) == 1);
    CHECK(xxwidgets_archivebrowser_up(browser) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_selection_count(browser) == 1);
    CHECK(xxwidgets_archivebrowser_set_entries(browser, NULL, 0) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_selection_count(browser) == 0);
    CHECK(xxwidgets_widget_destroy(browser) == XXWIDGETS_OK);
}

int main(void)
{
    xxwidgets_app *app = NULL, *other = NULL;
    xxwidgets_widget *window, *edit, *check, *list, *progress, *button, *hex, *unused = NULL;
    xxwidgets_rect rect = {0, 0, 20, 10};
    xxwidgets_config config = { XXWIDGETS_BACKEND_TUI, on_event, &events };
    char buffer[64];
    size_t size;
    int value;
    CHECK(xxwidgets_app_create(NULL, NULL) == XXWIDGETS_INVALID_ARGUMENT);
    config.backend = (xxwidgets_backend)99;
    CHECK(xxwidgets_app_create(&config, &app) == XXWIDGETS_INVALID_ARGUMENT && !app);
    config.backend = XXWIDGETS_BACKEND_NATIVE;
    CHECK(xxwidgets_app_create(&config, &app) == XXWIDGETS_UNAVAILABLE && !app);
    config.backend = XXWIDGETS_BACKEND_TUI;
    init_failure = 1;
    CHECK(xxwidgets_app_create(&config, &app) == XXWIDGETS_UNAVAILABLE && !app && shutdowns == 1);
    init_failure = 0;
    CHECK(xxwidgets_app_create(&config, &app) == XXWIDGETS_OK);
    CHECK(xxwidgets_app_backend(app) == XXWIDGETS_BACKEND_TUI);
    CHECK(strcmp(xxwidgets_app_backend_name(app), "mock") == 0);
    CHECK(xxwidgets_app_create(NULL, &other) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_create(app, NULL, XXWIDGETS_EDIT, "", rect, &unused) == XXWIDGETS_INVALID_ARGUMENT);
    create_failure = 1;
    CHECK(xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, "", rect, &unused) == XXWIDGETS_PLATFORM_ERROR);
    CHECK(!app->widgets && !unused && destroys == 1);
    create_failure = 0;
    window = make(app, NULL, XXWIDGETS_WINDOW, "window");
    CHECK(xxwidgets_widget_create(other, window, XXWIDGETS_LABEL, "", rect, &unused) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_widget_create(app, window, XXWIDGETS_WINDOW, "", rect, &unused) == XXWIDGETS_INVALID_ARGUMENT);
    edit = make(app, window, XXWIDGETS_EDIT, "hello");
    check = make(app, window, XXWIDGETS_CHECKBOX, "enabled");
    list = make(app, window, XXWIDGETS_LISTBOX, "");
    progress = make(app, window, XXWIDGETS_PROGRESS, "");
    button = make(app, window, XXWIDGETS_BUTTON, "go");
    hex = make(app, window, XXWIDGETS_HEXVIEW, "bytes");
    check_archiveview(app, window, edit);
    check_archivebrowser(app, window, edit);
    check_browser_multiselect(app, window);
    check_about(app, other, window, edit);
    check_combos(app, window, edit);
    {
        unsigned char data[19] = {0, 0x20, 0x41, 0x7e, 0x7f, 0x80, 0xff, 0x0a};
        size_t offset, length;
        uint64_t revision;
        CHECK(xxwidgets_hexview_size(hex) == 0);
        CHECK(xxwidgets_hexview_get_selection(hex, &offset, &length) == XXWIDGETS_OK && offset == SIZE_MAX && length == 0);
        CHECK(xxwidgets_hexview_set_data(edit, data, sizeof(data)) == XXWIDGETS_INVALID_ARGUMENT);
        CHECK(xxwidgets_hexview_set_data(hex, NULL, 1) == XXWIDGETS_INVALID_ARGUMENT);
        CHECK(xxwidgets_hexview_set_layout(hex, 0, 10) == XXWIDGETS_INVALID_ARGUMENT);
        CHECK(xxwidgets_hexview_set_data(hex, data, sizeof(data)) == XXWIDGETS_OK);
        CHECK(hex->item_count == 2 && hex->value == 0 && xxwidgets_hexview_size(hex) == 19);
        CHECK(strlen(hex->items[0]) == 85);
        CHECK(strncmp(hex->items[0], "0000000000000000  00 20 41 7E 7F 80 FF 0A", 41) == 0);
        CHECK(strstr(hex->items[0], "|. A~....") != NULL);
        data[0] = 0xaa;
        CHECK(hex->hex_data[0] == 0);
        CHECK(xxwidgets_widget_set_value(hex, 1) == XXWIDGETS_OK);
        CHECK(xxwidgets_hexview_get_selection(hex, &offset, &length) == XXWIDGETS_OK && offset == 16 && length == 3);
        CHECK(xxwidgets_widget_set_value(hex, 2) == XXWIDGETS_INVALID_ARGUMENT);
        CHECK(xxwidgets_hexview_set_layout(hex, UINT64_C(0x123456789ABC0000), 8) == XXWIDGETS_OK);
        CHECK(hex->item_count == 3 && hex->value == 2 && strlen(hex->items[0]) == 53);
        CHECK(strncmp(hex->items[2], "123456789ABC0010", 16) == 0);
        CHECK(strstr(hex->items[2], "|...     |") != NULL);
        CHECK(xxwidgets_hexview_set_layout(hex, UINT64_MAX - 17, 8) == XXWIDGETS_INVALID_ARGUMENT);
        CHECK(hex->hex_base == UINT64_C(0x123456789ABC0000));
        revision = hex->hex_revision;
        fail_sync = 1;
        CHECK(xxwidgets_hexview_set_data(hex, data, sizeof(data)) == XXWIDGETS_PLATFORM_ERROR);
        CHECK(hex->hex_data[0] == 0 && hex->value == 2 && hex->hex_revision == revision);
        fail_sync = 1;
        CHECK(xxwidgets_hexview_set_layout(hex, 0, 32) == XXWIDGETS_PLATFORM_ERROR);
        CHECK(hex->hex_columns == 8 && hex->hex_base == UINT64_C(0x123456789ABC0000));
        CHECK(xxwidgets_hexview_set_data(hex, data, sizeof(data)) == XXWIDGETS_OK);
        CHECK(hex->hex_data[0] == 0xaa && hex->value == 0 && hex->hex_revision != revision);
        CHECK(xxwidgets_listbox_add(hex, "invalid") == XXWIDGETS_INVALID_ARGUMENT);
        CHECK(xxwidgets_widget_focus(hex) == XXWIDGETS_OK);
        CHECK(xxwidgets_hexview_set_data(hex, NULL, 0) == XXWIDGETS_OK && hex->value == -1 && hex->item_count == 0);
        CHECK(xxwidgets_hexview_set_layout(hex, UINT64_MAX, 32) == XXWIDGETS_OK);
        CHECK(xxwidgets_hexview_set_data(hex, data, 1) == XXWIDGETS_OK);
        CHECK(strncmp(hex->items[0], "FFFFFFFFFFFFFFFF  AA", 20) == 0);
        CHECK(xxwidgets_hexview_set_data(hex, data, 2) == XXWIDGETS_INVALID_ARGUMENT);
        CHECK(xxwidgets_widget_set_value(hex, -1) == XXWIDGETS_OK);
        CHECK(xxwidgets_hexview_get_selection(hex, &offset, &length) == XXWIDGETS_OK && offset == SIZE_MAX && length == 0);
    }
    CHECK(xxwidgets_widget_set_text(edit, "\xf0\x80\x80\x80") == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_widget_set_text(edit, "\xed\xa0\x80") == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_widget_set_text(edit, "\xc3") == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_widget_set_text(edit, "\xe2\x82\xac") == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_get_text(edit, NULL, 0, &size) == XXWIDGETS_OK && size == 4);
    CHECK(xxwidgets_widget_get_text(edit, buffer, 3, NULL) == XXWIDGETS_BUFFER_TOO_SMALL && buffer[0] == 0);
    CHECK(xxwidgets_widget_get_text(edit, buffer, sizeof(buffer), NULL) == XXWIDGETS_OK && strlen(buffer) == 3);
    fail_sync = 1;
    CHECK(xxwidgets_widget_set_text(edit, "rollback") == XXWIDGETS_PLATFORM_ERROR);
    CHECK(strcmp(edit->text, "\xe2\x82\xac") == 0);
    CHECK(xxwidgets_widget_set_value(check, 2) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_widget_set_value(check, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_get_value(check, &value) == XXWIDGETS_OK && value == 1);
    CHECK(xxwidgets_widget_set_value(progress, 101) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_widget_set_value(progress, 50) == XXWIDGETS_OK);
    fail_sync = 1;
    CHECK(xxwidgets_widget_set_value(progress, 75) == XXWIDGETS_PLATFORM_ERROR && progress->value == 50);
    CHECK(xxwidgets_listbox_add(edit, "bad") == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_listbox_add(list, "first") == XXWIDGETS_OK);
    CHECK(xxwidgets_listbox_add(list, "second") == XXWIDGETS_OK);
    fail_sync = 1;
    CHECK(xxwidgets_listbox_add(list, "rollback") == XXWIDGETS_PLATFORM_ERROR && xxwidgets_listbox_count(list) == 2);
    CHECK(xxwidgets_widget_set_value(list, 2) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_widget_set_value(list, 1) == XXWIDGETS_OK);
    fail_sync = 1;
    CHECK(xxwidgets_listbox_clear(list) == XXWIDGETS_PLATFORM_ERROR && list->item_count == 2 && list->value == 1);
    CHECK(xxwidgets_listbox_clear(list) == XXWIDGETS_OK && list->item_count == 0 && list->value == -1);
    CHECK(xxwidgets_widget_set_enabled(edit, 0) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_focus(edit) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_widget_set_enabled(edit, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_set_visible(window, 0) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_focus(edit) == XXWIDGETS_INVALID_ARGUMENT);
    CHECK(xxwidgets_widget_set_visible(window, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_focus(edit) == XXWIDGETS_OK);
    xxwidgets_widget_set_user_data(button, &value);
    CHECK(xxwidgets_widget_user_data(button) == &value);
    rect.width = 0;
    CHECK(xxwidgets_widget_set_rect(button, rect) == XXWIDGETS_INVALID_ARGUMENT);
    {
        xxwidgets_option options[] = {{"Advanced", 0}, {"Show log", 1}};
        xxwidgets_event_fn callback = app->on_event;
        void *user = app->user_data;
        xxwidgets_widget *tail = app->widgets;
        int accepted;
        while (tail->next) tail = tail->next;
        CHECK(xxwidgets_options_dialog(edit, "invalid", options, 2, &accepted) == XXWIDGETS_INVALID_ARGUMENT);
        CHECK(xxwidgets_options_dialog(window, "invalid", options, 17, &accepted) == XXWIDGETS_INVALID_ARGUMENT);
        options[0].value = 2;
        CHECK(xxwidgets_options_dialog(window, "invalid", options, 2, &accepted) == XXWIDGETS_INVALID_ARGUMENT);
        options[0].value = 0;
        create_failure = 1;
        CHECK(xxwidgets_options_dialog(window, "failed", options, 2, &accepted) == XXWIDGETS_PLATFORM_ERROR);
        create_failure = 0;
        CHECK(window->enabled && !tail->next && app->on_event == callback && app->user_data == user);
        dialog_mode = 1;
        CHECK(xxwidgets_options_dialog(window, "Options", options, 2, &accepted) == XXWIDGETS_OK);
        CHECK(accepted == 1 && options[0].value == 1 && options[1].value == 1);
        CHECK(window->enabled && !app->modal_window && !tail->next);
        dialog_mode = 2;
        CHECK(xxwidgets_options_dialog(window, "Options", options, 2, &accepted) == XXWIDGETS_OK);
        CHECK(!accepted && options[0].value == 1);
        dialog_mode = 3;
        CHECK(xxwidgets_options_dialog(window, "Options", options, 2, &accepted) == XXWIDGETS_OK);
        CHECK(!accepted && options[0].value == 1);
        dialog_mode = 4;
        CHECK(xxwidgets_options_dialog(window, "Options", options, 2, &accepted) == XXWIDGETS_PLATFORM_ERROR);
        CHECK(!accepted && options[0].value == 1 && window->enabled);
        dialog_mode = 1;
        CHECK(xxwidgets_options_dialog(window, "Empty", NULL, 0, &accepted) == XXWIDGETS_OK && accepted);
        CHECK(dialog_polls == 5 && !tail->next && app->on_event == callback && app->user_data == user);
        dialog_mode = 0;
    }
#ifdef XXWIDGETS_WITH_SETTINGS
    check_settings_options(window);
#endif
    xxwidgets_test_shortcuts(app, window, edit);
    {
        xxwidgets_widget *tail = app->widgets;
        xxwidgets_event_fn callback = app->on_event;
        void *user = app->user_data;
        while (tail->next) tail = tail->next;
        CHECK(xxwidgets_text_dialog(window, NULL, "text") == XXWIDGETS_INVALID_ARGUMENT);
        CHECK(xxwidgets_text_dialog(edit, "Types", "text") == XXWIDGETS_INVALID_ARGUMENT);
        CHECK(xxwidgets_text_dialog(window, "Types", "\xff") == XXWIDGETS_INVALID_ARGUMENT);
        for (text_mode = 1; text_mode <= 4; ++text_mode) {
            text_polls = 0;
            CHECK(xxwidgets_text_dialog(window, "Types", text_expected) ==
                (text_mode == 4 ? XXWIDGETS_PLATFORM_ERROR : XXWIDGETS_OK));
            CHECK(window->enabled && !app->modal_window && !tail->next);
            CHECK(app->on_event == callback && app->user_data == user);
        }
        CHECK(text_copies == 3);
        text_mode = 0;
    }
    xxwidgets_test_process(app, window);
    CHECK(events == 0);
    CHECK(xxwidgets_app_poll(app, -1) == XXWIDGETS_INVALID_ARGUMENT);
    event_widget = button;
    CHECK(xxwidgets_app_run(app) == 42 && events == 1);
    event_widget = NULL;
    CHECK(xxwidgets_widget_destroy(window) == XXWIDGETS_OK && !app->widgets);
    CHECK(xxwidgets_app_destroy(app) == XXWIDGETS_OK);
    CHECK(xxwidgets_app_destroy(other) == XXWIDGETS_OK);
    CHECK(creates == destroys);
    CHECK(shutdowns == 3);
    CHECK(xxwidgets_app_create(NULL, &app) == XXWIDGETS_OK);
    window = make(app, NULL, XXWIDGETS_WINDOW, "close");
    xxwidgets_emit(window, XXWIDGETS_EVENT_CLOSE, 0);
    CHECK(xxwidgets_app_run(app) == 0 && app->quit);
    CHECK(xxwidgets_app_destroy(app) == XXWIDGETS_OK);
    puts("xxwidgets core tests passed");
    return 0;
}
