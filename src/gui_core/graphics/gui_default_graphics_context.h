#pragma once

// The graphics backend an app gets when it doesn't choose one.

#include <gui_core/graphics/gui_graphics_context.h>

#include <memory>

namespace tachyon
{
    /// The default graphics backend, not yet initialized: Vulkan when it's built in
    /// (TACHYON_VULKAN), else the null backend, which draws nothing.
    std::unique_ptr<GraphicsContext> CreateDefaultGraphicsContext();
}
