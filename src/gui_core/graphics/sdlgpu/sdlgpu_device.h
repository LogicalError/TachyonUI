#pragma once

// Inside the SDL GPU backend: the device, and what its graphics context and window contexts
// share. SDL GPU has no buffer addresses: a DeviceAddress here is an entry in a table of
// buffers (high 32 bits) and a byte offset into it (low 32 bits). It binds 16 images a draw:
// each draw is cut where it would need more (ImageBatcher). Copies are recorded at the start of
// a window frame, before its render pass, as SDL wants them outside it. What's released is
// freed by SDL once the GPU is done with it.

#include <gui_core/graphics/gui_image.h>
#include <gui_core/graphics/gui_image_batcher.h>
#include <gui_core/graphics/gui_primitive_renderer.h>
#include <gui_core/graphics/gui_rhi.h>
#include <gui_core/graphics/sdlgpu/sdlgpu_graphics_context.h>

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>

#include <array>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tachyon
{
    class StyleManager;
    class GradientManager;
}

namespace tachyon::sdlgpu
{
    /// The format windows draw in, whatever their swapchain's: the blit to it converts.
    inline constexpr SDL_GPUTextureFormat kDrawFormat  = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    inline constexpr SDL_GPUTextureFormat kDepthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    /// What a DeviceAddress holds: the buffer table's entry, and the byte offset into it.
    inline DeviceAddress MakeAddress(uint32_t entry, uint32_t offset) { return (DeviceAddress(entry) << 32) | offset; }
    inline uint32_t      AddressEntry(DeviceAddress address)  { return uint32_t(address >> 32); }
    inline uint32_t      AddressOffset(DeviceAddress address) { return uint32_t(address); }

    /// An image (GraphicsContext::CreateImage): a texture sampled by the shaders.
    class SDLGPUImage final : public Image
    {
    public:
        SDL_GPUTexture* texture = nullptr;
        Sampler         sampler = Sampler::NearestRepeat;
        uint2           size {};
    };

    /// The images the shaders sample, by ImageIndex (1-based; the shaders' slot is one less).
    class SDLGPUImageSet final : public ImageSet
    {
    public:
        void       SetCapacity(uint32_t capacity) { m_capacity = capacity; }
        void       Clear() override;
        ImageIndex Add(Image* image) override;
        void       Remove(ImageIndex index) override;
        /// A slot with no image yet, for one that comes later (a text atlas page).
        ImageIndex ReserveIndex();
        /// Points the slot at another image.
        void       SetImage(ImageIndex index, SDLGPUImage* image);
        /// The image in a 0-based slot, or null.
        const SDLGPUImage* At(uint32_t slot) const { return slot < m_images.size() ? m_images[slot] : nullptr; }

    private:
        ImageIndex Allocate();

        uint32_t                  m_capacity = 4096;
        std::vector<SDLGPUImage*> m_images;   ///< by slot; null for an empty one
        std::vector<uint32_t>     m_free;     ///< slots handed back
    };

    /// The uniforms both stages of every draw get, as all_common.slang declares them.
    struct DrawUniforms
    {
        float    worldColumns[16];
        float    elapsedTime;
        uint32_t firstVertex;
        uint32_t pad[2];
        uint32_t primitiveOffsets[8];   ///< bytes into each slot's buffer
        uint32_t imageSlots[16];        ///< the image slot each bound image is; ~0 for none
    };
    static_assert(sizeof(DrawUniforms) == 176);

    struct SDLGPUGraphicsContext::Impl
    {
        DebugConfig    debug;
        std::string    driverAsked;    ///< SetDriver; empty, SDL's choice
        bool           lowPower = false;
        WindowResolver resolveWindow = nullptr;
        uint32_t       imageSlots = 4096;

        SDL_GPUDevice*      device = nullptr;
        SDL_GPUShaderFormat shaderFormat = SDL_GPU_SHADERFORMAT_INVALID;   ///< the one the GUI's shaders are taken in
        std::string         driver;         ///< the one in use
        std::string         deviceName;

        /// SDL's log output while the debug callback takes SDL GPU's messages.
        SDL_LogOutputFunction previousLog = nullptr;
        void*                 previousLogData = nullptr;
        bool                  logHooked = false;

        // ---- Buffers a DeviceAddress names --------------------------------------------
        std::vector<SDL_GPUBuffer*> buffers { nullptr };   ///< entry 0 is none
        std::vector<uint32_t>       freeBuffers;
        uint32_t AddBuffer(SDL_GPUBuffer* buffer);
        void     RemoveBuffer(uint32_t entry);
        SDL_GPUBuffer* Buffer(uint32_t entry) const { return entry < buffers.size() ? buffers[entry] : nullptr; }

        /// A GPU buffer of `usage` and a transfer buffer holding `data`, copied at the next window
        /// frame's start. Null on failure.
        SDL_GPUBuffer* CreateBuffer(SDL_GPUBufferUsageFlags usage, const void* data, uint32_t bytes);
        /// Lets go of a buffer: its pending copy, if any, isn't made.
        void           ReleaseBuffer(SDL_GPUBuffer*& buffer);

        // ---- Copies made at the next window frame's start -----------------------------
        struct TextureCopy
        {
            SDLGPUImage*           image = nullptr;
            SDL_GPUTransferBuffer* staging = nullptr;
        };
        struct BufferCopy
        {
            SDL_GPUBuffer*         buffer = nullptr;
            SDL_GPUTransferBuffer* staging = nullptr;
            uint32_t               bytes = 0;
        };
        std::mutex               copyMutex;   ///< images are made and let go on any thread
        std::vector<TextureCopy> textureCopies;
        std::vector<BufferCopy>  bufferCopies;
        /// Records the queued copies; resident buffers they make are Ready after.
        void RecordCopies(SDL_GPUCopyPass* pass);

        // ---- Resident buffers (RHI::UploadResidentBuffers) ----------------------------
        struct Resident
        {
            SDL_GPUBuffer* buffer = nullptr;
            uint32_t       entry = 0;
            bool           ready = false;
        };
        std::unordered_map<RHI::ResidentBufferId, Resident> residents;
        RHI::ResidentBufferId nextResident = 1;
        RHI::ResidentBufferId CreateResident(std::span<const RHI::UploadRequest> requests, std::span<DeviceAddress> addresses);
        RHI::ResidentState    ResidentState(RHI::ResidentBufferId id) const;
        void                  ReleaseResident(RHI::ResidentBufferId id);

        // ---- Images -------------------------------------------------------------------
        /// Every image made and not let go for a frame: Cleanup frees what's left.
        std::unordered_set<SDLGPUImage*> liveImages;
        SDLGPUImageSet imageSet;
        std::array<SDL_GPUSampler*, 3> samplers {};   ///< nearest repeat, linear repeat, linear clamp
        SDL_GPUTexture* blankImage = nullptr;         ///< bound where a draw has fewer images
        SDL_GPUSampler* SamplerFor(tachyon::Sampler sampler) const;

        // ---- What every draw uses -----------------------------------------------------
        std::array<SDL_GPUGraphicsPipeline*, kPrimitiveKindCount> pipelineObjects {};
        PipelineSet     pipelines {};       ///< pipeline: the object; layout: the kind
        SDL_GPUBuffer*  blankBuffer = nullptr;   ///< bound to the storage slots a draw doesn't use
        SDL_GPUBuffer*  quadIndices = nullptr;   ///< p * 4 + { 0, 1, 3, 0, 3, 2 } for each quad p
        uint32_t        quadCapacity = 0;
        /// Grows the quads' index buffer to hold `quads` (copied with the window frame's copies).
        bool EnsureQuadIndices(uint32_t quads);

        // ---- The frame's styles (PrepareFrame) ------------------------------------------
        SDL_GPUBuffer*          boxStyles = nullptr;
        SDL_GPUBuffer*          colorStyles = nullptr;
        std::vector<ImageIndex> gradientPages;   ///< each gradient page's index in the image set
        ImageBatcher            batcher;
        float                   elapsedTime = 0.0f;
        StyleManager*           lastStyles = nullptr;   ///< PrepareFrame's, for a live resize's redraw
        GradientManager*        lastGradients = nullptr;

        // ---- Scenes the app draws (RenderScene) ----------------------------------------
        static constexpr SDL_GPUTextureFormat kSceneColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        static constexpr uint64_t             kSceneKeepFrames  = 4;   ///< a scene not asked for this long goes

        /// What RenderScene keeps for a key: its color texture (in the image set) and depth,
        /// both in liveImages.
        struct Scene
        {
            SDLGPUImage* color = nullptr;
            SDLGPUImage* depth = nullptr;
            ImageIndex   index = ImageIndex::Invalid;
            uint2        extent {};
            uint64_t     lastFrame = 0;   ///< the frame it was last asked for
        };
        struct SceneRequest
        {
            ID      key;
            ID      window;
            color4f clear;
            std::function<void(const ScenePayload& scene)> record;
        };
        std::unordered_map<ID, Scene> scenes;
        std::vector<SceneRequest>     sceneRequests;
        uint64_t                      sceneFrame = 0;

        /// Clears and records the scenes asked for in `window` this frame, before its UI's
        /// render pass and outside any pass.
        void RecordScenes(SDL_GPUCommandBuffer* cmd, ID window);
        /// A texture the GPU draws into (a scene's), in liveImages so DestroyImageAtEnd frees
        /// it; null on failure.
        SDLGPUImage* CreateTargetImage(uint2 size, SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, Sampler sampler);
    };
}
