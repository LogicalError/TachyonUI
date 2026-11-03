# Values

## Numbers and units

- Lengths are CSS pixels; `px` is optional (`4` = `4px`). A leading decimal point works
  (`.5`).
- No other length units, except `em` in `line-height`.
- Box-model lengths (margin, padding, border width, radius) are whole numbers 0 to 65535, so
  a negative margin becomes 0.

## Colors

| Form | Notes |
| --- | --- |
| `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa` | |
| `rgb(r, g, b)` | Each 0 to 255. Commas optional. |
| `rgba(r, g, b, a)` | `a` is 0 to 1; above 1 is read as 0-255. |
| Named colors | `transparent white black red green lime blue yellow cyan aqua magenta fuchsia orange purple gray grey silver navy teal maroon olive coral salmon gold pink brown indigo violet crimson darkgray darkgrey lightgray lightgrey` |
| `var(--name)` | A color variable |

## Gradients

Background, border, text and icon colors (and color variables) can be gradients:

```css
background-color: linear-gradient(90deg, #202A3D, #6B9BFF 60%, #E8EAEF);
background-color: radial-gradient(var(--accent), transparent);
```

- `linear-gradient([angle,] stop, stop, ...)`: angle in `deg`, `grad`, `rad`, `turn`, or a
  bare number in degrees. Default 0.
- `radial-gradient(stop, stop, ...)`: same form.
- A stop is a color with an optional percent position. Unpositioned stops are spread evenly
  (first 0%, last 100%). Up to 8 stops.
- Not supported: keyword directions (`to right`), shapes, sizes.

---

[Previous: Syntax](syntax.md) | [CSS and theming](../css.md) | [Next: Properties](properties.md)
