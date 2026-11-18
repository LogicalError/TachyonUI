#include <gui_core/graphics/d3d12/d3d12_rhi.h>

#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/gui_embedded_files_internal.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <string>

static constexpr tachyon::Diagnostics::LogCategory kLogD3D12 = tachyon::Diagnostics::MakeLogCategory("d3d12");

namespace tachyon::d3d12
{
    namespace
    {
        // The root signature's parameters, in order: the root constants (b0), then a root SRV
        // each for the primitive slots (t0 to t4), the box and color styles (t5, t6) and the
        // per-image sampler table (t7).
        constexpr UINT kRootConstants    = 0;
        constexpr UINT kRootPrimitives   = 1;   // + pushSlot
        constexpr UINT kRootBoxStyles    = 6;
        constexpr UINT kRootColorStyles  = 7;
        constexpr UINT kRootSamplerTable = 8;
        constexpr UINT kRootParameters   = 9;

        // The root constants: the world matrix's four columns, the time, and the draw's first
        // vertex (SV_VertexID starts at 0 for every draw).
        struct RootConstants
        {
            float    worldColumns[16];
            float    elapsedTime;
            uint32_t firstVertex;
        };
        static_assert(sizeof(RootConstants) == 18 * 4);

        D3D12_STATIC_SAMPLER_DESC StaticSampler(UINT shaderRegister, D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE address)
        {
            D3D12_STATIC_SAMPLER_DESC desc{};
            desc.Filter           = filter;
            desc.AddressU         = address;
            desc.AddressV         = address;
            desc.AddressW         = address;
            desc.MaxLOD           = D3D12_FLOAT32_MAX;
            desc.ShaderRegister   = shaderRegister;
            desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            return desc;
        }

        bool CreateRootSignature(D3D12GraphicsContext::Impl& d)
        {
            D3D12_ROOT_PARAMETER1 parameters[kRootParameters]{};
            parameters[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            parameters[kRootConstants].Constants.ShaderRegister = 0;
            parameters[kRootConstants].Constants.Num32BitValues = sizeof(RootConstants) / 4;
            for (UINT t = 0; t < 8; ++t)
            {
                D3D12_ROOT_PARAMETER1& srv = parameters[kRootPrimitives + t];
                srv.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
                srv.Descriptor.ShaderRegister = t;
                // Their buffers change between frames (a new upload), not while one runs.
                srv.Descriptor.Flags          = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
            }
            for (D3D12_ROOT_PARAMETER1& p : parameters)
                p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            // As s0, s1 and s2 in the shaders: SamplerSlot's order.
            const D3D12_STATIC_SAMPLER_DESC samplers[3] = {
                StaticSampler(0, D3D12_FILTER_MIN_MAG_MIP_POINT,  D3D12_TEXTURE_ADDRESS_MODE_WRAP),
                StaticSampler(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP),
                StaticSampler(2, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
            };

            D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc{};
            desc.Version                    = D3D_ROOT_SIGNATURE_VERSION_1_1;
            desc.Desc_1_1.NumParameters     = kRootParameters;
            desc.Desc_1_1.pParameters       = parameters;
            desc.Desc_1_1.NumStaticSamplers = 3;
            desc.Desc_1_1.pStaticSamplers   = samplers;
            // The images come from the descriptor heap by index (ResourceDescriptorHeap).
            desc.Desc_1_1.Flags             = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

            ComPtr<ID3DBlob> blob, error;
            if (FAILED(D3D12SerializeVersionedRootSignature(&desc, &blob, &error)))
            {
                TACHYON_LOG_ERROR(kLogD3D12, "root signature: {}",
                                  error ? std::string(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize()) : "");
                return false;
            }
            if (HRESULT hr = d.device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&d.rootSignature));
                FAILED(hr))
            {
                TACHYON_LOG_ERROR(kLogD3D12, "CreateRootSignature failed ({})", HResultText(hr));
                return false;
            }
            return true;
        }

        std::span<const std::byte> Shader(const char* baseName, const char* stage)
        {
            const std::string name = std::string(baseName) + stage + ".dxil";
            const std::span<const std::byte> code = detail::FindEmbeddedFile(detail::EmbeddedShaderFiles(), name);
            if (code.empty())
                TACHYON_LOG_ERROR(kLogD3D12, "no built-in shader {}", name);
            return code;
        }
    }

    bool CreatePipelines(D3D12GraphicsContext::Impl& d)
    {
        if (!CreateRootSignature(d))
            return false;
        d.pipelineStates.resize(kPrimitiveKindCount);
        for (size_t k = 0; k < kPrimitiveKindCount; ++k)
        {
            const PrimitiveSpec& spec = kPrimitiveSpecs[k];
            const std::span<const std::byte> vs = Shader(spec.shaderBaseName, ".vert");
            const std::span<const std::byte> ps = Shader(spec.shaderBaseName, ".frag");
            if (vs.empty() || ps.empty())
                return false;

            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = d.rootSignature.Get();
            desc.VS = { vs.data(), vs.size() };
            desc.PS = { ps.data(), ps.size() };
            // Premultiplied alpha, as the shaders write it; an occluder writes depth alone.
            D3D12_RENDER_TARGET_BLEND_DESC& blend = desc.BlendState.RenderTarget[0];
            blend.BlendEnable           = TRUE;
            blend.SrcBlend              = D3D12_BLEND_ONE;
            blend.DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOp               = D3D12_BLEND_OP_ADD;
            blend.SrcBlendAlpha         = D3D12_BLEND_ONE;
            blend.DestBlendAlpha        = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
            blend.LogicOp               = D3D12_LOGIC_OP_NOOP;
            blend.RenderTargetWriteMask = spec.depthOnly ? 0 : D3D12_COLOR_WRITE_ENABLE_ALL;
            desc.SampleMask = UINT_MAX;
            desc.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
            desc.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
            desc.RasterizerState.DepthClipEnable = TRUE;
            // Later primitives are nearer: a draw passes where it's at or above what's there.
            desc.DepthStencilState.DepthEnable    = TRUE;
            desc.DepthStencilState.DepthWriteMask = spec.depthOnly ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            desc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.NumRenderTargets      = 1;
            desc.RTVFormats[0]         = kDrawFormat;
            desc.DSVFormat             = kDepthFormat;
            desc.SampleDesc            = { 1, 0 };
            if (HRESULT hr = d.device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&d.pipelineStates[k])); FAILED(hr))
            {
                TACHYON_LOG_ERROR(kLogD3D12, "the {} pipeline failed ({})", spec.shaderBaseName, HResultText(hr));
                return false;
            }
            d.pipelines[k].pipeline = reinterpret_cast<uint64_t>(d.pipelineStates[k].Get());
            d.pipelines[k].layout   = reinterpret_cast<uint64_t>(d.rootSignature.Get());
        }
        return true;
    }

    D3D12_GPU_VIRTUAL_ADDRESS D3D12CommandList::Push(const void* data, size_t bytes, size_t alignment)
    {
        if (bytes == 0)
            return 0;
        alignment = std::max<size_t>(alignment, 1);
        UINT64 offset = (m_arena.offset + alignment - 1) / alignment * alignment;
        if (!m_arena.buffer || offset + bytes > m_arena.buffer.size)
        {
            // A bigger buffer; draws already recorded this frame keep using the old one.
            Buffer bigger = m_device.CreateUploadBuffer(std::bit_ceil(std::max<UINT64>(offset + bytes, 64 * 1024)));
            if (!bigger)
                return 0;
            m_device.DestroyAfterGpu(std::move(m_arena.buffer.allocation), m_device.submitted + 1);
            m_arena.buffer = std::move(bigger);
            offset = 0;
        }
        std::memcpy(static_cast<uint8_t*>(m_arena.buffer.mapped) + offset, data, bytes);
        m_arena.offset = offset + bytes;
        return m_arena.buffer.address + offset;
    }

    void D3D12CommandList::DrawPrimitives(const RHI::GpuPipeline& pipeline, const GPUDrawPushConstants& pushConstants,
                                          uint32_t primitiveCount, uint32_t firstPrimitive, PrimitiveShape shape)
    {
        if (primitiveCount == 0 || !pipeline.IsValid())
            return;
        const uint32_t corners = shape == PrimitiveShape::Quad ? 4u : 3u;
        RootConstants constants{};
        std::memcpy(constants.worldColumns, &pushConstants.worldMatrix, sizeof(constants.worldColumns));
        constants.elapsedTime = pushConstants.elapsedTime;
        constants.firstVertex = firstPrimitive * corners;

        m_list->SetPipelineState(reinterpret_cast<ID3D12PipelineState*>(pipeline.pipeline));
        m_list->SetGraphicsRoot32BitConstants(kRootConstants, sizeof(RootConstants) / 4, &constants, 0);
        for (UINT slot = 0; slot < 5; ++slot)
            m_list->SetGraphicsRootShaderResourceView(kRootPrimitives + slot, pushConstants.primitiveAddresses[slot]);
        m_list->SetGraphicsRootShaderResourceView(kRootBoxStyles, pushConstants.boxStyleBuffer);
        m_list->SetGraphicsRootShaderResourceView(kRootColorStyles, pushConstants.colorStyleBuffer);
        m_list->SetGraphicsRootShaderResourceView(kRootSamplerTable, m_samplerTable);

        if (shape == PrimitiveShape::Quad)
        {
            // Four corners a quad, two triangles from the shared index buffer.
            if (!m_device.EnsureQuadIndices(primitiveCount))
                return;
            const D3D12_INDEX_BUFFER_VIEW view{ m_device.quadIndices.address, UINT(m_device.quadIndices.size), DXGI_FORMAT_R32_UINT };
            m_list->IASetIndexBuffer(&view);
            m_list->DrawIndexedInstanced(primitiveCount * 6, 1, 0, 0, 0);
        }
        else
            m_list->DrawInstanced(primitiveCount * 3, 1, 0, 0);
    }

    void D3D12CommandList::UploadTransientBuffers(std::span<const RHI::UploadRequest> requests, std::span<DeviceAddress> addresses)
    {
        for (size_t i = 0; i < requests.size(); ++i)
            addresses[i] = DeviceAddress(Push(requests[i].data, requests[i].bytes, requests[i].alignment));
    }

    RHI::ResidentBufferId D3D12CommandList::UploadResidentBuffers(std::span<const RHI::UploadRequest> requests,
                                                                  std::span<DeviceAddress> addresses)
    {
        return m_device.CreateResidentBuffer(requests, addresses);
    }

    RHI::ResidentState D3D12CommandList::ResidentBufferState(RHI::ResidentBufferId id) const
    {
        return m_device.ResidentState(id);
    }

    void D3D12CommandList::ReleaseResidentBuffer(RHI::ResidentBufferId id)
    {
        m_device.ReleaseResident(id);
    }
}
