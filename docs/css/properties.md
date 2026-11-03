# Properties

"state" properties can differ per state (`:hover`, `:focus`, ...). The rest are layout
properties and apply to the whole style.

## Box model

| Property | Values |
| --- | --- |
| `margin`, `padding` | 1 to 4 lengths (top, right, bottom, left, as in CSS) or `var()` |
| `margin-top`, `-right`, `-bottom`, `-left` | length or `var()` |
| `padding-top`, `-right`, `-bottom`, `-left` | length or `var()` |

## Borders and radius

| Property | Values | |
| --- | --- | --- |
| `border-width` | 1 to 4 lengths or `var()` | |
| `border-top-width`, `-right-`, `-bottom-`, `-left-width` | length or `var()` | |
| `border-radius` | 1 to 4 lengths or `var()` (top-left, top-right, bottom-right, bottom-left; 2 values = TL/BR and TR/BL; 3 values = TL, TR/BL, BR) | |
| `border-top-left-radius`, `border-top-right-radius`, `border-bottom-right-radius`, `border-bottom-left-radius` | length or `var()` | |
| `border-color` | color or gradient, all four edges | state |
| `border-top-color`, `-right-`, `-bottom-`, `-left-color` | color or gradient | state |
| `border-style` | `solid`, `dashed` (6px dashes), `dotted` (2px dashes) | |
| `-tachyon-border-dash-size` | dash length; 0 = solid | |
| `-tachyon-border-dash-offset` | where the dash pattern starts | |

No `border` shorthand: set width, color and style separately.

## Colors and backgrounds

| Property | Values | |
| --- | --- | --- |
| `background-color` | color or gradient | state |
| `color` | color or gradient, for text | state |
| `-tachyon-icon-color` | color or gradient, for the icon. Unset: the text color. | state |
| `-tachyon-state-layer` | `none` (default) or `content`: hovered and pressed backgrounds come from the text color (below). | |

With `-tachyon-state-layer: content`:

- Hover background = text color at 8% over the non-hover background; pressed = 14%.
- Applies in every state with hover or press (including on/focused), not to selected text
  or disabled.
- Replaces any `background-color` in `:hover` and `:active` rules.
- Gradient or image backgrounds are left alone.

```css
.button       { color: var(--text-1); background-color: var(--bg-control); -tachyon-state-layer: content; }
.button:hover { border-color: var(--line-strong); }   /* the background comes from the layer */
```

## Opacity

| Property | Values | |
| --- | --- | --- |
| `opacity` | number 0 to 1, or `var()` | state |

- Applies to the element and its contents; nested opacities multiply.
- Each part fades separately, so overlapping parts (text over background) show through a
  little.
- Disabled controls fade on their own: the topmost disabled element uses
  `--disabled-opacity` from `:root` (0.4 in the default theme); parts inside don't fade
  twice. Override per class with `opacity` in its `:disabled` rule:

```css
:root                   { --disabled-opacity: 0.4; }
.menu-item-row:disabled { opacity: 0.45; }
```

## Box shadow and focus ring

| Property | Values | |
| --- | --- | --- |
| `box-shadow` | `none`, or 1 or 2 layers separated by commas | state |

A layer is `<x> <y> [<blur> [<spread>]]` plus a color (before or after the lengths), or a
`var()` holding shadows. The first layer is on top.

```css
.node                      { box-shadow: var(--shadow-node); }
.node::highlight(selected) { box-shadow: var(--selected-ring), var(--shadow-node); }
```

- Max 2 layers, color required, blur not negative, `inset` only in `:focus-visible`.
- `:focus-visible` takes exactly one `box-shadow` layer and nothing else, and can't combine
  with other states. It's the focus ring, drawn over the state's shadows when focus came
  from the keyboard.
- An inset ring (outline inside the box) must be `inset 0 0 0 <width> <color>`.

```css
.button:focus-visible { box-shadow: var(--focus-ring); }              /* 0 0 0 3px #6B9BFF4D */
.menu-item:focus-visible { box-shadow: inset 0 0 0 1px var(--accent); }
```

## Text and fonts

| Property | Values | |
| --- | --- | --- |
| `font-family` | comma-separated family names (quoted or not) and generics: `default`, `emoji`, `sans-serif`, `serif`, `monospace`, `math`. The first name is the font; the first generic is used until that font is registered. A quoted generic is a name. | state |
| `font-size` | number or `var()` | state |
| `font-weight` | `normal`, `thin`, `extra-light`/`ultralight`, `light`, `medium`, `demi-bold`/`semi-bold`/`semibold`, `bold`, `extra-bold`/`ultrabold`, `black`/`heavy`, `extra-black`/`ultra-black`, or a number (rounded up to the next hundred; above 900 is extra-black) | state |
| `font-style` | `normal`, `italic`, `oblique` | state |
| `font-stretch` | `normal`, `ultra-condensed`, `extra-condensed`, `condensed`, `semi-condensed`, `semi-expanded`, `expanded`, `extra-expanded`, `ultra-expanded` | state |
| `line-height` | `normal`, bare number (times font size), `<n>em` (same), `<n>px` | state |
| `letter-spacing`, `word-spacing` | number or `var()` | state |
| `-tachyon-language` | BCP 47 tag (`ja`, `"zh-Hans"`) for shaping and font fallback, or `none` | state |
| `text-align` | `start`, `end`, `center`, `left`, `right`, `top`, `bottom` | |
| `-tachyon-vertical-text-align` | `start`/`top`, `center`/`middle`, `end`/`bottom`. Vertical position of the text (and icon) block in the box. | |
| `vertical-align`, `dominant-baseline`, `-tachyon-text-baseline` | `auto`/`baseline`/`alphabetic`, `ideographic`, `central`, `hanging`, `mathematical`, `middle`, `text-top`, `text-bottom`. Picks the glyph baseline, not the block position. | |
| `text-wrap` | `nowrap`, `wrap`, `word-char` (wrap at words; break too-long words between characters) | Without a fixed height, the element is as tall as its wrapped lines. |
| `text-overflow` | `clip`, `ellipsis` | |
| `direction` | `auto`, `initial` (= auto), `ltr`, `rtl` | |

## Text decoration

Four lines: `underline`, `overline`, `throughline`, `bottomline`. A line is drawn when its
thickness is above 0, in the text color unless given its own.

| Property | Values | |
| --- | --- | --- |
| `text-<line>-color` | color (not a gradient) | state |
| `text-<line>-thickness` | number or `var()` | state |
| `text-<line>-offset` | number or `var()` | state |
| `text-<line>-style` | `solid`, `double`, `dotted`, `dashed`, `wavy` | state |

Example: `text-underline-color: var(--text-1); text-underline-offset: 2px;`. No
`text-decoration` shorthand.

## Text shadow and glow

| Property | Values | |
| --- | --- | --- |
| `text-shadow` | `none`, or 1 or 2 layers separated by commas | state |

A layer is `<x> <y> [<blur>]` plus a color (before or after the lengths), or a `var()`
holding shadows. The first layer is on top. The shadow has the shape of the text, its
decorations and icon (color emoji included), in the shadow color. No offset gives a glow:

```css
.title   { text-shadow: 0 1px 2px #00000080; }
.warning { text-shadow: 0 0 6px var(--accent); }   /* a glow */
```

- Max 2 layers, color required, blur not negative. No spread, no `inset`.
- Blur comes from the glyph distance field, which reaches about 7px around 14px text
  (11px+ for text 32px and up). Blur up to two thirds of that (about 4px on 14px text) is
  exact; wider blur is narrowed to fit.
- Clipped to the element's padding box, like its text.
- Changes with state at once; no transitions.

## Flex

Every box is a flex container unless it's `display: grid`.

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
| `flex` | `none` (0 0 auto), `auto` (1 1 auto), or `<grow> [<shrink> [<basis> \| auto]]`. One number: shrink 1, basis 0. |
| `break-after` | `auto`, `always`/`right` (line break after this item), `left` (line break before it), `avoid` (keep on the line with the next item) |

A wrapping column (`flex-direction: column; flex-wrap: wrap`) starts a new column when it
runs out of height: a set `height`, or content height capped by `max-height`. Each column
distributes its own free height (`flex-grow`, `flex-shrink`) and is placed by
`justify-content`.

## Grid

| Property | Values |
| --- | --- |
| `display: grid` | makes the box a grid container |
| `column-gap`, `row-gap` | length or `var()` |
| `justify-items` | `stretch` (default, items fill the cell), or a `justify-content` value to place items in the cell |
| `grid-column`, `grid-row` | `<start>`, `auto`, `span <n>`, or `<start> / span <n>` |
| `grid-column-start`, `grid-row-start` | number or `auto` |
| `-tachyon-grid-column-track`, `-tachyon-grid-row-track` | `auto`, `<n>px` (or bare number), `<n>fr`. Set on an item; sizes the column or row it sits in. Items in one track merge: fr beats auto, auto beats px, same kind takes the larger. |

No `grid-template-*` properties.

## Sizing

| Property | Values |
| --- | --- |
| `width`, `height` | `auto` or a length |
| `min-width`, `max-width`, `min-height`, `max-height` | a length |

No `var()` or percentages. Override per element from code with a `LayoutOverride` (see
[Controls](../controls.md)).

## Display, overflow and visibility

| Property | Values |
| --- | --- |
| `display` | `flex`, `grid`, `none` (not drawn, no space) |
| `overflow` | `auto` (clips, scrolls when needed), `hidden` (clips), `visible`. Both axes. |
| `overflow-x`, `overflow-y` | same values, one axis |
| `visibility` | `visible`, `collapsed` (not drawn, no space) |
| `-tachyon-clamp-scroll` | `true`/`1` (default), `false`/`0`. False lets content scroll past its edges. |

## Cursor

| Property | Values |
| --- | --- |
| `cursor` | `default`, `text`, `wait`, `crosshair`, `pointer`, `col-resize`, `row-resize`, `ew-resize`, `ns-resize`, `nwse-resize`, `nesw-resize`, `move`, `not-allowed`, `none` |

## Icons

For controls with an icon next to their text.

| Property | Values | |
| --- | --- | --- |
| `-tachyon-icon-side` | `left` (default), `right`, `top`, `bottom` | |
| `-tachyon-icon-text-gap` | length or `var()`, between icon and text | |
| `-tachyon-icon-size` | length or `var()`; 0 = the icon's own size | |
| `-tachyon-icon-offset-y` | length (may be negative) or `var()`; moves the icon up | |
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

A style starts with `display: flex`, `flex-direction: row`, `align-items: flex-start`,
`flex-grow: 0`, `flex-shrink: 0`, `flex-basis: auto`, `overflow: auto`, no border, padding,
margin or colors. Nothing is inherited: set it in the class or in `*`.

## Not supported

Errors or ignored:

- Selectors: combinators, type, id and attribute selectors, structural pseudo-classes
  other than `:first-child`, `:last-child` and `:only-child`, and `::before`/`::after`.
- At-rules other than `@font-face` (`@media`, `@import`, `@keyframes`, ...).
- `!important`, `inherit`, `initial` (except `direction: initial`) and `unset`.
- Units other than px (`%`, `rem`, `vw`, ...; `em` works only in `line-height`), `calc()`,
  `hsl()`, and keyword gradient directions.
- `position`, `top`/`left`/..., `z-index`, `transform`, `transition`,
  `animation`, `outline`, `background-image`, the `border`, `background`, `font` and
  `text-decoration` shorthands, and `grid-template-*`.

---

[Previous: Values](values.md) | [CSS and theming](../css.md) | [Next: Using styles from C++](from-cpp.md)
