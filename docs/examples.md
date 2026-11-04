# Examples

Each folder in [`examples/`](https://github.com/LogicalError/TachyonUI/tree/main/examples)
is a complete application. Build it and run it from any folder:

| | Build | Run |
| --- | --- | --- |
| Windows | `build.bat` | `bin\Debug\hello_world.exe` |
| Linux and macOS | `./build.sh` | `bin/Debug/hello_world` |

- The small examples need no files next to the program:
  - the shaders are built into the library
  - so are the default themes and fonts
- The examples are built only when TachyonUI is the top-level project, that is, when you
  run CMake on the TachyonUI folder itself and not on a project that includes it.
- To skip them, turn off:
  - `TACHYON_BUILD_EXAMPLES` for the small examples
  - `TACHYON_BUILD_SHOWCASE` for the showcase

  ```text
  cmake -B build -DTACHYON_BUILD_EXAMPLES=OFF -DTACHYON_BUILD_SHOWCASE=OFF
  ```

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
