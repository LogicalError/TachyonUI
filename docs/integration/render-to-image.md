# Rendering the UI into a host image

- `ElementFlags::WindowRenderToImage` makes a render-to-image window:
  - it gets no OS window
  - it is laid out and drawn into an image instead
- Use it for:
  - engine viewports
  - compositing the UI into your own scene
  - off-screen rendering, as the GPU tests do

```cpp
if (BeginWindow(windowID, "Panel", theme.windowStyleRef, tachyon::LayoutOverride{}.Size(400, 300),
                tachyon::ElementFlags::WindowRenderToImage))
{
    // controls as usual
}
EndWindow();
```

## Copying into the host's image

- When the GUI runs on your device (see [Using your own Vulkan device](vulkan-device.md)),
  each frame of the window can be copied into an image of yours:

```cpp
vk->SetWindowImageTarget(windowID, hostImage, VkExtent2D{ 400, 300 });
```

- You can set the target before the window exists.
- Passing `VK_NULL_HANDLE` stops the copies.
- The window's pixels are scaled to the extent you give.

```cpp
vk->SetWindowImageTarget(windowID, VK_NULL_HANDLE, VkExtent2D{});   // no more copies
```

Synchronization contract, on the queue passed to `SetExternalDevice`:

1. The image needs `TRANSFER_DST` usage. After your fragment-stage reads, the GUI's frame
   moves it from `SHADER_READ_ONLY_OPTIMAL` (`UNDEFINED` the first time) to
   `TRANSFER_DST_OPTIMAL`, copies the window in, and leaves it in
   `SHADER_READ_ONLY_OPTIMAL`, visible to the fragment stage.
2. Read it in work submitted after the GUI's frame (after `RecordFrame`), and leave it in
   `SHADER_READ_ONLY_OPTIMAL`.

```cpp
VkImageCreateInfo imageInfo{ .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;   // the GUI copies in, you sample
```

- Recording the GUI's draw commands into your own command buffer is not supported yet.

## Reading the pixels back

- `ReadWindowPixels` copies the window's last frame to memory.
  - The format is BGRA8, 4 bytes per pixel, with tightly packed rows.
  - It works on the GUI's own device too.
  - It waits for that frame to finish on the GPU.

```cpp
const tachyon::IntRect rect = app.windowManager->GetWindowRect(windowID);   // pixels
std::vector<uint8_t> pixels(size_t(rect.w) * rect.h * 4);
if (tachyon::Result<void> read = app.ReadWindowPixels(windowID, pixels); !read)
    std::fprintf(stderr, "%s\n", read.GetError().message.c_str());
```

- It returns `ErrorCode::NotFound` for an unknown window.
- It returns `ErrorCode::Unsupported` when:
  - the window is not a render-to-image window
  - no frame has been rendered yet
  - the span is too small

## Input for render-to-image windows

- No OS input reaches a render-to-image window. You inject it with the `Inject*` calls.
- Positions are in the window's pixels.

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

- The calls take an optional `KeyModifier` where it makes sense.

  ```cpp
  app.InjectMouseButton(windowID, pos, tachyon::MouseButtons::Left, true, tachyon::KeyModifier::Shift);
  ```

- After any mouse event, the window is under the pointer until `InjectMouseLeave`.
- After any injected input, it has keyboard focus until `InjectFocus(windowID, false)`.

---

[Previous: Your own loop](frame-loop.md) | [Integration](../integration.md) | [Next: Allocation hooks and headless backends](allocation-and-headless.md)
