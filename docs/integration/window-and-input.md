# Your own window and input

The GUI gets its OS windows and their input from a window backend, which is a
`tachyon::IWindowBackend` (`gui_core/windows/gui_window_backend.h`). If you don't
provide one, `Initialize` creates an `SDLWindowBackend`. To use your own, pass it in
before `Initialize`:

```cpp
app.SetWindowBackend(std::make_unique<MyWindowBackend>());
app.SetClipboard(std::make_unique<MyClipboard>());   // optional
```

The clipboard defaults to SDL's. In builds without SDL (`TACHYON_SDL=OFF`) it defaults
to a `LocalClipboard`, and `Initialize` fails with `ErrorCode::Unsupported` unless you
passed in a window backend.

## Writing a window backend

A backend creates, moves, shows and destroys OS windows. It reports their rects, the
display scale and the monitors, and it delivers input. All sizes and positions are in
physical pixels. These are the parts that matter most:

- `CreateWindow` returns an `OSWindowHandle`, which is a `void*` the backend chooses.
  `GetNativeHandle` returns what the graphics backend creates its surface from.
- `PollEvents` runs in `UpdateFrame`, and again in `RecordFrame` after the UI is built.
  It handles the OS's events and calls `cleanup` for any windows the OS destroyed.
- Input goes to the `IPlatformInputHandler` you get from `AttachInputHandlerToWindow`
  (`gui_core/input/gui_platform_input_handler.h`). Its methods are `OnMouseMove`,
  `OnMouseButton`, `OnMouseScroll`, `OnKeyboard`, `OnTextComposition` and `OnFileDrop`.
  The ID you pass is the window's ID, from the `IWindowContext` attached with
  `AttachContextToWindow`. Positions are in screen pixels.
- Typed text arrives as `OnKeyboard` with `KeyAction::KeyChar`, with the codepoint as
  the key code.
- A drag from another application arrives as a series of `OnFileDrop` steps. It starts
  with `Begin`, then `Move` to wherever the pointer is. If the user drops, a `File`
  follows for each path, plus any `Text`, and then `Complete`. A `Complete` with nothing
  before it means the drag left the window. While a modal window is open, only
  `Complete` gets through to the windows behind it, so the drag ends without dropping
  anything there.
- The Vulkan backend creates a surface for each window through a surface factory.
  `SDLWindowBackend` installs its own when the app calls `SetGraphicsContext` on it.
  Any other backend installs one with `VulkanGraphicsContext::SetSurfaceFactory`.
  `VulkanGraphicsContext::Initialize` creates a hidden 64 x 64 window through the
  backend so it can pick a GPU that's able to present to it.

`NullWindowBackend` (`gui_core/windows/null/gui_null_window_backend.h`) is the smallest
backend, and the best one to read first. Its windows are just records with a rect, and
no input ever arrives.

`Initialize` wraps your backend to add render-to-image windows. That means
`app.windowBackend` isn't the same object you passed to `SetWindowBackend`.

## Running SDL's event loop yourself

If your program already uses SDL, you can keep the `SDLWindowBackend` and turn off its
event pumping:

```cpp
#include <gui_core/windows/sdl/gui_window_backend_sdl.h>

tachyon::SDLWindowBackend::Options options;
options.pumpEvents = false;
app.SetWindowBackend(std::make_unique<tachyon::SDLWindowBackend>(options));
```

The backend reads events through SDL event watches, not through the event filter, so
your filter (`SDL_SetEventFilter`) stays in place. With `pumpEvents` on, which is the
default, `PollEvents` pumps SDL and empties its event queue. With it off, the GUI's
windows still get their events whenever you pump (`SDL_PollEvent`, `SDL_PumpEvents`),
and your own events stay in the queue for you. Handling `SDL_EVENT_QUIT` is then up to
you, and the GUI won't close its windows when it sees one. Pump before `UpdateFrame` so
the frame sees that input.

If SDL's video subsystem is already running, the backend uses it and leaves it running.
Otherwise it starts SDL itself, and quits it again when the backend is destroyed.
`tests/gpu/gpu_sdl_host_event_loop_test.cpp` checks both the filter and the queue.

`Options::hooks` sends SDL's memory through allocation hooks (see
[Allocation hooks and headless backends](allocation-and-headless.md)). When the app
creates the backend, it sets this up for you. If you create the backend yourself, pass
the hooks here.

---

[Previous: Using your own Vulkan device](vulkan-device.md) | [Integration](../integration.md) | [Next: Your own loop](frame-loop.md)
