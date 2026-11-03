# Rendering the UI into a host image

A window opened with `ElementFlags::WindowRenderToImage` doesn't get an OS window. It
lays out and draws like any other window, but into an image instead of onto the screen.
You can use this to put the UI inside an engine viewport, to composite it into your own
frame, or to render it without anything showing up on screen, which is what the GPU
tests do.

```cpp
if (BeginWindow(windowID, "Panel", theme.windowStyleRef, tachyon::LayoutOverride{}.Size(400, 300),
                tachyon::ElementFlags::WindowRenderToImage))
{
    // controls as usual
}
EndWindow();
```

## Copying into the host's image

If the GUI runs on your device (see
[Using your own Vulkan device](vulkan-device.md)), each frame of the window can be
copied into an image you own:

```cpp
vk->SetWindowImageTarget(windowID, hostImage, VkExtent2D{ 400, 300 });
```

You can set this before the window exists. Passing `VK_NULL_HANDLE` stops the copies.
The window's pixels are scaled to the extent you give.

The target image follows this synchronization contract, on the queue you passed to
`SetExternalDevice`:

1. The image needs `TRANSFER_DST` usage. After your fragment-stage reads, the GUI's
   frame moves it from `SHADER_READ_ONLY_OPTIMAL` (or `UNDEFINED` the first time) to
   `TRANSFER_DST_OPTIMAL` and copies the window into it. It then leaves the image in
   `SHADER_READ_ONLY_OPTIMAL`, visible to the fragment stage.
2. You read the image in work you submit after the GUI's frame (after `RecordFrame`),
   and leave it in `SHADER_READ_ONLY_OPTIMAL`.

Recording draw commands straight into your own command buffer isn't supported yet.

## Reading the pixels back

`ReadWindowPixels` copies the window's last frame into memory as BGRA8, with tightly
packed rows. It also works on the GUI's own device. It waits for that frame to finish on
the GPU before copying.

```cpp
const tachyon::IntRect rect = app.windowManager->GetWindowRect(windowID);   // pixels
std::vector<uint8_t> pixels(size_t(rect.w) * rect.h * 4);
if (tachyon::Result<void> read = app.ReadWindowPixels(windowID, pixels); !read)
    std::fprintf(stderr, "%s\n", read.GetError().message.c_str());
```

It fails with `ErrorCode::NotFound` if it doesn't know the window. It fails with
`ErrorCode::Unsupported` if the window isn't a render-to-image window, if it hasn't
rendered a frame yet, or if the span is too small.

## Input for render-to-image windows

The window doesn't get any input from the OS, so you pass it in yourself. Positions are
in the window's pixels.

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

Where it makes sense, each call also takes an optional `KeyModifier`. After any mouse
event the window counts as being under the pointer, until you call `InjectMouseLeave`.
After any injected input it has keyboard focus, until you call
`InjectFocus(windowID, false)`.

---

[Previous: Your own loop](frame-loop.md) | [Integration](../integration.md) | [Next: Allocation hooks and headless backends](allocation-and-headless.md)
