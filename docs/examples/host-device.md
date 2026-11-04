# Host device

A host application, such as a game engine, creates its own Vulkan instance and device and
hands them to the GUI. Each frame the host renders into its own image, and the GUI shows
that image in a window.
Full source: [examples/host_device/host_device.cpp](https://github.com/LogicalError/TachyonUI/blob/main/examples/host_device/host_device.cpp).

- On this page:
  - the host is the application that owns the Vulkan device
  - the GUI is TachyonUI, running on that device
- The example creates the device with vk-bootstrap. Any other way works too; what matters
  is what it enables and what it hands over.

## A device the GUI can use

- Enable everything `VulkanDeviceContext::GetDeviceRequirements()` lists.
- The instance also needs SDL's surface extensions, because SDL creates the GUI's windows.

```cpp
// Enable everything the GUI needs on the device.
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

The surface extensions come from SDL, so SDL must be running before the instance is
created:

```cpp
// In main, before the host creates its instance
SDL_Init(SDL_INIT_VIDEO);

// In the host, while building the instance
Uint32 count = 0;
if (const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count))
    for (Uint32 i = 0; i < count; ++i)
        builder.enable_extension(extensions[i]);
```

## Handing it over

- Describe the host's handles in an `ExternalDevice`.
- Host and GUI submit to the same queue, so pass the mutex the host locks around its own
  submissions in `queueMutex`.

```cpp
tachyon::vulkan::VulkanDeviceContext::ExternalDevice external;
external.instance            = instance.instance;
external.physicalDevice      = physicalDevice.physical_device;
external.device              = device.device;
external.graphicsQueue       = queue;
external.graphicsQueueFamily = queueFamily;
external.queueMutex          = &queueMutex;
```

- Give it to the Vulkan graphics backend before `Initialize`:
  - the graphics backend is the part of TachyonUI that draws, here
    `VulkanGraphicsContext`
  - `SetExternalDevice` makes it render on the host's device instead of creating one
  - `SetGraphicsBackend` hands the backend to the app, which owns it from then on
- Keep a plain pointer to the backend for the calls below.

```cpp
auto backend = std::make_unique<tachyon::vulkan::VulkanGraphicsContext>();
if (this->host.CreateDevice())
    backend->SetExternalDevice(this->host.Describe());
else
    std::printf("no Vulkan 1.3 device with the features the GUI needs\n");
vk = backend.get();
SetGraphicsBackend(std::move(backend));
```

## Showing the host's image

- After `Initialize`, register the host's image view and sampler once.
  - `RegisterExternalImage` returns an `ImageIndex`, the number controls use to show an
    image.
  - The host keeps owning the view and the sampler.

```cpp
// The GUI samples the host's view and sampler; the host keeps owning them.
hostImage = vk->RegisterExternalImage(host.view, host.sampler);
```

- Each frame the host submits its rendering during `OnGUI`, before the GUI submits its own
  frame.
- `Image` then shows the result.

```cpp
void OnGUI() override
{
    // The host submits its frame while the UI is built, before the GUI submits.
    const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
    host.Render(0.5f + 0.5f * std::sin(t), 0.5f + 0.5f * std::sin(t + 2.1f), 0.5f + 0.5f * std::sin(t + 4.2f));

    if (BeginWindow(windowID, "Host device", tachyon::LayoutOverride{}.Size(320, 340)))
    {
        BeginVertical(tachyon::LayoutOverride{}.FlexGrow(1));
        Text("Rendered by the host:");
        Image(hostImage, tachyon::uint2{ Host::kImageSize.width, Host::kImageSize.height });
        EndVertical();
    }
    EndWindow();
}
```

- `Host::Render` follows the GUI's rules for the image:
  - before writing it, it waits for the GUI's fragment-stage reads
  - when done, it leaves the image in `SHADER_READ_ONLY_OPTIMAL`

```cpp
// Wait for the GUI's reads, then clear the image.
Barrier(firstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
        VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);

// Leave it readable by the GUI's fragment shader.
Barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
```

## Shutting down

- `Cleanup` destroys only what the GUI created.
- The host destroys its own device afterwards.

```cpp
app.Cleanup();   // the GUI destroys only what it created; the device stays
```

Full requirements, ownership and synchronization rules:
[Using your own Vulkan device](../integration/vulkan-device.md).

---

[Previous: Custom theme](custom-theme.md) | [Examples](../examples.md) | [Next: Showcase](showcase.md)
