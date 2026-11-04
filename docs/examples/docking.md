# Docking

Four panels in a dock layout. A panel is a piece of UI with a title, shown as a tab. A
dock layout arranges the panels in the window, and the user can rearrange them:

- drag a tab to move its panel
- drop it on another group of tabs to dock it there
- drag it outside the window to float it in an OS window of its own

Full source: [examples/docking/docking.cpp](https://github.com/LogicalError/TachyonUI/blob/main/examples/docking/docking.cpp).

## Panels

- Create each panel once, in `Initialize`, with:
  - a title, shown on its tab
  - an icon, or `IconID::Invalid` for none
  - a callback that builds its content
- The callback runs only while the panel is visible.
- `CreatePanel` returns the panel's ID, which the dock tree below refers to.

```cpp
// Panels are created once; each draws its content in its callback.
const tachyon::IconID noIcon = tachyon::IconID::Invalid;
const tachyon::ID scene   = CreatePanel(layout.panels, "Scene",   noIcon, [this] { ScenePanel(); });
const tachyon::ID details = CreatePanel(layout.panels, "Details", noIcon, [this] { DetailsPanel(); });
const tachyon::ID log     = CreatePanel(layout.panels, "Log",     noIcon, [this] { Text("Nothing logged yet."); });
const tachyon::ID help    = CreatePanel(layout.panels, "Help",    noIcon, [this] { Text("Drag the tabs around."); });
```

- `layout` is a `tachyon::DockingLayout` member of the app.
- It owns the panels and the dock tree.

## The dock tree

- The dock tree says where each panel goes. It is made of splits and leaves:
  - a split divides its space between two children, side by side or one above the other
  - a leaf holds one or more panels, shown as tabs
- A pinned split keeps one side at a fixed size, in CSS px, and gives the rest to the
  other side. `LeftPinned, 220` keeps the left side 220 px wide.

```cpp
// Left column of 220 px, the rest split over a 150 px bottom row.
using tachyon::DockSplitMode;
layout.root = tachyon::MakeSplit(DockSplitMode::LeftPinned, 220,
    tachyon::MakeLeaf({ scene }),
    tachyon::MakeSplit(DockSplitMode::BottomPinned, 150,
        tachyon::MakeLeaf({ details, help }),
        tachyon::MakeLeaf({ log })));
```

## Building it each frame

- The docking container, `BeginDockingContainer`, builds the panels that are visible in
  the window.
- `EmitFloatingDockWindows` then builds the panels the user dragged out into their own
  windows.

```cpp
void OnGUI() override
{
    if (BeginWindow(windowID, "Docking", theme.mainWindowStyleRef, tachyon::LayoutOverride::FixedSize(900, 600).Centered()))
    {
        BeginDockingContainer(dockID, &layout, tachyon::LayoutOverride::Auto());
        EndDockingContainer();
        // Panels dragged out of the window get their own OS windows, owned by this one.
        EmitFloatingDockWindows();
    }
    EndWindow();
}
```

## Panel content

A panel callback is ordinary UI code, the same as in `OnGUI`:

```cpp
void DetailsPanel()
{
    Text("Selected object " + std::to_string(selected));
    BeginLabelGroup();
    DragLabel("Scale");
    Field(scale);
    EndLabelGroup();
}
```

## Keeping the arrangement

- To keep the user's arrangement across restarts:
  - call `PersistDocking("dock", layout)` once, after the panels and the dock tree exist
  - when the app quits, save the text `SaveUIState` returns, for example to a file
  - see [Errors and saved state](../overview/errors-and-state.md)

```cpp
// In Initialize, after layout.root is set
PersistDocking("dock", layout);
```

```cpp
// After Run returns, before Cleanup
if (std::ofstream file("ui_state.json", std::ios::binary); file)
    file << app.SaveUIState();
```

- Full docking reference: [Tabs, docking and toolbars](../controls/tabs-docking-toolbars.md).

---

[Previous: Controls](controls.md) | [Examples](../examples.md) | [Next: Custom theme](custom-theme.md)
