#pragma once

// Inside the SDL GPU backend: the GUI's pipelines, and a window frame's recording surface for the
// PrimitiveRenderer (RHI::RenderCommandList). SDL wants copies outside render passes, and the
// renderer uploads and draws in one go: the command list keeps the frame's uploads in a staging
// area and its draws in a list, which the window copies and replays after.

#include <gui_core/graphics/sdlgpu/sdlgpu_device.h>

#include <vector>

namespace tachyon::sdlgpu
{
    /// A pipeline per primitive kind from the built-in SDL GPU shaders, in the format the device
    /// takes. False, logged, when one can't be made.
    bool CreatePipelines(SDLGPUGraphicsContext::Impl& device);
    void ReleasePipelines(SDLGPUGraphicsContext::Impl& device);

    class SDLGPUCommandList final : public RHI::RenderCommandList
    {
    public:
        /// `staging` takes the frame's transient data; `stagingEntry` is the buffer table's entry
        /// of the buffer it's copied into.
        SDLGPUCommandList(SDLGPUGraphicsContext::Impl& device, std::vector<std::byte>& staging, uint32_t stagingEntry)
            : m_device(device), m_staging(staging), m_stagingEntry(stagingEntry) {}

        /// Cut where a run would read more than the 16 images a draw binds, and kept for Replay.
        void DrawPrimitives(const RHI::GpuPipeline& pipeline, const GPUDrawPushConstants& pushConstants,
                            uint32_t primitiveCount, uint32_t firstPrimitive, PrimitiveShape shape,
                            std::span<const std::byte> primitives) override;
        void UploadTransientBuffers(std::span<const RHI::UploadRequest> requests, std::span<DeviceAddress> addresses) override;
        RHI::ResidentBufferId UploadResidentBuffers(std::span<const RHI::UploadRequest> requests,
                                                    std::span<DeviceAddress> addresses) override;
        RHI::ResidentState ResidentBufferState(RHI::ResidentBufferId id) const override;
        void ReleaseResidentBuffer(RHI::ResidentBufferId id) override;

        /// The most quads one draw has: the quads' index buffer must hold them.
        uint32_t MostQuads() const { return m_mostQuads; }
        /// Records the kept draws into the render pass.
        void Replay(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass) const;

    private:
        struct Draw
        {
            uint32_t       kind = 0;
            PrimitiveShape shape = PrimitiveShape::Quad;
            uint32_t       count = 0;
            uint32_t       bufferEntry = 0;   ///< the kind's primitives' buffer
            uint32_t       imageCount = 0;
            std::array<uint32_t, ImageBatcher::kMaxImages> images {};
            DrawUniforms   uniforms {};
        };

        SDLGPUGraphicsContext::Impl& m_device;
        std::vector<std::byte>&      m_staging;
        uint32_t                     m_stagingEntry;
        std::vector<Draw>            m_draws;
        std::vector<ImageBatcher::Batch> m_batches;   ///< reused
        uint32_t                     m_mostQuads = 0;
    };
}
