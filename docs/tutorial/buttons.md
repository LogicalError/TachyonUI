# 2. Buttons

## Panels as classes

Each panel is a class:

- It holds the panel's values.
- Its `Draw` function builds the panel's controls.
- Controls are members of `tachyon::GUIApplication`, so `Draw` takes the app.

In `my_app.h`, after `#include <gui_controls/gui_application.h>`,
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

Close `Draw` with `}`.

- `Draw` runs every frame and describes the panel as it is now, so the text always shows
  the current count.
- No IDs are needed:
  - The library keeps a control's state between frames under its ID.
  - Each control here takes its ID from its call site, the file and line of the call.
  - The call site stays the same across frames, so the ID does too.
- `BeginHorizontal`/`EndHorizontal` put the buttons in a row.
  - Every `Begin*` needs its `End*`, whether or not the `Begin*` returned true.
- A button's callback runs after all controls are built.
  - So it can change the panel's values freely.
  - The next frame shows the result.
- `[this]` is safe because the app owns the panel (step 4).

---

[Previous: 1. Set up the project](setup.md) | [Tutorial](../tutorial.md) | [Next: 3. Fields](fields.md)
