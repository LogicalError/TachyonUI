# Using your own Direct3D 12 device

- With the Direct3D 12 backend (`TACHYON_D3D12`), the GUI creates its own `ID3D12Device` and
  queue by default, and owns them.
- You can hand it your own instead, for example when the GUI is the editor of a game engine
  that already renders with Direct3D 12.
- The tests that do what a host does: `tests/gpu/d3d12_external_device_test.cpp`.
- For Vulkan, see [Using your own Vulkan device](vulkan-device.md).

## Choosing the backend

- An app picks Direct3D 12 in the options it passes to `Initialize`:

```cpp
InitializeOptions options;
options.graphicsAPI = tachyon::GraphicsAPI::D3D12;
app.Initialize(options);
```

- Choosing the GPU works as on Vulkan: `options.gpu`, or the `TACHYON_GPU` environment variable.
  - `"cpu"` is WARP, Windows' software renderer.
  - See [Choosing the GPU](vulkan-device.md#choosing-the-gpu).
- With your own device (below), you choose the GPU when you create it.

## Device requirements

- The device runs Shader Model 6.6 with resource binding tier 3: the GUI's shaders index the
  descriptor heap directly.
  - Windows 11's Direct3D 12 runtime has both, on current GPUs and on WARP.
- `D3D12GraphicsContext::CheckDeviceSupport(device, &problems)` checks a device. It lists what
  the device lacks, one line each:

```cpp
#include <gui_core/graphics/d3d12/d3d12_graphics_context.h>

std::string problems;
if (!tachyon::d3d12::D3D12GraphicsContext::CheckDeviceSupport(device, &problems))
    std::fprintf(stderr, "%s", problems.c_str());
```

- The queue is a direct (graphics) queue of that device.

## Handing over the device

- Set your device and queue on a `D3D12GraphicsContext` with `SetExternalDevice`.
- Give the backend to the app with `SetGraphicsBackend`.
- Do both before `Initialize`.

```cpp
auto backend = std::make_unique<tachyon::d3d12::D3D12GraphicsContext>();
backend->SetExternalDevice({ hostDevice, hostQueue });

auto* d3d12 = backend.get();   // for RegisterExternalImage and SetWindowImageTarget
app.SetGraphicsBackend(std::move(backend));
if (tachyon::Result<void> result = app.Initialize(); !result)
    std::fprintf(stderr, "%s\n", result.GetError().message.c_str());   // e.g. a copy queue
```

- `Initialize` fails, with the reason, when:
  - the device or the queue is missing
  - the queue isn't a direct queue of the device
  - the device lacks a requirement
- On the device, the GUI makes its own allocator (D3D12 Memory Allocator), fence, swapchains
  and pipelines.

**Ownership.**

- The GUI holds a reference to the device and the queue while it runs. It releases them, and
  everything it made, at `Cleanup`.
- Release yours after `Cleanup`:

```cpp
app.Cleanup();         // the GUI's own objects
hostQueue->Release();  // then yours
hostDevice->Release();
```

**The debug layer.**

- It's yours to turn on, before you create the device.
  - The GUI doesn't turn it on for a device you made: turning it on later would remove the
    device.
- When it's on, the GUI's messages go to its `DebugConfig` callback, or to the log.
- The GUI checks a device it made for leaks at `Cleanup`, but not yours, since your own
  references remain.

**Threading.**

- Direct3D 12 queues are free-threaded: submit to the shared queue from any thread, with no lock.
- The GUI orders its own work on the queue with its own fence.

## Drawing a scene into a viewport

- The `Viewport` control calls your `onRender` with the scene's command list, its targets
  already set. See [Viewport](../controls/viewport.md).
- On Direct3D 12 the payload is a `D3D12SceneRender`:

```cpp
if (const auto* scene = ctx.scene.As<tachyon::d3d12::D3D12SceneRender>())
    DrawScene(scene->commandList, ctx.projection * ctx.view);
```

- `ctx.projection` is already in Direct3D's clip space (y up).

The rest of this page is for textures you render yourself, on your own schedule.

## Showing a host-rendered texture

To show a texture you render, such as a 3D viewport, register it. You get an `ImageIndex`, the
number the `Image` control takes:

```cpp
tachyon::ImageIndex index = d3d12->RegisterExternalImage(texture, tachyon::Sampler::LinearClampToEdge);   // once
d3d12->UpdateExternalImage(index, newTexture, tachyon::Sampler::LinearClampToEdge);                       // after a resize

Image(index, tachyon::uint2{ width, height });   // in OnGUI
```

- You own the texture.
  - It must outlive every GUI frame that samples it.
  - Its format is a typed one the shaders can sample, such as `DXGI_FORMAT_R8G8B8A8_UNORM`.
- `UnregisterImage(index)` frees the index.

Synchronization contract, all on the queue passed to `SetExternalDevice`:

1. In work you submit before the GUI's frame, leave the texture in
   `D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE`.
2. Move it out of that state only in work you submit after the GUI's frame.
3. Submit during `OnGUI`. One queue runs its work in order, so that order is enough.

- After a resize, no GUI frame still in flight may sample the old texture.
  - `d3d12->WaitIdle()` is the simplest way to make sure.

### Textures from another queue

A texture written on another queue (async compute, for example) doesn't have to go through
the GUI's queue. Instead, have the GUI's frame wait for it:

```cpp
d3d12->AddFrameWait(computeDone, computeValue);   // in OnGUI
```

- `computeDone` is an `ID3D12Fence` of yours, signalled by the other queue.
- The GUI's queue waits until it reaches `computeValue` before the frame's work.
- Two waits on one fence are merged: the higher value is used.
- The frame after doesn't wait on it again.
- The texture's state is still yours to settle, as above.

## Copying a window into your texture

- A render-to-image window (`ElementFlags::WindowRenderToImage`) has no OS window. See
  [Rendering the UI into a host image](render-to-image.md).
- Each of its frames can be copied into a texture of yours:

```cpp
d3d12->SetWindowImageTarget(windowID, hostTexture);   // can be set before the window exists
d3d12->SetWindowImageTarget(windowID, nullptr);       // no more copies
```

- The texture:
  - is `DXGI_FORMAT_B8G8R8A8_UNORM`, the window's format
  - is the window's size: it isn't scaled, so a texture of another size gets the part they share
  - holds premultiplied alpha, transparent where the UI draws nothing
- The GUI's frame moves it from `D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE` to `COPY_DEST`,
  copies, and leaves it in `ALL_SHADER_RESOURCE`.
  - Read it in work you submit after the GUI's frame.
  - Leave it in `ALL_SHADER_RESOURCE` for the next frame.

---

[Previous: Using your own Vulkan device](vulkan-device.md) | [Integration](../integration.md) | [Next: Using your own SDL GPU device](sdlgpu-device.md)
