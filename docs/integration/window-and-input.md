# Your own window and input

OS windows and input come from a `tachyon::IWindowBackend`
(`gui_core/windows/gui_window_backend.h`). Default: `SDLWindowBackend`, created by
`Initialize`. To use your own, set it before `Initialize`:

```cpp
app.SetWindowBackend(std::make_unique<MyWindowBackend>());
app.SetClipboard(std::make_unique<MyClipboard>());   // optional
```

The clipboard defaults to SDL's. Without SDL (`TACHYON_SDL=OFF`) it defaults to
`LocalClipboard`, and `Initialize` fails with `ErrorCode::Unsupported` unless you set a
window backend.

## Writing a window backend

A backend creates, moves, shows and destroys OS windows; reports their rects, display
scale and monitors; and delivers input. All sizes and positions are physical pixels.

- `CreateWindow` returns an `OSWindowHandle` (a backend-defined `void*`).
  `GetNativeHandle` returns what the graphics backend creates its surface from.
- `PollEvents` runs in `UpdateFrame`, and again in `RecordFrame` after the UI is built.
  It handles OS events and calls `cleanup` for windows the OS destroyed.
- Input goes to the `IPlatformInputHandler` from `AttachInputHandlerToWindow`
  (`gui_core/input/gui_platform_input_handler.h`): `OnMouseMove`, `OnMouseButton`,
  `OnMouseScroll`, `OnKeyboard`, `OnTextComposition`, `OnFileDrop`. Pass the window's ID
  from the `IWindowContext` attached with `AttachContextToWindow`. Positions are screen
  pixels.
- Typed text: `OnKeyboard` with `KeyAction::KeyChar`, codepoint as the key code.
- Drag from another application: `OnFileDrop` steps `Begin`, then `Move` to the pointer.
  On drop: a `File` per path, any `Text`, then `Complete`. `Complete` alone means the
  drag left the window. While a modal window is open, only `Complete` reaches windows
  behind it.
- The Vulkan backend creates each window's surface through a surface factory.
  `SDLWindowBackend` installs its own when the app calls `SetGraphicsContext` on it; other
  backends call `VulkanGraphicsContext::SetSurfaceFactory`.
  `VulkanGraphicsContext::Initialize` creates a hidden 64 x 64 window through the backend
  to pick a GPU that can present to it.

Simplest backend to read: `NullWindowBackend`
(`gui_core/windows/null/gui_null_window_backend.h`). Its windows are records with a rect;
no input arrives.

`Initialize` wraps your backend to add render-to-image windows, so `app.windowBackend` is
not the object you passed to `SetWindowBackend`.

## Running SDL's event loop yourself

Keep `SDLWindowBackend` but turn off its event pumping:

```cpp
#include <gui_core/windows/sdl/gui_window_backend_sdl.h>

tachyon::SDLWindowBackend::Options options;
options.pumpEvents = false;
app.SetWindowBackend(std::make_unique<tachyon::SDLWindowBackend>(options));
```

- The backend reads events through SDL event watches, so your `SDL_SetEventFilter` filter
  stays in place.
- `pumpEvents` on (default): `PollEvents` pumps SDL and empties its queue.
- `pumpEvents` off: GUI windows get their events whenever you pump (`SDL_PollEvent`,
  `SDL_PumpEvents`); your events stay in the queue. You handle `SDL_EVENT_QUIT`; the GUI
  won't close its windows on it. Pump before `UpdateFrame`.
- If SDL video is already running, the backend uses it and leaves it running. Otherwise
  it starts SDL and quits it when destroyed.

Tested in `tests/gpu/gpu_sdl_host_event_loop_test.cpp`.

`Options::hooks` routes SDL's memory through allocation hooks (see
[Allocation hooks and headless backends](allocation-and-headless.md)). Set automatically
when the app creates the backend; pass them yourself if you create it.

---

[Previous: Using your own Vulkan device](vulkan-device.md) | [Integration](../integration.md) | [Next: Your own loop](frame-loop.md)
