# Syntax

## Rules and selectors

A rule is a selector followed by declarations in braces. The selector names the style the
rule is for; each declaration sets one property.

```css
.button { padding: 2px 8px; }            /* a class */
.button, .toggle { cursor: pointer; }    /* a selector list shares one block */
* { font-family: "Inter"; }              /* the base of every style */
```

- A selector is a class name: the name of one style.
  - The leading `.` is optional.
  - Class names are case-sensitive.
  - Property names and keywords aren't.
- `*` is the base style.
  - It applies to every class defined so far.
  - Every class defined after it starts from it.
  - Class rules override it, so put it first.
  - A library style whose class no file defines gets the base.
- A class can appear in any number of rules.
  - Later declarations win.
  - There is no specificity, the browser rule that ranks one selector above another. File
    order is the only priority.
  - The exception is [child position](#child-position) rules, which always win.

  For example, `.label-help` gets its padding and color from the first rule below and its
  underline from the second (from `themes/styles.css`, shortened):

  ```css
  .label, .label-help {
      padding: 0px 8px;
      color: var(--text-2);
  }

  .label-help, .group-label-help, .drag-label-help, .group-drag-label-help {
      text-underline-thickness: 1px;
      text-underline-style: dotted;
  }
  ```

- There are no combinators: selectors that match an element by where it sits inside
  another.
  - `.a .b`, `.a > .b` and the like are errors.

## Pseudo-classes and states

A control can be in several states at once, such as hovered and focused. A pseudo-class
after the class name, such as `:hover`, makes a rule apply only in that state.

Each style has a slot per element state: one set of values for each combination of states.
A rule writes to every slot whose state bits include the selector's bits:

| Selector | State |
| --- | --- |
| (none) | Every state, including disabled |
| `:hover` | Under the pointer |
| `:active` | Being pressed |
| `:focus` | Has keyboard focus |
| `:selected` or `::highlight(selected)` | Selected or checked (a selected row, a toggle that's on) |
| `::selection` | Selected text in the element |
| `:disabled` | Disabled |
| `:not(:hover)`, `:not(:active)`, `:not(:focus)`, `:not(:selected)` | Slots without that bit |
| `:focus-visible` | Focus ring, shown when focus came from the keyboard |
| `:normal` | Same as no pseudo-class |

- `::selection`:
  - Its `background-color` is the highlight behind the selected text.
  - It takes two colons: `:selection` is an error.
- `:disabled`:
  - It is exclusive: it can't combine with other states.
  - Disabled controls are also faded (see [Opacity](properties.md#opacity)).
  - A `:disabled` rule changes only what it declares; the rest comes from the base rule.

  From `themes/styles.css`:

  ```css
  .menu-item-row:disabled { background-color: transparent; opacity: 0.45; }
  ```

- `:focus-visible`:
  - The focus ring is an outline drawn around the control that has keyboard focus.
  - It takes only `box-shadow` (see
    [Box shadow and focus ring](properties.md#box-shadow-and-focus-ring)).

States combine:

- `.a:hover` also applies while hovered and pressed.
- `.a:focus::selection` applies to selected text in a focused element.
- Write the general rule first and the specific ones after:

```css
.text-style        { background-color: var(--bg-control); border-color: var(--line-strong); }
.text-style:hover  { background-color: var(--bg-active); }
.text-style:active { border-color: var(--accent); background-color: var(--bg-pressed); }
.text-style:focus  { border-color: var(--accent); background-color: var(--bg-control); }
.text-style::highlight(selected) { color: var(--text-1); background-color: var(--accent-soft); }

.textbox::selection { color: var(--text-1); background-color: var(--selection); }
.textbox:not(:focus)::selection { background-color: var(--selection-inactive); }
```

- Only "state" properties differ per slot.
  - The [property tables](properties.md) mark them "state".
  - A layout property such as `padding` or `border-style` in a `:hover` rule changes the
    style for every state.

### Changing state

When a control changes state, its look moves from the old slot to the new one:

- The box (background, border and shadow) blends from the old slot to the new one.
- Text and icons switch at once.
- Each part of the state moves on its own: the hover, the press, the focus and the selected
  bit each have their own timing (below).
  - When two move at once, the box mixes the slots between them by how far each is.
  - For example, tabbing to a button the mouse just left: the hover fades out over 140 ms
    while the focus comes in over 100 ms, so for a moment the box shows a little of
    `:hover`, a little of `:focus` and some of the plain slot.
  - At most four slots are mixed; when more parts move at once, the four with the most
    share are shown.

Easing is the curve a blend follows over its time:

- Ease-out is `cubic-bezier(0.2, 0, 0, 1)`.
- Ease-in-out is `cubic-bezier(0.4, 0, 0.2, 1)`.

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

- How two colors blend depends on the colors:
  - Colors whose hues differ by more than 20 degrees (both ends non-grey) blend in Oklab,
    which keeps lightness and saturation.
  - Greys and shades of one hue blend in sRGB.
  - A transparent end adds only its alpha.
- A change that reverses mid-blend continues from what's shown, with the new timing. For
  example, a hover that leaves halfway in fades out from halfway, and is done sooner.
- A part blends its on/off flip only with `ElementFlags::AnimatesSelected`.
  - Other selection changes are instant.

Popups and palette switches:

| Change | Time | Easing |
| --- | --- | --- |
| A menu or popup opening | 120 ms, fading in and sliding 4px away from what it opens from | ease-out |
| A tooltip appearing | 100 ms, fading in | ease-out |
| Either closing | 80 ms, fading out | ease-out |
| Another palette (`RequestReloadTheme`) or accent (`SetAccent`) | 200 ms, with the old look fading out over the new one | ease-in-out |

With reduced motion every change is instant:

- It follows the OS setting (Windows: "Animation effects").
- `GUIApplication::SetReducedMotion` overrides it.

  ```cpp
  Button("Reduce motion", [this] { SetReducedMotion(true); });
  Button("Follow the OS", [this] { SetReducedMotion(std::nullopt); });
  ```

## Child position

`:first-child`, `:last-child` and `:only-child` style an element by its place among its
parent's children. For example, the segments of a button group can round only the outer
corners of the group:

```css
.segment             { border-radius: 0; }
.segment:first-child { border-radius: 4px 0 0 4px; }
.segment:last-child  { border-radius: 0 4px 4px 0; }
.segment:only-child  { border-radius: 4px; }
```

- An only child takes the `:first-child` and `:last-child` rules, then `:only-child`.
  - `:first-child:last-child` equals `:only-child`.
- They apply on top of all the class's other rules and `*`, wherever those are.
- They take box properties only:
  - backgrounds
  - borders (`border-*`, `-tachyon-border-dash-*`)
  - `box-shadow`
  - `margin`, `padding`
  - `gap`, `row-gap`, `column-gap`
  - `cursor`
- Anything else is an error, because size and text are computed before the last child is
  known.
- They combine with states, as in `.a:first-child:hover`.
  - Not with `:focus-visible`.
- A class is required: `*:first-child` is an error.
- Every child counts, including hidden ones.

## Comments

- `/* block */`
- `// to end of line`

## \@font-face

An `@font-face` rule adds a font file to a font family. A `font-family` property then picks
the family by name.

```css
@font-face { font-family: "Inter"; src: url("../fonts/Inter/Inter-Regular.ttf"); }
@font-face { font-family: "Inter"; src: url("../fonts/Inter/Inter-Bold.ttf"); }
```

- `font-family` is the family name, quoted or as plain words.
- `src` is the file:
  - `url("path")` or `"path"`
  - an optional `format(...)` after it is ignored
  - one source per rule
- Faces with the same family name form one family.
  - Weight and style come from the files.
- Relative paths are relative to the palette file's folder (`themes/`), in any style file.
- Use it with `font-family: "Inter";`.

## Custom properties

A custom property is a value with a name that starts with `--`. Define it once and use it
in many rules:

- Define them in `:root` blocks: any number, in any file.
- Use them with `var(--name)`.

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

What a variable can hold, and where you can use it:

| Value | Usable in |
| --- | --- |
| Color or gradient (any [color form](values.md#colors), or `var()` of an earlier color variable) | Color properties, gradient stops, `box-shadow` colors |
| One number (`px`/`em` suffix ignored) | Any property taking `var()` and one number |
| 2 to 4 numbers | `margin`, `padding`, `border-width`, `border-radius` (one value per slot) |
| A box-shadow list, or `none` | `box-shadow` |

- Numeric variables are whole numbers from 0 to 65535.
  - Fractions are cut off.
  - Negatives become 0.
- An undefined variable is an error.
- A variable of the wrong kind is an error.

From `themes/styles.css`, where `--border` and `--radius-md` hold one number each:

```css
.text-style {
    border-width: var(--border);
    border-radius: var(--radius-md);
}
```

### !important

A variable set with `!important` keeps its value when a later `:root` sets it again without
it:

- A later `!important` value does replace it.
- A rule that used the variable before either was set keeps what it had.

```css
:root { --radius-md: 6px !important; }   /* first */
:root { --radius-md: 4px; }              /* later, in styles.css: left as 6px */
```

The app's own changes to a theme (`SetThemeVariable`, see
[Changing variables from code](loading.md#changing-variables-from-code)) are put in front
of the palette this way, so they win over every file.

## \@property

An `@property` rule says what a custom property is, for a theme editor: what kind of value
it holds, a name to show, what it's for, and the group it belongs in. The library's are at
the top of `themes/styles.css`.

```css
@property --radius-md {
    syntax: "<length>";
    category: "Sizes";
    label: "Control corners";
    description: "Corner radius of controls";
    range: 0px 16px;
}
```

| Descriptor | What it says |
| --- | --- |
| `syntax` | The kind of value, from the table below |
| `category` | The group a theme editor shows it in, such as "Surfaces" |
| `label` | The name a theme editor shows for it, such as "Panel" |
| `description` | What it's for, a short sentence |
| `range` | The smallest and largest number it may be: `range: 0 1;` |

| `syntax` | Holds | A theme editor shows |
| --- | --- | --- |
| `"<color>"` | A color or a gradient | A color field |
| `"<number>"` | One number | A number field |
| `"<length>"` | One length, in pixels | A number field |
| `"<edges>"` | One to four lengths, a side each, as `border-width` takes them | `EdgesField` |
| `"<corners>"` | One to four lengths, a corner each, as `border-radius` takes them | `CornersField` |
| `"<length>+"` | One to four lengths | `EdgesField` |
| `"<shadow>"` | A box-shadow, or `none` | `ShadowField` |
| `"*"` | Anything | Its text |

- Each variable's value is checked against its rule once the whole theme is read, so the
  rule and the value can be in any file and either order.
  - A value of another kind is an error, for example a length for a `"<color>"`.
  - A number outside the `range` is an error. With several lengths, each is checked.
- `inherits` and `initial-value`, which browsers' `@property` has, are accepted and passed
  over: the palette sets every value.
- A second rule for the same name replaces the first, in the first's place.
- In C++, `StyleSheet::propertyInfos` lists them in order, as `CustomPropertyInfo`s.
  `GUITheme::GetVariables()` has the loaded theme's (see
  [A theme editor](loading.md#a-theme-editor)).

---

[Previous: How a theme loads](loading.md) | [CSS and theming](../css.md) | [Next: Values](values.md)
