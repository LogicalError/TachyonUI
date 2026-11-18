#pragma once

// For GPU tests that drive the app with injected input only.
//
// While a popup, menu or tooltip window is open, the app also reads the physical mouse
// buttons, to catch clicks the OS never delivered to it. In a test that injects its input,
// a click anywhere on the desktop while the test runs would then reach the test's windows:
// a menu it opened closes, a tooltip it waits for goes away. With physicalButtons off the
// physical buttons always read as up.

#include <gui_controls/gui_application.h>
#include <gui_core/windows/sdl/gui_window_backend_sdl.h>

#include <memory>

// Call before Initialize.
inline void UseInjectedInputOnly(tachyon::GUIApplication& app)
{
    tachyon::SDLWindowBackend::Options options;
    options.physicalButtons = false;
    app.SetWindowBackend(std::make_unique<tachyon::SDLWindowBackend>(options));
}
