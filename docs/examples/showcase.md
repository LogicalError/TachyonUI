# Showcase

The showcase is the editor-style application in the README's screenshot, with most of
the library spread across its panels. It's a testbed for the library's features rather
than a tidy example. Read it to see how a larger app fits together, and read the
smaller examples to learn how to use one part.
Source: [examples/showcase](https://github.com/LogicalError/TachyonUI/tree/main/examples/showcase).

## Running it

You can run it from any folder, or just double-click it:

```text
bin\Debug\showcase.exe                  rem Windows
bin/Debug/showcase                      # Linux and macOS
```

Unlike the small examples, it has files of its own in `workdirectory/`: its
`themes/showcase.css`, its icons and the optional Noto fallback fonts. It finds them
through `SetResourceRoot(TACHYON_RESOURCE_DIR)`, which points at the folder the build
defines for them.

It accepts these command-line options:

| Option | What it does |
| --- | --- |
| `--ui-scale <factor>` | Zooms the UI on top of the OS display scale, for example `1.5`. |
| `--theme <palette>` | Starts with a different palette, for example `themes/light.css`. |
| `--panel <title>` | Brings that panel to the front of its tab group. You can repeat it. |
| `--float <title>` | Starts with that panel in its own window. You can repeat it. |
| `--files <folder>` | Sets the folder the Files panel shows instead of the workdirectory. The folder is only read, never written to. |

When you quit, it saves its window and dock layout to `workdirectory/ui_state.json` and
restores them the next time it starts.

## Where to look

| File | What it shows |
| --- | --- |
| `application.h`, `application.cpp` | Setup: style files, icons, commands and shortcuts, the dock layout, toolbars and saved UI state. |
| `panel_hierarchy.cpp` | A scene tree made of tree cells in a virtual table. |
| `panel_inspector.cpp` | An inspector built from fields, menus and context menus. |
| `panel_files.cpp` | A file browser over a copy of the workdirectory (or `--files <folder>`), with a folder tree, a grid of files with the OS's icons, renaming, and drag and drop. Your changes are only kept in memory. |
| `panel_console.cpp` | A log in a virtual table, with level filters. |
| `panel_node_view.cpp` | The node graph editor, with undo. |
| `panel_table.cpp`, `panel_split_scroll.cpp`, `panel_tree_table.cpp` | Tables: sizing, a 50,000-row virtual table, frozen columns and tree rows. |
| `panel_profiler_tree.cpp` | The profiler's frame tree, with custom-drawn bars. |
| `panel_settings.cpp` | The settings panel, including a page for shortcuts. |
| `panel_reorderable.cpp` | Lists, tabs and wrapped groups you can reorder by dragging. |
| `panel_view.cpp` | A viewport with docked toolbars. |
| `panel_misc.cpp`, `panel_box_test.cpp` | Custom drawing, images, icon placement and box layout. |

Its own CSS classes live in `workdirectory/themes/showcase.css`. They load after the
library's `styles.css`, the same way the [Custom theme](custom-theme.md) example does it.

---

[Previous: Host device](host-device.md) | [Examples](../examples.md)
