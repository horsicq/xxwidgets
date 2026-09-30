#ifndef XXWIDGETS_DEMO_COMMON_H
#define XXWIDGETS_DEMO_COMMON_H

#include "xxwidgets/xxwidgets.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef struct demo_arguments {
    xxwidgets_backend backend;
    int smoke;
} demo_arguments;

/* Returns 1 to run, 0 for help, -1 for invalid arguments. */
static inline int demo_parse(int argc, char **argv, const char *name, demo_arguments *arguments)
{
    int i;
    memset(arguments, 0, sizeof(*arguments));
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--native")) arguments->backend = XXWIDGETS_BACKEND_NATIVE;
        else if (!strcmp(argv[i], "--tui")) arguments->backend = XXWIDGETS_BACKEND_TUI;
        else if (!strcmp(argv[i], "--smoke-test")) arguments->smoke = 1;
        else {
            int help = !strcmp(argv[i], "--help");
            fprintf(help ? stdout : stderr, "Usage: %s [--native|--tui] [--smoke-test]\n", name);
            return help ? 0 : -1;
        }
    }
    return 1;
}

static inline int demo_check(xxwidgets_status status)
{
    if (status == XXWIDGETS_OK) return 1;
    fprintf(stderr, "xxwidgets: %s\n", xxwidgets_status_string(status));
    return 0;
}

static inline xxwidgets_widget *demo_widget(xxwidgets_app *app, xxwidgets_widget *parent,
    xxwidgets_kind kind, const char *text, int x, int y, int width, int height)
{
    xxwidgets_widget *widget = NULL;
    xxwidgets_rect bounds = {x, y, width, height};
    if (!demo_check(xxwidgets_widget_create(app, parent, kind, text, bounds, &widget))) return NULL;
    return widget;
}

static inline int demo_finish(xxwidgets_app *app, int result)
{
    if (!demo_check(xxwidgets_app_destroy(app))) return 1;
    return result < 0 ? 1 : result;
}

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/* Only the explicitly requested smoke check drives its own modal window.
 * Interactive examples use only the public xxwidgets dialog APIs. */
static HWND demo_smoke_owner;
static xxwidgets_app *demo_smoke_app;
static UINT_PTR demo_smoke_timer;
static int demo_smoke_accept, demo_smoke_done, demo_smoke_ticks;

static BOOL CALLBACK demo_smoke_children(HWND child, LPARAM data)
{
    HWND *controls = (HWND *)data;
    wchar_t text[32], class_name[32];
    GetClassNameW(child, class_name, 32);
    if (wcscmp(class_name, L"Button")) return TRUE;
    GetWindowTextW(child, text, 32);
    if (!wcscmp(text, L"OK")) controls[1] = child;
    if ((GetWindowLongPtrW(child, GWL_STYLE) & BS_TYPEMASK) == BS_AUTOCHECKBOX && !controls[0])
        controls[0] = child;
    return TRUE;
}

static BOOL CALLBACK demo_smoke_windows(HWND window, LPARAM data)
{
    HWND controls[2] = {0};
    wchar_t owner_class[128], window_class[128];
    (void)data;
    if (GetWindow(window, GW_OWNER) != demo_smoke_owner) return TRUE;
    if (!GetClassNameW(demo_smoke_owner, owner_class, 128) ||
        !GetClassNameW(window, window_class, 128) || wcscmp(owner_class, window_class)) return TRUE;
    if (demo_smoke_accept) {
        EnumChildWindows(window, demo_smoke_children, (LPARAM)controls);
        if (!controls[1]) return TRUE;
        if (controls[0]) SendMessageW(controls[0], BM_CLICK, 0, 0);
        SendMessageW(controls[1], BM_CLICK, 0, 0);
    } else if (!PostMessageW(window, WM_CLOSE, 0, 0)) return TRUE;
    demo_smoke_done = 1;
    KillTimer(NULL, demo_smoke_timer); demo_smoke_timer = 0;
    return FALSE;
}

static VOID CALLBACK demo_smoke_tick(HWND window, UINT message, UINT_PTR timer, DWORD time)
{
    (void)window; (void)message; (void)timer; (void)time;
    EnumThreadWindows(GetCurrentThreadId(), demo_smoke_windows, 0);
    if (!demo_smoke_done && ++demo_smoke_ticks > 100) {
        KillTimer(NULL, demo_smoke_timer); demo_smoke_timer = 0;
        xxwidgets_app_quit(demo_smoke_app, 1);
    }
}

static inline int demo_modal_smoke_begin(xxwidgets_app *app, xxwidgets_widget *owner, int accept)
{
    demo_smoke_owner = (HWND)xxwidgets_widget_native_handle(owner);
    demo_smoke_app = app;
    demo_smoke_accept = accept;
    demo_smoke_done = demo_smoke_ticks = 0;
    if (!demo_smoke_owner) return 0;
    demo_smoke_timer = SetTimer(NULL, 0, 20, demo_smoke_tick);
    return demo_smoke_timer != 0;
}

static inline int demo_modal_smoke_finish(void)
{
    if (demo_smoke_timer) KillTimer(NULL, demo_smoke_timer);
    demo_smoke_timer = 0;
    return demo_smoke_done;
}
#else
static inline int demo_modal_smoke_begin(xxwidgets_app *app, xxwidgets_widget *owner, int accept)
{
    (void)app; (void)owner; (void)accept;
    fprintf(stderr, "Modal smoke checks require the Win32 native backend.\n");
    return 0;
}
static inline int demo_modal_smoke_finish(void) { return 0; }
#endif
#endif
