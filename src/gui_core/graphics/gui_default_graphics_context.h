#pragma once

// The graphics backends built in, made by the API they render with.

#include <gui_core/graphics/gui_graphics_context.h>

#include <memory>
#include <string_view>

namespace tachyon
{
    /// A graphics backend for `api`, not yet initialized: GraphicsAPI::None is the null
    /// backend, which draws nothing. Null when that API isn't built in (TACHYON_VULKAN,
    /// TACHYON_D3D12).
    std::unique_ptr<GraphicsContext> CreateGraphicsContext(GraphicsAPI api);

    /// The default graphics backend, not yet initialized: Metal when it's built in
    /// (TACHYON_METAL, macOS), else Vulkan when it is (TACHYON_VULKAN), else the null backend.
    std::unique_ptr<GraphicsContext> CreateDefaultGraphicsContext();

    /// The build option that builds `api`'s backend in: "TACHYON_VULKAN", "TACHYON_D3D12",
    /// "TACHYON_METAL", "TACHYON_SDLGPU".
    /// Empty for None, which is always there.
    std::string_view GraphicsAPIBuildOption(GraphicsAPI api);
}
