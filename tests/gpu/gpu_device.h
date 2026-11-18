#pragma once

// For GPU tests of a whole app, on whichever graphics backend is the default.

#include <gui_core/graphics/gui_default_graphics_context.h>
#include <gui_core/windows/sdl/gui_window_backend_sdl.h>

// Whether the default graphics backend finds a device and display the GUI can use, checked
// with the backend alone. A test of a whole app skips on this, so that a failure in the app's
// own setup (theme, fonts, shaders) fails the test instead of skipping it.
inline bool GpuDeviceAvailable()
{
    const std::unique_ptr<tachyon::GraphicsContext> graphics = tachyon::CreateDefaultGraphicsContext();
    if (graphics->GetAPI() == tachyon::GraphicsAPI::None)
        return false;
    tachyon::SDLWindowBackend windows;
    windows.SetGraphicsContext(graphics.get());
    const bool available = static_cast<bool>(graphics->Initialize(&windows));
    graphics->Cleanup();
    return available;
}
