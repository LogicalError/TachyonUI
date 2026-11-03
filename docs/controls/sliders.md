# Sliders

## SliderField

A filled bar with the value, and the optional text, drawn on it. Drag the bar to change
the value. A click without a drag turns it into a numeric field so you can type a value.
The value is clamped to `[minVal, maxVal]`, and the `int` form snaps to whole numbers.

```cpp
bool SliderField(ID id, float& value, float minVal, float maxVal, std::string_view text,
                 StyleRef style, LayoutOverride&& constraints = {});
bool SliderField(ID id, int& value, int minVal, int maxVal, std::string_view text,
                 StyleRef style, LayoutOverride&& constraints = {});
```

It uses the classes `slider-field-track`, `slider-field-bg`, `slider-field-fill`,
`slider-field-text`, `slider-field-value` and `slider-field-edge`.

```cpp
SliderField(volume, 0.0f, 100.0f, "Volume");
```

## Slider

A groove and a knob with no value text. You can only drag it, and it holds whole numbers.

```cpp
bool Slider(ID id, int& value, int minVal, int maxVal, StyleRef style, LayoutOverride&& constraints = {});
```

It uses the classes `slider`, `slider-groove`, `slider-fill` and `slider-knob`.

---

[Previous: Fields](fields.md) | [Controls](../controls.md) | [Next: Color and gradient fields](color-and-gradient.md)
