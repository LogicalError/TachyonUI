#include <gui_core/graphics/gui_default_graphics_context.h>

#if TACHYON_VULKAN
#include <gui_core/graphics/vulkan/vk_graphics_context.h>
#else
#include <gui_core/graphics/null/gui_null_graphics_context.h>
#endif

namespace tachyon
{
    std::unique_ptr<GraphicsContext> CreateDefaultGraphicsContext()
    {
#if TACHYON_VULKAN
        return std::make_unique<vulkan::VulkanGraphicsContext>();
#else
        return std::make_unique<NullGraphicsContext>();
#endif
    }
}
