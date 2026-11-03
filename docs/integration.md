# Integration

These pages are for putting TachyonUI into a program that already has its own build,
Vulkan device, windows or main loop, such as a game engine that uses it for its editor.
You only need the parts that apply to you. If the defaults suit you, calling
`Initialize` and `Run` is enough, as in the README's hello world.

The [Overview](overview.md) explains the frame model: what `OnGUI` is, when it runs and
how IDs stay stable from frame to frame.

| Page | What it covers |
| --- | --- |
| [Adding the library to a build](integration/build.md) | Using TachyonUI as a subproject or an installed package, and where its resources come from. |
| [Using your own Vulkan device](integration/vulkan-device.md) | What the device needs, how to hand it over, and how to show an image you rendered. |
| [Your own window and input](integration/window-and-input.md) | Writing a window backend, or running SDL's event loop yourself. |
| [Your own loop](integration/frame-loop.md) | Driving each frame from your own main loop, and handling errors and device loss. |
| [Rendering the UI into a host image](integration/render-to-image.md) | Windows that have no OS window: copying them into your image, reading their pixels back and sending them input. |
| [Allocation hooks and headless backends](integration/allocation-and-headless.md) | Sending allocations through your own allocator, and running without a GPU or a display. |

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
