# Color and gradient fields

## ColorField

A swatch, the hex value and the alpha in percent. It edits a `color4f` (four floats) or a
`color4b` (four 8-bit values). Clicking the swatch opens the picker:

- OK keeps the color.
- Cancel, Escape and the close button restore it.
  - Escape also returns focus to the swatch.
- In the square and the hue and alpha strips:
  - arrow keys step 1%, or 10% with Shift
  - Page Up / Down and Home / End also work
  - Escape in the middle of a drag restores the color from before the press and keeps
    the picker open
- `allowHDR` allows values above 1.

```cpp
bool ColorField(ID id, color4f& value, bool allowHDR, StyleRef style, LayoutOverride&& constraints = {});
bool ColorField(ID id, color4b& value, StyleRef style, LayoutOverride&& constraints = {});
```

- Returns `true` when the value changed.
- Classes: `color-field`, `color-swatch`, `color-hex`, `color-alpha`.
- The picker's classes: `color-picker` and `color-picker-*`.

```cpp
DragLabel("Color");
ColorField(color);
```

```cpp
DragLabel("Light");
if (ColorField(lightColor, true))   // color4f; true allows values above 1
    UpdateLighting();
```

```cpp
DragLabel("Tint");
ColorField(tint);   // member: color4b tint
```

## GradientField

A gradient swatch. Clicking it opens the gradient editor:

- OK, Cancel and Escape work as in the color picker.
- A stop dragged off the bar is removed on release.
- Escape in the middle of a drag restores the gradient.

```cpp
bool GradientField(ID id, Gradient& value, StyleRef style, LayoutOverride&& constraints = {});
```

```cpp
DragLabel("Gradient");
GradientField(gradient);   // member: Gradient gradient
```

```cpp
if (GradientField(gradient))   // true when the gradient changed
    RebuildRamp();
```

- Class: `gradient-swatch`.
- The editor's classes: `gradient-editor` and `gradient-editor-*`.

---

[Previous: Sliders](sliders.md) | [Controls](../controls.md) | [Next: Menus](menus.md)
