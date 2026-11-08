# CSS and theming

A theme decides how every control looks: its colors, borders, spacing and fonts. In
TachyonUI a theme is a set of CSS files:

- They use a subset of the CSS that web browsers read.
- They add properties of their own, whose names start with `-tachyon-`.
- Each class in them becomes a `tachyon::Style`, the object a control is drawn with.

A class is a named rule, such as `.link` below. A control asks for its style by that name.
From `themes/styles.css`:

```css
.link       { color: var(--text-link); cursor: pointer; }
.link:hover { color: var(--text-link-hover); }
```

| Page | Covers |
| --- | --- |
| [How a theme loads](css/loading.md) | Theme files, load order, palettes, reloads, the accent color |
| [Syntax](css/syntax.md) | Rules, selectors, states, comments, `@font-face`, custom properties |
| [Values](css/values.md) | Numbers, units, colors, gradients |
| [Properties](css/properties.md) | Every supported property, and what isn't supported |
| [Using styles from C++](css/from-cpp.md) | Theme classes, styles built in code, UI scale |

<!--!
\htmlonly <div class="page-tree"> \endhtmlonly
\subpage md_docs_2css_2loading
\subpage md_docs_2css_2syntax
\subpage md_docs_2css_2values
\subpage md_docs_2css_2properties
\subpage md_docs_2css_2from-cpp
\htmlonly </div> \endhtmlonly
-->
