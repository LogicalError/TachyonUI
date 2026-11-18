#pragma once

// Inside the Direct3D 12 backend: the GUI's pipelines, and a window frame's recording surface
// for the PrimitiveRenderer (RHI::RenderCommandList).

#include <gui_core/graphics/d3d12/d3d12_device.h>
#include <gui_core/graphics/gui_rhi.h>

namespace tachyon::d3d12
{
    /// The root signature, as the shaders' TACHYON_D3D12 path declares it (all_common.slang,
    /// sdf_color.slang), and a pipeline per primitive kind from the built-in DXIL.
    bool CreatePipelines(D3D12GraphicsContext::Impl& device);

    /// A frame slot's transient data: one upload buffer, written from the start each time the
    /// slot comes round, grown when a frame needs more.
    struct FrameArena
    {
        Buffer buffer;
        UINT64 offset = 0;
    };

    class D3D12CommandList final : public RHI::RenderCommandList
    {
    public:
        D3D12CommandList(D3D12GraphicsContext::Impl& device, ID3D12GraphicsCommandList* list, FrameArena& arena)
            : m_device(device), m_list(list), m_arena(arena) {}

        /// Writes `bytes` into the arena, `alignment` apart; the GPU address of the copy, or 0.
        D3D12_GPU_VIRTUAL_ADDRESS Push(const void* data, size_t bytes, size_t alignment);
        /// The per-image sampler table (t7) every draw of the frame binds.
        void SetSamplerTable(D3D12_GPU_VIRTUAL_ADDRESS table) { m_samplerTable = table; }

        void DrawPrimitives(const RHI::GpuPipeline& pipeline, const GPUDrawPushConstants& pushConstants,
                            uint32_t primitiveCount, uint32_t firstPrimitive, PrimitiveShape shape,
                            std::span<const std::byte> primitives) override;
        void UploadTransientBuffers(std::span<const RHI::UploadRequest> requests, std::span<DeviceAddress> addresses) override;
        RHI::ResidentBufferId UploadResidentBuffers(std::span<const RHI::UploadRequest> requests,
                                                    std::span<DeviceAddress> addresses) override;
        RHI::ResidentState ResidentBufferState(RHI::ResidentBufferId id) const override;
        void ReleaseResidentBuffer(RHI::ResidentBufferId id) override;

    private:
        D3D12GraphicsContext::Impl& m_device;
        ID3D12GraphicsCommandList*  m_list;
        FrameArena&                 m_arena;
        D3D12_GPU_VIRTUAL_ADDRESS   m_samplerTable = 0;
    };
}
