# Buttons and toggles

## Button

```cpp
bool Button(ID id, const GUIContent& content, std::function<void()> onClick, StyleRef style,
            LayoutOverride&& constraints = {});
```

Returns `true` the frame after the click. Class: `button` (`toolbar-button` in a toolbar).

```cpp
Button("Click me", [this] { ++clicks; });
```

## CommandButton

Icon button that runs a `commandManager` command, which supplies the icon and callback.
Tooltip: the command's description plus its shortcut.

```cpp
bool CommandButton(CommandID command, StyleRef style, LayoutOverride&& constraints = {});
```

Class: `button`.

## Toggle

A button that stays pressed while on.

```cpp
bool Toggle(ID id, bool value, const GUIContent& content, std::function<void(bool)> onToggle, StyleRef style,
            LayoutOverride&& constraints = {});
bool Toggle(ID id, bool& value, const GUIContent& content, StyleRef style, LayoutOverride&& constraints = {});
```

The `bool&` form flips `value` itself; otherwise `onToggle` gets the new value. Returns
`true` the frame after a click. Class: `toggle` (`toolbar-toggle` in a toolbar). On/off
blends over 120 ms ([Changing state](../css/syntax.md#changing-state)).

```cpp
Toggle(bold, "B");
Toggle(mode == Mode::Row, "Row", [this](bool) { mode = Mode::Row; });
```

`Field(bool& value, std::string_view text)` is the same toggle.

## Switch

Track and knob, with an optional label after it. Knob slides over 120 ms.

```cpp
bool Switch(ID id, bool value, std::string_view text, std::function<void(bool)> onToggle, StyleRef style,
            LayoutOverride&& constraints = {});
bool Switch(ID id, bool& value, std::string_view text = {}, LayoutOverride&& constraints = {});
```

Classes: `switch`, `switch-track`, `switch-knob`, `switch-label`.

```cpp
Switch(enabled, enabled ? "On" : "Off");
```

## Checkbox

Box with a tick, then its label. `onToggle` gets the new value; the `bool&` form writes it
itself. Box color blends over 120 ms; the tick appears at once.

```cpp
bool Checkbox(ID id, bool value, std::string_view text, std::function<void(bool)> onToggle,
              StyleRef style, LayoutOverride&& constraints = {});
bool Checkbox(ID id, bool& value, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
```

Classes: `checkbox`, `checkbox-box`, `checkbox-label`.

```cpp
Checkbox(gridCheckID, showGrid, "Show grid", [this](bool on) { showGrid = on; });
```

---

[Previous: Text and images](text-and-images.md) | [Controls](../controls.md) | [Next: Fields](fields.md)
