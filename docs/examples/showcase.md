# Showcase

The editor-style app in the README screenshot, using most of the library. It is a feature
testbed, not a tidy example; use the small examples to learn one part.
Source: [examples/showcase](https://github.com/LogicalError/TachyonUI/tree/main/examples/showcase).

## Running it

Run from any folder, or double-click:

```text
bin\Debug\showcase.exe                  rem Windows
bin/Debug/showcase                      # Linux and macOS
```

Its files live in `workdirectory/` (`themes/showcase.css`, icons, optional Noto fallback
fonts), found through `SetResourceRoot(TACHYON_RESOURCE_DIR)`.

| Option | Effect |
| --- | --- |
| `--ui-scale <factor>` | Zooms the UI on top of the OS display scale, e.g. `1.5`. |
| `--theme <palette>` | Starting palette, e.g. `themes/light.css`. |
| `--panel <title>` | Brings that panel to the front of its tab group. Repeatable. |
| `--float <title>` | Starts that panel in its own window. Repeatable. |
| `--files <folder>` | Folder the Files panel shows instead of the workdirectory. Read only. |

Window and dock layout are saved to `workdirectory/ui_state.json` on quit and restored on
start.

## Where to look

| File | Shows |
| --- | --- |
| `application.h`, `application.cpp` | Setup: style files, icons, commands and shortcuts, dock layout, toolbars, saved UI state. |
| `panel_hierarchy.cpp` | Scene tree of tree cells in a virtual table. |
| `panel_inspector.cpp` | Inspector: fields, menus, context menus. |
| `panel_files.cpp` | File browser over a copy of the workdirectory (or `--files <folder>`): folder tree, file grid with OS icons, renaming, drag and drop. Changes stay in memory. |
| `panel_console.cpp` | Log in a virtual table, with level filters. |
| `panel_node_view.cpp` | Node graph editor with undo. |
| `panel_table.cpp`, `panel_split_scroll.cpp`, `panel_tree_table.cpp` | Tables: sizing, a 50,000-row virtual table, frozen columns, tree rows. |
| `panel_profiler_tree.cpp` | Profiler frame tree with custom-drawn bars. |
| `panel_settings.cpp` | Settings panel, including shortcuts. |
| `panel_reorderable.cpp` | Lists, tabs and wrapped groups reorderable by dragging. |
| `panel_view.cpp` | Viewport with docked toolbars. |
| `panel_misc.cpp`, `panel_box_test.cpp` | Custom drawing, images, icon placement, box layout. |

`workdirectory/themes/showcase.css` loads after `styles.css`, as in
[Custom theme](custom-theme.md).

---

[Previous: Host device](host-device.md) | [Examples](../examples.md)
