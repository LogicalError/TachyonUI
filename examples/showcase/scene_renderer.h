#pragma once

// Draws the showcase's 3D scene (scene.h) into a Viewport's image, as an app's own renderer
// would: with Vulkan, Direct3D 12 or SDL GPU, whichever the scene's recording state is. Built
// with any of the backends.

#include "scene.h"

#if TACHYON_VULKAN
#include <vulkan/vulkan.h>
#endif

#include <span>
#include <vector>

struct ID3D12RootSignature;
struct ID3D12PipelineState;
struct ID3D12Resource;
struct SDL_GPUDevice;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUBuffer;

namespace tachyon
{
    namespace vulkan { class VulkanGraphicsContext; struct VulkanSceneRender; }
    namespace d3d12 { struct D3D12SceneRender; }
    namespace sdlgpu { struct SDLGPUSceneRender; }

    class SceneRenderer
    {
    public:
        SceneRenderer() = default;
        SceneRenderer(const SceneRenderer&) = delete;
        SceneRenderer& operator=(const SceneRenderer&) = delete;
        ~SceneRenderer() { ReleaseD3D12(); ReleaseSDLGPU(); }

#if TACHYON_VULKAN
        /// Draws the grid and `objects` into the viewport's image; `selected` lighter. Makes
        /// its pipelines and meshes the first time, on `vk`'s device; they go when it's cleaned up.
        void Render(vulkan::VulkanGraphicsContext& vk, const vulkan::VulkanSceneRender& target,
                    const ViewportRenderContext& ctx, std::span<const SceneObject> objects, int selected);
#endif
#if TACHYON_D3D12
        /// As above on Direct3D 12, on the device of the scene's command list. Its objects
        /// stay until ReleaseD3D12.
        void Render(const d3d12::D3D12SceneRender& target, const ViewportRenderContext& ctx,
                    std::span<const SceneObject> objects, int selected);
#endif
#if TACHYON_SDLGPU
        /// As above on SDL GPU, on the scene's device: its upload in a copy pass of the scene's
        /// command buffer the first time, then a render pass. Its objects stay until ReleaseSDLGPU.
        void Render(const sdlgpu::SDLGPUSceneRender& target, const ViewportRenderContext& ctx,
                    std::span<const SceneObject> objects, int selected);
#endif
        /// Lets go of the Direct3D 12 objects. Call once the GPU is done with them, before the
        /// graphics backend is cleaned up.
        void ReleaseD3D12();
        /// Lets go of the SDL GPU objects (SDL frees them once the GPU is done with them). Call
        /// before the graphics backend is cleaned up.
        void ReleaseSDLGPU();

        /// A mesh's vertex, as scene.slang reads it.
        struct Vertex
        {
            float4 position;
            float4 normal;
        };

    private:
        struct Range { uint32_t first = 0, count = 0; };

        /// The meshes and the grid, all in one buffer; sets their ranges.
        std::vector<Vertex> BuildMeshes();
        /// The scene's draws in order, for either API: `use(lines)` picks the lines' pipeline or
        /// the shapes', `draw(range, mvp, color, light)` draws a range of vertices.
        template <class Use, class Draw>
        void Walk(const ViewportRenderContext& ctx, std::span<const SceneObject> objects, int selected, Use&& use, Draw&& draw) const;

        bool m_created = false;
        bool m_failed  = false;
        Range m_meshes[3];
        Range m_grid, m_axisX, m_axisZ;

#if TACHYON_VULKAN
        bool             CreateVulkan(vulkan::VulkanGraphicsContext& vk, VkFormat colorFormat, VkFormat depthFormat);
        VkPipelineLayout m_layout = VK_NULL_HANDLE;
        VkPipeline       m_shapes = VK_NULL_HANDLE;   ///< lit triangles
        VkPipeline       m_lines  = VK_NULL_HANDLE;   ///< the grid
        VkDeviceAddress  m_vertices = 0;
#endif
#if TACHYON_D3D12
        bool CreateD3D12(const d3d12::D3D12SceneRender& target);
        ID3D12RootSignature* m_d3dRoot     = nullptr;
        ID3D12PipelineState* m_d3dShapes   = nullptr;
        ID3D12PipelineState* m_d3dLines    = nullptr;
        ID3D12Resource*      m_d3dVertices = nullptr;   ///< in an upload heap, read as t0
#endif

#if TACHYON_SDLGPU
        bool CreateSDLGPU(const sdlgpu::SDLGPUSceneRender& target);
        SDL_GPUDevice*           m_sdlDevice   = nullptr;
        SDL_GPUGraphicsPipeline* m_sdlShapes   = nullptr;
        SDL_GPUGraphicsPipeline* m_sdlLines    = nullptr;
        SDL_GPUBuffer*           m_sdlVertices = nullptr;   ///< the vertex stage's storage buffer
#endif
    };
}
