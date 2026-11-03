# Tutorial: your first app

From an empty folder to an app with two docked panels: buttons that change a count, and
fields that change a greeting.

Needs a C++20 compiler, CMake 3.22+, the [Vulkan SDK](https://vulkan.lunarg.com/) and
[Git LFS](https://git-lfs.com/); exact versions are in the
[README](https://github.com/LogicalError/TachyonUI#readme).

Finished program: [`examples/tutorial`](https://github.com/LogicalError/TachyonUI/tree/main/examples/tutorial)
(built with the other examples).

| Step | |
| --- | --- |
| [1. Set up the project](tutorial/setup.md) | Add TachyonUI to a CMake project and build it |
| [2. Buttons](tutorial/buttons.md) | Panels as classes; a count and buttons that change it |
| [3. Fields](tutorial/fields.md) | A panel with text, number and toggle fields |
| [4. The app](tutorial/app.md) | A window with the panels docked side by side, and `main` |

<!--!
\htmlonly <div class="page-tree"> \endhtmlonly
\subpage md_docs_2tutorial_2setup
\subpage md_docs_2tutorial_2buttons
\subpage md_docs_2tutorial_2fields
\subpage md_docs_2tutorial_2app
\htmlonly </div> \endhtmlonly
-->
