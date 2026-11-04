# Return values, content and units

## Return values and callbacks

A control reports a change in two ways; use either:

- The **callback** is a function you pass to the control. It runs in the input step of
  the frame that received the input.
- The **return value** is true in the next frame's `OnGUI`.

```cpp
// A callback.
Button("Greet", [this] { greeting = "Hello, " + name + "!"; });

// The return value.
if (Button("Reset", nullptr))
    ResetSettings();
```

Value editors take the value by reference:

- They write to it directly.
- They return whether it changed.

```cpp
Field(name);                    // std::string
Field(enabled, "Enabled");      // bool, with its label
if (Field(scale))               // float
    ApplyScale(scale);
```

Overloads per control: [Controls](../controls.md).


## GUIContent

A control shows a `GUIContent`:

- text, an icon, or both
- an optional tooltip

Strings and icons convert to it:

```cpp
Button("Save", onSave);
Button(saveIcon, onSave);
Button({ saveIcon, "Save", "Write the file to disk" }, onSave);
Button({ "Save", "Write the file to disk" }, onSave);   // text and tooltip
```

Its strings are `std::string_view`, so they only need to live for the duration of the
call. A local string is fine:

```cpp
const std::string title = "Save " + fileName;
Button(title, onSave);
```


## Units

- Sizes you pass in are CSS px, scaled by the UI scale like stylesheet sizes.
- Positions, rects and sizes the library measured or computed are whole device pixels.

```cpp
tachyon::LayoutOverride{}.Width(200)                            // 200 CSS px: 300 pixels at 150%
tachyon::LayoutOverride{}.Width(tachyon::Pixels{ measured })  // already pixels, used as is
```

- A plain `int` converts to `CssPx`; `Pixels` must be written explicitly.
- `int2` and `IntRect` are always pixels.
- The UI scale is the main window's display scale times the zoom:
  - set the zoom with `SetUIScale(scale)`
  - a zoom of 1 follows the OS

  ```cpp
  SetUIScale(1.25f);
  ```
- `styleManager.Px(n)` converts CSS px to pixels, for example for custom drawing:

  ```cpp
  const int margin = styleManager.Px(8);   // 8 CSS px, in pixels
  ```

---

[Previous: Begin and End](begin-end.md) | [Overview](../overview.md) | [Next: Errors and saved state](errors-and-state.md)
