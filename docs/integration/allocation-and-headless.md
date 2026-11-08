# Allocation hooks and headless backends

## Allocation hooks

- Allocation hooks are your own memory functions, which the library then uses instead of
  its default ones.
- Set them before `Initialize`:

```cpp
#include <gui_core/memory/gui_allocation_hooks.h>

tachyon::AllocationHooks hooks;
hooks.allocate   = &MyAllocate;     // void* (size_t size, size_t alignment, void* user)
hooks.reallocate = &MyReallocate;   // void* (void* ptr, size_t size, size_t alignment, void* user)
hooks.free       = &MyFree;         // void  (void* ptr, void* user)
hooks.user       = &myHeap;
app.SetAllocationHooks(hooks);
```

- The hooks cover:
  - Vulkan objects
  - VMA
  - vk-bootstrap
  - SDL
  - the text and SVG libraries: HarfBuzz, skribidi, SheenBidi and nanosvg
- SDL's memory functions are shared by the whole process. SDL uses the hooks only if:
  - the GUI starts SDL
  - and SDL hasn't allocated anything yet
- The text and SVG libraries use the hooks while the app's text system lives:
  - from `Initialize` until `Cleanup`
  - after `Cleanup` they are back on the C heap (`malloc` and `free`)
  - memory they took from the hooks is still given back to the hooks, even after `Cleanup`
- A text system you make yourself can take the hooks too:

```cpp
#include <gui_core/text/skribidi/gui_text_system_skribidi.h>

tachyon::SkribidiTextSystem text(hooks);   // the libraries use the hooks while `text` lives
```

- With a HarfBuzz of your own (`TACHYON_SYSTEM_HARFBUZZ`), HarfBuzz keeps its own allocator;
  skribidi, SheenBidi and nanosvg still use the hooks.
- The hooks must be thread-safe, because drivers call them from their own threads.
- They must outlive the GUI.
  - SDL keeps using them for a few allocations after `SDL_Quit`, so keep them alive for
    the whole process.
  - HarfBuzz keeps some data until the process ends and frees it at exit:
    - its Unicode tables, and the languages skribidi and the text system use, are made
      on the C heap, so the hooks never see them
    - a language first seen while the hooks are in use (a text style's own `language`,
      for example) is made through the hooks, and given back to them at exit

- `tachyon::TrackingAllocator` (`gui_core/memory/gui_tracking_allocator.h`) has
  ready-made hooks:
  - they count the allocations that are still live
  - they report leaks
  - `Hooks()` returns them

```cpp
#include <gui_core/memory/gui_tracking_allocator.h>

static tachyon::TrackingAllocator tracker;   // lives as long as the process
app.SetAllocationHooks(tracker.Hooks());
// ...
tracker.ReportLeaks();   // logs each allocation still live
```

- `tachyon::LibraryAllocator` (`gui_core/memory/gui_library_allocator.h`) is what sends the
  text and SVG libraries' memory to the hooks:
  - `IsRouted()` tells whether they use hooks right now
  - a `HeapScope` keeps what they allocate on its thread on the C heap, for data a
    library keeps until the process ends:

```cpp
#include <gui_core/memory/gui_library_allocator.h>

{
    tachyon::LibraryAllocator::HeapScope heap;
    hb_language_from_string("fr", -1);   // HarfBuzz keeps it: made on the C heap
}
```

## Headless backends

- Headless means running without a GPU or without a display.
- `NullGraphicsContext` (`gui_core/graphics/null/gui_null_graphics_context.h`) is a
  graphics backend without a GPU:
  - it draws nothing
  - the UI is still built, laid out and recorded
  - windows stay empty
- `NullWindowBackend` is a window backend without an OS:
  - input arrives only through the `Inject*` calls
- Together they need no GPU and no display, for example in tests.

```cpp
app.SetGraphicsBackend(std::make_unique<tachyon::NullGraphicsContext>());
app.SetWindowBackend(std::make_unique<tachyon::NullWindowBackend>());
```

- They are also the smallest backends to start from when writing your own:
  - a graphics backend derives from `tachyon::GraphicsContext`
  - a window backend derives from `tachyon::IWindowBackend`

---

[Previous: Rendering the UI into a host image](render-to-image.md) | [Integration](../integration.md)
