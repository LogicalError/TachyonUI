#pragma once

// Inside the Direct3D 12 backend: the device and what its graphics context and window contexts
// share. The queue's one fence counts every submit, the way the Vulkan backend's timeline does:
// a frame slot waits on it, and what's let go waits for it before it's freed.

#include <gui_core/graphics/d3d12/d3d12_common.h>
#include <gui_core/graphics/d3d12/d3d12_graphics_context.h>
#include <gui_core/graphics/gui_image.h>
#include <gui_core/graphics/gui_rhi.h>

#include <D3D12MemAlloc.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
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

namespace tachyon::d3d12
{
    /// A buffer in one of D3D12MA's heaps. An upload-heap buffer stays mapped (`mapped`).
    struct Buffer
    {
        ComPtr<D3D12MA::Allocation> allocation;
        void*                       mapped  = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS   address = 0;
        UINT64                      size    = 0;

        ID3D12Resource* Resource() const { return allocation ? allocation->GetResource() : nullptr; }
        explicit operator bool() const { return allocation != nullptr; }
    };

    /// An image (GraphicsContext::CreateImage): a texture sampled by the shaders. A host's
    /// (RegisterExternalImage) has no allocation: its texture stays the host's.
    class D3D12Image final : public Image
    {
    public:
        ComPtr<D3D12MA::Allocation> allocation;
        ID3D12Resource*             external = nullptr;
        DXGI_FORMAT                 format  = DXGI_FORMAT_UNKNOWN;
        Sampler                     sampler = Sampler::NearestRepeat;
        uint2                       size {};
        /// Made by CreateImage from pixels of this format: UpdateImageRows can write into it.
        std::optional<ImageFormat>  sourceFormat;

        ID3D12Resource* Resource() const { return allocation ? allocation->GetResource() : external; }
    };

    /// The static sampler (s0, s1, s2) the shaders sample an image with: its row in the
    /// per-image table at t7.
    uint32_t SamplerSlot(Sampler sampler);

    /// The images the shaders sample, by ImageIndex (1-based; the shaders' slot is one less).
    /// Their views are kept in a heap the CPU writes; each window frame copies the slots in use
    /// into its own shader-visible heap, so a slot pointed elsewhere never changes what a frame
    /// in flight reads.
    class D3D12ImageSet final : public ImageSet
    {
    public:
        bool Create(ID3D12Device* device, uint32_t capacity);

        void       Clear() override;
        ImageIndex Add(Image* image) override;
        void       Remove(ImageIndex index) override;
        /// A slot with no image yet, for one that comes later (a text atlas page).
        ImageIndex ReserveIndex();
        /// Points the slot at another image; the image it holds is no change.
        void       SetImage(ImageIndex index, D3D12Image* image);
        /// How many views were written into the heap.
        uint64_t   Writes() const { return m_writes; }

        uint32_t Capacity() const { return m_capacity; }
        /// One past the highest slot handed out: what a frame copies.
        uint32_t SlotsInUse() const { return uint32_t(m_images.size()); }
        D3D12_CPU_DESCRIPTOR_HANDLE CpuStart() const { return m_heap->GetCPUDescriptorHandleForHeapStart(); }
        /// Each slot's static sampler, as t7 holds them.
        const std::vector<uint32_t>& SamplerSlots() const { return m_samplers; }

    private:
        ImageIndex Allocate();
        void       WriteSlot(uint32_t slot);

        ID3D12Device*                m_device = nullptr;
        ComPtr<ID3D12DescriptorHeap> m_heap;
        UINT                         m_descriptorSize = 0;
        uint32_t                     m_capacity = 0;
        std::vector<D3D12Image*>     m_images;     ///< by slot; null for an empty one
        std::vector<uint32_t>        m_samplers;   ///< by slot
        std::vector<uint32_t>        m_free;       ///< slots handed back
        uint64_t                     m_writes = 0;
    };

    struct D3D12GraphicsContext::Impl
    {
        DebugConfig    debug;
        ExternalDevice external;   ///< the host's device and queue, when it gave them
        std::string    gpuPreference;
        std::string deviceName;
        bool        debugLayer = false;   ///< the debug layer is on: its messages go to `debug`
        uint32_t    imageSlots = 4096;    ///< asked for with SetImageSlotCount

        ComPtr<IDXGIFactory6>      factory;
        ComPtr<IDXGIAdapter1>      adapter;
        ComPtr<ID3D12Device>       device;
        ComPtr<ID3D12CommandQueue> queue;
        ComPtr<D3D12MA::Allocator> allocator;   ///< GPU memory: buffers and textures in its heaps
        ComPtr<ID3D12InfoQueue1>   infoQueue;
        DWORD                      messageCallback = 0;

        /// Every submit signals the next value: `submitted` is the last one signalled.
        ComPtr<ID3D12Fence> submitFence;
        /// Atomic: DestroyImageAtEnd, on any thread, reads it.
        std::atomic<UINT64> submitted = 0;
        /// The device was removed (a Present, a resize or the debug layer said so): told once,
        /// with the reason, and the GUI is told through DeviceLost.
        std::atomic<bool>   removed = false;
        /// Notes a failed call: logged, and when it says the device is gone, `removed` is set.
        void   NoteFailure(HRESULT hr, const char* what);
        HANDLE              waitEvent = nullptr;

        /// What the next submit waits for on the queue (AddFrameWait), a fence once each.
        std::vector<std::pair<ComPtr<ID3D12Fence>, UINT64>> frameWaits;
        /// Runs `list`, after the frame waits, and signals the fence's next value, which it returns.
        UINT64 Submit(ID3D12CommandList* list);
        /// Waits on the CPU until the fence reaches `value`.
        void   Wait(UINT64 value);
        /// Waits until everything submitted so far is done, presents included.
        void   WaitIdle();
        UINT64 Completed() const { return submitFence ? submitFence->GetCompletedValue() : 0; }

        // ---- Memory -------------------------------------------------------------------
        /// A buffer the CPU writes and the GPU reads, mapped. Empty on failure.
        Buffer CreateUploadBuffer(UINT64 size);
        /// A buffer in GPU memory, made ready for a copy into it.
        Buffer CreateGpuBuffer(UINT64 size);

        /// What's let go is freed once the GPU is past `afterValue`: the next submit
        /// (`submitted + 1`) for anything a window frame being recorded may use, which is
        /// everything let go while frames are built; 0 for what no command list refers to.
        void DestroyAfterGpu(ComPtr<D3D12MA::Allocation> allocation, UINT64 afterValue);
        void DestroyAfterGpu(std::unique_ptr<D3D12Image> image, UINT64 afterValue);
        void DestroyAfterGpu(ComPtr<ID3D12Resource> resource, UINT64 afterValue);
        /// Frees what the GPU is done with.
        void FlushDeletions();

        struct Garbage
        {
            UINT64                      afterValue = 0;
            ComPtr<D3D12MA::Allocation> allocation;
            std::unique_ptr<D3D12Image> image;
            ComPtr<ID3D12Resource>      resource;
        };
        std::mutex           garbageMutex;   ///< images are let go on any thread
        std::vector<Garbage> garbage;

        // ---- Uploads, recorded at the start of the next window frame -----------------
        /// A whole image's pixels, or with `partial` a band of rows from `firstRow` written
        /// into an image already up (UpdateImageRows): its other rows are kept.
        struct ImageUpload
        {
            D3D12Image*                        image = nullptr;
            Buffer                             staging;
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
            uint32_t                           firstRow = 0;
            bool                               partial  = false;
        };
        std::mutex               uploadMutex;   ///< images are made on any thread
        std::vector<ImageUpload> imageUploads;
        /// RecordPendingUploads' working lists, kept between frames for their storage.
        struct UploadScratch
        {
            std::vector<ImageUpload>            images;
            std::vector<D3D12_RESOURCE_BARRIER> barriers;
            std::vector<const D3D12Image*>      copying;
        } uploadScratch;

        /// Records the queued image and resident-buffer copies into `list`.
        void RecordPendingUploads(ID3D12GraphicsCommandList* list);

        // ---- Resident buffers (RHI::UploadResidentBuffers) ----------------------------
        struct Resident
        {
            Buffer gpu;
            Buffer staging;   ///< filled at once; copied into `gpu` by RecordPendingUploads
            bool   ready = false;
        };
        std::unordered_map<RHI::ResidentBufferId, Resident> residents;
        /// Made and not copied yet, oldest first; one let go of meanwhile is no longer in
        /// `residents` and is passed over. Ids are never handed out twice.
        std::vector<RHI::ResidentBufferId> pendingResidents;
        RHI::ResidentBufferId nextResident = 1;

        RHI::ResidentBufferId CreateResidentBuffer(std::span<const RHI::UploadRequest> requests,
                                                   std::span<DeviceAddress> addresses);
        RHI::ResidentState    ResidentState(RHI::ResidentBufferId id) const;
        void                  ReleaseResident(RHI::ResidentBufferId id);

        // ---- Images ------------------------------------------------------------------------
        /// Every image made and not let go for a frame (DeviceScope::Frame): those handed over
        /// for the application's lifetime too. Cleanup frees what's left.
        std::unordered_set<D3D12Image*> liveImages;

        // ---- What every frame draws with ------------------------------------------------
        D3D12ImageSet                imageSet;
        /// The box styles in GPU memory (a resident buffer), made again when they change; 0: none.
        RHI::ResidentBufferId        boxStyles   = 0;
        /// The color styles, with their gradient pages' slots, likewise.
        RHI::ResidentBufferId        colorStyles = 0;
        std::vector<ImageIndex>      gradientPages; ///< each gradient page's index in the image set
        StyleManager*                lastStyles    = nullptr;   ///< PrepareFrame's, for a live resize's redraw
        GradientManager*             lastGradients = nullptr;
        float                        lastTime      = 0.0f;
        ComPtr<ID3D12RootSignature>  rootSignature;
        std::vector<ComPtr<ID3D12PipelineState>> pipelineStates;   ///< by PrimitiveKind
        std::array<RHI::GpuPipeline, kPrimitiveKindCount> pipelines {};
        /// p * 4 + { 0, 1, 3, 0, 3, 2 } for each quad p, in GPU memory.
        Buffer                       quadIndices;
        uint32_t                     quadCapacity = 0;
        GPUDrawPushConstants         pushConstants {};   ///< the styles' addresses and the time, from PrepareFrame

        /// Grows the quads' index buffer to hold `quads`, its copy recorded into `list` before
        /// the draw that needs it; false when it can't.
        bool EnsureQuadIndices(ID3D12GraphicsCommandList* list, uint32_t quads);

        // ---- Scenes the app draws (RenderScene) ----------------------------------------
        static constexpr DXGI_FORMAT kSceneColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        static constexpr uint64_t    kSceneKeepFrames  = 4;   ///< a scene not asked for this long goes

        /// What RenderScene keeps for a key: its color texture (in the image set) and depth,
        /// both in liveImages, and a view of each.
        struct Scene
        {
            D3D12Image*                  color = nullptr;
            D3D12Image*                  depth = nullptr;
            ComPtr<ID3D12DescriptorHeap> rtv;
            ComPtr<ID3D12DescriptorHeap> dsv;
            ImageIndex                   index = ImageIndex::Invalid;
            uint2                        extent {};
            color4f                      clear {};        ///< the color texture's optimized clear
            uint64_t                     lastFrame = 0;   ///< the frame it was last asked for
        };
        struct SceneRequest
        {
            ID      key;
            ID      window;
            color4f clear;
            std::function<void(const ScenePayload& scene)> record;
        };
        std::unordered_map<ID, Scene> scenes;

        // ---- A host's textures (RegisterExternalImage, SetWindowImageTarget) -----------
        /// By image index: what the image set points at for a host's texture.
        std::unordered_map<uint32_t, std::unique_ptr<D3D12Image>> externalImages;
        /// The texture a render-to-image window is copied into, by window.
        std::unordered_map<ID, ID3D12Resource*> imageTargets;
        std::vector<SceneRequest>     sceneRequests;
        uint64_t                      sceneFrame = 0;

        /// Records the scenes asked for in `window` this frame, before its UI.
        void RecordScenes(ID3D12GraphicsCommandList* list, ID window);

        /// A texture the GPU draws into (a scene's), in liveImages so DestroyImageAtEnd frees
        /// it; null on failure. `clear`: its optimized clear value, or null when it's cleared to
        /// values not known in advance.
        D3D12Image* CreateTargetImage(uint2 size, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state,
                                      const D3D12_CLEAR_VALUE* clear, Sampler sampler);
    };
}
