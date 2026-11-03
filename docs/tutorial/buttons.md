# 2. Buttons

## Panels as classes

Each panel is a small class that holds the values it shows and has a `Draw` function
that builds its controls. Every control is a member function of
`tachyon::GUIApplication`, so `Draw` takes the app as a parameter. Put this in
`my_app.h`, after `#include <gui_controls/gui_application.h>`,
`#include <gui_controls/gui_dock_node.h>` and `#include <string>`:

```cpp
namespace my_app
{
```

<!-- snippet: examples/tutorial/my_app.h [panels] -->
```cpp
// A count, and buttons that change it.
class CounterPanel
{
public:
    void Draw(tachyon::GUIApplication& ui);

    int count = 0;
};

// A few fields, and a greeting made from them.
class GreetingPanel
{
public:
    void Draw(tachyon::GUIApplication& ui);

    std::string name    = "World";
    int         times   = 1;
    bool        excited = false;
};
```

In this step you'll draw the counter panel. The greeting panel comes in the next one.

## The counter panel

In `my_app.cpp`:

```cpp
#include "my_app.h"

namespace my_app
{
    void CounterPanel::Draw(tachyon::GUIApplication& ui)
    {
```

<!-- snippet: examples/tutorial/my_app.cpp [counter] -->
```cpp
ui.Text("Count: " + std::to_string(count));

ui.BeginHorizontal();
ui.Button("Add one", [this] { ++count; });
ui.Button("Reset",   [this] { count = 0; });
ui.EndHorizontal();
```

Then close `Draw` with `}`.

There are a few things to notice here. `Draw` runs every frame and describes the whole
panel as it should look right now, so the text always shows the current count.

You don't have to manage any IDs. Each control takes its ID from its call site (the file
and line of the call), and that stays the same from one frame to the next.

`BeginHorizontal` and `EndHorizontal` put the buttons in a row. Every `Begin*` needs its
`End*`, and you call it whether or not the `Begin*` returned true.

A button's callback runs when the button is clicked, after all the controls have been
built. That means it can change the panel's values freely, and you'll see the result in
the next frame.

The `[this]` capture refers to the panel. That's safe because the app owns the panel,
so the panel lives as long as the app does (you'll set that up in step 4).

---

[Previous: 1. Set up the project](setup.md) | [Tutorial](../tutorial.md) | [Next: 3. Fields](fields.md)
