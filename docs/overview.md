# Overview

Your app builds its whole UI in code, every frame. The library does the rest:

- It keeps the state between frames:
  - focus
  - scroll positions
  - open menus
  - window rects
- It lays the UI out with flexbox and grid.
- It handles input.
- It draws the UI.

Everything is in the `tachyon` namespace. Examples are written as members of a class
derived from `GUIApplication`, like the one in the
[README](https://github.com/LogicalError/TachyonUI#readme).

Then: [Controls](controls.md), [CSS and theming](css.md), or [Integration](integration.md)
to host the library in an existing application.

| Page | Covers |
| --- | --- |
| [The application and the frame](overview/application.md) | `GUIApplication`, `OnGUI`, the steps of a frame |
| [IDs](overview/ids.md) | Call-site, static and ranged IDs, parent scopes, when an ID must be stable |
| [Begin and End](overview/begin-end.md) | Pairing `Begin*` with `End*`, scope objects, misuse |
| [Return values, content and units](overview/values.md) | Return values, callbacks, `GUIContent`, CSS px versus pixels |
| [Errors and saved state](overview/errors-and-state.md) | `Result<T>`, error codes, saving window and dock layouts |

<!--!
\htmlonly <div class="page-tree"> \endhtmlonly
\subpage md_docs_2overview_2application
\subpage md_docs_2overview_2ids
\subpage md_docs_2overview_2begin-end
\subpage md_docs_2overview_2values
\subpage md_docs_2overview_2errors-and-state
\htmlonly </div> \endhtmlonly
-->
