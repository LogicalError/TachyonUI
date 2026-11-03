# Properties

Properties marked "state" can have a different value in each state (`:hover`, `:focus`, and
so on). The rest are layout properties and apply to the style as a whole.

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
| `border-color` | color or gradient, for all four edges | state |
| `border-top-color`, `-right-`, `-bottom-`, `-left-color` | color or gradient | state |
| `border-style` | `solid`, `dashed` (6px dashes), `dotted` (2px dashes) | |
| `-tachyon-border-dash-size` | the dash length; 0 means solid | |
| `-tachyon-border-dash-offset` | where the dash pattern starts | |

There's no `border` shorthand, so set the width, color and style separately.

## Colors and backgrounds

| Property | Values | |
| --- | --- | --- |
| `background-color` | color or gradient | state |
| `color` | color or gradient, used for the text | state |
| `-tachyon-icon-color` | color or gradient, used for the icon. If it's unset, the icon uses the text color. | state |
| `-tachyon-state-layer` | `none` (default) or `content`. With `content`, the hovered and pressed backgrounds are made from the text color (see below). | |

With `-tachyon-state-layer: content`, a control's hovered background is its text color at
8% laid over its background without the hover. The pressed background is the same at 14%.
This applies in every state that has a hover or press, including when the control is on or
focused, but not for selected text and not when it's disabled. The effect is that a dark
button lightens, a primary button (dark text on the accent) darkens, and the light theme
darkens. It replaces any `background-color` in `:hover` and `:active` rules. A background
that's a gradient or an image is left as it is.

```css
.button       { color: var(--text-1); background-color: var(--bg-control); -tachyon-state-layer: content; }
.button:hover { border-color: var(--line-strong); }   /* the background comes from the layer */
```

## Opacity

| Property | Values | |
| --- | --- | --- |
| `opacity` | a number from 0 to 1, or `var()` | state |

The element and everything inside it are drawn at that opacity. An element inside a faded
one multiplies its own opacity with its parent's. Each part is faded separately, so where a
control's parts overlap (its text over its background), they show through each other a
little.

You don't need a rule to fade a disabled control; it fades as a whole on its own. Its
topmost disabled element takes `--disabled-opacity` from `:root` (0.4 in the default theme),
and the parts inside it don't fade a second time. To use a different value for one class,
give its `:disabled` rule an `opacity`:

```css
:root                   { --disabled-opacity: 0.4; }
.menu-item-row:disabled { opacity: 0.45; }
```

## Box shadow and focus ring

| Property | Values | |
| --- | --- | --- |
| `box-shadow` | `none`, or 1 or 2 layers separated by commas | state |

A layer is `<x> <y> [<blur> [<spread>]]` plus a color, which can go before or after the
lengths. A layer can also be a `var()` holding shadows. The first layer is drawn on top.

```css
.node                      { box-shadow: var(--shadow-node); }
.node::highlight(selected) { box-shadow: var(--selected-ring), var(--shadow-node); }
```

There are a few limits. You can have at most 2 layers, the color is required, blur can't be
negative, and `inset` is only allowed in `:focus-visible`.

`:focus-visible` takes exactly one `box-shadow` layer and nothing else, and it can't be
combined with other states. It sets the focus ring, which is drawn over the state's own
shadows when focus came from the keyboard. An inset ring is an outline inside the box, and
it must have the form `inset 0 0 0 <width> <color>`:

```css
.button:focus-visible { box-shadow: var(--focus-ring); }              /* 0 0 0 3px #6B9BFF4D */
.menu-item:focus-visible { box-shadow: inset 0 0 0 1px var(--accent); }
```

## Text and fonts

| Property | Values | |
| --- | --- | --- |
| `font-family` | a comma-separated list of family names (quoted or not) and generics: `default`, `emoji`, `sans-serif`, `serif`, `monospace`, `math`. The first name is the font. The first generic is used while that font isn't registered. A generic in quotes counts as a name. | state |
| `font-size` | number or `var()` | state |
| `font-weight` | `normal`, `thin`, `extra-light`/`ultralight`, `light`, `medium`, `demi-bold`/`semi-bold`/`semibold`, `bold`, `extra-bold`/`ultrabold`, `black`/`heavy`, `extra-black`/`ultra-black`, or a number (rounded up to the next hundred; anything above 900 is extra-black) | state |
| `font-style` | `normal`, `italic`, `oblique` | state |
| `font-stretch` | `normal`, `ultra-condensed`, `extra-condensed`, `condensed`, `semi-condensed`, `semi-expanded`, `expanded`, `extra-expanded`, `ultra-expanded` | state |
| `line-height` | `normal`, a bare number (times the font size), `<n>em` (the same), `<n>px` | state |
| `letter-spacing`, `word-spacing` | number or `var()` | state |
| `-tachyon-language` | a BCP 47 tag (`ja`, `"zh-Hans"`) used for shaping and font fallback, or `none` | state |
| `text-align` | `start`, `end`, `center`, `left`, `right`, `top`, `bottom` | |
| `-tachyon-vertical-text-align` | `start`/`top`, `center`/`middle`, `end`/`bottom`. This sets where the text (and icon) block sits vertically in the box. | |
| `vertical-align`, `dominant-baseline`, `-tachyon-text-baseline` | `auto`/`baseline`/`alphabetic`, `ideographic`, `central`, `hanging`, `mathematical`, `middle`, `text-top`, `text-bottom`. This picks the glyph baseline, not the block position. | |
| `text-wrap` | `nowrap`, `wrap`, `word-char` (wrap at words, and break a word that's too long between characters) | If the element's height isn't fixed, it's as tall as its lines at the width it's given. |
| `text-overflow` | `clip`, `ellipsis` | |
| `direction` | `auto`, `initial` (same as auto), `ltr`, `rtl` | |

## Text decoration

There are four lines: `underline`, `overline`, `throughline` and `bottomline`. Each one has
four properties, all per state. A line is drawn when its thickness is above 0, in the text
color unless you give it a color of its own.

| Property | Values | |
| --- | --- | --- |
| `text-<line>-color` | color (not a gradient) | state |
| `text-<line>-thickness` | number or `var()` | state |
| `text-<line>-offset` | number or `var()` | state |
| `text-<line>-style` | `solid`, `double`, `dotted`, `dashed`, `wavy` | state |

For example: `text-underline-color: var(--text-1); text-underline-offset: 2px;`. There's
no `text-decoration` shorthand.

## Text shadow and glow

| Property | Values | |
| --- | --- | --- |
| `text-shadow` | `none`, or 1 or 2 layers separated by commas | state |

A layer is `<x> <y> [<blur>]` plus a color, which can go before or after the lengths. A
layer can also be a `var()` holding shadows. The first layer is drawn on top. The shadow has
the shape of the text, its decorations and its icon, in the shadow's color (color emoji
included). It's moved by the offset and blurred like a `box-shadow`. With no offset, you get
a glow:

```css
.title   { text-shadow: 0 1px 2px #00000080; }
.warning { text-shadow: 0 0 6px var(--accent); }   /* a glow */
```

- You can have at most 2 layers. The color is required and blur can't be negative. There's
  no spread and no `inset`.
- The blur is made from the glyphs' distance field in the atlas, which reaches 16 atlas
  pixels past the text. Small text is rasterized at 32px, so that works out to about 7px
  around 14px text. Text of 32px and up gets 11px or more. A blur up to two thirds of that
  (about 4px on 14px text) is drawn exactly as asked. A wider one is narrowed to fit, so it
  still fades out smoothly, just not as far.
- Shadows are clipped to the element's padding box, the same as its text.
- A shadow changes with the state straight away. It doesn't take part in transitions.

## Flex

Every box is a flex container unless it's `display: grid`.

| Property | Values |
| --- | --- |
| `flex-direction` | `row`, `row-reverse`, `column`, `column-reverse` |
| `flex-wrap` | `nowrap`, `wrap`, `wrap-reverse` |
| `justify-content` | `flex-start`/`start`, `flex-end`/`end`, `center`, `space-between`, `space-around`, `space-evenly` (works for flex and grid) |
| `align-items` | `flex-start`/`start`, `flex-end`/`end`, `center`, `stretch` (works for flex and grid) |
| `align-content` | `flex-start`/`start`, `flex-end`/`end`, `center`, `stretch`, `space-between`, `space-around`, `space-evenly` (between wrapped lines) |
| `align-self` | `auto`, `flex-start`/`start`, `flex-end`/`end`, `center`, `stretch` |
| `gap` | length or `var()`, the space between flex items |
| `flex-grow`, `flex-shrink` | whole number or `var()` (a fraction is cut off) |
| `flex-basis` | `auto`, length or `var()` |
| `flex` | `none` (0 0 auto), `auto` (1 1 auto), or `<grow> [<shrink> [<basis> \| auto]]`. With one number, shrink is 1 and basis is 0. |
| `break-after` | `auto`, `always`/`right` (break the line after this item), `left` (break the line before it), `avoid` (keep it on the same line as the next item) |

A wrapping column (`flex-direction: column; flex-wrap: wrap`) starts a new column where it
runs out of height. That's either a set `height`, or its content's height capped by
`max-height`. Each column shares out its own free height (`flex-grow`, `flex-shrink`) and is
placed by `justify-content`, just as each line of a wrapping row is across.

## Grid

| Property | Values |
| --- | --- |
| `display: grid` | makes the box a grid container |
| `column-gap`, `row-gap` | length or `var()` |
| `justify-items` | `stretch` (the default, where items fill their cell), or a `justify-content` value to place items within the cell |
| `grid-column`, `grid-row` | `<start>`, `auto`, `span <n>`, or `<start> / span <n>` |
| `grid-column-start`, `grid-row-start` | number or `auto` |
| `-tachyon-grid-column-track`, `-tachyon-grid-row-track` | `auto`, `<n>px` (or a bare number), `<n>fr`. You set this on an item, and it sizes the column or row the item sits in. Items in the same track are merged: fr wins over auto, auto wins over px, and of two of the same kind the larger value wins. |

There are no `grid-template-*` properties.

## Sizing

| Property | Values |
| --- | --- |
| `width`, `height` | `auto` or a length |
| `min-width`, `max-width`, `min-height`, `max-height` | a length |

Sizes don't take `var()` or percentages. Your code can override them for a single element
with a `LayoutOverride` (see [Controls](../controls.md)).

## Display, overflow and visibility

| Property | Values |
| --- | --- |
| `display` | `flex`, `grid`, `none` (not drawn and takes no space) |
| `overflow` | `auto` (clips, and scrolls when needed), `hidden` (clips), `visible`. Sets both axes. |
| `overflow-x`, `overflow-y` | the same values, for one axis |
| `visibility` | `visible`, `collapsed` (not drawn and takes no space) |
| `-tachyon-clamp-scroll` | `true`/`1` (default), `false`/`0`. Set it to false to let the content scroll past its edges. |

## Cursor

| Property | Values |
| --- | --- |
| `cursor` | `default`, `text`, `wait`, `crosshair`, `pointer`, `col-resize`, `row-resize`, `ew-resize`, `ns-resize`, `nwse-resize`, `nesw-resize`, `move`, `not-allowed`, `none` |

## Icons

These are for controls that show an icon next to their text.

| Property | Values | |
| --- | --- | --- |
| `-tachyon-icon-side` | `left` (default), `right`, `top`, `bottom` | |
| `-tachyon-icon-text-gap` | length or `var()`, the space between icon and text | |
| `-tachyon-icon-size` | length or `var()`. 0 means the icon's own size. | |
| `-tachyon-icon-offset-y` | length (may be negative) or `var()`. Moves the icon up. | |
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
`flex-grow: 0`, `flex-shrink: 0`, `flex-basis: auto`, `overflow: auto`, no border, no
padding, no margin and no colors. Nothing is inherited from the parent element, so a class
has to set what it needs or get it from `*`.

## Not supported

These parts of browser CSS aren't implemented. Using them is either an error or ignored:

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
