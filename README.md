# xxwidgets

`xxwidgets` is a C11 widget library with one API for native desktop controls
and a keyboard-operated terminal UI. It follows the sibling `xxfclib` and
`xxemul` projects: a standalone CMake library, public headers under
`include/xxwidgets`, optional examples/tests, and static or shared builds.
It has no Qt dependency.

| Platform | Native backend | Terminal backend |
| --- | --- | --- |
| Windows | Unicode WinAPI controls and common controls | Windows console screen buffer |
| Linux / Unix desktop | GTK 3.16 or newer | POSIX terminal, termios, ANSI escape sequences |
| macOS | AppKit controls through a private Objective-C bridge | POSIX terminal, termios, ANSI escape sequences |

The public interface and application code are C. macOS enables Objective-C
only for its private AppKit implementation. Native controls retain their OS
appearance, keyboard behavior, accessibility, and input methods.

## Current controls

- Top-level windows, labels, push buttons, single-line text fields
- Checkboxes, single-selection scrolling lists, progress bars (0–100)
- Normal and checkbox comboboxes with typed `xx_meta_string` records
- Modal application options forms with checkboxes, OK and Cancel
- Reusable About dialogs with application-defined text and RGBA images
- Read-only HexView with addresses, hexadecimal bytes, ASCII, and row selection
- Read-only ArchiveView with UTF-8 paths, directory markers, byte sizes, and entry selection
- ArchiveBrowser with an address bar, folder navigation, icons, and sortable metadata columns
- UTF-8 text, visibility, enablement, focus, application/widget user data
- Click, change, list selection, close, resize, activation, and context-menu request events
- A shared demo that runs with either native controls or TUI
- A HexView demo with sample bytes or file loading
- An ArchiveView demo with copied archive-entry metadata
- An ArchiveBrowser demo with explicit and inferred folders and file activation
- Dedicated demos for every basic control and for Options, About and persistent settings

This is an initial common control set. Menus as a public API, nested containers,
automatic layout, multiline/rich text, tree/table models, drawing surfaces,
file dialogs, and mobile backends are outside the current API.

`xxwidgets_options_dialog(owner, title, options, count, &accepted)` presents
up to 16 application-defined boolean options. It disables the owner while open
and applies values only on OK. Cancel, Escape, or closing the dialog leaves the
supplied values unchanged. Invoke it on the UI thread after returning from an
event callback, for example through a pending menu action in the main loop.
The same form works with the native and terminal backends.

`xxwidgets_text_dialog(owner, title, text)` shows a scrollable, selectable text
report with **Close** and **Copy all** on native desktops. Copy places the entire
UTF-8 report on the system clipboard and changes the button to **Copied**; a
clipboard failure is displayed on the button. Terminal backends show a scrolling
list with Close. Call outside an event callback, with the same modal rules as the
options and About dialogs.

About dialogs are configured through public methods and can be shown repeatedly:

```c
xxwidgets_about_dialog *about = NULL;
xxwidgets_about_dialog_create(&about);
xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_TITLE, "About My App");
xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_PROGRAM_NAME, "My App");
xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_VERSION, "Version 1.0");
xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_DESCRIPTION, "Application description.");
xxwidgets_about_dialog_set_image(about, rgba_pixels, width, height, stride);
/* On the UI thread, after the menu event callback returns: */
xxwidgets_about_dialog_show(about, owner_window);
xxwidgets_about_dialog_destroy(about);
```

Check the returned status for each call. Text fields also include copyright,
website, license, credits, and the Close button label. Empty optional fields
are omitted. Setters copy UTF-8 strings and top-down, straight-alpha RGBA8
pixels; the application loads or decodes its image. Passing NULL/0/0/0 to
`set_image` removes it. The native dialog preserves the image's aspect ratio
and provides selectable, scrollable text. The TUI shows wrapped, scrollable
text. Close, Enter, Escape, or the window close button dismisses the dialog
and restores the owner's enabled state. Setters/destruction while showing,
or showing from an event callback/nested modal loop, return `XXWIDGETS_BUSY`.

For persistent application options, enable `XXWIDGETS_BUILD_SETTINGS` and link
`xxwidgets::settings`. This optional adapter builds xxfclib's settings subset
from `xxsettings.cmake`. Include `<xxwidgets/xxwidgets_settings.h>`, create and
load an `xx_settings` store, then supply checkbox labels, settings keys, and
boolean defaults to `xxwidgets_settings_options_dialog`. It reads typed
booleans from that store and saves changes on OK. Cancel leaves the store
unchanged; failures return a status with `accepted == 0`. The caller owns the
store and chooses native, INI, or memory storage. The boolean get/set helpers
let toolbar controls use the same settings. The base widget library can still
be built independently.

## Build

Comboboxes accept `xx_meta_string` from xxfclib's `xx_format.h`. Include
`<xxwidgets/xxwidgets_combobox.h>`, create `XXWIDGETS_COMBOBOX` or
`XXWIDGETS_CHECKCOMBOBOX`, and call `xxwidgets_combobox_set_records` with an
array and count. The label is a wide `xx_str_w_s`; its `xx_var` retains the
application's typed value. Duplicate labels remain distinct records.

```c
xxwidgets_combobox_set_records(combo, records, record_count);
xx_var current = {0};
xxwidgets_combobox_get_current(combo, &current);

xxwidgets_combobox_set_records(check_combo, records, record_count);
xxwidgets_checkcombobox_set_checked(check_combo, 0, 1);
size_t checked_count = 0;
xxwidgets_checkcombobox_get_checked(check_combo, NULL, 0, &checked_count);
/* Allocate checked_count pointers, then retrieve the checked records: */
xxwidgets_checkcombobox_get_checked(check_combo, checked_records, checked_count, &checked_count);
```

Labels, strings, wide strings, and byte values are copied. Generic pointer
values remain borrowed; keep their pointees alive. Retrieved values and record
pointers are read-only borrowed views until replacement or widget destruction;
do not free their buffers. A normal combo selects the first record initially
and emits `SELECT` with its index. A checkbox combo starts unchecked and emits
`CHANGE` with the toggled index. State is updated before callbacks; setters
emit no input events. NULL/count 0 clears either control. Checkbox results
follow the original input order.

Native controls provide dropdown lists/popovers. In the TUI, Enter opens or
closes a list, arrows navigate, Space toggles a checkbox, and Escape closes the
dropdown. `xxwidgets_combobox_demo --native` or `--tui` demonstrates both.
Builds use the sibling xxfclib headers (override `XXWIDGETS_XXFCLIB_DIR` when
needed), and installation includes the small public header subset needed for
these types. The base library does not link the format engine.

From this directory:

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Windows requires a C11 compiler (current MSVC or MinGW) and the Windows SDK.
For example, in a Visual Studio developer shell:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\xxwidgets_demo.exe --native
.\build\Release\xxwidgets_demo.exe --tui
.\build\Release\xxwidgets_hexview_demo.exe --native
.\build\Release\xxwidgets_hexview_demo.exe --tui
.\build\Release\xxwidgets_archiveview_demo.exe --native
.\build\Release\xxwidgets_archiveview_demo.exe --tui
.\build\Release\xxwidgets_archivebrowser_demo.exe --native
.\build\Release\xxwidgets_archivebrowser_demo.exe --tui
```

Linux desktop builds require `pkg-config` and GTK3 development headers/libraries
(for example `libgtk-3-dev` on Debian/Ubuntu or `gtk3-devel` on Fedora). A live
graphical session is required to initialize GTK. macOS requires the Xcode
command-line tools and AppKit SDK; use macOS 10.13 or newer. All AppKit calls,
including initialization and destruction, must run on the main thread.

For a terminal-only build on any supported OS, no GUI toolkit is needed:

```sh
cmake -S . -B build-tui -DXXWIDGETS_NATIVE_BACKEND=none
cmake --build build-tui --config Release
./build-tui/xxwidgets_demo --tui
```

The TUI needs interactive input and output. Initialization returns
`XXWIDGETS_UNAVAILABLE` for redirected streams. Only one TUI app may own a
terminal at a time. Normal destruction restores the console/terminal state.

| CMake option | Default | Purpose |
| --- | --- | --- |
| `BUILD_SHARED_LIBS` | `OFF` | Shared library instead of static archive |
| `XXWIDGETS_NATIVE_BACKEND` | `auto` | `auto`, `win32`, `gtk3`, `cocoa`, `none` |
| `XXWIDGETS_BUILD_EXAMPLES` | Top-level builds: `ON` | Build demos for every control and the common dialogs |
| `XXWIDGETS_BUILD_TESTS` | Top-level builds: `ON` | Build deterministic API tests and available platform tests |
| `XXWIDGETS_BUILD_SETTINGS` | `OFF` | Build the optional xxfclib settings adapter |
| `XXWIDGETS_XXFCLIB_DIR` | `../xxfclib` | xxfclib source directory for the settings subset |

`auto` chooses WinAPI on Windows, AppKit on macOS, GTK on other Unix systems.
Missing native development dependencies cause configuration to fail; select
`none` explicitly for a TUI-only build. Both native and TUI are included in
normal desktop builds. Runtime `AUTO` chooses the compiled native backend;
it chooses TUI in a TUI-only build. Native initialization failure is reported
to the application, which can explicitly request TUI if desired.

## Demos

See [the complete demo catalog](examples/README.md) for all executables,
covered widgets, and native/TUI usage. Enable `XXWIDGETS_BUILD_SETTINGS=ON`
to include the xxfclib-backed persistent-options demo. The existing combined
`xxwidgets_demo` remains available alongside dedicated examples for Window,
Label, Button, Edit, Checkbox, ListBox, Progress, the data views, comboboxes,
Options and About. Windows native demo smoke checks run automatically through
CTest when examples and tests are enabled.

## Integration

As a subdirectory:

```cmake
add_subdirectory(path/to/xxwidgets)
target_link_libraries(my_app PRIVATE xxwidgets::xxwidgets)
```

As an installed package:

```sh
cmake --install build --config Release --prefix /path/to/install
```

```cmake
find_package(xxwidgets 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE xxwidgets::xxwidgets)
```

Set `CMAKE_PREFIX_PATH` to the installation prefix. The exported target carries
the include path, static/DLL declarations, and platform link dependencies.

## API and ownership

Include `<xxwidgets/xxwidgets.h>`. Create an app, then a window, then controls
whose parent is that window. Destroying a window destroys all its controls;
destroying the app destroys its remaining windows and controls. All handles
are invalid after destruction. UTF-8 text, list strings, and archive entry paths are copied. Invalid
UTF-8, widget/value combinations, parents, and rectangles return a status.

Rectangles use integer text-cell units. Native backends scale them using
system font metrics; TUI uses terminal columns and rows. Window dimensions
describe the client area. Child coordinates start at the window's client
origin. Coordinates must be 0–32767, dimensions 1–32767. Desktop window
placement is advisory because the window manager can choose its position.
TUI clips windows and children to the screen.

All calls belong to one UI thread. Call `xxwidgets_app_run` for the built-in
loop or `xxwidgets_app_poll` to integrate an existing loop; polling takes a
nonnegative timeout in milliseconds. Callback handlers may update/focus
controls and call `xxwidgets_app_quit`. Creation, destruction, and nested
event loops return `XXWIDGETS_BUSY` while handling an event; perform them after
polling returns. Programmatic setters do not emit user-input events.

With no callback, closing a window quits the app. When a callback is supplied,
it must handle `XXWIDGETS_EVENT_CLOSE`, typically by calling
`xxwidgets_app_quit(app, 0)`. Close events leave the window owned by the app
until explicit destruction. `xxwidgets_app_run` returns the requested exit
code, or `-1` on a backend polling error.

`xxwidgets_widget_get_text` supports size queries with a NULL buffer and zero
capacity. The required size includes the terminator. Too-small buffers return
`XXWIDGETS_BUFFER_TOO_SMALL` and preserve complete UTF-8 characters.
`xxwidgets_widget_native_handle` returns an `HWND`, `GtkWidget *`, or AppKit
object pointer for native controls; TUI returns NULL. The caller does not own
that handle and must not destroy native objects directly.

See `examples/widgets_demo.c` for the complete common application.

## HexView

Create a control with `XXWIDGETS_HEXVIEW`, then call
`xxwidgets_hexview_set_data(widget, bytes, size)`. The data is copied; NULL
with size 0 clears the view. Each row shows a 16-digit hexadecimal address,
hexadecimal bytes, and printable ASCII (other bytes display as `.`). The
partial final row is padded for alignment. Desktop backends use native
scrolling list/table controls with monospace fonts; TUI uses the same rows.

`xxwidgets_hexview_set_layout(widget, base_address, bytes_per_row)` accepts
8, 16, or 32 bytes per row. Addresses are 64-bit and overflow is rejected.
Changing the layout keeps the row containing the previously selected byte;
setting new data selects the first row. Both operations suppress input events.
Rows are formatted eagerly and the copied bytes and formatted rows remain
owned by the widget, so memory usage grows with data size.

`XXWIDGETS_EVENT_SELECT` reports the selected row in `event.value`.
`xxwidgets_widget_set_value` / `get_value` also use row indices (-1 clears
selection). `xxwidgets_hexview_get_selection` returns that row's byte offset
and length, including a short final row; no selection returns `SIZE_MAX, 0`.
Offsets refer to the supplied buffer; add the base address for display.
`xxwidgets_hexview_size` returns the copied buffer size. HexView is read-only,
with selection of whole rows rather than individual bytes.

The separate `xxwidgets_hexview_demo` accepts:

```text
xxwidgets_hexview_demo [--native|--tui] [--cols 8|16|32] [--base address] [file]
```

Without a file it shows built-in sample data, including nonprintable bytes and
a partial final row. File loading has a 16 MiB demo limit. The default is 16
bytes per row on desktop and 8 in TUI; buttons switch layouts. `--base` accepts
decimal or `0x` hexadecimal addresses. Selection updates the status line.
On TUI, Up/Down, PageUp/PageDown, and Home/End navigate rows; Left/Right pan
horizontally when the line exceeds the available width. Tab reaches the
layout buttons and Quit. Native list/table controls provide their platform
scrollbars and keyboard navigation.

## ArchiveView

Create a control with `XXWIDGETS_ARCHIVEVIEW` and populate it with
`xxwidgets_archiveview_set_entries(widget, entries, count)`. Each
`xxwidgets_archive_entry` contains a nonempty UTF-8 `path`, a `uint64_t`
uncompressed `size` in bytes, and an `is_directory` flag (0 or 1). The library
copies the entries and paths and preserves the input order. It does not open
archives or depend on an archive format library: the application supplies
metadata from its chosen archive reader, for example `xxfclib`.

Rows show a directory marker, an aligned 64-bit decimal byte size for files,
and the path. Desktop backends use a monospace font and native horizontal and
vertical scrolling. TUI Left/Right pan by Unicode scalar without splitting
UTF-8 characters; the longest entry determines the available horizontal range.
ASCII control characters and DEL in paths display as `\xHH`; entry metadata
keeps the original path. Directory sizes are retained in metadata but omitted
from displayed rows.

Population is transactional, selects the first entry (or -1 when empty), and
does not emit input events. Replacing entries refreshes all rows even when the
entry count stays the same. `xxwidgets_archiveview_clear` is equivalent to
passing NULL and count 0. `xxwidgets_archiveview_count` returns the entry count.
`xxwidgets_archiveview_get_entry(widget, index, &entry)` retrieves original
metadata. Returned path pointers belong to the widget until the next successful
replacement, clear, or destruction; copy a path if it must outlive those calls.

`XXWIDGETS_EVENT_SELECT`, `xxwidgets_widget_set_value`, and `get_value` use the
zero-based entry index. `xxwidgets_archiveview_get_selection(widget, &index,
&entry)` returns both the selected index and metadata. Without a selection it
returns `SIZE_MAX` and `{NULL, 0, 0}`. All calls use the same UI-thread and
callback rules as other widgets.

```c
xxwidgets_archive_entry entries[] = {
    {"documents/", 0, 1},
    {"documents/readme.txt", UINT64_C(2048), 0}
};
xxwidgets_archiveview_set_entries(archive_widget, entries, 2);
```

The independent `xxwidgets_archiveview_demo [--native|--tui]` shows sample
entries, including UTF-8 paths, directory entries, files larger than 4 GiB, and
a path containing a control character. Selection updates the status line;
buttons reload the entries, clear them, and quit.

## ArchiveBrowser

Create `XXWIDGETS_ARCHIVEBROWSER` for a file-manager style archive view. Its
native control combines an Up button and read-only archive address with a
report table containing Name, Size, Packed Size, Modified, and Attributes.
The terminal control shows the same address, column header, and metadata.
The original `XXWIDGETS_ARCHIVEVIEW` remains available for flat member lists.

The application supplies metadata with `xxwidgets_archivebrowser_set_entries`;
the widget does not open archives or depend on a format library. Each
`xxwidgets_archive_browser_entry` contains the original UTF-8 member path,
64-bit size and packed size, a directory flag, metadata flags, and optional
modified/attributes strings. Set `XXWIDGETS_ARCHIVE_SIZE_KNOWN` and
`XXWIDGETS_ARCHIVE_PACKED_SIZE_KNOWN` only for sizes reported by the reader;
unknown values appear blank. All strings are copied. Control characters are
escaped for display.

Flat member paths become a browsable hierarchy, including folders inferred
from nested member names. The browser normalizes slash/backslash separators
and `.` components, and rejects `..` components. Replacing members resets the
current directory to the archive root. `xxwidgets_archivebrowser_set_archive`
sets the display path; `set_directory` navigates to an archive-relative folder,
and `up` returns to its parent. These programmatic changes do not emit events.

Double-click a folder or press Enter to enter it. The Up button and Backspace
return to the parent. Clicking a native column heading sorts that column;
TUI keys `1` through `5` select the same columns. Repeating a column reverses
its order. Directories remain before files, and sizes sort numerically.
TUI Left/Right pan the header and rows together.

`SELECT` and `ACTIVATE` event values are visible row indexes. Folder navigation
emits `CHANGE`; file activation emits `ACTIVATE` so the application can decide
how to open or extract it. `xxwidgets_archivebrowser_get_selection` returns
the original member index and metadata; an inferred directory has source
index `SIZE_MAX`. Borrowed strings remain valid until the next successful
browser change. `count` reports supplied archive members; `visible_count`
reports rows in the current directory.

Right-clicking an ArchiveBrowser row selects it before emitting
`XXWIDGETS_EVENT_CONTEXT_MENU`. A request over empty space clears the selection;
its `event.value` is `-1`. Shift+F10 and the Menu key request a menu for the
focused or selected row, with an anchor beside that row. `event.value` is the
visible row index, and `get_selection` is already updated when the callback
runs. The application defines and displays its menu and commands.

For context requests, `event.x` and `event.y` locate the popup anchor relative
to the widget: native backends use pixels in the ArchiveBrowser container,
and TUI uses text cells. Other event types set both coordinates to zero.

```c
xxwidgets_archive_browser_entry entries[] = {
    {"docs/readme.txt", 2048, 812, 0,
        XXWIDGETS_ARCHIVE_SIZE_KNOWN | XXWIDGETS_ARCHIVE_PACKED_SIZE_KNOWN,
        "2026-09-28 10:00", "A"},
    {"LICENSE", 1092, 0, 0, XXWIDGETS_ARCHIVE_SIZE_KNOWN, NULL, NULL}
};
xxwidgets_archivebrowser_set_archive(browser_widget, "example.zip");
xxwidgets_archivebrowser_set_entries(browser_widget, entries, 2);
```

The independent `xxwidgets_archivebrowser_demo [--native|--tui]` demonstrates
folder navigation, sorting, and activation with copied sample metadata.

## TUI keyboard and text

Tab / Shift+Tab move focus. Enter activates a button; Enter / Space toggle a
checkbox. Text fields support typing, left/right, Home/End, Backspace/Delete.
Up/down, PageUp/PageDown, and Home/End select list/HexView/archive rows. Left/Right
pan HexView and archive controls horizontally. ArchiveBrowser Enter opens the
selected folder or activates the file; Backspace goes up, and `1`–`5` sort
columns. Escape requests window close.

Text storage is UTF-8. The initial terminal renderer treats each Unicode
scalar as one cell; wide and combining characters can have imperfect
alignment. On the Windows console, non-BMP characters display a replacement
glyph while the original text remains intact. TUI has keyboard input and
single-line edits; it does not provide terminal mouse input or native IME
behavior. Unhandled process termination can bypass terminal restoration.

## Validation

`xxwidgets_core_tests` exercises argument validation, UTF-8, ownership,
callback reentrancy, suppression of programmatic notifications, and rollback
after backend failures through a deterministic backend. HexView tests cover
copied data, formatting, layouts, final rows, address overflow, and rollback.
Windows native tests
exercise actual WinAPI controls, UTF-8 round trips, user notifications,
selection, close handling, and cleanup. Linux GTK and macOS AppKit require
compilation and runtime validation on their respective platforms. Windows TUI
tests use a private hidden console to exercise rendering, Unicode editing,
navigation, repeated key events, callback setters, and terminal restoration.
Both native and TUI tests exercise HexView and archive control refresh,
scrolling, and selection. ArchiveView core tests cover entry ownership,
invalid metadata, full 64-bit sizes, control-character escaping, transactional
rollback, and clearing. ArchiveBrowser tests cover inferred directory
navigation, file activation with original source indexes, and numeric size
sorting. Windows integration tests also exercise unchanged-count refresh
and Unicode horizontal navigation.
`tests/package` is a separate `find_package` consumer for verifying installed
static and shared packages.

License: MIT, matching the sibling library projects.

## Process dialog

Include `xxwidgets/xxwidgets_process.h` and call
`xxwidgets_process_dialog(owner, title, &progress, update, context)` outside an
input callback. It consumes xxfclib's `xx_pd_struct` without linking format
engines. The owner is disabled while the operation runs. No dialog is created
for a completed job; unfinished work opens the modal form only after more than
1,000 ms. Each of its five bars and count/status labels follows the corresponding
`xx_pd_record.is_busy`. Inactive rows disappear, and active rows pack together.
Counts retain their full 64-bit values. A zero total is displayed as `?` with
zero percent. Elapsed time and a Cancel button appear below the bars.

The short update callback receives `(context, stop_requested, progress,
finished)` on the UI thread, immediately and between event polls with a 30 ms timeout.
It advances bounded work or copies a synchronized snapshot from a worker, and
sets `*finished` when that operation is actually done. Idle records can occur
between stages and do not imply completion. `progress` must be a UI-owned
snapshot, since xxfclib's plain counters and volatile stop flag do not provide
thread synchronization. Start asynchronous work before the call, protect both
snapshot reads and stop delivery, then join the worker after returning.

Cancel, Escape, and closing the window set the sticky stop request. The dialog
keeps pumping events until the callback acknowledges completion. The library
retains `progress.is_stop` even if a copied snapshot cleared it. Job errors stay
in `last_error` / `error_string`; an `XXWIDGETS_OK` return reports successful UI
handling. On a UI/callback error or app quit, the callback receives one final
stop request before returning, so the caller can stop and join unfinished work.

`xxwidgets_process_demo [--native|--tui]` shows a fast job that never opens a
window, a five-second job with changing active levels, large counts, an unknown
total, and cooperative cancellation. Its work advances in short UI-thread
steps. Design notes from the Qt reference are in
[docs/process-dialog-reference.md](docs/process-dialog-reference.md).

## Keyboard shortcuts

Install a list on a window with `xxwidgets_window_set_shortcuts()`:

```c
const xxwidgets_shortcut shortcuts[] = {{"Ctrl+O", 1}, {"Ctrl+Shift+E", 2}, {"F5", 3}};
xxwidgets_status status = xxwidgets_window_set_shortcuts(window, shortcuts, 3);
```

The window receives `XXWIDGETS_EVENT_SHORTCUT` with the action ID in
`event.value`. Bindings apply while focus is in any child of that window,
before normal control input. Hidden/disabled windows and owners blocked by a
modal dialog do not fire. Installation parses input into owned bindings;
callers may release the input strings immediately. An invalid sequence or a
duplicate combination preserves the previous list. An empty sequence disables
that entry; `NULL, 0` clears the list. `xxwidgets_window_shortcut_count()` reports
the number of active bindings.

Sequences are case-insensitive, with Ctrl/Control, Alt/Option, Shift and
Meta/Cmd/Command/Win modifiers separated by `+`. Keys include letters, digits,
F1..F24, Enter, Escape, Tab, Backspace, Delete, Insert, Home, End, PageUp,
PageDown, arrows and Space. Named punctuation includes Comma, Period, Minus,
Plus, Equal, Slash, Backslash, Semicolon, Apostrophe and brackets. Use Cmd for
macOS command shortcuts. Terminal support depends on the key/modifier codes
reported by the terminal; ordinary POSIX terminal input cannot distinguish
Ctrl+letter from Ctrl+Shift+letter.

The optional `xxwidgets::settings` adapter provides
`xxwidgets_settings_install_shortcuts(window, loaded, actions, count)` to map
the named list from `xx_shortcuts_load()` into numeric application actions.
Unknown names are ignored; recognized bindings use the same validation rules.
