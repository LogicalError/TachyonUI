# Sliders

Each slider edits a number of yours in a range. You pass the value by reference, and the
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
- Works with any number type: `float`, `double`, and the 8 to 64 bit integers.
  - Integer types step in whole numbers.
    - For example, a `uint8_t` in `[50, 100]` goes 50, 51, 52 ...
  - `float` and `double` move smoothly.
    - For example, a `float` in `[0, 1]` dragged a quarter of the way is about 0.25.
- Keys, once it has focus:
  - Arrows step by one (integers) or a hundredth of the range.
    - For example, a `float` in `[0, 1]` steps by 0.01.
  - Page Up / Page Down step a tenth of the range.
    - For example, an `int` in `[0, 100]` steps by 10.
  - Home and End jump to the ends.

```cpp
template<typename T>
bool Slider(ID id, T& value, T minVal, T maxVal, StyleRef style, LayoutOverride&& constraints = {});
```

```cpp
Label("Quality");
Slider(quality, 0, 4);   // member: int quality
```

```cpp
Label("Opacity");
Slider(opacity, 0.0f, 1.0f);   // member: float opacity
```

Classes: `slider`, `slider-groove`, `slider-fill`, `slider-knob`.

## RangeField

A slider with a value box after it, for a number that has a range.

- The slider takes the room; the box is 56px wide.
- Drag the slider, or type into the box.
  - For example, type 0.4 and the knob moves to 0.4.
- Both clamp to `[minVal, maxVal]`.
  - For example, typing 7 into a `[0, 1]` field gives 1.
- The number types are Slider's.

```cpp
template<typename T>
bool RangeField(ID id, T& value, T minVal, T maxVal, LayoutOverride&& constraints = {});
```

```cpp
DragLabel("Roughness");
RangeField(roughness, 0.0f, 1.0f);   // member: float roughness
```

```cpp
DragLabel("Quality");
RangeField(quality, uint8_t(50), uint8_t(100));   // member: uint8_t quality
```

Classes: `range-field` (the row), then the `slider` and `number-field` classes for its parts.

---

[Previous: Fields](fields.md) | [Controls](../controls.md) | [Next: Color and gradient fields](color-and-gradient.md)
