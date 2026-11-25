# xxwidgets

C11 widgets for native desktop and terminal applications, with a shared API.
Includes basic controls, tables, trees, hex and disassembly views, binary
inspectors, scan panels, and optional debugger and emulator adapters.
Trees can also load lazily from a callback source, a page at a time
(`xxwidgets_treeview_set_source`), which the snapshot tree-diff viewer
(`xxwidgets/xxwidget_treediffviewer.h`) builds on.
An image view (`xxwidgets/xxwidgets_imageview.h`) turns a canvas into an
image viewer that renders the visible part of an RGBA image at the widget's
size (fit, fill, actual size, zoom and pan, alpha over a checkerboard).
The optional image editor (`xxwidgets/xxwidget_imageeditor.h`,
`XXWIDGETS_BUILD_IMAGE_EDITOR`, needs xxvis) edits an xxfclib `xx_image` with
every xxvis operation (geometry, colour, filters, dither, text, charts) and keeps
an undo / redo history.
The optional binary visualization (`xxwidgets/xxwidget_visualization.h`,
`XXWIDGETS_BUILD_VISUALIZATION`) is the counterpart of XVisualizationWidget: a file
as a grid of blocks shaded by entropy, gradient, zeros or text, coloured by the
format regions and data highlights, with a schema view, click-to-offset and Save.

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
