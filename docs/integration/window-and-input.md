# Your own window and input

- A window backend is the part of the GUI that talks to the OS:
  - it creates the OS windows
  - it delivers mouse, keyboard and other input
- Every window backend implements `tachyon::IWindowBackend`
  (`gui_core/windows/gui_window_backend.h`).
- The default one is `SDLWindowBackend`, which `Initialize` creates.
- To use your own, set it before `Initialize`:

```cpp
app.SetWindowBackend(std::make_unique<MyWindowBackend>());
app.SetClipboard(std::make_unique<MyClipboard>());   // optional
```

- The clipboard is what copy and paste in text fields use.
  - It defaults to SDL's.
- Without SDL, when built with `TACHYON_SDL=OFF`:
  - the clipboard defaults to `LocalClipboard`
  - `Initialize` fails with `ErrorCode::Unsupported` unless you set a window backend

## Writing a window backend

- A backend:
  - creates, moves, shows and destroys OS windows
  - reports their rects, their display scale and the monitors
  - delivers input
- All sizes and positions are in physical pixels.
- `CreateWindow` returns an `OSWindowHandle`, a `void*` whose meaning the backend decides.
- `GetNativeHandle` returns what the graphics backend creates its surface from.

```cpp
class MyWindowBackend : public tachyon::IWindowBackend
{
public:
    tachyon::OSWindowHandle CreateWindow(std::string_view title, tachyon::int2 size,
                                         tachyon::WindowFlags flags, tachyon::OSWindowHandle parent) override;
    void* GetNativeHandle(tachyon::OSWindowHandle window) override;
    // ... the other functions of IWindowBackend
};
```

- `PollEvents` runs in `UpdateFrame`, and again in `RecordFrame` after the UI is built.
  - It handles OS events.
  - It calls `cleanup` for each window the OS destroyed.
- Input goes to the `IPlatformInputHandler` from `AttachInputHandlerToWindow`
  (`gui_core/input/gui_platform_input_handler.h`). Its functions are:
  - `OnMouseMove`, `OnMouseButton`, `OnMouseScroll`
  - `OnKeyboard`, `OnTextComposition`
  - `OnFileDrop`
- Pass each call the window's ID, from the `IWindowContext` attached with
  `AttachContextToWindow`.
- Positions are in screen pixels.

```cpp
// A mouse move over `window`, at a position in screen pixels
const tachyon::ID id = context->GetID();   // context: from AttachContextToWindow
inputHandler->OnMouseMove(id, screenPos, buttons, modifiers);
```

- Typed text arrives as `OnKeyboard` with `KeyAction::KeyChar`, the codepoint as the key
  code.

```cpp
// The user typed codepoint U+00E9
inputHandler->OnKeyboard(id, tachyon::KeyCode(0x00E9), 0, modifiers, tachyon::KeyAction::KeyChar);
```

- A drag from another application calls `OnFileDrop` in steps:
  - `Begin` when it enters the window
  - `Move` as the pointer moves
  - on drop: a `File` for each path, any `Text`, then `Complete`
  - `Complete` alone means the drag left the window
  - while a modal window is open, only `Complete` reaches the windows behind it
- The Vulkan backend creates each window's surface through a surface factory, a function
  that makes a `VkSurfaceKHR` for one window.
  - `SDLWindowBackend` installs its own when the app calls `SetGraphicsContext` on it.
  - Other backends call `VulkanGraphicsContext::SetSurfaceFactory`.

```cpp
vk->SetSurfaceFactory([](VkInstance instance, const VkAllocationCallbacks* callbacks,
                         tachyon::OSWindowHandle window) -> VkSurfaceKHR
{
    return MyCreateSurface(instance, callbacks, window);   // VK_NULL_HANDLE on failure
});
```

- `VulkanGraphicsContext::Initialize` creates a hidden 64 x 64 window through the backend,
  to pick a GPU that can present to it.
- The simplest backend to read is `NullWindowBackend`
  (`gui_core/windows/null/gui_null_window_backend.h`).
  - Its windows are only records with a rect.
  - No input arrives.
- `Initialize` wraps your backend to add render-to-image windows (see
  [Rendering the UI into a host image](render-to-image.md)).
  - So `app.windowBackend` is not the object you passed to `SetWindowBackend`.

## Running SDL's event loop yourself

Keep `SDLWindowBackend` but turn off its event pumping:

```cpp
#include <gui_core/windows/sdl/gui_window_backend_sdl.h>

tachyon::SDLWindowBackend::Options options;
options.pumpEvents = false;
app.SetWindowBackend(std::make_unique<tachyon::SDLWindowBackend>(options));
```

- The backend reads events through SDL event watches.
  - So a filter you set with `SDL_SetEventFilter` stays in place.
- With `pumpEvents` on, the default:
  - `PollEvents` pumps SDL and empties its queue
- With `pumpEvents` off:
  - GUI windows get their events whenever you pump, with `SDL_PollEvent` or
    `SDL_PumpEvents`
  - your own events stay in the queue
  - you handle `SDL_EVENT_QUIT`; the GUI won't close its windows on it
  - pump before `UpdateFrame`

```cpp
while (true)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
        if (event.type == SDL_EVENT_QUIT)
            app.applicationQuit = true;
    if (!app.UpdateFrame())
        break;
    if (!app.RecordFrame())
        break;
    app.PresentFrame();
}
```

- If SDL video is already running, the backend uses it and leaves it running.
- Otherwise it starts SDL, and quits it when destroyed.
- Tested in `tests/gpu/gpu_sdl_host_event_loop_test.cpp`.

- `Options::hooks` routes SDL's memory through allocation hooks (see
  [Allocation hooks and headless backends](allocation-and-headless.md)).
  - When the app creates the backend, it sets them for you.
  - When you create the backend, pass them yourself.

```cpp
tachyon::SDLWindowBackend::Options options;
options.hooks = &hooks;   // the same AllocationHooks you give SetAllocationHooks
```

---

[Previous: Using your own Vulkan device](vulkan-device.md) | [Integration](../integration.md) | [Next: Your own loop](frame-loop.md)
