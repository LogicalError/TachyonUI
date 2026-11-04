# Menus

A menu is a list of items that pops up when the user clicks the button that opens it,
called its trigger. The user picks an item, and the menu closes.

## BeginMenu / EndMenu

What `BeginMenu` draws depends on where you call it:

- On its own: a dropdown button. Clicking it opens the menu under it.
- Inside another menu: an item that opens a submenu, a second menu beside the first.
- Inside a menu bar: one of the bar's triggers, such as "File" or "Edit".

`BeginMenu` returns whether the menu is open. Add the items only then, and call `EndMenu`
either way.

```cpp
bool BeginMenu(ID id, std::string_view text, StyleRef buttonStyle, StyleRef menuStyle, StyleRef itemStyle,
               LayoutOverride&& constraints = {});
void EndMenu();
```

- `buttonStyle` is the trigger's style, `menuStyle` the popup's, `itemStyle` the items'.
- The CSS classes:
  - `menu-trigger` for the trigger
  - `toolbar-menu-trigger` for a trigger in a toolbar
  - `menu` for the popup
  - `menu-item-row` for the items

```cpp
if (BeginMenu(menuID, "Choose...", LayoutOverride{}.NoGrow().NoShrink()))
{
    for (const char* item : { "Apple", "Banana", "Cherry" })
        MenuItem(item, [this, item] { choice = item; });
}
EndMenu();
```

A menu inside a menu becomes a submenu:

```cpp
if (BeginMenu("View"))
{
    if (BeginMenu("Theme"))
    {
        MenuItem("Dark", [this] { themeIndex = 0; });
        MenuItem("Light", [this] { themeIndex = 1; });
    }
    EndMenu();
}
EndMenu();
```

The menu animates:

- It opens with a 120 ms fade and a 4px slide.
- It closes with an 80 ms fade.
- With reduced motion there is no animation (see
  [Changing state](../css/syntax.md#changing-state)).

To let the user pick one value from a list, use [`Dropdown`](fields.md#dropdown) instead.

Wrap the current choice in `BeginSelected()` / `EndSelected()` to show a check mark before
it:

```cpp
for (int i = 0; i < 2; i++)
{
    if (i == themeIndex) BeginSelected();
    MenuItem(i == 0 ? "Dark" : "Light", [this, i] { themeIndex = i; });
    if (i == themeIndex) EndSelected();
}
```

## MenuItem

One item in a menu.

```cpp
bool MenuItem(ID id, const GUIContent& content, std::string_view shortcut, std::function<void()> onClick,
              StyleRef style, LayoutOverride&& constraints = {});
bool MenuItem(const GUIContent& content, std::function<void()> onClick = nullptr, LayoutOverride&& constraints = {});
bool MenuItem(const GUIContent& content, std::string_view shortcut, std::function<void()> onClick,
              LayoutOverride&& constraints = {});
bool MenuItem(CommandID command);
```

- It returns `true` when the user picks it. The menu then closes.
- An item shows, from left to right:
  - an optional icon
  - its label
  - optional shortcut text, such as "Ctrl+C"
- If any item in the menu has an icon or is selected, all its items leave room for the
  icon, so the labels line up.
- `content` is a [`GUIContent`](../overview/values.md#guicontent): text, an icon, or both.
- The CSS classes:
  - `menu-item-row` for the item
  - `menu-item-icon`, `menu-item-label` and `menu-item-shortcut` for its parts
  - `menu-item-arrow` for the arrow of an item that opens a submenu

```cpp
MenuItem("Copy", "Ctrl+C", [this] { Copy(); });
```

The `CommandID` form takes the icon, label, shortcut and callback from a command you
registered with `commandManager`:

```cpp
MenuItem(saveCommand);   // member: CommandID saveCommand, from commandManager.RegisterCommand
```

## Other menu parts

```cpp
bool DestructiveMenuItem(ID id, const GUIContent& content, std::string_view shortcut,
                         std::function<void()> onClick, StyleRef style, LayoutOverride&& constraints = {});
void MenuSeparator();
void MenuHeader(std::string_view text);
void MenuFilter();
```

```cpp
if (BeginMenu("Edit"))
{
    MenuFilter();
    MenuHeader("Clipboard");
    MenuItem("Copy", "Ctrl+C", [this] { Copy(); });
    MenuSeparator();
    DestructiveMenuItem({ theme.deleteIcon, "Delete" }, "Del", [this] { DeleteSelection(); });
}
EndMenu();
```

| Call | Effect |
| --- | --- |
| `DestructiveMenuItem` | Item in the error color, for Delete/Discard (`menu-item-label-destructive`, `menu-item-icon-destructive`) |
| `MenuSeparator` | Divider (`menu-separator`) |
| `MenuHeader` | Section name aligned with the labels (`menu-header`) |
| `MenuFilter` | Filter field; put it first. Takes the keyboard on open and hides items not containing its text |

## Menu bar and title bar

A menu bar is the row of menus along the top of a window: "File", "Edit", "View".

- `BeginMenuBar` starts the row. Put a `BeginMenu` inside it for each menu.
- Left and Right move between the menus.
- Down opens a menu.

A title bar is the strip at the top of a window that the user drags to move the window.
`BeginTitleBar` makes a menu bar that is also the window's title bar:

- Its empty space acts as the caption: dragging it moves the window.
- Double-clicking the empty space maximizes the window.
- In a window with `ElementFlags::WindowCustomTitleBar`, `EndTitleBar` adds the minimize,
  maximize and close buttons.

```cpp
bool BeginMenuBar(ID id, StyleRef style, LayoutOverride&& constraints = {});
void EndMenuBar();
bool BeginTitleBar(ID id, StyleRef style, LayoutOverride&& constraints = {});
void EndTitleBar();
WindowHitArea TitleBarHitTest(ID windowID, int2 screenPos) const;
```

- `TitleBarHitTest` tells the OS what a point of a window with a custom title bar is: the
  caption, the maximize button, or content the GUI handles.
- The CSS classes:
  - `menu-bar`
  - `title-bar`
  - `title-bar-button`
  - `title-bar-close-button`

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

A window that draws its own title bar, with the window buttons on the right:

```cpp
if (BeginWindow(windowID, "Editor", theme.mainWindowStyleRef, LayoutOverride::FixedSize(900, 600),
                ElementFlags::WindowUserControlled | ElementFlags::WindowCustomTitleBar))
{
    BeginTitleBar();
    if (BeginMenu("File"))
        MenuItem("Quit", [this] { applicationQuit = true; });
    EndMenu();
    EndTitleBar();
}
EndWindow();
```

## Context menus

A context menu is a menu that opens over something the user right-clicks. The thing it
belongs to is its target. It opens:

- at the pointer, when the user right-clicks the target
- below the target, on Shift+F10 or the Menu key while the target has the keyboard focus

Which target a context menu belongs to:

- With no target given, it belongs to the control just drawn, like `SetTooltip`. That
  includes all the control's parts.
- If several targets match, the first `BeginContextMenu` of the frame wins. For example,
  a control in a table row, where the row has its own menu too. So declare a control's
  menu right after the control.

```cpp
bool BeginContextMenu(ID targetID);
bool BeginContextMenu();
void EndContextMenu();
void OpenContextMenu(ID targetID, int2 screenPos);
```

- `BeginContextMenu` returns whether the menu is open. Add the items only then, and call
  `EndContextMenu` either way.
- The popup's CSS class is `menu`.

```cpp
Button("Click me", [this] { ++clicks; });
if (BeginContextMenu())
    MenuItem("Reset counter", [this] { clicks = 0; });
EndContextMenu();
```

`OpenContextMenu` opens a target's menu at a point on the screen, for things that aren't
elements, such as an empty spot of a canvas you draw yourself. `BeginContextMenu(targetID)`
then builds the menu:

```cpp
if (canvasRightClicked)   // members: bool canvasRightClicked; int2 rightClickPos (screen pixels)
{
    OpenContextMenu(canvasID, rightClickPos);
    canvasRightClicked = false;
}
if (BeginContextMenu(canvasID))
    MenuItem("Add node", [this] { AddNode(); });
EndContextMenu();
```

---

[Previous: Color and gradient fields](color-and-gradient.md) | [Controls](../controls.md) | [Next: Tooltips and message dialogs](tooltips-and-dialogs.md)
