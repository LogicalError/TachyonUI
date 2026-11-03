# Allocation hooks and headless backends

## Allocation hooks

If your program tracks its memory, you can give the library your own memory functions.
Do this before `Initialize`:

```cpp
#include <gui_core/memory/gui_allocation_hooks.h>

tachyon::AllocationHooks hooks;
hooks.allocate   = &MyAllocate;     // void* (size_t size, size_t alignment, void* user)
hooks.reallocate = &MyReallocate;   // void* (void* ptr, size_t size, size_t alignment, void* user)
hooks.free       = &MyFree;         // void  (void* ptr, void* user)
hooks.user       = &myHeap;
app.SetAllocationHooks(hooks);
```

The hooks receive every allocation that can be redirected: Vulkan objects, VMA,
vk-bootstrap and SDL. SDL's memory functions apply to the whole process, so SDL's
memory only goes through the hooks if the GUI is the one that starts SDL and SDL hasn't
allocated anything yet.

The hooks have to outlive the GUI, and they have to be thread-safe, because drivers may
call them from their own threads. SDL can hold on to a few allocations after `SDL_Quit`
and keeps using the hooks for them, so it's best to keep the hooks alive for as long as
the process runs.

`tachyon::TrackingAllocator` (`gui_core/memory/gui_tracking_allocator.h`) is a ready-made
set of hooks that counts live allocations and reports leaks. `Hooks()` returns them.


## Headless backends

`NullGraphicsContext` (`gui_core/graphics/null/gui_null_graphics_context.h`) draws
nothing and doesn't need a GPU. The UI is still built, laid out and recorded as usual,
but the windows stay empty. Add `NullWindowBackend` and there's no OS involved either,
so input only comes from the `Inject*` calls. Used together, they run an app with no
GPU and no display, which is handy for tests:

```cpp
app.SetGraphicsBackend(std::make_unique<tachyon::NullGraphicsContext>());
app.SetWindowBackend(std::make_unique<tachyon::NullWindowBackend>());
```

They're also the smallest examples of a graphics backend (`tachyon::GraphicsContext`)
and a window backend, so they're a good place to start if you're writing your own.

---

[Previous: Rendering the UI into a host image](render-to-image.md) | [Integration](../integration.md)
