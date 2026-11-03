# Using your own Vulkan device

By default the GUI creates and owns its `VkInstance` and `VkDevice`. You can hand it your
own instead. Complete program: `examples/host_device/host_device.cpp`.

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

`VulkanDeviceContext::CheckPhysicalDeviceSupport(physicalDevice, &problems)` lists what a
physical device lacks, one line each. The GUI runs it when adopting the device, and
`Initialize` fails with a message if unsupported. It cannot see what you actually enabled
at device creation.

Also required:

- The instance enables the window backend's surface extensions. For SDL, get them from
  `SDL_Vulkan_GetInstanceExtensions`, which needs SDL running first (hence
  `SDL_Init(SDL_INIT_VIDEO)` before creating the instance).
- The queue supports graphics and presenting to the GUI's windows.

## Handing over the device

Set the device on a `VulkanGraphicsContext` and give it to the app, both before
`Initialize`:

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

`allocator` is optional; without it the GUI creates and owns its own `VmaAllocator`. A
passed allocator must be created with `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT`,
or `Initialize` fails.

**Ownership.** The GUI destroys only what it created. Destroy your instance, device and
allocator after `Cleanup`.

**Threading.**

- The GUI locks `queueMutex` around every `vkQueueSubmit2`, `vkQueuePresentKHR` and
  `vkDeviceWaitIdle`.
- If other threads submit to the same queue, pass the mutex they lock. Without one the
  GUI uses a private mutex that only guards its own threads.
- Presenting can block until vsync while holding the lock. With a second graphics queue,
  give the GUI its own to avoid that.
- During recording the GUI copies uploads into mapped memory on a separate thread and
  waits for it before each submit. That thread never touches the queue.

## Showing a host-rendered image

Show an image you render (e.g. a 3D viewport):

```cpp
tachyon::ImageIndex index = vk->RegisterExternalImage(view, sampler);   // once
vk->UpdateExternalImage(index, newView, sampler);                          // after a resize

Image(index, tachyon::uint2{ width, height });   // in OnGUI
```

- You own the view and sampler; they must outlive every GUI frame that uses the index.
- `Image` uses `theme.imageStyleRef`. A custom element or style shows the image only if
  its background color style type is `ColorType::StretchedTexture`.

Synchronization contract, all on the queue passed to `SetExternalDevice`:

1. By the end of your frame's command buffer, leave the image in
   `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`, writes visible to the fragment stage
   (destination stage `FRAGMENT_SHADER`, access `SHADER_SAMPLED_READ`).
2. Before writing it again, transition out of that layout with source stage
   `FRAGMENT_SHADER`, so your write waits for the GUI's earlier reads.
3. Submit during `OnGUI`, before the GUI submits the frame. Ordering on one queue makes
   both barriers apply across command buffers.

After a resize, no in-flight GUI frame may still sample the old view; `vkDeviceWaitIdle`
is simplest. Reference: `Host::Render` in `examples/host_device/host_device.cpp`;
checked under synchronization validation by `tests/gpu/gpu_external_device_test.cpp`.

---

[Previous: Adding the library to a build](build.md) | [Integration](../integration.md) | [Next: Your own window and input](window-and-input.md)
