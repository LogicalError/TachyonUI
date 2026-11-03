# The application and the frame

## The application

Your application is a class derived from `tachyon::GUIApplication` that implements
`OnGUI`:

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

You don't need to ship any files next to the program. The shaders, the default themes
and the fonts are built into the library. If you want to change the defaults, you can
call `SetResourceRoot` (for your own themes and fonts), `SetGraphicsBackend`,
`SetWindowBackend`, `SetClipboard` and `SetStyleFiles` before `Initialize`. They're all
optional. `Cleanup` releases everything `Initialize` created.


## The frame

Every frame goes through four steps, always in this order:

1. **Build.** The library calls your `OnGUI`. Each control you call adds an element to
   this frame's tree, but nothing is drawn yet. A control reads its state (hovered,
   focused, pressed, checked) from what the library remembered from the last frame.
2. **Layout.** The tree is laid out using the styles from the CSS files and the
   `LayoutOverride` you passed to each call. OS windows are created, moved and resized
   to match their window elements.
3. **Input.** Any input that arrived since the last frame is hit-tested against the new
   layout and sent to the elements. This is where event handlers and control callbacks
   run, after `OnGUI` has already returned.
4. **Render.** The tree is drawn into each window.

This means a click shows up in two places. The button's callback runs during the input
step of the frame that received the click, and the UI you build in the next frame sees
the result.

Since callbacks run after `OnGUI` returns, anything they capture has to outlive the
frame. Capturing members of your application, objects you own, or copies is fine.
Capturing references to locals of `OnGUI` is not.

```cpp
// Fine: captures `this` and a copy of the index.
Button("Select", [this, i] { selected = i; });

// Wrong: `count` is a local of OnGUI and is gone when the callback runs.
int count = 0;
Button("Add", [&count] { ++count; });
```

### Run and the split frame calls

`Run()` keeps going until `applicationQuit` is set or the last window closes. The loop
inside it looks like this:

```cpp
while (app.UpdateFrame())
{
    if (!app.RecordFrame()) break;   // build, layout, input, render; submitted to the GPU
    // ... the host's own work ...
    app.PresentFrame();              // may wait for vsync
}
```

`UpdateFrame` pumps OS events and input, and returns false once the app quits.
`RecordFrame` runs the frame described above and submits every window. It only returns
an error when the GPU device was lost and can't be recreated, which can happen when you
supply your own device. `PresentFrame` presents the OS windows. If you have your own
main loop, call these three yourself. [Integration](../integration/frame-loop.md) shows
how.

---

[Overview](../overview.md) | [Next: IDs](ids.md)
