#include <gui_core/graphics/metal/metal_rhi.h>

#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/gui_embedded_files_internal.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <string>

#include <dispatch/dispatch.h>

static constexpr tachyon::Diagnostics::LogCategory kLogMetal = tachyon::Diagnostics::MakeLogCategory("metal");

namespace tachyon::metal
{
    namespace
    {
        std::span<const std::byte> Shader(const char* baseName, const char* stage)
        {
            const std::string name = std::string(baseName) + stage + ".metal";
            const std::span<const std::byte> code = detail::FindEmbeddedFile(detail::EmbeddedShaderFiles(), name);
            if (code.empty())
                TACHYON_LOG_ERROR(kLogMetal, "no built-in shader {}", name);
            return code;
        }

        // One stage's function, compiled from its source. slangc names it as the module does
        // (vertexMain, fragmentMain); a library with one function gives that one.
        id<MTLFunction> CompileFunction(id<MTLDevice> device, std::span<const std::byte> source, NSString* entry, const char* name,
                                        std::string& error)
        {
            NSString* text = [[NSString alloc] initWithBytes:source.data() length:source.size() encoding:NSUTF8StringEncoding];
            MTLCompileOptions* options = [MTLCompileOptions new];
            options.languageVersion = MTLLanguageVersion3_0;
            // The shaders count on IEEE results (their SDFs divide by distances near 0), as on the
            // other APIs.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            if (@available(macOS 15.0, *))
                options.mathMode = MTLMathModeSafe;
            else
                options.fastMathEnabled = NO;
#pragma clang diagnostic pop
            NSError* nsError = nil;
            id<MTLLibrary> library = [device newLibraryWithSource:text options:options error:&nsError];
            if (library == nil)
            {
                error = std::string(name) + ": " + (nsError != nil ? nsError.localizedDescription.UTF8String : "unknown error");
                return nil;
            }
            id<MTLFunction> function = [library newFunctionWithName:entry];
            if (function == nil && library.functionNames.count == 1)
                function = [library newFunctionWithName:library.functionNames.firstObject];
            if (function == nil)
                error = std::string(name) + ": no function " + entry.UTF8String;
            return function;
        }

        id<MTLSamplerState> MakeSampler(id<MTLDevice> device, MTLSamplerMinMagFilter filter, MTLSamplerAddressMode address)
        {
            MTLSamplerDescriptor* desc = [MTLSamplerDescriptor new];
            desc.minFilter    = filter;
            desc.magFilter    = filter;
            desc.mipFilter    = MTLSamplerMipFilterNotMipmapped;
            desc.sAddressMode = address;
            desc.tAddressMode = address;
            desc.rAddressMode = address;
            // In an argument buffer's draws: the samplers are bound directly, but this costs nothing.
            desc.supportArgumentBuffers = YES;
            return [device newSamplerStateWithDescriptor:desc];
        }
    }

    bool CreatePipelines(MetalGraphicsContext::Impl& d)
    {
        // Every kind's two stages, compiled at once: the compiler takes most of the start-up.
        constexpr size_t kStages = kPrimitiveKindCount * 2;
        std::array<id<MTLFunction>, kStages> functions {};
        std::array<std::string, kStages>     errors {};
        std::array<std::span<const std::byte>, kStages> sources {};
        for (size_t i = 0; i < kStages; ++i)
        {
            sources[i] = Shader(kPrimitiveSpecs[i / 2].shaderBaseName, i % 2 == 0 ? ".vert" : ".frag");
            if (sources[i].empty())
                return false;
        }
        id<MTLDevice> device = d.device;
        auto* functionsPtr = functions.data();
        auto* errorsPtr    = errors.data();
        auto* sourcesPtr   = sources.data();
        dispatch_apply(kStages, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^(size_t i) {
            const bool vertex = i % 2 == 0;
            const std::string name = std::string(kPrimitiveSpecs[i / 2].shaderBaseName) + (vertex ? ".vert" : ".frag");
            functionsPtr[i] = CompileFunction(device, sourcesPtr[i], vertex ? @"vertexMain" : @"fragmentMain", name.c_str(), errorsPtr[i]);
        });
        for (size_t i = 0; i < kStages; ++i)
            if (functions[i] == nil)
            {
                TACHYON_LOG_ERROR(kLogMetal, "a shader didn't compile: {}", errors[i]);
                return false;
            }

        d.pipelineStates.assign(kPrimitiveKindCount, nil);
        for (size_t k = 0; k < kPrimitiveKindCount; ++k)
        {
            const PrimitiveSpec& spec = kPrimitiveSpecs[k];
            MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
            desc.label            = @(spec.shaderBaseName);
            desc.vertexFunction   = functions[k * 2];
            desc.fragmentFunction = functions[k * 2 + 1];
            desc.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
            // Premultiplied alpha, as the shaders write it; an occluder writes depth alone.
            MTLRenderPipelineColorAttachmentDescriptor* color = desc.colorAttachments[0];
            color.pixelFormat                 = kDrawFormat;
            color.blendingEnabled             = YES;
            color.sourceRGBBlendFactor        = MTLBlendFactorOne;
            color.destinationRGBBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
            color.rgbBlendOperation           = MTLBlendOperationAdd;
            color.sourceAlphaBlendFactor      = MTLBlendFactorOne;
            color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            color.alphaBlendOperation         = MTLBlendOperationAdd;
            color.writeMask                   = spec.depthOnly ? MTLColorWriteMaskNone : MTLColorWriteMaskAll;
            desc.depthAttachmentPixelFormat   = kDepthFormat;

            NSError* error = nil;
            d.pipelineStates[k] = [d.device newRenderPipelineStateWithDescriptor:desc error:&error];
            if (d.pipelineStates[k] == nil)
            {
                TACHYON_LOG_ERROR(kLogMetal, "the {} pipeline failed: {}", spec.shaderBaseName,
                                  error != nil ? error.localizedDescription.UTF8String : "unknown error");
                return false;
            }
            d.pipelines[k].pipeline = reinterpret_cast<uint64_t>((__bridge void*)d.pipelineStates[k]);
            d.pipelines[k].layout   = spec.depthOnly ? 1 : 0;   // which depth state it draws with
        }

        // Later primitives are nearer: a draw passes where it's at or above what's there.
        MTLDepthStencilDescriptor* depth = [MTLDepthStencilDescriptor new];
        depth.depthCompareFunction = MTLCompareFunctionGreaterEqual;
        depth.depthWriteEnabled    = NO;
        d.depthTest = [d.device newDepthStencilStateWithDescriptor:depth];
        depth.depthWriteEnabled    = YES;
        d.depthWrite = [d.device newDepthStencilStateWithDescriptor:depth];

        // As sampler(0), sampler(1) and sampler(2) in the shaders: SamplerSlot's order.
        d.samplers[0] = MakeSampler(d.device, MTLSamplerMinMagFilterNearest, MTLSamplerAddressModeRepeat);
        d.samplers[1] = MakeSampler(d.device, MTLSamplerMinMagFilterLinear,  MTLSamplerAddressModeRepeat);
        d.samplers[2] = MakeSampler(d.device, MTLSamplerMinMagFilterLinear,  MTLSamplerAddressModeClampToEdge);
        d.emptyBuffer = [d.device newBufferWithLength:256 options:MTLResourceStorageModeShared];
        return d.depthTest != nil && d.depthWrite != nil && d.samplers[0] != nil && d.samplers[1] != nil &&
               d.samplers[2] != nil && d.emptyBuffer != nil;
    }

    DeviceAddress MetalCommandList::Push(const void* data, size_t bytes, size_t alignment)
    {
        if (bytes == 0)
            return 0;
        // Metal binds a buffer at a multiple of 4 bytes, 16 keeps the shaders' vectors aligned.
        alignment = std::max<size_t>(alignment, 16);
        size_t offset = (m_arena.offset + alignment - 1) / alignment * alignment;
        if (!m_arena.buffer || offset + bytes > m_arena.buffer.size)
        {
            // A bigger buffer; the addresses already handed out this frame keep naming the old one.
            Buffer bigger = m_device.CreateBuffer(std::bit_ceil(std::max<size_t>(offset + bytes, 64 * 1024)));
            if (!bigger)
                return 0;
            if (m_arena.buffer)
                m_arena.retired.push_back(std::move(m_arena.buffer));
            m_arena.buffer = std::move(bigger);
            offset = 0;
        }
        std::memcpy(static_cast<uint8_t*>(m_arena.buffer.Mapped()) + offset, data, bytes);
        m_arena.offset = offset + bytes;
        return m_arena.buffer.Address(offset);
    }

    void MetalCommandList::Bind(NSUInteger index, DeviceAddress address)
    {
        if (index < m_bound.size() && m_boundValid[index] && m_bound[index] == address)
            return;
        auto [buffer, offset] = m_device.Resolve(address);
        if (buffer == nil)
        {
            buffer = m_device.emptyBuffer;
            offset = 0;
        }
        [m_encoder setVertexBuffer:buffer offset:offset atIndex:index];
        [m_encoder setFragmentBuffer:buffer offset:offset atIndex:index];
        if (index < m_bound.size())
        {
            m_bound[index]      = address;
            m_boundValid[index] = true;
        }
    }

    void MetalCommandList::DrawPrimitives(const RHI::GpuPipeline& pipeline, const GPUDrawPushConstants& pushConstants,
                                          uint32_t primitiveCount, uint32_t firstPrimitive, PrimitiveShape shape,
                                          std::span<const std::byte>)
    {
        if (primitiveCount == 0 || !pipeline.IsValid())
            return;
        const uint32_t corners = shape == PrimitiveShape::Quad ? 4u : 3u;
        RootConstants constants{};
        std::memcpy(constants.worldColumns, &pushConstants.worldMatrix, sizeof(constants.worldColumns));
        constants.elapsedTime = pushConstants.elapsedTime;
        constants.firstVertex = firstPrimitive * corners;

        if (m_pipeline != pipeline.pipeline)
        {
            [m_encoder setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)reinterpret_cast<void*>(pipeline.pipeline)];
            [m_encoder setDepthStencilState:pipeline.layout != 0 ? m_device.depthWrite : m_device.depthTest];
            m_pipeline = pipeline.pipeline;
        }
        [m_encoder setVertexBytes:&constants length:sizeof(constants) atIndex:binding::kConstants];
        [m_encoder setFragmentBytes:&constants length:sizeof(constants) atIndex:binding::kConstants];
        for (NSUInteger slot = 0; slot < 5; ++slot)
            Bind(binding::kPrimitives + slot, pushConstants.primitiveAddresses[slot]);
        Bind(binding::kBoxStyles, pushConstants.boxStyleBuffer);
        Bind(binding::kColorStyles, pushConstants.colorStyleBuffer);

        if (shape == PrimitiveShape::Quad)
        {
            // Four corners a quad, two triangles from the shared index buffer.
            if (!m_device.EnsureQuadIndices(primitiveCount))
                return;
            [m_encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                  indexCount:NSUInteger(primitiveCount) * 6
                                   indexType:MTLIndexTypeUInt32
                                 indexBuffer:m_device.quadIndices
                           indexBufferOffset:0];
        }
        else
            [m_encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:NSUInteger(primitiveCount) * 3];
    }

    void MetalCommandList::UploadTransientBuffers(std::span<const RHI::UploadRequest> requests, std::span<DeviceAddress> addresses)
    {
        for (size_t i = 0; i < requests.size(); ++i)
            addresses[i] = Push(requests[i].data, requests[i].bytes, requests[i].alignment);
    }

    RHI::ResidentBufferId MetalCommandList::UploadResidentBuffers(std::span<const RHI::UploadRequest> requests,
                                                                  std::span<DeviceAddress> addresses)
    {
        return m_device.CreateResidentBuffer(requests, addresses);
    }

    RHI::ResidentState MetalCommandList::ResidentBufferState(RHI::ResidentBufferId id) const
    {
        return m_device.ResidentState(id);
    }

    void MetalCommandList::ReleaseResidentBuffer(RHI::ResidentBufferId id)
    {
        m_device.ReleaseResident(id);
    }
}
