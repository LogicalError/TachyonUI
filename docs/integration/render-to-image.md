# Rendering the UI into a host image

`ElementFlags::WindowRenderToImage` gives a window no OS window: it lays out and draws
into an image. Use it for engine viewports, compositing, or off-screen rendering (as the
GPU tests do).

```cpp
if (BeginWindow(windowID, "Panel", theme.windowStyleRef, tachyon::LayoutOverride{}.Size(400, 300),
                tachyon::ElementFlags::WindowRenderToImage))
{
    // controls as usual
}
EndWindow();
```

## Copying into the host's image

When the GUI runs on your device ([Using your own Vulkan device](vulkan-device.md)), each
frame can be copied into your image:

```cpp
vk->SetWindowImageTarget(windowID, hostImage, VkExtent2D{ 400, 300 });
```

Can be set before the window exists. `VK_NULL_HANDLE` stops the copies. Pixels are scaled
to the given extent.

Synchronization contract, on the queue passed to `SetExternalDevice`:

1. The image needs `TRANSFER_DST` usage. After your fragment-stage reads, the GUI's frame
   moves it from `SHADER_READ_ONLY_OPTIMAL` (`UNDEFINED` the first time) to
   `TRANSFER_DST_OPTIMAL`, copies the window in, and leaves it in
   `SHADER_READ_ONLY_OPTIMAL`, visible to the fragment stage.
2. Read it in work submitted after the GUI's frame (after `RecordFrame`), and leave it in
   `SHADER_READ_ONLY_OPTIMAL`.

Recording draw commands into your own command buffer is not supported yet.

## Reading the pixels back

`ReadWindowPixels` copies the window's last frame to memory as tightly packed BGRA8. Works
on the GUI's own device too. Waits for that frame to finish on the GPU.

```cpp
const tachyon::IntRect rect = app.windowManager->GetWindowRect(windowID);   // pixels
std::vector<uint8_t> pixels(size_t(rect.w) * rect.h * 4);
if (tachyon::Result<void> read = app.ReadWindowPixels(windowID, pixels); !read)
    std::fprintf(stderr, "%s\n", read.GetError().message.c_str());
```

Errors: `ErrorCode::NotFound` for an unknown window; `ErrorCode::Unsupported` if not a
render-to-image window, no frame rendered yet, or the span is too small.

## Input for render-to-image windows

No OS input arrives; inject it. Positions are in the window's pixels.

```cpp
app.InjectMouseMove(windowID, pos);
app.InjectMouseButton(windowID, pos, tachyon::MouseButtons::Left, true);    // down
app.InjectMouseButton(windowID, pos, tachyon::MouseButtons::Left, false);   // up
app.InjectMouseScroll(windowID, pos, tachyon::float2{ 0.0f, 1.0f });
app.InjectMouseLeave(windowID);
app.InjectKey(windowID, key, tachyon::KeyAction::KeyDown);
app.InjectText(windowID, "text");   // UTF-8
app.InjectFocus(windowID, false);
```

- Calls take an optional `KeyModifier` where it makes sense.
- After any mouse event the window is under the pointer until `InjectMouseLeave`.
- After any injected input it has keyboard focus until `InjectFocus(windowID, false)`.

---

[Previous: Your own loop](frame-loop.md) | [Integration](../integration.md) | [Next: Allocation hooks and headless backends](allocation-and-headless.md)
