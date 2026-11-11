# Your own loop

- `Run()` is a loop over three frame calls.
- If your program has its own main loop, call them yourself instead, once per frame and in
  this order:

```cpp
while (app.UpdateFrame())
{
    if (!app.RecordFrame()) break;   // render-to-image windows are drawn now
    // ... the host's own work ...
    app.PresentFrame();              // may wait for vsync
}
```

| Call | Does |
| --- | --- |
| `bool UpdateFrame()` | Starts the frame, pumps OS events and input. False once the app quits. |
| `Result<void> RecordFrame()` | Recovers a lost device if needed. Builds the UI (`OnGUI`, layout, input dispatch, draw recording), uploads what the build asked for (glyphs, styles, new or resized windows), records and submits every window. Errors if a lost device can't be recreated. |
| `void PresentFrame()` | Presents OS windows; may wait for vsync. Nothing needed for render-to-image windows. |

- Render-to-image windows are windows drawn into an image instead of an OS window (see
  [Rendering the UI into a host image](render-to-image.md)).
  - When `RecordFrame` returns, they are drawn and can be read.
  - Your own work before `PresentFrame` can use them.
  - Tested by `AHostLoopDrivesTheFrameCalls` in `tests/gpu/gpu_application_init_test.cpp`.
- The app quits when:
  - its last main window closes
  - or you set `app.applicationQuit = true`
- After that, the next `UpdateFrame` returns false.
- `Run` sets `applicationQuit` back to false when it starts.
- Call `Cleanup` after the loop.

## Drawing only when something changes

By default `Run()` doesn't draw frames while nothing changes: it sleeps, and the CPU and GPU
rest.

- A frame is drawn when:
  - there's input: the mouse moves, a key is pressed, a window is resized or uncovered
  - something animates: a hover fading in, a spinner turning, a menu sliding open
  - the last frame looked different from the one before it
  - a timer is due, such as a tooltip's delay or a toast hiding itself
  - your app asks for one (see below)
- Only the windows that look different are drawn again; the others keep what they show.

### The modes

`SetRedrawMode` picks how often frames are drawn. Switch it any time.

| Mode | Does |
| --- | --- |
| `RedrawMode::WhenNeeded` (the default) | Sleeps until a frame is needed, as above |
| `RedrawMode::WhenChanged` | Builds a frame at the display's refresh rate, but draws only the windows that changed |
| `RedrawMode::Always` | Draws every frame, as fast as the display refreshes, like a game |

- For example, an editor with a live 3D view that always moves:

  ```cpp
  app.SetRedrawMode(tachyon::RedrawMode::Always);
  ```

### When your app changes something by itself

The library sees input and its own animations. It can't see a change that comes from your
app alone, so ask for a frame:

- `RequestRedraw()`: a frame as soon as possible.
  - For example, new data arrived from a download:

    ```cpp
    onDownloaded = [this] { RequestRedraw(); };   // safe from any thread
    ```
- `RequestRedrawIn(seconds)`: a frame later.
  - For example, a clock that shows minutes:

    ```cpp
    Text(id, CurrentTime());
    RequestRedrawIn(SecondsToNextMinute());
    ```
- `ElementFlags::Animated` on an element: it moves every frame, so frames keep coming while
  it's on screen.
  - For example, a live view of a running process: flag its element, or call
    `RequestRedraw()` each frame while it should move.
- Both requests are safe from any thread and wake a sleeping `Run()`.
- Without a request, a change made from another thread shows only at the next input.

### In your own loop

`UpdateFrame` never sleeps in your loop. Ask whether a frame is needed, and skip it, or sleep,
when it isn't:

```cpp
while (app.UpdateFrame())
{
    if (app.NeedsFrame())
    {
        if (!app.RecordFrame()) break;
        app.PresentFrame();
    }
    else
        WaitForYourEvents(app.SecondsUntilFrame());   // INFINITY: until input or a request
}
```

- `NeedsFrame()` is always true outside `RedrawMode::WhenNeeded`.
- `SecondsUntilFrame()` is 0 when a frame is needed now.

## Errors and device loss

- Calls that can fail return `Result<T>`:
  - it converts to true on success
  - else `GetError()` gives an `Error`, with a `code` (an `ErrorCode`) and a `message`
- `Initialize`, `Run`, `RecordFrame` and `ReadWindowPixels` return one.

```cpp
if (tachyon::Result<void> result = app.RecordFrame(); !result)
{
    if (result.GetError().code == tachyon::ErrorCode::DeviceLost)
        std::fprintf(stderr, "device lost: %s\n", result.GetError().message.c_str());
}
```

- When the device is lost, `RecordFrame`, and so `Run`, handles it before the next frame:
  - with a device the GUI created, it creates a new device and new window contexts
    - images made with `CreateImage` keep their `ImageIndex` and are uploaded again from
      their source pixels
    - the app continues
  - with your own device, `RecordFrame` returns `ErrorCode::DeviceLost`
    - create a new device and a new app
- `VulkanGraphicsContext::SimulateDeviceLoss()` makes the next frame act as if the device
  was lost, for testing:

```cpp
vk->SimulateDeviceLoss();   // vk: the app's VulkanGraphicsContext
```

---

[Previous: Your own window and input](window-and-input.md) | [Integration](../integration.md) | [Next: Rendering the UI into a host image](render-to-image.md)
