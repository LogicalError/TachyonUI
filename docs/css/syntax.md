# Syntax

## Rules and selectors

```css
.button { padding: 2px 8px; }            /* a class */
.button, .toggle { cursor: pointer; }    /* a selector list shares one block */
* { font-family: "Inter"; }              /* the base of every style */
```

- A selector is a class name; the leading `.` is optional. Class names are case-sensitive;
  property names and keywords aren't.
- `*` is the base: it applies to every class defined so far and every class defined after
  starts from it. Class rules override it, so put it first. A library style whose class no
  file defines gets the base.
- A class can appear in any number of rules; later declarations win. No specificity: file
  order is the only priority, except [child position](#child-position) rules, which always
  win.
- No combinators: `.a .b`, `.a > .b` and the like are errors.

## Pseudo-classes and states

Each style has a slot per element state. A rule writes to every slot whose state bits
include the selector's bits:

| Selector | State |
| --- | --- |
| (none) | Every state, including disabled |
| `:hover` | Under the pointer |
| `:active` | Being pressed |
| `:focus` | Has keyboard focus |
| `:selected` or `::highlight(selected)` | Selected or checked (a selected row, a toggle that's on) |
| `::selection` | Selected text in the element; its `background-color` is the highlight. Two colons; `:selection` is an error. |
| `:disabled` | Disabled. Exclusive: can't combine with other states. Disabled controls are also faded (see `opacity`). |
| `:not(:hover)`, `:not(:active)`, `:not(:focus)`, `:not(:selected)` | Slots without that bit |
| `:focus-visible` | Focus ring, shown when focus came from the keyboard. Takes only `box-shadow`. |
| `:normal` | Same as no pseudo-class |

States combine: `.a:hover` also applies while hovered and pressed; `.a:focus::selection`
applies to selected text in a focused element. Write the general rule first, specific ones
after:

```css
.text-style        { background-color: var(--bg-control); border-color: var(--line-strong); }
.text-style:hover  { background-color: var(--bg-active); }
.text-style:active { border-color: var(--accent); background-color: var(--bg-pressed); }
.text-style:focus  { border-color: var(--accent); background-color: var(--bg-control); }
.text-style::highlight(selected) { color: var(--text-1); background-color: var(--accent-soft); }

.textbox::selection { color: var(--text-1); background-color: var(--selection); }
.textbox:not(:focus)::selection { background-color: var(--selection-inactive); }
```

- A `:disabled` rule changes only what it declares; the rest comes from the base rule.
- Only "state" properties (see the property tables) differ per slot. A layout property such
  as `padding` or `border-style` in a `:hover` rule changes the style for every state.

### Changing state

On a state change the box (background and border) blends from the old slot to the new one.
Text, icons and shadows switch at once. Ease-out is `cubic-bezier(0.2, 0, 0, 1)`,
ease-in-out is `cubic-bezier(0.4, 0, 0.2, 1)`:

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

- Colors whose hues differ by more than 20 degrees (both ends non-grey) blend in Oklab,
  which keeps lightness and saturation; greys and shades of one hue blend in sRGB. A transparent
  end adds only its alpha.
- A change that reverses mid-blend continues from what's shown, with the new timing.
- A part blends its on/off flip only with `ElementFlags::AnimatesSelected`; other selection
  changes are instant.

Popups and palette switches:

| Change | Time | Easing |
| --- | --- | --- |
| A menu or popup opening | 120 ms, fading in and sliding 4px away from what it opens from | ease-out |
| A tooltip appearing | 100 ms, fading in | ease-out |
| Either closing | 80 ms, fading out | ease-out |
| Another palette (`RequestReloadTheme`) | 200 ms, with the old look fading out over the new one | ease-in-out |

With reduced motion every change is instant. It follows the OS setting (Windows: "Animation
effects"); `GUIApplication::SetReducedMotion` overrides it.

## Child position

`:first-child`, `:last-child` and `:only-child` style an element by its place among its
parent's children:

```css
.segment             { border-radius: 0; }
.segment:first-child { border-radius: 4px 0 0 4px; }
.segment:last-child  { border-radius: 0 4px 4px 0; }
.segment:only-child  { border-radius: 4px; }
```

- An only child takes the `:first-child` and `:last-child` rules, then `:only-child`.
  `:first-child:last-child` equals `:only-child`.
- They apply on top of all the class's other rules and `*`, wherever those are.
- Box properties only: backgrounds, borders (`border-*`, `-tachyon-border-dash-*`),
  `box-shadow`, `margin`, `padding`, `gap`, `row-gap`, `column-gap`, `cursor`. Anything else
  is an error (size and text are computed before the last child is known).
- Combine with states (`.a:first-child:hover`), not with `:focus-visible`. A class is
  required: `*:first-child` is an error.
- Every child counts, including hidden ones.

## Comments

`/* block */` and `// to end of line`.

## \@font-face

```css
@font-face { font-family: "Inter"; src: url("../fonts/Inter/Inter-Regular.ttf"); }
@font-face { font-family: "Inter"; src: url("../fonts/Inter/Inter-Bold.ttf"); }
```

- `font-family`: quoted or plain words. `src`: `url("path")` or `"path"`, optional
  `format(...)` (ignored). One source per rule.
- Faces with the same family name form one family; weight and style come from the files.
- Relative paths are relative to the palette file's folder (`themes/`), in any style file.
- Use it with `font-family: "Inter";`.

## Custom properties

Define them in `:root` blocks (any number, any file); use them with `var(--name)`:

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

| Value | Usable in |
| --- | --- |
| Color or gradient (any [color form](values.md#colors), or `var()` of an earlier color variable) | Color properties, gradient stops, `box-shadow` colors |
| One number (`px`/`em` suffix ignored) | Any property taking `var()` and one number |
| 2 to 4 numbers | `margin`, `padding`, `border-width`, `border-radius` (one value per slot) |
| A box-shadow list, or `none` | `box-shadow` |

Numeric variables are whole numbers 0 to 65535: fractions are cut off, negatives become 0.
An undefined variable, or one of the wrong kind, is an error.

---

[Previous: How a theme loads](loading.md) | [CSS and theming](../css.md) | [Next: Values](values.md)
