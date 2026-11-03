# The application and the frame

## The application

Derive from `tachyon::GUIApplication` and implement `OnGUI`:

```cpp
#include <gui_controls/gui_application.h>

class MyApp : public tachyon::GUIApplication
{
public:
    tachyon::Result<void> Initialize(const InitializeOptions& options = {}) override
    {
        if (tachyon::Result<void> result = GUIApplication::Initialize(options); !result)
            return result;
        windowID = idManager.CreateStaticID();
        return {};
    }

    void OnGUI() override
    {
        if (BeginWindow(windowID, "My app", tachyon::LayoutOverride{}.Size(360, 160)))
        {
            Text("Hello");
            Button("Quit", [this] { applicationQuit = true; });
        }
        EndWindow();
    }

private:
    tachyon::ID windowID = tachyon::ID::Invalid();
};

int main()
{
    MyApp app;
    tachyon::Result<void> result = app.Initialize();
    if (result)
        result = app.Run();   // until the last window closes
    app.Cleanup();
    return result ? 0 : 1;
}
```

Shaders, default themes and fonts are built in; no files ship next to the program.
Optional, before `Initialize`: `SetResourceRoot` (your own themes and fonts),
`SetGraphicsBackend`, `SetWindowBackend`, `SetClipboard`, `SetStyleFiles`. `Cleanup`
releases everything `Initialize` created.


## The frame

Each frame runs these steps in order:

1. **Build.** The library calls `OnGUI`. Each control adds an element to the frame's
   tree; nothing is drawn yet. Control state (hovered, focused, pressed, checked) comes
   from the previous frame.
2. **Layout.** The tree is laid out from the CSS and each call's `LayoutOverride`. OS
   windows are created, moved and resized to match.
3. **Input.** Input since the last frame is hit-tested against the new layout. Event
   handlers and control callbacks run here, after `OnGUI` has returned.
4. **Render.** Each window is drawn.

So a click's callback runs in the input step of the frame that received it, and the next
frame's `OnGUI` sees the result.

Callbacks run after `OnGUI` returns, so anything they capture must outlive the frame.
Capture members, objects you own or copies, never references to `OnGUI` locals:

```cpp
// Fine: captures `this` and a copy of the index.
Button("Select", [this, i] { selected = i; });

// Wrong: `count` is a local of OnGUI and is gone when the callback runs.
int count = 0;
Button("Add", [&count] { ++count; });
```

### Run and the split frame calls

`Run()` loops until `applicationQuit` is set or the last window closes:

```cpp
while (app.UpdateFrame())
{
    if (!app.RecordFrame()) break;   // build, layout, input, render; submitted to the GPU
    // ... the host's own work ...
    app.PresentFrame();              // may wait for vsync
}
```

| Call | Does |
| --- | --- |
| `UpdateFrame` | Pumps OS events and input; false once the app quits |
| `RecordFrame` | Runs the frame and submits every window; fails only if the GPU device was lost and can't be recreated (possible with your own device) |
| `PresentFrame` | Presents the OS windows |

With your own main loop, call these yourself: see
[Integration](../integration/frame-loop.md).

---

[Overview](../overview.md) | [Next: IDs](ids.md)
