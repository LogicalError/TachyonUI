# Controls

Every control and container is a member function of `tachyon::GUIApplication`, and you
call it from `OnGUI()` each frame. The pages below group the controls by family. For each
one you'll find its main signatures, what it returns, the callback it takes and the CSS
class it uses by default. The [Overview](overview.md) explains how IDs, Begin / End pairs
and return values work in general, and [CSS and theming](css.md) shows how to style the
classes named here.

The snippets assume they run inside `OnGUI()` of a `GUIApplication` subclass, with
`using namespace tachyon;`. The programs in `examples/` show the same calls in context.

| Page | What it covers |
| --- | --- |
| [Common conventions](controls/conventions.md) | Signatures, return values, callbacks, IDs and styles shared by every control. |
| [Windows and layout](controls/windows-and-layout.md) | Windows, horizontal and vertical groups, areas, scroll bars, caching, label and button groups. |
| [Text and images](controls/text-and-images.md) | `Text`, `Label`, `DragLabel`, `SelectableText` and `Image`. |
| [Buttons and toggles](controls/buttons-and-toggles.md) | `Button`, `CommandButton`, `Toggle`, `Switch` and `Checkbox`. |
| [Fields](controls/fields.md) | Text, search, numeric, vector and typed fields, dropdowns, lists and dictionaries. |
| [Sliders](controls/sliders.md) | `SliderField` and `Slider`. |
| [Color and gradient fields](controls/color-and-gradient.md) | `ColorField` and `GradientField` with their picker popups. |
| [Menus](controls/menus.md) | Menus, menu items, the menu bar and title bar, and context menus. |
| [Tooltips and message dialogs](controls/tooltips-and-dialogs.md) | Tooltips on any control, and modal `MessageDialog` questions. |
| [Lists and reorderable groups](controls/lists.md) | List views and drag-to-reorder groups. |
| [Tables](controls/tables.md) | Tables, virtual tables for large data, and tree cells. |
| [Tree and grid views](controls/trees-and-grids.md) | `VirtualTreeView`, grid views and virtual grid views. |
| [Tabs, docking and toolbars](controls/tabs-docking-toolbars.md) | Tab bars, docking containers, toolbars and the toolbar dock. |
| [Node view](controls/node-view.md) | `RenderNodeView`, a self-contained node graph editor. |
| [Other controls](controls/other-controls.md) | Breadcrumb, status bar, command search and the settings panel. |
| [Custom drawing](controls/custom-drawing.md) | Drawing your own shapes and text inside an element. |
| [Default CSS classes](controls/css-classes.md) | The CSS class each control uses unless you pass your own style. |
| [Writing a control](controls/writing-controls.md) | What every control must do, and what to decide for the optional features. |

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
\subpage md_docs_2controls_2writing-controls
\htmlonly </div> \endhtmlonly
-->
