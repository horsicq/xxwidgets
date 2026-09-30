# xxwidgets demos

Every public widget kind has a working example. All interactive demos accept
`--native`, `--tui`, and `--help`. Without a backend option they use `AUTO`.
Sample data is built in; no archive files or image assets are required.

| Widget or dialog | Executable | What to try |
| --- | --- | --- |
| Window | `xxwidgets_window_demo` | Resize the window; Ctrl+R invokes the action, Ctrl+Q quits |
| Label | `xxwidgets_label_demo` | Change UTF-8 text; toggle visibility and enablement |
| Button | `xxwidgets_button_demo` | Click and count activations; disable the button |
| Edit | `xxwidgets_edit_demo` | Enter text, read it, and reset it |
| Checkbox | `xxwidgets_checkbox_demo` | Check it directly or use the toggle action |
| ListBox | `xxwidgets_listbox_demo` | Select entries, add items, and reload the sample |
| Progress | `xxwidgets_progress_demo` | Advance from 0 to 100 percent and reset |
| Process dialog | `xxwidgets_process_demo` | Compare 200 ms and 5 s jobs; watch five busy-record bars hide, and request cancellation |
| HexView | `xxwidgets_hexview_demo` | Select byte rows; change 8/16/32-byte layouts; load a file |
| ArchiveView | `xxwidgets_archiveview_demo` | Select flat archive records; clear/reload Unicode and large-size samples |
| ArchiveBrowser | `xxwidgets_archivebrowser_demo` | Navigate folders, sort, select multiple rows, and enable Advanced columns |
| Combobox and checkbox combobox | `xxwidgets_combobox_demo` | Choose one or several typed records; inspect their values and reset |
| Options dialog | `xxwidgets_options_demo` | Open a modal form; compare OK with Cancel/Escape |
| About dialog | `xxwidgets_about_demo` | Edit the application name; reopen a dialog with copied text and RGBA image |
| Persistent options | `xxwidgets_settings_demo` | Save through xxfclib, close, and reopen to restore preferences |
| Basic controls together | `xxwidgets_demo` | Edit a name, check an option, select a list item, and update progress |

The seven basic examples share `control_demo.c`; CMake selects the showcased
kind for each executable. The views and dialogs have separate source files.
Modal dialogs are opened from pending actions after input callbacks return.

## Build and run

From the `xxwidgets` directory, in a Visual Studio developer shell on Windows:

```sh
cmake -S . -B build-demos -G Ninja -DCMAKE_BUILD_TYPE=Release -DXXWIDGETS_BUILD_EXAMPLES=ON -DXXWIDGETS_BUILD_SETTINGS=ON
cmake --build build-demos --parallel
ctest --test-dir build-demos --output-on-failure
```

```powershell
.\build-demos\xxwidgets_button_demo.exe --native
.\build-demos\xxwidgets_archivebrowser_demo.exe --native
.\build-demos\xxwidgets_about_demo.exe --native
.\build-demos\xxwidgets_settings_demo.exe --native
.\build-demos\xxwidgets_combobox_demo.exe --tui
```

For a Visual Studio generator, executables are in `build-demos/Release`.
On Unix, omit `.exe`; native builds use GTK or AppKit. A TUI demo requires
interactive input/output. To build just the terminal backend, add
`-DXXWIDGETS_NATIVE_BACKEND=none` and run with `--tui`.

`XXWIDGETS_BUILD_SETTINGS=ON` enables the persistent-options demo and links
only xxfclib's settings subset. Interactive runs use the native per-user
`xxwidgets` / `SettingsDemo` store. On Windows this is
`HKCU\Software\xxwidgets\SettingsDemo`; keys are `Demo/Details` and `Demo/Log`.
The other demos do not write persistent settings.

## Automated checks

All demos support `--smoke-test`. On the Win32 native backend, CTest runs each
demo with `--native --smoke-test`, checks initialization and sample operations,
and closes it automatically. Modal smoke checks accept the Options forms or
close About; the settings check uses an isolated memory store. Dialog smoke
automation is Win32-only. On other native backends, run dialogs interactively.
CTest also checks every demo's `--help` without opening a window.
