# Using your own Vulkan device

By default the GUI creates its own `VkInstance` and `VkDevice` and owns them. If your
program already has a device, you can hand its handles to the GUI so that both render on
the same device. `examples/host_device/host_device.cpp` is a complete program that does
this.

## Device requirements

`tachyon::vulkan::VulkanDeviceContext::GetDeviceRequirements()` returns what the device
has to enable:

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

The feature structs don't have a `pNext` chain, so you chain them yourself. Here's how
it looks with vk-bootstrap, as in the example:

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

`VulkanDeviceContext::CheckPhysicalDeviceSupport(physicalDevice, &problems)` tells you
what a physical device is missing, with one line per missing capability. The GUI runs
this check when it adopts the device, and `Initialize` fails with a message if the
device can't run the GUI. The check can't see what you actually enabled when you created
the device, though.

There are two more requirements besides the device:

- The instance has to enable the extensions the window backend needs to create
  surfaces. For SDL, you get those from `SDL_Vulkan_GetInstanceExtensions`. SDL has to
  be running before you create the instance for that to work, which is why the example
  calls `SDL_Init(SDL_INIT_VIDEO)` first.
- The queue has to support graphics, and presenting to the GUI's windows.

## Handing over the device

Set the device on a `VulkanGraphicsContext` and give that to the app. Do both before
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

You don't have to pass an `allocator`. Without one, the GUI creates its own
`VmaAllocator` and owns it. If you do pass one, it must have been created with
`VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT`, or `Initialize` fails.

**Ownership.** The GUI only destroys what it created. Your instance, device and
allocator are still alive after `Cleanup`, and you destroy them after that.

**Threading.** A Vulkan queue needs external synchronization. The GUI locks
`queueMutex` around every `vkQueueSubmit2` and `vkQueuePresentKHR` call it makes, and
around `vkDeviceWaitIdle`. If you submit to the same queue from other threads, pass in
the mutex you lock around your own submissions. Without one, the GUI uses a private
mutex, which only protects it from its own threads. Keep in mind that presenting can
block until vsync while the lock is held. If you have a second graphics queue, you can
give the GUI a queue of its own to avoid that.

While it records commands, the GUI copies its uploads into mapped memory on a separate
thread, and waits for that thread before each submit. That thread never touches the
queue.

## Showing a host-rendered image

You can render into an image you own and show it in the UI, for example as a 3D
viewport:

```cpp
tachyon::ImageIndex index = vk->RegisterExternalImage(view, sampler);   // once
vk->UpdateExternalImage(index, newView, sampler);                          // after a resize

Image(index, tachyon::uint2{ width, height });   // in OnGUI
```

You keep ownership of the view and sampler. They have to outlive every GUI frame that
uses the index. `Image` uses `theme.imageStyleRef`. If you use a custom element or
style, it only shows the image when the type of its background color style is
`ColorType::StretchedTexture`.

The synchronization contract has three rules, all on the queue you passed to
`SetExternalDevice`:

1. By the end of the command buffer you submit for the frame, leave the image in
   `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`, with your writes visible to the fragment
   stage (destination stage `FRAGMENT_SHADER`, access `SHADER_SAMPLED_READ`).
2. Before you write the image again, transition it out of that layout with a source
   stage of `FRAGMENT_SHADER`. That way your write waits for the GUI's earlier reads.
3. Submit while the UI is being built (in `OnGUI`), before the GUI submits the frame.
   Because the submissions are ordered on one queue, both barriers take effect across
   command buffers.

After a resize, make sure no GUI frame that's still in flight samples the old view. The
easiest way is `vkDeviceWaitIdle`. `Host::Render` in
`examples/host_device/host_device.cpp` follows this contract, and
`tests/gpu/gpu_external_device_test.cpp` checks it under synchronization validation.

---

[Previous: Adding the library to a build](build.md) | [Integration](../integration.md) | [Next: Your own window and input](window-and-input.md)
