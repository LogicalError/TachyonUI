# CSS and theming

You style TachyonUI with CSS files. The parser reads a subset of CSS, plus a few
`-tachyon-*` properties of its own, and turns each class into a `tachyon::Style`. The pages
below spell out exactly what it accepts. For layout and the controls themselves, see the
[Overview](overview.md) and [Controls](controls.md).

| Page | What it covers |
| --- | --- |
| [How a theme loads](css/loading.md) | Which files load and in what order, and how palettes and reloads work. |
| [Syntax](css/syntax.md) | Rules, selectors, pseudo-classes and states, comments, `@font-face` and custom properties. |
| [Values](css/values.md) | Numbers and units, colors and gradients. |
| [Properties](css/properties.md) | Every supported property (box model, borders, colors, text, flex, grid, sizing, icons) and what isn't supported. |
| [Using styles from C++](css/from-cpp.md) | Theme classes, styles built in code, and the UI scale. |

<!--!
\htmlonly <div class="page-tree"> \endhtmlonly
\subpage md_docs_2css_2loading
\subpage md_docs_2css_2syntax
\subpage md_docs_2css_2values
\subpage md_docs_2css_2properties
\subpage md_docs_2css_2from-cpp
\htmlonly </div> \endhtmlonly
-->
