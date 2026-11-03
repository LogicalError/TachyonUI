# Tutorial: your first app

In this tutorial you'll go from an empty folder to an app with two docked panels. One
panel has buttons that change a count, and the other has fields that change a greeting.

You'll need a C++20 compiler, CMake 3.22 or newer, the [Vulkan SDK](https://vulkan.lunarg.com/)
and [Git LFS](https://git-lfs.com/). The [README](https://github.com/LogicalError/TachyonUI#readme)
lists the exact versions.

The finished program is in [`examples/tutorial`](https://github.com/LogicalError/TachyonUI/tree/main/examples/tutorial).
It's built along with the other examples, so you can run it first to see where you're
headed.

| Step | |
| --- | --- |
| [1. Set up the project](tutorial/setup.md) | Add TachyonUI to a CMake project and build it. |
| [2. Buttons](tutorial/buttons.md) | Write panels as classes, starting with a count and buttons that change it. |
| [3. Fields](tutorial/fields.md) | Add a panel with text, number and toggle fields. |
| [4. The app](tutorial/app.md) | Open a window with the panels docked side by side, and write `main`. |

<!--!
\htmlonly <div class="page-tree"> \endhtmlonly
\subpage md_docs_2tutorial_2setup
\subpage md_docs_2tutorial_2buttons
\subpage md_docs_2tutorial_2fields
\subpage md_docs_2tutorial_2app
\htmlonly </div> \endhtmlonly
-->
