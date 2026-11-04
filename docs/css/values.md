# Values

## Numbers and units

- Lengths are CSS pixels.
  - `px` is optional: `4` is the same as `4px`.
  - A leading decimal point works: `.5`.
- There are no other length units, except `em` in `line-height`.
- Box-model lengths are whole numbers from 0 to 65535.
  - These are margin, padding, border width and radius.
  - So a negative margin becomes 0.

From `themes/styles.css` and `themes/showcase.css`:

```css
padding: 4px 2px;
font-size: 11.5px;
line-height: 1.4;
```

## Colors

| Form | Notes |
| --- | --- |
| `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa` | |
| `rgb(r, g, b)` | Each 0 to 255. Commas optional. |
| `rgba(r, g, b, a)` | `a` is 0 to 1; above 1 is read as 0-255. |
| Named colors | `transparent white black red green lime blue yellow cyan aqua magenta fuchsia orange purple gray grey silver navy teal maroon olive coral salmon gold pink brown indigo violet crimson darkgray darkgrey lightgray lightgrey` |
| `var(--name)` | A color variable |

- In `rgba`, `a` is the alpha: how opaque the color is.
  - From 0 to 1, where 0 is fully transparent and 1 fully opaque.
  - A value above 1 is read on the 0 to 255 scale instead.
- In `#rgba` and `#rrggbbaa`, the last digits are the alpha.

From `themes/dark.css`:

```css
--accent: #6B9BFF;
--selection: #6B9BFF59;          /* selected text: the accent at 35%, under text that stays --text-1 */
--shadow-node: 0 8px 20px rgba(0, 0, 0, 0.35);
```

## Gradients

A gradient is a color that changes across the element. These can be gradients:

- background colors
- border colors
- text colors
- icon colors
- color variables

```css
background-color: linear-gradient(90deg, #202A3D, #6B9BFF 60%, #E8EAEF);
background-color: radial-gradient(var(--accent), transparent);
```

- `linear-gradient([angle,] stop, stop, ...)` changes color along a straight line.
  - The angle is in `deg`, `grad`, `rad` or `turn`, or a bare number in degrees.
  - The default angle is 0.
- `radial-gradient(stop, stop, ...)` changes color outward from the center. It takes the
  same form.
- A stop is a color with an optional percent position, such as `#6B9BFF 60%`.
  - Stops without a position are spread evenly: the first at 0%, the last at 100%.
  - A gradient has up to 8 stops.
- Not supported:
  - keyword directions (`to right`)
  - shapes
  - sizes

---

[Previous: Syntax](syntax.md) | [CSS and theming](../css.md) | [Next: Properties](properties.md)
