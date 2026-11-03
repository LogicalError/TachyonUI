# Allocation hooks and headless backends

## Allocation hooks

Route the library's allocations through your own functions. Set before `Initialize`:

```cpp
#include <gui_core/memory/gui_allocation_hooks.h>

tachyon::AllocationHooks hooks;
hooks.allocate   = &MyAllocate;     // void* (size_t size, size_t alignment, void* user)
hooks.reallocate = &MyReallocate;   // void* (void* ptr, size_t size, size_t alignment, void* user)
hooks.free       = &MyFree;         // void  (void* ptr, void* user)
hooks.user       = &myHeap;
app.SetAllocationHooks(hooks);
```

- Covers Vulkan objects, VMA, vk-bootstrap and SDL.
- SDL's memory functions are process-wide: SDL uses the hooks only if the GUI starts SDL
  and SDL hasn't allocated yet.
- Hooks must be thread-safe (drivers call them from their own threads) and outlive the
  GUI. SDL keeps using them for a few allocations after `SDL_Quit`, so keep them alive for
  the whole process.

`tachyon::TrackingAllocator` (`gui_core/memory/gui_tracking_allocator.h`): ready-made
hooks that count live allocations and report leaks. `Hooks()` returns them.

## Headless backends

- `NullGraphicsContext` (`gui_core/graphics/null/gui_null_graphics_context.h`): no GPU,
  draws nothing. The UI is still built, laid out and recorded; windows stay empty.
- `NullWindowBackend`: no OS; input only via the `Inject*` calls.

Together: no GPU and no display, e.g. for tests.

```cpp
app.SetGraphicsBackend(std::make_unique<tachyon::NullGraphicsContext>());
app.SetWindowBackend(std::make_unique<tachyon::NullWindowBackend>());
```

They are also the smallest graphics backend (`tachyon::GraphicsContext`) and window
backend to start from when writing your own.

---

[Previous: Rendering the UI into a host image](render-to-image.md) | [Integration](../integration.md)
