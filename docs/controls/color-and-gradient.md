# Color and gradient fields

## ColorField

Swatch, hex value and alpha in percent. Clicking the swatch opens the picker:

- OK keeps the color. Cancel, Escape and the close button restore it; Escape also returns
  focus to the swatch.
- Square, hue and alpha strips: arrow keys (1% per step, 10% with Shift), Page Up / Down,
  Home / End. Escape mid-drag restores the color from before the press and keeps the
  picker open.
- `allowHDR` allows values above 1.

```cpp
bool ColorField(ID id, color4f& value, bool allowHDR, StyleRef style, LayoutOverride&& constraints = {});
bool ColorField(ID id, color4b& value, StyleRef style, LayoutOverride&& constraints = {});
```

Returns `true` when the value changed. Classes: `color-field`, `color-swatch`,
`color-hex`, `color-alpha`; picker `color-picker` and `color-picker-*`.

```cpp
DragLabel("Color");
ColorField(color);
```

## GradientField

Gradient swatch; clicking opens the gradient editor (OK, Cancel, Escape as in the color
picker). A stop dragged off the bar is removed on release. Escape mid-drag restores the
gradient.

```cpp
bool GradientField(ID id, Gradient& value, StyleRef style, LayoutOverride&& constraints = {});
```

Classes: `gradient-swatch`; editor `gradient-editor` and `gradient-editor-*`.

---

[Previous: Sliders](sliders.md) | [Controls](../controls.md) | [Next: Menus](menus.md)
