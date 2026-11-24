#pragma once

// Inside the Metal backend: the GUI's pipelines, and a window frame's recording surface for the
// PrimitiveRenderer (RHI::RenderCommandList).

#include <gui_core/graphics/metal/metal_device.h>
#include <gui_core/graphics/gui_rhi.h>

namespace tachyon::metal
{
    /// A pipeline per primitive kind from the built-in shaders (Metal Shading Language source,
    /// compiled now), the depth states and the samplers. False, with the reason logged, on failure.
    bool CreatePipelines(MetalGraphicsContext::Impl& device);

    /// A frame slot's transient data: one buffer, written from the start each time the slot
    /// comes round, grown when a frame needs more.
    struct FrameArena
    {
        Buffer buffer;
        size_t offset = 0;
        /// Buffers outgrown this frame: their addresses stay valid until the slot comes round.
        std::vector<Buffer> retired;

        /// The slot again, its last frame done: writes start over, the outgrown buffers go.
        void Reset(MetalGraphicsContext::Impl& device)
        {
            for (Buffer& old : retired)
                device.ReleaseBuffer(old);
            retired.clear();
            offset = 0;
        }
    };

    class MetalCommandList final : public RHI::RenderCommandList
    {
    public:
        MetalCommandList(MetalGraphicsContext::Impl& device, id<MTLRenderCommandEncoder> encoder, FrameArena& arena)
            : m_device(device), m_encoder(encoder), m_arena(arena) {}

        /// Writes `bytes` into the arena, `alignment` apart; the address of the copy, or 0.
        DeviceAddress Push(const void* data, size_t bytes, size_t alignment);
        /// Binds a buffer the shaders read at `index` in both stages, as the address names it;
        /// the empty buffer for 0.
        void Bind(NSUInteger index, DeviceAddress address);

        void DrawPrimitives(const RHI::GpuPipeline& pipeline, const GPUDrawPushConstants& pushConstants,
                            uint32_t primitiveCount, uint32_t firstPrimitive, PrimitiveShape shape,
                            std::span<const std::byte> primitives) override;
        void UploadTransientBuffers(std::span<const RHI::UploadRequest> requests, std::span<DeviceAddress> addresses) override;
        RHI::ResidentBufferId UploadResidentBuffers(std::span<const RHI::UploadRequest> requests,
                                                    std::span<DeviceAddress> addresses) override;
        RHI::ResidentState ResidentBufferState(RHI::ResidentBufferId id) const override;
        void ReleaseResidentBuffer(RHI::ResidentBufferId id) override;

    private:
        MetalGraphicsContext::Impl& m_device;
        id<MTLRenderCommandEncoder> m_encoder;
        FrameArena&                 m_arena;
        /// What's bound at each buffer index, so a draw binds only what changed.
        std::array<DeviceAddress, 10> m_bound {};
        std::array<bool, 10>          m_boundValid {};
        uint64_t                      m_pipeline = 0;
    };
}
