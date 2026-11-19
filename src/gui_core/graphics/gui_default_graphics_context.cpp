#include <gui_core/graphics/gui_default_graphics_context.h>
#include <gui_core/graphics/gui_default_graphics_context_internal.h>

#include <gui_core/graphics/null/gui_null_graphics_context.h>
#if TACHYON_VULKAN
#include <gui_core/graphics/vulkan/vk_graphics_context.h>
#endif
#if TACHYON_D3D12
#include <gui_core/graphics/d3d12/d3d12_graphics_context.h>
#endif
#if TACHYON_SDLGPU
#include <gui_core/graphics/sdlgpu/sdlgpu_graphics_context.h>
#endif

namespace tachyon
{
    std::unique_ptr<GraphicsContext> CreateGraphicsContext(GraphicsAPI api)
    {
        switch (api)
        {
        case GraphicsAPI::None:
            return std::make_unique<NullGraphicsContext>();
#if TACHYON_VULKAN
        case GraphicsAPI::Vulkan:
            return std::make_unique<vulkan::VulkanGraphicsContext>();
#endif
#if TACHYON_D3D12
        case GraphicsAPI::D3D12:
            return std::make_unique<d3d12::D3D12GraphicsContext>();
#endif
#if TACHYON_SDLGPU
        case GraphicsAPI::SDLGPU:
            return std::make_unique<sdlgpu::SDLGPUGraphicsContext>();
#endif
        default:
            return nullptr;
        }
    }

    namespace
    {
        std::optional<GraphicsAPI> g_defaultOverride;

        constexpr bool GraphicsAPIBuilt(GraphicsAPI api)
        {
            switch (api)
            {
            case GraphicsAPI::None:   return true;
            case GraphicsAPI::Vulkan: return TACHYON_VULKAN != 0;
            case GraphicsAPI::D3D12:  return TACHYON_D3D12 != 0;
            case GraphicsAPI::SDLGPU: return TACHYON_SDLGPU != 0;
            default:                  return false;
            }
        }
    }

    std::unique_ptr<GraphicsContext> CreateDefaultGraphicsContext()
    {
        return CreateGraphicsContext(g_defaultOverride.value_or(TACHYON_VULKAN ? GraphicsAPI::Vulkan : GraphicsAPI::None));
    }

    bool SetDefaultGraphicsAPIOverride(std::optional<GraphicsAPI> api)
    {
        // Whether the build has the API, without making a context to find out.
        if (api && !GraphicsAPIBuilt(*api))
            return false;
        g_defaultOverride = api;
        return true;
    }

    std::string_view GraphicsAPIBuildOption(GraphicsAPI api)
    {
        switch (api)
        {
        case GraphicsAPI::Vulkan: return "TACHYON_VULKAN";
        case GraphicsAPI::D3D12:  return "TACHYON_D3D12";
        case GraphicsAPI::SDLGPU: return "TACHYON_SDLGPU";
        default:                  return {};
        }
    }
}
