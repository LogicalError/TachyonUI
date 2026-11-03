# Docking

This example shows four panels in a dock layout. You can drag a tab to move its panel,
drop it on another leaf to dock it there, or drag it outside the window to float it in
its own OS window.
Full source: [examples/docking/docking.cpp](https://github.com/LogicalError/TachyonUI/blob/main/examples/docking/docking.cpp).

## Panels

You create each panel once, in `Initialize`, with a title and a callback that draws its
content. The callback only runs while the panel is visible.

```cpp
// Panels are created once; each draws its content in its callback.
const tachyon::IconID noIcon = tachyon::IconID::Invalid;
const tachyon::ID scene   = CreatePanel(layout.panels, "Scene",   noIcon, [this] { ScenePanel(); });
const tachyon::ID details = CreatePanel(layout.panels, "Details", noIcon, [this] { DetailsPanel(); });
const tachyon::ID log     = CreatePanel(layout.panels, "Log",     noIcon, [this] { Text("Nothing logged yet."); });
const tachyon::ID help    = CreatePanel(layout.panels, "Help",    noIcon, [this] { Text("Drag the tabs around."); });
```

`layout` is a `tachyon::DockingLayout` member of the app. It owns the panels and the
dock tree.

## The dock tree

The dock tree is made of splits and leaves. A leaf holds tabs. A pinned split keeps one
side at a fixed size in CSS px and gives the rest of the space to the other side.

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

The docking container builds the panels that are visible. After it,
`EmitFloatingDockWindows` builds the panels the user has dragged out into their own
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

A panel's callback is ordinary UI code that runs inside the panel:

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

If you want the arrangement to survive a restart, give the layout a name once with
`PersistDocking("dock", layout)` and save the text that `SaveUIState` returns (see
[Errors and saved state](../overview/errors-and-state.md)). Docking is covered in full
on the [Tabs, docking and toolbars](../controls/tabs-docking-toolbars.md) page.

---

[Previous: Controls](controls.md) | [Examples](../examples.md) | [Next: Custom theme](custom-theme.md)
