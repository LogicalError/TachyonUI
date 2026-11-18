#pragma once

// Not installed: for the library's own tests, which run their whole-app GPU tests on another
// backend than the default.

#include <gui_core/graphics/gui_graphics_api.h>

#include <optional>

namespace tachyon
{
    /// The API CreateDefaultGraphicsContext makes from now on instead of the built-in default
    /// (an app that names one in InitializeOptions still gets that); empty restores it. Set it
    /// before any app starts. False, with nothing changed, when `api` isn't built in.
    bool SetDefaultGraphicsAPIOverride(std::optional<GraphicsAPI> api);
}
