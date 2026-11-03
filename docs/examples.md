# Examples

Each folder in [`examples/`](https://github.com/LogicalError/TachyonUI/tree/main/examples)
is a complete application. Run from any folder:

| | Build | Run |
| --- | --- | --- |
| Windows | `build.bat` | `bin\Debug\hello_world.exe` |
| Linux and macOS | `./build.sh` | `bin/Debug/hello_world` |

- The small examples need no files: shaders, default themes and fonts are built in.
- Built when TachyonUI is the top-level project. Turn off with `TACHYON_BUILD_EXAMPLES` /
  `TACHYON_BUILD_SHOWCASE`.

| Example | Shows |
| --- | --- |
| [Hello world](examples/hello-world.md) | The smallest app: a window, a text field, buttons, `main`. |
| [Controls](examples/controls.md) | Common controls in label/value rows: buttons, toggles, text and number fields, slider, color, menu, tabs. |
| [Docking](examples/docking.md) | Panels in a dock layout the user can rearrange, tear off and redock. |
| [Custom theme](examples/custom-theme.md) | Your own CSS classes; switching palette and font size at run time. |
| [Host device](examples/host-device.md) | Running on your own Vulkan device and showing an image it renders. |
| [Showcase](examples/showcase.md) | Editor-style app: docking, tables, trees, file browser, node graph, settings. |
| [Tutorial](tutorial.md) (`examples/tutorial`) | The tutorial's app: two docked panels, buttons and fields. |

The API reference has each program's source under **Examples**.

<!--!
\htmlonly <div class="page-tree"> \endhtmlonly
\subpage md_docs_2examples_2hello-world
\subpage md_docs_2examples_2controls
\subpage md_docs_2examples_2docking
\subpage md_docs_2examples_2custom-theme
\subpage md_docs_2examples_2host-device
\subpage md_docs_2examples_2showcase
\htmlonly </div> \endhtmlonly
-->
