# Menus

## BeginMenu / EndMenu

`BeginMenu` does different things depending on where you call it. On its own it's a
dropdown button that opens a menu. Inside another menu it becomes a submenu item, and
inside a menu bar it's one of the bar's triggers. It returns whether the menu is open, and
you call `EndMenu` whether it is or not.

A menu fades in over 120 ms while sliding 4px out from whatever opened it, and fades out
over 80 ms. With reduced motion turned on there's no animation (see
[Changing state](../css/syntax.md#changing-state)). If you want the user to pick one value
from a list, [`Dropdown`](fields.md#dropdown) is a better fit.

```cpp
bool BeginMenu(ID id, std::string_view text, StyleRef buttonStyle, StyleRef menuStyle, StyleRef itemStyle,
               LayoutOverride&& constraints = {});
void EndMenu();
```

The button uses the `menu-trigger` class (`toolbar-menu-trigger` in a toolbar), the popup
uses `menu`, and its items use `menu-item-row`.

```cpp
if (BeginMenu(menuID, "Choose...", LayoutOverride{}.NoGrow().NoShrink()))
{
    for (const char* item : { "Apple", "Banana", "Cherry" })
        MenuItem(item, [this, item] { choice = item; });
}
EndMenu();
```

To show the current choice with a check mark, wrap its item in `BeginSelected()` /
`EndSelected()`.

## MenuItem

```cpp
bool MenuItem(ID id, const GUIContent& content, std::string_view shortcut, std::function<void()> onClick,
              StyleRef style, LayoutOverride&& constraints = {});
bool MenuItem(const GUIContent& content, std::function<void()> onClick = nullptr, LayoutOverride&& constraints = {});
bool MenuItem(const GUIContent& content, std::string_view shortcut, std::function<void()> onClick,
              LayoutOverride&& constraints = {});
bool MenuItem(CommandID command);
```

A menu item shows an optional icon, its label and an optional shortcut text. If any item
in the menu has an icon or is selected, every item gets room for the icon column so the
labels line up. `MenuItem` returns `true` when the user picks it, and the menu closes.

The `CommandID` form takes the icon, label, shortcut and callback from the command, so you
don't have to repeat them. The parts are styled with `menu-item-row`, `menu-item-icon`,
`menu-item-label`, `menu-item-shortcut` and `menu-item-arrow` (the arrow on a submenu).

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

- `DestructiveMenuItem` is for items that destroy something, like Delete or Discard. It's
  drawn in the error color (`menu-item-label-destructive`, `menu-item-icon-destructive`).
- `MenuSeparator` draws a divider (`menu-separator`).
- `MenuHeader` puts a section name between items, lined up with the labels
  (`menu-header`).
- `MenuFilter` adds a filter field to a big menu. Put it first. It takes the keyboard when
  the menu opens, and while it holds text, items that don't contain that text are hidden.

## Menu bar and title bar

`BeginMenuBar` gives you a row of `BeginMenu` triggers. The user can move between them
with Left and Right, and Down opens the dropdown.

`BeginTitleBar` is a menu bar that also drags the window. Any part of it that isn't a
control acts as the caption, and double-clicking it maximizes the window. If the window
has `ElementFlags::WindowCustomTitleBar`, `EndTitleBar` adds the minimize, maximize and
close buttons for you.

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

A context menu opens at the pointer when the user right-clicks the target element. It
also opens below the target on Shift+F10 or the Menu key while the target has focus.

If you don't pass a target, the menu attaches to the control you just drew, the same way
`SetTooltip` does. For a control made of several parts, right-clicking any of them opens
it. Sometimes more than one target matches, for example a control inside a table row that
has its own menu. In that case the first `BeginContextMenu` of the frame wins, which is the
control's, since you declare it right after the control.

```cpp
bool BeginContextMenu(ID targetID);
bool BeginContextMenu();
void EndContextMenu();
void OpenContextMenu(ID targetID, int2 screenPos);
```

Use `OpenContextMenu` when the thing being clicked isn't an element. It opens the target's
menu at a screen point, and `BeginContextMenu(targetID)` then builds it. The popup uses the
`menu` class.

```cpp
Button("Click me", [this] { ++clicks; });
if (BeginContextMenu())
    MenuItem("Reset counter", [this] { clicks = 0; });
EndContextMenu();
```

---

[Previous: Color and gradient fields](color-and-gradient.md) | [Controls](../controls.md) | [Next: Tooltips and message dialogs](tooltips-and-dialogs.md)
