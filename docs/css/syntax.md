# Syntax

## Rules and selectors

```css
.button { padding: 2px 8px; }            /* a class */
.button, .toggle { cursor: pointer; }    /* a selector list shares one block */
* { font-family: "Inter"; }              /* the base of every style */
```

- A selector is a class name. The leading `.` is optional, so `button { }` is the class
  `button`. Class names are case-sensitive, but property names and keywords aren't.
- `*` sets the base. It applies to every class defined so far, and every class defined
  after it starts from it. Class rules override it, so put it before the rules it should be
  the base of. A library style whose class isn't defined in any file gets the base.
- A class can appear in as many rules as you like, and later declarations overwrite earlier
  ones. There's no specificity: file order is the only priority. The one exception is the
  [child position](#child-position) rules, which always win over the class's other rules.
- There are no combinators. `.a .b`, `.a > .b` and the like are reported as errors.

## Pseudo-classes and states

Each style has a slot for every element state. A rule writes to every slot whose state bits
include the selector's bits:

| Selector | State |
| --- | --- |
| (none) | Every state, including disabled |
| `:hover` | Under the pointer |
| `:active` | Being pressed |
| `:focus` | Has keyboard focus |
| `:selected` or `::highlight(selected)` | Selected or checked (a selected row, a toggle that's on) |
| `::selection` | Selected text in the element. Its `background-color` is the highlight. |
| `:disabled` | Disabled. This one is exclusive and can't be combined with other states. A disabled control is also drawn faded (see `opacity`). |
| `:not(:hover)`, `:not(:active)`, `:not(:focus)`, `:not(:selected)` | The slots without that bit |
| `:focus-visible` | The focus ring, shown when focus came from the keyboard. It takes only `box-shadow` (see below). |
| `:normal` | Same as no pseudo-class |

States combine. `.a:hover` also applies while the element is hovered and pressed, and
`.a:focus::selection` applies to selected text in a focused element. Since a rule covers
every slot that has its bits, write the general rule first and the specific ones after it:

```css
.text-style        { background-color: var(--bg-control); border-color: var(--line-strong); }
.text-style:hover  { background-color: var(--bg-active); }
.text-style:active { border-color: var(--accent); background-color: var(--bg-pressed); }
.text-style:focus  { border-color: var(--accent); background-color: var(--bg-control); }
.text-style::highlight(selected) { color: var(--text-1); background-color: var(--accent-soft); }

.textbox::selection { color: var(--text-1); background-color: var(--selection); }
.textbox:not(:focus)::selection { background-color: var(--selection-inactive); }
```

A `:disabled` rule only changes what it declares. Everything else comes from the class's
base rule. Note that `::selection` needs two colons; `:selection` is an error.

Only per-state properties (marked "state" in the property tables) can differ between slots.
If you write a layout property such as `padding` or `border-style` in a `:hover` rule, it
changes the whole style, for every state.

### Changing state

When an element's state changes, its box (background and border) blends from the old
state's slot to the new one. Text, icons and shadows switch straight away. Each kind of
change has its own timing. Ease-out is `cubic-bezier(0.2, 0, 0, 1)`, ease-in-out is `cubic-bezier(0.4, 0, 0.2, 1)`:

| Change | Time | Easing |
| --- | --- | --- |
| Hover in | 70 ms | ease-out |
| Hover out | 140 ms | ease-out |
| Press | at once | |
| Release | 120 ms | ease-out |
| Focus in or out | 100 ms | ease-out |
| Selected (a row) | at once | |
| A switch, check box or toggle turning on or off | 120 ms | ease-in-out |
| Disabled | at once | |
| Invalid (`BeginInvalid`) | the border at once, the halo 150 ms | ease-out |

Colors of different hues blend in Oklab. That's when both ends have some color and their
hues are more than 20 degrees apart, such as blue to red, or ok to warning to error. Oklab
keeps the lightness and saturation of the ends in the middle, where sRGB would turn dark and
grey. Greys and shades of a single hue blend in sRGB. A transparent end adds no color, only
its alpha.

If a change reverses before the blend has finished (moving out again during a hover in), it
carries on from what's currently shown, with the new change's timing. A control part only
blends its on/off flip when it has `ElementFlags::AnimatesSelected`. Other selection changes
are instant.

Popups and theme switches are animated too:

| Change | Time | Easing |
| --- | --- | --- |
| A menu or popup opening | 120 ms, fading in and sliding 4px away from what it opens from | ease-out |
| A tooltip appearing | 100 ms, fading in | ease-out |
| Either closing | 80 ms, fading out | ease-out |
| Another palette (`RequestReloadTheme`) | 200 ms, with the old look fading out over the new one | ease-in-out |

With reduced motion, every change is instant. It follows the OS setting (on Windows, the
"Animation effects" switch), and `GUIApplication::SetReducedMotion` sets it for your app.

## Child position

`:first-child`, `:last-child` and `:only-child` style an element by where it sits among its
parent's children:

```css
.segment             { border-radius: 0; }
.segment:first-child { border-radius: 4px 0 0 4px; }
.segment:last-child  { border-radius: 0 4px 4px 0; }
.segment:only-child  { border-radius: 4px; }
```

- An only child is both the first and the last child. It takes the `:first-child` and
  `:last-child` rules, then the `:only-child` ones. `:first-child:last-child` means the same
  as `:only-child`.
- These rules apply on top of all the class's other rules, wherever those are in the files,
  and on top of `*`.
- They only accept box properties: backgrounds, borders (`border-*` and
  `-tachyon-border-dash-*`), `box-shadow`, `margin`, `padding`, `gap`, `row-gap`,
  `column-gap` and `cursor`. Any other property is an error, because the element's size and
  text are worked out before its parent's last child is known.
- You can combine them with states (`.a:first-child:hover`), but not with `:focus-visible`.
  They need a class, so `*:first-child` is an error.
- Every child counts, including hidden ones.

## Comments

Both `/* block */` and `// to the end of the line` work as comments.

## \@font-face

```css
@font-face { font-family: "Inter"; src: url("../fonts/Inter/Inter-Regular.ttf"); }
@font-face { font-family: "Inter"; src: url("../fonts/Inter/Inter-Bold.ttf"); }
```

- There are two descriptors. `font-family` is quoted or plain words. `src` is
  `url("path")` or `"path"`, optionally followed by `format(...)`, which is ignored. Each
  rule takes one source.
- Faces with the same family name make up one family. Weight and style come from the font
  files.
- Relative paths are relative to the palette file's folder (`themes/`), whichever style file
  they're in.
- To use the family, write `font-family: "Inter";`.

## Custom properties

You define custom properties in `:root` blocks (as many as you like, in any file) and use
them with `var(--name)`:

```css
:root {
    --accent: #6B9BFF;                          /* a color */
    --bg-band: linear-gradient(90deg, #000, #333); /* a gradient */
    --radius-md: 4px;                           /* one number */
    --radius-md-first: 4px 0px 0px 4px;         /* 2 to 4 numbers, for shorthands */
    --focus-ring: 0 0 0 3px #6B9BFF4D;          /* a box-shadow (numbers then a color) */
    --shadow-control: none;                     /* no box-shadow */
}
```

| Value | Can be used in |
| --- | --- |
| A color or gradient (any color form on the Values page, or `var()` of an earlier color variable) | Color properties, gradient stops, `box-shadow` colors |
| One number (a `px` or `em` suffix is ignored) | Any property that takes `var()` and one number |
| 2 to 4 numbers | `margin`, `padding`, `border-width`, `border-radius` (each value fills one slot) |
| A box-shadow list, or `none` | `box-shadow` |

Numeric variables are stored as whole numbers from 0 to 65535. A fraction is cut off and a
negative value becomes 0. Using an undefined variable, or one of the wrong kind, is an error.

---

[Previous: How a theme loads](loading.md) | [CSS and theming](../css.md) | [Next: Values](values.md)
