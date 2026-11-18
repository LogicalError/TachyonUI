#include <gui_core/graphics/gui_default_graphics_context.h>

#include <gui_core/graphics/null/gui_null_graphics_context.h>
#if TACHYON_VULKAN
#include <gui_core/graphics/vulkan/vk_graphics_context.h>
#endif
#if TACHYON_D3D12
#include <gui_core/graphics/d3d12/d3d12_graphics_context.h>
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
        default:
            return nullptr;
        }
    }

    std::unique_ptr<GraphicsContext> CreateDefaultGraphicsContext()
    {
        return CreateGraphicsContext(TACHYON_VULKAN ? GraphicsAPI::Vulkan : GraphicsAPI::None);
    }

    std::string_view GraphicsAPIBuildOption(GraphicsAPI api)
    {
        switch (api)
        {
        case GraphicsAPI::Vulkan: return "TACHYON_VULKAN";
        case GraphicsAPI::D3D12:  return "TACHYON_D3D12";
        default:                  return {};
        }
    }
}
