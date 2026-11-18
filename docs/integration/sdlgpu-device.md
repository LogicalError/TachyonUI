# Using your own SDL GPU device

- With the SDL GPU backend (`TACHYON_SDLGPU`), the GUI creates its own `SDL_GPUDevice` by
  default, and owns it.
- You can hand it your own instead, for example when your app already renders with SDL3's GPU
  API.
- The tests that do what a host does: `tests/gpu/sdlgpu_external_device_test.cpp`.
- For the other backends, see [Using your own Vulkan device](vulkan-device.md) and
  [Using your own Direct3D 12 device](d3d12-device.md).

## Choosing the backend

- An app picks SDL GPU in the options it passes to `Initialize`:

```cpp
InitializeOptions options;
options.graphicsAPI = tachyon::GraphicsAPI::SDLGPU;
app.Initialize(options);
```

- SDL picks the driver: Vulkan or Direct3D 12.
  - `SDLGPUGraphicsContext::SetDriver("direct3d12")` asks for one, before `Initialize`.
  - So does SDL's `SDL_GPU_DRIVER` environment variable, for example `SDL_GPU_DRIVER=vulkan`.
- Metal (macOS) isn't supported yet: the GUI's shaders aren't built for it.

## Device requirements

- The GUI's shaders are built as SPIR-V (for Vulkan) and, on Windows, DXIL (for Direct3D 12).
  The device must take one of them.
- On Vulkan, the shaders use features SDL doesn't turn on by itself:
  - 16-bit storage and 16-bit types
  - draw parameters
- `SDLGPUGraphicsContext::ApplyDeviceRequirements(props)` sets all of that on the properties
  you create the device with:

```cpp
#include <gui_core/graphics/sdlgpu/sdlgpu_graphics_context.h>

SDL_PropertiesID props = SDL_CreateProperties();
tachyon::sdlgpu::SDLGPUGraphicsContext::ApplyDeviceRequirements(props);
SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, debug);   // your choice
SDL_GPUDevice* device = SDL_CreateGPUDeviceWithProperties(props);
SDL_DestroyProperties(props);
```

- It sets the Vulkan options property (`SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER`).
  - If you set Vulkan options of your own, they replace it. Then turn these on in yours, with
    Vulkan 1.2:
    - `storageBuffer16BitAccess`, `uniformAndStorageBuffer16BitAccess` and
      `shaderDrawParameters` (Vulkan 1.1 features)
    - `shaderFloat16` (Vulkan 1.2)
    - `shaderInt16` (Vulkan 1.0)
- `SDLGPUGraphicsContext::CheckDeviceSupport(device, &problems)` checks a device. It lists what
  the device lacks, one line each:

```cpp
std::string problems;
if (!tachyon::sdlgpu::SDLGPUGraphicsContext::CheckDeviceSupport(device, &problems))
    std::fprintf(stderr, "%s", problems.c_str());
```

- SDL can't tell which Vulkan features a device was made with, so the check can't see them.
  - A device made without them may draw the UI wrongly.
  - SDL GPU's debug mode reports it.

## Handing over the device

- Set your device on an `SDLGPUGraphicsContext` with `SetExternalDevice`.
- Give the backend to the app with `SetGraphicsBackend`.
- Do both before `Initialize`.

```cpp
auto backend = std::make_unique<tachyon::sdlgpu::SDLGPUGraphicsContext>();
backend->SetExternalDevice(device);

auto* sdlgpu = backend.get();   // for RegisterExternalImage and SetWindowImageTarget
app.SetGraphicsBackend(std::move(backend));
if (tachyon::Result<void> result = app.Initialize(); !result)
    std::fprintf(stderr, "%s\n", result.GetError().message.c_str());
```

- `Initialize` fails, with the reason, when the device lacks a requirement it can check.
- On the device, the GUI makes its own pipelines, samplers, buffers and textures.
- The GUI claims its OS windows for the device (`SDL_ClaimWindowForGPUDevice`), and releases
  them when they close.

**Ownership.**

- The device stays yours. At `Cleanup` the GUI releases everything it made on it, and leaves
  the device running.
- Destroy it after `Cleanup`:

```cpp
app.Cleanup();                  // the GUI's own objects
SDL_DestroyGPUDevice(device);   // then yours
```

**Debug mode.**

- It's yours to turn on, when you create the device.
- When it's on, SDL GPU's messages go to the GUI's `DebugConfig` callback, if you set one, or
  to SDL's log.

**Ordering.**

- SDL GPU runs every command buffer submitted to a device in the order it was submitted, with
  the barriers between them.
- So work you submit before the GUI's frame (during `OnGUI`, say) is done before the GUI's frame
  reads it, and work you submit after sees what the GUI's frame wrote.
- There's no fence to hand over, unlike Direct3D 12's `AddFrameWait`.

## Drawing a scene into a viewport

- The `Viewport` control calls your `onRender` with the scene's command buffer and textures.
  See [Viewport](../controls/viewport.md).
- On SDL GPU the payload is an `SDLGPUSceneRender`:

```cpp
if (const auto* scene = ctx.scene.As<tachyon::sdlgpu::SDLGPUSceneRender>())
{
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(scene->commandBuffer, &scene->colorTarget, 1, &scene->depthTarget);
    DrawScene(pass, ctx.projection * ctx.view);
    SDL_EndGPURenderPass(pass);
}
```

- `ctx.projection` is already in SDL GPU's clip space (y up, on every driver).

The rest of this page is for textures you render yourself, on your own schedule.

## Showing a host-rendered texture

To show a texture you render, such as a 3D viewport, register it. You get an `ImageIndex`, the
number the `Image` control takes:

```cpp
tachyon::ImageIndex index = sdlgpu->RegisterExternalImage(texture, tachyon::Sampler::LinearClampToEdge);   // once
sdlgpu->UpdateExternalImage(index, newTexture, tachyon::Sampler::LinearClampToEdge);                       // after a resize

Image(index, tachyon::uint2{ width, height });   // in OnGUI
```

- You own the texture.
  - It must outlive every GUI frame that samples it.
  - It has `SDL_GPU_TEXTUREUSAGE_SAMPLER`, and a format the shaders can sample, such as
    `SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM`.
- Render it in a command buffer you submit before the GUI's frame, for example during `OnGUI`.
- After a resize, no GUI frame still in flight may sample the old texture.
  - `sdlgpu->WaitIdle()` is the simplest way to make sure.
- `UnregisterImage(index)` frees the index.

## Copying a window into your texture

- A render-to-image window (`ElementFlags::WindowRenderToImage`) has no OS window. See
  [Rendering the UI into a host image](render-to-image.md).
- Each of its frames can be copied into a texture of yours:

```cpp
sdlgpu->SetWindowImageTarget(windowID, hostTexture, tachyon::uint2{ 400, 300 });   // can be set before the window exists
sdlgpu->SetWindowImageTarget(windowID, nullptr, {});                              // no more copies
```

- The texture:
  - is `SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM`, the window's format
  - is the window's size, which you pass: SDL can't tell a texture's size, and the copy isn't
    scaled, so a texture of another size gets the part they share
  - holds premultiplied alpha, transparent where the UI draws nothing
- Read it in work you submit after the GUI's frame.

---

[Previous: Using your own Direct3D 12 device](d3d12-device.md) | [Integration](../integration.md) | [Next: Your own window and input](window-and-input.md)
