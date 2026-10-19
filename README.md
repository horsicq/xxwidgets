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
- HexView with copied editable bytes, 64-bit addresses, hexadecimal and ASCII views, resizable columns and header menus, byte-range selection and context menus, a nibble cursor, and read-only mode
- Read-only ArchiveView with UTF-8 paths, directory markers, byte sizes, and entry selection
- ArchiveBrowser with an address bar, folder navigation, icons, and sortable metadata columns
- ScanResults with copied Type/Name/Version/Info metadata, row selection, and report export
- ResourcesViewer, ImportsViewer, ExportsViewer and SymbolsViewer with typed xxfclib records and native/TUI tables
- A two-panel data-structure browser and a nine-column structure-field table
- TreeView with copied hierarchy nodes, expansion, and stable node selection
- A complete ScanPanel with a result tree, file-type combo, Flags/Databases checkbox combos, and Scan/Report buttons
- UTF-8 text, visibility, enablement, focus, application/widget user data
- Click, change, list selection, close, resize, activation, and context-menu request events
- Native file drag-and-drop on Windows, macOS and GTK, including drops over child controls
- Native Open/Save file choosers on Windows, macOS and GTK
- Entropy/byte analysis with region tables, curves, histograms and CSV/SVG export
- A shared demo that runs with either native controls or TUI
- A HexView demo with sample bytes or file loading
- An ArchiveView demo with copied archive-entry metadata
- An ArchiveBrowser demo with explicit and inferred folders and file activation
- ScanResults and complete ScanPanel demos with copied records and report handling
- Dedicated demos for every basic control and for Options, About and persistent settings

This is an initial common control set. Menus as a public API, nested containers,
automatic layout, multiline/rich text, editable table models, drawing surfaces,
and mobile backends are outside the current API.

`xxwidgets_options_dialog(owner, title, options, count, &accepted)` presents
up to 16 application-defined boolean options. It disables the owner while open
and applies values only on OK. Cancel, Escape, or closing the dialog leaves the
supplied values unchanged. Invoke it on the UI thread after returning from an
event callback, for example through a pending menu action in the main loop.
The same form works with the native and terminal backends.

Enable file drops with `xxwidgets_window_set_accept_file_drops(window, 1)`.
The callback receives `XXWIDGETS_EVENT_DROP_FILES` on that window, with
`event->paths` and `event->path_count` containing local UTF-8 paths in source
order. Paths are borrowed for the callback; copy any that need to be retained.
Queue scanning/loading work for the main loop. Multiple paths and directories
are delivered so the application can choose its own handling. No source files
are moved or deleted. Hidden/disabled windows and owners blocked by modal
dialogs reject drops. Pass `0` to disable support. Enabling it on the TUI
backend returns `XXWIDGETS_UNAVAILABLE`; terminal paste remains text input.

Create `XXWIDGETS_SCANRESULTS` to show application scan records. Supply an array
of `xxwidgets_scan_result` to `xxwidgets_scanresults_set_results`; every UTF-8
field is copied, and NULL fields become empty strings. Replacement selects the
first row and emits no input events. `xxwidgets_scanresults_get_result` and
`xxwidgets_scanresults_get_selection` return borrowed metadata until the next
replacement, clear, or destruction. Native backends display resizable
Type/Name/Version/Info columns; TUI displays aligned selectable rows with long
fields shortened for readability. The complete values remain in the model.

`xxwidgets_scanresults_get_report` exports a tab-separated report with a header
and complete field values. Control characters are escaped in both the display
and report, while returned metadata retains its original text. A NULL buffer
and zero capacity query the required size including NUL.
`xxwidgets_scanresults_copy` copies the report to the native clipboard and
returns `XXWIDGETS_UNAVAILABLE` on TUI. The widget has no scan-engine dependency.
`xxwidgets_scanresults_demo --native` or `--tui` demonstrates the control.

Include `<xxwidgets/xxwidgets_data_struct_view.h>` for a reusable two-panel
data-structure browser. The left list shows every supplied structure. Selecting
an item updates the right panel: `XX_DATA_STRUCT_TYPE_STRUCT` uses the dedicated
`XXWIDGETS_DATA_STRUCT_TABLE`, while other types show their ID, offsets,
address, sizes, and count. Pass application events to
`xxwidgets_data_struct_view_handle_event()` from the normal event callback so a
left-list selection updates the detail panel.

The view deep-copies `xxwidgets_data_struct_item` records, names, fields, and
field text. The structure table has Name, Type, Relative offset, File offset,
Size, Role, Raw, Value, and Comment columns on every native and TUI backend.
Raw contains the provider-supplied hexadecimal representation. A zero
field property appears as `data`; `XX_DATA_STRUCT_RECORD_PROPERTY_SIGNATURE`
appears as `signature`; combined and unknown property bits remain visible. Use
`xxwidgets_data_struct_view_control()` when the host needs a child handle for
focus, event comparison, or custom behavior. Destroy the view before its owner.

Create `XXWIDGETS_TREEVIEW` for a general tree. `xxwidgets_treeview_set_nodes`
copies an array of `xxwidgets_tree_node` records: `parent` is `SIZE_MAX` for
a root or the index of an earlier node, `text` is UTF-8, and `expanded` is 0/1.
Selection values and events always identify the original node index. Selecting
a hidden node expands its ancestors; collapsing an ancestor selects it when
the current selection would become hidden. Programmatic changes emit no input
events. Native controls provide tree branches and keyboard expansion. TUI uses
Up/Down to select visible nodes and Left/Right to collapse/expand or move to a
parent/child; Shift+Left/Right pans long text horizontally.
`xxwidgets_treeview_set_colored_nodes` also copies optional light/dark
foreground and background colors for each node (`0xRRGGBB`, or `UINT32_MAX`
for the system color). Native backends select the current appearance; TUI
maps the dark palette to terminal colors. Selection uses the backend's
selection colors.

Include `<xxwidgets/xxwidgets_scan_panel.h>` for the complete reusable scan UI.
`xxwidgets_scan_panel_create(owner, bounds, &panel)` creates a labeled file-type
combo box, Flags and Databases checkbox combo boxes, Scan and Report push
buttons, and a TreeView.
The main database is always used; optional databases and the six scan flags
are exposed as bitmasks. Setters synchronize the controls without input events.
`xxwidgets_scan_panel_set_results` takes the detected format as its root label
and copies each detection into one `Type: Name (Version) [Info]` child row.
Empty version/info fields are omitted. `xxwidgets_scan_result_color` delegates
to `xxfclib/src/scan/xx_scan_color.c`, the C port of XScanEngine's category
rules and light/dark palettes, including protection aliases and malware
background colors. The build compiles that standalone module without linking
the scan engine. Operating systems are olive in light mode, languages teal,
and protection dark red, matching XScanEngine's defaults.
Use `xxwidgets_scan_panel_set_tree` to supply nested scan nodes, such as an
overlay with offset/size and its own detections. Both setters retain a copied
full engine report. The same hierarchy is used by Cocoa, Win32, GTK and TUI.
NULL fields are empty; replacing invalid input preserves the previous results.
The application handles the panel's Scan button event and supplies results from
its engine. Handle Report after returning from polling with
`xxwidgets_scan_panel_show_report`; the text dialog supports Copy all.

The file-type control is a normal `XXWIDGETS_COMBOBOX`, initially containing
selected **Automatic** with a `UINT64` value of zero. Obtain it with
`xxwidgets_scan_panel_control(panel, XXWIDGETS_SCAN_PANEL_FILE_TYPE)` and replace
its copied label/value records with `xxwidgets_combobox_set_records`. The host
handles `XXWIDGETS_EVENT_SELECT` and reads `xxwidgets_combobox_get_current` to
rescan the current file using the chosen engine type. Programmatic selection
changes emit no input events.

`xxwidgets_scan_panel_control` returns borrowed child controls for event
comparison or custom placement. Logical bounds require at least 44 x 11 cells;
the file-type row precedes Flags/Databases and the result tree.
Destroy the panel before its owner/app; do not destroy its children separately.
`xxwidgets_scan_panel_demo --native` or `--tui` demonstrates the complete UI.
Include `<xxwidgets/xxwidgets_scan_options.h>` for an embedded scan-settings
form and `xxwidgets_scan_options_dialog`. Both expose the same six scan flags
and two optional databases as the scan panel. The modal form copies values
back only after OK; Cancel, Escape, and closing preserve the input settings.

Include `<xxwidgets/xxwidgets_font_options.h>` for font preferences for controls,
tables, trees, and text edits. `xxwidgets_font_options_dialog` offers editable
family, point size, styles, previews, and native Choose buttons where supported.
Apply accepted values with `xxwidgets_app_set_font_options`; the library owns
native font resources and applies the choices to current and future controls,
including report text. Font changes preserve explicit control bounds. The TUI
stores preferences while the terminal controls their appearance.

Include `<xxwidgets/xxwidgets_optimization_options.h>` for embedded or modal
optimization settings. General and file buffer sizes use xxwidgets comboboxes
with byte values: Default, then powers of two from 1 KiB through 1 GiB. Custom
existing sizes round-trip without being replaced by a preset. Get/set APIs
expose both sizes and the Use SSE2/Use SSE4.2/Use AVX2/Use AES/Use SHA/
Use VPCLMULQDQ/Use Arm NEON/Use Arm CRC32/Use Arm AES/SHA/Use Arm Dot Product
switches. SHA acceleration applies to SHA-1 and SHA-256, while VPCLMULQDQ
accelerates CRC64 in xxfclib. The host supplies CPU and OS
capabilities. Arm targets hide the six x86 switches and compact the Arm choices
into two rows. x86/x64 targets hide the four Arm switches and show the x86
choices in three rows. On unknown targets, Arm-only capabilities retain the
compact Arm layout; otherwise both families remain visible. Visible unsupported
switches stay unchecked and disabled, while a supported switch can be turned off
and on. The modal Defaults button restores both buffers to 64 KiB and turns on
every supported feature without closing. Only OK commits modal changes; Cancel,
Escape, and closing preserve the caller's settings.
The widget stores preferences; the host applies accepted values to its engine.

Include `<xxwidgets/xxwidgets_context_options.h>` for Explorer context-menu
preferences on Windows. The embedded form and modal dialog query the actual
registration. OK or Apply writes the selected state; Cancel performs no writes.
Current-user registrations use `Software\Classes\<class>\shell\<id>` under
HKCU and quote both the executable and `%1` file argument. All-user scope is
also available and reports access errors to the caller. Other platforms report
`XXWIDGETS_UNAVAILABLE`. Applications must handle the file argument on startup.

Push buttons are also available independently as `XXWIDGETS_BUTTON`, with
caption, visibility, enablement, focus, and `XXWIDGETS_EVENT_CLICK` support on
every backend; `xxwidgets_button_demo` demonstrates them.

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
On Windows, both kinds use a native ComboBox field and dropdown arrow. The
checkbox variant displays a selection summary and opens a checklist; its
checks and record cursor remain independent of the displayed summary.
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
| `XXWIDGETS_BUILD_INSPECTORS` | Top-level builds: `ON` | Build `xxwidgets::inspect` for executable-file loading and the binary viewer demos |
| `XXWIDGETS_XXFCLIB_DIR` | `../xxfclib` | xxfclib headers and optional settings/inspection adapter sources |

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
Options and About. Windows and macOS native demo smoke checks run automatically
through CTest when examples and tests are enabled. The macOS suite exercises
AppKit control creation and actions, Unicode editing, selection,
structure/entropy views, modal OK/Cancel, and native control sizing. Native
macOS tests share a CTest resource lock so parallel runs do not compete for
focus. Explorer context options are Windows-only and are excluded on macOS.

For a macOS run that also includes the optional settings dialog:

```sh
cmake -S . -B build-macos -DXXWIDGETS_NATIVE_BACKEND=cocoa \
  -DXXWIDGETS_BUILD_EXAMPLES=ON -DXXWIDGETS_BUILD_TESTS=ON \
  -DXXWIDGETS_BUILD_SETTINGS=ON -DBUILD_TESTING=ON
cmake --build build-macos --parallel
ctest --test-dir build-macos --output-on-failure
```

Run native tests in a logged-in macOS desktop session. The settings smoke test
uses an in-memory store. For a visual check, run the demos with `--native` and
inspect them with the screen unlocked. Native buttons and dropdowns use
AppKit alignment insets, keeping their bezels and text intact in one-cell rows.

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
origin. Coordinates must be 0–32767, dimensions 1–32767; a window's own
position may also be negative, down to -32767 (a desktop monitor left of or
above the primary one, or a maximized frame). Desktop window placement is
advisory because the window manager can choose its position.
TUI clips windows and children to the screen.

Users can resize desktop windows; `XXWIDGETS_EVENT_RESIZE` reports the new
client size in whole cells through `xxwidgets_widget_get_rect` (GTK rounds
down, so a layout for that size never overhangs the window edge).
A window that re-lays out its controls on RESIZE should pass its layout's
smallest grid to `xxwidgets_window_set_minimum_size(window, columns, rows)`.
Without that call GTK keeps a window at least as large as its controls'
rects; backends with no such limit (WinAPI, AppKit, TUI today) accept and
ignore it. GTK also shrinks a window that would not fit its monitor's work
area, and the following RESIZE reports the size it got. GTK sizes a cell from
the font and from its one-row controls: buttons, entries and combo boxes keep
their theme but drop its minimum height, and a cell is as tall as the tallest
of them plus a 2-pixel gap. Text that cannot fit its cells is ellipsized, with
the whole text as the control's tooltip.

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
with size 0 clears the view. Three independently resizable columns show the
location, elements, and decoded symbols. Desktop headers open a context menu
on a click; drag their separators to resize. The location menu selects base
10/16 and address/file offset. Elements offer hexadecimal groups or signed and
unsigned 8/16/32/64-bit numbers (little endian), plus the row width. Symbols
selects one of the portable xxfclib codepages. ASCII is the default; malformed,
nonprinting, and truncated input characters display as dots. A partial
numeric group displays `?`.

Multibyte symbols appear at the first of their byte positions. Remaining byte
positions show gray `·` continuation markers. Selecting either the symbol or
its markers selects the full encoded byte range: one to four bytes for UTF-8,
two for a UTF-16 code unit (four for a surrogate pair), and four for UTF-32 in
either byte order. A Unicode character crossing a row boundary appears once on
its starting row, with grey continuation markers on the next row; selecting
either fragment selects the whole character. Native backends fit wide glyphs into the first
position; a wide terminal glyph occupies two cells, with markers in any remaining
continuation cells.

`xxwidgets_hexview_set_display`/`get_display` expose those display settings.
`xxwidgets_hexview_set_column_width(widget, column, cells)` sets a column width
in character cells (0 restores automatic sizing); explicit widths survive data
and layout changes. `get_layout` returns the current base and row width. Header
menu actions emit `XXWIDGETS_EVENT_VIEW_CHANGE` with the column index. Setters
remain silent. In TUI, F6 selects the header, F10 opens its menu,
Left/Right switches menus, arrows select an item, Enter applies, and Escape
closes the menu. `+`/`-` resizes the selected column.

Desktop backends use native scrolling controls with monospace fonts; TUI
clips each column separately. The widget
owns its copy: editing it never changes the caller's input buffer or writes a
loaded file. `xxwidgets_hexview_get_data` copies the current bytes out. Pass
NULL with capacity 0 to query the required byte count; a short buffer returns
`XXWIDGETS_BUFFER_TOO_SMALL` without a partial copy.

`xxwidgets_hexview_set_layout(widget, base_address, bytes_per_row)` accepts
8, 16, 24, 32, 48, or 64 bytes per row. Addresses are 64-bit and overflow is rejected.
Changing the layout keeps the row containing the previously selected byte;
setting new data selects the first byte. Both operations suppress input events.
Rows are formatted eagerly and the copied bytes and formatted rows remain
owned by the widget, so memory usage grows with data size. Cursor movement and
editing refresh only the affected rows; replacing data or changing layout
rebuilds the complete view.

HexView starts read-only. `xxwidgets_hexview_set_editable(widget, 1)` enables
hexadecimal editing and `xxwidgets_hexview_get_editable` reports the current
mode. The editable cursor identifies a byte offset and its high (0) or low (1)
nibble. Use `xxwidgets_hexview_set_cursor` and
`xxwidgets_hexview_get_cursor` to change or inspect it. Empty data reports
`SIZE_MAX, 0`. Setting new data places the cursor on the first byte's high
nibble; changing the layout preserves the exact byte and nibble. The active
byte is bracketed as `[HH]` without changing the formatted row width.

In editable mode, `0`-`9`, `A`-`F`, and `a`-`f` replace the active nibble.
Typing a high nibble moves to the low nibble; typing a low nibble moves to the
next byte's high nibble and stops at the final byte. Left/Right move one
nibble, Up/Down move by one row while keeping the byte column, and Home/End
move to the first/high or last/low nibble of the current row. Accepted edits
emit `XXWIDGETS_EVENT_CHANGE` after the copied data has changed. Its
`event.value` is the row containing the edited byte; query the cursor for the
full `size_t` byte offset after the edit. Cursor, mode, layout, and data
setters do not emit input events.

Click or drag in Elements or Symbols to select bytes, including ranges spanning
rows. Shift+click and Shift+arrows extend from the selection anchor. Both columns
highlight the same byte range; grouped numbers and multibyte symbols map back to
the underlying bytes. Clicking a column activates it; F6 cycles the active column.
In Symbols, Left/Right move by decoded symbol, and Shift+Left/Right select whole
symbols including every encoded byte. In Elements, Left/Right retain byte
navigation (or nibble navigation when editing). In Symbols, Up/Down preserve the
preferred byte column in the previous/next row and select the whole symbol at
that position. The preferred column survives snapping to a multibyte symbol or
clamping in a short final row; Shift+Up/Down includes complete endpoint symbols.
PageUp/PageDown move by one visible page of rows at the same preferred byte
column and land on a whole symbol. Shift+PageUp/PageDown includes every encoded
byte of both endpoint symbols. Paging and Up/Down share the preferred column.
Home/End select the complete symbol owning the first/last byte position of the
current row, including a symbol continuing across a row boundary. Shift+Home/End
extend through every byte of that symbol; Ctrl/Meta+Home/End use the buffer
boundaries with complete endpoints. Subsequent vertical movement starts at the
selected symbol's leading visible position. Other navigation or a click starts
a new preferred-column run. In Elements, Up/Down move by row,
Home/End within the row, and Ctrl+Home/End to the buffer boundaries. PageUp/Down
move by a page; Shift extends any of these movements. Alt+Left/Right pans in TUI.

For a file supplied in windows, `xxwidgets_hexview_set_source` attaches borrowed
read and seek callbacks plus the complete file size. The widget's base address
is the window's absolute file offset. In Symbols, Ctrl/Meta+Home/End then reaches
the file endpoints, and Ctrl/Meta+Shift+Home/End preserves an absolute whole-symbol
anchor across window changes. The seek callback supplies the endpoint window,
including the complete endpoint symbol if it begins before a block boundary.
`xxwidgets_hexview_get_source_selection` returns the complete absolute range;
`get_selection` returns its intersection with the current window. Copy reads
the complete selected range; dump streams it through the read callback. Ordinary
data or cursor setters start a new selection outside a source seek callback.
Passing NULL callbacks detaches the source. Callback user data remains caller-owned.
In Symbols, Ctrl+A / Cmd+A selects the entire attached source, with every byte
of its first and last symbols included. Copy and dump use that complete range.

Right-click the body, or press Shift+F10, for the byte context menu. Right-clicking
inside a selection preserves it. The menu provides:

- Dump to file (Ctrl+D, also Cmd+D on macOS), writing exactly the selected bytes.
- Go to beginning/end or selection start/end.
- Select all (Ctrl+A / Cmd+A), current row, or clear.
- Copy hex, decoded text, C array, or offset. Ctrl+C / Cmd+C copies decoded
  text when Symbols is active and hex bytes when Elements is active.

Native export uses the platform save dialog. TUI export asks for an output path;
replacement of existing files requires its Replace checkbox. Clipboard copying
uses the system clipboard on desktops and Windows TUI, and OSC 52 on POSIX
terminals that support it. Text copying uses the selected codepage and substitutes
dots for invalid sequences and control characters.

`xxwidgets_hexview_set_selection(widget, offset, length)` selects the half-open
range `[offset, offset + length)`; zero length clears it.
`xxwidgets_hexview_get_selection` returns the exact range, or `SIZE_MAX, 0` when
empty. Offsets refer to the supplied buffer; add the base address for display.
`xxwidgets_hexview_copy_selection` and `xxwidgets_hexview_save_selection` expose
the copy/export actions to applications. The save API replaces an existing file.
`xxwidgets_hexview_size` returns the copied buffer size.

`XXWIDGETS_EVENT_SELECT` carries the cursor's row in `event.value`; query the range
for exact byte offsets, including changes within a row. Programmatic setters are
silent. Legacy `xxwidgets_widget_set_value` still selects a whole row (-1 clears);
`get_value` reports the cursor's row. Layout and codepage changes preserve the
range, while replacing the data resets it.

`xxwidgets_hex_options.h` provides a reusable embedded options form for a
HexView. `xxwidgets_hex_options_widget_create` creates an 8/16/24/32/48/64-byte row
selector and an Editable checkbox. Read the passive form with
`xxwidgets_hex_options_widget_get`, then apply its values with
`xxwidgets_hexview_set_layout` and `xxwidgets_hexview_set_editable`.
`xxwidgets_hex_options_init` supplies the HexView defaults of 16 bytes per row
and read-only mode. The form owns its child controls and must be destroyed
before its owner window.

The separate `xxwidgets_hexview_demo` accepts:

```text
xxwidgets_hexview_demo [--native|--tui] [--cols 8|16|32] [--base address] [file]
```

Without a file it shows built-in sample data, including nonprintable bytes and
a partial final row. File loading has a 16 MiB demo limit. The default is 16
bytes per row on desktop and 8 in TUI; the options selector switches layouts. `--base` accepts
decimal or `0x` hexadecimal addresses. The demo enables editing explicitly;
the Editable checkbox can return to read-only mode. The status line reports
the selected range, length, or cursor address and nibble. Edits change only the
widget-owned copy; Dump to file exports a selection. In TUI, Alt+Left/Right pans a wide row while plain
arrows move the editable cursor. Tab reaches the layout selector, Editable, and
Quit. Native controls provide their platform scrollbars and keyboard input.

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
Up/down, PageUp/PageDown, and Home/End select list/archive rows. Left/Right pan
archive controls horizontally. HexView provides byte navigation and Shift-selection.
For an editable HexView, hex digits replace nibbles, arrows and Home/End move
the cursor, and Alt+Left/Right pans. ArchiveBrowser Enter opens the selected
folder or activates the file; Backspace goes up, and `1`–`5` sort columns.
Escape requests window close.

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
copied input and output, formatting, layouts, final rows, address overflow,
editable/read-only modes, nibble cursor movement, change events, and rollback.
Windows native tests
exercise actual WinAPI controls, UTF-8 round trips, user notifications,
selection, close handling, and cleanup. Linux GTK and macOS AppKit require
compilation and runtime validation on their respective platforms. Windows TUI
tests use a private hidden console to exercise rendering, Unicode editing,
navigation, repeated key events, callback setters, and terminal restoration.
Both native and TUI tests exercise HexView editing, cursor navigation,
read-only input suppression, refresh, scrolling, and selection. ArchiveView core tests cover entry ownership,
invalid metadata, full 64-bit sizes, control-character escaping, transactional
rollback, and clearing. ArchiveBrowser tests cover inferred directory
navigation, file activation with original source indexes, and numeric size
sorting. Windows integration tests also exercise unchanged-count refresh
and Unicode horizontal navigation.
Data-structure tests cover copied field metadata, role formatting (including
unknown bits), transactional replacement and rollback, two-panel selection,
native nine-column controls, and TUI headers, panning, and navigation.
An install-tree `find_package` consumer verifies the exported target and public
header dependency closure.

License: MIT, matching the sibling library projects.

## SystemProcessesViewer

Include `xxwidgets/xxwidget_systemprocessesviewer.h` and create
`XXWIDGET_SYSTEMPROCESSESVIEWER` with `xxwidgets_widget_create`. It displays Name and
PID columns using native Win32, GTK and AppKit tables or selectable TUI rows.
`xxwidget_systemprocessesviewer_refresh` enumerates the local process list using
Toolhelp32 on Windows, `/proc` on Linux, or libproc on macOS. Applications can
also supply copied snapshots using `xxwidget_systemprocessesviewer_set_processes`.
Replacement preserves selection by PID plus process start identity. Executable
paths can be empty for protected processes; UTF-8 display text escapes control
characters. PIDs retain all 64 bits.

Right-click selects a native row and emits `XXWIDGETS_EVENT_CONTEXT_MENU`;
Shift+F10/Menu and TUI F10 request the same actions. Enter or double-click emits
`ACTIVATE`. Queue those requests in the application event callback and call
`xxwidget_systemprocessesviewer_show_actions` after `xxwidgets_app_poll` returns. The
shared menu offers Open executable location, Copy PID (native clipboard),
Refresh, and Kill process with a separate confirmation. Termination rechecks
the start identity and refuses the current process and PID 0/1. POSIX uses
SIGTERM by default; the standalone kill API also supports SIGKILL. Windows
uses TerminateProcess. OS permissions can deny either operation.

The source is `src/xxwidget_systemprocessesviewer.c`, with OS adapters in
`src/xxwidget_systemprocessesviewer_platform.c`. The independent example is
`examples/systemprocessesviewer_demo.c`:

```sh
cmake --build build --target xxwidget_systemprocessesviewer_demo
build/xxwidget_systemprocessesviewer_demo --native
build/xxwidget_systemprocessesviewer_demo --tui
```

The example refreshes every three seconds, preserves the selected identity,
and supports F5 (refresh), F10 (actions), and window resizing. The separate
`xxwidgets_process_dialog` below is a progress dialog for running operations.

## Binary metadata viewers

ResourcesViewer, ImportsViewer, ExportsViewer and SymbolsViewer display typed
xxfclib metadata in resizable Win32, GTK and AppKit tables and aligned,
selectable TUI rows. Include the corresponding public header and create its
kind with `xxwidgets_widget_create`:

| Public header under `xxwidgets/` | Widget kind | Record type | Columns |
| --- | --- | --- | --- |
| `xxwidget_resourcesviewer.h` | `XXWIDGET_RESOURCESVIEWER` | `xx_resource_record` | Type, Name, Language, Size, Offset |
| `xxwidget_importsviewer.h` | `XXWIDGET_IMPORTSVIEWER` | `xx_import_record` | Library, Name, Ordinal, Address, Kind |
| `xxwidget_exportsviewer.h` | `XXWIDGET_EXPORTSVIEWER` | `xx_export_record` | Name, Ordinal, Address, Forwarder |
| `xxwidget_symbolsviewer.h` | `XXWIDGET_SYMBOLSVIEWER` | `xx_symbol_record` | Name, Value, Size, Type, Binding |

Each family provides `xxwidget_<name>viewer_set_records`, `clear`, `count`,
`get_record` and `get_selection`; for example,
`xxwidget_importsviewer_set_records(imports, records, count)`. The base
`xxwidgets::xxwidgets` target supplies these models. Setters deep-copy records
and their strings, so callers may release their input after the call. Display
text escapes control characters and shortens long fields; getters retain the
complete metadata and numeric values. Successful replacement selects the
first row, or clears selection for an empty table, without emitting input
events. A failed validation, allocation or backend update preserves the
previous records and selection.

Getter strings are borrowed until successful replacement, clear or widget
destruction; do not free them. An empty `get_selection` succeeds with an index
of `SIZE_MAX` and a zeroed record. Record offsets and addresses keep xxfclib's
conventions: offsets are file positions, `-1` means an unavailable offset, and
`UINT64_MAX` means an unavailable address. Native selection emits `SELECT`;
Enter or double-click emits `ACTIVATE`. TUI uses arrows, Home/End and
PageUp/PageDown to select rows, Left/Right to pan, and Enter to activate.
All viewer APIs run on the UI thread.

Enable `XXWIDGETS_BUILD_INSPECTORS` and link `xxwidgets::inspect` to load files
directly through xxfclib. The option defaults to `ON` for a standalone
xxwidgets build and `OFF` when included as a subdirectory. If no
`xxfclib::xxfclib` target exists, the build adds `XXWIDGETS_XXFCLIB_DIR` with
xxfclib's `formats` profile by default. For a subdirectory consumer:

```cmake
set(XXWIDGETS_BUILD_INSPECTORS ON CACHE BOOL "Build executable inspection")
add_subdirectory(path/to/xxwidgets)
target_link_libraries(my_app PRIVATE xxwidgets::inspect)
```

The adapter supplies `xxwidget_<name>viewer_load_file(widget, path)` and the
kind-dispatching `xxwidget_binaryviewer_load_file(widget, path)` declared in
`xxwidget_binaryviewer.h`. These synchronous reads support PE, ELF, Mach-O
and the first architecture slice in a universal Mach-O file. File offsets in
a universal file refer to the containing file, including the slice offset.
Tables absent from a supported file load successfully as empty tables;
unrecognized formats return `XXWIDGETS_UNAVAILABLE`. Read or parse failures
preserve the previous table. Queue file loading after an input callback
returns so parsing does not run inside event dispatch. ELF imports have no
library association in the current records, and show an empty Library field.

With an existing application and window, a file-loading ImportsViewer uses:

```c
#include <xxwidgets/xxwidget_importsviewer.h>

xxwidgets_widget *imports = NULL;
xxwidgets_status status = xxwidgets_widget_create(app, window,
    XXWIDGET_IMPORTSVIEWER, "Imports", (xxwidgets_rect){1, 1, 100, 18}, &imports);
if (status == XXWIDGETS_OK)
    status = xxwidget_importsviewer_load_file(imports, "application.exe");
```

Enable `XXWIDGETS_BUILD_EXAMPLES` as well to build
`xxwidgets_binaryviewer_demo` and `xxwidget_resourcesviewer_demo`,
`xxwidget_importsviewer_demo`, `xxwidget_exportsviewer_demo` and
`xxwidget_symbolsviewer_demo`. Each executable can switch among all four
views; its name selects the initial view. Use a local executable or library:

```sh
cmake --build build --target xxwidgets_binaryviewer_demo xxwidget_importsviewer_demo
build/xxwidgets_binaryviewer_demo --native --file /path/to/program
build/xxwidget_importsviewer_demo --tui --file /path/to/program
```

The demo provides a path field, Load/F5, a native Browse button, four view
buttons, row counts and resizing. Details are in the
[demo catalog](examples/README.md#binary-metadata-viewers).

## Process dialog

Include `xxwidgets/xxwidgets_process.h` and call
`xxwidgets_process_dialog(owner, title, &progress, update, context)` outside an
input callback. It consumes xxfclib's `xx_pd_struct` without linking format
engines. The owner is disabled while the operation runs. No dialog is created
for a completed job; unfinished work opens the modal form only after more than
1,000 ms. Each of its five bars and count/status labels follows the corresponding
`xx_pd_record.is_busy`: a row appears once its level has been busy for 200 ms
and goes once it has been idle as long (the first view shows the busy ones at
once), so a level that flashes by does not resize the dialog. Inactive rows
disappear, and active rows pack together.
Counts retain their full 64-bit values. A zero total is displayed as `?` with
zero percent. Elapsed time and a Cancel button appear below the bars. A label
too long for the dialog keeps its count and the end of its status behind `…`;
`xxwidgets_process_status_tail(status, size, text)` cuts a longer text into a
fixed `xx_pd_record.status` buffer the same way, so the end of a long path (the
file name) is what remains.

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

## Entropy widget

`xxwidgets_entropy` follows the XEntropyWidget layout: file type and range,
block count/reload, total Shannon entropy and packed percentage, a regions
table above a red entropy curve, and a Bytes view with 256 byte counts,
percentages and a blue histogram. It includes a region-name filter, grid,
selected-region shading and click-to-inspect sample values. Native backends
use AppKit, Win32/GDI and GTK/Cairo; TUI draws a colored text graph.

Include `xxwidgets/xxwidgets_entropy.h`, create the composite inside a window,
and pass application events to `xxwidgets_entropy_handle_event`. Bounds must
be at least 88 × 27 text cells. Destroy the composite before its owner window.
The child handles returned by `xxwidgets_entropy_control` remain borrowed.

`xxwidgets_entropy_set_data` copies the bytes, display base, file-type label
and optional named regions. Region offsets are relative to that buffer; a
nonzero display base is added exactly once. Format detection and memory-map
construction belong to the caller's parser. With no region list, the widget
shows Whole file. The map selector switches between supplied regions and the
whole buffer. The demo includes a PNG chunk adapter; other files use Binary.

The default block count is `clamp(size / 512, 1, 100)`, like XEntropyWidget.
Reload accepts 1–1000 blocks. The final block includes remaining bytes; empty
inputs have zero entropy and no samples. The packed threshold is 6.5 bits/byte,
matching XBinary. A high entropy value is a heuristic, not proof of packing.

Save buttons expose ordinary click events. The owner chooses a destination
and calls `xxwidgets_entropy_export_csv` or `xxwidgets_entropy_export_svg`.
CSV contains the current table (including the region filter); SVG preserves
the current graph, grid and selected-region shading. Both APIs can first
query the required buffer size and never write files themselves. The demo
uses the shared native file chooser; terminal users can export with command
line arguments.

Build `xxwidgets_entropy_demo`, then run it with `--native` or `--tui` and
optionally `--file PATH`. It supports file drops, path entry, resizing, Save
and Save diagram. On macOS the executable is in
`xxwidgets_entropy_demo.app/Contents/MacOS/xxwidgets_entropy_demo`.

```sh
xxwidgets_entropy_demo --native --file sample.png
xxwidgets_entropy_demo --tui --file sample.bin
xxwidgets_entropy_demo --native --smoke-test --file sample.png --csv entropy.csv --svg entropy.svg
```
