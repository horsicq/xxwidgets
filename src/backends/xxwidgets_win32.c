/* Native Windows controls. Public text remains UTF-8; all WinAPI calls use UTF-16. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "../xxwidgets_internal.h"

typedef struct xxwidgets_win32_app {
    HINSTANCE instance;
    HFONT font;
    HFONT monospace_font;
    int owns_font;
    int owns_monospace_font;
    HFONT role_fonts[XXWIDGETS_FONT_ROLE_COUNT];
    int owns_role_font[XXWIDGETS_FONT_ROLE_COUNT];
    int cell_width;
    int cell_height;
    ATOM window_class;
    wchar_t class_name[80];
    ATOM browser_class;
    wchar_t browser_class_name[80];
} xxwidgets_win32_app;

typedef struct win32_font_layout {
    struct { HWND handle; RECT rect; int extra_height; } children[40];
    HWND last_focus;
    size_t count;
    int width, height, x, y, client_width, client_height, step_x, step_y;
    int updating;
} win32_font_layout;

typedef struct xxwidgets_win32_widget {
    xxwidgets_rect applied_rect;
    size_t list_count;
    uint64_t hex_revision;
    int list_initialized;
    HWND browser_up;
    HWND browser_address;
    HWND browser_list;
    HIMAGELIST browser_images;
    uint64_t browser_revision;
    int browser_column_syncing;
    int browser_name_manual;
    int browser_viewport_width;
    int browser_context_key;
    HBITMAP about_bitmap;
    HWND about_edit;
    HFONT preview_font;
    win32_font_layout *font_layout;
    HWND combo_popup, combo_list;
    uint64_t combo_revision;
    int combo_initialized;
    int combo_skip_mouse_open;
    uint64_t scan_revision;
    HTREEITEM *tree_items;
    size_t tree_count;
    uint64_t tree_content_revision;
    int tree_initialized;
} xxwidgets_win32_widget;

static xxwidgets_status win32_sync(xxwidgets_widget *widget);
static xxwidgets_status win32_read_text(xxwidgets_widget *widget);
static xxwidgets_status win32_read_value(xxwidgets_widget *widget);
static xxwidgets_status win32_checkcombo_open(xxwidgets_widget *widget);
static LRESULT CALLBACK win32_checkcombo_control_proc(HWND handle, UINT message, WPARAM wp, LPARAM lp,
    UINT_PTR id, DWORD_PTR data);
static xxwidgets_status win32_sync_combo(xxwidgets_widget *widget);
static xxwidgets_status win32_scanresults_columns(xxwidgets_widget *widget);
static LRESULT CALLBACK win32_tree_proc(HWND handle, UINT message, WPARAM wp, LPARAM lp,
    UINT_PTR id, DWORD_PTR data);
static void win32_tree_expansion_input(xxwidgets_widget *widget, size_t index,
    HTREEITEM item, int expanded);
static xxwidgets_status win32_apply_fonts(xxwidgets_app *app, const xxwidgets_font_options *options);
static xxwidgets_status win32_choose_font(xxwidgets_widget *owner, xxwidgets_font_role role,
    xxwidgets_font *font, int *accepted);
static xxwidgets_status win32_preview_font(xxwidgets_widget *widget, xxwidgets_font_role role,
    const xxwidgets_font *font);
static LRESULT CALLBACK win32_browser_proc(HWND handle, UINT message, WPARAM wp, LPARAM lp);

#define XXWIDGETS_BROWSER_UP_ID 1
#define XXWIDGETS_BROWSER_ADDRESS_ID 2
#define XXWIDGETS_BROWSER_LIST_ID 3

static void win32_browser_context(xxwidgets_widget *widget, LPARAM position)
{
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
    HWND list;
    POINT point = {(LONG)(short)LOWORD(position), (LONG)(short)HIWORD(position)};
    POINT screen_point = point;
    RECT client;
    LVITEMW item;
    int row = -1;
    int keyboard = point.x == -1 && point.y == -1;
    if (!native || !native->browser_list || !widget->enabled || !widget->visible ||
        (widget->parent && (!widget->parent->enabled || !widget->parent->visible)) ||
        widget->app->syncing || widget->app->quit)
        return;
    list = native->browser_list;
    if (!GetClientRect(list, &client)) return;
    if (keyboard) {
        row = (int)SendMessageW(list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_FOCUSED);
        if (row < 0) row = (int)SendMessageW(list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    } else {
        LVHITTESTINFO hit;
        if (!ScreenToClient(list, &point)) return;
        memset(&hit, 0, sizeof(hit));
        hit.pt = point;
        if (PtInRect(&client, point))
            row = (int)SendMessageW(list, LVM_SUBITEMHITTEST, 0, (LPARAM)&hit);
    }
    if (row >= 0 && (size_t)row >= xxwidgets_archivebrowser_visible_count(widget)) row = -1;
    /* Right-click within a selection preserves the group. Clicking a different
     * row replaces it; empty space clears it. Publish the final selection once. */
    ++widget->app->syncing;
    memset(&item, 0, sizeof(item));
    item.stateMask = LVIS_FOCUSED;
    SendMessageW(list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&item);
    if (row < 0 || !xxwidgets_archivebrowser_row_selected(widget, (size_t)row)) {
        item.stateMask = LVIS_SELECTED;
        SendMessageW(list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&item);
        xxwidgets_archivebrowser_selection_clear(widget);
    }
    if (row >= 0) {
        item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        item.state = LVIS_SELECTED | LVIS_FOCUSED;
        SendMessageW(list, LVM_SETITEMSTATE, (WPARAM)row, (LPARAM)&item);
        xxwidgets_archivebrowser_selection_input(widget, (size_t)row, 1);
    }
    widget->value = row;
    --widget->app->syncing;
    xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, row);
    /* SELECT callbacks may replace the contents; use the resulting selection. */
    row = (int)SendMessageW(list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_FOCUSED);
    if (row < 0) row = (int)SendMessageW(list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    widget->value = row;
    if (keyboard) {
        RECT bounds;
        point.x = 8;
        point.y = 8;
        if (row >= 0) {
            SendMessageW(list, LVM_ENSUREVISIBLE, (WPARAM)row, FALSE);
            memset(&bounds, 0, sizeof(bounds));
            bounds.left = LVIR_BOUNDS;
            if (SendMessageW(list, LVM_GETITEMRECT, (WPARAM)row, (LPARAM)&bounds)) {
                point.x = bounds.left + 24;
                point.y = bounds.bottom;
            }
        } else {
            HWND header = (HWND)SendMessageW(list, LVM_GETHEADER, 0, 0);
            if (header && GetWindowRect(header, &bounds)) {
                POINT bottom = {bounds.left, bounds.bottom};
                if (ScreenToClient(list, &bottom)) point.y = bottom.y + 8;
            }
        }
        if (point.x >= client.right) point.x = client.right > 0 ? client.right - 1 : 0;
        if (point.y >= client.bottom) point.y = client.bottom > 0 ? client.bottom - 1 : 0;
        if (point.x < 0) point.x = 0;
        if (point.y < 0) point.y = 0;
    }
    if (!widget->enabled || !widget->visible ||
        (widget->parent && (!widget->parent->enabled || !widget->parent->visible))) return;
    if (keyboard) {
        if (!ClientToScreen(list, &point)) return;
    } else point = screen_point;
    if (!ScreenToClient((HWND)widget->native, &point)) return;
    xxwidgets_emit_context(widget, row, (int)point.x, (int)point.y);
}

static void win32_browser_fit_columns(xxwidgets_widget *widget, int viewport_changed)
{
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
    xxwidgets_win32_app *state = (xxwidgets_win32_app *)widget->app->platform;
    RECT client;
    int other_width = 0, name_width, column;
    if (!native || !native->browser_list || native->browser_column_syncing) return;
    if (!GetClientRect(native->browser_list, &client)) return;
    if (viewport_changed && client.right - client.left != native->browser_viewport_width)
        native->browser_name_manual = 0;
    native->browser_viewport_width = client.right - client.left;
    if (native->browser_name_manual) return;
    for (column = 1; column < (int)xxwidgets_archivebrowser_column_count(widget); ++column)
        other_width += (int)SendMessageW(native->browser_list, LVM_GETCOLUMNWIDTH, (WPARAM)column, 0);
    name_width = client.right - client.left - other_width;
    if (name_width < 24 * state->cell_width) name_width = 24 * state->cell_width;
    if ((int)SendMessageW(native->browser_list, LVM_GETCOLUMNWIDTH, 0, 0) != name_width) {
        native->browser_column_syncing = 1;
        SendMessageW(native->browser_list, LVM_SETCOLUMNWIDTH, 0, (LPARAM)name_width);
        native->browser_column_syncing = 0;
    }
}

static void win32_browser_layout(xxwidgets_widget *widget)
{
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
    xxwidgets_win32_app *state = (xxwidgets_win32_app *)widget->app->platform;
    RECT client;
    int bar_height = state->cell_height + 4;
    int up_width = bar_height + 6;
    int width, height;
    if (!native || !GetClientRect((HWND)widget->native, &client)) return;
    width = client.right - client.left;
    height = client.bottom - client.top;
    if (bar_height < 24) bar_height = 24;
    if (bar_height > height) bar_height = height;
    if (up_width > width) up_width = width;
    if (native->browser_up)
        MoveWindow(native->browser_up, 0, 0, up_width, bar_height, TRUE);
    if (native->browser_address)
        MoveWindow(native->browser_address, up_width + 2, 0,
                   width > up_width + 2 ? width - up_width - 2 : 0, bar_height, TRUE);
    if (native->browser_list)
        MoveWindow(native->browser_list, 0, bar_height + 3, width,
                   height > bar_height + 3 ? height - bar_height - 3 : 0, TRUE);
}

static LRESULT CALLBACK win32_browser_list_proc(HWND handle, UINT message, WPARAM wp,
                                               LPARAM lp, UINT_PTR id, DWORD_PTR data)
{
    xxwidgets_widget *widget = (xxwidgets_widget *)data;
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
    (void)id;
    /* Preserve normal Tab navigation while keeping Enter/Backspace in the browser. */
    if (message == WM_GETDLGCODE && lp) {
        const MSG *key = (const MSG *)lp;
        if ((key->message == WM_KEYDOWN && (key->wParam == VK_RETURN || key->wParam == VK_BACK)) ||
            ((key->message == WM_KEYDOWN || key->message == WM_SYSKEYDOWN) &&
             (key->wParam == VK_APPS || (key->wParam == VK_F10 && GetKeyState(VK_SHIFT) < 0))))
            return DefSubclassProc(handle, message, wp, lp) | DLGC_WANTMESSAGE;
    }
    if (message == WM_CONTEXTMENU) {
        win32_browser_context(widget, lp);
        return 0;
    }
    if ((message == WM_KEYUP || message == WM_SYSKEYUP) && native &&
        native->browser_context_key == (int)wp) {
        native->browser_context_key = 0;
        return 0;
    }
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) &&
        !widget->app->syncing && widget->enabled) {
        if (message == WM_KEYDOWN && wp == 'A' && GetKeyState(VK_CONTROL) < 0) {
            if (xxwidgets_archivebrowser_select_all(widget) == XXWIDGETS_OK)
                xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, widget->value);
            return 0;
        }
        if (wp == VK_APPS || (wp == VK_F10 && GetKeyState(VK_SHIFT) < 0)) {
            if (native) native->browser_context_key = (int)wp;
            if (!(lp & ((LPARAM)1 << 30))) win32_browser_context(widget, (LPARAM)-1);
            return 0;
        }
        if (native && native->browser_context_key == (int)wp) native->browser_context_key = 0;
        if (message == WM_KEYDOWN && wp == VK_RETURN) {
            int row = (int)SendMessageW(handle, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_FOCUSED);
            if (row < 0) row = (int)SendMessageW(handle, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
            if (row >= 0) xxwidgets_archivebrowser_user_activate(widget, (size_t)row);
            return 0;
        }
        if (message == WM_KEYDOWN && wp == VK_BACK) {
            xxwidgets_archivebrowser_user_up(widget);
            return 0;
        }
    }
    if (message == WM_SIZE) {
        LRESULT result = DefSubclassProc(handle, message, wp, lp);
        win32_browser_fit_columns(widget, 1);
        return result;
    }
    if (message == WM_NOTIFY && native && lp && !native->browser_column_syncing) {
        NMHDR *notification = (NMHDR *)lp;
        if (notification->code == HDN_BEGINTRACKW || notification->code == HDN_BEGINTRACKA) {
            const NMHEADERW *heading = (const NMHEADERW *)lp;
            if (heading->iItem == 0) native->browser_name_manual = 1;
        }
        if (notification->code == HDN_ENDTRACKW || notification->code == HDN_ENDTRACKA) {
            const NMHEADERW *heading = (const NMHEADERW *)lp;
            LRESULT result = DefSubclassProc(handle, message, wp, lp);
            if (heading->iItem == 0) native->browser_name_manual = 1;
            else win32_browser_fit_columns(widget, 0);
            return result;
        }
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(handle, win32_browser_list_proc, 1);
    return DefSubclassProc(handle, message, wp, lp);
}

static LRESULT CALLBACK win32_browser_proc(HWND handle, UINT message, WPARAM wp, LPARAM lp)
{
    xxwidgets_widget *widget = (xxwidgets_widget *)GetWindowLongPtrW(handle, GWLP_USERDATA);
    xxwidgets_win32_widget *native;
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW *creation = (const CREATESTRUCTW *)lp;
        widget = (xxwidgets_widget *)creation->lpCreateParams;
        SetWindowLongPtrW(handle, GWLP_USERDATA, (LONG_PTR)widget);
        widget->native = handle;
    }
    if (!widget) return DefWindowProcW(handle, message, wp, lp);
    native = (xxwidgets_win32_widget *)widget->platform;
    switch (message) {
    case WM_SIZE:
        win32_browser_layout(widget);
        return 0;
    case WM_SETFOCUS:
        if (native && native->browser_list) SetFocus(native->browser_list);
        return 0;
    case WM_CONTEXTMENU:
        if (!native || (HWND)wp == handle || (HWND)wp == native->browser_list) {
            win32_browser_context(widget, lp);
            return 0;
        }
        break;
    case WM_COMMAND:
        if (native && (HWND)lp == native->browser_up && HIWORD(wp) == BN_CLICKED &&
            !widget->app->syncing && widget->enabled) {
            xxwidgets_archivebrowser_user_up(widget);
            if (native->browser_list) SetFocus(native->browser_list);
            return 0;
        }
        break;
    case WM_NOTIFY:
        if (native && lp && ((NMHDR *)lp)->hwndFrom == native->browser_list &&
            !widget->app->syncing && widget->enabled) {
            NMHDR *notification = (NMHDR *)lp;
            if (notification->code == LVN_ITEMCHANGED) {
                const NMLISTVIEW *change = (const NMLISTVIEW *)lp;
                if ((change->uChanged & LVIF_STATE) && change->iItem >= 0) {
                    int selected, changed = !!((change->uOldState ^ change->uNewState) & LVIS_SELECTED);
                    /* Focus-only notifications need not carry the unchanged
                     * selected bit. Keep the group when focus moves within it. */
                    if (changed)
                        xxwidgets_archivebrowser_selection_input(widget, (size_t)change->iItem,
                            !!(change->uNewState & LVIS_SELECTED));
                    selected = (int)SendMessageW(native->browser_list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_FOCUSED);
                    if (selected < 0 || !xxwidgets_archivebrowser_row_selected(widget, (size_t)selected))
                        selected = (int)SendMessageW(native->browser_list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
                    if (widget->value != selected || changed) {
                        widget->value = selected;
                        xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, selected);
                    }
                }
                return 0;
            }
            if (notification->code == NM_DBLCLK) {
                const NMITEMACTIVATE *activation = (const NMITEMACTIVATE *)lp;
                if (activation->iItem >= 0)
                    xxwidgets_archivebrowser_user_activate(widget, (size_t)activation->iItem);
                return 0;
            }
            if (notification->code == LVN_COLUMNCLICK) {
                const NMLISTVIEW *column = (const NMLISTVIEW *)lp;
                if (column->iSubItem >= 0 &&
                    (size_t)column->iSubItem < xxwidgets_archivebrowser_column_count(widget))
                    xxwidgets_archivebrowser_user_sort(widget, (xxwidgets_archive_column)column->iSubItem);
                return 0;
            }
        }
        break;
    case WM_NCDESTROY:
        widget->native = NULL;
        SetWindowLongPtrW(handle, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcW(handle, message, wp, lp);
}

static xxwidgets_widget *win32_find_widget(xxwidgets_app *app, HWND handle)
{
    xxwidgets_widget *widget;
    for (widget = app->widgets; widget; widget = widget->next)
        if (widget->native == handle) return widget;
    return NULL;
}

static xxwidgets_status win32_wide(const char *text, wchar_t **result)
{
    int length;
    wchar_t *wide;
    *result = NULL;
    if (strlen(text) >= INT_MAX) return XXWIDGETS_INVALID_ARGUMENT;
    length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (!length) return XXWIDGETS_INVALID_ARGUMENT;
    wide = (wchar_t *)malloc((size_t)length * sizeof(*wide));
    if (!wide) return XXWIDGETS_OUT_OF_MEMORY;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, length)) {
        free(wide);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    *result = wide;
    return XXWIDGETS_OK;
}

static HFONT win32_default_role_font(xxwidgets_win32_app *app, xxwidgets_font_role role)
{
    return role == XXWIDGETS_FONT_TABLE_VIEWS ? app->monospace_font : app->font;
}

static xxwidgets_status win32_font_logfont(xxwidgets_win32_app *app, xxwidgets_font_role role,
    const xxwidgets_font *font, LOGFONTW *logfont)
{
    HDC dc;
    int dpi;
    if (!GetObjectW(win32_default_role_font(app, role), sizeof(*logfont), logfont))
        return XXWIDGETS_PLATFORM_ERROR;
    if (font->family[0]) {
        wchar_t *family;
        xxwidgets_status status = win32_wide(font->family, &family);
        if (status != XXWIDGETS_OK) return status;
        if (wcslen(family) >= LF_FACESIZE) { free(family); return XXWIDGETS_INVALID_ARGUMENT; }
        memset(logfont->lfFaceName, 0, sizeof(logfont->lfFaceName));
        memcpy(logfont->lfFaceName, family, (wcslen(family) + 1) * sizeof(*family));
        free(family);
    }
    dc = GetDC(NULL); if (!dc) return XXWIDGETS_PLATFORM_ERROR;
    dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(NULL, dc);
    if (font->point_size) logfont->lfHeight = -MulDiv((int)font->point_size, dpi, 72);
    logfont->lfWeight = font->bold ? FW_BOLD : FW_NORMAL;
    logfont->lfItalic = (BYTE)font->italic; logfont->lfCharSet = DEFAULT_CHARSET;
    return XXWIDGETS_OK;
}

static xxwidgets_status win32_make_font(xxwidgets_win32_app *app, xxwidgets_font_role role,
    const xxwidgets_font *font, HFONT *handle, int *owned)
{
    LOGFONTW logfont;
    xxwidgets_status status;
    *owned = 0;
    if (!font->family[0] && !font->point_size && !font->bold && !font->italic) {
        *handle = win32_default_role_font(app, role); return XXWIDGETS_OK;
    }
    status = win32_font_logfont(app, role, font, &logfont);
    if (status != XXWIDGETS_OK) return status;
    *handle = CreateFontIndirectW(&logfont);
    if (!*handle) return XXWIDGETS_PLATFORM_ERROR;
    *owned = 1; return XXWIDGETS_OK;
}

static void win32_apply_widget_font(xxwidgets_widget *widget)
{
    xxwidgets_win32_app *app = (xxwidgets_win32_app *)widget->app->platform;
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
    HFONT font = native && native->preview_font ? native->preview_font :
        app->role_fonts[xxwidgets_widget_font_role(widget)];
    if (widget->native) SendMessageW((HWND)widget->native, WM_SETFONT, (WPARAM)font, TRUE);
    if (!native) return;
    if (native->browser_up) SendMessageW(native->browser_up, WM_SETFONT, (WPARAM)app->role_fonts[XXWIDGETS_FONT_CONTROLS], TRUE);
    if (native->browser_address) SendMessageW(native->browser_address, WM_SETFONT, (WPARAM)app->role_fonts[XXWIDGETS_FONT_CONTROLS], TRUE);
    if (native->browser_list) SendMessageW(native->browser_list, WM_SETFONT, (WPARAM)app->role_fonts[XXWIDGETS_FONT_TABLE_VIEWS], TRUE);
    if (native->combo_list) SendMessageW(native->combo_list, WM_SETFONT, (WPARAM)app->role_fonts[XXWIDGETS_FONT_CONTROLS], TRUE);
    if (native->about_edit) SendMessageW(native->about_edit, WM_SETFONT, (WPARAM)app->role_fonts[XXWIDGETS_FONT_TEXT_EDITS], TRUE);
    if (widget->kind == XXWIDGETS_SCANRESULTS || native->browser_list) {
        HWND list = native->browser_list ? native->browser_list : (HWND)widget->native;
        HWND header = (HWND)SendMessageW(list, LVM_GETHEADER, 0, 0);
        if (header) SendMessageW(header, WM_SETFONT, (WPARAM)app->role_fonts[XXWIDGETS_FONT_TABLE_VIEWS], TRUE);
    }
}

static xxwidgets_status win32_apply_fonts(xxwidgets_app *app, const xxwidgets_font_options *options)
{
    xxwidgets_win32_app *native = (xxwidgets_win32_app *)app->platform;
    HFONT fonts[XXWIDGETS_FONT_ROLE_COUNT] = {0}, previous[XXWIDGETS_FONT_ROLE_COUNT];
    int owned[XXWIDGETS_FONT_ROLE_COUNT] = {0}, previous_owned[XXWIDGETS_FONT_ROLE_COUNT];
    xxwidgets_widget *widget;
    size_t role;
    xxwidgets_status status;
    for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role) {
        status = win32_make_font(native, (xxwidgets_font_role)role, &options->fonts[role], &fonts[role], &owned[role]);
        if (status != XXWIDGETS_OK) {
            size_t i;
            for (i = 0; i < role; ++i) if (owned[i]) DeleteObject(fonts[i]);
            return status;
        }
    }
    memcpy(previous, native->role_fonts, sizeof(previous));
    memcpy(previous_owned, native->owns_role_font, sizeof(previous_owned));
    memcpy(native->role_fonts, fonts, sizeof(fonts));
    memcpy(native->owns_role_font, owned, sizeof(owned));
    for (widget = app->widgets; widget; widget = widget->next) win32_apply_widget_font(widget);
    for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role)
        if (previous_owned[role]) DeleteObject(previous[role]);
    return XXWIDGETS_OK;
}

static xxwidgets_status win32_preview_font(xxwidgets_widget *widget, xxwidgets_font_role role,
    const xxwidgets_font *font)
{
    xxwidgets_win32_app *app = (xxwidgets_win32_app *)widget->app->platform;
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
    HFONT handle, previous;
    int owned;
    xxwidgets_status status = win32_make_font(app, role, font, &handle, &owned);
    if (status != XXWIDGETS_OK) return status;
    /* Even defaults need a local copy: application font replacement must not
     * invalidate a preview while its options dialog is open. */
    if (!owned) {
        LOGFONTW logfont;
        if (!GetObjectW(handle, sizeof(logfont), &logfont)) return XXWIDGETS_PLATFORM_ERROR;
        handle = CreateFontIndirectW(&logfont);
        if (!handle) return XXWIDGETS_PLATFORM_ERROR;
    }
    previous = native->preview_font; native->preview_font = handle;
    SendMessageW((HWND)widget->native, WM_SETFONT, (WPARAM)handle, TRUE);
    if (previous) DeleteObject(previous);
    return XXWIDGETS_OK;
}

static xxwidgets_status win32_choose_font(xxwidgets_widget *owner, xxwidgets_font_role role,
    xxwidgets_font *font, int *accepted)
{
    xxwidgets_win32_app *app = (xxwidgets_win32_app *)owner->app->platform;
    LOGFONTW logfont;
    CHOOSEFONTW dialog;
    xxwidgets_font copied = {0};
    xxwidgets_status status = win32_font_logfont(app, role, font, &logfont);
    if (status != XXWIDGETS_OK) return status;
    memset(&dialog, 0, sizeof(dialog)); dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = (HWND)owner->native; dialog.lpLogFont = &logfont;
    dialog.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_LIMITSIZE | CF_NOVERTFONTS;
    dialog.nSizeMin = 4; dialog.nSizeMax = 96;
    if (!ChooseFontW(&dialog)) return CommDlgExtendedError() ? XXWIDGETS_PLATFORM_ERROR : XXWIDGETS_OK;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, logfont.lfFaceName, -1,
        copied.family, sizeof(copied.family), NULL, NULL)) return XXWIDGETS_PLATFORM_ERROR;
    copied.point_size = (unsigned int)((dialog.iPointSize + 5) / 10);
    copied.bold = logfont.lfWeight >= FW_BOLD; copied.italic = !!logfont.lfItalic;
    *font = copied; *accepted = 1; return XXWIDGETS_OK;
}

static int win32_font_metrics(HWND window, HFONT font, TEXTMETRICW *metrics)
{
    HDC dc = GetDC(window);
    HGDIOBJ previous;
    int result;
    if (!dc) return 0;
    previous = SelectObject(dc, font);
    result = previous && previous != HGDI_ERROR && GetTextMetricsW(dc, metrics);
    if (previous && previous != HGDI_ERROR) SelectObject(dc, previous);
    ReleaseDC(window, dc);
    return result;
}

static xxwidgets_status win32_font_layout_viewport(xxwidgets_widget *widget, int force)
{
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
    win32_font_layout *layout = native->font_layout;
    HWND window = (HWND)widget->native;
    RECT client;
    int pass, old_x = layout->x, old_y = layout->y, changed;
    size_t i;
    xxwidgets_status status = XXWIDGETS_OK;
    if (layout->updating) return XXWIDGETS_OK;
    if (!GetClientRect(window, &client)) return XXWIDGETS_PLATFORM_ERROR;
    changed = force || client.right != layout->client_width || client.bottom != layout->client_height;
    if (!changed) return XXWIDGETS_OK;
    layout->updating = 1; ++widget->app->syncing;
    /* One scrollbar can reduce the other dimension. Recheck the viewport after
     * each change before clamping offsets and positioning the cached children. */
    for (pass = 0; pass < 3; ++pass) {
        SCROLLINFO info = {sizeof(info)};
        RECT next;
        int max_x = layout->width > client.right ? layout->width - client.right : 0;
        int max_y = layout->height > client.bottom ? layout->height - client.bottom : 0;
        if (layout->x < 0) layout->x = 0;
        if (layout->x > max_x) layout->x = max_x;
        if (layout->y < 0) layout->y = 0;
        if (layout->y > max_y) layout->y = max_y;
        info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        info.nMax = layout->width - 1; info.nPage = (UINT)client.right; info.nPos = layout->x;
        SetScrollInfo(window, SB_HORZ, &info, TRUE);
        info.nMax = layout->height - 1; info.nPage = (UINT)client.bottom; info.nPos = layout->y;
        SetScrollInfo(window, SB_VERT, &info, TRUE);
        if (!GetClientRect(window, &next)) { status = XXWIDGETS_PLATFORM_ERROR; break; }
        if (next.right == client.right && next.bottom == client.bottom) break;
        client = next;
    }
    changed |= old_x != layout->x || old_y != layout->y;
    if (status == XXWIDGETS_OK && changed) {
        for (i = 0; i < layout->count; ++i) {
            RECT *rect = &layout->children[i].rect;
            if (!SetWindowPos(layout->children[i].handle, NULL, rect->left - layout->x, rect->top - layout->y,
                rect->right - rect->left, rect->bottom - rect->top + layout->children[i].extra_height,
                SWP_NOZORDER | SWP_NOACTIVATE)) {
                status = XXWIDGETS_PLATFORM_ERROR; break;
            }
        }
        InvalidateRect(window, NULL, TRUE);
    }
    layout->client_width = client.right; layout->client_height = client.bottom;
    --widget->app->syncing; layout->updating = 0;
    return status;
}

static LRESULT CALLBACK win32_font_options_proc(HWND window, UINT message, WPARAM wp, LPARAM lp,
    UINT_PTR id, DWORD_PTR data)
{
    xxwidgets_widget *widget = (xxwidgets_widget *)data;
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
    win32_font_layout *layout = native ? native->font_layout : NULL;
    if (layout && !layout->updating) {
        if (message == WM_HSCROLL || message == WM_VSCROLL) {
            int horizontal = message == WM_HSCROLL, action = LOWORD(wp);
            int *offset = horizontal ? &layout->x : &layout->y;
            int page = horizontal ? layout->client_width : layout->client_height;
            int step = horizontal ? layout->step_x : layout->step_y;
            SCROLLINFO info = {sizeof(info)};
            info.fMask = SIF_TRACKPOS;
            switch (action) {
            case SB_LINEUP: *offset -= step; break;
            case SB_LINEDOWN: *offset += step; break;
            case SB_PAGEUP: *offset -= page; break;
            case SB_PAGEDOWN: *offset += page; break;
            case SB_TOP: *offset = 0; break;
            case SB_BOTTOM: *offset = horizontal ? layout->width : layout->height; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
                if (GetScrollInfo(window, horizontal ? SB_HORZ : SB_VERT, &info)) *offset = info.nTrackPos;
                break;
            default: return 0;
            }
            win32_font_layout_viewport(widget, 1); return 0;
        }
        if (message == WM_MOUSEWHEEL) {
            layout->y -= ((int)(short)HIWORD(wp) / WHEEL_DELTA) * 3 * layout->step_y;
            win32_font_layout_viewport(widget, 1); return 0;
        }
        if (message == WM_SIZE && wp != SIZE_MINIMIZED) win32_font_layout_viewport(widget, 0);
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, win32_font_options_proc, id);
    return DefSubclassProc(window, message, wp, lp);
}

static xxwidgets_status win32_font_layout_start(xxwidgets_widget *dialog)
{
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)dialog->platform;
    if (native->font_layout) return XXWIDGETS_OK;
    native->font_layout = (win32_font_layout *)calloc(1, sizeof(*native->font_layout));
    if (!native->font_layout) return XXWIDGETS_OUT_OF_MEMORY;
    return SetWindowSubclass((HWND)dialog->native, win32_font_options_proc, 1, (DWORD_PTR)dialog)
        ? XXWIDGETS_OK : XXWIDGETS_PLATFORM_ERROR;
}

static xxwidgets_status win32_font_layout_fit(xxwidgets_widget *dialog)
{
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)dialog->platform;
    win32_font_layout *layout = native->font_layout;
    HWND window = (HWND)dialog->native;
    MONITORINFO monitor = {sizeof(monitor)};
    RECT frame = {0, 0, layout->width, layout->height}, position;
    LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE) | WS_HSCROLL | WS_VSCROLL;
    int width, height, x, y;
    xxwidgets_status status;
    if (!GetWindowRect(window, &position) ||
        !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor) ||
        !AdjustWindowRectEx(&frame, (DWORD)style, FALSE, (DWORD)GetWindowLongPtrW(window, GWL_EXSTYLE)))
        return XXWIDGETS_PLATFORM_ERROR;
    width = frame.right - frame.left; height = frame.bottom - frame.top;
    if (width > monitor.rcWork.right - monitor.rcWork.left) width = monitor.rcWork.right - monitor.rcWork.left;
    if (height > monitor.rcWork.bottom - monitor.rcWork.top) height = monitor.rcWork.bottom - monitor.rcWork.top;
    x = position.left; y = position.top;
    if (x + width > monitor.rcWork.right) x = monitor.rcWork.right - width;
    if (x < monitor.rcWork.left) x = monitor.rcWork.left;
    if (y + height > monitor.rcWork.bottom) y = monitor.rcWork.bottom - height;
    if (y < monitor.rcWork.top) y = monitor.rcWork.top;
    layout->updating = 1; ++dialog->app->syncing;
    SetWindowLongPtrW(window, GWL_STYLE, style);
    status = SetWindowPos(window, NULL, x, y, width, height,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED) ? XXWIDGETS_OK : XXWIDGETS_PLATFORM_ERROR;
    --dialog->app->syncing; layout->updating = 0;
    return status;
}

static xxwidgets_status win32_font_layout_update(xxwidgets_widget *dialog, int refresh)
{
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)dialog->platform;
    win32_font_layout *layout = native->font_layout;
    HWND focus;
    size_t i;
    xxwidgets_status status = win32_font_layout_viewport(dialog, refresh);
    if (status != XXWIDGETS_OK) return status;
    focus = GetFocus();
    /* Preserve manual scrolling until focus or the measured layout changes. */
    if (!refresh && focus == layout->last_focus) return XXWIDGETS_OK;
    layout->last_focus = focus;
    for (i = 0; i < layout->count; ++i) if (layout->children[i].handle == focus) {
        RECT *rect = &layout->children[i].rect;
        int x = layout->x, y = layout->y;
        if (rect->left < x || rect->right - rect->left > layout->client_width) x = rect->left;
        else if (rect->right > x + layout->client_width) x = rect->right - layout->client_width;
        if (rect->top < y || rect->bottom - rect->top > layout->client_height) y = rect->top;
        else if (rect->bottom > y + layout->client_height) y = rect->bottom - layout->client_height;
        if (x != layout->x || y != layout->y) {
            layout->x = x; layout->y = y; return win32_font_layout_viewport(dialog, 1);
        }
        break;
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status win32_font_options_layout(xxwidgets_widget *dialog, int refresh)
{
    xxwidgets_win32_app *app = (xxwidgets_win32_app *)dialog->app->platform;
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)dialog->platform;
    win32_font_layout *layout = native->font_layout;
    HWND window = (HWND)dialog->native;
    xxwidgets_widget *child;
    int first = !layout;
    xxwidgets_status status = win32_font_layout_start(dialog);
    if (status != XXWIDGETS_OK) return status;
    layout = native->font_layout;
    if (first || refresh) {
        TEXTMETRICW controls, edits;
        int row_y[4], preview_y[4], preview_height[4], role, y;
        int cw = app->cell_width, height = app->cell_height, gap = app->cell_height / 2;
        int preview_width = 0, right_column, footer_y;
        HDC dc;
        if (!win32_font_metrics(window, app->role_fonts[XXWIDGETS_FONT_CONTROLS], &controls) ||
            !win32_font_metrics(window, app->role_fonts[XXWIDGETS_FONT_TEXT_EDITS], &edits))
            return XXWIDGETS_PLATFORM_ERROR;
        if (cw < controls.tmAveCharWidth) cw = controls.tmAveCharWidth;
        if (cw < edits.tmAveCharWidth) cw = edits.tmAveCharWidth;
        if (height < controls.tmHeight + controls.tmExternalLeading + 8)
            height = controls.tmHeight + controls.tmExternalLeading + 8;
        if (height < edits.tmHeight + edits.tmExternalLeading + 8)
            height = edits.tmHeight + edits.tmExternalLeading + 8;
        if (gap < 4) gap = 4;
        for (role = 0; role < 4; ++role) preview_height[role] = height;
        dc = GetDC(window);
        if (!dc) return XXWIDGETS_PLATFORM_ERROR;
        for (child = dialog->app->widgets; child; child = child->next) {
            if (child->parent == dialog && child->kind == XXWIDGETS_LABEL && child->rect.y >= 3 &&
                child->rect.y <= 15 && (child->rect.y - 3) % 4 == 0) {
                wchar_t text[192]; TEXTMETRICW metrics; SIZE size;
                HFONT font = (HFONT)SendMessageW((HWND)child->native, WM_GETFONT, 0, 0);
                HGDIOBJ previous = SelectObject(dc, font);
                if (!previous || previous == HGDI_ERROR) { ReleaseDC(window, dc); return XXWIDGETS_PLATFORM_ERROR; }
                role = (child->rect.y - 3) / 4;
                GetWindowTextW((HWND)child->native, text, sizeof(text) / sizeof(text[0]));
                if (GetTextMetricsW(dc, &metrics) && preview_height[role] < metrics.tmHeight + metrics.tmExternalLeading + 4)
                    preview_height[role] = metrics.tmHeight + metrics.tmExternalLeading + 4;
                if (GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size) && preview_width < size.cx)
                    preview_width = size.cx;
                SelectObject(dc, previous);
            }
        }
        ReleaseDC(window, dc);
        right_column = 88 * cw;
        if (right_column < 22 * cw + preview_width + gap) right_column = 22 * cw + preview_width + gap;
        y = gap;
        for (role = 0; role < 4; ++role) {
            row_y[role] = y; preview_y[role] = y + height + gap;
            y = preview_y[role] + preview_height[role] + 2 * gap;
        }
        footer_y = y + height + gap;
        layout->count = 0; layout->width = right_column + 12 * cw;
        layout->height = footer_y + height + gap; layout->step_x = cw; layout->step_y = height;
        for (child = dialog->app->widgets; child; child = child->next) {
            RECT rect;
            if (child->parent != dialog) continue;
            if (layout->count == sizeof(layout->children) / sizeof(layout->children[0])) return XXWIDGETS_PLATFORM_ERROR;
            rect.left = child->rect.x * cw; rect.right = rect.left + child->rect.width * cw;
            if (child->rect.y >= 1 && child->rect.y <= 13 && (child->rect.y - 1) % 4 == 0) {
                role = (child->rect.y - 1) / 4; rect.top = row_y[role]; rect.bottom = rect.top + height;
                if (child->rect.x == 88) { rect.left = right_column; rect.right = rect.left + 10 * cw; }
            } else if (child->rect.y >= 3 && child->rect.y <= 15 && (child->rect.y - 3) % 4 == 0) {
                role = (child->rect.y - 3) / 4; rect.top = preview_y[role]; rect.bottom = rect.top + preview_height[role];
                if (child->rect.x == 88) { rect.left = right_column; rect.right = rect.left + 10 * cw; }
                else rect.right = right_column - gap;
            } else if (child->rect.y == 18) {
                rect.top = y; rect.bottom = y + height; rect.right = layout->width - 2 * cw;
            } else {
                rect.top = footer_y; rect.bottom = footer_y + height;
                if (child->rect.x == 84) { rect.right = layout->width - 2 * cw; rect.left = rect.right - 14 * cw; }
                else if (child->rect.x == 72) { rect.right = layout->width - 18 * cw; rect.left = rect.right - 10 * cw; }
            }
            layout->children[layout->count].handle = (HWND)child->native;
            layout->children[layout->count].extra_height = 0;
            layout->children[layout->count++].rect = rect;
        }
        if (first) {
            status = win32_font_layout_fit(dialog);
            if (status != XXWIDGETS_OK) return status;
        }
    }
    return win32_font_layout_update(dialog, first || refresh);
}

static xxwidgets_status win32_optimization_options_layout(xxwidgets_widget *dialog, int refresh)
{
    xxwidgets_win32_app *app = (xxwidgets_win32_app *)dialog->app->platform;
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)dialog->platform;
    win32_font_layout *layout;
    HWND window = (HWND)dialog->native;
    xxwidgets_widget *child;
    int first = !native->font_layout;
    xxwidgets_status status = win32_font_layout_start(dialog);
    if (status != XXWIDGETS_OK) return status;
    layout = native->font_layout;
    if (first || refresh) {
        TEXTMETRICW metrics;
        int columns[2] = {29 * app->cell_width, 29 * app->cell_width};
        int buttons[2] = {10 * app->cell_width, 12 * app->cell_width};
        int cw = app->cell_width, row = 2 * app->cell_height, label, gap, margin, combo_y, cpu_y, check_y, button_y;
        int cpu_width = 0, second_x;
        HDC dc;
        HGDIOBJ previous;
        if (!win32_font_metrics(window, app->role_fonts[XXWIDGETS_FONT_CONTROLS], &metrics))
            return XXWIDGETS_PLATFORM_ERROR;
        if (cw < metrics.tmAveCharWidth) cw = metrics.tmAveCharWidth;
        if (row < metrics.tmHeight + metrics.tmExternalLeading + 10)
            row = metrics.tmHeight + metrics.tmExternalLeading + 10;
        label = metrics.tmHeight + metrics.tmExternalLeading + 4;
        if (label < app->cell_height) label = app->cell_height;
        gap = cw / 2; if (gap < 8) gap = 8;
        margin = 2 * gap;
        dc = GetDC(window);
        if (!dc) return XXWIDGETS_PLATFORM_ERROR;
        previous = SelectObject(dc, app->role_fonts[XXWIDGETS_FONT_CONTROLS]);
        if (!previous || previous == HGDI_ERROR) { ReleaseDC(window, dc); return XXWIDGETS_PLATFORM_ERROR; }
        for (child = dialog->app->widgets; child && status == XXWIDGETS_OK; child = child->next) {
            SIZE size;
            wchar_t text[128];
            int column = child->rect.x > 2, width;
            if (child->parent != dialog) continue;
            if (child->kind == XXWIDGETS_COMBOBOX) {
                size_t i;
                for (i = 0; i < child->item_count; ++i) {
                    wchar_t *item;
                    status = win32_wide(child->items[i], &item);
                    if (status != XXWIDGETS_OK) break;
                    if (!GetTextExtentPoint32W(dc, item, (int)wcslen(item), &size)) status = XXWIDGETS_PLATFORM_ERROR;
                    else {
                        width = size.cx + GetSystemMetrics(SM_CXVSCROLL) + 2 * gap + 12;
                        if (columns[column] < width) columns[column] = width;
                    }
                    free(item);
                }
                continue;
            }
            GetWindowTextW((HWND)child->native, text, sizeof(text) / sizeof(text[0]));
            if (!GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size)) { status = XXWIDGETS_PLATFORM_ERROR; break; }
            width = size.cx + 2 * gap;
            if (child->kind == XXWIDGETS_CHECKBOX) {
                width += GetSystemMetrics(SM_CXMENUCHECK) + gap;
                if (columns[column] < width) columns[column] = width;
            } else if (child->kind == XXWIDGETS_BUTTON) {
                int cancel = child->rect.x >= 50;
                if (buttons[cancel] < width) buttons[cancel] = width;
            } else if (child->rect.y == 5) cpu_width = width;
            else if (columns[column] < width) columns[column] = width;
        }
        SelectObject(dc, previous); ReleaseDC(window, dc);
        if (status != XXWIDGETS_OK) return status;
        combo_y = margin + label + gap;
        cpu_y = combo_y + row + 2 * gap;
        check_y = cpu_y + label + gap;
        button_y = check_y + row + 2 * gap;
        second_x = margin + columns[0] + gap;
        layout->width = second_x + columns[1] + margin;
        if (layout->width < buttons[0] + buttons[1] + gap + 2 * margin)
            layout->width = buttons[0] + buttons[1] + gap + 2 * margin;
        if (layout->width < cpu_width + 2 * margin) layout->width = cpu_width + 2 * margin;
        layout->height = button_y + row + margin;
        layout->step_x = cw; layout->step_y = row; layout->count = 0;
        ++dialog->app->syncing;
        for (child = dialog->app->widgets; child; child = child->next) {
            RECT rect;
            int column = child->rect.x > 2, extra = 0;
            if (child->parent != dialog) continue;
            if (layout->count == sizeof(layout->children) / sizeof(layout->children[0])) {
                --dialog->app->syncing; return XXWIDGETS_PLATFORM_ERROR;
            }
            rect.left = column ? second_x : margin; rect.right = rect.left + columns[column];
            if (child->kind == XXWIDGETS_COMBOBOX) {
                rect.top = combo_y; rect.bottom = rect.top + row;
                /* Cache only the closed selection for focus scrolling; native
                 * ComboBox geometry also reserves space for its dropdown. */
                extra = 10 * row;
                SendMessageW((HWND)child->native, CB_SETITEMHEIGHT, (WPARAM)-1, row - 6);
                SendMessageW((HWND)child->native, CB_SETITEMHEIGHT, 0, row - 6);
            } else if (child->kind == XXWIDGETS_CHECKBOX) {
                rect.top = check_y; rect.bottom = rect.top + row;
            } else if (child->kind == XXWIDGETS_BUTTON) {
                int cancel = child->rect.x >= 50;
                rect.right = layout->width - margin - (cancel ? 0 : buttons[1] + gap);
                rect.left = rect.right - buttons[cancel]; rect.top = button_y; rect.bottom = rect.top + row;
            } else {
                rect.top = child->rect.y == 5 ? cpu_y : margin; rect.bottom = rect.top + label;
                if (child->rect.y == 5) { rect.left = margin; rect.right = layout->width - margin; }
            }
            layout->children[layout->count].handle = (HWND)child->native;
            layout->children[layout->count].extra_height = extra;
            layout->children[layout->count++].rect = rect;
        }
        --dialog->app->syncing;
        if (first) {
            status = win32_font_layout_fit(dialog);
            if (status != XXWIDGETS_OK) return status;
        }
    }
    return win32_font_layout_update(dialog, first || refresh);
}

static xxwidgets_status win32_window_text(HWND handle, wchar_t **result)
{
    int length;
    wchar_t *wide;
    *result = NULL;
    SetLastError(ERROR_SUCCESS);
    length = GetWindowTextLengthW(handle);
    if (!length && GetLastError() != ERROR_SUCCESS) return XXWIDGETS_PLATFORM_ERROR;
    if (length == INT_MAX) return XXWIDGETS_INVALID_ARGUMENT;
    wide = (wchar_t *)malloc(((size_t)length + 1) * sizeof(*wide));
    if (!wide) return XXWIDGETS_OUT_OF_MEMORY;
    SetLastError(ERROR_SUCCESS);
    if (!GetWindowTextW(handle, wide, length + 1) && GetLastError() != ERROR_SUCCESS) {
        free(wide);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    *result = wide;
    return XXWIDGETS_OK;
}

static xxwidgets_status win32_geometry(xxwidgets_widget *widget, DWORD style,
                                       DWORD extended, int *x, int *y, int *width, int *height)
{
    xxwidgets_win32_app *state = (xxwidgets_win32_app *)widget->app->platform;
    int64_t px = (int64_t)widget->rect.x * state->cell_width;
    int64_t py = (int64_t)widget->rect.y * state->cell_height;
    int64_t pw = (int64_t)widget->rect.width * state->cell_width;
    int64_t ph = (int64_t)widget->rect.height * state->cell_height;
    RECT frame;
    /* Reserve space for non-client decoration before calling the integer WinAPI. */
    if (px < INT_MIN || px > INT_MAX || py < INT_MIN || py > INT_MAX ||
        pw < 1 || pw > INT_MAX - 32768 || ph < 1 || ph > INT_MAX - 32768)
        return XXWIDGETS_INVALID_ARGUMENT;
    *x = (int)px;
    *y = (int)py;
    *width = (int)pw;
    *height = (int)ph;
    if (widget->kind == XXWIDGETS_WINDOW) {
        frame.left = frame.top = 0;
        frame.right = *width;
        frame.bottom = *height;
        if (!AdjustWindowRectEx(&frame, style, FALSE, extended))
            return XXWIDGETS_PLATFORM_ERROR;
        *width = frame.right - frame.left;
        *height = frame.bottom - frame.top;
    }
    return XXWIDGETS_OK;
}

static LRESULT CALLBACK win32_window_proc(HWND handle, UINT message, WPARAM wp, LPARAM lp)
{
    xxwidgets_widget *widget = (xxwidgets_widget *)GetWindowLongPtrW(handle, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        CREATESTRUCTW *creation = (CREATESTRUCTW *)lp;
        widget = (xxwidgets_widget *)creation->lpCreateParams;
        SetWindowLongPtrW(handle, GWLP_USERDATA, (LONG_PTR)widget);
        widget->native = handle;
    }
    if (!widget) return DefWindowProcW(handle, message, wp, lp);
    switch (message) {
    case WM_CLOSE:
        xxwidgets_emit(widget, XXWIDGETS_EVENT_CLOSE, 0);
        return 0;
    case WM_COMMAND:
        if (lp && !widget->app->syncing) {
            HWND control_handle = (HWND)lp;
            xxwidgets_widget *control = win32_find_widget(widget->app, control_handle);
            if (!control || control->parent != widget) break;
            switch (control->kind) {
            case XXWIDGETS_COMBOBOX:
                if (HIWORD(wp) == CBN_SELCHANGE && win32_read_value(control) == XXWIDGETS_OK)
                    xxwidgets_emit(control, XXWIDGETS_EVENT_SELECT, control->value);
                break;
            case XXWIDGETS_CHECKCOMBOBOX:
                break;
            case XXWIDGETS_BUTTON:
                if (HIWORD(wp) == BN_CLICKED)
                    xxwidgets_emit(control, XXWIDGETS_EVENT_CLICK, 0);
                break;
            case XXWIDGETS_CHECKBOX:
                if (HIWORD(wp) == BN_CLICKED && win32_read_value(control) == XXWIDGETS_OK)
                    xxwidgets_emit(control, XXWIDGETS_EVENT_CHANGE, control->value);
                break;
            case XXWIDGETS_EDIT:
                if (HIWORD(wp) == EN_CHANGE && win32_read_text(control) == XXWIDGETS_OK)
                    xxwidgets_emit(control, XXWIDGETS_EVENT_CHANGE, 0);
                break;
            case XXWIDGETS_LISTBOX:
            case XXWIDGETS_ARCHIVEVIEW:
            case XXWIDGETS_HEXVIEW:
                if (HIWORD(wp) == LBN_SELCHANGE && win32_read_value(control) == XXWIDGETS_OK)
                    xxwidgets_emit(control, XXWIDGETS_EVENT_SELECT, control->value);
                break;
            default:
                break;
            }
        }
        break;
    case WM_NOTIFY:
        if (lp && !widget->app->syncing) {
            NMHDR *notification = (NMHDR *)lp;
            xxwidgets_widget *control = win32_find_widget(widget->app, notification->hwndFrom);
            if (control && control->parent == widget && control->kind == XXWIDGETS_SCANRESULTS &&
                control->enabled && notification->code == LVN_ITEMCHANGED) {
                const NMLISTVIEW *change = (const NMLISTVIEW *)lp;
                if ((change->uChanged & LVIF_STATE) &&
                    ((change->uOldState ^ change->uNewState) & LVIS_SELECTED) &&
                    win32_read_value(control) == XXWIDGETS_OK)
                    xxwidgets_emit(control, XXWIDGETS_EVENT_SELECT, control->value);
            }
            if (control && control->parent == widget && control->kind == XXWIDGETS_TREEVIEW &&
                control->enabled && control->platform &&
                ((xxwidgets_win32_widget *)control->platform)->tree_initialized &&
                ((xxwidgets_win32_widget *)control->platform)->tree_content_revision == control->tree_content_revision) {
                if (notification->code == TVN_SELCHANGEDW || notification->code == TVN_SELCHANGEDA) {
                    if (win32_read_value(control) == XXWIDGETS_OK)
                        xxwidgets_emit(control, XXWIDGETS_EVENT_SELECT, control->value);
                } else if (notification->code == TVN_ITEMEXPANDEDW || notification->code == TVN_ITEMEXPANDEDA) {
                    const NMTREEVIEWW *change = (const NMTREEVIEWW *)lp;
                    size_t index = (size_t)change->itemNew.lParam;
                    TVITEMW item;
                    int expanded;
                    memset(&item, 0, sizeof(item)); item.hItem = change->itemNew.hItem;
                    item.mask = TVIF_STATE; item.stateMask = TVIS_EXPANDED;
                    if (!SendMessageW((HWND)control->native, TVM_GETITEMW, 0, (LPARAM)&item)) break;
                    expanded = !!(item.state & TVIS_EXPANDED);
                    win32_tree_expansion_input(control, index, change->itemNew.hItem, expanded);
                } else if (notification->code == NM_DBLCLK) {
                    if (win32_read_value(control) == XXWIDGETS_OK && control->value >= 0 &&
                        !xxwidgets_treeview_has_children(control, (size_t)control->value))
                        xxwidgets_emit(control, XXWIDGETS_EVENT_ACTIVATE, control->value);
                }
            }
        }
        break;
    case WM_MOVE:
        if (!widget->app->syncing && widget->platform) {
            xxwidgets_win32_app *state = (xxwidgets_win32_app *)widget->app->platform;
            xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
            RECT position;
            if (GetWindowRect(handle, &position)) {
                widget->rect.x = position.left / state->cell_width;
                widget->rect.y = position.top / state->cell_height;
                native->applied_rect = widget->rect;
            }
        }
        break;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED && !widget->app->syncing && widget->platform) {
            xxwidgets_win32_app *state = (xxwidgets_win32_app *)widget->app->platform;
            xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
            RECT client;
            if (GetClientRect(handle, &client)) {
                widget->rect.width = (client.right - client.left) / state->cell_width;
                widget->rect.height = (client.bottom - client.top) / state->cell_height;
                if (widget->rect.width < 1) widget->rect.width = 1;
                if (widget->rect.height < 1) widget->rect.height = 1;
                native->applied_rect = widget->rect;
                xxwidgets_emit(widget, XXWIDGETS_EVENT_RESIZE, 0);
            }
        }
        break;
    case WM_NCDESTROY:
        widget->native = NULL;
        SetWindowLongPtrW(handle, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcW(handle, message, wp, lp);
}

static xxwidgets_status win32_init(xxwidgets_app *app)
{
    xxwidgets_win32_app *state;
    INITCOMMONCONTROLSEX controls;
    NONCLIENTMETRICSW metrics;
    WNDCLASSEXW window_class;
    HDC dc;
    TEXTMETRICW font_metrics;
    state = (xxwidgets_win32_app *)calloc(1, sizeof(*state));
    if (!state) return XXWIDGETS_OUT_OF_MEMORY;
    state->instance = GetModuleHandleW(NULL);
    state->cell_width = 8;
    state->cell_height = 20;
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES;
    if (!InitCommonControlsEx(&controls)) { free(state); return XXWIDGETS_PLATFORM_ERROR; }
    memset(&metrics, 0, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        state->font = CreateFontIndirectW(&metrics.lfMessageFont);
        state->owns_font = state->font != NULL;
    }
    if (!state->font) state->font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    {
        LOGFONTW fixed_font;
        memset(&fixed_font, 0, sizeof(fixed_font));
        if (!GetObjectW(state->font, sizeof(fixed_font), &fixed_font))
            fixed_font.lfHeight = -14;
        fixed_font.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
        memcpy(fixed_font.lfFaceName, L"Consolas", sizeof(L"Consolas"));
        state->monospace_font = CreateFontIndirectW(&fixed_font);
        state->owns_monospace_font = state->monospace_font != NULL;
        if (!state->monospace_font)
            state->monospace_font = (HFONT)GetStockObject(ANSI_FIXED_FONT);
    }
    dc = GetDC(NULL);
    if (dc) {
        HGDIOBJ previous = SelectObject(dc, state->font);
        if (GetTextMetricsW(dc, &font_metrics)) {
            if (font_metrics.tmAveCharWidth > 0) state->cell_width = font_metrics.tmAveCharWidth;
            if (font_metrics.tmHeight > 0)
                state->cell_height = font_metrics.tmHeight + font_metrics.tmExternalLeading + 4;
        }
        if (previous && previous != HGDI_ERROR) SelectObject(dc, previous);
        ReleaseDC(NULL, dc);
    }
    swprintf(state->class_name, sizeof(state->class_name) / sizeof(state->class_name[0]),
             L"xxwidgets_window_%p", (void *)app);
    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = win32_window_proc;
    window_class.hInstance = state->instance;
    window_class.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    window_class.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    window_class.lpszClassName = state->class_name;
    state->window_class = RegisterClassExW(&window_class);
    if (!state->window_class) {
        if (state->owns_font) DeleteObject(state->font);
        if (state->owns_monospace_font) DeleteObject(state->monospace_font);
        free(state);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    swprintf(state->browser_class_name,
             sizeof(state->browser_class_name) / sizeof(state->browser_class_name[0]),
             L"xxwidgets_archive_browser_%p", (void *)app);
    window_class.lpfnWndProc = win32_browser_proc;
    window_class.lpszClassName = state->browser_class_name;
    state->browser_class = RegisterClassExW(&window_class);
    if (!state->browser_class) {
        UnregisterClassW(state->class_name, state->instance);
        if (state->owns_font) DeleteObject(state->font);
        if (state->owns_monospace_font) DeleteObject(state->monospace_font);
        free(state);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    {
        size_t role;
        for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role)
            state->role_fonts[role] = win32_default_role_font(state, (xxwidgets_font_role)role);
    }
    app->platform = state;
    return XXWIDGETS_OK;
}

static void win32_shutdown(xxwidgets_app *app)
{
    xxwidgets_win32_app *state = (xxwidgets_win32_app *)app->platform;
    size_t role;
    if (!state) return;
    UnregisterClassW(state->browser_class_name, state->instance);
    UnregisterClassW(state->class_name, state->instance);
    for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role)
        if (state->owns_role_font[role]) DeleteObject(state->role_fonts[role]);
    if (state->owns_font) DeleteObject(state->font);
    if (state->owns_monospace_font) DeleteObject(state->monospace_font);
    free(state);
    app->platform = NULL;
}

static void win32_browser_icon_rect(unsigned char *pixels, int left, int top,
                                    int right, int bottom, COLORREF color)
{
    int x, y;
    for (y = top; y < bottom; ++y)
        for (x = left; x < right; ++x) {
            unsigned char *pixel = pixels + ((size_t)y * 16 + (size_t)x) * 3;
            pixel[0] = GetBValue(color);
            pixel[1] = GetGValue(color);
            pixel[2] = GetRValue(color);
        }
}

static HIMAGELIST win32_browser_images(void)
{
    const COLORREF transparent = RGB(255, 0, 255);
    /* A 24-bit color image plus a color-key mask has no implicit alpha bytes.
     * GDI-created 32-bit DDBs otherwise render black in modern ListViews. */
    HIMAGELIST images = ImageList_Create(16, 16, ILC_COLOR24 | ILC_MASK, 2, 0);
    BITMAPINFO info;
    HBITMAP bitmap;
    unsigned char *pixels = NULL;
    int success = 0;
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = 16;
    info.bmiHeader.biHeight = -16;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 24;
    info.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    if (!images || !bitmap || !pixels)
        goto finish;
    win32_browser_icon_rect(pixels, 0, 0, 16, 16, transparent);
    win32_browser_icon_rect(pixels, 1, 3, 8, 7, RGB(132, 109, 53));
    win32_browser_icon_rect(pixels, 2, 4, 7, 7, RGB(255, 225, 118));
    win32_browser_icon_rect(pixels, 1, 5, 15, 14, RGB(132, 109, 53));
    win32_browser_icon_rect(pixels, 2, 6, 14, 13, RGB(255, 207, 82));
    win32_browser_icon_rect(pixels, 2, 6, 14, 7, RGB(255, 233, 148));
    if (ImageList_AddMasked(images, bitmap, transparent) < 0) goto finish;
    GdiFlush();
    win32_browser_icon_rect(pixels, 0, 0, 16, 16, transparent);
    win32_browser_icon_rect(pixels, 3, 1, 13, 15, RGB(97, 115, 131));
    win32_browser_icon_rect(pixels, 4, 2, 12, 14, RGB(255, 255, 255));
    win32_browser_icon_rect(pixels, 5, 5, 11, 6, RGB(140, 160, 176));
    win32_browser_icon_rect(pixels, 5, 8, 11, 9, RGB(140, 160, 176));
    win32_browser_icon_rect(pixels, 5, 11, 10, 12, RGB(140, 160, 176));
    if (ImageList_AddMasked(images, bitmap, transparent) < 0) goto finish;
    GdiFlush();
    success = 1;
finish:
    if (bitmap) DeleteObject(bitmap);
    if (!success && images) { ImageList_Destroy(images); images = NULL; }
    return images;
}

static xxwidgets_status win32_browser_create_children(xxwidgets_widget *widget)
{
    xxwidgets_win32_app *state = (xxwidgets_win32_app *)widget->app->platform;
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
    HWND parent = (HWND)widget->native;
    static const wchar_t *headings[] = {L"Name", L"Size", L"Packed Size", L"Modified", L"Attributes"};
    static const int widths[] = {38, 15, 16, 24, 12};
    int i;
    native->browser_up = CreateWindowExW(0, L"BUTTON", L"\x2191", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                         0, 0, 0, 0, parent, (HMENU)(INT_PTR)XXWIDGETS_BROWSER_UP_ID,
                                         state->instance, NULL);
    native->browser_address = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                              WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL,
                                              0, 0, 0, 0, parent, (HMENU)(INT_PTR)XXWIDGETS_BROWSER_ADDRESS_ID,
                                              state->instance, NULL);
    native->browser_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT |
                                           LVS_SHOWSELALWAYS,
                                           0, 0, 0, 0, parent, (HMENU)(INT_PTR)XXWIDGETS_BROWSER_LIST_ID,
                                           state->instance, NULL);
    if (!native->browser_up || !native->browser_address || !native->browser_list)
        return XXWIDGETS_PLATFORM_ERROR;
    SendMessageW(native->browser_up, WM_SETFONT, (WPARAM)state->role_fonts[XXWIDGETS_FONT_CONTROLS], FALSE);
    SendMessageW(native->browser_address, WM_SETFONT, (WPARAM)state->role_fonts[XXWIDGETS_FONT_CONTROLS], FALSE);
    SendMessageW(native->browser_address, EM_SETLIMITTEXT, (WPARAM)INT_MAX - 1, 0);
    SendMessageW(native->browser_list, WM_SETFONT, (WPARAM)state->role_fonts[XXWIDGETS_FONT_TABLE_VIEWS], FALSE);
    SendMessageW(native->browser_list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                 LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    native->browser_images = win32_browser_images();
    if (!native->browser_images) return XXWIDGETS_PLATFORM_ERROR;
    SendMessageW(native->browser_list, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)native->browser_images);
    for (i = 0; i < 5; ++i) {
        LVCOLUMNW column;
        memset(&column, 0, sizeof(column));
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;
        column.pszText = (wchar_t *)headings[i];
        column.cx = widths[i] * state->cell_width;
        column.fmt = i == 1 || i == 2 ? LVCFMT_RIGHT : LVCFMT_LEFT;
        column.iSubItem = i;
        if (SendMessageW(native->browser_list, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&column) == -1)
            return XXWIDGETS_PLATFORM_ERROR;
    }
    if (!SetWindowSubclass(native->browser_list, win32_browser_list_proc, 1, (DWORD_PTR)widget))
        return XXWIDGETS_PLATFORM_ERROR;
    win32_browser_layout(widget);
    return XXWIDGETS_OK;
}

static xxwidgets_status win32_create(xxwidgets_widget *widget)
{
    xxwidgets_win32_app *state = (xxwidgets_win32_app *)widget->app->platform;
    xxwidgets_win32_widget *native;
    LPCWSTR class_name = NULL;
    DWORD style = WS_CHILD;
    DWORD extended = 0;
    wchar_t *text;
    HWND handle;
    int x, y, width, height;
    xxwidgets_status status;
    switch (widget->kind) {
    case XXWIDGETS_WINDOW:
        class_name = state->class_name;
        style = WS_OVERLAPPEDWINDOW;
        extended = WS_EX_CONTROLPARENT;
        break;
    case XXWIDGETS_LABEL: class_name = L"STATIC"; style |= SS_LEFT; break;
    case XXWIDGETS_BUTTON: class_name = L"BUTTON"; style |= WS_TABSTOP | BS_PUSHBUTTON; break;
    case XXWIDGETS_EDIT:
        class_name = L"EDIT";
        style |= WS_TABSTOP | ES_AUTOHSCROLL;
        extended = WS_EX_CLIENTEDGE;
        break;
    case XXWIDGETS_CHECKBOX: class_name = L"BUTTON"; style |= WS_TABSTOP | BS_AUTOCHECKBOX; break;
    case XXWIDGETS_COMBOBOX:
        class_name = L"COMBOBOX"; style |= WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS; break;
    case XXWIDGETS_CHECKCOMBOBOX:
        class_name = L"COMBOBOX"; style |= WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS; break;
    case XXWIDGETS_LISTBOX:
    case XXWIDGETS_ARCHIVEVIEW:
    case XXWIDGETS_HEXVIEW:
        class_name = L"LISTBOX";
        style |= WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT;
        if (xxwidgets_formatted_rows(widget)) style |= WS_HSCROLL;
        extended = WS_EX_CLIENTEDGE;
        break;
    case XXWIDGETS_PROGRESS: class_name = PROGRESS_CLASSW; style |= PBS_SMOOTH; break;
    case XXWIDGETS_ARCHIVEBROWSER:
        class_name = state->browser_class_name;
        style |= WS_TABSTOP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        extended = WS_EX_CONTROLPARENT;
        break;
    case XXWIDGETS_SCANRESULTS:
        class_name = WC_LISTVIEWW;
        style |= WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL;
        extended = WS_EX_CLIENTEDGE;
        break;
    case XXWIDGETS_TREEVIEW:
        class_name = WC_TREEVIEWW;
        style |= WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS;
        extended = WS_EX_CLIENTEDGE;
        break;
    default: return XXWIDGETS_INVALID_ARGUMENT;
    }
    status = win32_geometry(widget, style, extended, &x, &y, &width, &height);
    if (status != XXWIDGETS_OK) return status;
    if (widget->kind == XXWIDGETS_COMBOBOX) height += 10 * state->cell_height;
    status = win32_wide(widget->text, &text);
    if (status != XXWIDGETS_OK) return status;
    native = (xxwidgets_win32_widget *)calloc(1, sizeof(*native));
    if (!native) { free(text); return XXWIDGETS_OUT_OF_MEMORY; }
    native->applied_rect = widget->rect;
    widget->platform = native;
    ++widget->app->syncing;
    handle = CreateWindowExW(extended, class_name, text, style, x, y, width, height,
                            widget->parent ? (HWND)widget->parent->native : NULL,
                            NULL, state->instance, widget);
    free(text);
    if (!handle) {
        --widget->app->syncing;
        free(native);
        widget->platform = NULL;
        return XXWIDGETS_PLATFORM_ERROR;
    }
    widget->native = handle;
    if (widget->kind != XXWIDGETS_WINDOW)
        SetWindowLongPtrW(handle, GWLP_USERDATA, (LONG_PTR)widget);
    SendMessageW(handle, WM_SETFONT, (WPARAM)state->role_fonts[xxwidgets_widget_font_role(widget)], FALSE);
    if (widget->kind == XXWIDGETS_PROGRESS)
        SendMessageW(handle, PBM_SETRANGE32, 0, 100);
    if (widget->kind == XXWIDGETS_EDIT)
        SendMessageW(handle, EM_SETLIMITTEXT, (WPARAM)INT_MAX - 1, 0);
    if (widget->kind == XXWIDGETS_TREEVIEW)
        SendMessageW(handle, CCM_SETUNICODEFORMAT, TRUE, 0);
    status = widget->kind == XXWIDGETS_ARCHIVEBROWSER ? win32_browser_create_children(widget) : XXWIDGETS_OK;
    if (status == XXWIDGETS_OK && widget->kind == XXWIDGETS_SCANRESULTS)
        status = win32_scanresults_columns(widget);
    if (status == XXWIDGETS_OK && widget->kind == XXWIDGETS_TREEVIEW &&
        !SetWindowSubclass(handle, win32_tree_proc, 1, (DWORD_PTR)widget))
        status = XXWIDGETS_PLATFORM_ERROR;
    if (status == XXWIDGETS_OK && widget->kind == XXWIDGETS_CHECKCOMBOBOX &&
        !SetWindowSubclass(handle, win32_checkcombo_control_proc, 1, (DWORD_PTR)widget))
        status = XXWIDGETS_PLATFORM_ERROR;
    if (status == XXWIDGETS_OK) status = win32_sync(widget);
    if (status != XXWIDGETS_OK) {
        DestroyWindow(handle);
        widget->native = NULL;
        if (native->browser_images) ImageList_Destroy(native->browser_images);
        free(native->tree_items);
        free(native);
        widget->platform = NULL;
    }
    --widget->app->syncing;
    return status;
}

static void win32_destroy(xxwidgets_widget *widget)
{
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
    if (native && native->combo_popup) DestroyWindow(native->combo_popup);
    if (widget->native) DestroyWindow((HWND)widget->native);
    widget->native = NULL;
    if (native && native->browser_images) ImageList_Destroy(native->browser_images);
    if (native && native->about_bitmap) DeleteObject(native->about_bitmap);
    if (native && native->preview_font) DeleteObject(native->preview_font);
    if (native) free(native->font_layout);
    if (native) free(native->tree_items);
    free(widget->platform);
    widget->platform = NULL;
}

static void win32_tree_expansion_input(xxwidgets_widget *widget, size_t index,
    HTREEITEM item, int expanded)
{
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
    int previous;
    xxwidgets_status result;
    if (!native || !native->tree_initialized || native->tree_content_revision != widget->tree_content_revision ||
        widget->app->syncing ||
        xxwidgets_treeview_get_expanded(widget, index, &previous) != XXWIDGETS_OK || previous == expanded) return;
    result = xxwidgets_treeview_expansion_input(widget, index, expanded);
    ++widget->app->syncing;
    if (result == XXWIDGETS_OK)
        SendMessageW((HWND)widget->native, TVM_SELECTITEM, TVGN_CARET,
            widget->value < 0 ? 0 : (LPARAM)native->tree_items[widget->value]);
    else SendMessageW((HWND)widget->native, TVM_EXPAND,
        previous ? TVE_EXPAND : TVE_COLLAPSE, (LPARAM)item);
    --widget->app->syncing;
    if (result == XXWIDGETS_OK) xxwidgets_emit(widget, XXWIDGETS_EVENT_CHANGE, (int)index);
}

static LRESULT CALLBACK win32_tree_proc(HWND handle, UINT message, WPARAM wp, LPARAM lp,
    UINT_PTR id, DWORD_PTR data)
{
    xxwidgets_widget *widget = (xxwidgets_widget *)data;
    (void)id;
    if (message == TVM_EXPAND && !widget->app->syncing && widget->platform &&
        ((xxwidgets_win32_widget *)widget->platform)->tree_initialized) {
        TVITEMW item;
        uint64_t revision = widget->tree_content_revision;
        LRESULT result;
        memset(&item, 0, sizeof(item)); item.hItem = (HTREEITEM)lp;
        item.mask = TVIF_STATE | TVIF_PARAM; item.stateMask = TVIS_EXPANDED;
        if (!SendMessageW(handle, TVM_GETITEMW, 0, (LPARAM)&item))
            return DefSubclassProc(handle, message, wp, lp);
        result = DefSubclassProc(handle, message, wp, lp);
        /* TVIS_EXPANDEDONCE suppresses later programmatic notifications. Cache
         * the final state even then; ordinary notifications already cached it.
         * Callbacks may replace the complete hierarchy, invalidating the item. */
        if (revision == widget->tree_content_revision &&
            SendMessageW(handle, TVM_GETITEMW, 0, (LPARAM)&item))
            win32_tree_expansion_input(widget, (size_t)item.lParam, item.hItem,
                !!(item.state & TVIS_EXPANDED));
        return result;
    }
    if (message == WM_GETDLGCODE && lp) {
        const MSG *key = (const MSG *)lp;
        if (key->message == WM_KEYDOWN && (key->wParam == VK_RETURN || key->wParam == VK_SPACE))
            return DefSubclassProc(handle, message, wp, lp) | DLGC_WANTMESSAGE;
    }
    if (message == WM_KEYDOWN && !widget->app->syncing && xxwidgets_focusable(widget) &&
        (wp == VK_RETURN || wp == VK_SPACE)) {
        if (win32_read_value(widget) == XXWIDGETS_OK && widget->value >= 0) {
            size_t index = (size_t)widget->value;
            if (wp == VK_SPACE && xxwidgets_treeview_has_children(widget, index)) {
                int expanded;
                if (xxwidgets_treeview_get_expanded(widget, index, &expanded) == XXWIDGETS_OK)
                    xxwidgets_treeview_user_expand(widget, index, !expanded);
            } else if (wp == VK_RETURN) xxwidgets_emit(widget, XXWIDGETS_EVENT_ACTIVATE, widget->value);
        }
        return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(handle, win32_tree_proc, 1);
    return DefSubclassProc(handle, message, wp, lp);
}

static LRESULT CALLBACK win32_checkcombo_popup_proc(HWND handle, UINT message, WPARAM wp, LPARAM lp,
    UINT_PTR id, DWORD_PTR data)
{
    xxwidgets_widget *widget = (xxwidgets_widget *)data;
    xxwidgets_win32_widget *state = widget->platform;
    if (message == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE) {
        POINT cursor;
        RECT control;
        state->combo_skip_mouse_open = GetAsyncKeyState(VK_LBUTTON) < 0 && GetCursorPos(&cursor) &&
            GetWindowRect((HWND)widget->native, &control) && PtInRect(&control, cursor);
        ShowWindow(handle, SW_HIDE);
    }
    if (message == WM_NOTIFY && lp && ((NMHDR *)lp)->hwndFrom == state->combo_list &&
        ((NMHDR *)lp)->code == LVN_ITEMCHANGED && !widget->app->syncing && xxwidgets_focusable(widget)) {
        NMLISTVIEW *change = (NMLISTVIEW *)lp;
        if (change->iItem >= 0 && (size_t)change->iItem < widget->item_count && (change->uChanged & LVIF_STATE) &&
            ((change->uOldState ^ change->uNewState) & LVIS_STATEIMAGEMASK)) {
            int checked = ((change->uNewState & LVIS_STATEIMAGEMASK) >> 12) == 2;
            widget->value = change->iItem;
            if (checked != xxwidgets_checkcombobox_checked(widget, (size_t)change->iItem))
                xxwidgets_checkcombobox_user_toggle(widget, (size_t)change->iItem);
        }
        return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(handle, win32_checkcombo_popup_proc, id);
    return DefSubclassProc(handle, message, wp, lp);
}

static LRESULT CALLBACK win32_checkcombo_control_proc(HWND handle, UINT message, WPARAM wp, LPARAM lp,
    UINT_PTR id, DWORD_PTR data)
{
    xxwidgets_widget *widget = (xxwidgets_widget *)data;
    xxwidgets_win32_widget *state = (xxwidgets_win32_widget *)widget->platform;
    int opening_key = (message == WM_KEYDOWN &&
        (wp == VK_RETURN || wp == VK_SPACE || wp == VK_F4 || wp == VK_DOWN || wp == VK_UP)) ||
        (message == WM_SYSKEYDOWN && (wp == VK_DOWN || wp == VK_F4));
    if (message == WM_GETDLGCODE && lp) {
        const MSG *key = (const MSG *)lp;
        if ((key->message == WM_KEYDOWN &&
            (key->wParam == VK_RETURN || key->wParam == VK_SPACE || key->wParam == VK_F4)) ||
            (key->message == WM_SYSKEYDOWN && (key->wParam == VK_DOWN || key->wParam == VK_F4)))
            return DefSubclassProc(handle, message, wp, lp) | DLGC_WANTMESSAGE;
    }
    if (message == CB_GETDROPPEDSTATE)
        return state && state->combo_popup && IsWindowVisible(state->combo_popup);
    if (message == CB_SHOWDROPDOWN) {
        if (!state) return FALSE;
        if (!wp) {
            if (state->combo_popup) ShowWindow(state->combo_popup, SW_HIDE);
            return TRUE;
        }
        if (state->combo_popup && IsWindowVisible(state->combo_popup)) return TRUE;
        return win32_checkcombo_open(widget) == XXWIDGETS_OK;
    }
    if (message == WM_LBUTTONDOWN || opening_key) {
        if (!widget->app->syncing && xxwidgets_focusable(widget)) {
            if (message == WM_LBUTTONDOWN && state->combo_skip_mouse_open) {
                state->combo_skip_mouse_open = 0; SetFocus(handle);
            } else {
                win32_checkcombo_open(widget);
                if (message == WM_LBUTTONDOWN) state->combo_skip_mouse_open = 0;
            }
        }
        return 0;
    }
    if (message == WM_LBUTTONDBLCLK || (message == WM_CHAR && (wp == VK_SPACE || wp == VK_RETURN)))
        return 0;
    if (message == WM_NCDESTROY) RemoveWindowSubclass(handle, win32_checkcombo_control_proc, id);
    return DefSubclassProc(handle, message, wp, lp);
}

static LRESULT CALLBACK win32_checkcombo_list_proc(HWND handle, UINT message, WPARAM wp, LPARAM lp,
    UINT_PTR id, DWORD_PTR data)
{
    xxwidgets_widget *widget = (xxwidgets_widget *)data;
    xxwidgets_win32_widget *state = widget->platform;
    if ((message == WM_KEYDOWN && (wp == VK_ESCAPE || wp == VK_RETURN || wp == VK_F4)) ||
        (message == WM_SYSKEYDOWN && (wp == VK_UP || wp == VK_DOWN || wp == VK_F4))) {
        ShowWindow(state->combo_popup, SW_HIDE); SetFocus((HWND)widget->native); return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(handle, win32_checkcombo_list_proc, id);
    return DefSubclassProc(handle, message, wp, lp);
}

static xxwidgets_status win32_checkcombo_open(xxwidgets_widget *widget)
{
    xxwidgets_win32_app *app = widget->app->platform;
    xxwidgets_win32_widget *state = widget->platform;
    RECT button, work;
    MONITORINFO monitor;
    int width, height, x, y, row_height = app->cell_height;
    TEXTMETRICW metrics;
    xxwidgets_status status;
    if (!xxwidgets_focusable(widget) || !widget->item_count) return XXWIDGETS_OK;
    if (widget->value < 0) widget->value = 0;
    if (state->combo_popup && IsWindowVisible(state->combo_popup)) {
        ShowWindow(state->combo_popup, SW_HIDE); return XXWIDGETS_OK;
    }
    if (!state->combo_popup) {
        state->combo_popup = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"",
            WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, 0, 0, 0, 0,
            (HWND)widget->parent->native, NULL, app->instance, NULL);
        if (!state->combo_popup || !SetWindowSubclass(state->combo_popup, win32_checkcombo_popup_proc, 1, (DWORD_PTR)widget))
            goto create_failed;
        state->combo_list = CreateWindowExW(0, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL,
            0, 0, 0, 0, state->combo_popup, (HMENU)(INT_PTR)1, app->instance, NULL);
        if (!state->combo_list || !SetWindowSubclass(state->combo_list, win32_checkcombo_list_proc, 1, (DWORD_PTR)widget))
            goto create_failed;
        SendMessageW(state->combo_list, WM_SETFONT, (WPARAM)app->role_fonts[XXWIDGETS_FONT_CONTROLS], FALSE);
        SendMessageW(state->combo_list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                     LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        { LVCOLUMNW column = {0}; column.mask = LVCF_WIDTH; column.cx = 240;
          if (SendMessageW(state->combo_list, LVM_INSERTCOLUMNW, 0, (LPARAM)&column) == -1) goto create_failed; }
        state->combo_initialized = 0;
    }
    ++widget->app->syncing; status = win32_sync_combo(widget); --widget->app->syncing;
    if (status != XXWIDGETS_OK) return status;
    if (!GetWindowRect((HWND)widget->native, &button)) return XXWIDGETS_PLATFORM_ERROR;
    width = button.right - button.left;
    if (width < 24 * app->cell_width) width = 24 * app->cell_width;
    if (win32_font_metrics((HWND)widget->native, app->role_fonts[XXWIDGETS_FONT_CONTROLS], &metrics) &&
        row_height < metrics.tmHeight + metrics.tmExternalLeading + 6)
        row_height = metrics.tmHeight + metrics.tmExternalLeading + 6;
    height = ((int)(widget->item_count < 10 ? widget->item_count : 10) + 1) * row_height;
    x = button.left; y = button.bottom;
    memset(&monitor, 0, sizeof(monitor)); monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfoW(MonitorFromWindow((HWND)widget->native, MONITOR_DEFAULTTONEAREST), &monitor)) {
        work = monitor.rcWork;
        if (width > work.right - work.left) width = work.right - work.left;
        if (height > work.bottom - work.top) height = work.bottom - work.top;
        if (x + width > work.right) x = work.right - width;
        if (x < work.left) x = work.left;
        if (y + height > work.bottom) y = button.top - height;
        if (y < work.top) y = work.top;
    }
    if (!SetWindowPos(state->combo_popup, HWND_TOP, x, y, width, height, SWP_SHOWWINDOW) ||
        !SetWindowPos(state->combo_list, NULL, 0, 0, width - 2, height - 2, SWP_NOZORDER)) return XXWIDGETS_PLATFORM_ERROR;
    SendMessageW(state->combo_list, LVM_SETCOLUMNWIDTH, 0, width - GetSystemMetrics(SM_CXVSCROLL) - 4);
    SetFocus(state->combo_list);
    if (widget->value >= 0 && (size_t)widget->value < widget->item_count) {
        LVITEMW item = {0}; item.stateMask = LVIS_FOCUSED | LVIS_SELECTED; item.state = item.stateMask;
        SendMessageW(state->combo_list, LVM_SETITEMSTATE, (WPARAM)widget->value, (LPARAM)&item);
        SendMessageW(state->combo_list, LVM_ENSUREVISIBLE, (WPARAM)widget->value, FALSE);
    }
    return XXWIDGETS_OK;
create_failed:
    if (state->combo_popup) DestroyWindow(state->combo_popup);
    state->combo_popup = state->combo_list = NULL; state->combo_initialized = 0;
    return XXWIDGETS_PLATFORM_ERROR;
}

static xxwidgets_status win32_sync_combo(xxwidgets_widget *widget)
{
    xxwidgets_win32_widget *state = widget->platform;
    HWND list = widget->kind == XXWIDGETS_COMBOBOX ? (HWND)widget->native : state->combo_list;
    size_t i;
    int rebuild = !state->combo_initialized || state->combo_revision != widget->combo_revision;
    xxwidgets_status status;
    if (list && rebuild) {
        SendMessageW(list, widget->kind == XXWIDGETS_COMBOBOX ? CB_RESETCONTENT : LVM_DELETEALLITEMS, 0, 0);
        for (i = 0; i < widget->item_count; ++i) {
            wchar_t *label;
            status = win32_wide(widget->items[i], &label); if (status != XXWIDGETS_OK) return status;
            if (widget->kind == XXWIDGETS_COMBOBOX) {
                LRESULT result = SendMessageW(list, CB_ADDSTRING, 0, (LPARAM)label); free(label);
                if (result == CB_ERR || result == CB_ERRSPACE) return XXWIDGETS_PLATFORM_ERROR;
            } else {
                LVITEMW item = {0}; LRESULT result;
                item.mask = LVIF_TEXT; item.iItem = (int)i; item.pszText = label;
                result = SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item); free(label);
                if (result == -1) return XXWIDGETS_PLATFORM_ERROR;
            }
        }
        state->combo_initialized = 1; state->combo_revision = widget->combo_revision;
    }
    if (widget->kind == XXWIDGETS_COMBOBOX) {
        SendMessageW(list, CB_SETCURSEL, (WPARAM)widget->value, 0);
    } else {
        wchar_t *label;
        wchar_t *previous;
        status = win32_wide(xxwidgets_combobox_caption(widget), &label); if (status != XXWIDGETS_OK) return status;
        status = win32_window_text((HWND)widget->native, &previous);
        if (status != XXWIDGETS_OK) { free(label); return status; }
        if (SendMessageW((HWND)widget->native, CB_GETCOUNT, 0, 0) != 1 || wcscmp(previous, label)) {
            LRESULT result;
            SendMessageW((HWND)widget->native, CB_RESETCONTENT, 0, 0);
            result = SendMessageW((HWND)widget->native, CB_ADDSTRING, 0, (LPARAM)label);
            if (result == CB_ERR || result == CB_ERRSPACE) status = XXWIDGETS_PLATFORM_ERROR;
        }
        if (status == XXWIDGETS_OK && SendMessageW((HWND)widget->native, CB_SETCURSEL, 0, 0) == CB_ERR)
            status = XXWIDGETS_PLATFORM_ERROR;
        free(previous); free(label);
        if (status != XXWIDGETS_OK) return status;
        if (list) for (i = 0; i < widget->item_count; ++i) {
            LVITEMW item = {0};
            unsigned int desired = INDEXTOSTATEIMAGEMASK(xxwidgets_checkcombobox_checked(widget, i) ? 2 : 1);
            if ((unsigned int)SendMessageW(list, LVM_GETITEMSTATE, (WPARAM)i, LVIS_STATEIMAGEMASK) == desired) continue;
            item.stateMask = LVIS_STATEIMAGEMASK; item.state = desired;
            SendMessageW(list, LVM_SETITEMSTATE, (WPARAM)i, (LPARAM)&item);
        }
        if (state->combo_popup && (!widget->visible || !widget->enabled || !widget->parent->enabled || !widget->item_count))
            ShowWindow(state->combo_popup, SW_HIDE);
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status win32_sync_list(xxwidgets_widget *widget)
{
    HWND handle = (HWND)widget->native;
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
    wchar_t **items = NULL;
    LRESULT old_count = SendMessageW(handle, LB_GETCOUNT, 0, 0);
    LRESULT old_selection = SendMessageW(handle, LB_GETCURSEL, 0, 0);
    LRESULT top = SendMessageW(handle, LB_GETTOPINDEX, 0, 0);
    int hexview = xxwidgets_formatted_rows(widget);
    int rebuild;
    int redraw_disabled = 0;
    int extent = 0;
    HDC dc = NULL;
    HGDIOBJ previous_font = NULL;
    size_t i;
    size_t first, count;
    xxwidgets_status status = XXWIDGETS_OK;
    if (widget->item_count > INT_MAX || old_count == LB_ERR) return XXWIDGETS_PLATFORM_ERROR;
    rebuild = !native->list_initialized || (size_t)old_count != native->list_count ||
        (size_t)old_count > widget->item_count ||
        (hexview && native->hex_revision != xxwidgets_row_revision(widget));
    first = rebuild ? 0 : (size_t)old_count;
    count = widget->item_count - first;
    if (count) {
        items = (wchar_t **)calloc(count, sizeof(*items));
        if (!items) return XXWIDGETS_OUT_OF_MEMORY;
    }
    /* Ordinary list items can only be appended or cleared through the C API. */
    for (i = 0; i < count; ++i) {
        status = win32_wide(widget->items[first + i], &items[i]);
        if (status != XXWIDGETS_OK) goto finish;
    }
    if (hexview && rebuild && count) {
        dc = GetDC(handle);
        if (!dc) { status = XXWIDGETS_PLATFORM_ERROR; goto finish; }
        previous_font = SelectObject(dc, (HFONT)SendMessageW(handle, WM_GETFONT, 0, 0));
        for (i = 0; i < count; ++i) {
            SIZE size;
            if (!GetTextExtentPoint32W(dc, items[i], (int)wcslen(items[i]), &size)) {
                status = XXWIDGETS_PLATFORM_ERROR;
                goto finish;
            }
            if (size.cx > extent) extent = size.cx;
        }
    }
    if (rebuild || count) {
        SendMessageW(handle, WM_SETREDRAW, FALSE, 0);
        redraw_disabled = 1;
        if (rebuild) SendMessageW(handle, LB_RESETCONTENT, 0, 0);
    }
    for (i = 0; i < count; ++i) {
        LRESULT added = SendMessageW(handle, LB_ADDSTRING, 0, (LPARAM)items[i]);
        if (added == LB_ERR || added == LB_ERRSPACE) { status = XXWIDGETS_PLATFORM_ERROR; goto finish; }
    }
    if (hexview && rebuild) SendMessageW(handle, LB_SETHORIZONTALEXTENT, (WPARAM)(extent ? extent + 4 : 0), 0);
    if (SendMessageW(handle, LB_GETCURSEL, 0, 0) != widget->value)
        SendMessageW(handle, LB_SETCURSEL, (WPARAM)widget->value, 0);
    if ((rebuild || count) && widget->item_count && top >= 0 && widget->value == old_selection) {
        if ((size_t)top >= widget->item_count) top = (LRESULT)widget->item_count - 1;
        SendMessageW(handle, LB_SETTOPINDEX, (WPARAM)top, 0);
    }
    native->list_count = widget->item_count;
    native->hex_revision = xxwidgets_row_revision(widget);
    native->list_initialized = 1;
finish:
    if (redraw_disabled) {
        SendMessageW(handle, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(handle, NULL, TRUE);
    }
    if (dc) {
        if (previous_font && previous_font != HGDI_ERROR) SelectObject(dc, previous_font);
        ReleaseDC(handle, dc);
    }
    for (i = 0; i < count; ++i) free(items ? items[i] : NULL);
    free(items);
    return status;
}

typedef struct win32_browser_row {
    wchar_t **cells;
    int image;
} win32_browser_row;

static void win32_browser_number(uint64_t value, wchar_t text[32])
{
    wchar_t reverse[32];
    size_t count = 0, i, length = 0;
    do {
        if (count && count % 4 == 3) reverse[count++] = L' ';
        reverse[count++] = (wchar_t)(L'0' + value % 10);
        value /= 10;
    } while (value);
    for (i = count; i; --i) text[length++] = reverse[i - 1];
    text[length] = 0;
}

static xxwidgets_status win32_browser_number_cell(uint64_t value, int known, wchar_t **result)
{
    wchar_t number[32];
    size_t length;
    if (known) win32_browser_number(value, number);
    else number[0] = 0;
    length = wcslen(number) + 1;
    *result = (wchar_t *)malloc(length * sizeof(**result));
    if (!*result) return XXWIDGETS_OUT_OF_MEMORY;
    memcpy(*result, number, length * sizeof(**result));
    return XXWIDGETS_OK;
}

static xxwidgets_status win32_browser_address(xxwidgets_widget *widget)
{
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
    const char *archive = xxwidgets_archivebrowser_archive(widget);
    const char *directory = xxwidgets_archivebrowser_directory(widget);
    size_t archive_length = strlen(archive), directory_length = strlen(directory);
    char *address;
    wchar_t *wide, *current;
    xxwidgets_status status;
    size_t i;
    if (archive_length > SIZE_MAX - directory_length - 2)
        return XXWIDGETS_INVALID_ARGUMENT;
    address = (char *)malloc(archive_length + directory_length + 2);
    if (!address) return XXWIDGETS_OUT_OF_MEMORY;
    memcpy(address, archive, archive_length);
    if (archive_length) address[archive_length++] = '\\';
    memcpy(address + archive_length, directory, directory_length + 1);
    status = win32_wide(address, &wide);
    free(address);
    if (status != XXWIDGETS_OK) return status;
    for (i = 0; wide[i]; ++i) if (wide[i] == L'/') wide[i] = L'\\';
    status = win32_window_text(native->browser_address, &current);
    if (status == XXWIDGETS_OK) {
        if (wcscmp(current, wide) != 0 && !SetWindowTextW(native->browser_address, wide))
            status = XXWIDGETS_PLATFORM_ERROR;
        free(current);
    }
    free(wide);
    EnableWindow(native->browser_up, widget->enabled && directory[0]);
    EnableWindow(native->browser_address, widget->enabled);
    EnableWindow(native->browser_list, widget->enabled);
    return status;
}

static xxwidgets_status win32_sync_browser(xxwidgets_widget *widget)
{
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
    HWND list = native->browser_list;
    size_t count = xxwidgets_archivebrowser_visible_count(widget);
    int columns = (int)xxwidgets_archivebrowser_column_count(widget);
    size_t i;
    int column;
    int rebuild;
    int redraw_disabled = 0;
    int old_selection = (int)SendMessageW(list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    int top = (int)SendMessageW(list, LVM_GETTOPINDEX, 0, 0);
    win32_browser_row *rows = NULL;
    xxwidgets_status status;
    LVITEMW item;
    if (count > INT_MAX) return XXWIDGETS_INVALID_ARGUMENT;
    status = win32_browser_address(widget);
    if (status != XXWIDGETS_OK) return status;
    rebuild = !native->list_initialized || native->browser_revision != widget->browser_revision ||
              (size_t)SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) != count;
    if (rebuild && count) {
        rows = (win32_browser_row *)calloc(count, sizeof(*rows));
        if (!rows) return XXWIDGETS_OUT_OF_MEMORY;
        /* Prepare every string before changing the native list. */
        for (i = 0; i < count; ++i) {
            xxwidgets_archive_browser_entry entry;
            size_t source_index;
            status = xxwidgets_archivebrowser_get_entry(widget, i, &source_index, &entry);
            if (status != XXWIDGETS_OK) goto finish;
            rows[i].image = entry.is_directory ? 0 : 1;
            rows[i].cells = (wchar_t **)calloc((size_t)columns, sizeof(*rows[i].cells));
            if (!rows[i].cells) { status = XXWIDGETS_OUT_OF_MEMORY; goto finish; }
            status = win32_wide(xxwidgets_archivebrowser_cell(widget, i, XXWIDGETS_ARCHIVE_COLUMN_NAME),
                                &rows[i].cells[0]);
            if (status != XXWIDGETS_OK) goto finish;
            status = win32_browser_number_cell(entry.size, !!(entry.flags & XXWIDGETS_ARCHIVE_SIZE_KNOWN),
                                               &rows[i].cells[1]);
            if (status != XXWIDGETS_OK) goto finish;
            status = win32_browser_number_cell(entry.packed_size,
                                               !!(entry.flags & XXWIDGETS_ARCHIVE_PACKED_SIZE_KNOWN),
                                               &rows[i].cells[2]);
            if (status != XXWIDGETS_OK) goto finish;
            status = win32_wide(xxwidgets_archivebrowser_cell(widget, i, XXWIDGETS_ARCHIVE_COLUMN_MODIFIED),
                                &rows[i].cells[3]);
            if (status != XXWIDGETS_OK) goto finish;
            status = win32_wide(xxwidgets_archivebrowser_cell(widget, i, XXWIDGETS_ARCHIVE_COLUMN_ATTRIBUTES),
                                &rows[i].cells[4]);
            if (status != XXWIDGETS_OK) goto finish;
            for (column = 5; column < columns; ++column) {
                status = win32_wide(xxwidgets_archivebrowser_cell(widget, i,
                    (xxwidgets_archive_column)column), &rows[i].cells[column]);
                if (status != XXWIDGETS_OK) goto finish;
            }
        }
    }
    if (rebuild) {
        SendMessageW(list, WM_SETREDRAW, FALSE, 0);
        redraw_disabled = 1;
        {
            HWND header = (HWND)SendMessageW(list, LVM_GETHEADER, 0, 0);
            while (SendMessageW(header, HDM_GETITEMCOUNT, 0, 0) > 5)
                if (!SendMessageW(list, LVM_DELETECOLUMN, 5, 0)) {
                    status = XXWIDGETS_PLATFORM_ERROR; goto finish;
                }
            for (column = 5; column < columns; ++column) {
                LVCOLUMNW heading;
                wchar_t *title = NULL;
                size_t length;
                status = win32_wide(xxwidgets_archivebrowser_column_title(widget, (size_t)column), &title);
                if (status != XXWIDGETS_OK) goto finish;
                length = wcslen(title);
                memset(&heading, 0, sizeof(heading));
                heading.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;
                heading.pszText = title; heading.iSubItem = column; heading.fmt = LVCFMT_LEFT;
                heading.cx = (int)(length < 12 ? 12 : length > 32 ? 32 : length) *
                    ((xxwidgets_win32_app *)widget->app->platform)->cell_width;
                if (SendMessageW(list, LVM_INSERTCOLUMNW, (WPARAM)column, (LPARAM)&heading) == -1)
                    status = XXWIDGETS_PLATFORM_ERROR;
                free(title);
                if (status != XXWIDGETS_OK) goto finish;
            }
        }
        if (!SendMessageW(list, LVM_DELETEALLITEMS, 0, 0)) {
            status = XXWIDGETS_PLATFORM_ERROR;
            goto finish;
        }
        for (i = 0; i < count; ++i) {
            memset(&item, 0, sizeof(item));
            item.mask = LVIF_TEXT | LVIF_IMAGE;
            item.iItem = (int)i;
            item.pszText = rows[i].cells[0];
            item.iImage = rows[i].image;
            if (SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item) == -1) {
                status = XXWIDGETS_PLATFORM_ERROR;
                goto finish;
            }
            for (column = 1; column < columns; ++column) {
                item.mask = LVIF_TEXT;
                item.iSubItem = column;
                item.pszText = rows[i].cells[column];
                if (!SendMessageW(list, LVM_SETITEMW, 0, (LPARAM)&item)) {
                    status = XXWIDGETS_PLATFORM_ERROR;
                    goto finish;
                }
            }
        }
        native->list_count = count;
        native->browser_revision = widget->browser_revision;
        native->list_initialized = 1;
    }
    {
        memset(&item, 0, sizeof(item));
        item.stateMask = LVIS_FOCUSED;
        SendMessageW(list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&item);
        for (i = 0; i < count; ++i) {
            unsigned int desired = xxwidgets_archivebrowser_row_selected(widget, i) ? LVIS_SELECTED : 0;
            unsigned int actual = (unsigned int)SendMessageW(list, LVM_GETITEMSTATE, (WPARAM)i, LVIS_SELECTED);
            if (desired != actual || (int)i == widget->value) {
                item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
                item.state = desired | ((int)i == widget->value ? LVIS_FOCUSED : 0);
                SendMessageW(list, LVM_SETITEMSTATE, (WPARAM)i, (LPARAM)&item);
            }
        }
        if (widget->value >= 0 && old_selection != widget->value)
            SendMessageW(list, LVM_ENSUREVISIBLE, (WPARAM)widget->value, FALSE);
    }
    if (rebuild && count && widget->value == old_selection && top > 0) {
        if ((size_t)top >= count) top = (int)count - 1;
        SendMessageW(list, LVM_ENSUREVISIBLE, (WPARAM)top, FALSE);
    }
    {
        HWND header = (HWND)SendMessageW(list, LVM_GETHEADER, 0, 0);
        for (column = 0; column < columns; ++column) {
            HDITEMW heading;
            memset(&heading, 0, sizeof(heading));
            heading.mask = HDI_FORMAT;
            if (SendMessageW(header, HDM_GETITEMW, (WPARAM)column, (LPARAM)&heading)) {
                heading.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
                if (column == (int)xxwidgets_archivebrowser_sort_column(widget))
                    heading.fmt |= xxwidgets_archivebrowser_sort_descending(widget) ? HDF_SORTDOWN : HDF_SORTUP;
                SendMessageW(header, HDM_SETITEMW, (WPARAM)column, (LPARAM)&heading);
            }
        }
    }
    status = XXWIDGETS_OK;
finish:
    if (status == XXWIDGETS_OK) win32_browser_fit_columns(widget, 0);
    if (redraw_disabled) {
        SendMessageW(list, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(list, NULL, TRUE);
    }
    if (rows) {
        for (i = 0; i < count; ++i) {
            if (rows[i].cells)
                for (column = 0; column < columns; ++column) free(rows[i].cells[column]);
            free(rows[i].cells);
        }
        free(rows);
    }
    return status;
}

static int win32_same_rect(xxwidgets_rect a, xxwidgets_rect b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

static xxwidgets_status win32_scanresults_columns(xxwidgets_widget *widget)
{
    static const wchar_t *titles[4] = {L"Type", L"Name", L"Version", L"Info"};
    static const int widths[4] = {14, 30, 14, 32};
    xxwidgets_win32_app *state = (xxwidgets_win32_app *)widget->app->platform;
    HWND handle = (HWND)widget->native;
    int column;
    SendMessageW(handle, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    for (column = 0; column < 4; ++column) {
        LVCOLUMNW heading;
        memset(&heading, 0, sizeof(heading));
        heading.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;
        heading.pszText = (wchar_t *)titles[column]; heading.iSubItem = column;
        heading.cx = widths[column] * state->cell_width; heading.fmt = LVCFMT_LEFT;
        if (SendMessageW(handle, LVM_INSERTCOLUMNW, (WPARAM)column, (LPARAM)&heading) == -1)
            return XXWIDGETS_PLATFORM_ERROR;
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status win32_sync_scanresults(xxwidgets_widget *widget)
{
    HWND handle = (HWND)widget->native;
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
    size_t row, column, count = widget->item_count;
    wchar_t **cells = NULL;
    LVITEMW item;
    int rebuild = !native->list_initialized || native->scan_revision != widget->scan_revision;
    int redraw_disabled = 0;
    xxwidgets_status status = XXWIDGETS_OK;
    if (rebuild && count) {
        if (count > SIZE_MAX / (4 * sizeof(*cells))) return XXWIDGETS_INVALID_ARGUMENT;
        cells = (wchar_t **)calloc(count * 4, sizeof(*cells));
        if (!cells) return XXWIDGETS_OUT_OF_MEMORY;
        for (row = 0; row < count; ++row)
            for (column = 0; column < 4; ++column) {
                status = win32_wide(xxwidgets_scanresults_cell(widget, row, column), &cells[row * 4 + column]);
                if (status != XXWIDGETS_OK) goto finish;
            }
    }
    if (rebuild) {
        SendMessageW(handle, WM_SETREDRAW, FALSE, 0); redraw_disabled = 1;
        /* Force rollback to rebuild even if the OS rejects part of replacement. */
        native->list_initialized = 0;
        if (!SendMessageW(handle, LVM_DELETEALLITEMS, 0, 0)) { status = XXWIDGETS_PLATFORM_ERROR; goto finish; }
        for (row = 0; row < count; ++row) {
            memset(&item, 0, sizeof(item));
            item.mask = LVIF_TEXT; item.iItem = (int)row; item.pszText = cells[row * 4];
            if (SendMessageW(handle, LVM_INSERTITEMW, 0, (LPARAM)&item) == -1) {
                status = XXWIDGETS_PLATFORM_ERROR; goto finish;
            }
            for (column = 1; column < 4; ++column) {
                item.iSubItem = (int)column; item.pszText = cells[row * 4 + column];
                if (!SendMessageW(handle, LVM_SETITEMW, 0, (LPARAM)&item)) {
                    status = XXWIDGETS_PLATFORM_ERROR; goto finish;
                }
            }
        }
        native->list_count = count; native->scan_revision = widget->scan_revision;
        native->list_initialized = 1;
    }
    {
        int current = (int)SendMessageW(handle, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
        if (rebuild || current != widget->value) {
            memset(&item, 0, sizeof(item)); item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
            SendMessageW(handle, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&item);
            if (widget->value >= 0) {
                item.state = LVIS_SELECTED | LVIS_FOCUSED;
                SendMessageW(handle, LVM_SETITEMSTATE, (WPARAM)widget->value, (LPARAM)&item);
                SendMessageW(handle, LVM_ENSUREVISIBLE, (WPARAM)widget->value, FALSE);
            }
        }
    }
finish:
    if (redraw_disabled) {
        SendMessageW(handle, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(handle, NULL, TRUE);
    }
    if (cells) for (row = 0; row < count * 4; ++row) free(cells[row]);
    free(cells); return status;
}

static xxwidgets_status win32_sync_tree(xxwidgets_widget *widget)
{
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
    HWND handle = (HWND)widget->native;
    size_t count = xxwidgets_treeview_count(widget), i;
    int rebuild = !native->tree_initialized ||
        native->tree_content_revision != widget->tree_content_revision;
    wchar_t **texts = NULL;
    HTREEITEM *items = NULL;
    xxwidgets_status status = XXWIDGETS_OK;
    if (rebuild && count) {
        texts = (wchar_t **)calloc(count, sizeof(*texts));
        items = (HTREEITEM *)calloc(count, sizeof(*items));
        if (!texts || !items) { status = XXWIDGETS_OUT_OF_MEMORY; goto finish; }
        for (i = 0; i < count; ++i) {
            status = win32_wide(xxwidgets_treeview_display_text(widget, i), &texts[i]);
            if (status != XXWIDGETS_OK) goto finish;
        }
    }
    if (rebuild) {
        native->tree_initialized = 0;
        SendMessageW(handle, WM_SETREDRAW, FALSE, 0);
        SendMessageW(handle, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
        for (i = 0; i < count; ++i) {
            xxwidgets_tree_node node;
            TVINSERTSTRUCTW insertion;
            xxwidgets_treeview_get_node(widget, i, &node);
            memset(&insertion, 0, sizeof(insertion));
            insertion.hParent = node.parent == SIZE_MAX ? TVI_ROOT : items[node.parent];
            insertion.hInsertAfter = TVI_LAST;
            insertion.item.mask = TVIF_TEXT | TVIF_PARAM;
            insertion.item.pszText = texts[i]; insertion.item.lParam = (LPARAM)i;
            items[i] = (HTREEITEM)SendMessageW(handle, TVM_INSERTITEMW, 0, (LPARAM)&insertion);
            if (!items[i]) { status = XXWIDGETS_PLATFORM_ERROR; break; }
        }
        SendMessageW(handle, WM_SETREDRAW, TRUE, 0); InvalidateRect(handle, NULL, TRUE);
        if (status != XXWIDGETS_OK) goto finish;
        free(native->tree_items); native->tree_items = items; items = NULL;
        native->tree_count = count; native->tree_content_revision = widget->tree_content_revision;
        native->tree_initialized = 1;
    }
    for (i = 0; i < count; ++i) {
        int expanded;
        if (!xxwidgets_treeview_has_children(widget, i)) continue;
        xxwidgets_treeview_get_expanded(widget, i, &expanded);
        SendMessageW(handle, TVM_EXPAND, expanded ? TVE_EXPAND : TVE_COLLAPSE,
            (LPARAM)native->tree_items[i]);
    }
    SendMessageW(handle, TVM_SELECTITEM, TVGN_CARET,
        widget->value < 0 ? 0 : (LPARAM)native->tree_items[widget->value]);
    if (widget->value >= 0)
        SendMessageW(handle, TVM_ENSUREVISIBLE, 0, (LPARAM)native->tree_items[widget->value]);
finish:
    if (texts) for (i = 0; i < count; ++i) free(texts[i]);
    free(texts); free(items); return status;
}

static xxwidgets_status win32_sync(xxwidgets_widget *widget)
{
    HWND handle = (HWND)widget->native;
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
    xxwidgets_status status;
    wchar_t *text;
    wchar_t *current;
    if (!handle || !native) return XXWIDGETS_PLATFORM_ERROR;
    if (!win32_same_rect(native->applied_rect, widget->rect)) {
        int x, y, width, height;
        status = win32_geometry(widget, (DWORD)GetWindowLongPtrW(handle, GWL_STYLE),
                                (DWORD)GetWindowLongPtrW(handle, GWL_EXSTYLE),
                                &x, &y, &width, &height);
        if (status != XXWIDGETS_OK) return status;
        if (widget->kind == XXWIDGETS_COMBOBOX)
            height += 10 * ((xxwidgets_win32_app *)widget->app->platform)->cell_height;
        if (!SetWindowPos(handle, NULL, x, y, width, height, SWP_NOACTIVATE | SWP_NOZORDER))
            return XXWIDGETS_PLATFORM_ERROR;
        native->applied_rect = widget->rect;
    }
    if (xxwidgets_combo_kind(widget)) {
        status = win32_sync_combo(widget);
        if (status != XXWIDGETS_OK) return status;
    } else if (widget->kind == XXWIDGETS_ARCHIVEBROWSER) {
        status = win32_sync_browser(widget);
        if (status != XXWIDGETS_OK) return status;
    } else if (widget->kind == XXWIDGETS_SCANRESULTS) {
        status = win32_sync_scanresults(widget);
        if (status != XXWIDGETS_OK) return status;
    } else if (widget->kind == XXWIDGETS_TREEVIEW) {
        status = win32_sync_tree(widget);
        if (status != XXWIDGETS_OK) return status;
    } else if (xxwidgets_list_kind(widget)) {
        status = win32_sync_list(widget);
        if (status != XXWIDGETS_OK) return status;
    } else {
        status = win32_wide(widget->text, &text);
        if (status != XXWIDGETS_OK) return status;
        status = win32_window_text(handle, &current);
        if (status == XXWIDGETS_OK) {
            if (wcscmp(current, text) != 0 && !SetWindowTextW(handle, text))
                status = XXWIDGETS_PLATFORM_ERROR;
            free(current);
        }
        free(text);
        if (status != XXWIDGETS_OK) return status;
    }
    if (widget->kind == XXWIDGETS_CHECKBOX)
        SendMessageW(handle, BM_SETCHECK, widget->value ? BST_CHECKED : BST_UNCHECKED, 0);
    if (widget->kind == XXWIDGETS_PROGRESS)
        SendMessageW(handle, PBM_SETPOS, (WPARAM)widget->value, 0);
    if (!(GetWindowLongPtrW(handle, GWL_STYLE) & WS_DISABLED) != !!widget->enabled)
        EnableWindow(handle, widget->enabled);
    if (widget->kind == XXWIDGETS_WINDOW && (!widget->enabled || !widget->visible)) {
        xxwidgets_widget *child;
        for (child = widget->app->widgets; child; child = child->next)
            if (child->parent == widget && child->kind == XXWIDGETS_CHECKCOMBOBOX) {
                xxwidgets_win32_widget *combo = child->platform;
                if (combo && combo->combo_popup) ShowWindow(combo->combo_popup, SW_HIDE);
            }
    }
    /* IsWindowVisible also examines ancestors, so use this control's own bit. */
    if (!!(GetWindowLongPtrW(handle, GWL_STYLE) & WS_VISIBLE) != !!widget->visible)
        ShowWindow(handle, widget->visible ? (widget->kind == XXWIDGETS_WINDOW ? SW_SHOWNORMAL : SW_SHOW) : SW_HIDE);
    return XXWIDGETS_OK;
}

static xxwidgets_status win32_read_text(xxwidgets_widget *widget)
{
    wchar_t *wide;
    char *text;
    int length;
    xxwidgets_status status;
    /* List items and progress captions are maintained by the portable model. */
    if (xxwidgets_list_kind(widget) || xxwidgets_combo_kind(widget) || widget->kind == XXWIDGETS_ARCHIVEBROWSER ||
        widget->kind == XXWIDGETS_PROGRESS)
        return XXWIDGETS_OK;
    if (!widget->native) return XXWIDGETS_PLATFORM_ERROR;
    status = win32_window_text((HWND)widget->native, &wide);
    if (status != XXWIDGETS_OK) return status;
    length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, NULL, 0, NULL, NULL);
    if (!length) { free(wide); return XXWIDGETS_PLATFORM_ERROR; }
    text = (char *)malloc((size_t)length);
    if (!text) { free(wide); return XXWIDGETS_OUT_OF_MEMORY; }
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, text, length, NULL, NULL)) {
        free(wide);
        free(text);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    free(wide);
    status = xxwidgets_store_text(widget, text);
    free(text);
    return status;
}

static xxwidgets_status win32_read_value(xxwidgets_widget *widget)
{
    HWND handle = (HWND)widget->native;
    if (!handle) return XXWIDGETS_PLATFORM_ERROR;
    switch (widget->kind) {
    case XXWIDGETS_COMBOBOX:
        widget->value = (int)SendMessageW(handle, CB_GETCURSEL, 0, 0); break;
    case XXWIDGETS_CHECKBOX:
        widget->value = SendMessageW(handle, BM_GETCHECK, 0, 0) == BST_CHECKED;
        break;
    case XXWIDGETS_LISTBOX:
    case XXWIDGETS_ARCHIVEVIEW:
    case XXWIDGETS_HEXVIEW:
        widget->value = (int)SendMessageW(handle, LB_GETCURSEL, 0, 0);
        break;
    case XXWIDGETS_PROGRESS:
        widget->value = (int)SendMessageW(handle, PBM_GETPOS, 0, 0);
        break;
    case XXWIDGETS_SCANRESULTS:
        widget->value = (int)SendMessageW(handle, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
        break;
    case XXWIDGETS_TREEVIEW:
        {
            xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
            TVITEMW item;
            if (!native || !native->tree_initialized || native->tree_content_revision != widget->tree_content_revision ||
                native->tree_count != xxwidgets_treeview_count(widget)) return XXWIDGETS_PLATFORM_ERROR;
            memset(&item, 0, sizeof(item)); item.mask = TVIF_PARAM;
            item.hItem = (HTREEITEM)SendMessageW(handle, TVM_GETNEXTITEM, TVGN_CARET, 0);
            if (!item.hItem) widget->value = -1;
            else if (!SendMessageW(handle, TVM_GETITEMW, 0, (LPARAM)&item) ||
                (size_t)item.lParam >= xxwidgets_treeview_count(widget)) return XXWIDGETS_PLATFORM_ERROR;
            else widget->value = (int)item.lParam;
        }
        break;
    case XXWIDGETS_ARCHIVEBROWSER:
        {
            xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)widget->platform;
            widget->value = (int)SendMessageW(native->browser_list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_FOCUSED);
            if (widget->value < 0 || !xxwidgets_archivebrowser_row_selected(widget, (size_t)widget->value))
                widget->value = (int)SendMessageW(native->browser_list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
        }
        break;
    default:
        break;
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status win32_focus(xxwidgets_widget *widget)
{
    HWND handle = (HWND)widget->native;
    if (widget->kind == XXWIDGETS_ARCHIVEBROWSER && widget->platform)
        handle = ((xxwidgets_win32_widget *)widget->platform)->browser_list;
    if (!handle) return XXWIDGETS_PLATFORM_ERROR;
    SetFocus(handle);
    return GetFocus() == handle ? XXWIDGETS_OK : XXWIDGETS_PLATFORM_ERROR;
}

static int win32_shortcut(xxwidgets_widget *window, const MSG *message)
{
    unsigned int key = (unsigned int)message->wParam, modifiers = 0;
    if (!window || (message->message != WM_KEYDOWN && message->message != WM_SYSKEYDOWN)) return 0;
    if (key >= VK_F1 && key <= VK_F24) key = XXWIDGETS_KEY_F1 + key - VK_F1;
    else if (!(key >= 'A' && key <= 'Z') && !(key >= '0' && key <= '9')) {
        switch (key) {
        case VK_ESCAPE: key = XXWIDGETS_KEY_ESCAPE; break;
        case VK_RETURN: key = XXWIDGETS_KEY_ENTER; break;
        case VK_TAB: key = XXWIDGETS_KEY_TAB; break;
        case VK_BACK: key = XXWIDGETS_KEY_BACKSPACE; break;
        case VK_DELETE: key = XXWIDGETS_KEY_DELETE; break;
        case VK_INSERT: key = XXWIDGETS_KEY_INSERT; break;
        case VK_HOME: key = XXWIDGETS_KEY_HOME; break;
        case VK_END: key = XXWIDGETS_KEY_END; break;
        case VK_PRIOR: key = XXWIDGETS_KEY_PAGEUP; break;
        case VK_NEXT: key = XXWIDGETS_KEY_PAGEDOWN; break;
        case VK_UP: key = XXWIDGETS_KEY_UP; break;
        case VK_DOWN: key = XXWIDGETS_KEY_DOWN; break;
        case VK_LEFT: key = XXWIDGETS_KEY_LEFT; break;
        case VK_RIGHT: key = XXWIDGETS_KEY_RIGHT; break;
        case VK_SPACE: key = ' '; break;
        case VK_ADD: key = '+'; break;
        case VK_SUBTRACT: case VK_OEM_MINUS: key = '-'; break;
        case VK_OEM_PLUS: key = '='; break;
        case VK_OEM_COMMA: key = ','; break;
        case VK_OEM_PERIOD: key = '.'; break;
        case VK_OEM_1: key = ';'; break;
        case VK_OEM_2: key = '/'; break;
        case VK_OEM_4: key = '['; break;
        case VK_OEM_5: key = '\\'; break;
        case VK_OEM_6: key = ']'; break;
        case VK_OEM_7: key = '\''; break;
        default: return 0;
        }
    }
    if (GetKeyState(VK_CONTROL) < 0) modifiers |= XXWIDGETS_MOD_CTRL;
    if (GetKeyState(VK_MENU) < 0) modifiers |= XXWIDGETS_MOD_ALT;
    if (GetKeyState(VK_SHIFT) < 0) modifiers |= XXWIDGETS_MOD_SHIFT;
    if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) modifiers |= XXWIDGETS_MOD_META;
    return xxwidgets_shortcut_dispatch(window, key, modifiers);
}

static xxwidgets_status win32_poll(xxwidgets_app *app, int timeout_ms)
{
    DWORD started = GetTickCount();
    unsigned int processed = 0;
    int waited = 0;
    MSG message;
    for (;;) {
        if (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            HWND root;
            xxwidgets_widget *window;
            if (message.message == WM_QUIT) {
                /* A caller may own a separate WinAPI loop on this UI thread. */
                PostQuitMessage((int)message.wParam);
                return XXWIDGETS_OK;
            }
            root = message.hwnd ? GetAncestor(message.hwnd, GA_ROOT) : NULL;
            if (app->modal_window && root == (HWND)app->modal_window->native && message.message == WM_KEYDOWN) {
                if (message.wParam == VK_ESCAPE) {
                    xxwidgets_emit(app->modal_window, XXWIDGETS_EVENT_CLOSE, 0); continue;
                }
                if (message.wParam == VK_RETURN) {
                    xxwidgets_widget *button = app->modal_default, *candidate;
                    HWND focus = GetFocus();
                    for (candidate = app->widgets; candidate; candidate = candidate->next)
                        if (candidate->parent == app->modal_window && candidate->kind == XXWIDGETS_BUTTON) {
                            if ((HWND)candidate->native == focus) { button = candidate; break; }
                            if (!strcmp(candidate->text, "OK")) button = candidate;
                        }
                    if (button) SendMessageW((HWND)button->native, BM_CLICK, 0, 0);
                    continue;
                }
            }
            window = root ? win32_find_widget(app, root) : NULL;
            /* Only interpret dialog navigation for windows belonging to this app. */
            if (!win32_shortcut(window, &message) && (!window || !IsDialogMessageW(root, &message))) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            ++processed;
            if (app->quit || processed >= 256 ||
                (timeout_ms > 0 && GetTickCount() - started >= (DWORD)timeout_ms))
                return XXWIDGETS_OK;
            continue;
        }
        if (processed || waited || timeout_ms == 0 || app->quit) return XXWIDGETS_OK;
        {
            DWORD elapsed = GetTickCount() - started;
            DWORD result;
            if (elapsed >= (DWORD)timeout_ms) return XXWIDGETS_OK;
            result = MsgWaitForMultipleObjectsEx(0, NULL, (DWORD)timeout_ms - elapsed,
                                                 QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (result == WAIT_FAILED) return XXWIDGETS_PLATFORM_ERROR;
            if (result == WAIT_TIMEOUT) return XXWIDGETS_OK;
            waited = 1;
        }
    }
}

static xxwidgets_status win32_modal_owner(xxwidgets_widget *dialog, xxwidgets_widget *owner, int active)
{
    HWND window = (HWND)dialog->native, parent = (HWND)owner->native;
    RECT bounds, area;
    SetLastError(0);
    if (!SetWindowLongPtrW(window, GWLP_HWNDPARENT, active ? (LONG_PTR)parent : 0) && GetLastError())
        return XXWIDGETS_PLATFORM_ERROR;
    if (active && GetWindowRect(window, &bounds) && GetWindowRect(parent, &area)) {
        int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        int x = area.left + ((area.right - area.left) - width) / 2;
        int y = area.top + ((area.bottom - area.top) - height) / 2;
        xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)dialog->platform;
        if (native && native->font_layout) {
            MONITORINFO monitor = {sizeof(monitor)};
            if (GetMonitorInfoW(MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST), &monitor)) {
                if (x + width > monitor.rcWork.right) x = monitor.rcWork.right - width;
                if (x < monitor.rcWork.left) x = monitor.rcWork.left;
                if (y + height > monitor.rcWork.bottom) y = monitor.rcWork.bottom - height;
                if (y < monitor.rcWork.top) y = monitor.rcWork.top;
            }
        }
        SetWindowPos(window, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    if (active) {
        SendMessageW(window, WM_SETICON, ICON_BIG, SendMessageW(parent, WM_GETICON, ICON_BIG, 0));
        SendMessageW(window, WM_SETICON, ICON_SMALL, SendMessageW(parent, WM_GETICON, ICON_SMALL, 0));
    }
    if (!active) SetActiveWindow(parent);
    return XXWIDGETS_OK;
}

static xxwidgets_status win32_about_content(xxwidgets_widget *window,
    const xxwidgets_about_dialog *about, const char *body)
{
    xxwidgets_win32_app *app = (xxwidgets_win32_app *)window->app->platform;
    xxwidgets_win32_widget *native = (xxwidgets_win32_widget *)window->platform;
    HWND handle = (HWND)window->native, edit;
    RECT client;
    wchar_t *wide;
    char *lines;
    size_t i, length = strlen(body), used = 0;
    int padding = 2 * app->cell_width, image_box = 12 * app->cell_width;
    int text_x = about->image ? image_box + 2 * padding : padding;
    xxwidgets_status status;
    LONG_PTR style = GetWindowLongPtrW(handle, GWL_STYLE);
    SetWindowLongPtrW(handle, GWL_STYLE, style & ~(WS_THICKFRAME | WS_MAXIMIZEBOX));
    if (!SetWindowPos(handle, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER) ||
        !GetClientRect(handle, &client)) return XXWIDGETS_PLATFORM_ERROR;
    if (length > (SIZE_MAX - 1) / 2) return XXWIDGETS_OUT_OF_MEMORY;
    lines = (char *)malloc(length * 2 + 1);
    if (!lines) return XXWIDGETS_OUT_OF_MEMORY;
    for (i = 0; i < length; ++i) {
        if (body[i] == '\n' && (!i || body[i - 1] != '\r')) lines[used++] = '\r';
        lines[used++] = body[i];
    }
    lines[used] = 0;
    status = win32_wide(lines, &wide); free(lines);
    if (status != XXWIDGETS_OK) return status;
    edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        text_x, app->cell_height, client.right - text_x - padding, 17 * app->cell_height,
        handle, (HMENU)(INT_PTR)101, app->instance, NULL);
    if (!edit) { free(wide); return XXWIDGETS_PLATFORM_ERROR; }
    native->about_edit = edit;
    SendMessageW(edit, WM_SETFONT, (WPARAM)app->role_fonts[XXWIDGETS_FONT_TEXT_EDITS], FALSE);
    SendMessageW(edit, EM_SETLIMITTEXT, (WPARAM)INT_MAX - 1, 0);
    if (!SetWindowTextW(edit, wide)) { free(wide); return XXWIDGETS_PLATFORM_ERROR; }
    free(wide);
    if (about->image) {
        BITMAPINFO info;
        unsigned char *pixels;
        HWND picture;
        COLORREF background = GetSysColor(COLOR_BTNFACE);
        unsigned int w = (unsigned int)image_box, h = (unsigned int)image_box, x, y;
        if (about->image_width >= about->image_height)
            h = (unsigned int)((uint64_t)w * about->image_height / about->image_width);
        else w = (unsigned int)((uint64_t)h * about->image_width / about->image_height);
        if (!w) w = 1;
        if (!h) h = 1;
        memset(&info, 0, sizeof(info));
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = (LONG)w; info.bmiHeader.biHeight = -(LONG)h;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        native->about_bitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
        if (!native->about_bitmap) return XXWIDGETS_PLATFORM_ERROR;
        for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) {
            const unsigned char *src = about->image +
                ((size_t)((uint64_t)y * about->image_height / h) * about->image_width +
                 (size_t)((uint64_t)x * about->image_width / w)) * 4;
            unsigned char *dst = pixels + ((size_t)y * w + x) * 4;
            unsigned int alpha = src[3];
            dst[0] = (unsigned char)((src[2] * alpha + GetBValue(background) * (255 - alpha) + 127) / 255);
            dst[1] = (unsigned char)((src[1] * alpha + GetGValue(background) * (255 - alpha) + 127) / 255);
            dst[2] = (unsigned char)((src[0] * alpha + GetRValue(background) * (255 - alpha) + 127) / 255);
            /* Already composited; a zero reserved byte avoids SS_BITMAP making an alpha copy. */
            dst[3] = 0;
        }
        picture = CreateWindowExW(0, L"STATIC", L"About image", WS_CHILD | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE,
            padding, app->cell_height, image_box, image_box, handle, (HMENU)(INT_PTR)102, app->instance, NULL);
        if (!picture) return XXWIDGETS_PLATFORM_ERROR;
        SendMessageW(picture, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)native->about_bitmap);
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status win32_copy_text(xxwidgets_widget *window, const char *text)
{
    wchar_t *wide = NULL;
    HGLOBAL memory;
    wchar_t *target;
    size_t length, i, used = 0;
    xxwidgets_status status = win32_wide(text, &wide);
    if (status != XXWIDGETS_OK) return status;
    length = wcslen(wide);
    if (length > (SIZE_MAX / sizeof(*wide) - 1) / 2) { free(wide); return XXWIDGETS_OUT_OF_MEMORY; }
    length = (length * 2 + 1) * sizeof(*wide);
    memory = GlobalAlloc(GMEM_MOVEABLE, length);
    target = memory ? GlobalLock(memory) : NULL;
    if (!target) { if (memory) GlobalFree(memory); free(wide); return XXWIDGETS_OUT_OF_MEMORY; }
    for (i = 0; wide[i]; ++i) {
        if (wide[i] == L'\n' && (!i || wide[i - 1] != L'\r')) target[used++] = L'\r';
        target[used++] = wide[i];
    }
    target[used] = 0;
    GlobalUnlock(memory); free(wide);
    status = XXWIDGETS_PLATFORM_ERROR;
    if (OpenClipboard((HWND)window->native)) {
        if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory)) {
            memory = NULL; status = XXWIDGETS_OK;
        }
        CloseClipboard();
    }
    if (memory) GlobalFree(memory);
    return status;
}

const xxwidgets_backend_ops xxwidgets_native_ops = {
    "WinAPI", win32_init, win32_shutdown, win32_poll, win32_create, win32_destroy,
    win32_sync, win32_read_text, win32_read_value, win32_focus, win32_modal_owner, win32_about_content, win32_copy_text,
    win32_apply_fonts, win32_choose_font, win32_preview_font, win32_font_options_layout, win32_optimization_options_layout
};
