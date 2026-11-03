# 4. The app

## The app class

The app is a class derived from `tachyon::GUIApplication`. It owns the panels and keeps
track of the window and the dock layout. Add it at the end of `my_app.h`:

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

Then close the namespace with `}`.

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

In step 2 you didn't need any IDs, but static IDs are the exception. A window and a dock
layout remember their position, size and arrangement by ID, so they need an ID that
doesn't depend on where in the code they're called from.

`CreatePanel` registers a dockable panel with a title and a callback that draws it. Here
the callback just calls the panel's `Draw`.

The dock tree puts the counter in a 240 px column on the left and gives the greeting
the rest of the space. The user can drag the tabs around to rearrange them, or drag one
out of the window to float that panel in a window of its own.

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

`OnGUI` runs every frame. The docking container draws the visible panels, each through
its own callback, and `EmitFloatingDockWindows` draws any panels the user has floated.
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

If `Initialize` or `Run` fails, the `Result<void>` it returns tells you why. That could
happen, for example, on a GPU that's missing features the library needs.

## Run it

Build it the same way as in [step 1](setup.md). Then run `build\Debug\my_app.exe` on
Windows, or `build/Debug/my_app` on Linux and macOS. You can run it from any folder.

## Where next

- [Controls](../controls.md) has every control, with its options and its CSS class.
- [CSS and theming](../css.md) shows how to write your own style file and switch
  between dark and light.
- [Overview](../overview.md) goes deeper into IDs, Begin and End, and how a frame works.
- [Integration](../integration.md) covers using your own Vulkan device, window backend
  or main loop.

---

[Previous: 3. Fields](fields.md) | [Tutorial](../tutorial.md)
