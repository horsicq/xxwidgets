# xxwidgets

C11 widgets for native desktop and terminal applications, with a shared API.
Includes basic controls, tables, trees, hex and disassembly views, binary
inspectors, scan panels, and optional debugger and emulator adapters.

- Windows: WinAPI controls.
- Linux: GTK 3.16+ controls.
- macOS: AppKit controls through a private Objective-C backend.
- Terminal UI: Windows console or POSIX terminals.

## Build

See [build instructions](docs/BUILD.md).
Applications can link `xxwidgets::xxwidgets` after adding this project with CMake.

See the [demo catalog](examples/README.md) and
[public headers](include/xxwidgets) for usage and optional integrations.

License: [MIT](LICENSE).
