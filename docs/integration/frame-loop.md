# Your own loop

`Run()` loops over three calls. From your own main loop, call them once per frame, in
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

After `RecordFrame` returns, render-to-image windows are drawn and readable; your work
before `PresentFrame` can use them (tested by `AHostLoopDrivesTheFrameCalls` in
`tests/gpu/gpu_application_init_test.cpp`).

The app quits when its last main window closes or you set `app.applicationQuit = true`;
the next `UpdateFrame` returns false. `Run` clears `applicationQuit` on start. Call
`Cleanup` after the loop.

## Errors and device loss

Fallible calls return `Result<T>`: true on success, else `GetError()` gives an `Error`
with `code` (`ErrorCode`) and `message`. Returned by `Initialize`, `Run`, `RecordFrame`,
`ReadWindowPixels`.

On device loss, `RecordFrame` (and `Run`) handles it before the next frame:

- GUI-created device: a new device and window contexts are created. `CreateImage` images
  keep their `ImageIndex` and are re-uploaded from their source pixels. The app continues.
- Your device: `RecordFrame` returns `ErrorCode::DeviceLost`. Create a new device and a
  new app.

`VulkanGraphicsContext::SimulateDeviceLoss()` makes the next frame act as if the device
was lost (for testing).

---

[Previous: Your own window and input](window-and-input.md) | [Integration](../integration.md) | [Next: Rendering the UI into a host image](render-to-image.md)
