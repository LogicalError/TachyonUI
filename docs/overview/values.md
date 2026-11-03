# Return values, content and units

## Return values and callbacks

Controls tell you about a change in two ways, and that's deliberate:

- The **callback** runs during the input step of the frame that received the input.
- The **return value** is true in the next frame's `OnGUI`.

Use whichever suits your code:

```cpp
// A callback.
Button("Greet", [this] { greeting = "Hello, " + name + "!"; });

// The return value.
if (Button("Reset", nullptr))
    ResetSettings();
```

Controls that edit a value take it by reference and write to it directly. Their return
value tells you whether it changed:

```cpp
Field(name);                    // std::string
Field(enabled, "Enabled");      // bool, with its label
if (Field(scale))               // float
    ApplyScale(scale);
```

[Controls](../controls.md) lists the overloads of each control.


## GUIContent

What a control shows is a `GUIContent`. That's a text, an icon or both, plus an optional
tooltip. Strings and icons convert to it automatically, so all of these work:

```cpp
Button("Save", onSave);
Button(saveIcon, onSave);
Button({ saveIcon, "Save", "Write the file to disk" }, onSave);
Button({ "Save", "Write the file to disk" }, onSave);   // text and tooltip
```

`GUIContent` only refers to its strings (they're `std::string_view`), so they just need
to stay alive for the duration of the call.


## Units

Sizes you pass to the API are in CSS px. The library scales them by the UI scale, the
same way it scales sizes in the stylesheet. Positions, rects, and any sizes the library
measured or computed itself are in whole device pixels.

```cpp
tachyon::LayoutOverride{}.Width(200)                            // 200 CSS px: 300 pixels at 150%
tachyon::LayoutOverride{}.Width(tachyon::Pixels{ measured })  // already pixels, used as is
```

A plain `int` converts to `CssPx`, but you have to write `Pixels` out explicitly. `int2`
and `IntRect` are always in pixels.

The UI scale is the main window's display scale multiplied by the zoom you set with
`SetUIScale(scale)`, where 1 follows the OS. When you need pixels, for example in custom
drawing, `styleManager.Px(n)` converts CSS px to pixels.

---

[Previous: Begin and End](begin-end.md) | [Overview](../overview.md) | [Next: Errors and saved state](errors-and-state.md)
