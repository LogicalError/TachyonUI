# Using your own Vulkan device

- By default the GUI creates its own `VkInstance` and `VkDevice`, and owns them.
- You can hand it your own instead, for example when the GUI is the editor of a game
  engine that already has a device.
- A complete program: `examples/host_device/host_device.cpp`. The
  [Host device](../examples/host-device.md) example walks through it.

## Device requirements

`tachyon::vulkan::VulkanDeviceContext::GetDeviceRequirements()` returns what the device
must enable:

```cpp
struct DeviceRequirements
{
    uint32_t                         apiVersion = VK_API_VERSION_1_3;
    VkPhysicalDeviceFeatures         features{};
    VkPhysicalDeviceVulkan11Features features11{};
    VkPhysicalDeviceVulkan12Features features12{};
    VkPhysicalDeviceVulkan13Features features13{};
    std::vector<const char*>         deviceExtensions;
};
```

The feature structs have no `pNext` chain; chain them yourself. With vk-bootstrap:

```cpp
auto requirements = tachyon::vulkan::VulkanDeviceContext::GetDeviceRequirements();
auto selected = vkb::PhysicalDeviceSelector{ instance }
    .set_minimum_version(1, 3)
    .set_required_features(requirements.features)
    .set_required_features_11(requirements.features11)
    .set_required_features_12(requirements.features12)
    .set_required_features_13(requirements.features13)
    .add_required_extensions(requirements.deviceExtensions)
    .defer_surface_initialization()
    .select();
```

- `VulkanDeviceContext::CheckPhysicalDeviceSupport(physicalDevice, &problems)` lists what
  a physical device lacks, one line per missing feature or extension.
- The GUI runs the same check when it adopts your device:
  - if the device lacks something, `Initialize` fails with a message
  - the check cannot see what you actually enabled when you created the device

```cpp
std::string problems;
if (!tachyon::vulkan::VulkanDeviceContext::CheckPhysicalDeviceSupport(physicalDevice, &problems))
    std::fprintf(stderr, "%s", problems.c_str());
```

Also required:

- The instance enables the window backend's surface extensions.
  - The window backend is the part of the GUI that creates OS windows. The default one
    uses SDL.
  - For SDL, get the extensions from `SDL_Vulkan_GetInstanceExtensions`.
  - That function needs SDL running, so call `SDL_Init(SDL_INIT_VIDEO)` before creating
    the instance.
- The queue supports graphics, and presenting to the GUI's windows.

```cpp
SDL_Init(SDL_INIT_VIDEO);   // before the instance is created

Uint32 count = 0;
if (const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count))
    for (Uint32 i = 0; i < count; ++i)
        builder.enable_extension(extensions[i]);
```

## Handing over the device

- The graphics backend is the part of the GUI that draws. The Vulkan one is
  `VulkanGraphicsContext`.
- Set your device on a `VulkanGraphicsContext` with `SetExternalDevice`.
- Give the backend to the app with `SetGraphicsBackend`.
- Do both before `Initialize`.

```cpp
#include <gui_core/graphics/vulkan/vk_graphics_context.h>

auto backend = std::make_unique<tachyon::vulkan::VulkanGraphicsContext>();
tachyon::vulkan::VulkanDeviceContext::ExternalDevice external;
external.instance            = hostInstance;
external.physicalDevice      = hostPhysicalDevice;
external.device              = hostDevice;
external.graphicsQueue       = hostGraphicsQueue;
external.graphicsQueueFamily = hostGraphicsQueueFamily;
external.allocator           = hostAllocator;     // optional
external.queueMutex          = &hostQueueMutex;   // optional
backend->SetExternalDevice(external);

auto* vk = backend.get();   // for RegisterExternalImage and SetWindowImageTarget
app.SetGraphicsBackend(std::move(backend));
if (tachyon::Result<void> result = app.Initialize(); !result)
    std::fprintf(stderr, "%s\n", result.GetError().message.c_str());   // e.g. a missing device feature
```

- `allocator` is optional.
  - Without it, the GUI creates its own `VmaAllocator` and owns it.
  - An allocator you pass must be created with
    `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT`, or `Initialize` fails.

```cpp
VmaAllocatorCreateInfo info{};
info.flags          = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
info.instance       = hostInstance;
info.physicalDevice = hostPhysicalDevice;
info.device         = hostDevice;
info.vulkanApiVersion = VK_API_VERSION_1_3;
vmaCreateAllocator(&info, &hostAllocator);
```

**Ownership.**

- The GUI destroys only what it created.
- Destroy your instance, device and allocator after `Cleanup`.

```cpp
app.Cleanup();                       // the GUI's own objects
vmaDestroyAllocator(hostAllocator);  // then yours
vkDestroyDevice(hostDevice, nullptr);
vkDestroyInstance(hostInstance, nullptr);
```

**Threading.**

- The GUI locks `queueMutex` around every `vkQueueSubmit2`, `vkQueuePresentKHR` and
  `vkDeviceWaitIdle`.
- If other threads submit to the same queue, pass the mutex they lock.
  - Without one, the GUI uses a private mutex that only guards its own threads.
- Presenting can block until vsync while holding the lock.
  - If the device has a second graphics queue, give the GUI its own queue to avoid that.
- While recording a frame, the GUI copies uploads into mapped memory on a separate thread,
  and waits for it before each submit.
  - That thread never touches the queue.

```cpp
// The host's own submits lock the mutex it passed as queueMutex.
std::lock_guard lock(hostQueueMutex);
vkQueueSubmit2(hostGraphicsQueue, 1, &submit, fence);
```

## Showing a host-rendered image

To show an image you render, such as a 3D viewport, register its view and sampler. You get
an `ImageIndex`, the number the `Image` control takes:

```cpp
tachyon::ImageIndex index = vk->RegisterExternalImage(view, sampler);   // once
vk->UpdateExternalImage(index, newView, sampler);                          // after a resize

Image(index, tachyon::uint2{ width, height });   // in OnGUI
```

- You own the view and the sampler.
  - They must outlive every GUI frame that uses the index.
- `Image` uses the style `theme.imageStyleRef`.
  - A custom element or style shows the image only if its background color style type is
    `ColorType::StretchedTexture`.

Synchronization contract, all on the queue passed to `SetExternalDevice`:

1. By the end of your frame's command buffer, leave the image in
   `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`, writes visible to the fragment stage
   (destination stage `FRAGMENT_SHADER`, access `SHADER_SAMPLED_READ`).
2. Before writing it again, transition out of that layout with source stage
   `FRAGMENT_SHADER`, so your write waits for the GUI's earlier reads.
3. Submit during `OnGUI`, before the GUI submits the frame. Ordering on one queue makes
   both barriers apply across command buffers.

- After a resize, no GUI frame still in flight may sample the old view.
  - `vkDeviceWaitIdle` is the simplest way to make sure.
- Reference: `Host::Render` in `examples/host_device/host_device.cpp`.
  - `tests/gpu/gpu_external_device_test.cpp` checks it under synchronization validation.

### Images from another queue

An image written on another queue (async compute, for example) doesn't have to go
through the GUI's queue. Instead, have the GUI's frame wait for it:

```cpp
vk->AddFrameWait(computeDone, computeValue, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);   // in OnGUI
```

- `computeDone` is a timeline semaphore on the GUI's device.
- Every window's submission of the next frame waits until it reaches `computeValue`,
  before the stages you pass.
- Two waits on one semaphore are merged:
  - the higher value is used
  - both stages wait
- The frame after doesn't wait on it again.
- The image's layout and queue family ownership are still yours to settle, as above.

---

[Previous: Adding the library to a build](build.md) | [Integration](../integration.md) | [Next: Your own window and input](window-and-input.md)
