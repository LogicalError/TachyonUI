#pragma once

// Inside the Metal backend (Objective-C++ only): the device and what its graphics context and
// window contexts share. Every commit counts up `submitted`, the way the Vulkan backend's
// timeline does: a frame slot waits for its count, and what's let go waits for it before it's
// freed.
//
// A buffer the shaders read is named by a DeviceAddress the backend makes up: the buffer's
// handle in the top 32 bits and the offset in the low 32 (Resolve). Draws bind the buffer at
// that offset, so the shaders need no pointers.

#import <Metal/Metal.h>

#include <gui_core/graphics/gui_image.h>
#include <gui_core/graphics/gui_render_primitives.h>
#include <gui_core/graphics/gui_rhi.h>
#include <gui_core/graphics/metal/metal_graphics_context.h>

#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tachyon
{
    class StyleManager;
    class GradientManager;
}

namespace tachyon::metal
{
    /// What a window frame draws into, as on the other APIs: 8-bit BGRA, and 32-bit float depth.
    inline constexpr MTLPixelFormat kDrawFormat  = MTLPixelFormatBGRA8Unorm;
    inline constexpr MTLPixelFormat kDepthFormat = MTLPixelFormatDepth32Float;

    /// The most images the shaders' image table holds (all_common.slang's kMetalMaxImages).
    inline constexpr uint32_t kMaxImageSlots = 500000;

    /// Where the shaders find their resources (all_common.slang, sdf_color.slang): buffer,
    /// texture and sampler indices, the same in the vertex and the fragment stage.
    namespace binding
    {
        inline constexpr NSUInteger kConstants     = 0;   ///< RootConstants
        inline constexpr NSUInteger kPrimitives    = 1;   ///< + PrimitiveSpec::pushSlot (1 to 5)
        inline constexpr NSUInteger kBoxStyles     = 6;
        inline constexpr NSUInteger kColorStyles   = 7;
        inline constexpr NSUInteger kImageSamplers = 8;   ///< each image slot's sampler, a uint each
        inline constexpr NSUInteger kImages        = 9;   ///< each image slot's MTLResourceID
        inline constexpr NSUInteger kSamplers      = 0;   ///< nearest repeat, linear repeat, linear clamp
    }

    /// The constants each draw sets (setVertexBytes): the world matrix's four columns, the time,
    /// and the draw's first vertex (each draw's vertices start at 0).
    struct RootConstants
    {
        float    worldColumns[16];
        float    elapsedTime;
        uint32_t firstVertex;
        uint32_t _pad[2];
    };
    static_assert(sizeof(RootConstants) == 80);

    /// A buffer the CPU writes and the GPU reads (shared storage), and its handle for addresses.
    struct Buffer
    {
        id<MTLBuffer> buffer = nil;
        uint32_t      handle = 0;    ///< 0: none
        size_t        size   = 0;

        void*         Mapped() const { return buffer != nil ? buffer.contents : nullptr; }
        DeviceAddress Address(size_t offset) const { return (DeviceAddress(handle) << 32) | DeviceAddress(offset); }
        explicit operator bool() const { return buffer != nil; }
    };

    /// An image (GraphicsContext::CreateImage): a texture sampled by the shaders. A host's
    /// (RegisterExternalImage) is its texture; `external` says so.
    class MetalImage final : public Image
    {
    public:
        id<MTLTexture>             texture = nil;
        bool                       external = false;
        MTLPixelFormat             format  = MTLPixelFormatInvalid;
        Sampler                    sampler = Sampler::NearestRepeat;
        uint2                      size {};
        /// Made by CreateImage from pixels of this format: UpdateImageRows can write into it.
        std::optional<ImageFormat> sourceFormat;
    };

    /// The sampler (0, 1, 2) the shaders sample an image with: its entry in the per-image table.
    uint32_t SamplerSlot(Sampler sampler);

    /// The images the shaders sample, by ImageIndex (1-based; the shaders' slot is one less).
    /// Each window frame writes the slots' textures into its own table (MTLResourceIDs), so a
    /// slot pointed elsewhere never changes what a frame in flight reads.
    class MetalImageSet final : public ImageSet
    {
    public:
        void Create(uint32_t capacity) { m_capacity = capacity; }

        void       Clear() override;
        ImageIndex Add(Image* image) override;
        void       Remove(ImageIndex index) override;
        /// A slot with no image yet, for one that comes later (a text atlas page).
        ImageIndex ReserveIndex();
        /// Points the slot at another image.
        void       SetImage(ImageIndex index, MetalImage* image);

        uint32_t Capacity() const { return m_capacity; }
        /// One past the highest slot handed out: what a frame writes.
        uint32_t SlotsInUse() const { return uint32_t(m_images.size()); }
        const std::vector<MetalImage*>& Images() const { return m_images; }
        /// Each slot's sampler (SamplerSlot), as the shaders' table holds them.
        const std::vector<uint32_t>&    SamplerSlots() const { return m_samplers; }

    private:
        ImageIndex Allocate();

        uint32_t                 m_capacity = 0;
        std::vector<MetalImage*> m_images;     ///< by slot; null for an empty one
        std::vector<uint32_t>    m_samplers;   ///< by slot
        std::vector<uint32_t>    m_free;       ///< slots handed back
    };

    struct MetalGraphicsContext::Impl
    {
        ExternalDevice external;      ///< the host's device and queue, when it gave them
        std::string    gpuPreference;
        std::string    deviceName;
        uint32_t       imageSlots = 4096;   ///< asked for with SetImageSlotCount

        id<MTLDevice>       device = nil;
        id<MTLCommandQueue> queue  = nil;

        // ---- Commits ---------------------------------------------------------------------
        /// Every commit counts the next value: `submitted` is the last one. Atomic:
        /// DestroyImageAtEnd, on any thread, reads it.
        std::atomic<uint64_t> submitted = 0;
        /// The command buffers committed and not seen complete, oldest first.
        std::mutex                                           inFlightMutex;
        std::deque<std::pair<uint64_t, id<MTLCommandBuffer>>> inFlight;
        uint64_t                                             completed = 0;
        /// A command buffer failed in a way that loses the device: told once, and the GUI is
        /// told through DeviceLost.
        std::atomic<bool>                                    removed = false;

        /// Commits `commandBuffer` and counts it; its value.
        uint64_t Commit(id<MTLCommandBuffer> commandBuffer);
        /// Waits on the CPU until the commit counted `value` (and every one before) is done.
        void     Wait(uint64_t value);
        /// Waits until everything committed so far is done.
        void     WaitIdle();
        /// The highest value all commits up to are done.
        uint64_t Completed();

        // ---- Memory ----------------------------------------------------------------------
        /// Every buffer the shaders read, by its handle (Buffer::handle); handles are never
        /// handed out twice.
        std::unordered_map<uint32_t, id<MTLBuffer>> buffers;
        uint32_t                                    nextBufferHandle = 1;
        /// A shared buffer the shaders can read, with a handle. Empty on failure.
        Buffer CreateBuffer(size_t size);
        /// Its handle given back: draws recorded with it keep the buffer (Metal holds it).
        void   ReleaseBuffer(Buffer& buffer);
        /// The buffer and offset an address names; nil for 0 or a buffer let go of.
        std::pair<id<MTLBuffer>, NSUInteger> Resolve(DeviceAddress address) const;

        /// What's let go is freed once the GPU is past `afterValue`: the next commit
        /// (`submitted + 1`) for anything a window frame being recorded may use.
        void DestroyAfterGpu(std::unique_ptr<MetalImage> image, uint64_t afterValue);
        void DestroyAfterGpu(id<MTLResource> resource, uint64_t afterValue);
        /// Frees what the GPU is done with.
        void FlushDeletions();

        struct Garbage
        {
            uint64_t                    afterValue = 0;
            std::unique_ptr<MetalImage> image;
            id<MTLResource>             resource = nil;
        };
        std::mutex           garbageMutex;   ///< images are let go on any thread
        std::vector<Garbage> garbage;

        // ---- Uploads, recorded at the start of the next window frame -------------------
        /// A whole image's pixels, or with `partial` a band of rows from `firstRow` written
        /// into an image already up (UpdateImageRows): its other rows are kept.
        struct ImageUpload
        {
            MetalImage*   image = nullptr;
            id<MTLBuffer> staging = nil;
            NSUInteger    rowPitch = 0;
            uint32_t      firstRow = 0;
            uint32_t      rowCount = 0;
        };
        std::mutex               uploadMutex;   ///< images are made on any thread
        std::vector<ImageUpload> imageUploads;
        std::vector<ImageUpload> uploadScratch;

        /// Records the queued image copies into `commandBuffer`, ahead of its draws.
        void RecordPendingUploads(id<MTLCommandBuffer> commandBuffer);

        // ---- Resident buffers (RHI::UploadResidentBuffers) ------------------------------
        /// Shared memory: written at once and never again, so ready for the draws recorded
        /// from now on.
        std::unordered_map<RHI::ResidentBufferId, Buffer> residents;
        RHI::ResidentBufferId nextResident = 1;

        RHI::ResidentBufferId CreateResidentBuffer(std::span<const RHI::UploadRequest> requests,
                                                   std::span<DeviceAddress> addresses);
        RHI::ResidentState    ResidentState(RHI::ResidentBufferId id) const;
        void                  ReleaseResident(RHI::ResidentBufferId id);

        // ---- Images ----------------------------------------------------------------------
        /// Every image made and not let go for a frame (DeviceScope::Frame): those handed over
        /// for the application's lifetime too. Cleanup frees what's left.
        std::unordered_set<MetalImage*> liveImages;

        // ---- What every frame draws with ----------------------------------------------
        MetalImageSet                imageSet;
        /// The box styles (a resident buffer), made again when they change; 0: none.
        RHI::ResidentBufferId        boxStyles   = 0;
        /// The color styles, with their gradient pages' slots, likewise.
        RHI::ResidentBufferId        colorStyles = 0;
        std::vector<ImageIndex>      gradientPages; ///< each gradient page's index in the image set
        StyleManager*                lastStyles    = nullptr;   ///< PrepareFrame's, for a live resize's redraw
        GradientManager*             lastGradients = nullptr;
        float                        lastTime      = 0.0f;
        /// By PrimitiveKind; GpuPipeline::pipeline is the id<MTLRenderPipelineState>, unretained.
        std::vector<id<MTLRenderPipelineState>>            pipelineStates;
        std::array<RHI::GpuPipeline, kPrimitiveKindCount>  pipelines {};
        id<MTLDepthStencilState>     depthTest  = nil;   ///< GREATER_EQUAL, no writes
        id<MTLDepthStencilState>     depthWrite = nil;   ///< GREATER_EQUAL, writes (occluders)
        std::array<id<MTLSamplerState>, 3> samplers {};  ///< SamplerSlot's order
        /// p * 4 + { 0, 1, 3, 0, 3, 2 } for each quad p.
        id<MTLBuffer>                quadIndices = nil;
        uint32_t                     quadCapacity = 0;
        /// Bound where a draw's buffer address is 0, so every buffer the shaders declare is bound.
        id<MTLBuffer>                emptyBuffer = nil;
        GPUDrawPushConstants         pushConstants {};   ///< the styles' addresses and the time, from PrepareFrame

        /// Grows the quads' index buffer to hold `quads`; false when it can't.
        bool EnsureQuadIndices(uint32_t quads);

        // ---- Scenes the app draws (RenderScene) ----------------------------------------
        static constexpr MTLPixelFormat kSceneColorFormat = MTLPixelFormatRGBA8Unorm;
        static constexpr uint64_t       kSceneKeepFrames  = 4;   ///< a scene not asked for this long goes

        /// What RenderScene keeps for a key: its color texture (in the image set) and depth,
        /// both in liveImages.
        struct Scene
        {
            MetalImage* color = nullptr;
            MetalImage* depth = nullptr;
            ImageIndex  index = ImageIndex::Invalid;
            uint2       extent {};
            uint64_t    lastFrame = 0;   ///< the frame it was last asked for
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

        /// By image index: what the image set points at for a host's texture.
        std::unordered_map<uint32_t, std::unique_ptr<MetalImage>> externalImages;

        /// Records the scenes asked for in `window` this frame, before its UI.
        void RecordScenes(id<MTLCommandBuffer> commandBuffer, ID window);

        /// A texture the GPU draws into (a scene's), in liveImages so DestroyImageAtEnd frees it;
        /// null on failure.
        MetalImage* CreateTargetImage(uint2 size, MTLPixelFormat format, MTLTextureUsage usage, Sampler sampler);
    };
}
