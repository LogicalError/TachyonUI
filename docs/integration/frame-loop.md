# Your own loop

`Run()` calls three functions in a loop until the app quits. If your program has its own
main loop, you call them yourself, once per frame, in this order:

```cpp
while (app.UpdateFrame())
{
    if (!app.RecordFrame()) break;   // render-to-image windows are drawn now
    // ... the host's own work ...
    app.PresentFrame();              // may wait for vsync
}
```

| Call | What it does |
| --- | --- |
| `bool UpdateFrame()` | Starts the frame and pumps OS events and input. Returns false once the app quits. |
| `Result<void> RecordFrame()` | Recovers a lost device if it needs to. Then it builds the UI (`OnGUI`, layout, input dispatch and draw recording), uploads what the build asked for (glyphs, styles, new or resized windows), and records and submits every window. Returns an error when a lost device can't be recreated. |
| `void PresentFrame()` | Presents the OS windows, and may wait for vsync. Render-to-image windows don't need anything here. |

Once `RecordFrame` returns, render-to-image windows have been drawn and you can read
them back. Your own work between `RecordFrame` and `PresentFrame` can use them.
`tests/gpu/gpu_application_init_test.cpp` (`AHostLoopDrivesTheFrameCalls`) checks this.

The app quits when its last main window closes, or when you set
`app.applicationQuit = true`. The next `UpdateFrame` then returns false. `Run` clears
`applicationQuit` when it starts. Call `Cleanup` after the loop.

## Errors and device loss

Calls that can fail return a `Result<T>`. It's true on success. Otherwise `GetError()`
gives you an `Error` with a `code` (an `ErrorCode`) and a `message`. `Initialize`,
`Run`, `RecordFrame` and `ReadWindowPixels` all return one.

If the GPU device is lost, `RecordFrame` (and so `Run`) deals with it before the next
frame. What happens depends on who owns the device:

- If the GUI created the device, it creates a new device and new window contexts.
  Images made with `CreateImage` keep their `ImageIndex` and are uploaded again from the
  pixels they were made from. The app carries on with the next frame.
- If the device is yours, the GUI can't create a new one, so `RecordFrame` returns an
  error with `ErrorCode::DeviceLost`. You then create a new device and a new app.

For testing, `VulkanGraphicsContext::SimulateDeviceLoss()` makes the next frame behave
as if the device had been lost.

---

[Previous: Your own window and input](window-and-input.md) | [Integration](../integration.md) | [Next: Rendering the UI into a host image](render-to-image.md)
