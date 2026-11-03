# Host device

In this example a "host" application, such as a game engine, creates its own Vulkan
instance and device and hands them to the GUI. Every frame the host renders into an
image of its own, and the GUI shows that image in a window.
Full source: [examples/host_device/host_device.cpp](https://github.com/LogicalError/TachyonUI/blob/main/examples/host_device/host_device.cpp).

The example uses vk-bootstrap so it can create the device in a few lines. Your own host
will have its own code for that. What matters is what it enables and what it hands over.

## A device the GUI can use

The host has to enable everything the GUI needs, and
`VulkanDeviceContext::GetDeviceRequirements()` gives you that list. The instance also
needs the extensions SDL uses to create window surfaces.

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

## Handing it over

The host describes its handles in an `ExternalDevice`. Since the host and the GUI submit
to the same queue, the host also passes in the mutex it locks around its own
submissions.

```cpp
tachyon::vulkan::VulkanDeviceContext::ExternalDevice external;
external.instance            = instance.instance;
external.physicalDevice      = physicalDevice.physical_device;
external.device              = device.device;
external.graphicsQueue       = queue;
external.graphicsQueueFamily = queueFamily;
external.queueMutex          = &queueMutex;
```

The app gives that device to the Vulkan backend before calling `Initialize`:

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

After `Initialize`, the app registers the host's image view and sampler once. The GUI
samples them, but the host still owns them.

```cpp
// The GUI samples the host's view and sampler; the host keeps owning them.
hostImage = vk->RegisterExternalImage(host.view, host.sampler);
```

Each frame, the host submits its rendering while the UI is being built, before the GUI
submits its own work. `Image` then shows the result.

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

`Host::Render` follows the GUI's synchronization contract. Before it writes the image,
it waits for the GUI's fragment-stage reads to finish. When it's done, it leaves the
image in `SHADER_READ_ONLY_OPTIMAL` so the fragment stage can read it.

## Shutting down

`Cleanup` only destroys what the GUI created. The host's device is still there
afterwards, and the host destroys it itself.

```cpp
app.Cleanup();   // the GUI destroys only what it created; the device stays
```

For the full requirements, the ownership rules and the synchronization contract, see
[Using your own Vulkan device](../integration/vulkan-device.md).

---

[Previous: Custom theme](custom-theme.md) | [Examples](../examples.md) | [Next: Showcase](showcase.md)
