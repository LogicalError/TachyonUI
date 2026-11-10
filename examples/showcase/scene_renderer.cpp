#include "scene_renderer.h"

#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/gui_embedded_files_internal.h>
#include <gui_core/graphics/vulkan/vk_graphics_context.h>
#include <gui_core/graphics/vulkan/vk_pipelines.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

static constexpr tachyon::Diagnostics::LogCategory kLogScene = tachyon::Diagnostics::MakeLogCategory("showcase");

namespace tachyon::detail
{
    /// The showcase's compiled shaders (scene.vert.spv, scene.frag.spv), built in by CMake.
    std::span<const EmbeddedFile> ShowcaseShaderFiles();
}

namespace tachyon
{
    namespace
    {
        constexpr float kPi = std::numbers::pi_v<float>;

        // As scene.vert reads them.
        struct Vertex
        {
            float4 position;
            float4 normal;
        };

        struct Push
        {
            float4x4        mvp;
            color4f         color;
            float4          light;
            VkDeviceAddress vertices;
        };

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
    }

    float SceneObjectRadius(const SceneObject& object)
    {
        const float3 s = object.transform.scale;
        // Half the cube's diagonal; the sphere and cone fit inside it.
        return 0.87f * std::max({ std::fabs(s.x), std::fabs(s.y), std::fabs(s.z) });
    }

    int PickSceneObject(std::span<const SceneObject> objects, const Ray& ray)
    {
        int best = -1;
        float nearest = 0.0f;
        for (int i = 0; i < (int)objects.size(); ++i)
        {
            const float3 to = objects[size_t(i)].transform.position - ray.origin;
            const float along = Dot(to, ray.direction);
            const float r = SceneObjectRadius(objects[size_t(i)]);
            const float miss2 = Dot(to, to) - along * along;
            if (along < 0.0f || miss2 > r * r)
                continue;
            if (best < 0 || along < nearest) { best = i; nearest = along; }
        }
        return best;
    }

    bool SceneRenderer::Create(vulkan::VulkanGraphicsContext& vk, VkFormat colorFormat, VkFormat depthFormat)
    {
        vulkan::VulkanDeviceContext& dc = vk.deviceContext;
        const VkDevice device = dc.device;
        const VkAllocationCallbacks* callbacks = dc.AllocationCallbacks();

        // The meshes, all in one buffer the shaders read by address.
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
        const vulkan::Buffer buffer = dc.CreateMappedStorageBuffer(vertices.data(), vertices.size() * sizeof(Vertex), m_vertices);

        const VkPushConstantRange range{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push) };
        VkPipelineLayoutCreateInfo layoutInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &range;
        if (vkCreatePipelineLayout(device, &layoutInfo, callbacks, &m_layout) != VK_SUCCESS)
            return false;

        const VkShaderModule vert = Module(device, callbacks, "scene.vert.spv");
        const VkShaderModule frag = Module(device, callbacks, "scene.frag.spv");
        if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE)
            return false;
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
        dc.GetDeletionQueue(DeviceScope::Application).push_function([&dc, device, callbacks, layout, shapes, lines, buffer] {
            vkDestroyPipeline(device, shapes, callbacks);
            vkDestroyPipeline(device, lines, callbacks);
            vkDestroyPipelineLayout(device, layout, callbacks);
            dc.DestroyBuffer(buffer);
        });
        return m_shapes != VK_NULL_HANDLE && m_lines != VK_NULL_HANDLE;
    }

    void SceneRenderer::Render(vulkan::VulkanGraphicsContext& vk, const vulkan::VulkanSceneRender& target,
                               const ViewportRenderContext& ctx, std::span<const SceneObject> objects, int selected)
    {
        if (m_failed)
            return;
        if (!m_created)
        {
            m_created = true;
            if (!Create(vk, target.colorFormat, target.depthFormat))
            {
                m_failed = true;
                TACHYON_LOG_ERROR(kLogScene, "The scene's pipelines weren't made; the viewport shows its background only");
                return;
            }
        }
        const VkCommandBuffer cmd = target.commandBuffer;
        const float4x4 viewProjection = ctx.projection * ctx.view;
        auto draw = [&](Range range, const float4x4& model, color4f color, float4 light) {
            const Push push{ viewProjection * model, color, light, m_vertices };
            vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push), &push);
            vkCmdDraw(cmd, range.count, 1, range.first, 0);
        };

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lines);
        draw(m_grid,  {}, { 0.35f, 0.38f, 0.44f, 0.6f }, {});
        draw(m_axisX, {}, { 0.94f, 0.42f, 0.42f, 0.9f }, {});
        draw(m_axisZ, {}, { 0.42f, 0.61f, 1.00f, 0.9f }, {});

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shapes);
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
            draw(m_meshes[size_t(o.mesh)], o.transform.Matrix(), color, { light.x, light.y, light.z, 1.0f });
        }
    }
}
