# TachyonUI documentation

TachyonUI is an immediate-mode C++20 GUI library for tools:

- You build the UI in `OnGUI()`, a function of your app that runs every frame.
- The library keeps the state between frames, lays the UI out and draws it on the GPU.

```cpp
void OnGUI() override
{
    if (BeginWindow(windowID, "Hello", LayoutOverride{}.Size(360, 160)))
        Button("Quit", [this] { applicationQuit = true; });
    EndWindow();
}
```

New? Start with the [Tutorial](tutorial.md).

| Guide | Covers |
| --- | --- |
| [Tutorial](tutorial.md) | Your first app, step by step |
| [Overview](overview.md) | The frame, IDs, Begin/End, return values, units, errors, saved state |
| [Controls](controls.md) | Every control: signatures, return values, CSS classes |
| [CSS and theming](css.md) | Themes, selectors, values, properties |
| [Integration](integration.md) | Build setup, your own Vulkan device, window, input and loop |
| [Examples](examples.md) | The example programs |

[API reference](https://logicalerror.github.io/TachyonUI/): every class and function, also
grouped by topic.

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
