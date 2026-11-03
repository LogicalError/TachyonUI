# Examples

Each folder in [`examples/`](https://github.com/LogicalError/TachyonUI/tree/main/examples)
is a complete application. The small ones each focus on one part of the library, and the
showcase puts most of it together in an editor-style app. They build along with the
project, and you can run any of them from any folder:

| | Build | Run |
| --- | --- | --- |
| Windows | `build.bat` | `bin\Debug\hello_world.exe` |
| Linux and macOS | `./build.sh` | `bin/Debug/hello_world` |

The small examples don't need any files. The shaders, the default themes and the fonts
are built into the library, so what you see in them is what integrating TachyonUI into
your own program looks like.

They're built when TachyonUI is the top-level project. Set `TACHYON_BUILD_EXAMPLES` or
`TACHYON_BUILD_SHOWCASE` to off if you don't want them.

| Example | What it shows |
| --- | --- |
| [Hello world](examples/hello-world.md) | The smallest app: a window with a text field and some buttons, plus the program around them. |
| [Controls](examples/controls.md) | The common controls laid out as label/value rows: buttons, toggles, text and number fields, a slider, a color, a menu and tabs. |
| [Docking](examples/docking.md) | Panels in a dock layout that the user can rearrange, tear off and dock again. |
| [Custom theme](examples/custom-theme.md) | Adding your own CSS classes next to the library's, and switching palettes and font size while the app runs. |
| [Host device](examples/host-device.md) | Running on a Vulkan device your application already owns, and showing an image it renders. |
| [Showcase](examples/showcase.md) | An editor-style app that uses most of the library in its panels: docking, tables, trees, a file browser, a node graph and settings. |
| [Tutorial](tutorial.md) (`examples/tutorial`) | The tutorial's app, built up step by step: two docked panels, one with buttons and one with fields. |

The API reference shows the full source of each program under **Examples**, and links
the functions they use back to them.

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
