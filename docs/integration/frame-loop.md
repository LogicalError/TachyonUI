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
