# Begin and End

Containers come as pairs of calls: `BeginWindow` and `EndWindow`, `BeginMenu` and
`EndMenu`, `BeginVertical` and `EndVertical`, and so on. They all follow one rule:

**Every `Begin*` needs its `End*`, and you call it whether or not the `Begin*` returned
true.** The return value only tells you whether to build the content.

```cpp
if (BeginMenu(menuID, "File"))
{
    MenuItem("Open", [this] { OpenFile(); });
    MenuItem("Quit", [this] { applicationQuit = true; });
}
EndMenu();   // also when the menu is closed
```

You'll get false for a closed menu, a tooltip that isn't showing, or a window the user
closed. Layout containers such as `BeginVertical` always return true.

## Scopes

Every pair also has a scope object that calls the `End*` for you when it goes out of
scope. It takes the same arguments as the `Begin*`, and converts to the same result:

```cpp
if (auto menu = Menu(menuID, "File"))
    MenuItem("Quit", [this] { applicationQuit = true; });

auto window = Window(windowID, "Controls", tachyon::LayoutOverride{}.Size(480, 560));
if (!window)
    return;   // the scope ends the window, open or not
```

A scope has the name of its pair without the `Begin`: `Window`, `Menu`, `MenuBar`,
`ContextMenu`, `Tooltip`, `Horizontal`, `Vertical`, `LabelGroup`, `Table`, `Row`,
`ListView`, `TabBar`, `Disabled`, and the rest you'll find in `gui_application.h`. If
you want to end the container early, call `End()` on the scope. It won't end it a second
time when it goes out of scope.

## Misuse

Getting a pair wrong won't crash your program. The library logs an error and carries on:

- An `End*` without a matching `Begin*` is reported and ignored.
- A `Begin*` that never gets its `End*` is reported at the end of the frame and closed
  for you, so the next frame starts clean.
- A call in the wrong place, such as a `MenuItem` outside an open menu, is reported and
  ignored.

Since the same mistake usually happens every frame, each message is only logged once.

---

[Previous: IDs](ids.md) | [Overview](../overview.md) | [Next: Return values, content and units](values.md)
