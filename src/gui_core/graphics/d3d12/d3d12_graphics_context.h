#pragma once

// The Direct3D 12 graphics backend, on Windows (TACHYON_D3D12): the UI in OS, layered and
// render-to-image windows, and the app's scenes (RenderScene).

#include <gui_core/graphics/null/gui_null_graphics_context.h>

#include <dxgiformat.h>

#include <memory>
#include <string>

struct ID3D12GraphicsCommandList;

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

    class D3D12GraphicsContext : public NullGraphicsContext
    {
    public:
        D3D12GraphicsContext();
        ~D3D12GraphicsContext() override;

        GraphicsAPI      GetAPI() const override { return GraphicsAPI::D3D12; }
        std::string_view GetDeviceName() const override;
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

        /// Waits until the GPU has done all work submitted so far.
        void WaitIdle();

        struct Impl;
        /// Library internal: the device, its queue, and what the window contexts draw with.
        Impl& Internal() { return *m_impl; }

    private:
        std::unique_ptr<Impl> m_impl;
    };
    /// @}
}
