#pragma once

// The Direct3D 12 graphics backend, on Windows (TACHYON_D3D12). It's being built (#248): it
// draws the UI into its OS windows. Render-to-image windows are still the null backend's
// stand-in (so their images stay empty), as is drawing a 3D scene.

#include <gui_core/graphics/null/gui_null_graphics_context.h>

#include <memory>
#include <string>

namespace tachyon::d3d12
{
    /// @addtogroup graphics
    /// @{

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

        /// An OS window gets a swapchain; a render-to-image window, for now, the null stand-in.
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
