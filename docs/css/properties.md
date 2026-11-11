# Properties

Properties come in two kinds:

- "state" properties can differ per state (`:hover`, `:focus`, ...). The tables mark them
  "state".
- The rest are layout properties. They apply to the whole style, in every state.

For example, `color` is a state property, so a label can change color under the pointer.
From `themes/styles.css`:

```css
.label:hover, .label-help:hover  { color: var(--text-1); }
```

## Box model

Every element is a box:

- `padding` is the space between the box's border and its contents.
- `margin` is the space outside the border, between the box and its neighbours.

| Property | Values |
| --- | --- |
| `margin`, `padding` | 1 to 4 lengths, or `var()` |
| `margin-top`, `-right`, `-bottom`, `-left` | length or `var()` |
| `padding-top`, `-right`, `-bottom`, `-left` | length or `var()` |

- With 1 to 4 lengths, the order is top, right, bottom, left, as in CSS.

From `themes/styles.css` (`.axis-label-x`, the axis label beside a vector field):

```css
margin: 1px 0px 1px 1px;
padding: 0px;
```

## Borders and radius

The radius rounds the corners of the box.

| Property | Values | |
| --- | --- | --- |
| `border-width` | 1 to 4 lengths or `var()` | |
| `border-top-width`, `-right-`, `-bottom-`, `-left-width` | length or `var()` | |
| `border-radius` | 1 to 4 lengths or `var()` (corner order below) | |
| `border-top-left-radius`, `border-top-right-radius`, `border-bottom-right-radius`, `border-bottom-left-radius` | length or `var()` | |
| `border-color` | color or gradient, all four edges | state |
| `border-top-color`, `-right-`, `-bottom-`, `-left-color` | color or gradient | state |
| `border-style` | `solid`, `dashed` (6px dashes), `dotted` (2px dashes) | |
| `-tachyon-border-dash-size` | the dash length; 0 = solid | |
| `-tachyon-border-dash-offset` | where the dash pattern starts | |

- `border-radius` takes its values in this order:
  - 1 value: all four corners
  - 4 values: top-left, top-right, bottom-right, bottom-left
  - 2 values: top-left and bottom-right, then top-right and bottom-left
  - 3 values: top-left, then top-right and bottom-left, then bottom-right
- There is no `border` shorthand: set width, color and style separately.

From `themes/styles.css` (`.axis-label-x`):

```css
border-width: var(--border) 0px var(--border) var(--border);
border-radius: var(--radius-md) 0px 0px var(--radius-md);
border-color: var(--line-node);
```

And a dashed border (`.reorder-drop-slot`, where a dragged list row will land):

```css
border-width: 1px;
border-color: var(--accent);
-tachyon-border-dash-size: 3px;
```

## Colors and backgrounds

| Property | Values | |
| --- | --- | --- |
| `background-color` | color or gradient | state |
| `color` | color or gradient, for text | state |
| `-tachyon-icon-color` | color or gradient, for the icon. Unset: the text color. | state |
| `-tachyon-state-layer` | `none` (default), or `content` or `light` with its hovered and pressed strengths | |

`-tachyon-state-layer: content` makes the hovered and pressed backgrounds come from the
text color. With it:

- Hover background = text color at 8% over the non-hover background; pressed = 14%.
- Applies in every state with hover or press (including on/focused), not to selected text
  or disabled.
- Replaces any `background-color` in `:hover` and `:active` rules.
- Gradient or image backgrounds are left alone.
- Two percentages after `content` make it stronger or weaker, hovered then pressed.
  - Over a saturated fill, such as the accent or the danger red, 8% barely shows.
  - Only the hovered one given: pressed stays 14%.
- `light` instead of `content` uses white, whatever the text color: hover brightens.
  - For example, a red button with dark text, which `content` would darken.

```css
.button       { color: var(--text-1); background-color: var(--bg-control); -tachyon-state-layer: content; }
.button:hover { border-color: var(--line-strong); }   /* the background comes from the layer */

.primary      { color: var(--on-accent); background-color: var(--accent); -tachyon-state-layer: content 18% 30%; }
.danger       { color: var(--on-danger); background-color: var(--danger); -tachyon-state-layer: light 18% 30%; }
```

## Opacity

Opacity is how see-through an element is: 1 is solid, 0 is invisible.

| Property | Values | |
| --- | --- | --- |
| `opacity` | number 0 to 1, or `var()` | state |

- It applies to the element and its contents.
  - Nested opacities multiply.
- Each part fades separately.
  - So overlapping parts (text over background) show through a little.
- Disabled controls fade on their own.
  - The topmost disabled element uses `--disabled-opacity` from `:root` (0.4 in the
    default theme).
  - Parts inside it don't fade twice.
  - Override it per class with `opacity` in the class's `:disabled` rule:

```css
:root                   { --disabled-opacity: 0.4; }
.menu-item-row:disabled { opacity: 0.45; }
```

## Box shadow and focus ring

| Property | Values | |
| --- | --- | --- |
| `box-shadow` | `none`, or 1 or 2 layers separated by commas | state |

A layer is one shadow:

- It is `<x> <y> [<blur> [<spread>]]` plus a color.
  - `x` and `y` move the shadow away from the box.
  - The color goes before or after the lengths.
- Or it is a `var()` holding shadows.
- The first layer is on top.

```css
.node                      { box-shadow: var(--shadow-node); }
.node::highlight(selected) { box-shadow: var(--selected-ring), var(--shadow-node); }
```

- At most 2 layers.
- A color is required.
- Blur can't be negative.
- `inset`, a shadow inside the box, works only in `:focus-visible`.

The focus ring is the outline that shows which control has keyboard focus:

- It is the `box-shadow` of the `:focus-visible` rule.
- `:focus-visible` takes exactly one `box-shadow` layer and nothing else.
- It can't combine with other states.
- It's drawn over the state's shadows when focus came from the keyboard.
- An inset ring (an outline inside the box) must be `inset 0 0 0 <width> <color>`.

```css
.button:focus-visible { box-shadow: var(--focus-ring); }              /* 0 0 0 3px #6B9BFF4D */
.menu-item:focus-visible { box-shadow: inset 0 0 0 1px var(--accent); }
```

## Text and fonts

| Property | Values | |
| --- | --- | --- |
| `font-family` | comma-separated family names (quoted or not) and generics (see below) | state |
| `font-size` | number or `var()` | state |
| `font-weight` | `normal`, `thin`, `extra-light`/`ultralight`, `light`, `medium`, `demi-bold`/`semi-bold`/`semibold`, `bold`, `extra-bold`/`ultrabold`, `black`/`heavy`, `extra-black`/`ultra-black`, or a number (see below) | state |
| `font-style` | `normal`, `italic`, `oblique` | state |
| `font-stretch` | `normal`, `ultra-condensed`, `extra-condensed`, `condensed`, `semi-condensed`, `semi-expanded`, `expanded`, `extra-expanded`, `ultra-expanded` | state |
| `line-height` | `normal`; a bare number (times the font size); `<n>em` (the same); `<n>px` | state |
| `letter-spacing`, `word-spacing` | number or `var()` | state |
| `-tachyon-language` | a BCP 47 language tag (`ja`, `"zh-Hans"`), or `none` | state |
| `text-align` | `start`, `end`, `center`, `left`, `right`, `top`, `bottom` | |
| `-tachyon-vertical-text-align` | `start`/`top`, `center`/`middle`, `end`/`bottom`: the vertical position of the text (and icon) block in the box | |
| `vertical-align`, `dominant-baseline`, `-tachyon-text-baseline` | `auto`/`baseline`/`alphabetic`, `ideographic`, `central`, `hanging`, `mathematical`, `middle`, `text-top`, `text-bottom`: which glyph baseline to use, not the block position | |
| `text-wrap` | `nowrap`, `wrap`, `word-char` (wrap at words; break too-long words between characters) | |
| `text-overflow` | `clip`, `ellipsis` | |
| `direction` | `auto`, `initial` (= auto), `ltr`, `rtl` | |

- `font-family`:
  - The generics are `default`, `emoji`, `sans-serif`, `serif`, `monospace` and `math`.
  - The first name is the font.
  - The first generic is used until that font is registered.
  - A quoted generic is a name.
- `font-weight` as a number is rounded up to the next hundred. Above 900 is extra-black.
- `-tachyon-language` is used for shaping and font fallback.
- `text-wrap`: without a fixed height, the element is as tall as its wrapped lines.
- `text-overflow: ellipsis` ends text that doesn't fit with "...".
- `direction` is the reading direction: `ltr` is left to right, `rtl` right to left.

From `themes/styles.css` (`.menu-trigger`, a dropdown's button):

```css
text-align: start;
vertical-align: middle;
-tachyon-vertical-text-align: middle;
text-wrap: nowrap;
text-overflow: ellipsis;
font-size: var(--font-size);
```

## Text decoration

Decorations are lines drawn with the text. There are four lines: `underline`, `overline`,
`throughline`, `bottomline`.

- A line is drawn when its thickness is above 0.
- It's drawn in the text color unless given its own.

In the property names below, `<line>` is one of the four.

| Property | Values | |
| --- | --- | --- |
| `text-<line>-color` | color (not a gradient) | state |
| `text-<line>-thickness` | number or `var()` | state |
| `text-<line>-offset` | number or `var()` | state |
| `text-<line>-style` | `solid`, `double`, `dotted`, `dashed`, `wavy` | state |

Example: `text-underline-color: var(--text-1); text-underline-offset: 2px;`. No
`text-decoration` shorthand.

From `themes/styles.css`, the dotted underline under a label that has help:

```css
text-underline-thickness: 1px;
text-underline-style: dotted;
text-underline-color: var(--text-3);
text-underline-offset: 3px;
```

## Text shadow and glow

| Property | Values | |
| --- | --- | --- |
| `text-shadow` | `none`, or 1 or 2 layers separated by commas | state |

A layer is one shadow:

- It is `<x> <y> [<blur>]` plus a color.
  - The color goes before or after the lengths.
- Or it is a `var()` holding shadows.
- The first layer is on top.
- The shadow has the shape of the text, its decorations and icon (color emoji included),
  in the shadow color.
- No offset gives a glow:

```css
.title   { text-shadow: 0 1px 2px #00000080; }
.warning { text-shadow: 0 0 6px var(--accent); }   /* a glow */
```

- At most 2 layers.
- A color is required.
- Blur can't be negative.
- No spread, no `inset`.
- Blur comes from the glyph distance field: for each glyph, an image of the distance to
  its edge.
  - It reaches about 7px around 14px text (11px+ for text 32px and up).
  - Blur up to two thirds of that (about 4px on 14px text) is exact.
  - Wider blur is narrowed to fit.
- It's clipped to the element's padding box, like its text.
- It changes with state at once; there are no transitions.

## Flex

Every box is a flex container unless it's `display: grid`. A flex container lays its
children (its items) out in a row or a column.

| Property | Values |
| --- | --- |
| `flex-direction` | `row`, `row-reverse`, `column`, `column-reverse` |
| `flex-wrap` | `nowrap`, `wrap`, `wrap-reverse` |
| `justify-content` | `flex-start`/`start`, `flex-end`/`end`, `center`, `space-between`, `space-around`, `space-evenly` (flex and grid) |
| `align-items` | `flex-start`/`start`, `flex-end`/`end`, `center`, `stretch` (flex and grid) |
| `align-content` | `flex-start`/`start`, `flex-end`/`end`, `center`, `stretch`, `space-between`, `space-around`, `space-evenly` (between wrapped lines) |
| `align-self` | `auto`, `flex-start`/`start`, `flex-end`/`end`, `center`, `stretch` |
| `gap` | length or `var()`, between flex items |
| `flex-grow`, `flex-shrink` | whole number or `var()` (fractions cut off) |
| `flex-basis` | `auto`, length or `var()` |
| `flex` | `none` (0 0 auto), `auto` (1 1 auto), or `<grow> [<shrink> [<basis> \| auto]]` |
| `break-after` | `auto`, `always`/`right`, `left`, `avoid` |

- `justify-content` places items along the row or column.
- `align-items` places them across it.
- `flex-grow` is how much of the free space an item takes; `flex-shrink` how much it gives
  up when there isn't enough.
- `flex` with one number: shrink 1, basis 0.
- `break-after`:
  - `always` or `right`: a line break after this item
  - `left`: a line break before it
  - `avoid`: keep it on the line with the next item

A wrapping column (`flex-direction: column; flex-wrap: wrap`) starts a new column when it
runs out of height:

- That is a set `height`, or the content height capped by `max-height`.
- Each column distributes its own free height (`flex-grow`, `flex-shrink`).
- Each column is placed by `justify-content`.

From `themes/styles.css` (`.slider-field-track`):

```css
display: flex;
flex-direction: row;
align-items: center;
justify-content: space-between;   /* the label at the left, the value at the right */
gap: 8px;
```

## Grid

A grid container lays its items out in rows and columns, called tracks.

| Property | Values |
| --- | --- |
| `display: grid` | makes the box a grid container |
| `column-gap`, `row-gap` | length or `var()` |
| `justify-items` | `stretch` (default, items fill the cell), or a `justify-content` value to place items in the cell |
| `grid-column`, `grid-row` | `<start>`, `auto`, `span <n>`, or `<start> / span <n>` |
| `grid-column-start`, `grid-row-start` | number or `auto` |
| `-tachyon-grid-column-track`, `-tachyon-grid-row-track` | `auto`, `<n>px` (or a bare number), `<n>fr` |

- `-tachyon-grid-column-track` and `-tachyon-grid-row-track`:
  - Set them on an item. They size the column or row the item sits in.
  - `fr` is a share of the free space.
  - Items in one track merge: `fr` beats `auto`, `auto` beats `px`, and the same kind
    takes the larger.
- There are no `grid-template-*` properties.

From `themes/styles.css` (`.table-header-section`):

```css
display: grid;
column-gap: 0px;
row-gap: 0px;
```

## Sizing

| Property | Values |
| --- | --- |
| `width`, `height` | `auto` or a length |
| `min-width`, `max-width`, `min-height`, `max-height` | a length |

- These take no `var()` and no percentages.
- Override them per element from code with a `LayoutOverride` (see
  [Controls](../controls.md)).

From `themes/styles.css` (`.menu-trigger`):

```css
min-width: 160px;
height: 24px;
```

And from C++, in `OnGUI()` (`examples/custom_theme/custom_theme.cpp`):

```cpp
auto window = Window(windowID, "Custom theme", tachyon::LayoutOverride{}.Size(400, 220));
```

## Display, overflow and visibility

| Property | Values |
| --- | --- |
| `display` | `flex`, `grid`, `none` (not drawn, no space) |
| `overflow` | `auto`, `hidden`, `visible`, for both axes |
| `overflow-x`, `overflow-y` | the same values, for one axis |
| `visibility` | `visible`, `collapsed` (not drawn, no space) |
| `-tachyon-clamp-scroll` | `true`/`1` (default), `false`/`0` |

- `overflow` says what happens to contents bigger than the box:
  - `auto` clips them and scrolls when needed.
  - `hidden` clips them.
  - `visible` doesn't clip them.
- `-tachyon-clamp-scroll: false` lets content scroll past its edges.

From `themes/styles.css` (`.table-scrollable-body`):

```css
overflow-x: auto;
overflow-y: auto;
```

## Cursor

The mouse pointer's shape while it's over the element.

| Property | Values |
| --- | --- |
| `cursor` | `default`, `text`, `wait`, `crosshair`, `pointer`, `col-resize`, `row-resize`, `ew-resize`, `ns-resize`, `nwse-resize`, `nesw-resize`, `move`, `not-allowed`, `none` |

```css
.link       { color: var(--text-link); cursor: pointer; }
```

## Icons

For controls with an icon next to their text.

| Property | Values | |
| --- | --- | --- |
| `-tachyon-icon-side` | `left` (default), `right`, `top`, `bottom` | |
| `-tachyon-icon-text-gap` | length or `var()`: the space between icon and text | |
| `-tachyon-icon-size` | length or `var()`; 0 = the icon's own size | |
| `-tachyon-icon-offset-y` | length (may be negative) or `var()`: moves the icon up | |
| `-tachyon-icon-color` | color or gradient (see above) | state |

```css
.icon-label {
    -tachyon-icon-side: left;
    -tachyon-icon-text-gap: 6px;
    -tachyon-icon-size: 16px;
    -tachyon-vertical-text-align: middle;
}
```

## Defaults that differ from browsers

A style starts with:

- `display: flex`
- `flex-direction: row`
- `align-items: flex-start`
- `flex-grow: 0`
- `flex-shrink: 0`
- `flex-basis: auto`
- `overflow: auto`
- no border, padding, margin or colors

Nothing is inherited from the parent element: set it in the class or in `*`.

## Not supported

These are errors, or ignored:

- Selectors:
  - combinators
  - type, id and attribute selectors
  - structural pseudo-classes other than `:first-child`, `:last-child` and `:only-child`
  - `::before` and `::after`
- At-rules other than `@font-face` (`@media`, `@import`, `@keyframes`, ...).
- `!important`, `inherit`, `initial` (except `direction: initial`) and `unset`.
- Values:
  - units other than px (`%`, `rem`, `vw`, ...; `em` works only in `line-height`)
  - `calc()`
  - `hsl()`
  - keyword gradient directions
- Properties:
  - `position`, `top`/`left`/..., `z-index`
  - `transform`, `transition`, `animation`
  - `outline`, `background-image`
  - the `border`, `background`, `font` and `text-decoration` shorthands
  - `grid-template-*`

---

[Previous: Values](values.md) | [CSS and theming](../css.md) | [Next: Using styles from C++](from-cpp.md)
