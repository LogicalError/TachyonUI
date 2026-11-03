# Color and gradient fields

## ColorField

Shows a swatch, the color as hex, and its alpha in percent. Click the swatch to open the
color picker. OK keeps the new color. Cancel, Escape and the close button all restore the
original, and Escape also puts the keyboard back on the swatch.

Inside the picker, the square and the hue and alpha strips respond to the arrow keys (1%
per step, 10% with Shift), Page Up / Down and Home / End. If you press Escape while
dragging on one of them, the color goes back to what it was before the press and the
picker stays open. Set `allowHDR` to allow values above 1.

```cpp
bool ColorField(ID id, color4f& value, bool allowHDR, StyleRef style, LayoutOverride&& constraints = {});
bool ColorField(ID id, color4b& value, StyleRef style, LayoutOverride&& constraints = {});
```

It returns `true` when the value changed. The field uses the classes `color-field`,
`color-swatch`, `color-hex` and `color-alpha`. The picker uses `color-picker` and the
`color-picker-*` classes.

```cpp
DragLabel("Color");
ColorField(color);
```

## GradientField

Shows a gradient swatch. Clicking it opens the gradient editor, which handles OK, Cancel
and Escape the same way as the color picker. If you drag a stop off the bar, it's removed
when you let go. Pressing Escape while dragging a stop puts the gradient back.

```cpp
bool GradientField(ID id, Gradient& value, StyleRef style, LayoutOverride&& constraints = {});
```

The swatch uses the class `gradient-swatch`. The editor uses `gradient-editor` and the
`gradient-editor-*` classes.

---

[Previous: Sliders](sliders.md) | [Controls](../controls.md) | [Next: Menus](menus.md)
