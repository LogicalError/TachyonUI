#include <gui_core/graphics/sdlgpu/sdlgpu_rhi.h>

#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/gui_embedded_files_internal.h>

#include <algorithm>
#include <cstring>
#include <string>

static constexpr tachyon::Diagnostics::LogCategory kLogSDLGPU = tachyon::Diagnostics::MakeLogCategory("sdlgpu");

namespace tachyon::sdlgpu
{
    namespace
    {
        // The storage buffers a stage reads (all_common.slang): the five primitive slots, the box
        // styles and the color styles.
        constexpr uint32_t kStorageBuffers = 7;

        SDL_GPUShader* CreateShader(SDLGPUGraphicsContext::Impl& d, const char* baseName, SDL_GPUShaderStage stage)
        {
            const bool spirv = d.shaderFormat == SDL_GPU_SHADERFORMAT_SPIRV;
            const bool vertex = stage == SDL_GPU_SHADERSTAGE_VERTEX;
            const std::string name = std::string(baseName) + (vertex ? ".vert" : ".frag") + (spirv ? ".sdl.spv" : ".sdl.dxil");
            const std::span<const std::byte> code = detail::FindEmbeddedFile(detail::EmbeddedShaderFiles(), name);
            if (code.empty())
            {
                TACHYON_LOG_ERROR(kLogSDLGPU, "no built-in shader {}", name);
                return nullptr;
            }
            SDL_GPUShaderCreateInfo info{};
            info.code_size           = code.size();
            info.code                = reinterpret_cast<const Uint8*>(code.data());
            // Slang names SPIR-V's entry point main, and keeps DXIL's.
            info.entrypoint          = spirv ? "main" : vertex ? "vertexMain" : "fragmentMain";
            info.format              = d.shaderFormat;
            info.stage               = stage;
            info.num_samplers        = ImageBatcher::kMaxImages;
            info.num_storage_buffers = kStorageBuffers;
            info.num_uniform_buffers = 1;
            SDL_GPUShader* shader = SDL_CreateGPUShader(d.device, &info);
            if (shader == nullptr)
                TACHYON_LOG_ERROR(kLogSDLGPU, "the shader {} failed ({})", name, SDL_GetError());
            return shader;
        }
    }

    bool CreatePipelines(SDLGPUGraphicsContext::Impl& d)
    {
        for (size_t k = 0; k < kPrimitiveKindCount; ++k)
        {
            const PrimitiveSpec& spec = kPrimitiveSpecs[k];
            SDL_GPUShader* vs = CreateShader(d, spec.shaderBaseName, SDL_GPU_SHADERSTAGE_VERTEX);
            SDL_GPUShader* fs = CreateShader(d, spec.shaderBaseName, SDL_GPU_SHADERSTAGE_FRAGMENT);
            if (vs == nullptr || fs == nullptr)
            {
                if (vs != nullptr) SDL_ReleaseGPUShader(d.device, vs);
                if (fs != nullptr) SDL_ReleaseGPUShader(d.device, fs);
                return false;
            }

            // Premultiplied alpha, as the shaders write it; an occluder writes depth alone.
            SDL_GPUColorTargetDescription color{};
            color.format = kDrawFormat;
            SDL_GPUColorTargetBlendState& blend = color.blend_state;
            blend.src_color_blendfactor   = SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_color_blendfactor   = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.color_blend_op          = SDL_GPU_BLENDOP_ADD;
            blend.src_alpha_blendfactor   = SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_alpha_blendfactor   = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alpha_blend_op          = SDL_GPU_BLENDOP_ADD;
            blend.enable_blend            = true;
            blend.enable_color_write_mask = true;
            blend.color_write_mask        = spec.depthOnly ? 0
                                          : SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;

            SDL_GPUGraphicsPipelineCreateInfo info{};
            info.vertex_shader   = vs;
            info.fragment_shader = fs;
            info.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            info.rasterizer_state.fill_mode         = SDL_GPU_FILLMODE_FILL;
            info.rasterizer_state.cull_mode         = SDL_GPU_CULLMODE_NONE;
            info.rasterizer_state.front_face        = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
            info.rasterizer_state.enable_depth_clip = true;
            // Later primitives are nearer: a draw passes where it's at or above what's there.
            info.depth_stencil_state.compare_op         = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
            info.depth_stencil_state.enable_depth_test  = true;
            info.depth_stencil_state.enable_depth_write = spec.depthOnly;
            info.target_info.color_target_descriptions = &color;
            info.target_info.num_color_targets         = 1;
            info.target_info.depth_stencil_format      = kDepthFormat;
            info.target_info.has_depth_stencil_target  = true;
            d.pipelineObjects[k] = SDL_CreateGPUGraphicsPipeline(d.device, &info);
            SDL_ReleaseGPUShader(d.device, vs);
            SDL_ReleaseGPUShader(d.device, fs);
            if (d.pipelineObjects[k] == nullptr)
            {
                TACHYON_LOG_ERROR(kLogSDLGPU, "the {} pipeline failed ({})", spec.shaderBaseName, SDL_GetError());
                return false;
            }
            d.pipelines[k].pipeline = reinterpret_cast<uint64_t>(d.pipelineObjects[k]);
            d.pipelines[k].layout   = k;   // which kind it draws
        }
        return true;
    }

    void ReleasePipelines(SDLGPUGraphicsContext::Impl& d)
    {
        for (SDL_GPUGraphicsPipeline*& pipeline : d.pipelineObjects)
        {
            if (pipeline != nullptr)
                SDL_ReleaseGPUGraphicsPipeline(d.device, pipeline);
            pipeline = nullptr;
        }
        d.pipelines = {};
    }

    void SDLGPUCommandList::DrawPrimitives(const RHI::GpuPipeline& pipeline, const GPUDrawPushConstants& pushConstants,
                                           uint32_t primitiveCount, uint32_t firstPrimitive, PrimitiveShape shape,
                                           std::span<const std::byte> primitives)
    {
        if (primitiveCount == 0 || !pipeline.IsValid())
            return;
        const auto kind = PrimitiveKind(pipeline.layout);
        const PrimitiveSpec& spec = kPrimitiveSpecs[size_t(kind)];
        const DeviceAddress address = pushConstants.primitiveAddresses[spec.pushSlot];
        const uint32_t corners = shape == PrimitiveShape::Quad ? 4u : 3u;

        m_batches.clear();
        m_device.batcher.Split(kind, primitives, firstPrimitive, ImageBatcher::kMaxImages, m_batches);
        for (const ImageBatcher::Batch& batch : m_batches)
        {
            Draw draw;
            draw.kind        = uint32_t(kind);
            draw.shape       = shape;
            draw.count       = batch.count;
            draw.bufferEntry = AddressEntry(address);
            draw.imageCount  = batch.imageCount;
            draw.images      = batch.images;
            DrawUniforms& u = draw.uniforms;
            std::memcpy(u.worldColumns, &pushConstants.worldMatrix, sizeof(u.worldColumns));
            u.elapsedTime = pushConstants.elapsedTime;
            u.firstVertex = batch.first * corners;
            u.primitiveOffsets[spec.pushSlot] = AddressOffset(address);
            std::fill(std::begin(u.imageSlots), std::end(u.imageSlots), ~0u);
            std::copy_n(batch.images.begin(), batch.imageCount, u.imageSlots);
            if (shape == PrimitiveShape::Quad)
                m_mostQuads = std::max(m_mostQuads, batch.count);
            m_draws.push_back(draw);
        }
    }

    void SDLGPUCommandList::UploadTransientBuffers(std::span<const RHI::UploadRequest> requests, std::span<DeviceAddress> addresses)
    {
        for (size_t i = 0; i < requests.size(); ++i)
        {
            if (requests[i].bytes == 0)
            {
                addresses[i] = 0;
                continue;
            }
            const size_t alignment = std::max<size_t>(requests[i].alignment, 4);
            const size_t offset    = (m_staging.size() + alignment - 1) / alignment * alignment;
            m_staging.resize(offset + requests[i].bytes);
            std::memcpy(m_staging.data() + offset, requests[i].data, requests[i].bytes);
            addresses[i] = MakeAddress(m_stagingEntry, uint32_t(offset));
        }
    }

    RHI::ResidentBufferId SDLGPUCommandList::UploadResidentBuffers(std::span<const RHI::UploadRequest> requests,
                                                                   std::span<DeviceAddress> addresses)
    {
        return m_device.CreateResident(requests, addresses);
    }

    RHI::ResidentState SDLGPUCommandList::ResidentBufferState(RHI::ResidentBufferId id) const
    {
        return m_device.ResidentState(id);
    }

    void SDLGPUCommandList::ReleaseResidentBuffer(RHI::ResidentBufferId id)
    {
        m_device.ReleaseResident(id);
    }

    void SDLGPUCommandList::Replay(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass) const
    {
        const SDLGPUGraphicsContext::Impl& d = m_device;
        SDL_GPUGraphicsPipeline* bound = nullptr;
        bool indexed = false;
        for (const Draw& draw : m_draws)
        {
            if (d.pipelineObjects[draw.kind] != bound)
            {
                bound = d.pipelineObjects[draw.kind];
                SDL_BindGPUGraphicsPipeline(pass, bound);
            }

            // The kind's buffer in its slot; the slots it doesn't read get a buffer all the same.
            const uint32_t slot = kPrimitiveSpecs[draw.kind].pushSlot;
            SDL_GPUBuffer* storage[kStorageBuffers];
            for (uint32_t i = 0; i < 5; ++i)
                storage[i] = d.blankBuffer;
            if (SDL_GPUBuffer* primitives = d.Buffer(draw.bufferEntry))
                storage[slot] = primitives;
            storage[5] = d.boxStyles != nullptr ? d.boxStyles : d.blankBuffer;
            storage[6] = d.colorStyles != nullptr ? d.colorStyles : d.blankBuffer;
            SDL_BindGPUVertexStorageBuffers(pass, 0, storage, kStorageBuffers);
            SDL_BindGPUFragmentStorageBuffers(pass, 0, storage, kStorageBuffers);

            // The run's images; the rest a blank one.
            SDL_GPUTextureSamplerBinding images[ImageBatcher::kMaxImages];
            for (uint32_t i = 0; i < ImageBatcher::kMaxImages; ++i)
            {
                const SDLGPUImage* image = i < draw.imageCount ? d.imageSet.At(draw.images[i]) : nullptr;
                images[i] = image != nullptr && image->texture != nullptr
                          ? SDL_GPUTextureSamplerBinding{ image->texture, d.SamplerFor(image->sampler) }
                          : SDL_GPUTextureSamplerBinding{ d.blankImage, d.samplers[0] };
            }
            SDL_BindGPUVertexSamplers(pass, 0, images, ImageBatcher::kMaxImages);
            SDL_BindGPUFragmentSamplers(pass, 0, images, ImageBatcher::kMaxImages);

            SDL_PushGPUVertexUniformData(commands, 0, &draw.uniforms, sizeof(DrawUniforms));
            SDL_PushGPUFragmentUniformData(commands, 0, &draw.uniforms, sizeof(DrawUniforms));
            if (draw.shape == PrimitiveShape::Quad)
            {
                // Four corners a quad, two triangles from the shared index buffer.
                if (!indexed)
                {
                    const SDL_GPUBufferBinding indices{ d.quadIndices, 0 };
                    SDL_BindGPUIndexBuffer(pass, &indices, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                    indexed = true;
                }
                SDL_DrawGPUIndexedPrimitives(pass, draw.count * 6, 1, 0, 0, 0);
            }
            else
                SDL_DrawGPUPrimitives(pass, draw.count * 3, 1, 0, 0);
        }
    }
}
