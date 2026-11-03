# TachyonUI documentation

TachyonUI is an immediate-mode GUI library for C++20. It draws on the GPU (through Vulkan
for now) and gets its windows and input from SDL3 by default. You describe the whole UI
in code every frame, and the library keeps track of the state, lays everything out with
flexbox and grid, and draws it.

If you're new, start with the [Tutorial](tutorial.md). It takes you from an empty folder
to an app with docked panels, buttons and fields. After that, read the Overview.

| Guide | Read it for |
| --- | --- |
| [Tutorial](tutorial.md) | Your first app, step by step: the project, panels, buttons, fields and docking. |
| [Overview](overview.md) | The frame model, IDs, the Begin/End rules, return values, units, errors and saved state. |
| [Controls](controls.md) | Every control and container, grouped by family, with what each one takes, returns and how it's styled. |
| [CSS and theming](css.md) | How themes are loaded, the selectors and states, values, and every supported property. |
| [Integration](integration.md) | Adding the library to your build, using your own Vulkan device, window, input and frame loop, and rendering into your own image. |
| [Examples](examples.md) | Walkthroughs of the example programs, from hello world to hosting on your own Vulkan device. |

The [API reference](https://logicalerror.github.io/TachyonUI/) covers every class and
function, and has these guides alongside it. If you'd rather browse by area, its
**API by topic** pages group the classes into application, controls, layout, styling,
text, input, graphics and windows.

<!--!
\htmlonly <div class="page-tree"> \endhtmlonly
\subpage md_docs_2tutorial
\subpage md_docs_2overview
\subpage md_docs_2controls
\subpage md_docs_2css
\subpage md_docs_2integration
\subpage md_docs_2examples
\htmlonly </div> \endhtmlonly
-->
