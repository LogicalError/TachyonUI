# Menus

## BeginMenu / EndMenu

On its own: a dropdown button. Inside a menu: a submenu item. Inside a menu bar: a bar
trigger. Returns whether the menu is open; call `EndMenu` either way.

Opens with a 120 ms fade and 4px slide, closes with an 80 ms fade; no animation with
reduced motion (see [Changing state](../css/syntax.md#changing-state)). To pick one value
from a list, use [`Dropdown`](fields.md#dropdown).

```cpp
bool BeginMenu(ID id, std::string_view text, StyleRef buttonStyle, StyleRef menuStyle, StyleRef itemStyle,
               LayoutOverride&& constraints = {});
void EndMenu();
```

Classes: `menu-trigger` (`toolbar-menu-trigger` in a toolbar), `menu` (popup),
`menu-item-row` (items).

```cpp
if (BeginMenu(menuID, "Choose...", LayoutOverride{}.NoGrow().NoShrink()))
{
    for (const char* item : { "Apple", "Banana", "Cherry" })
        MenuItem(item, [this, item] { choice = item; });
}
EndMenu();
```

Wrap the current choice in `BeginSelected()` / `EndSelected()` to show a check mark.

## MenuItem

```cpp
bool MenuItem(ID id, const GUIContent& content, std::string_view shortcut, std::function<void()> onClick,
              StyleRef style, LayoutOverride&& constraints = {});
bool MenuItem(const GUIContent& content, std::function<void()> onClick = nullptr, LayoutOverride&& constraints = {});
bool MenuItem(const GUIContent& content, std::string_view shortcut, std::function<void()> onClick,
              LayoutOverride&& constraints = {});
bool MenuItem(CommandID command);
```

Returns `true` when picked; the menu then closes. Shows optional icon, label and optional
shortcut text. If any item has an icon or is selected, all items reserve the icon column.
The `CommandID` form takes icon, label, shortcut and callback from the command.

Classes: `menu-item-row`, `menu-item-icon`, `menu-item-label`, `menu-item-shortcut`,
`menu-item-arrow` (submenu arrow).

```cpp
MenuItem("Copy", "Ctrl+C", [this] { Copy(); });
```

## Other menu parts

```cpp
bool DestructiveMenuItem(ID id, const GUIContent& content, std::string_view shortcut,
                         std::function<void()> onClick, StyleRef style, LayoutOverride&& constraints = {});
void MenuSeparator();
void MenuHeader(std::string_view text);
void MenuFilter();
```

| Call | Effect |
| --- | --- |
| `DestructiveMenuItem` | Item in the error color, for Delete/Discard (`menu-item-label-destructive`, `menu-item-icon-destructive`) |
| `MenuSeparator` | Divider (`menu-separator`) |
| `MenuHeader` | Section name aligned with the labels (`menu-header`) |
| `MenuFilter` | Filter field; put it first. Takes the keyboard on open and hides items not containing its text |

## Menu bar and title bar

`BeginMenuBar`: a row of `BeginMenu` triggers. Left/Right move between them, Down opens.

`BeginTitleBar`: a menu bar that also drags the window. Empty space acts as the caption;
double-click maximizes. With `ElementFlags::WindowCustomTitleBar`, `EndTitleBar` adds the
minimize, maximize and close buttons.

```cpp
bool BeginMenuBar(ID id, StyleRef style, LayoutOverride&& constraints = {});
void EndMenuBar();
bool BeginTitleBar(ID id, StyleRef style, LayoutOverride&& constraints = {});
void EndTitleBar();
WindowHitArea TitleBarHitTest(ID windowID, int2 screenPos) const;
```

Classes: `menu-bar`, `title-bar`, `title-bar-button`, `title-bar-close-button`.

```cpp
BeginMenuBar();
if (BeginMenu("File"))
{
    MenuItem("Open", "Ctrl+O", [this] { Open(); });
    MenuSeparator();
    MenuItem("Quit", [this] { applicationQuit = true; });
}
EndMenu();
EndMenuBar();
```

## Context menus

Opens at the pointer on right-click of the target, or below the target on Shift+F10 or the
Menu key while it has focus.

- No target: attaches to the control just drawn (like `SetTooltip`), including all its
  parts.
- If several targets match (a control in a table row with its own menu), the first
  `BeginContextMenu` of the frame wins, so declare it right after the control.

```cpp
bool BeginContextMenu(ID targetID);
bool BeginContextMenu();
void EndContextMenu();
void OpenContextMenu(ID targetID, int2 screenPos);
```

`OpenContextMenu` opens the target's menu at a screen point, for things that aren't
elements; `BeginContextMenu(targetID)` then builds it. Popup class: `menu`.

```cpp
Button("Click me", [this] { ++clicks; });
if (BeginContextMenu())
    MenuItem("Reset counter", [this] { clicks = 0; });
EndContextMenu();
```

---

[Previous: Color and gradient fields](color-and-gradient.md) | [Controls](../controls.md) | [Next: Tooltips and message dialogs](tooltips-and-dialogs.md)
