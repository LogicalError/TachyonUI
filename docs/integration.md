# Integration

For programs that already have their own build, Vulkan device, windows or main loop.
Read only what applies; with the defaults, `Initialize` and `Run` are enough. The frame
model is in the [Overview](overview.md).

| Page | Covers |
| --- | --- |
| [Adding the library to a build](integration/build.md) | Subproject or installed package; CMake options; resources. |
| [Using your own Vulkan device](integration/vulkan-device.md) | Device requirements, handing it over, showing your own image. |
| [Your own window and input](integration/window-and-input.md) | Writing a window backend; running SDL's event loop yourself. |
| [Your own loop](integration/frame-loop.md) | Per-frame calls; errors and device loss. |
| [Rendering the UI into a host image](integration/render-to-image.md) | Windows without an OS window: copy to your image, read pixels, inject input. |
| [Allocation hooks and headless backends](integration/allocation-and-headless.md) | Your own allocator; running without GPU or display. |

<!--!
\htmlonly <div class="page-tree"> \endhtmlonly
\subpage md_docs_2integration_2build
\subpage md_docs_2integration_2vulkan-device
\subpage md_docs_2integration_2window-and-input
\subpage md_docs_2integration_2frame-loop
\subpage md_docs_2integration_2render-to-image
\subpage md_docs_2integration_2allocation-and-headless
\htmlonly </div> \endhtmlonly
-->
