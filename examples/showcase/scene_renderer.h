#pragma once

// The showcase's 3D scene: a few shapes on a ground grid, drawn into a Viewport's image with
// Vulkan, as an app's own renderer would.

#include <gui_controls/gui_viewport_types.h>
#include <gui_core/gui_math3d.h>

#include <vulkan/vulkan.h>

#include <span>
#include <string>

namespace tachyon
{
    namespace vulkan { class VulkanGraphicsContext; struct VulkanSceneRender; }

    enum class SceneMesh : uint8_t { Cube, Sphere, Cone };

    /// A shape in the scene, one unit across before its scale.
    struct SceneObject
    {
        std::string       name;
        SceneMesh         mesh = SceneMesh::Cube;
        ViewportTransform transform;
        color4f           color { 0.8f, 0.8f, 0.8f, 1.0f };
    };

    /// How far round an object reaches from its middle, for picking and framing it.
    float SceneObjectRadius(const SceneObject& object);
    /// The object `ray` hits first (by the sphere round each); -1 when it hits none.
    int PickSceneObject(std::span<const SceneObject> objects, const Ray& ray);

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
