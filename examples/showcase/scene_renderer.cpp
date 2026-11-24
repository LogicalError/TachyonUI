#include "scene_renderer.h"

#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/gui_embedded_files_internal.h>
#if TACHYON_VULKAN
#include <gui_core/graphics/vulkan/vk_graphics_context.h>
#include <gui_core/graphics/vulkan/vk_pipelines.h>
#endif
#if TACHYON_D3D12
#include <gui_core/graphics/d3d12/d3d12_graphics_context.h>
#include <d3d12.h>
#endif
#if TACHYON_SDLGPU
#include <gui_core/graphics/sdlgpu/sdlgpu_graphics_context.h>
#include <SDL3/SDL_gpu.h>
#endif
#if TACHYON_METAL
// This file is compiled as Objective-C++ with Metal (examples/showcase/CMakeLists.txt).
#include <gui_core/graphics/metal/metal_graphics_context.h>
#import <Metal/Metal.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <string>
#include <vector>

static constexpr tachyon::Diagnostics::LogCategory kLogScene = tachyon::Diagnostics::MakeLogCategory("showcase");

namespace tachyon::detail
{
    /// The showcase's compiled shaders (scene.vert.spv and scene.frag.spv with Vulkan,
    /// scene.vert.dxil and scene.frag.dxil with Direct3D 12, scene.*.sdl.spv, .sdl.dxil and .sdl.metal
    /// with SDL GPU, scene.vert.metal and scene.frag.metal with Metal), built in by CMake.
    std::span<const EmbeddedFile> ShowcaseShaderFiles();
}

namespace tachyon
{
    namespace
    {
        constexpr float kPi = std::numbers::pi_v<float>;

        using Vertex = SceneRenderer::Vertex;

#if TACHYON_VULKAN
        struct Push
        {
            float4x4        mvp;
            color4f         color;
            float4          light;
            VkDeviceAddress vertices;
        };
#endif
#if TACHYON_D3D12 || TACHYON_SDLGPU || TACHYON_METAL
        // Direct3D 12's root constants (b0), SDL GPU's uniforms, Metal's buffer(0): Push's
        // values, and the draw's first vertex.
        struct FirstVertexPush
        {
            float4x4 mvp;
            color4f  color;
            float4   light;
            uint32_t firstVertex;
        };
        static_assert(sizeof(FirstVertexPush) == 25 * 4);
#endif

        void Triangle(std::vector<Vertex>& out, float3 a, float3 b, float3 c, float3 n)
        {
            for (const float3 p : { a, b, c })
                out.push_back({ { p.x, p.y, p.z, 1 }, { n.x, n.y, n.z, 0 } });
        }

        void Cube(std::vector<Vertex>& out)
        {
            for (int axis = 0; axis < 3; ++axis)
                for (const float side : { -1.0f, 1.0f })
                {
                    float3 n{}; n[axis] = side;
                    float3 u{}; u[(axis + 1) % 3] = 0.5f;
                    float3 v{}; v[(axis + 2) % 3] = 0.5f;
                    const float3 c = n * 0.5f;
                    // Counterclockwise seen from outside.
                    if (side > 0) { Triangle(out, c - u - v, c + u - v, c + u + v, n); Triangle(out, c - u - v, c + u + v, c - u + v, n); }
                    else          { Triangle(out, c - u - v, c + u + v, c + u - v, n); Triangle(out, c - u - v, c - u + v, c + u + v, n); }
                }
        }

        void Sphere(std::vector<Vertex>& out)
        {
            constexpr int rings = 16, segments = 32;
            auto at = [](int ring, int segment) {
                const float theta = kPi * float(ring) / float(rings), phi = 2.0f * kPi * float(segment) / float(segments);
                return float3{ std::sin(theta) * std::cos(phi), std::cos(theta), -std::sin(theta) * std::sin(phi) } * 0.5f;
            };
            for (int r = 0; r < rings; ++r)
                for (int s = 0; s < segments; ++s)
                {
                    const float3 a = at(r, s), b = at(r + 1, s), c = at(r + 1, s + 1), d = at(r, s + 1);
                    auto push = [&](float3 p) { const float3 n = Normalize(p); out.push_back({ { p.x, p.y, p.z, 1 }, { n.x, n.y, n.z, 0 } }); };
                    push(a); push(b); push(c);
                    push(a); push(c); push(d);
                }
        }

        void Cone(std::vector<Vertex>& out)
        {
            constexpr int segments = 32;
            const float3 tip{ 0, 0.5f, 0 }, base{ 0, -0.5f, 0 };
            for (int s = 0; s < segments; ++s)
            {
                const float a0 = 2.0f * kPi * float(s) / float(segments), a1 = 2.0f * kPi * float(s + 1) / float(segments);
                const float3 p0{ std::cos(a0) * 0.5f, -0.5f, -std::sin(a0) * 0.5f }, p1{ std::cos(a1) * 0.5f, -0.5f, -std::sin(a1) * 0.5f };
                const float mid = (a0 + a1) * 0.5f;
                Triangle(out, p0, p1, tip, Normalize({ std::cos(mid), 0.5f, -std::sin(mid) }));
                Triangle(out, base, p1, p0, { 0, -1, 0 });
            }
        }

#if TACHYON_VULKAN
        VkShaderModule Module(VkDevice device, const VkAllocationCallbacks* callbacks, std::string_view name)
        {
            const std::span<const std::byte> code = detail::FindEmbeddedFile(detail::ShowcaseShaderFiles(), name);
            if (code.empty())
                return VK_NULL_HANDLE;
            VkShaderModuleCreateInfo info{ .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            info.codeSize = code.size();
            info.pCode = reinterpret_cast<const uint32_t*>(code.data());
            VkShaderModule module = VK_NULL_HANDLE;
            return vkCreateShaderModule(device, &info, callbacks, &module) == VK_SUCCESS ? module : VK_NULL_HANDLE;
        }
#endif
    }

    std::vector<SceneRenderer::Vertex> SceneRenderer::BuildMeshes()
    {
        // The meshes, all in one buffer the shaders read.
        std::vector<Vertex> vertices;
        auto add = [&](auto&& build) {
            const uint32_t first = uint32_t(vertices.size());
            build(vertices);
            return Range{ first, uint32_t(vertices.size()) - first };
        };
        m_meshes[size_t(SceneMesh::Cube)]   = add(Cube);
        m_meshes[size_t(SceneMesh::Sphere)] = add(Sphere);
        m_meshes[size_t(SceneMesh::Cone)]   = add(Cone);
        m_grid = add([](std::vector<Vertex>& out) {
            for (int i = -10; i <= 10; ++i)
            {
                if (i == 0) continue;   // the axes go there
                const float f = float(i);
                for (const float3 p : { float3{ f, 0, -10 }, float3{ f, 0, 10 }, float3{ -10, 0, f }, float3{ 10, 0, f } })
                    out.push_back({ { p.x, p.y, p.z, 1 }, {} });
            }
        });
        m_axisX = add([](std::vector<Vertex>& out) { out.push_back({ { -10, 0, 0, 1 }, {} }); out.push_back({ { 10, 0, 0, 1 }, {} }); });
        m_axisZ = add([](std::vector<Vertex>& out) { out.push_back({ { 0, 0, -10, 1 }, {} }); out.push_back({ { 0, 0, 10, 1 }, {} }); });
        return vertices;
    }

    template <class Use, class Draw>
    void SceneRenderer::Walk(const ViewportRenderContext& ctx, std::span<const SceneObject> objects, int selected,
                             Use&& use, Draw&& draw) const
    {
        const float4x4 viewProjection = ctx.projection * ctx.view;
        use(true);
        draw(m_grid,  viewProjection, color4f{ 0.35f, 0.38f, 0.44f, 0.6f }, float4{});
        draw(m_axisX, viewProjection, color4f{ 0.94f, 0.42f, 0.42f, 0.9f }, float4{});
        draw(m_axisZ, viewProjection, color4f{ 0.42f, 0.61f, 1.00f, 0.9f }, float4{});

        use(false);
        const float3 sun = Normalize({ 0.4f, 0.8f, 0.5f });
        for (int i = 0; i < (int)objects.size(); ++i)
        {
            const SceneObject& o = objects[size_t(i)];
            // The light in the object's own space, as its normals are.
            const quat inverse{ -o.transform.rotation.x, -o.transform.rotation.y, -o.transform.rotation.z, o.transform.rotation.w };
            const float3 light = inverse.Rotate(sun);
            color4f color = o.color;
            if (i == selected)
                color = { color.r + (1.0f - color.r) * 0.3f, color.g + (1.0f - color.g) * 0.3f, color.b + (1.0f - color.b) * 0.3f, color.a };
            draw(m_meshes[size_t(o.mesh)], viewProjection * o.transform.Matrix(), color, float4{ light.x, light.y, light.z, 1.0f });
        }
    }

#if TACHYON_VULKAN
    bool SceneRenderer::CreateVulkan(vulkan::VulkanGraphicsContext& vk, VkFormat colorFormat, VkFormat depthFormat)
    {
        vulkan::VulkanDeviceContext& dc = vk.deviceContext;
        const VkDevice device = dc.device;
        const VkAllocationCallbacks* callbacks = dc.AllocationCallbacks();

        const std::vector<Vertex> vertices = BuildMeshes();
        const vulkan::Buffer buffer = dc.CreateMappedStorageBuffer(vertices.data(), vertices.size() * sizeof(Vertex), m_vertices);

        const VkPushConstantRange range{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push) };
        VkPipelineLayoutCreateInfo layoutInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &range;
        if (vkCreatePipelineLayout(device, &layoutInfo, callbacks, &m_layout) != VK_SUCCESS)
        {
            dc.DestroyBuffer(buffer);
            return false;
        }

        const VkShaderModule vert = Module(device, callbacks, "scene.vert.spv");
        const VkShaderModule frag = Module(device, callbacks, "scene.frag.spv");
        if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE)
        {
            // What was made goes: nothing else would free it.
            if (vert != VK_NULL_HANDLE) vkDestroyShaderModule(device, vert, callbacks);
            if (frag != VK_NULL_HANDLE) vkDestroyShaderModule(device, frag, callbacks);
            vkDestroyPipelineLayout(device, m_layout, callbacks);
            m_layout = VK_NULL_HANDLE;
            dc.DestroyBuffer(buffer);
            return false;
        }
        PipelineBuilder builder;
        builder._pipelineLayout = m_layout;
        builder.set_shaders(vert, frag);
        builder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.set_polygon_mode(VK_POLYGON_MODE_FILL);
        builder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        builder.set_multisampling_none();
        builder.disable_blending();
        builder.enable_depthtest(true, VK_COMPARE_OP_LESS_OR_EQUAL);
        builder.set_color_attachment_format(colorFormat);
        builder.set_depth_format(depthFormat);
        m_shapes = builder.build_pipeline(device, callbacks);
        builder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
        builder.enable_blending_alphablend();
        builder.enable_depthtest(false, VK_COMPARE_OP_LESS_OR_EQUAL);
        m_lines = builder.build_pipeline(device, callbacks);
        vkDestroyShaderModule(device, vert, callbacks);
        vkDestroyShaderModule(device, frag, callbacks);

        // Gone with the app's other GPU objects, once nothing draws with them.
        VkPipelineLayout layout = m_layout;
        VkPipeline shapes = m_shapes, lines = m_lines;
        m_vkObjectsLive = std::make_shared<bool>(true);
        dc.GetDeletionQueue(DeviceScope::Application).push_function([&dc, device, callbacks, layout, shapes, lines, buffer,
                                                                     live = m_vkObjectsLive] {
            vkDestroyPipeline(device, shapes, callbacks);
            vkDestroyPipeline(device, lines, callbacks);
            vkDestroyPipelineLayout(device, layout, callbacks);
            dc.DestroyBuffer(buffer);
            *live = false;
        });
        return m_shapes != VK_NULL_HANDLE && m_lines != VK_NULL_HANDLE;
    }

    void SceneRenderer::Render(vulkan::VulkanGraphicsContext& vk, const vulkan::VulkanSceneRender& target,
                               const ViewportRenderContext& ctx, std::span<const SceneObject> objects, int selected)
    {
        if (m_failed)
            return;
        if (m_created && m_vkObjectsLive && !*m_vkObjectsLive)
            m_created = false;   // the device was remade: the pipelines are made again on it
        if (!m_created)
        {
            m_created = true;
            if (!CreateVulkan(vk, target.colorFormat, target.depthFormat))
            {
                m_failed = true;
                TACHYON_LOG_ERROR(kLogScene, "The scene's pipelines weren't made; the viewport shows its background only");
                return;
            }
        }
        const VkCommandBuffer cmd = target.commandBuffer;
        Walk(ctx, objects, selected,
            [&](bool lines) { vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lines ? m_lines : m_shapes); },
            [&](Range range, const float4x4& mvp, color4f color, float4 light) {
                const Push push{ mvp, color, light, m_vertices };
                vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push), &push);
                vkCmdDraw(cmd, range.count, 1, range.first, 0);
            });
    }
#endif

#if TACHYON_D3D12
    namespace
    {
        template <class T>
        void Release(T*& object)
        {
            if (object != nullptr)
                object->Release();
            object = nullptr;
        }
    }

    bool SceneRenderer::CreateD3D12(const d3d12::D3D12SceneRender& target)
    {
        ID3D12Device* device = nullptr;
        if (FAILED(target.commandList->GetDevice(IID_PPV_ARGS(&device))))
            return false;
        // Released at the end: what's made from the device holds it.
        struct DeviceRef { ID3D12Device* d; ~DeviceRef() { d->Release(); } } deviceRef{ device };

        // The meshes, in an upload heap the vertex shader reads (t0).
        const std::vector<Vertex> vertices = BuildMeshes();
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width            = vertices.size() * sizeof(Vertex);
        buffer.Height           = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels        = 1;
        buffer.SampleDesc       = { 1, 0 };
        buffer.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                   IID_PPV_ARGS(&m_d3dVertices))))
            return false;
        void* mapped = nullptr;
        const D3D12_RANGE none{ 0, 0 };
        if (FAILED(m_d3dVertices->Map(0, &none, &mapped)))
            return false;
        std::memcpy(mapped, vertices.data(), size_t(buffer.Width));
        m_d3dVertices->Unmap(0, nullptr);

        // The root constants (b0) and the vertices (t0), as scene.slang declares them.
        D3D12_ROOT_PARAMETER parameters[2]{};
        parameters[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants.Num32BitValues = sizeof(FirstVertexPush) / 4;
        parameters[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        parameters[1].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[1].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = 2;
        rootDesc.pParameters   = parameters;
        ID3DBlob* blob = nullptr;
        const bool serialized = SUCCEEDED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, nullptr));
        const bool rooted = serialized && SUCCEEDED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                                                                IID_PPV_ARGS(&m_d3dRoot)));
        if (blob != nullptr)
            blob->Release();
        if (!rooted)
            return false;

        const std::span<const std::byte> vs = detail::FindEmbeddedFile(detail::ShowcaseShaderFiles(), "scene.vert.dxil");
        const std::span<const std::byte> ps = detail::FindEmbeddedFile(detail::ShowcaseShaderFiles(), "scene.frag.dxil");
        if (vs.empty() || ps.empty())
            return false;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = m_d3dRoot;
        desc.VS = { vs.data(), vs.size() };
        desc.PS = { ps.data(), ps.size() };
        desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.SampleMask = UINT_MAX;
        desc.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable = TRUE;
        desc.DepthStencilState.DepthEnable    = TRUE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        desc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets      = 1;
        desc.RTVFormats[0]         = target.colorFormat;
        desc.DSVFormat             = target.depthFormat;
        desc.SampleDesc            = { 1, 0 };
        if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_d3dShapes))))
            return false;

        // The grid: alpha blended, over everything.
        D3D12_RENDER_TARGET_BLEND_DESC& blend = desc.BlendState.RenderTarget[0];
        blend.BlendEnable    = TRUE;
        blend.SrcBlend       = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOp        = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha  = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
        desc.DepthStencilState.DepthEnable    = FALSE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        return SUCCEEDED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_d3dLines)));
    }

    void SceneRenderer::Render(const d3d12::D3D12SceneRender& target, const ViewportRenderContext& ctx,
                               std::span<const SceneObject> objects, int selected)
    {
        if (m_failed)
            return;
        if (!m_created)
        {
            m_created = true;
            if (!CreateD3D12(target))
            {
                m_failed = true;
                ReleaseD3D12();
                TACHYON_LOG_ERROR(kLogScene, "The scene's pipelines weren't made; the viewport shows its background only");
                return;
            }
        }
        ID3D12GraphicsCommandList* list = target.commandList;
        list->SetGraphicsRootSignature(m_d3dRoot);
        list->SetGraphicsRootShaderResourceView(1, m_d3dVertices->GetGPUVirtualAddress());
        Walk(ctx, objects, selected,
            [&](bool lines) {
                list->SetPipelineState(lines ? m_d3dLines : m_d3dShapes);
                list->IASetPrimitiveTopology(lines ? D3D_PRIMITIVE_TOPOLOGY_LINELIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            },
            [&](Range range, const float4x4& mvp, color4f color, float4 light) {
                const FirstVertexPush push{ mvp, color, light, range.first };
                list->SetGraphicsRoot32BitConstants(0, sizeof(FirstVertexPush) / 4, &push, 0);
                list->DrawInstanced(range.count, 1, 0, 0);
            });
    }

    void SceneRenderer::ReleaseD3D12()
    {
        Release(m_d3dLines);
        Release(m_d3dShapes);
        Release(m_d3dRoot);
        Release(m_d3dVertices);
    }
#else
    void SceneRenderer::ReleaseD3D12() {}
#endif

#if TACHYON_SDLGPU
    namespace
    {
        SDL_GPUShader* SDLShader(SDL_GPUDevice* device, SDL_GPUShaderStage stage)
        {
            const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
            const SDL_GPUShaderFormat format  = (formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0 ? SDL_GPU_SHADERFORMAT_SPIRV
                                              : (formats & SDL_GPU_SHADERFORMAT_DXIL)  != 0 ? SDL_GPU_SHADERFORMAT_DXIL
                                                                                             : SDL_GPU_SHADERFORMAT_MSL;
            const bool spirv  = format == SDL_GPU_SHADERFORMAT_SPIRV;
            const bool vertex = stage == SDL_GPU_SHADERSTAGE_VERTEX;
            const char* extension = spirv ? ".sdl.spv" : format == SDL_GPU_SHADERFORMAT_MSL ? ".sdl.metal" : ".sdl.dxil";
            const std::string name = std::string("scene") + (vertex ? ".vert" : ".frag") + extension;
            const std::span<const std::byte> code = detail::FindEmbeddedFile(detail::ShowcaseShaderFiles(), name);
            if (code.empty())
                return nullptr;
            SDL_GPUShaderCreateInfo info{};
            info.code_size           = code.size();
            info.code                = reinterpret_cast<const Uint8*>(code.data());
            // Slang names SPIR-V's entry point main, and keeps DXIL's and Metal's.
            info.entrypoint          = spirv ? "main" : vertex ? "vertexMain" : "fragmentMain";
            info.format              = format;
            info.stage               = stage;
            info.num_storage_buffers = vertex ? 1 : 0;   // the vertices
            info.num_uniform_buffers = 1;
            return SDL_CreateGPUShader(device, &info);
        }
    }

    bool SceneRenderer::CreateSDLGPU(const sdlgpu::SDLGPUSceneRender& target)
    {
        SDL_GPUDevice* device = target.device;
        m_sdlDevice = device;

        // The meshes, copied into the vertex stage's storage buffer in a copy pass of the
        // scene's command buffer; the transfer buffer goes once that copy is done.
        const std::vector<Vertex> vertices = BuildMeshes();
        const uint32_t bytes = uint32_t(vertices.size() * sizeof(Vertex));
        const SDL_GPUBufferCreateInfo bufferInfo{ SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, bytes, 0 };
        m_sdlVertices = SDL_CreateGPUBuffer(device, &bufferInfo);
        const SDL_GPUTransferBufferCreateInfo uploadInfo{ SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bytes, 0 };
        SDL_GPUTransferBuffer* upload = m_sdlVertices != nullptr ? SDL_CreateGPUTransferBuffer(device, &uploadInfo) : nullptr;
        void* mapped = upload != nullptr ? SDL_MapGPUTransferBuffer(device, upload, false) : nullptr;
        if (mapped == nullptr)
        {
            if (upload != nullptr)
                SDL_ReleaseGPUTransferBuffer(device, upload);
            return false;
        }
        std::memcpy(mapped, vertices.data(), bytes);
        SDL_UnmapGPUTransferBuffer(device, upload);
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(target.commandBuffer);
        const SDL_GPUTransferBufferLocation from{ upload, 0 };
        const SDL_GPUBufferRegion to{ m_sdlVertices, 0, bytes };
        SDL_UploadToGPUBuffer(copy, &from, &to, false);
        SDL_EndGPUCopyPass(copy);
        SDL_ReleaseGPUTransferBuffer(device, upload);

        SDL_GPUShader* vert = SDLShader(device, SDL_GPU_SHADERSTAGE_VERTEX);
        SDL_GPUShader* frag = SDLShader(device, SDL_GPU_SHADERSTAGE_FRAGMENT);
        if (vert != nullptr && frag != nullptr)
        {
            SDL_GPUColorTargetDescription color{};
            color.format = target.colorFormat;
            SDL_GPUGraphicsPipelineCreateInfo info{};
            info.vertex_shader                           = vert;
            info.fragment_shader                         = frag;
            info.primitive_type                          = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            info.rasterizer_state.fill_mode              = SDL_GPU_FILLMODE_FILL;
            info.rasterizer_state.cull_mode              = SDL_GPU_CULLMODE_NONE;
            info.rasterizer_state.front_face             = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
            info.depth_stencil_state.enable_depth_test   = true;
            info.depth_stencil_state.enable_depth_write  = true;
            info.depth_stencil_state.compare_op          = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
            info.target_info.color_target_descriptions   = &color;
            info.target_info.num_color_targets           = 1;
            info.target_info.depth_stencil_format        = target.depthFormat;
            info.target_info.has_depth_stencil_target    = true;
            m_sdlShapes = SDL_CreateGPUGraphicsPipeline(device, &info);

            // The grid: alpha blended, over everything.
            color.blend_state.enable_blend          = true;
            color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.color_blend_op        = SDL_GPU_BLENDOP_ADD;
            color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            color.blend_state.alpha_blend_op        = SDL_GPU_BLENDOP_ADD;
            info.primitive_type                         = SDL_GPU_PRIMITIVETYPE_LINELIST;
            info.depth_stencil_state.enable_depth_test  = false;
            info.depth_stencil_state.enable_depth_write = false;
            m_sdlLines = SDL_CreateGPUGraphicsPipeline(device, &info);
        }
        if (vert != nullptr)
            SDL_ReleaseGPUShader(device, vert);
        if (frag != nullptr)
            SDL_ReleaseGPUShader(device, frag);
        return m_sdlShapes != nullptr && m_sdlLines != nullptr;
    }

    void SceneRenderer::Render(const sdlgpu::SDLGPUSceneRender& target, const ViewportRenderContext& ctx,
                               std::span<const SceneObject> objects, int selected)
    {
        if (m_failed)
            return;
        if (!m_created)
        {
            m_created = true;
            if (!CreateSDLGPU(target))
            {
                m_failed = true;
                ReleaseSDLGPU();
                TACHYON_LOG_ERROR(kLogScene, "The scene's pipelines weren't made; the viewport shows its background only");
                return;
            }
        }
        SDL_GPUCommandBuffer* cmd = target.commandBuffer;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target.colorTarget, 1, &target.depthTarget);
        SDL_BindGPUVertexStorageBuffers(pass, 0, &m_sdlVertices, 1);
        Walk(ctx, objects, selected,
            [&](bool lines) { SDL_BindGPUGraphicsPipeline(pass, lines ? m_sdlLines : m_sdlShapes); },
            [&](Range range, const float4x4& mvp, color4f color, float4 light) {
                const FirstVertexPush push{ mvp, color, light, range.first };
                SDL_PushGPUVertexUniformData(cmd, 0, &push, sizeof(push));
                SDL_PushGPUFragmentUniformData(cmd, 0, &push, sizeof(push));
                SDL_DrawGPUPrimitives(pass, range.count, 1, 0, 0);
            });
        SDL_EndGPURenderPass(pass);
    }

    void SceneRenderer::ReleaseSDLGPU()
    {
        if (m_sdlDevice != nullptr)
        {
            if (m_sdlShapes != nullptr)
                SDL_ReleaseGPUGraphicsPipeline(m_sdlDevice, m_sdlShapes);
            if (m_sdlLines != nullptr)
                SDL_ReleaseGPUGraphicsPipeline(m_sdlDevice, m_sdlLines);
            if (m_sdlVertices != nullptr)
                SDL_ReleaseGPUBuffer(m_sdlDevice, m_sdlVertices);
        }
        m_sdlDevice   = nullptr;
        m_sdlShapes   = nullptr;
        m_sdlLines    = nullptr;
        m_sdlVertices = nullptr;
    }
#else
    void SceneRenderer::ReleaseSDLGPU() {}
#endif

#if TACHYON_METAL
    namespace
    {
        // A Metal function from the showcase's built-in source.
        id<MTLFunction> MetalFunction(id<MTLDevice> device, const char* file, NSString* entry)
        {
            const std::span<const std::byte> code = detail::FindEmbeddedFile(detail::ShowcaseShaderFiles(), file);
            if (code.empty())
                return nil;
            NSString* source = [[NSString alloc] initWithBytes:code.data() length:code.size() encoding:NSUTF8StringEncoding];
            NSError* error = nil;
            id<MTLLibrary> library = [device newLibraryWithSource:source options:[MTLCompileOptions new] error:&error];
            if (library == nil)
            {
                TACHYON_LOG_ERROR(kLogScene, "{}: {}", file, error != nil ? error.localizedDescription.UTF8String : "unknown error");
                return nil;
            }
            id<MTLFunction> function = [library newFunctionWithName:entry];
            return function != nil ? function : [library newFunctionWithName:library.functionNames.firstObject];
        }

        void ReleaseObject(void*& object)
        {
            if (object != nullptr)
                CFBridgingRelease(object);
            object = nullptr;
        }
    }

    bool SceneRenderer::CreateMetal(const metal::MetalSceneRender& target)
    {
        id<MTLCommandBuffer> commandBuffer = (__bridge id<MTLCommandBuffer>)target.commandBuffer;
        id<MTLDevice> device = commandBuffer.device;

        // The meshes, in a shared buffer the vertex shader reads (buffer(1)).
        const std::vector<Vertex> vertices = BuildMeshes();
        id<MTLBuffer> buffer = [device newBufferWithBytes:vertices.data()
                                                   length:vertices.size() * sizeof(Vertex)
                                                  options:MTLResourceStorageModeShared];
        id<MTLFunction> vs = MetalFunction(device, "scene.vert.metal", @"vertexMain");
        id<MTLFunction> fs = MetalFunction(device, "scene.frag.metal", @"fragmentMain");
        if (buffer == nil || vs == nil || fs == nil)
            return false;
        m_mtlVertices = (void*)CFBridgingRetain(buffer);

        MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
        desc.vertexFunction                  = vs;
        desc.fragmentFunction                = fs;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormat(target.colorFormat);
        desc.depthAttachmentPixelFormat      = MTLPixelFormat(target.depthFormat);
        NSError* error = nil;
        id<MTLRenderPipelineState> shapes = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        // The grid: alpha blended, over everything.
        MTLRenderPipelineColorAttachmentDescriptor* color = desc.colorAttachments[0];
        color.blendingEnabled             = YES;
        color.sourceRGBBlendFactor        = MTLBlendFactorSourceAlpha;
        color.destinationRGBBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
        color.sourceAlphaBlendFactor      = MTLBlendFactorOne;
        color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        id<MTLRenderPipelineState> lines = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (shapes == nil || lines == nil)
        {
            TACHYON_LOG_ERROR(kLogScene, "a scene pipeline failed: {}", error != nil ? error.localizedDescription.UTF8String : "unknown error");
            return false;
        }
        m_mtlShapes = (void*)CFBridgingRetain(shapes);
        m_mtlLines  = (void*)CFBridgingRetain(lines);

        MTLDepthStencilDescriptor* depth = [MTLDepthStencilDescriptor new];
        depth.depthCompareFunction = MTLCompareFunctionLessEqual;
        depth.depthWriteEnabled    = YES;
        m_mtlDepthShapes = (void*)CFBridgingRetain([device newDepthStencilStateWithDescriptor:depth]);
        depth.depthCompareFunction = MTLCompareFunctionAlways;
        depth.depthWriteEnabled    = NO;
        m_mtlDepthLines  = (void*)CFBridgingRetain([device newDepthStencilStateWithDescriptor:depth]);
        return true;
    }

    void SceneRenderer::Render(const metal::MetalSceneRender& target, const ViewportRenderContext& ctx,
                               std::span<const SceneObject> objects, int selected)
    {
        if (m_failed)
            return;
        if (!m_created)
        {
            m_created = true;
            if (!CreateMetal(target))
            {
                m_failed = true;
                ReleaseMetal();
                TACHYON_LOG_ERROR(kLogScene, "The scene's pipelines weren't made; the viewport shows its background only");
                return;
            }
        }
        id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)target.encoder;
        [encoder setCullMode:MTLCullModeNone];
        [encoder setVertexBuffer:(__bridge id<MTLBuffer>)m_mtlVertices offset:0 atIndex:1];
        MTLPrimitiveType primitive = MTLPrimitiveTypeTriangle;
        Walk(ctx, objects, selected,
            [&](bool lines) {
                [encoder setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)(lines ? m_mtlLines : m_mtlShapes)];
                [encoder setDepthStencilState:(__bridge id<MTLDepthStencilState>)(lines ? m_mtlDepthLines : m_mtlDepthShapes)];
                primitive = lines ? MTLPrimitiveTypeLine : MTLPrimitiveTypeTriangle;
            },
            [&](Range range, const float4x4& mvp, color4f color, float4 light) {
                const FirstVertexPush push{ mvp, color, light, range.first };
                [encoder setVertexBytes:&push length:sizeof(push) atIndex:0];
                [encoder setFragmentBytes:&push length:sizeof(push) atIndex:0];
                [encoder drawPrimitives:primitive vertexStart:0 vertexCount:range.count];
            });
    }

    void SceneRenderer::ReleaseMetal()
    {
        ReleaseObject(m_mtlShapes);
        ReleaseObject(m_mtlLines);
        ReleaseObject(m_mtlDepthShapes);
        ReleaseObject(m_mtlDepthLines);
        ReleaseObject(m_mtlVertices);
    }
#else
    void SceneRenderer::ReleaseMetal() {}
#endif
}
