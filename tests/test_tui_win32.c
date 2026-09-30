/* Exercise the real console backend even when CTest redirects standard handles. */
#include "xxwidgets/xxwidgets.h"
#include "xxwidgets/xxwidgets_combobox.h"
#include "xxwidgets/xxwidgets_process.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "line %d: %s (WinAPI error %lu)\n", __LINE__, #expr, (unsigned long)GetLastError()); \
    exit(1); \
} } while (0)

static HANDLE inherited_input, inherited_output, inherited_error;
static HANDLE console_input = INVALID_HANDLE_VALUE;
static HANDLE console_output = INVALID_HANDLE_VALUE;
static HANDLE private_output = INVALID_HANDLE_VALUE;
static DWORD original_input_mode;
static int saved_handles, allocated_console, saved_input_mode;
static xxwidgets_app *test_app;

typedef struct test_events {
    xxwidgets_widget *label, *progress, *edit, *checkbox, *list, *hexview, *archive, *browser;
    int clicks, edit_changes, checkbox_changes, selections, closes;
    int hex_selections;
    int archive_selections;
    int browser_changes, browser_activations;
    int browser_contexts, context_row, context_x, context_y;
    size_t browser_source;
    size_t archive_index;
    size_t hex_offset, hex_length;
    int callback_error;
    int shortcuts, shortcut_action;
} test_events;

static void cleanup(void)
{
    if (test_app) {
        xxwidgets_app_destroy(test_app);
        test_app = NULL;
    }
    if (saved_input_mode && console_input != INVALID_HANDLE_VALUE)
        SetConsoleMode(console_input, original_input_mode);
    if (console_output != INVALID_HANDLE_VALUE)
        SetConsoleActiveScreenBuffer(console_output);
    if (private_output != INVALID_HANDLE_VALUE) CloseHandle(private_output);
    if (console_input != INVALID_HANDLE_VALUE) CloseHandle(console_input);
    if (console_output != INVALID_HANDLE_VALUE) CloseHandle(console_output);
    private_output = console_input = console_output = INVALID_HANDLE_VALUE;
    saved_input_mode = 0;
    if (allocated_console) { FreeConsole(); allocated_console = 0; }
    if (saved_handles) {
        SetStdHandle(STD_INPUT_HANDLE, inherited_input);
        SetStdHandle(STD_OUTPUT_HANDLE, inherited_output);
        SetStdHandle(STD_ERROR_HANDLE, inherited_error);
        saved_handles = 0;
    }
}

static HANDLE open_console(const WCHAR *name)
{
    return CreateFileW(name, GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
}

static void setup_console(void)
{
    HWND window;
    COORD size = {80, 25};
    SMALL_RECT viewport = {0, 0, 79, 24};
    CONSOLE_SCREEN_BUFFER_INFO info;
    inherited_input = GetStdHandle(STD_INPUT_HANDLE);
    inherited_output = GetStdHandle(STD_OUTPUT_HANDLE);
    inherited_error = GetStdHandle(STD_ERROR_HANDLE);
    saved_handles = 1;
    CHECK(atexit(cleanup) == 0);
    /* Detach this test process from a parent console before creating its own. */
    if (!AllocConsole()) {
        CHECK(GetLastError() == ERROR_ACCESS_DENIED);
        CHECK(FreeConsole());
        CHECK(AllocConsole());
    }
    allocated_console = 1;
    window = GetConsoleWindow();
    if (window) ShowWindow(window, SW_HIDE);
    console_input = open_console(L"CONIN$");
    console_output = open_console(L"CONOUT$");
    CHECK(console_input != INVALID_HANDLE_VALUE && console_output != INVALID_HANDLE_VALUE);
    CHECK(GetConsoleMode(console_input, &original_input_mode));
    saved_input_mode = 1;
    private_output = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
    CHECK(private_output != INVALID_HANDLE_VALUE);
    CHECK(GetConsoleScreenBufferInfo(private_output, &info));
    /* Grow first when necessary; shrink the viewport before shrinking its buffer. */
    if (info.dwSize.X < size.X || info.dwSize.Y < size.Y) {
        COORD grown = {info.dwSize.X > size.X ? info.dwSize.X : size.X,
                       info.dwSize.Y > size.Y ? info.dwSize.Y : size.Y};
        CHECK(SetConsoleScreenBufferSize(private_output, grown));
    }
    CHECK(SetConsoleWindowInfo(private_output, TRUE, &viewport));
    CHECK(SetConsoleScreenBufferSize(private_output, size));
    CHECK(SetConsoleActiveScreenBuffer(private_output));
    CHECK(SetStdHandle(STD_INPUT_HANDLE, console_input));
    CHECK(SetStdHandle(STD_OUTPUT_HANDLE, private_output));
    CHECK(SetStdHandle(STD_ERROR_HANDLE, private_output));
    CHECK(FlushConsoleInputBuffer(console_input));
}

static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user_data)
{
    test_events *events = (test_events *)user_data;
    if (event->type == XXWIDGETS_EVENT_SHORTCUT) {
        ++events->shortcuts; events->shortcut_action = event->value;
    } else if (event->type == XXWIDGETS_EVENT_CLICK) {
        ++events->clicks;
        if (xxwidgets_widget_set_text(events->label, "Applied") != XXWIDGETS_OK ||
            xxwidgets_widget_set_value(events->progress, 65) != XXWIDGETS_OK ||
            xxwidgets_app_poll(app, 0) != XXWIDGETS_BUSY ||
            xxwidgets_widget_destroy(events->edit) != XXWIDGETS_BUSY)
            events->callback_error = 1;
    } else if (event->type == XXWIDGETS_EVENT_CHANGE && event->widget == events->edit) {
        ++events->edit_changes;
        if (xxwidgets_widget_set_text(events->label, "Edited") != XXWIDGETS_OK)
            events->callback_error = 1;
    } else if (event->type == XXWIDGETS_EVENT_CHANGE && event->widget == events->checkbox) {
        ++events->checkbox_changes;
        if (event->value != 1) events->callback_error = 1;
    } else if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == events->list) {
        ++events->selections;
    } else if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == events->hexview) {
        int value;
        ++events->hex_selections;
        if (xxwidgets_widget_get_value(event->widget, &value) != XXWIDGETS_OK ||
            value != event->value || xxwidgets_hexview_get_selection(event->widget,
                &events->hex_offset, &events->hex_length) != XXWIDGETS_OK)
            events->callback_error = 1;
    } else if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == events->archive) {
        xxwidgets_archive_entry entry;
        ++events->archive_selections;
        if (xxwidgets_archiveview_get_selection(event->widget, &events->archive_index, &entry) != XXWIDGETS_OK ||
            events->archive_index != (size_t)event->value || !entry.path)
            events->callback_error = 1;
    } else if (event->type == XXWIDGETS_EVENT_CHANGE && event->widget == events->browser) {
        ++events->browser_changes;
    } else if (event->type == XXWIDGETS_EVENT_ACTIVATE && event->widget == events->browser) {
        xxwidgets_archive_browser_entry entry;
        int value;
        ++events->browser_activations;
        if (xxwidgets_archivebrowser_get_selection(event->widget, &events->browser_source, &entry) != XXWIDGETS_OK ||
            !entry.path || entry.is_directory ||
            xxwidgets_widget_get_value(event->widget, &value) != XXWIDGETS_OK || value != event->value)
            events->callback_error = 1;
    } else if (event->type == XXWIDGETS_EVENT_CONTEXT_MENU && event->widget == events->browser) {
        int value;
        ++events->browser_contexts;
        events->context_row = event->value;
        events->context_x = event->x;
        events->context_y = event->y;
        if (xxwidgets_widget_get_value(event->widget, &value) != XXWIDGETS_OK || value != event->value)
            events->callback_error = 1;
    } else if (event->type == XXWIDGETS_EVENT_CLOSE) {
        ++events->closes;
        xxwidgets_app_quit(app, 23);
    }
}

static xxwidgets_widget *make(xxwidgets_widget *parent, xxwidgets_kind kind,
                               const char *text, int x, int y, int width, int height)
{
    xxwidgets_widget *widget = NULL;
    xxwidgets_rect rect = {x, y, width, height};
    CHECK(xxwidgets_widget_create(test_app, parent, kind, text, rect, &widget) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_native_handle(widget) == NULL);
    return widget;
}

static void key(WORD virtual_key, WCHAR character, DWORD modifiers)
{
    INPUT_RECORD record;
    DWORD written, remaining;
    unsigned int attempts;
    memset(&record, 0, sizeof(record));
    record.EventType = KEY_EVENT;
    record.Event.KeyEvent.bKeyDown = TRUE;
    record.Event.KeyEvent.wRepeatCount = 1;
    record.Event.KeyEvent.wVirtualKeyCode = virtual_key;
    record.Event.KeyEvent.uChar.UnicodeChar = character;
    record.Event.KeyEvent.dwControlKeyState = modifiers;
    CHECK(WriteConsoleInputW(GetStdHandle(STD_INPUT_HANDLE), &record, 1, &written) && written == 1);
    /* All polls are nonblocking and bounded; a broken event loop cannot hang CTest. */
    for (attempts = 0; attempts < 16; ++attempts) {
        CHECK(xxwidgets_app_poll(test_app, 0) == XXWIDGETS_OK);
        CHECK(GetNumberOfConsoleInputEvents(console_input, &remaining));
        if (!remaining) return;
    }
    CHECK(remaining == 0);
}

static void text_is(xxwidgets_widget *widget, const char *expected)
{
    char buffer[64];
    CHECK(xxwidgets_widget_get_text(widget, buffer, sizeof(buffer), NULL) == XXWIDGETS_OK);
    CHECK(strcmp(buffer, expected) == 0);
}

static void repeated_characters(xxwidgets_widget *edit)
{
    INPUT_RECORD records[2];
    DWORD written;
    size_t required = 0, i;
    unsigned int attempts;
    char *text;
    memset(records, 0, sizeof(records));
    records[0].EventType = KEY_EVENT;
    records[0].Event.KeyEvent.bKeyDown = TRUE;
    records[0].Event.KeyEvent.wRepeatCount = 257;
    records[0].Event.KeyEvent.uChar.UnicodeChar = L'x';
    records[1].EventType = KEY_EVENT;
    records[1].Event.KeyEvent.bKeyDown = TRUE;
    records[1].Event.KeyEvent.wRepeatCount = 1;
    records[1].Event.KeyEvent.uChar.UnicodeChar = L'!';
    CHECK(xxwidgets_widget_focus(edit) == XXWIDGETS_OK);
    CHECK(WriteConsoleInputW(GetStdHandle(STD_INPUT_HANDLE), records, 2, &written) && written == 2);
    for (attempts = 0; attempts < 16; ++attempts) {
        CHECK(xxwidgets_app_poll(test_app, 0) == XXWIDGETS_OK);
        CHECK(xxwidgets_widget_get_text(edit, NULL, 0, &required) == XXWIDGETS_OK);
        CHECK(required <= 260);
        if (required == 260) break; /* "A", 257 repeats, the following "!", and NUL. */
    }
    CHECK(required == 260);
    text = (char *)malloc(required);
    CHECK(text != NULL);
    CHECK(xxwidgets_widget_get_text(edit, text, required, NULL) == XXWIDGETS_OK);
    CHECK(text[0] == 'A' && text[258] == '!' && text[259] == '\0');
    for (i = 1; i <= 257; ++i) CHECK(text[i] == 'x');
    free(text);
}

static void rendered(int x, int y, const WCHAR *expected)
{
    HANDLE active = open_console(L"CONOUT$");
    CONSOLE_SCREEN_BUFFER_INFO info;
    WCHAR buffer[128];
    COORD position;
    DWORD read_count;
    DWORD length = (DWORD)wcslen(expected);
    CHECK(active != INVALID_HANDLE_VALUE);
    CHECK(length < sizeof(buffer) / sizeof(buffer[0]));
    CHECK(GetConsoleScreenBufferInfo(active, &info));
    position.X = (SHORT)(info.srWindow.Left + x);
    position.Y = (SHORT)(info.srWindow.Top + y);
    CHECK(ReadConsoleOutputCharacterW(active, buffer, length, position, &read_count));
    CHECK(CloseHandle(active));
    CHECK(read_count == length && wmemcmp(buffer, expected, length) == 0);
}

static void hexview_checks(xxwidgets_widget *window, test_events *events)
{
    unsigned char data[65];
    size_t i, offset, length;
    int value, selections;
    for (i = 0; i < sizeof(data); ++i) data[i] = (unsigned char)('A' + i % 26);
    data[0] = 0; data[1] = 0x7f; data[2] = ' '; data[3] = '~';
    data[4] = 0x80; data[5] = 'A'; data[6] = 'B'; data[7] = 'C';
    events->hexview = make(window, XXWIDGETS_HEXVIEW, "Bytes", 1, 13, 57, 2);
    CHECK(xxwidgets_hexview_set_layout(events->hexview, UINT64_C(0x100000000), 8) == XXWIDGETS_OK);
    CHECK(xxwidgets_hexview_set_data(events->hexview, data, sizeof(data)) == XXWIDGETS_OK);
    CHECK(xxwidgets_hexview_size(events->hexview) == sizeof(data));
    CHECK(xxwidgets_widget_focus(events->hexview) == XXWIDGETS_OK);
    CHECK(xxwidgets_hexview_get_selection(events->hexview, &offset, &length) == XXWIDGETS_OK);
    CHECK(offset == 0 && length == 8 && events->hex_selections == 0);
    rendered(2, 14, L">0000000100000000  00 7F 20 7E 80 41 42 43  |.. ~.ABC|");
    key(VK_DOWN, 0, 0);
    CHECK(events->hex_selections == 1 && events->hex_offset == 8 && events->hex_length == 8);
    key(VK_NEXT, 0, 0);
    CHECK(events->hex_selections == 2 && events->hex_offset == 24);
    key(VK_PRIOR, 0, 0);
    CHECK(events->hex_selections == 3 && events->hex_offset == 8);
    key(VK_HOME, 0, 0);
    CHECK(events->hex_selections == 4 && events->hex_offset == 0);
    key(VK_END, 0, 0);
    CHECK(events->hex_selections == 5 && events->hex_offset == 64 && events->hex_length == 1);
    CHECK(xxwidgets_widget_get_value(events->hexview, &value) == XXWIDGETS_OK && value == 8);
    rendered(2, 15, L">0000000100000040  4D ");
    rendered(46, 15, L"|M       |");

    /* Wider layouts preserve the selected byte range. Right/Left reveal the
     * complete ASCII field in a view narrower than the formatted row. */
    selections = events->hex_selections;
    CHECK(xxwidgets_hexview_set_layout(events->hexview, UINT64_C(0x100000000), 16) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_get_value(events->hexview, &value) == XXWIDGETS_OK && value == 4);
    CHECK(xxwidgets_hexview_get_selection(events->hexview, &offset, &length) == XXWIDGETS_OK);
    CHECK(offset == 64 && length == 1 && events->hex_selections == selections);
    for (i = 0; i < 40; ++i) key(VK_RIGHT, 0, 0);
    rendered(41, 15, L"|M               |");
    key(VK_LEFT, 0, 0);
    rendered(42, 15, L"|M");
    CHECK(events->hex_selections == selections);

    /* Data replacement with unchanged row count refreshes text and resets
     * both selection and horizontal scroll, without emitting input events. */
    memset(data, 'Z', sizeof(data));
    CHECK(xxwidgets_hexview_set_data(events->hexview, data, sizeof(data)) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_get_value(events->hexview, &value) == XXWIDGETS_OK && value == 0);
    rendered(2, 14, L">0000000100000000  5A 5A ");
    CHECK(events->hex_selections == selections);
    for (i = 0; i < 40; ++i) key(VK_RIGHT, 0, 0);
    rendered(41, 14, L"|ZZZZZZZZZZZZZZZZ|");
    key(VK_END, 0, 0);
    CHECK(events->hex_offset == 64 && events->hex_length == 1);
    rendered(41, 15, L"|Z               |");
    selections = events->hex_selections;
    CHECK(xxwidgets_hexview_set_data(events->hexview, NULL, 0) == XXWIDGETS_OK);
    CHECK(xxwidgets_hexview_get_selection(events->hexview, &offset, &length) == XXWIDGETS_OK);
    CHECK(offset == SIZE_MAX && length == 0 && events->hex_selections == selections);
    rendered(2, 14, L"        ");
    CHECK(!events->callback_error);
}

static void archiveview_checks(xxwidgets_widget *window, test_events *events)
{
    xxwidgets_archive_entry entries[] = {
        {"folder/", 0, 1}, {"\xce\xbb\xc3\xa9-abcdefghijklmnopqrstuv", UINT64_MAX, 0},
        {"last.txt", 7, 0}
    }, entry;
    size_t index, i;
    int value;
    xxwidgets_rect narrow = {1, 13, 20, 2}, wide = {1, 13, 40, 2};
    events->archive = make(window, XXWIDGETS_ARCHIVEVIEW, "Archive", 1, 13, 40, 2);
    CHECK(xxwidgets_archiveview_set_entries(events->archive, entries, 3) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_focus(events->archive) == XXWIDGETS_OK);
    rendered(2, 14, L"><DIR>");
    rendered(31, 14, L"folder/");
    CHECK(events->archive_selections == 0);
    key(VK_DOWN, 0, 0);
    CHECK(events->archive_selections == 1 && events->archive_index == 1);
    rendered(31, 15, L"\x03bb\x00e9-abcdefgh");
    /* Pan past the multibyte path characters: offsets must be Unicode scalars,
     * and the longest row can occur after the first entry. */
    CHECK(xxwidgets_widget_set_rect(events->archive, narrow) == XXWIDGETS_OK);
    for (i = 0; i < 31; ++i) key(VK_RIGHT, 0, 0);
    rendered(3, 15, L"abcdefghijklmno");
    CHECK(events->archive_selections == 1);
    key(VK_LEFT, 0, 0);
    rendered(3, 15, L"-abcdefghijklmn");
    key(VK_END, 0, 0);
    CHECK(events->archive_selections == 2 && events->archive_index == 2);
    key(VK_HOME, 0, 0);
    CHECK(events->archive_selections == 3 && events->archive_index == 0);
    CHECK(xxwidgets_widget_set_rect(events->archive, wide) == XXWIDGETS_OK);
    entries[0].path = "updated/";
    CHECK(xxwidgets_archiveview_set_entries(events->archive, entries, 3) == XXWIDGETS_OK);
    rendered(2, 14, L"><DIR>");
    rendered(31, 14, L"updated/");
    CHECK(events->archive_selections == 3);
    CHECK(xxwidgets_widget_get_value(events->archive, &value) == XXWIDGETS_OK && value == 0);
    CHECK(xxwidgets_archiveview_clear(events->archive) == XXWIDGETS_OK);
    CHECK(xxwidgets_archiveview_get_selection(events->archive, &index, &entry) == XXWIDGETS_OK);
    CHECK(index == SIZE_MAX && !entry.path);
    rendered(2, 14, L"        ");
    CHECK(!events->callback_error);
}

static void archivebrowser_checks(xxwidgets_widget *window, test_events *events)
{
    xxwidgets_archive_browser_entry entries[] = {
        {"docs/nested/readme.txt", 12, 6, 0, 3, "2026-09-28 10:00", "A"},
        {"docs/notes.txt", 3, 2, 0, 3, NULL, NULL},
        {"z.bin", UINT64_MAX, 0, 0, XXWIDGETS_ARCHIVE_SIZE_KNOWN, NULL, "A"},
        {"a.bin", 4, 2, 0, 3, NULL, NULL}
    }, entry;
    size_t source, i;
    int value;
    xxwidgets_rect narrow = {1, 1, 40, 13};
    events->browser = make(window, XXWIDGETS_ARCHIVEBROWSER, "Browser", 1, 1, 56, 13);
    CHECK(xxwidgets_archivebrowser_set_archive(events->browser, "sample.zip") == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_set_entries(events->browser, entries, 4) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_focus(events->browser) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_visible_count(events->browser) == 3);
    CHECK(xxwidgets_archivebrowser_count(events->browser) == 4);
    rendered(2, 2, L"[^] sample.zip:/");
    rendered(7, 3, L"Name");
    rendered(3, 4, L"[D] docs");
    CHECK(events->browser_changes == 0 && events->browser_activations == 0);
    key(VK_APPS, 0, 0);
    key(VK_F10, 0, SHIFT_PRESSED);
    CHECK(events->browser_contexts == 2 && events->context_row == 0 &&
        events->context_x == 1 && events->context_y == 2 && !events->callback_error);
    CHECK(xxwidgets_widget_set_enabled(events->browser, 0) == XXWIDGETS_OK);
    key(VK_APPS, 0, 0);
    key(VK_F10, 0, SHIFT_PRESSED);
    CHECK(events->browser_contexts == 2);
    CHECK(xxwidgets_widget_set_enabled(events->browser, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_focus(events->browser) == XXWIDGETS_OK);
    /* Neither directory needs an explicit archive member. Enter follows the
     * inferred hierarchy, while file activation retains the original index. */
    key(VK_RETURN, L'\r', 0);
    CHECK(strcmp(xxwidgets_archivebrowser_directory(events->browser), "docs/") == 0);
    CHECK(xxwidgets_archivebrowser_visible_count(events->browser) == 2);
    CHECK(events->browser_changes == 1);
    rendered(2, 2, L"[^] sample.zip:/docs/");
    rendered(3, 4, L"[D] nested");
    key(VK_RETURN, L'\r', 0);
    CHECK(strcmp(xxwidgets_archivebrowser_directory(events->browser), "docs/nested/") == 0);
    CHECK(events->browser_changes == 2);
    CHECK(xxwidgets_archivebrowser_visible_count(events->browser) == 1);
    key(VK_RETURN, L'\r', 0);
    CHECK(events->browser_activations == 1 && events->browser_source == 0 && !events->callback_error);
    key(VK_BACK, L'\b', 0);
    CHECK(strcmp(xxwidgets_archivebrowser_directory(events->browser), "docs/") == 0);
    key(VK_BACK, L'\b', 0);
    CHECK(strcmp(xxwidgets_archivebrowser_directory(events->browser), "") == 0);
    CHECK(events->browser_changes == 4);
    /* Size sorting is numeric across the whole uint64_t range; directories
     * remain first in both directions. Repeating a column toggles direction. */
    key('2', L'2', 0);
    CHECK(xxwidgets_archivebrowser_get_entry(events->browser, 1, &source, &entry) == XXWIDGETS_OK);
    CHECK(source == 3 && entry.size == 4);
    key('2', L'2', 0);
    CHECK(xxwidgets_archivebrowser_get_entry(events->browser, 0, &source, &entry) == XXWIDGETS_OK);
    CHECK(source == SIZE_MAX && entry.is_directory);
    CHECK(xxwidgets_archivebrowser_get_entry(events->browser, 1, &source, &entry) == XXWIDGETS_OK);
    CHECK(source == 2 && entry.size == UINT64_MAX);
    CHECK(xxwidgets_widget_set_rect(events->browser, narrow) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_set_value(events->browser, 1) == XXWIDGETS_OK);
    for (i = 0; i < 21; ++i) key(VK_RIGHT, 0, 0);
    rendered(36, 3, L"Size");
    rendered(20, 5, L"18446744073709551615");
    key(VK_APPS, 0, 0);
    CHECK(events->browser_contexts == 3 && events->context_row == 1 && events->context_y == 3);
    CHECK(xxwidgets_archivebrowser_set_entries(events->browser, NULL, 0) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_visible_count(events->browser) == 0);
    key(VK_RETURN, L'\r', 0);
    CHECK(xxwidgets_widget_get_value(events->browser, &value) == XXWIDGETS_OK && value == -1);
    key(VK_F10, 0, SHIFT_PRESSED);
    CHECK(events->browser_contexts == 4 && events->context_row == -1 && events->context_y == 2);
    CHECK(events->browser_activations == 1 && !events->callback_error);
    rendered(2, 4, L"        ");
}

static void combo_checks(xxwidgets_widget *window, test_events *events)
{
    xxwidgets_widget *combo = make(window, XXWIDGETS_COMBOBOX, "Choose", 1, 1, 24, 1);
    xxwidgets_widget *check = make(window, XXWIDGETS_CHECKCOMBOBOX, "Choose several", 1, 3, 24, 1);
    xx_str_w_s labels[] = {{L"First", 5, 6, true}, {L"Second \x03bb", 8, 9, true}};
    xx_meta_string records[2] = {0};
    const xx_meta_string *checked[2];
    xx_var current = {0};
    int i, closes_before = events->closes;
    size_t count;
    for (i = 0; i < 2; ++i) { records[i].meta_string = labels + i; records[i].var.type = XX_VAR_TYPE_UINT64; records[i].var.val.u64 = (uint64_t)i + 7; }
    CHECK(xxwidgets_combobox_set_records(combo, records, 2) == XXWIDGETS_OK);
    CHECK(xxwidgets_combobox_set_records(check, records, 2) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_focus(combo) == XXWIDGETS_OK);
    key(VK_RETURN, L'\r', 0); key(VK_DOWN, 0, 0); key(VK_RETURN, L'\r', 0);
    CHECK(xxwidgets_combobox_get_current(combo, &current) == XXWIDGETS_OK && current.val.u64 == 8);
    CHECK(xxwidgets_widget_focus(check) == XXWIDGETS_OK);
    key(VK_RETURN, L'\r', 0); key(VK_SPACE, ' ', 0); key(VK_DOWN, 0, 0); key(VK_SPACE, ' ', 0);
    CHECK(xxwidgets_checkcombobox_get_checked(check, checked, 2, &count) == XXWIDGETS_OK && count == 2);
    CHECK(checked[0]->var.val.u64 == 7 && checked[1]->var.val.u64 == 8);
    key(VK_ESCAPE, 27, 0); CHECK(events->closes == closes_before);
    CHECK(xxwidgets_widget_destroy(combo) == XXWIDGETS_OK && xxwidgets_widget_destroy(check) == XXWIDGETS_OK);
}

static void about_checks(xxwidgets_widget *window, test_events *events)
{
    xxwidgets_about_dialog *about = NULL;
    int mode, before_clicks = events->clicks, before_closes = events->closes;
    CHECK(xxwidgets_about_dialog_create(&about) == XXWIDGETS_OK);
    CHECK(xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_PROGRAM_NAME, "Terminal \xce\xbb") == XXWIDGETS_OK);
    CHECK(xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_DESCRIPTION, "Scrollable terminal content") == XXWIDGETS_OK);
    for (mode = 0; mode < 2; ++mode) {
        INPUT_RECORD records[2];
        DWORD written, count = mode ? 2 : 1;
        memset(records, 0, sizeof(records));
        records[0].EventType = KEY_EVENT;
        records[0].Event.KeyEvent.bKeyDown = TRUE;
        records[0].Event.KeyEvent.wRepeatCount = 1;
        records[0].Event.KeyEvent.wVirtualKeyCode = mode ? VK_TAB : VK_ESCAPE;
        records[1] = records[0]; records[1].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
        CHECK(WriteConsoleInputW(console_input, records, count, &written) && written == count);
        CHECK(xxwidgets_about_dialog_show(about, window) == XXWIDGETS_OK);
        CHECK(events->clicks == before_clicks && events->closes == before_closes);
    }
    CHECK(xxwidgets_about_dialog_destroy(about) == XXWIDGETS_OK);
}

typedef struct process_test {
    uint64_t started;
    int phase, stopped;
} process_test;

static int process_visible(void)
{
    HANDLE active = open_console(L"CONOUT$");
    CONSOLE_SCREEN_BUFFER_INFO info;
    WCHAR title[12];
    DWORD count;
    COORD position;
    int visible;
    CHECK(active != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(active, &info));
    position.X = (SHORT)(info.srWindow.Left + 2);
    position.Y = info.srWindow.Top;
    CHECK(ReadConsoleOutputCharacterW(active, title, 12, position, &count));
    visible = count == 12 && wmemcmp(title, L"Process test", 12) == 0;
    CHECK(CloseHandle(active));
    return visible;
}

static xxwidgets_status process_update(void *user, int stop, xx_pd_struct *progress, int *finished)
{
    process_test *test = (process_test *)user;
    uint64_t elapsed = (uint64_t)GetTickCount64() - test->started;
    int i;
    CHECK(elapsed < 5000);
    if (stop) { CHECK(progress->is_stop); test->stopped = 1; *finished = 1; return XXWIDGETS_OK; }
    /* The update callback runs before delayed creation. A busy host can reach
     * the delay on a callback that still precedes the first rendered frame. */
    if (elapsed > 1100 && process_visible()) {
        rendered(2, 0, L"Process test");
        rendered(3, 2, L"[50/100] Terminal record");
        if (!test->phase) {
            rendered(30, 3, L" 50% ");
            rendered(3, 14, L"[50/100] Terminal record");
            ++test->phase;
        } else {
            INPUT_RECORD input = {0};
            DWORD written;
            /* Slots 1 and 3 hide; slot 4 moves from row 14 to row 8. */
            rendered(3, 8, L"[50/100] Terminal record");
            rendered(3, 11, L"Elapsed:");
            input.EventType = KEY_EVENT; input.Event.KeyEvent.bKeyDown = TRUE;
            input.Event.KeyEvent.wRepeatCount = 1; input.Event.KeyEvent.wVirtualKeyCode = VK_ESCAPE;
            CHECK(WriteConsoleInputW(console_input, &input, 1, &written) && written == 1);
            ++test->phase;
        }
    }
    for (i = 0; i < XX_PD_LEVELS; ++i) {
        progress->records[i].is_busy = !test->phase || i % 2 == 0;
        progress->records[i].current = 50; progress->records[i].total = 100;
        memcpy(progress->records[i].status, "Terminal record", sizeof("Terminal record"));
    }
    return XXWIDGETS_OK;
}

static void process_checks(xxwidgets_widget *window, test_events *events)
{
    process_test test = {0};
    xx_pd_struct progress = {0};
    int before_clicks = events->clicks, before_closes = events->closes;
    CHECK(FlushConsoleInputBuffer(console_input));
    test.started = (uint64_t)GetTickCount64();
    CHECK(xxwidgets_process_dialog(window, "Process test", &progress, process_update, &test) == XXWIDGETS_OK);
    CHECK(test.phase >= 2 && test.stopped && progress.is_stop);
    CHECK(events->clicks == before_clicks && events->closes == before_closes);
    CHECK(xxwidgets_app_poll(test_app, 0) == XXWIDGETS_OK);
    rendered(2, 0, L"Console test");
}

int main(void)
{
    test_events events;
    xxwidgets_config config = {XXWIDGETS_BACKEND_TUI, on_event, &events};
    xxwidgets_app *other = NULL;
    xxwidgets_widget *window, *button;
    DWORD baseline_mode, mode, output_mode, restored_output_mode, written;
    COORD marker_position = {0, 0};
    int value;
    memset(&events, 0, sizeof(events));
    setup_console();
    baseline_mode = (original_input_mode | ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT |
        ENABLE_ECHO_INPUT | ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT) & ~ENABLE_QUICK_EDIT_MODE;
    CHECK(SetConsoleMode(console_input, baseline_mode));
    CHECK(GetConsoleMode(private_output, &output_mode));
    CHECK(WriteConsoleOutputCharacterW(private_output, L"original buffer marker", 22, marker_position, &written));
    CHECK(written == 22);

    /* Failed initialization must leave the console untouched and release ownership. */
    CHECK(SetStdHandle(STD_INPUT_HANDLE, INVALID_HANDLE_VALUE));
    CHECK(xxwidgets_app_create(&config, &other) == XXWIDGETS_UNAVAILABLE && other == NULL);
    CHECK(SetStdHandle(STD_INPUT_HANDLE, console_input));
    CHECK(GetConsoleMode(console_input, &mode) && mode == baseline_mode);
    CHECK(SetStdHandle(STD_OUTPUT_HANDLE, INVALID_HANDLE_VALUE));
    CHECK(xxwidgets_app_create(&config, &other) == XXWIDGETS_UNAVAILABLE && other == NULL);
    CHECK(SetStdHandle(STD_OUTPUT_HANDLE, private_output));
    rendered(0, 0, L"original buffer marker");

    CHECK(xxwidgets_app_create(&config, &test_app) == XXWIDGETS_OK);
    CHECK(xxwidgets_app_backend(test_app) == XXWIDGETS_BACKEND_TUI);
    CHECK(GetConsoleMode(console_input, &mode));
    CHECK(mode == (ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS));
    CHECK(xxwidgets_app_create(&config, &other) == XXWIDGETS_BUSY && other == NULL);
    window = make(NULL, XXWIDGETS_WINDOW, "Console test", 0, 0, 60, 16);
    events.label = make(window, XXWIDGETS_LABEL, "Ready \xce\xbb", 1, 1, 35, 1);
    button = make(window, XXWIDGETS_BUTTON, "Apply", 1, 5, 12, 1);
    events.edit = make(window, XXWIDGETS_EDIT, "A", 1, 3, 30, 1);
    events.checkbox = make(window, XXWIDGETS_CHECKBOX, "Check", 18, 5, 20, 1);
    events.list = make(window, XXWIDGETS_LISTBOX, "", 1, 7, 25, 3);
    events.progress = make(window, XXWIDGETS_PROGRESS, "", 1, 11, 30, 1);
    CHECK(xxwidgets_listbox_add(events.list, "one") == XXWIDGETS_OK);
    CHECK(xxwidgets_listbox_add(events.list, "two") == XXWIDGETS_OK);
    CHECK(xxwidgets_listbox_add(events.list, "three") == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_set_value(events.progress, 10) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_focus(button) == XXWIDGETS_OK);
    CHECK(FlushConsoleInputBuffer(console_input));
    CHECK(xxwidgets_app_poll(test_app, 0) == XXWIDGETS_OK);
    rendered(2, 2, L"Ready \x03bb");
    rendered(2, 6, L"[Apply");
    CHECK(events.clicks == 0 && events.edit_changes == 0 && events.selections == 0);

    key(VK_TAB, L'\t', 0);                 /* Button -> edit. */
    key(0, 0x03bb, 0);
    text_is(events.edit, "A\xce\xbb");
    rendered(2, 4, L"[A\x03bb");
    key(0, 0xd83d, 0);                     /* U+1F642 surrogate pair. */
    text_is(events.edit, "A\xce\xbb");
    key(0, 0xde42, 0);
    text_is(events.edit, "A\xce\xbb\xf0\x9f\x99\x82");
    key(VK_BACK, L'\b', 0);                /* Delete the whole supplementary scalar. */
    text_is(events.edit, "A\xce\xbb");
    key(VK_BACK, L'\b', 0);                /* Delete the whole two-byte scalar. */
    text_is(events.edit, "A");
    CHECK(events.edit_changes == 4);
    key(VK_TAB, L'\t', SHIFT_PRESSED);      /* Edit -> button. */
    key(VK_RETURN, L'\r', 0);
    CHECK(events.clicks == 1 && !events.callback_error);
    text_is(events.label, "Applied");
    CHECK(xxwidgets_widget_get_value(events.progress, &value) == XXWIDGETS_OK && value == 65);
    rendered(14, 12, L" 65% ");
    key(VK_TAB, L'\t', 0);                 /* Button -> edit -> checkbox. */
    key(VK_TAB, L'\t', 0);
    key(VK_SPACE, L' ', 0);
    CHECK(events.checkbox_changes == 1 && !events.callback_error);
    CHECK(xxwidgets_widget_get_value(events.checkbox, &value) == XXWIDGETS_OK && value == 1);
    rendered(19, 6, L"[x] Check");
    key(VK_TAB, L'\t', 0);                 /* Checkbox -> list. */
    key(VK_DOWN, 0, 0);
    key(VK_DOWN, 0, 0);
    CHECK(xxwidgets_widget_get_value(events.list, &value) == XXWIDGETS_OK && value == 1);
    key(VK_RETURN, L'\r', 0);              /* Activate the selected item. */
    CHECK(events.selections == 3);
    rendered(2, 9, L">two");
    key(VK_NEXT, 0, 0);
    CHECK(xxwidgets_widget_get_value(events.list, &value) == XXWIDGETS_OK && value == 2);
    key(VK_PRIOR, 0, 0);
    CHECK(xxwidgets_widget_get_value(events.list, &value) == XXWIDGETS_OK && value == 0);
    CHECK(events.selections == 5);
    repeated_characters(events.edit);
    CHECK(events.edit_changes == 262 && !events.callback_error);
    hexview_checks(window, &events);
    CHECK(xxwidgets_widget_set_visible(events.hexview, 0) == XXWIDGETS_OK);
    archiveview_checks(window, &events);
    CHECK(xxwidgets_widget_set_visible(events.archive, 0) == XXWIDGETS_OK);
    archivebrowser_checks(window, &events);
    about_checks(window, &events);
    process_checks(window, &events);
    {
        const xxwidgets_shortcut shortcuts[] = {{"Ctrl+Shift+O", 101}, {"F8", 102}};
        CHECK(xxwidgets_widget_focus(events.edit) == XXWIDGETS_OK);
        CHECK(xxwidgets_window_set_shortcuts(window, shortcuts, 2) == XXWIDGETS_OK);
        key('O', 15, LEFT_CTRL_PRESSED | SHIFT_PRESSED);
        CHECK(events.shortcuts == 1 && events.shortcut_action == 101);
        key(VK_F8, 0, 0);
        CHECK(events.shortcuts == 2 && events.shortcut_action == 102);
        CHECK(xxwidgets_window_set_shortcuts(window, NULL, 0) == XXWIDGETS_OK);
        key(VK_F8, 0, 0);
        CHECK(events.shortcuts == 2);
    }
    combo_checks(window, &events);
    key(VK_ESCAPE, 27, 0);
    CHECK(events.closes == 1 && !events.callback_error);
    /* CLOSE was processed by bounded polling, so run cannot block here. */
    CHECK(xxwidgets_app_run(test_app) == 23);
    CHECK(xxwidgets_app_destroy(test_app) == XXWIDGETS_OK);
    test_app = NULL;
    CHECK(GetConsoleMode(console_input, &mode) && mode == baseline_mode);
    CHECK(GetConsoleMode(private_output, &restored_output_mode) && restored_output_mode == output_mode);
    rendered(0, 0, L"original buffer marker");
    CHECK(xxwidgets_app_create(&config, &test_app) == XXWIDGETS_OK);
    CHECK(xxwidgets_app_destroy(test_app) == XXWIDGETS_OK);
    test_app = NULL;
    CHECK(GetConsoleMode(console_input, &mode) && mode == baseline_mode);
    rendered(0, 0, L"original buffer marker");
    cleanup();
    CHECK(GetStdHandle(STD_INPUT_HANDLE) == inherited_input);
    CHECK(GetStdHandle(STD_OUTPUT_HANDLE) == inherited_output);
    CHECK(GetStdHandle(STD_ERROR_HANDLE) == inherited_error);
    puts("xxwidgets Windows TUI tests passed");
    return 0;
}
