# Values

## Numbers and units

Lengths are in CSS pixels, and the `px` suffix is optional, so `4` and `4px` mean the same.
A number can start with its decimal point (`.5`). There are no other length units, except
that `em` works in `line-height`. Box-model lengths (margin, padding, border width, radius)
are whole numbers from 0 to 65535, so a negative margin becomes 0.

## Colors

| Form | Notes |
| --- | --- |
| `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa` | |
| `rgb(r, g, b)` | Each from 0 to 255. The commas are optional. |
| `rgba(r, g, b, a)` | `a` is from 0 to 1. A value above 1 is read as 0-255. |
| Named colors | `transparent white black red green lime blue yellow cyan aqua magenta fuchsia orange purple gray grey silver navy teal maroon olive coral salmon gold pink brown indigo violet crimson darkgray darkgrey lightgray lightgrey` |
| `var(--name)` | A color variable |

## Gradients

Background, border, text and icon colors (and color variables) can also be gradients:

```css
background-color: linear-gradient(90deg, #202A3D, #6B9BFF 60%, #E8EAEF);
background-color: radial-gradient(var(--accent), transparent);
```

- `linear-gradient([angle,] stop, stop, ...)` takes an optional angle: a number with `deg`,
  `grad`, `rad` or `turn`, or a bare number in degrees. The default is 0.
- `radial-gradient(stop, stop, ...)` has the same form.
- A stop is a color with an optional position in percent. Stops without a position are
  spread evenly, with the first at 0% and the last at 100% by default. You can have up to 8
  stops.
- Keyword directions such as `to right`, shapes and sizes aren't supported.

---

[Previous: Syntax](syntax.md) | [CSS and theming](../css.md) | [Next: Properties](properties.md)
