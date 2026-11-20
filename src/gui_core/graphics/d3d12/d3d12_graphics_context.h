#pragma once

// The Direct3D 12 graphics backend, on Windows (TACHYON_D3D12): the UI in OS, layered and
// render-to-image windows, and the app's scenes (RenderScene).

#include <gui_core/graphics/null/gui_null_graphics_context.h>

#include <dxgiformat.h>

#include <memory>
#include <string>

struct ID3D12CommandQueue;
struct ID3D12Device;
struct ID3D12Fence;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

namespace tachyon::d3d12
{
    /// @addtogroup graphics
    /// @{

    /// What GraphicsContext::RenderScene's `record` gets on Direct3D 12 (ScenePayload::As): the
    /// window frame's command list with the scene's color and depth textures set as its render
    /// targets (color cleared to the scene's clear color, depth to 1), and the viewport and
    /// scissor set to all of it. Draw with your own root signature, pipeline and descriptor
    /// heaps; the UI sets its own again after. The textures are in the RENDER_TARGET and
    /// DEPTH_WRITE states: leave them so.
    struct D3D12SceneRender
    {
        static constexpr GraphicsAPI kAPI = GraphicsAPI::D3D12;

        ID3D12GraphicsCommandList* commandList = nullptr;
        DXGI_FORMAT                colorFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT                depthFormat = DXGI_FORMAT_UNKNOWN;
        uint2                      extent {};
        /// The views the targets are set with, as D3D12_CPU_DESCRIPTOR_HANDLE::ptr: to clear
        /// part of them, or set them again after drawing elsewhere.
        size_t                     renderTargetView = 0;
        size_t                     depthStencilView = 0;
    };

    /// How serious a debug-layer message is.
    enum class DebugSeverity : uint8_t { Info, Warning, Error };

    /// Where the debug layer's messages go. Without a callback they're logged, and the debug
    /// layer is on in debug builds only. Tests set one, so a message fails them.
    struct DebugConfig
    {
        void (*callback)(DebugSeverity severity, const char* message, void* userData) = nullptr;
        void* userData = nullptr;
    };

    /// A host's device and queue, for the GUI to render on (SetExternalDevice) instead of making
    /// its own. The GUI holds a reference to each while it runs, and releases it at Cleanup.
    struct ExternalDevice
    {
        /// Runs Shader Model 6.6 with resource binding tier 3 (CheckDeviceSupport).
        ID3D12Device*       device = nullptr;
        /// A direct queue of `device`: every GUI submit, wait and present goes here. Direct3D 12
        /// queues are free-threaded, so the host submits to it from any thread.
        ID3D12CommandQueue* queue = nullptr;
    };

    class D3D12GraphicsContext : public NullGraphicsContext
    {
    public:
        D3D12GraphicsContext();
        ~D3D12GraphicsContext() override;

        /// Whether `device` runs the GUI's shaders; what it lacks, one line each, in `problems`.
        static bool CheckDeviceSupport(ID3D12Device* device, std::string* problems = nullptr);
        /// Render on the host's device and queue instead of making them. Call before Initialize,
        /// which fails when they can't be used. The debug layer is the host's to turn on; its
        /// messages go to the DebugConfig. A device the host made isn't checked for leaks at
        /// Cleanup, as the host's own references remain.
        void SetExternalDevice(const ExternalDevice& external);

        GraphicsAPI      GetAPI() const override { return GraphicsAPI::D3D12; }
        std::string_view GetDeviceName() const override;
        /// The device was removed. This backend doesn't make a new one: the app's frames end
        /// with the error, for it to start again.
        bool DeviceLost() const override;
        void             SetGpuPreference(std::string_view preference) override;
        /// 4096 by default, 1,000,000 at most (a shader-visible descriptor heap's size). Call
        /// before Initialize.
        void             SetImageSlotCount(uint32_t slots) override;
        uint32_t         GetImageSlotCount() const override;
        /// Call before Initialize.
        void             SetDebugConfig(const DebugConfig& config);

        /// Picks a GPU that runs Shader Model 6.6 with resource binding tier 3 (the shaders'
        /// descriptor heap indexing) and makes the device, its queue and the GUI's pipelines.
        /// Fails when none can, which a Windows 10 without the newer D3D12 runtime is.
        Result<void> Initialize(IWindowBackend* windowBackend) override;
        void         Cleanup() override;
        /// Frees what the GPU is done with, and resets the renderer.
        void         BeginFrame() override;

        /// R8G8B8 is widened to R8G8B8A8 (Direct3D 12 has no 3-byte format), and R8 and R8G8 in
        /// sRGB too (it has no sRGB format of fewer channels). `mipmapped` makes the base level
        /// only: the samplers read nothing else, as on Vulkan. The pixels go to the GPU with the
        /// next window frame.
        Image* CreateImage(void* data, uint2 size, ImageFormat format, Sampler sampler, bool mipmapped = false) override;
        bool   UpdateImageRows(Image* image, uint32_t firstRow, uint32_t rowCount, const void* data) override;
        void   DestroyImageAtEnd(Image* img, DeviceScope scope, const std::source_location& loc = std::source_location::current()) override;

        TextAtlasManager* CreateTextAtlasManager() override;

        /// An OS window gets a swapchain, a layered (transparent) one none: its frames are read
        /// back and shown with UpdateLayeredWindow. A render-to-image window's are read back.
        IWindowContext* CreateWindowContext(void* nativeWindowHandle, OSWindowHandle osHandle, ID id, int2 size,
                                            WindowFlags windowFlags, OnGuiFunc onGui) override;
        void            DestroyWindowContext(IWindowContext* context) override;

        ImageSet*  GetImageSet() const override;
        ImageIndex RegisterImage(Image* image) override;
        void       UnregisterImage(ImageIndex index) override;

        /// The gradient pages into the image set, and the styles uploaded where they changed.
        void PrepareFrame(StyleManager& styleManager, GradientManager& gradientManager, float elapsedTimeSecs) override;
        /// PrepareFrame again with its last arguments: a live resize's redraw changes styles.
        void RePrepareFrame();

        /// The scene's color texture (R8G8B8A8_UNORM, sampled with Sampler::LinearClampToEdge)
        /// and depth (D32_FLOAT), drawn by `record` in `window`'s next frame before its UI.
        ImageIndex RenderScene(ID key, ID window, uint2 size, color4f clear,
                               std::function<void(const ScenePayload& scene)> record) override;

        // ---- A host's own textures, on its device (SetExternalDevice) -----------------------

        /// A texture the host renders (a 3D viewport) in the image set, for the Image control
        /// or a primitive to sample with `sampler`. The texture stays the host's and must
        /// outlive every GUI frame that samples it. Call it while the UI is built (OnGUI).
        ///
        /// The GUI's frame reads it in D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE: the host leaves
        /// it so in work submitted to the GUI's queue before the GUI's frame (which orders the
        /// two), and moves it out only in work submitted after.
        ImageIndex RegisterExternalImage(ID3D12Resource* texture, Sampler sampler);
        /// Points an existing index at another texture, for example after a resize, under the
        /// same rules. No GUI frame in flight may still sample the old one (WaitIdle).
        void       UpdateExternalImage(ImageIndex index, ID3D12Resource* texture, Sampler sampler);

        /// The host texture a render-to-image window (WindowFlags::RenderToImage) is copied
        /// into each frame; null stops. Can be set before the window exists. A
        /// DXGI_FORMAT_B8G8R8A8_UNORM texture the window's size: a texture of another size gets
        /// the part they share, as it isn't scaled. The GUI's frame moves it from
        /// D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE to COPY_DEST, copies, and leaves it in
        /// ALL_SHADER_RESOURCE; the host reads it in work submitted after the GUI's frame and
        /// leaves it in that state.
        void SetWindowImageTarget(ID window, ID3D12Resource* texture);

        /// The GUI's next frame waits on the queue for `fence` to reach `value`: for work the
        /// host submits to another queue (async compute) that the GUI shows. Waits on the same
        /// fence merge (the higher value). Call it while the UI is built (OnGUI) or before the
        /// frame; the frame after that one doesn't wait on it again.
        void AddFrameWait(ID3D12Fence* fence, uint64_t value);

        /// Waits until the GPU has done all work submitted so far.
        void WaitIdle();

        /// How many image views the image set has written: a slot set to the image it holds
        /// writes none.
        uint64_t ImageViewWrites() const;

        struct Impl;
        /// Library internal: the device, its queue, and what the window contexts draw with.
        Impl& Internal() { return *m_impl; }

    private:
        /// The host's device and queue, checked (SetExternalDevice).
        Result<void> AdoptExternalDevice();

        std::unique_ptr<Impl> m_impl;
    };
    /// @}
}
