# Sliders

Both sliders edit a number of yours in a range. You pass the value by reference, and the
slider writes the user's changes into it.

## SliderField

A filled bar with the value, and optional text, on it.

- Drag to change the value.
- Click without dragging to type a value.
- The value is clamped to `[minVal, maxVal]`.
- The `int` form snaps to whole numbers.

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

```cpp
if (SliderField(samples, 1, 64, "Samples"))   // int: whole numbers only
    RestartRender();
```

## Slider

A groove and a knob, with no value text.

- Drag only: you can't type a value.
- Whole numbers.

```cpp
bool Slider(ID id, int& value, int minVal, int maxVal, StyleRef style, LayoutOverride&& constraints = {});
```

```cpp
Label("Quality");
Slider(quality, 0, 4);   // member: int quality
```

Classes: `slider`, `slider-groove`, `slider-fill`, `slider-knob`.

---

[Previous: Fields](fields.md) | [Controls](../controls.md) | [Next: Color and gradient fields](color-and-gradient.md)
