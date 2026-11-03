# Sliders

## SliderField

Filled bar with the value and optional text on it. Drag to change; click without dragging
to type a value. Clamped to `[minVal, maxVal]`; the `int` form snaps to whole numbers.

```cpp
bool SliderField(ID id, float& value, float minVal, float maxVal, std::string_view text,
                 StyleRef style, LayoutOverride&& constraints = {});
bool SliderField(ID id, int& value, int minVal, int maxVal, std::string_view text,
                 StyleRef style, LayoutOverride&& constraints = {});
```

Classes: `slider-field-track`, `slider-field-bg`, `slider-field-fill`,
`slider-field-text`, `slider-field-value`, `slider-field-edge`.

```cpp
SliderField(volume, 0.0f, 100.0f, "Volume");
```

## Slider

Groove and knob, no value text. Drag only; whole numbers.

```cpp
bool Slider(ID id, int& value, int minVal, int maxVal, StyleRef style, LayoutOverride&& constraints = {});
```

Classes: `slider`, `slider-groove`, `slider-fill`, `slider-knob`.

---

[Previous: Fields](fields.md) | [Controls](../controls.md) | [Next: Color and gradient fields](color-and-gradient.md)
