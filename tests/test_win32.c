#include "xxwidgets/xxwidgets.h"
#include "xxwidgets/xxwidgets_combobox.h"
#include "xxwidgets/xxwidgets_process.h"
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "line %d: %s\n", __LINE__, #expr); exit(1); } } while (0)

static int clicks, changes, selections, closes, activations;
static int shortcuts, shortcut_action;
static HWND options_owner;
static int options_action, options_ticks;

typedef struct process_test {
    HWND owner;
    uint64_t started;
    int fast, cancel, phase, stopped;
} process_test;

static BOOL CALLBACK find_process(HWND window, LPARAM result)
{
    wchar_t title[80];
    GetWindowTextW(window, title, 80);
    if (!wcscmp(title, L"Process test")) { *(HWND *)result = window; return FALSE; }
    return TRUE;
}

static xxwidgets_status process_update(void *user, int stop, xx_pd_struct *progress, int *finished)
{
    process_test *test = (process_test *)user;
    HWND dialog = NULL, bar = NULL;
    uint64_t elapsed = (uint64_t)GetTickCount64() - test->started;
    int i;
    CHECK(elapsed < 5000);
    EnumThreadWindows(GetCurrentThreadId(), find_process, (LPARAM)&dialog);
    CHECK(!IsWindowEnabled(test->owner));
    if (test->fast) { CHECK(!dialog); *finished = elapsed >= 100; return XXWIDGETS_OK; }
    if (stop) { CHECK(progress->is_stop); test->stopped = 1; *finished = 1; return XXWIDGETS_OK; }
    if (dialog) {
        CHECK(elapsed > 1000 && IsWindowVisible(dialog) && GetWindow(dialog, GW_OWNER) == test->owner);
        for (i = 0; i < XX_PD_LEVELS; ++i) {
            bar = FindWindowExW(dialog, bar, PROGRESS_CLASSW, NULL);
            CHECK(bar);
            CHECK(IsWindowVisible(bar) == (!test->phase || i % 2 == 0));
            CHECK(SendMessageW(bar, PBM_GETPOS, 0, 0) == (i == 1 ? 99 : 50));
        }
        CHECK(!FindWindowExW(dialog, bar, PROGRESS_CLASSW, NULL));
        if (test->phase == 1) {
            if (test->cancel) CHECK(PostMessageW(dialog, WM_KEYDOWN, VK_ESCAPE, 0));
            else *finished = 1;
        }
        ++test->phase;
    }
    for (i = 0; i < XX_PD_LEVELS; ++i) {
        progress->records[i].is_busy = !test->phase || i % 2 == 0;
        progress->records[i].current = i == 1 ? UINT64_MAX - 1 : 50;
        progress->records[i].total = i == 1 ? UINT64_MAX : 100;
        memcpy(progress->records[i].status, "Native record \xce\xbb", sizeof("Native record \xce\xbb"));
    }
    return XXWIDGETS_OK;
}

static void check_process_dialog(xxwidgets_widget *window)
{
    int mode, before_clicks = clicks, before_changes = changes, before_closes = closes;
    for (mode = 0; mode < 3; ++mode) {
        xx_pd_struct progress = {0};
        process_test test = {0};
        HWND dialog = NULL;
        test.owner = (HWND)xxwidgets_widget_native_handle(window);
        test.started = (uint64_t)GetTickCount64(); test.fast = mode == 0; test.cancel = mode == 2;
        {
            xxwidgets_status status = xxwidgets_process_dialog(window, "Process test", &progress, process_update, &test);
            if (status != XXWIDGETS_OK) fprintf(stderr, "Process mode %d: %s, phase %d\n", mode, xxwidgets_status_string(status), test.phase);
            CHECK(status == XXWIDGETS_OK);
        }
        CHECK(IsWindowEnabled(test.owner) && progress.is_stop == (mode == 2));
        CHECK(test.fast || test.phase >= 2);
        CHECK(test.stopped == (mode == 2));
        EnumThreadWindows(GetCurrentThreadId(), find_process, (LPARAM)&dialog);
        CHECK(!dialog);
    }
    CHECK(clicks == before_clicks && changes == before_changes && closes == before_closes);
}

static BOOL CALLBACK find_options(HWND window, LPARAM result)
{
    wchar_t title[80];
    GetWindowTextW(window, title, 80);
    if (!wcscmp(title, L"Options test")) { *(HWND *)result = window; return FALSE; }
    return TRUE;
}

static VOID CALLBACK drive_options(HWND unused, UINT message, UINT_PTR timer, DWORD time)
{
    HWND dialog = NULL, checkbox, button;
    (void)unused; (void)message; (void)time;
    CHECK(++options_ticks < 100);
    EnumThreadWindows(GetCurrentThreadId(), find_options, (LPARAM)&dialog);
    if (!dialog) return;
    CHECK(!IsWindowEnabled(options_owner) && GetWindow(dialog, GW_OWNER) == options_owner);
    checkbox = FindWindowExW(dialog, NULL, L"BUTTON", L"Advanced \x03bb");
    CHECK(checkbox);
    SendMessageW(checkbox, BM_CLICK, 0, 0);
    CHECK(KillTimer(NULL, timer));
    if (options_action == 3 || options_action == 5)
        CHECK(PostMessageW(checkbox, WM_KEYDOWN, options_action == 3 ? VK_ESCAPE : VK_RETURN, 0));
    else if (options_action == 4) CHECK(PostMessageW(dialog, WM_CLOSE, 0, 0));
    else {
        button = FindWindowExW(dialog, NULL, L"BUTTON", options_action == 1 ? L"OK" : L"Cancel");
        CHECK(button); SendMessageW(button, BM_CLICK, 0, 0);
    }
}

static void check_options_dialog(xxwidgets_widget *window)
{
    int action, accepted, before_clicks = clicks, before_changes = changes, before_closes = closes;
    options_owner = (HWND)xxwidgets_widget_native_handle(window);
    for (action = 1; action <= 5; ++action) {
        xxwidgets_option options[] = {{"Advanced \xce\xbb", 0}, {"Show log", 1}};
        options_action = action; options_ticks = 0;
        CHECK(SetTimer(NULL, 0, 20, drive_options));
        CHECK(xxwidgets_options_dialog(window, "Options test", options, 2, &accepted) == XXWIDGETS_OK);
        CHECK(accepted == (action == 1 || action == 5));
        CHECK(options[0].value == accepted && options[1].value == 1);
        CHECK(IsWindowEnabled(options_owner));
    }
    CHECK(clicks == before_clicks && changes == before_changes && closes == before_closes);
}
static HWND about_owner;
static int about_action, about_ticks;

static BOOL CALLBACK find_about(HWND window, LPARAM result)
{
    wchar_t title[80];
    GetWindowTextW(window, title, 80);
    if (!wcscmp(title, L"About test")) { *(HWND *)result = window; return FALSE; }
    return TRUE;
}

static VOID CALLBACK drive_about(HWND unused, UINT message, UINT_PTR timer, DWORD time)
{
    HWND dialog = NULL, edit, picture, button;
    wchar_t *body;
    int length;
    RECT bounds, area;
    (void)unused; (void)message; (void)time;
    CHECK(++about_ticks < 100);
    EnumThreadWindows(GetCurrentThreadId(), find_about, (LPARAM)&dialog);
    if (!dialog) return;
    CHECK(!IsWindowEnabled(about_owner) && GetWindow(dialog, GW_OWNER) == about_owner);
    CHECK(!(GetWindowLongPtrW(dialog, GWL_STYLE) & (WS_THICKFRAME | WS_MAXIMIZEBOX)));
    CHECK(SendMessageW(dialog, WM_GETICON, ICON_BIG, 0) == SendMessageW(about_owner, WM_GETICON, ICON_BIG, 0));
    edit = GetDlgItem(dialog, 101); picture = GetDlgItem(dialog, 102);
    CHECK(edit && (GetWindowLongPtrW(edit, GWL_STYLE) & ES_READONLY));
    length = GetWindowTextLengthW(edit);
    CHECK(length > 0 && (about_action != 1 || length > 50000));
    body = (wchar_t *)calloc((size_t)length + 1, sizeof(*body)); CHECK(body);
    CHECK(GetWindowTextW(edit, body, length + 1) == length);
    CHECK(wcsstr(body, L"Native \x03bb\r\n\r\nVersion 3") && wcsstr(body, L"End of credits"));
    free(body);
    if (about_action != 6) {
        BITMAP bitmap;
        HBITMAP image;
        unsigned char *pixels;
        CHECK(picture);
        image = (HBITMAP)SendMessageW(picture, STM_GETIMAGE, IMAGE_BITMAP, 0);
        CHECK(image && GetObjectW(image, sizeof(bitmap), &bitmap));
        CHECK(bitmap.bmWidth == 2 * bitmap.bmHeight && bitmap.bmBits);
        pixels = (unsigned char *)bitmap.bmBits;
        CHECK(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 255);
    } else CHECK(!picture);
    button = FindWindowExW(dialog, NULL, L"BUTTON", L"Dismiss \x03bb"); CHECK(button);
    CHECK(GetClientRect(dialog, &area) && GetWindowRect(button, &bounds));
    MapWindowPoints(NULL, dialog, (POINT *)&bounds, 2);
    CHECK(bounds.top >= 0 && bounds.bottom <= area.bottom && bounds.right <= area.right);
    CHECK(KillTimer(NULL, timer));
    if (about_action == 2 || about_action == 3)
        CHECK(PostMessageW(edit, WM_KEYDOWN, about_action == 2 ? VK_ESCAPE : VK_RETURN, 0));
    else if (about_action == 4) CHECK(PostMessageW(dialog, WM_CLOSE, 0, 0));
    else SendMessageW(button, BM_CLICK, 0, 0);
}

static void check_about_dialog(xxwidgets_widget *window)
{
    xxwidgets_about_dialog *about = NULL;
    unsigned char pixels[] = {255, 0, 0, 255, 0, 255, 0, 128};
    char *description = (char *)malloc(50001);
    int action, before_clicks = clicks, before_changes = changes, before_closes = closes;
    DWORD gdi = 0;
    CHECK(description); memset(description, 'x', 50000); description[50000] = 0;
    CHECK(xxwidgets_about_dialog_create(&about) == XXWIDGETS_OK);
    CHECK(xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_TITLE, "About test") == XXWIDGETS_OK);
    CHECK(xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_PROGRAM_NAME, "Native \xce\xbb") == XXWIDGETS_OK);
    CHECK(xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_VERSION, "Version 3") == XXWIDGETS_OK);
    CHECK(xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_DESCRIPTION, description) == XXWIDGETS_OK);
    CHECK(xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_CREDITS, "End of credits") == XXWIDGETS_OK);
    CHECK(xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_CLOSE_LABEL, "Dismiss \xce\xbb") == XXWIDGETS_OK);
    free(description);
    CHECK(xxwidgets_about_dialog_set_image(about, pixels, 2, 1, 8) == XXWIDGETS_OK);
    pixels[0] = 0;
    about_owner = (HWND)xxwidgets_widget_native_handle(window);
    SendMessageW(about_owner, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(NULL, MAKEINTRESOURCEW(32512)));
    for (action = 1; action <= 6; ++action) {
        if (action == 2)
            CHECK(xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_DESCRIPTION, "Updated description") == XXWIDGETS_OK);
        if (action == 6) CHECK(xxwidgets_about_dialog_set_image(about, NULL, 0, 0, 0) == XXWIDGETS_OK);
        about_action = action; about_ticks = 0;
        CHECK(SetTimer(NULL, 0, 20, drive_about));
        CHECK(xxwidgets_about_dialog_show(about, window) == XXWIDGETS_OK);
        CHECK(IsWindowEnabled(about_owner));
        if (action == 1) gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        else CHECK(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdi);
    }
    CHECK(xxwidgets_about_dialog_destroy(about) == XXWIDGETS_OK);
    CHECK(clicks == before_clicks && changes == before_changes && closes == before_closes);
}

static int context_requests;
static xxwidgets_event last_context;
static size_t last_context_source;
static xxwidgets_widget *disable_on_select;
static xxwidgets_widget *combo_under_test, *checkcombo_under_test;
static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user_data)
{
    (void)user_data;
    if (event->widget == combo_under_test && event->type == XXWIDGETS_EVENT_SELECT) {
        xx_var current = {0};
        CHECK(xxwidgets_combobox_get_current(event->widget, &current) == XXWIDGETS_OK);
        CHECK(current.type == XX_VAR_TYPE_UINT64 && current.val.u64 == (uint64_t)event->value + 100);
    }
    if (event->widget == checkcombo_under_test && event->type == XXWIDGETS_EVENT_CHANGE) {
        int checked;
        CHECK(xxwidgets_checkcombobox_is_checked(event->widget, (size_t)event->value, &checked) == XXWIDGETS_OK && checked);
    }
    if (event->type != XXWIDGETS_EVENT_CONTEXT_MENU)
        CHECK(event->x == 0 && event->y == 0);
    if (event->type == XXWIDGETS_EVENT_CLICK) ++clicks;
    if (event->type == XXWIDGETS_EVENT_CHANGE) ++changes;
    if (event->type == XXWIDGETS_EVENT_SELECT) {
        ++selections;
        if (event->widget == disable_on_select) {
            disable_on_select = NULL;
            CHECK(xxwidgets_widget_set_enabled(event->widget, 0) == XXWIDGETS_OK);
        }
    }
    if (event->type == XXWIDGETS_EVENT_ACTIVATE) ++activations;
    if (event->type == XXWIDGETS_EVENT_SHORTCUT) { ++shortcuts; shortcut_action = event->value; }
    if (event->type == XXWIDGETS_EVENT_CONTEXT_MENU) {
        xxwidgets_archive_browser_entry entry;
        int value;
        ++context_requests;
        last_context = *event;
        CHECK(xxwidgets_widget_get_value(event->widget, &value) == XXWIDGETS_OK && value == event->value);
        CHECK(xxwidgets_archivebrowser_get_selection(event->widget, &last_context_source, &entry) == XXWIDGETS_OK);
    }
    if (event->type == XXWIDGETS_EVENT_CLOSE) { ++closes; xxwidgets_app_quit(app, 7); }
}
static xxwidgets_widget *make(xxwidgets_app *app, xxwidgets_widget *parent, xxwidgets_kind kind, const char *text)
{
    xxwidgets_widget *widget = NULL;
    xxwidgets_rect rect = {1, 1, 25, 2};
    CHECK(xxwidgets_widget_create(app, parent, kind, text, rect, &widget) == XXWIDGETS_OK);
    return widget;
}

static BOOL CALLBACK find_checkcombo(HWND window, LPARAM result)
{
    wchar_t class_name[32];
    GetClassNameW(window, class_name, 32);
    if (!_wcsicmp(class_name, L"STATIC") && IsWindowVisible(window) && GetDlgItem(window, 1)) {
        *(HWND *)result = window; return FALSE;
    }
    return TRUE;
}

static void check_comboboxes(xxwidgets_app *app, xxwidgets_widget *window)
{
    xxwidgets_widget *combo = make(app, window, XXWIDGETS_COMBOBOX, "Choose");
    xxwidgets_widget *check = make(app, window, XXWIDGETS_CHECKCOMBOBOX, "Choose several");
    xxwidgets_rect position = {1, 1, 28, 2}, owner = {1, 1, 60, 20};
    xx_str_w_s labels[] = {{L"Same \x03bb", 6, 7, true}, {L"Same \x03bb", 6, 7, true}, {L"Last", 4, 5, true}};
    xx_meta_string records[3] = {0};
    const xx_meta_string *checked[3];
    xx_var current = {0};
    HWND control = xxwidgets_widget_native_handle(combo), button = xxwidgets_widget_native_handle(check), popup = NULL, list;
    wchar_t caption[80];
    int i, before = selections, before_changes = changes;
    size_t count;
    DWORD gdi;
    CHECK(xxwidgets_widget_set_rect(window, owner) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_set_rect(combo, position) == XXWIDGETS_OK);
    position.y = 4; CHECK(xxwidgets_widget_set_rect(check, position) == XXWIDGETS_OK);
    for (i = 0; i < 3; ++i) { records[i].meta_string = labels + i; records[i].var.type = XX_VAR_TYPE_UINT64; records[i].var.val.u64 = (uint64_t)i + 100; }
    CHECK(xxwidgets_combobox_set_records(combo, records, 3) == XXWIDGETS_OK);
    CHECK(xxwidgets_combobox_set_records(check, records, 3) == XXWIDGETS_OK);
    CHECK(SendMessageW(control, CB_GETCOUNT, 0, 0) == 3 && SendMessageW(control, CB_GETCURSEL, 0, 0) == 0);
    CHECK(SendMessageW(control, CB_GETLBTEXT, 1, (LPARAM)caption) == 6 && !wcscmp(caption, L"Same \x03bb"));
    CHECK(xxwidgets_widget_set_value(combo, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_combobox_get_current(combo, &current) == XXWIDGETS_OK && current.val.u64 == 101);
    CHECK(selections == before);
    combo_under_test = combo; checkcombo_under_test = check;
    SendMessageW(control, CB_SETCURSEL, 2, 0);
    SendMessageW(xxwidgets_widget_native_handle(window), WM_COMMAND,
        MAKEWPARAM(GetDlgCtrlID(control), CBN_SELCHANGE), (LPARAM)control);
    CHECK(selections == before + 1);
    CHECK(xxwidgets_widget_set_visible(window, 1) == XXWIDGETS_OK);
    SendMessageW(button, BM_CLICK, 0, 0);
    EnumThreadWindows(GetCurrentThreadId(), find_checkcombo, (LPARAM)&popup);
    CHECK(popup && GetWindow(popup, GW_OWNER) == xxwidgets_widget_native_handle(window));
    list = GetDlgItem(popup, 1);
    CHECK(SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) == 3);
    for (i = 1; i < 3; ++i) {
        LVITEMW state = {0}; state.stateMask = LVIS_STATEIMAGEMASK; state.state = INDEXTOSTATEIMAGEMASK(2);
        CHECK(SendMessageW(list, LVM_SETITEMSTATE, (WPARAM)i, (LPARAM)&state));
    }
    CHECK(changes == before_changes + 2);
    CHECK(xxwidgets_checkcombobox_get_checked(check, checked, 3, &count) == XXWIDGETS_OK && count == 2);
    CHECK(checked[0]->var.val.u64 == 101 && checked[1]->var.val.u64 == 102);
    CHECK(GetWindowTextW(button, caption, 80) && wcsstr(caption, L"2 selected"));
    CHECK(PostMessageW(list, WM_KEYDOWN, VK_ESCAPE, 0));
    CHECK(xxwidgets_app_poll(app, 0) == XXWIDGETS_OK && !IsWindowVisible(popup));
    SendMessageW(button, BM_CLICK, 0, 0); CHECK(IsWindowVisible(popup));
    CHECK(xxwidgets_widget_set_enabled(window, 0) == XXWIDGETS_OK && !IsWindowVisible(popup));
    CHECK(xxwidgets_widget_set_enabled(window, 1) == XXWIDGETS_OK);
    SendMessageW(button, BM_CLICK, 0, 0); CHECK(IsWindowVisible(popup));
    CHECK(xxwidgets_widget_set_enabled(check, 0) == XXWIDGETS_OK && !IsWindowVisible(popup));
    CHECK(xxwidgets_widget_set_enabled(check, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_combobox_set_records(check, records, 2) == XXWIDGETS_OK);
    CHECK(xxwidgets_checkcombobox_get_checked(check, NULL, 0, &count) == XXWIDGETS_OK && !count);
    SendMessageW(button, BM_CLICK, 0, 0);
    CHECK(IsWindowVisible(popup) && SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) == 2);
    CHECK(xxwidgets_combobox_set_records(check, NULL, 0) == XXWIDGETS_OK && !IsWindowVisible(popup));
    CHECK(xxwidgets_combobox_set_records(combo, NULL, 0) == XXWIDGETS_OK && SendMessageW(control, CB_GETCOUNT, 0, 0) == 0);
    combo_under_test = checkcombo_under_test = NULL;
    CHECK(xxwidgets_widget_destroy(combo) == XXWIDGETS_OK);
    gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    CHECK(xxwidgets_widget_destroy(check) == XXWIDGETS_OK && !IsWindow(popup));
    CHECK(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdi);
}

static void hex_row(HWND handle, int index, WCHAR *text, size_t capacity)
{
    LRESULT length = SendMessageW(handle, LB_GETTEXTLEN, (WPARAM)index, 0);
    CHECK(length != LB_ERR && (size_t)length < capacity);
    CHECK(SendMessageW(handle, LB_GETTEXT, (WPARAM)index, (LPARAM)text) == length);
}

static void check_hexview(xxwidgets_app *app, xxwidgets_widget *window, HWND hwnd)
{
    unsigned char data[] = {'A', 'B', 'C', 0, 0x1f, 0x7f, 0x80, 0xff,
                            1, 2, 3, 4, 5, 6, 7, 8, 'x', 'y', 'z'};
    xxwidgets_widget *hexview = make(app, window, XXWIDGETS_HEXVIEW, "memory");
    HWND handle = (HWND)xxwidgets_widget_native_handle(hexview);
    WCHAR class_name[32], row[256];
    HFONT font;
    HDC dc;
    HGDIOBJ previous;
    SIZE wide, narrow;
    LRESULT extent16, extent8;
    size_t offset, length;
    char caption[32];
    int value, before = selections;
    CHECK(GetClassNameW(handle, class_name, 32) && _wcsicmp(class_name, L"LISTBOX") == 0);
    font = (HFONT)SendMessageW(handle, WM_GETFONT, 0, 0);
    CHECK(font != NULL);
    dc = GetDC(handle);
    CHECK(dc != NULL);
    previous = SelectObject(dc, font);
    CHECK(previous != NULL && previous != HGDI_ERROR);
    CHECK(GetTextExtentPoint32W(dc, L"W", 1, &wide));
    CHECK(GetTextExtentPoint32W(dc, L"i", 1, &narrow));
    CHECK(wide.cx > 0 && wide.cx == narrow.cx);
    CHECK(SelectObject(dc, previous) != NULL);
    CHECK(ReleaseDC(handle, dc));
    CHECK(xxwidgets_hexview_set_data(hexview, data, sizeof(data)) == XXWIDGETS_OK);
    CHECK(xxwidgets_hexview_size(hexview) == sizeof(data));
    CHECK(SendMessageW(handle, LB_GETCOUNT, 0, 0) == 2);
    CHECK(xxwidgets_widget_get_value(hexview, &value) == XXWIDGETS_OK && value == 0);
    hex_row(handle, 0, row, sizeof(row) / sizeof(row[0]));
    CHECK(wcsncmp(row, L"0000000000000000  41 42 43 00", 29) == 0);
    CHECK(wcsstr(row, L"|ABC.............|") != NULL);
    hex_row(handle, 1, row, sizeof(row) / sizeof(row[0]));
    CHECK(wcsncmp(row, L"0000000000000010", 16) == 0);
    CHECK(wcsstr(row, L"78 79 7A") != NULL && wcsstr(row, L"|xyz             |") != NULL);
    extent16 = SendMessageW(handle, LB_GETHORIZONTALEXTENT, 0, 0);
    CHECK(extent16 > 0 && (GetWindowLongPtrW(handle, GWL_STYLE) & WS_HSCROLL));
    CHECK(xxwidgets_widget_set_value(hexview, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_hexview_get_selection(hexview, &offset, &length) == XXWIDGETS_OK);
    CHECK(offset == 16 && length == 3);
    CHECK(selections == before);
    SendMessageW(handle, LB_SETCURSEL, 0, 0);
    SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(handle), LBN_SELCHANGE), (LPARAM)handle);
    CHECK(selections == before + 1);
    CHECK(xxwidgets_hexview_get_selection(hexview, &offset, &length) == XXWIDGETS_OK);
    CHECK(offset == 0 && length == 16);

    /* New bytes with the same row count must replace native list contents. */
    data[0] = 'Q';
    CHECK(xxwidgets_hexview_set_data(hexview, data, sizeof(data)) == XXWIDGETS_OK);
    CHECK(SendMessageW(handle, LB_GETCOUNT, 0, 0) == 2);
    hex_row(handle, 0, row, sizeof(row) / sizeof(row[0]));
    CHECK(wcsstr(row, L"51 42 43 00") != NULL && wcsstr(row, L"|QBC.............|") != NULL);
    CHECK(xxwidgets_hexview_set_layout(hexview, UINT64_C(0x12340000), 8) == XXWIDGETS_OK);
    CHECK(SendMessageW(handle, LB_GETCOUNT, 0, 0) == 3);
    hex_row(handle, 2, row, sizeof(row) / sizeof(row[0]));
    CHECK(wcsncmp(row, L"0000000012340010", 16) == 0);
    CHECK(wcsstr(row, L"|xyz     |") != NULL);
    extent8 = SendMessageW(handle, LB_GETHORIZONTALEXTENT, 0, 0);
    CHECK(extent8 > 0 && extent8 < extent16);
    CHECK(xxwidgets_widget_set_value(hexview, 2) == XXWIDGETS_OK);
    CHECK(xxwidgets_hexview_get_selection(hexview, &offset, &length) == XXWIDGETS_OK);
    CHECK(offset == 16 && length == 3);
    /* Changing addresses alone keeps the row count but refreshes each line. */
    CHECK(xxwidgets_hexview_set_layout(hexview, UINT64_C(0x20000000), 8) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_get_value(hexview, &value) == XXWIDGETS_OK && value == 2);
    hex_row(handle, 2, row, sizeof(row) / sizeof(row[0]));
    CHECK(wcsncmp(row, L"0000000020000010", 16) == 0);
    CHECK(xxwidgets_hexview_set_layout(hexview, UINT64_C(0x20000000), 32) == XXWIDGETS_OK);
    CHECK(SendMessageW(handle, LB_GETCOUNT, 0, 0) == 1);
    CHECK(SendMessageW(handle, LB_GETHORIZONTALEXTENT, 0, 0) > extent16);
    CHECK(xxwidgets_hexview_get_selection(hexview, &offset, &length) == XXWIDGETS_OK);
    CHECK(offset == 0 && length == sizeof(data));
    CHECK(xxwidgets_widget_set_text(hexview, "caption") == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_get_text(hexview, caption, sizeof(caption), NULL) == XXWIDGETS_OK);
    CHECK(strcmp(caption, "caption") == 0 && SendMessageW(handle, LB_GETCOUNT, 0, 0) == 1);
    CHECK(xxwidgets_hexview_set_data(hexview, NULL, 0) == XXWIDGETS_OK);
    CHECK(SendMessageW(handle, LB_GETCOUNT, 0, 0) == 0);
    CHECK(SendMessageW(handle, LB_GETHORIZONTALEXTENT, 0, 0) == 0);
    CHECK(xxwidgets_hexview_get_selection(hexview, &offset, &length) == XXWIDGETS_OK);
    CHECK(offset == SIZE_MAX && length == 0);
    CHECK(selections == before + 1);
}

static void check_archiveview(xxwidgets_app *app, xxwidgets_widget *window, HWND hwnd)
{
    char path[] = "folder/Gr\xc3\xbc\xc3\x9f" "e.txt";
    xxwidgets_archive_entry entries[] = {{"folder/", 0, 1}, {path, UINT64_MAX, 0}}, entry;
    xxwidgets_widget *archive = make(app, window, XXWIDGETS_ARCHIVEVIEW, "archive");
    HWND handle = (HWND)xxwidgets_widget_native_handle(archive);
    WCHAR row[256];
    size_t index;
    int before = selections;
    CHECK(xxwidgets_archiveview_set_entries(archive, entries, 2) == XXWIDGETS_OK);
    CHECK(SendMessageW(handle, LB_GETCOUNT, 0, 0) == 2);
    CHECK(GetWindowLongPtrW(handle, GWL_STYLE) & WS_HSCROLL);
    CHECK(SendMessageW(handle, LB_GETHORIZONTALEXTENT, 0, 0) > 0);
    hex_row(handle, 0, row, sizeof(row) / sizeof(row[0]));
    CHECK(wcsncmp(row, L"<DIR>", 5) == 0 && wcscmp(row + 28, L"folder/") == 0);
    hex_row(handle, 1, row, sizeof(row) / sizeof(row[0]));
    CHECK(wcsstr(row, L"18446744073709551615") != NULL);
    CHECK(wcscmp(row + 28, L"folder/Gr\x00fc\x00df" L"e.txt") == 0);
    CHECK(xxwidgets_widget_set_value(archive, 1) == XXWIDGETS_OK && selections == before);
    CHECK(xxwidgets_archiveview_get_selection(archive, &index, &entry) == XXWIDGETS_OK);
    CHECK(index == 1 && entry.size == UINT64_MAX && strcmp(entry.path, path) == 0);
    SendMessageW(handle, LB_SETCURSEL, 0, 0);
    SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(handle), LBN_SELCHANGE), (LPARAM)handle);
    CHECK(selections == before + 1);
    CHECK(xxwidgets_archiveview_get_selection(archive, &index, &entry) == XXWIDGETS_OK && index == 0);
    /* Replacing the same number of entries must refresh the native rows. */
    entries[1].path = "changed.bin"; entries[1].size = UINT64_C(1234567890123);
    CHECK(xxwidgets_archiveview_set_entries(archive, entries, 2) == XXWIDGETS_OK);
    CHECK(SendMessageW(handle, LB_GETCOUNT, 0, 0) == 2 && selections == before + 1);
    hex_row(handle, 1, row, sizeof(row) / sizeof(row[0]));
    CHECK(wcsstr(row, L"1234567890123") != NULL && wcscmp(row + 28, L"changed.bin") == 0);
    CHECK(xxwidgets_archiveview_clear(archive) == XXWIDGETS_OK);
    CHECK(SendMessageW(handle, LB_GETCOUNT, 0, 0) == 0);
    CHECK(SendMessageW(handle, LB_GETHORIZONTALEXTENT, 0, 0) == 0);
    CHECK(xxwidgets_archiveview_get_selection(archive, &index, &entry) == XXWIDGETS_OK && index == SIZE_MAX);
}

static void browser_cell(HWND list, int row, int column, WCHAR *text, size_t capacity)
{
    LVITEMW item;
    memset(&item, 0, sizeof(item));
    item.iSubItem = column;
    item.pszText = text;
    item.cchTextMax = (int)capacity;
    SendMessageW(list, LVM_GETITEMTEXTW, (WPARAM)row, (LPARAM)&item);
}

static void browser_double_click(HWND container, HWND list, int row)
{
    NMITEMACTIVATE activation;
    memset(&activation, 0, sizeof(activation));
    activation.hdr.hwndFrom = list;
    activation.hdr.idFrom = (UINT_PTR)GetDlgCtrlID(list);
    activation.hdr.code = NM_DBLCLK;
    activation.iItem = row;
    SendMessageW(container, WM_NOTIFY, activation.hdr.idFrom, (LPARAM)&activation);
}

static void check_browser_icons(HWND list)
{
    HIMAGELIST images = (HIMAGELIST)SendMessageW(list, LVM_GETIMAGELIST, LVSIL_SMALL, 0);
    BITMAPINFO info;
    void *pixels = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bitmap;
    HGDIOBJ previous;
    const COLORREF background_color = RGB(17, 83, 129);
    HBRUSH background = CreateSolidBrush(background_color);
    RECT area = {0, 0, 16, 16};
    int image;
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = 16;
    info.bmiHeader.biHeight = -16;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
    CHECK(images && dc && bitmap && pixels && background);
    previous = SelectObject(dc, bitmap);
    CHECK(previous && previous != HGDI_ERROR);
    for (image = 0; image < 2; ++image) {
        CHECK(FillRect(dc, &area, background));
        CHECK(ImageList_Draw(images, image, dc, 0, 0, ILD_TRANSPARENT));
        CHECK(GdiFlush());
        /* Transparency must reveal the destination; visible pixels must keep
         * their colors instead of rendering as an opaque black square. */
        CHECK(GetPixel(dc, 0, 0) == background_color);
        CHECK(GetPixel(dc, 15, 15) == background_color);
        if (!image) {
            CHECK(GetPixel(dc, 8, 8) == RGB(255, 207, 82));
            CHECK(GetPixel(dc, 1, 5) == RGB(132, 109, 53));
        } else {
            CHECK(GetPixel(dc, 8, 7) == RGB(255, 255, 255));
            CHECK(GetPixel(dc, 3, 1) == RGB(97, 115, 131));
        }
    }
    CHECK(SelectObject(dc, previous) != NULL);
    CHECK(DeleteObject(bitmap));
    CHECK(DeleteObject(background));
    CHECK(DeleteDC(dc));
}

static void check_browser_column_width(HWND list)
{
    RECT client;
    int width = 0, column;
    CHECK(GetClientRect(list, &client));
    for (column = 0; column < 5; ++column)
        width += (int)SendMessageW(list, LVM_GETCOLUMNWIDTH, (WPARAM)column, 0);
    CHECK(width == client.right - client.left);
}

static void check_archivebrowser(xxwidgets_app *app, xxwidgets_widget *window)
{
    xxwidgets_archive_browser_entry entries[] = {
        {"folder/nested/a.txt", 42, 12, 0,
         XXWIDGETS_ARCHIVE_SIZE_KNOWN | XXWIDGETS_ARCHIVE_PACKED_SIZE_KNOWN, "2026-09-28 12:34", "A"},
        {"folder/Gr\xc3\xbc\xc3\x9f" "e.txt", UINT64_MAX, 1234, 0,
         XXWIDGETS_ARCHIVE_SIZE_KNOWN | XXWIDGETS_ARCHIVE_PACKED_SIZE_KNOWN, NULL, "RA"},
        {"root.txt", 9, 4, 0, XXWIDGETS_ARCHIVE_SIZE_KNOWN | XXWIDGETS_ARCHIVE_PACKED_SIZE_KNOWN, NULL, NULL},
        {"zeta/", 0, 0, 1, 0, NULL, "D"},
        {"alpha.bin", 12, 0, 0, XXWIDGETS_ARCHIVE_SIZE_KNOWN, "2026-01-01\nX", "A\tR"}
    }, entry;
    xxwidgets_widget *browser = make(app, window, XXWIDGETS_ARCHIVEBROWSER, "archive browser");
    xxwidgets_rect rect = {0, 0, 100, 18};
    HWND container = (HWND)xxwidgets_widget_native_handle(browser);
    HWND up = GetDlgItem(container, 1), address = GetDlgItem(container, 2), list = GetDlgItem(container, 3);
    HWND header;
    WCHAR class_name[64], text[256];
    size_t index;
    int value, before_changes = changes, before_selections = selections, before_activations = activations;
    LVITEMW item;
    NMLISTVIEW column;
    HDITEMW heading;
    RECT list_rect;
    CHECK(IsWindow(up) && IsWindow(address) && IsWindow(list));
    CHECK(GetClassNameW(list, class_name, 64) && _wcsicmp(class_name, WC_LISTVIEWW) == 0);
    CHECK((GetWindowLongPtrW(list, GWL_STYLE) & LVS_TYPEMASK) == LVS_REPORT);
    CHECK(GetWindowLongPtrW(address, GWL_STYLE) & ES_READONLY);
    header = (HWND)SendMessageW(list, LVM_GETHEADER, 0, 0);
    CHECK(SendMessageW(header, HDM_GETITEMCOUNT, 0, 0) == 5);
    {
        LVCOLUMNW native_column;
        memset(&native_column, 0, sizeof(native_column));
        native_column.mask = LVCF_TEXT;
        native_column.pszText = text;
        native_column.cchTextMax = 256;
        CHECK(SendMessageW(list, LVM_GETCOLUMNW, 0, (LPARAM)&native_column));
        CHECK(wcscmp(text, L"Name") == 0);
        CHECK(SendMessageW(list, LVM_GETCOLUMNW, 2, (LPARAM)&native_column));
        CHECK(wcscmp(text, L"Packed Size") == 0);
    }
    CHECK(SendMessageW(list, LVM_GETIMAGELIST, LVSIL_SMALL, 0) != 0);
    check_browser_icons(list);
    CHECK(xxwidgets_widget_set_rect(browser, rect) == XXWIDGETS_OK);
    CHECK(GetClientRect(list, &list_rect) && list_rect.right > 0 && list_rect.bottom > 0);
    check_browser_column_width(list);
    {
        NMHEADERW end_track;
        int before_width = (int)SendMessageW(list, LVM_GETCOLUMNWIDTH, 0, 0);
        int size_width = (int)SendMessageW(list, LVM_GETCOLUMNWIDTH, 1, 0);
        memset(&end_track, 0, sizeof(end_track));
        end_track.hdr.hwndFrom = header; end_track.hdr.code = HDN_ENDTRACKW; end_track.iItem = 1;
        CHECK(SendMessageW(list, LVM_SETCOLUMNWIDTH, 1, size_width + 10));
        SendMessageW(list, WM_NOTIFY, 0, (LPARAM)&end_track);
        CHECK((int)SendMessageW(list, LVM_GETCOLUMNWIDTH, 0, 0) == before_width - 10);
        check_browser_column_width(list);
        end_track.iItem = 0;
        end_track.hdr.code = HDN_BEGINTRACKW;
        SendMessageW(list, WM_NOTIFY, 0, (LPARAM)&end_track);
        CHECK(SendMessageW(list, LVM_SETCOLUMNWIDTH, 0, before_width - 30));
        end_track.hdr.code = HDN_ENDTRACKW;
        SendMessageW(list, WM_NOTIFY, 0, (LPARAM)&end_track);
        CHECK(xxwidgets_widget_set_enabled(browser, 1) == XXWIDGETS_OK);
        CHECK((int)SendMessageW(list, LVM_GETCOLUMNWIDTH, 0, 0) == before_width - 30);
        rect.width += 20;
        CHECK(xxwidgets_widget_set_rect(browser, rect) == XXWIDGETS_OK);
        check_browser_column_width(list);
    }
    CHECK(xxwidgets_archivebrowser_set_archive(browser, "C:\\test\\sample.zip") == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_set_entries(browser, entries, 5) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_count(browser) == 5);
    CHECK(SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) == 4);
    CHECK(!IsWindowEnabled(up));
    CHECK(GetWindowTextW(address, text, 256) && wcscmp(text, L"C:\\test\\sample.zip\\") == 0);
    browser_cell(list, 0, 0, text, 256); CHECK(wcscmp(text, L"folder") == 0);
    browser_cell(list, 2, 0, text, 256); CHECK(wcscmp(text, L"alpha.bin") == 0);
    browser_cell(list, 2, 1, text, 256); CHECK(wcscmp(text, L"12") == 0);
    browser_cell(list, 2, 2, text, 256); CHECK(text[0] == 0);
    browser_cell(list, 2, 3, text, 256); CHECK(wcscmp(text, L"2026-01-01\\x0AX") == 0);
    browser_cell(list, 2, 4, text, 256); CHECK(wcscmp(text, L"A\\x09R") == 0);
    memset(&item, 0, sizeof(item)); item.mask = LVIF_IMAGE; item.iItem = 0;
    CHECK(SendMessageW(list, LVM_GETITEMW, 0, (LPARAM)&item) && item.iImage == 0);
    item.iItem = 2;
    CHECK(SendMessageW(list, LVM_GETITEMW, 0, (LPARAM)&item) && item.iImage == 1);
    CHECK(xxwidgets_widget_set_value(browser, 2) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_get_selection(browser, &index, &entry) == XXWIDGETS_OK);
    CHECK(index == 4 && strcmp(entry.path, "alpha.bin") == 0);
    CHECK(strcmp(entry.modified, "2026-01-01\nX") == 0 && strcmp(entry.attributes, "A\tR") == 0);
    CHECK(changes == before_changes && selections == before_selections && activations == before_activations);

    /* Real native selection notifications update the portable selection. */
    memset(&item, 0, sizeof(item)); item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&item);
    item.state = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(list, LVM_SETITEMSTATE, 3, (LPARAM)&item);
    CHECK(selections > before_selections);
    CHECK(xxwidgets_archivebrowser_get_selection(browser, &index, &entry) == XXWIDGETS_OK && index == 2);
    browser_double_click(container, list, 0);
    CHECK(changes == before_changes + 1 && activations == before_activations);
    CHECK(strcmp(xxwidgets_archivebrowser_directory(browser), "folder/") == 0 && IsWindowEnabled(up));
    CHECK(SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) == 2);
    CHECK(GetWindowTextW(address, text, 256) && wcscmp(text, L"C:\\test\\sample.zip\\folder\\") == 0);
    browser_cell(list, 0, 0, text, 256); CHECK(wcscmp(text, L"nested") == 0);
    browser_cell(list, 1, 0, text, 256); CHECK(wcscmp(text, L"Gr\x00fc\x00df" L"e.txt") == 0);
    browser_cell(list, 1, 1, text, 256); CHECK(wcscmp(text, L"18 446 744 073 709 551 615") == 0);
    browser_cell(list, 1, 2, text, 256); CHECK(wcscmp(text, L"1 234") == 0);
    browser_cell(list, 1, 4, text, 256); CHECK(wcscmp(text, L"RA") == 0);
    CHECK(xxwidgets_widget_set_value(browser, 0) == XXWIDGETS_OK);
    SendMessageW(list, WM_KEYDOWN, VK_RETURN, 0);
    CHECK(strcmp(xxwidgets_archivebrowser_directory(browser), "folder/nested/") == 0);
    browser_cell(list, 0, 0, text, 256); CHECK(wcscmp(text, L"a.txt") == 0);
    browser_cell(list, 0, 3, text, 256); CHECK(wcscmp(text, L"2026-09-28 12:34") == 0);
    SendMessageW(list, WM_KEYDOWN, VK_RETURN, 0);
    CHECK(activations == before_activations + 1);
    SendMessageW(list, WM_KEYDOWN, VK_BACK, 0);
    CHECK(strcmp(xxwidgets_archivebrowser_directory(browser), "folder/") == 0);
    SendMessageW(up, BM_CLICK, 0, 0);
    CHECK(xxwidgets_archivebrowser_directory(browser)[0] == 0 && !IsWindowEnabled(up));

    /* Header clicks sort ascending then descending, with directories first. */
    memset(&column, 0, sizeof(column));
    column.hdr.hwndFrom = list; column.hdr.idFrom = (UINT_PTR)GetDlgCtrlID(list);
    column.hdr.code = LVN_COLUMNCLICK; column.iSubItem = XXWIDGETS_ARCHIVE_COLUMN_SIZE;
    SendMessageW(container, WM_NOTIFY, column.hdr.idFrom, (LPARAM)&column);
    browser_cell(list, 2, 0, text, 256); CHECK(wcscmp(text, L"root.txt") == 0);
    SendMessageW(container, WM_NOTIFY, column.hdr.idFrom, (LPARAM)&column);
    browser_cell(list, 2, 0, text, 256); CHECK(wcscmp(text, L"alpha.bin") == 0);
    memset(&heading, 0, sizeof(heading)); heading.mask = HDI_FORMAT;
    CHECK(SendMessageW(header, HDM_GETITEMW, XXWIDGETS_ARCHIVE_COLUMN_SIZE, (LPARAM)&heading));
    CHECK(heading.fmt & HDF_SORTDOWN);
    CHECK(xxwidgets_widget_set_enabled(browser, 0) == XXWIDGETS_OK && !IsWindowEnabled(list));
    CHECK(xxwidgets_widget_set_enabled(browser, 1) == XXWIDGETS_OK && IsWindowEnabled(list));
    CHECK(xxwidgets_widget_get_value(browser, &value) == XXWIDGETS_OK && value >= 0);
    CHECK(xxwidgets_archivebrowser_set_entries(browser, NULL, 0) == XXWIDGETS_OK);
    CHECK(SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) == 0);
    CHECK(xxwidgets_widget_get_value(browser, &value) == XXWIDGETS_OK && value == -1);
}

static void check_archivebrowser_advanced(xxwidgets_app *app, xxwidgets_widget *window)
{
    xxwidgets_archive_property properties[] = {{"CRC32", "75BCC38E"}, {"Method", "Deflate:Fastest"}};
    xxwidgets_archive_browser_entry entries[2] = {0}, selected;
    xxwidgets_widget *browser = make(app, window, XXWIDGETS_ARCHIVEBROWSER, "advanced browser");
    HWND container = (HWND)xxwidgets_widget_native_handle(browser);
    HWND list = GetDlgItem(container, 3), header = (HWND)SendMessageW(list, LVM_GETHEADER, 0, 0);
    LVITEMW item;
    LVCOLUMNW column;
    wchar_t text[128];
    size_t source;
    entries[0].path = "word/document.xml"; entries[0].properties = properties; entries[0].property_count = 2;
    entries[1].path = "word/theme/theme1.xml";
    CHECK(xxwidgets_archivebrowser_set_entries(browser, entries, 2) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_set_directory(browser, "word") == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_set_value(browser, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_set_advanced(browser, 1) == XXWIDGETS_OK);
    CHECK(SendMessageW(header, HDM_GETITEMCOUNT, 0, 0) == 7);
    CHECK(SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) == 2);
    memset(&column, 0, sizeof(column)); column.mask = LVCF_TEXT;
    column.pszText = text; column.cchTextMax = 128;
    CHECK(SendMessageW(list, LVM_GETCOLUMNW, 5, (LPARAM)&column) && !wcscmp(text, L"CRC32"));
    memset(&item, 0, sizeof(item)); item.iSubItem = 6; item.pszText = text; item.cchTextMax = 128;
    CHECK(SendMessageW(list, LVM_GETITEMTEXTW, 1, (LPARAM)&item) && !wcscmp(text, L"Deflate:Fastest"));
    {
        NMLISTVIEW click;
        memset(&click, 0, sizeof(click)); click.hdr.hwndFrom = list; click.hdr.code = LVN_COLUMNCLICK;
        click.iSubItem = 5;
        SendMessageW(container, WM_NOTIFY, 0, (LPARAM)&click);
    }
    CHECK(xxwidgets_archivebrowser_get_selection(browser, &source, &selected) == XXWIDGETS_OK && source == 0);
    CHECK(xxwidgets_archivebrowser_set_advanced(browser, 0) == XXWIDGETS_OK);
    CHECK(SendMessageW(header, HDM_GETITEMCOUNT, 0, 0) == 5);
    CHECK(!strcmp(xxwidgets_archivebrowser_directory(browser), "word/"));
    CHECK(xxwidgets_archivebrowser_get_selection(browser, &source, &selected) == XXWIDGETS_OK && source == 0);
    CHECK(xxwidgets_archivebrowser_set_advanced(browser, 1) == XXWIDGETS_OK);
    properties[0].name = "Comment"; properties[0].value = "New field";
    CHECK(xxwidgets_archivebrowser_set_entries(browser, entries, 2) == XXWIDGETS_OK);
    CHECK(SendMessageW(list, LVM_GETCOLUMNW, 5, (LPARAM)&column) && !wcscmp(text, L"Comment"));
    CHECK(xxwidgets_widget_destroy(browser) == XXWIDGETS_OK);
}

static void browser_click_selection(HWND list, int row, int control, int shift)
{
    RECT bounds = {0};
    BYTE keys[256], changed[256];
    WPARAM flags = (control ? MK_CONTROL : 0) | (shift ? MK_SHIFT : 0);
    bounds.left = LVIR_BOUNDS;
    CHECK(SendMessageW(list, LVM_GETITEMRECT, (WPARAM)row, (LPARAM)&bounds));
    CHECK(GetKeyboardState(keys)); memcpy(changed, keys, sizeof(changed));
    changed[VK_CONTROL] = control ? 0x80 : 0; changed[VK_SHIFT] = shift ? 0x80 : 0;
    CHECK(SetKeyboardState(changed));
    /* The native mouse-down handler can enter its drag-detection loop. Queue
     * mouse-up first so that loop can finish without physical mouse input. */
    CHECK(PostMessageW(list, WM_LBUTTONUP, flags, MAKELPARAM(40, (bounds.top + bounds.bottom) / 2)));
    SendMessageW(list, WM_LBUTTONDOWN, flags | MK_LBUTTON, MAKELPARAM(40, (bounds.top + bounds.bottom) / 2));
    SendMessageW(list, WM_LBUTTONUP, flags, MAKELPARAM(40, (bounds.top + bounds.bottom) / 2));
    CHECK(SetKeyboardState(keys));
}

static void check_archivebrowser_multiselect(xxwidgets_app *app, xxwidgets_widget *window)
{
    xxwidgets_archive_browser_entry entries[4] = {0};
    xxwidgets_widget *browser = make(app, window, XXWIDGETS_ARCHIVEBROWSER, "multi browser");
    xxwidgets_rect rect = {0, 0, 110, 15};
    HWND container = (HWND)xxwidgets_widget_native_handle(browser), list = GetDlgItem(container, 3);
    size_t count, indexes[4];
    RECT bounds = {0};
    POINT point;
    entries[0].path = "folder/child.txt"; entries[1].path = "a.txt";
    entries[2].path = "b.txt"; entries[3].path = "c.txt";
    CHECK(!(GetWindowLongPtrW(list, GWL_STYLE) & LVS_SINGLESEL));
    CHECK(xxwidgets_widget_set_rect(browser, rect) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_set_entries(browser, entries, 4) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_set_visible(window, 1) == XXWIDGETS_OK);
    browser_click_selection(list, 1, 0, 0);
    CHECK(xxwidgets_archivebrowser_selection_count(browser) == 1);
    browser_click_selection(list, 3, 0, 1);
    CHECK(xxwidgets_archivebrowser_selection_count(browser) == 3);
    CHECK(SendMessageW(list, LVM_GETSELECTEDCOUNT, 0, 0) == 3);
    browser_click_selection(list, 2, 1, 0);
    CHECK(xxwidgets_archivebrowser_selection_count(browser) == 2);
    CHECK(SendMessageW(list, LVM_GETSELECTEDCOUNT, 0, 0) == 2);
    CHECK(xxwidgets_archivebrowser_selected_sources(browser, indexes, 4, &count) == XXWIDGETS_OK);
    CHECK(count == 2 && indexes[0] == 1 && indexes[1] == 3);
    /* Right-click an already selected row: retain the entire group. */
    bounds.left = LVIR_BOUNDS; CHECK(SendMessageW(list, LVM_GETITEMRECT, 1, (LPARAM)&bounds));
    point.x = 40; point.y = (bounds.top + bounds.bottom) / 2;
    CHECK(ClientToScreen(list, &point));
    {
        int before = context_requests;
        SendMessageW(list, WM_CONTEXTMENU, (WPARAM)list, MAKELPARAM(point.x, point.y));
        CHECK(context_requests == before + 1);
    }
    CHECK(xxwidgets_archivebrowser_selection_count(browser) == 2);
    CHECK(SendMessageW(list, LVM_GETSELECTEDCOUNT, 0, 0) == 2);
    CHECK(xxwidgets_archivebrowser_sort(browser, XXWIDGETS_ARCHIVE_COLUMN_NAME, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_set_advanced(browser, 1) == XXWIDGETS_OK);
    CHECK(SendMessageW(list, LVM_GETSELECTEDCOUNT, 0, 0) == 2);
    CHECK(xxwidgets_archivebrowser_selected_sources(browser, indexes, 4, &count) == XXWIDGETS_OK);
    CHECK(count == 2 && indexes[0] == 1 && indexes[1] == 3);
    /* A different row replaces the group, including inferred folder contents. */
    bounds.left = LVIR_BOUNDS; CHECK(SendMessageW(list, LVM_GETITEMRECT, 0, (LPARAM)&bounds));
    point.x = 40; point.y = (bounds.top + bounds.bottom) / 2; CHECK(ClientToScreen(list, &point));
    {
        int before = context_requests;
        SendMessageW(list, WM_CONTEXTMENU, (WPARAM)list, MAKELPARAM(point.x, point.y));
        CHECK(context_requests == before + 1);
    }
    CHECK(xxwidgets_archivebrowser_selection_count(browser) == 1);
    CHECK(xxwidgets_archivebrowser_selected_sources(browser, indexes, 4, &count) == XXWIDGETS_OK && count == 1 && indexes[0] == 0);
    {
        BYTE keys[256], changed[256];
        CHECK(GetKeyboardState(keys)); memcpy(changed, keys, sizeof(changed)); changed[VK_CONTROL] = 0x80;
        CHECK(SetKeyboardState(changed)); SendMessageW(list, WM_KEYDOWN, 'A', 0); CHECK(SetKeyboardState(keys));
    }
    CHECK(xxwidgets_archivebrowser_selection_count(browser) == 4);
    CHECK(SendMessageW(list, LVM_GETSELECTEDCOUNT, 0, 0) == 4);
    CHECK(xxwidgets_widget_destroy(browser) == XXWIDGETS_OK);
}

static void check_archivebrowser_context(xxwidgets_app *app, xxwidgets_widget *window)
{
    const xxwidgets_archive_browser_entry entries[] = {
        {"folder/", 0, 0, 1, 0, NULL, NULL},
        {"alpha.bin", 12, 0, 0, XXWIDGETS_ARCHIVE_SIZE_KNOWN, NULL, NULL},
        {"beta.txt", 24, 0, 0, XXWIDGETS_ARCHIVE_SIZE_KNOWN, NULL, NULL}
    };
    xxwidgets_widget *browser = make(app, window, XXWIDGETS_ARCHIVEBROWSER, "context test");
    xxwidgets_rect rect = {0, 0, 120, 20};
    HWND container = (HWND)xxwidgets_widget_native_handle(browser), list = GetDlgItem(container, 3);
    RECT bounds, client;
    POINT point, expected;
    LVITEMW state;
    int before = context_requests, value;
    size_t source;
    xxwidgets_archive_browser_entry entry;
    CHECK(xxwidgets_widget_set_rect(browser, rect) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_set_entries(browser, entries, 3) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_set_visible(window, 1) == XXWIDGETS_OK);
    CHECK(context_requests == before);
    memset(&bounds, 0, sizeof(bounds)); bounds.left = LVIR_BOUNDS;
    CHECK(SendMessageW(list, LVM_GETITEMRECT, 1, (LPARAM)&bounds));
    point.x = bounds.left + 24; point.y = (bounds.top + bounds.bottom) / 2;
    CHECK(ClientToScreen(list, &point));
    expected = point; CHECK(ScreenToClient(container, &expected));
    SendMessageW(list, WM_CONTEXTMENU, (WPARAM)list, MAKELPARAM(point.x, point.y));
    CHECK(context_requests == before + 1);
    CHECK(last_context.widget == browser && last_context.value == 1 && last_context_source == 1);
    CHECK(last_context.x == expected.x && last_context.y == expected.y);
    CHECK(xxwidgets_widget_get_value(browser, &value) == XXWIDGETS_OK && value == 1);
    CHECK(xxwidgets_app_poll(app, 0) == XXWIDGETS_OK);
    CHECK(context_requests == before + 1); /* The consumed child request is not forwarded twice. */

    CHECK(GetClientRect(list, &client));
    point.x = client.right - 8; point.y = client.bottom - 8;
    CHECK(ClientToScreen(list, &point));
    expected = point; CHECK(ScreenToClient(container, &expected));
    SendMessageW(container, WM_CONTEXTMENU, (WPARAM)list, MAKELPARAM(point.x, point.y));
    CHECK(context_requests == before + 2 && last_context.value == -1);
    CHECK(last_context_source == SIZE_MAX && last_context.x == expected.x && last_context.y == expected.y);
    CHECK(xxwidgets_archivebrowser_get_selection(browser, &source, &entry) == XXWIDGETS_OK);
    CHECK(source == SIZE_MAX && entry.path == NULL);

    /* Keyboard requests prefer the focused row and anchor at its native bounds. */
    memset(&state, 0, sizeof(state)); state.stateMask = LVIS_FOCUSED; state.state = LVIS_FOCUSED;
    SendMessageW(list, LVM_SETITEMSTATE, 2, (LPARAM)&state);
    SendMessageW(container, WM_CONTEXTMENU, (WPARAM)container, (LPARAM)-1);
    CHECK(context_requests == before + 3 && last_context.value == 2 && last_context_source == 2);
    memset(&bounds, 0, sizeof(bounds)); bounds.left = LVIR_BOUNDS;
    CHECK(SendMessageW(list, LVM_GETITEMRECT, 2, (LPARAM)&bounds));
    point.x = bounds.left + 24; point.y = bounds.bottom;
    CHECK(ClientToScreen(list, &point) && ScreenToClient(container, &point));
    CHECK(last_context.x == point.x && last_context.y == point.y);
    SendMessageW(list, WM_KEYDOWN, VK_APPS, 0);
    CHECK(context_requests == before + 4 && last_context.value == 2);
    SendMessageW(list, WM_KEYDOWN, VK_APPS, (LPARAM)1 << 30);
    SendMessageW(list, WM_KEYUP, VK_APPS, 0);
    CHECK(xxwidgets_app_poll(app, 0) == XXWIDGETS_OK);
    CHECK(context_requests == before + 4);

    CHECK(xxwidgets_widget_set_enabled(browser, 0) == XXWIDGETS_OK);
    SendMessageW(list, WM_CONTEXTMENU, (WPARAM)list, (LPARAM)-1);
    SendMessageW(container, WM_CONTEXTMENU, (WPARAM)container, (LPARAM)-1);
    CHECK(context_requests == before + 4);
    CHECK(xxwidgets_widget_set_enabled(browser, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_archivebrowser_set_entries(browser, NULL, 0) == XXWIDGETS_OK);
    SendMessageW(list, WM_CONTEXTMENU, (WPARAM)list, (LPARAM)-1);
    CHECK(context_requests == before + 5 && last_context.value == -1 && last_context_source == SIZE_MAX);
    CHECK(last_context.x >= 0 && last_context.y >= 0);

    CHECK(xxwidgets_archivebrowser_set_entries(browser, entries, 3) == XXWIDGETS_OK);
    memset(&bounds, 0, sizeof(bounds)); bounds.left = LVIR_BOUNDS;
    CHECK(SendMessageW(list, LVM_GETITEMRECT, 1, (LPARAM)&bounds));
    point.x = bounds.left + 24; point.y = (bounds.top + bounds.bottom) / 2;
    CHECK(ClientToScreen(list, &point));
    disable_on_select = browser;
    SendMessageW(list, WM_CONTEXTMENU, (WPARAM)list, MAKELPARAM(point.x, point.y));
    CHECK(disable_on_select == NULL && !IsWindowEnabled(list));
    CHECK(context_requests == before + 5); /* The SELECT callback disabled its browser. */
    CHECK(xxwidgets_widget_get_value(browser, &value) == XXWIDGETS_OK && value == 1);
    CHECK(xxwidgets_widget_set_enabled(browser, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_set_visible(browser, 0) == XXWIDGETS_OK);
    SendMessageW(list, WM_CONTEXTMENU, (WPARAM)list, (LPARAM)-1);
    CHECK(context_requests == before + 5);
    CHECK(xxwidgets_widget_set_visible(browser, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_set_visible(window, 0) == XXWIDGETS_OK);
    SendMessageW(list, WM_CONTEXTMENU, (WPARAM)list, (LPARAM)-1);
    CHECK(context_requests == before + 5);
}

static void check_archivebrowser_shift_f10(xxwidgets_app *app, xxwidgets_widget *window)
{
    const xxwidgets_archive_browser_entry entry = {"file.bin", 12, 0, 0,
                                                   XXWIDGETS_ARCHIVE_SIZE_KNOWN, NULL, NULL};
    xxwidgets_widget *browser = make(app, window, XXWIDGETS_ARCHIVEBROWSER, "system key test");
    HWND container = (HWND)xxwidgets_widget_native_handle(browser), list = GetDlgItem(container, 3);
    HWND hwnd = (HWND)xxwidgets_widget_native_handle(window);
    HMENU previous_menu = GetMenu(hwnd), menu = CreateMenu(), file = CreatePopupMenu();
    BYTE original_keys[256], keys[256];
    int before, after, shifted;
    CHECK(menu && file && AppendMenuW(file, MF_STRING, 1, L"Test command"));
    CHECK(AppendMenuW(menu, MF_POPUP, (UINT_PTR)file, L"&File"));
    CHECK(SetMenu(hwnd, menu));
    CHECK(xxwidgets_archivebrowser_set_entries(browser, &entry, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_set_visible(window, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_focus(browser) == XXWIDGETS_OK && GetFocus() == list);
    CHECK(xxwidgets_app_poll(app, 0) == XXWIDGETS_OK);
    CHECK(GetKeyboardState(original_keys));
    memcpy(keys, original_keys, sizeof(keys));
    keys[VK_SHIFT] |= 0x80;
    keys[VK_LSHIFT] |= 0x80;
    CHECK(SetKeyboardState(keys) && GetKeyState(VK_SHIFT) < 0);
    before = context_requests;
    /* F10 is a system key. Send it through the real message pump, with a menu
     * bar attached, so dialog/menu processing cannot steal Shift+F10. */
    CHECK(PostMessageW(list, WM_SYSKEYDOWN, VK_F10, 0));
    CHECK(xxwidgets_app_poll(app, 0) == XXWIDGETS_OK);
    after = context_requests;
    if (after == before + 1) {
        CHECK(PostMessageW(list, WM_SYSKEYUP, VK_F10, 0));
        CHECK(xxwidgets_app_poll(app, 0) == XXWIDGETS_OK);
        after = context_requests;
    }
    shifted = GetKeyState(VK_SHIFT) < 0;
    CHECK(SetKeyboardState(original_keys));
    CHECK(SetMenu(hwnd, previous_menu) && DestroyMenu(menu));
    CHECK(xxwidgets_widget_set_visible(window, 0) == XXWIDGETS_OK);
    CHECK(shifted && after == before + 1);
    CHECK(last_context.widget == browser && last_context.value == 0 && last_context_source == 0);
}
static void check_shortcuts(xxwidgets_app *app, xxwidgets_widget *window, xxwidgets_widget *edit)
{
    const xxwidgets_shortcut bindings[] = {{"Ctrl+Shift+O", 73}, {"F8", 74}};
    HWND input = (HWND)xxwidgets_widget_native_handle(edit);
    BYTE saved[256], keys[256];
    int before = shortcuts;
    CHECK(xxwidgets_widget_set_visible(window, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_focus(edit) == XXWIDGETS_OK);
    CHECK(xxwidgets_window_set_shortcuts(window, bindings, 2) == XXWIDGETS_OK);
    CHECK(GetKeyboardState(saved)); memset(keys, 0, sizeof(keys)); CHECK(SetKeyboardState(keys));
    CHECK(PostMessageW(input, WM_KEYDOWN, VK_F8, 1));
    CHECK(xxwidgets_app_poll(app, 0) == XXWIDGETS_OK);
    CHECK(shortcuts == before + 1 && shortcut_action == 74);
    keys[VK_CONTROL] = keys[VK_SHIFT] = 0x80; CHECK(SetKeyboardState(keys));
    CHECK(PostMessageW(input, WM_KEYDOWN, 'O', 1));
    CHECK(xxwidgets_app_poll(app, 0) == XXWIDGETS_OK);
    CHECK(shortcuts == before + 2 && shortcut_action == 73);
    memset(keys, 0, sizeof(keys)); CHECK(SetKeyboardState(keys));
    CHECK(xxwidgets_window_set_shortcuts(window, NULL, 0) == XXWIDGETS_OK);
    CHECK(PostMessageW(input, WM_KEYDOWN, VK_F8, 1));
    CHECK(xxwidgets_app_poll(app, 0) == XXWIDGETS_OK && shortcuts == before + 2);
    CHECK(SetKeyboardState(saved));
}

int main(void)
{
    xxwidgets_config config = {XXWIDGETS_BACKEND_NATIVE, on_event, NULL};
    xxwidgets_app *app = NULL;
    xxwidgets_widget *window, *button, *edit, *checkbox, *list, *progress;
    HWND hwnd, control;
    WCHAR class_name[32];
    char text[64];
    int value;
    CHECK(xxwidgets_app_create(&config, &app) == XXWIDGETS_OK);
    CHECK(xxwidgets_app_backend(app) == XXWIDGETS_BACKEND_NATIVE);
    window = make(app, NULL, XXWIDGETS_WINDOW, "xxwidgets test");
    CHECK(xxwidgets_widget_set_visible(window, 0) == XXWIDGETS_OK);
    hwnd = (HWND)xxwidgets_widget_native_handle(window);
    CHECK(IsWindow(hwnd));
    button = make(app, window, XXWIDGETS_BUTTON, "Push");
    edit = make(app, window, XXWIDGETS_EDIT, "initial");
    checkbox = make(app, window, XXWIDGETS_CHECKBOX, "check");
    list = make(app, window, XXWIDGETS_LISTBOX, "");
    progress = make(app, window, XXWIDGETS_PROGRESS, "");
    make(app, window, XXWIDGETS_LABEL, "label");
    control = (HWND)xxwidgets_widget_native_handle(button);
    CHECK(GetClassNameW(control, class_name, 32) && _wcsicmp(class_name, L"BUTTON") == 0);
    CHECK(xxwidgets_widget_set_text(edit, "Gr\xc3\xbc\xc3\x9f" "e") == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_get_text(edit, text, sizeof(text), NULL) == XXWIDGETS_OK);
    CHECK(strcmp(text, "Gr\xc3\xbc\xc3\x9f" "e") == 0);
    {
        DWORD start, end;
        HWND edit_handle = (HWND)xxwidgets_widget_native_handle(edit);
        SendMessageW(edit_handle, EM_SETSEL, 1, 3);
        CHECK(xxwidgets_widget_set_enabled(edit, 1) == XXWIDGETS_OK);
        SendMessageW(edit_handle, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
        CHECK(start == 1 && end == 3);
        CHECK(xxwidgets_widget_set_visible(button, 0) == XXWIDGETS_OK);
        CHECK(xxwidgets_widget_set_visible(window, 1) == XXWIDGETS_OK);
        CHECK(!(GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE));
        CHECK(xxwidgets_widget_set_visible(window, 0) == XXWIDGETS_OK);
        CHECK(xxwidgets_widget_set_visible(button, 1) == XXWIDGETS_OK);
    }
    CHECK(xxwidgets_widget_set_value(checkbox, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_get_value(checkbox, &value) == XXWIDGETS_OK && value == 1);
    CHECK(xxwidgets_listbox_add(list, "one") == XXWIDGETS_OK);
    CHECK(xxwidgets_listbox_add(list, "two") == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_set_value(list, 1) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_get_value(list, &value) == XXWIDGETS_OK && value == 1);
    CHECK(xxwidgets_listbox_add(list, "three") == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_get_value(list, &value) == XXWIDGETS_OK && value == 1);
    CHECK(xxwidgets_widget_set_value(progress, 60) == XXWIDGETS_OK);
    CHECK(xxwidgets_widget_get_value(progress, &value) == XXWIDGETS_OK && value == 60);
    CHECK(xxwidgets_widget_set_enabled(button, 0) == XXWIDGETS_OK && !IsWindowEnabled(control));
    CHECK(xxwidgets_widget_set_enabled(button, 1) == XXWIDGETS_OK);
    CHECK(clicks == 0 && changes == 0 && selections == 0);
    SendMessageW(control, BM_CLICK, 0, 0);
    CHECK(clicks == 1);
    CHECK(SetWindowTextW((HWND)xxwidgets_widget_native_handle(edit), L"edited \x03bb"));
    CHECK(changes == 1);
    CHECK(xxwidgets_widget_get_text(edit, text, sizeof(text), NULL) == XXWIDGETS_OK && strcmp(text, "edited \xce\xbb") == 0);
    control = (HWND)xxwidgets_widget_native_handle(list);
    SendMessageW(control, LB_SETCURSEL, 0, 0);
    SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(control), LBN_SELCHANGE), (LPARAM)control);
    CHECK(selections == 1);
    CHECK(xxwidgets_widget_get_value(list, &value) == XXWIDGETS_OK && value == 0);
    check_about_dialog(window);
    check_hexview(app, window, hwnd);
    check_archiveview(app, window, hwnd);
    check_archivebrowser(app, window);
    check_archivebrowser_advanced(app, window);
    check_archivebrowser_multiselect(app, window);
    check_archivebrowser_context(app, window);
    check_archivebrowser_shift_f10(app, window);
    check_options_dialog(window);
    check_process_dialog(window);
    check_shortcuts(app, window, edit);
    check_comboboxes(app, window);
    CHECK(xxwidgets_app_poll(app, 0) == XXWIDGETS_OK);
    CHECK(PostMessageW(hwnd, WM_CLOSE, 0, 0));
    CHECK(xxwidgets_app_run(app) == 7 && closes == 1);
    CHECK(xxwidgets_app_destroy(app) == XXWIDGETS_OK);
    CHECK(!IsWindow(hwnd));
    puts("xxwidgets WinAPI tests passed");
    return 0;
}
