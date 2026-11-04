# Buttons and toggles

The controls on this page take a callback: a function the control calls when the user
clicks it. The callback runs after the frame the click arrived in, and the control
returns `true` in the frame after that. See [Common conventions](conventions.md).

## Button

```cpp
bool Button(ID id, const GUIContent& content, std::function<void()> onClick, StyleRef style,
            LayoutOverride&& constraints = {});
```

- `content` is a `GUIContent`: text, an icon, or both, and an optional tooltip.
- `onClick` runs when the button is clicked.
- Returns `true` the frame after the click.
- Class: `button`.
  - In a toolbar: `toolbar-button`.

```cpp
Button("Click me", [this] { ++clicks; });
```

```cpp
if (Button("Reset", [this] { Reset(); }))   // true the frame after the click
    statusText = "Reset done";
```

## CommandButton

An icon button that runs a command of `commandManager`. A command is registered once,
with an icon, a shortcut, a callback and a description:

- The button's icon and callback come from the command.
- Its tooltip is the command's description plus its shortcut.

```cpp
bool CommandButton(CommandID command, StyleRef style, LayoutOverride&& constraints = {});
```

```cpp
CommandButton(cmdUndo);   // member: CommandID cmdUndo = commandManager.RegisterCommand(...)
CommandButton(cmdRedo);
```

```cpp
// Once, in Initialize:
cmdUndo = commandManager.RegisterCommand(undoIcon, KeyCode::KeyZ, KeyModifier::Control,
                                         [this] { commandManager.History().Undo(); }, "Undo");
```

Class: `button`.

## Toggle

A button that stays pressed while on.

```cpp
bool Toggle(ID id, bool value, const GUIContent& content, std::function<void(bool)> onToggle, StyleRef style,
            LayoutOverride&& constraints = {});
bool Toggle(ID id, bool& value, const GUIContent& content, StyleRef style, LayoutOverride&& constraints = {});
```

- The `bool&` form flips `value` itself.
- The other form leaves `value` alone and calls `onToggle` with the new value.
- Returns `true` the frame after a click.
- Class: `toggle`.
  - In a toolbar: `toolbar-toggle`.
- On and off blend over 120 ms ([Changing state](../css/syntax.md#changing-state)).

```cpp
Toggle(bold, "B");
Toggle(mode == Mode::Row, "Row", [this](bool) { mode = Mode::Row; });
```

`Field(bool& value, std::string_view text)` is the same toggle:

```cpp
Field(wireframe, "Wireframe");   // member: bool wireframe
```

## Switch

A track and knob, with an optional label after it.

- The knob slides over 120 ms.
- The `bool&` form switches `value` itself.
- The other form calls `onToggle` with the new value.

```cpp
bool Switch(ID id, bool value, std::string_view text, std::function<void(bool)> onToggle, StyleRef style,
            LayoutOverride&& constraints = {});
bool Switch(ID id, bool& value, std::string_view text = {}, LayoutOverride&& constraints = {});
```

Classes: `switch`, `switch-track`, `switch-knob`, `switch-label`.

```cpp
Switch(enabled, enabled ? "On" : "Off");
```

```cpp
Switch(autosave, "Autosave", [this](bool on) { SetAutosave(on); });
```

## Checkbox

A box with a tick, then its label.

- `onToggle` gets the new value.
- The `bool&` form writes the new value itself.
- The box color blends over 120 ms.
- The tick appears at once.

```cpp
bool Checkbox(ID id, bool value, std::string_view text, std::function<void(bool)> onToggle,
              StyleRef style, LayoutOverride&& constraints = {});
bool Checkbox(ID id, bool& value, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
```

Classes: `checkbox`, `checkbox-box`, `checkbox-label`.

```cpp
Checkbox(gridCheckID, showGrid, "Show grid", [this](bool on) { showGrid = on; });
```

```cpp
Checkbox(showGrid, "Show grid");   // writes showGrid itself
```

---

[Previous: Text and images](text-and-images.md) | [Controls](../controls.md) | [Next: Fields](fields.md)
