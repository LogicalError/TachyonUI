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
- SDL's memory functions are shared by the whole process. SDL uses the hooks only if:
  - the GUI starts SDL
  - and SDL hasn't allocated anything yet
- The hooks must be thread-safe, because drivers call them from their own threads.
- They must outlive the GUI.
  - SDL keeps using them for a few allocations after `SDL_Quit`, so keep them alive for
    the whole process.

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
