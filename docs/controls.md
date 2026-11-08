# Controls

Every control is a member function of `tachyon::GUIApplication`. Your application is a
subclass of it and builds its whole UI again each frame, in `OnGUI()`. The examples on
these pages are written as if inside that `OnGUI()`, with `using namespace tachyon;`.

```cpp
class MyApp : public tachyon::GUIApplication
{
public:
    void OnGUI() override
    {
        using namespace tachyon;
        if (BeginWindow(windowID, "Tool"))
            Button("Save", [this] { Save(); });   // a control, called from OnGUI
        EndWindow();
    }

    // ...
};
```

Read [Common conventions](controls/conventions.md) first: it explains the parameters and
results all controls share.

| Page | Covers |
| --- | --- |
| [Common conventions](controls/conventions.md) | Parameter order, return values, callbacks, `LayoutOverride`, state scopes |
| [Windows and layout](controls/windows-and-layout.md) | Windows, horizontal and vertical groups, areas, scroll bars, caching, label and button groups |
| [Text and images](controls/text-and-images.md) | `Text`, `Label`, `DragLabel`, `SelectableText`, `Image` |
| [Buttons and toggles](controls/buttons-and-toggles.md) | `Button`, `CommandButton`, `Toggle`, `Switch`, `Checkbox` |
| [Fields](controls/fields.md) | Text, search, numeric, vector and typed fields, dropdowns, lists, dictionaries |
| [Sliders](controls/sliders.md) | `SliderField`, `Slider` |
| [Color and gradient fields](controls/color-and-gradient.md) | `ColorField`, `GradientField` and their pickers |
| [Menus](controls/menus.md) | Menus, menu items, menu bar, title bar, context menus |
| [Tooltips and message dialogs](controls/tooltips-and-dialogs.md) | Tooltips, modal `MessageDialog` |
| [Lists and reorderable groups](controls/lists.md) | List views, drag-to-reorder groups |
| [Tables](controls/tables.md) | Tables, virtual tables, tree cells |
| [Tree and grid views](controls/trees-and-grids.md) | `VirtualTreeView`, grid views, virtual grid views |
| [Tabs, docking and toolbars](controls/tabs-docking-toolbars.md) | Tab bars, docking containers, toolbars, toolbar dock |
| [Node view](controls/node-view.md) | `RenderNodeView` node graph editor |
| [Other controls](controls/other-controls.md) | Breadcrumb, status bar, progress bars, spinners and skeletons, task progress and background tasks, command search, settings panel |
| [Custom drawing](controls/custom-drawing.md) | Drawing shapes and text inside an element |
| [Default CSS classes](controls/css-classes.md) | Each control's class when you pass no style |

<!--!
\htmlonly <div class="page-tree"> \endhtmlonly
\subpage md_docs_2controls_2conventions
\subpage md_docs_2controls_2windows-and-layout
\subpage md_docs_2controls_2text-and-images
\subpage md_docs_2controls_2buttons-and-toggles
\subpage md_docs_2controls_2fields
\subpage md_docs_2controls_2sliders
\subpage md_docs_2controls_2color-and-gradient
\subpage md_docs_2controls_2menus
\subpage md_docs_2controls_2tooltips-and-dialogs
\subpage md_docs_2controls_2lists
\subpage md_docs_2controls_2tables
\subpage md_docs_2controls_2trees-and-grids
\subpage md_docs_2controls_2tabs-docking-toolbars
\subpage md_docs_2controls_2node-view
\subpage md_docs_2controls_2other-controls
\subpage md_docs_2controls_2custom-drawing
\subpage md_docs_2controls_2css-classes
\htmlonly </div> \endhtmlonly
-->
