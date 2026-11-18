#pragma once

// Draws the showcase's 3D scene (scene.h) into a Viewport's image with Vulkan, as an app's own
// renderer would. Built with the Vulkan backend only.

#include "scene.h"

#include <vulkan/vulkan.h>

#include <span>

namespace tachyon
{
    namespace vulkan { class VulkanGraphicsContext; struct VulkanSceneRender; }

    class SceneRenderer
    {
    public:
        /// Draws the grid and `objects` into the viewport's image; `selected` lighter. Makes
        /// its pipelines and meshes the first time, on `vk`'s device; they go when it's cleaned up.
        void Render(vulkan::VulkanGraphicsContext& vk, const vulkan::VulkanSceneRender& target,
                    const ViewportRenderContext& ctx, std::span<const SceneObject> objects, int selected);

    private:
        struct Range { uint32_t first = 0, count = 0; };

        bool Create(vulkan::VulkanGraphicsContext& vk, VkFormat colorFormat, VkFormat depthFormat);

        bool             m_created = false;
        bool             m_failed  = false;
        VkPipelineLayout m_layout = VK_NULL_HANDLE;
        VkPipeline       m_shapes = VK_NULL_HANDLE;   ///< lit triangles
        VkPipeline       m_lines  = VK_NULL_HANDLE;   ///< the grid
        VkDeviceAddress  m_vertices = 0;
        Range            m_meshes[3];
        Range            m_grid, m_axisX, m_axisZ;
    };
}
