# Overview

TachyonUI is an immediate-mode GUI library. Your application describes its whole UI in
code every frame. The library remembers everything else between frames (focus, scroll
positions, open menus, window rects), lays the UI out with flexbox and grid, handles
input and draws the result.

The pages below cover the ideas you'll use with every control. Once you're comfortable
with them, move on to [Controls](controls.md), [CSS and theming](css.md), or
[Integration](integration.md) if you're hosting the library in an existing application.

Everything lives in the `tachyon` namespace. The examples are written as if they were
inside a class derived from `GUIApplication`, like the one in the
[README](https://github.com/LogicalError/TachyonUI#readme).

| Page | What it covers |
| --- | --- |
| [The application and the frame](overview/application.md) | `GUIApplication`, `OnGUI`, and what happens during one frame. |
| [IDs](overview/ids.md) | Call-site, static and ranged IDs, parent scopes, and when an ID needs to be stable. |
| [Begin and End](overview/begin-end.md) | Pairing `Begin*` with `End*`, scope objects, and what happens when you get it wrong. |
| [Return values, content and units](overview/values.md) | What controls return, callbacks, `GUIContent`, and CSS px versus pixels. |
| [Errors and saved state](overview/errors-and-state.md) | `Result<T>`, error codes, and saving window and dock layouts. |

<!--!
\htmlonly <div class="page-tree"> \endhtmlonly
\subpage md_docs_2overview_2application
\subpage md_docs_2overview_2ids
\subpage md_docs_2overview_2begin-end
\subpage md_docs_2overview_2values
\subpage md_docs_2overview_2errors-and-state
\htmlonly </div> \endhtmlonly
-->
