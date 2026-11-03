# Buttons and toggles

## Button

```cpp
bool Button(ID id, const GUIContent& content, std::function<void()> onClick, StyleRef style,
            LayoutOverride&& constraints = {});
```

`onClick` runs when the click is handled, and `Button` returns `true` in the frame after
the click. The default class is `button`, or `toolbar-button` inside a toolbar.

```cpp
Button("Click me", [this] { ++clicks; });
```

## CommandButton

An icon button that runs a command from `commandManager`. The icon and the callback come
from the command. Its tooltip shows the command's description, followed by the shortcut
in a small chip.

```cpp
bool CommandButton(CommandID command, StyleRef style, LayoutOverride&& constraints = {});
```

The default class is `button`.

## Toggle

A button that stays pressed while it's on.

```cpp
bool Toggle(ID id, bool value, const GUIContent& content, std::function<void(bool)> onToggle, StyleRef style,
            LayoutOverride&& constraints = {});
bool Toggle(ID id, bool& value, const GUIContent& content, StyleRef style, LayoutOverride&& constraints = {});
```

If you pass a `bool&`, the toggle flips `value` itself. Otherwise `onToggle` gets the new
value. Either way it returns `true` in the frame after a click. The default class is
`toggle`, or `toolbar-toggle` in a toolbar. Switching on or off blends over 120 ms (see
[Changing state](../css/syntax.md#changing-state)).

```cpp
Toggle(bold, "B");
Toggle(mode == Mode::Row, "Row", [this](bool) { mode = Mode::Row; });
```

`Field(bool& value, std::string_view text)` gives you the same toggle.

## Switch

An on/off switch, drawn as a track and a knob, with an optional label after it. When you
turn it on or off, the knob slides across over 120 ms while the track and knob change
color.

```cpp
bool Switch(ID id, bool value, std::string_view text, std::function<void(bool)> onToggle, StyleRef style,
            LayoutOverride&& constraints = {});
bool Switch(ID id, bool& value, std::string_view text = {}, LayoutOverride&& constraints = {});
```

It uses the classes `switch`, `switch-track`, `switch-knob` and `switch-label`.

```cpp
Switch(enabled, enabled ? "On" : "Off");
```

## Checkbox

A box with a tick, followed by its label. A click calls `onToggle` with the new value,
and the forms that take a `bool&` change the value themselves. The box's color blends
over 120 ms, but the tick appears straight away.

```cpp
bool Checkbox(ID id, bool value, std::string_view text, std::function<void(bool)> onToggle,
              StyleRef style, LayoutOverride&& constraints = {});
bool Checkbox(ID id, bool& value, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
```

It uses the classes `checkbox`, `checkbox-box` and `checkbox-label`.

```cpp
Checkbox(gridCheckID, showGrid, "Show grid", [this](bool on) { showGrid = on; });
```

---

[Previous: Text and images](text-and-images.md) | [Controls](../controls.md) | [Next: Fields](fields.md)
