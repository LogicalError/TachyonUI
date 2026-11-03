# Begin and End

**Every `Begin*` needs its `End*`, called whether or not the `Begin*` returned true.** The
return value only says whether to build the content.

```cpp
if (BeginMenu(menuID, "File"))
{
    MenuItem("Open", [this] { OpenFile(); });
    MenuItem("Quit", [this] { applicationQuit = true; });
}
EndMenu();   // also when the menu is closed
```

`Begin*` returns false for a closed menu, a tooltip that isn't showing or a window the
user closed. Layout containers such as `BeginVertical` always return true.

## Scopes

Each pair has a scope object that calls `End*` when it goes out of scope. Same arguments
as the `Begin*`, converts to the same result:

```cpp
if (auto menu = Menu(menuID, "File"))
    MenuItem("Quit", [this] { applicationQuit = true; });

auto window = Window(windowID, "Controls", tachyon::LayoutOverride{}.Size(480, 560));
if (!window)
    return;   // the scope ends the window, open or not
```

The scope is named after the pair without `Begin`: `Window`, `Menu`, `MenuBar`,
`ContextMenu`, `Tooltip`, `Horizontal`, `Vertical`, `LabelGroup`, `Table`, `Row`,
`ListView`, `TabBar`, `Disabled`, and the rest in `gui_application.h`. `End()` on the
scope ends the container early; it is not ended again at scope exit.

## Misuse

The library logs an error (once per message) and carries on:

- `End*` without a matching `Begin*`: ignored.
- `Begin*` without its `End*`: closed for you at the end of the frame.
- A call in the wrong place, such as `MenuItem` outside an open menu: ignored.

---

[Previous: IDs](ids.md) | [Overview](../overview.md) | [Next: Return values, content and units](values.md)
