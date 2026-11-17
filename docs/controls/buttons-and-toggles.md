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

### Icon buttons

A button with only an icon is a square a row never stretches. The theme has a class per size:

- `icon-button-20`, `icon-button-24` (the usual size), `icon-button-28`, `icon-button-32`
  - the icon is 14px, and 16px from 28
  - hover and pressed lay the text color over the whole square, at 8% and 14%
  - focused: a 1px accent border
  - with `Toggle`, on is the soft accent fill with the icon in the accent
- `icon-button-primary`: the main action of a group (Play, Run), filled with the accent; 24px square
- `icon-button-danger`: an action that removes something; the icon in the error color; 24px square
- `icon-button-group`: a well round icon buttons that belong together (Bold, Italic, Link)

Look a class up once with `theme.GetStyle`, after each theme load.

```cpp
const StyleRef iconButton = theme.GetStyle("icon-button-24");
Button(GUIContent{ deleteIcon, {}, "Delete" }, [this] { Delete(); }, theme.GetStyle("icon-button-danger"));

BeginHorizontal(theme.GetStyle("icon-button-group"));
Toggle(bold,   GUIContent{ boldIcon,   {}, "Bold" },   iconButton);
Toggle(italic, GUIContent{ italicIcon, {}, "Italic" }, iconButton);
EndHorizontal();
```

- Give each icon button a tooltip: it has no text to say what it does.
- To make a primary or danger button another size, pass a fixed size: `LayoutOverride::FixedSize(28, 28)`.

## BusyButton

A button that can show it's at work, for example while a save runs.

- While `busy` is `false` it's an ordinary [Button](#button).
- While `busy` is `true`:
  - a turning spinner takes the place of its icon
  - `busyText` takes the place of its text ("Saving..."); an empty `busyText` keeps the
    text
  - it stays exactly as wide as it was, so the row around it doesn't shift
  - it still hovers and takes focus, but clicks do nothing
  - it returns `false`

```cpp
bool BusyButton(ID id, const GUIContent& content, bool busy, std::string_view busyText,
                std::function<void()> onClick, StyleRef style, LayoutOverride&& constraints = {});
```

You decide when it's busy, so pass a flag your own work sets and clears:

```cpp
// member: bool saving
BusyButton(saveID, { saveIcon, "Save" }, saving, "Saving...", [this] { saving = true; StartSave(); });
```

```cpp
// When the save finishes, somewhere else in your code:
saving = false;
```

Class: `button`, the same as Button.

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

## ShortcutButton

A command's shortcut as keycaps, one small chip per key: `Ctrl` `C`.

- A chord's two steps are apart by a comma: `Ctrl` `K` , `Ctrl` `S`.
- A command without a shortcut reads "Unassigned", muted; pass other text to say it another
  way.
- It's a flat button: a click anywhere on it runs `onClick`.
  - For example, to rebind the shortcut with `commandManager.BeginRebind`.

```cpp
bool ShortcutButton(ID id, const CommandManager::Command& command, std::function<void()> onClick,
                    std::string_view unassigned = "Unassigned", LayoutOverride&& constraints = {});
```

```cpp
// In a shortcuts table's cell: the keys, or a prompt while they're being rebound.
if (commandManager.RebindTarget() == commandID)
    Button(buttonID, "Press a key... (Esc to cancel)", [this] { commandManager.CancelRebind(); });
else
    ShortcutButton(buttonID, *commandManager.GetCommand(commandID),
                   [this] { commandManager.BeginRebind(commandID); });
```

Its parts come from `CommandManager`:

- `CommandManager::ShortcutKeys(key, modifiers, out)` lists a shortcut's key names, one per
  key: "Ctrl", "Shift", "D". A key whose name has a space ("Num +") is one name.
- `commandManager.ResetShortcuts()` gives every command the shortcut it was registered with
  back: a "Reset to defaults" button.

Classes: `shortcut-button`, `keycap`, and `shortcut-muted` for "Unassigned" and the comma.

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
