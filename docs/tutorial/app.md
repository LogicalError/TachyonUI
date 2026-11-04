# 4. The app

## The app class

The app class owns the panels, the window and the dock layout. The dock layout describes
how the panels are arranged in the window. At the end of `my_app.h`:

<!-- snippet: examples/tutorial/my_app.h [app] -->
```cpp
class MyApp : public tachyon::GUIApplication
{
public:
    tachyon::Result<void> Initialize(const InitializeOptions& options = {}) override;
    void OnGUI() override;

    CounterPanel  counter;
    GreetingPanel greeting;

private:
    tachyon::ID            m_windowID = tachyon::ID::Invalid();
    tachyon::ID            m_dockID   = tachyon::ID::Invalid();
    tachyon::DockingLayout m_layout;
};
```

Close the namespace with `}`.

## Setting up once

`Initialize` runs once, after the library has started. In `my_app.cpp`:

<!-- snippet: examples/tutorial/my_app.cpp [app-initialize] -->
```cpp
tachyon::Result<void> MyApp::Initialize(const InitializeOptions& options)
{
    if (tachyon::Result<void> result = GUIApplication::Initialize(options); !result)
        return result;

    // The window and the dock layout keep their state between frames, so they get
    // IDs that stay the same: static IDs, made once.
    m_windowID = idManager.CreateStaticID();
    m_dockID   = idManager.CreateStaticID();

    const tachyon::IconID noIcon = tachyon::IconID::Invalid;
    const tachyon::ID left  = CreatePanel(m_layout.panels, "Counter",  noIcon, [this] { counter.Draw(*this); });
    const tachyon::ID right = CreatePanel(m_layout.panels, "Greeting", noIcon, [this] { greeting.Draw(*this); });
    m_layout.root = tachyon::MakeSplit(tachyon::DockSplitMode::LeftPinned, 240,
        tachyon::MakeLeaf({ left }),
        tachyon::MakeLeaf({ right }));
    return {};
}
```

- The window and the dock layout remember their position, size and arrangement by ID.
  - So they need static IDs: IDs made once, which stay the same.
  - Call-site IDs, made from the file and line of a call, won't do here.
- `CreatePanel` registers a dockable panel with a title and a draw callback.
- The dock tree gives the counter a 240 px left column and the greeting the rest.
- Users can drag tabs to rearrange the panels, or drag a tab out of the window to float
  its panel.

## Every frame

<!-- snippet: examples/tutorial/my_app.cpp [app-ongui] -->
```cpp
void MyApp::OnGUI()
{
    if (BeginWindow(m_windowID, "My app", theme.mainWindowStyleRef,
                    tachyon::LayoutOverride::FixedSize(640, 360).Centered()))
    {
        BeginDockingContainer(m_dockID, &m_layout, tachyon::LayoutOverride::Auto());
        EndDockingContainer();
        EmitFloatingDockWindows();
    }
    EndWindow();
}
```

- The docking container draws the visible panels through their callbacks.
- `EmitFloatingDockWindows` draws the floated ones.

Close the namespace with `}`.

## main

In `main.cpp`, after `#include "my_app.h"` and `#include <cstdio>`:

<!-- snippet: examples/tutorial/main.cpp [main] -->
```cpp
int main()
{
    my_app::MyApp app;
    tachyon::Result<void> result = app.Initialize();
    if (result)
        result = app.Run();   // until the last window closes
    app.Cleanup();
    if (!result)
    {
        std::fprintf(stderr, "%s\n", result.GetError().message.c_str());
        return 1;
    }
    return 0;
}
```

On failure, the `Result<void>` says why. For example, the GPU may be missing required
features.

## Run it

Build as in [step 1](setup.md), then run it from any folder:

- `build\Debug\my_app.exe` on Windows
- `build/Debug/my_app` on Linux and macOS

## Where next

- [Controls](../controls.md): every control, its options and CSS class.
- [CSS and theming](../css.md): your own style file, dark and light.
- [Overview](../overview.md): IDs, Begin and End, the frame.
- [Integration](../integration.md): your own Vulkan device, window backend or main loop.

---

[Previous: 3. Fields](fields.md) | [Tutorial](../tutorial.md)
