#pragma once

// The SDL GPU graphics backend (TACHYON_SDLGPU): SDL3's GPU API, which renders with Vulkan or
// Direct3D 12, whichever SDL picks (Metal isn't built yet): the UI in OS, layered and
// render-to-image windows, and the app's scenes (RenderScene).

#include <gui_core/graphics/null/gui_null_graphics_context.h>

#include <SDL3/SDL_gpu.h>

#include <memory>
#include <string>

struct SDL_Window;

namespace tachyon::sdlgpu
{
    /// @addtogroup graphics
    /// @{

    /// What GraphicsContext::RenderScene's `record` gets on SDL GPU (ScenePayload::As): the
    /// window frame's command buffer, outside any pass, and the scene's color and depth
    /// textures, cleared already (color to the scene's clear color, depth to 1). Record copy
    /// passes and render passes into it; `colorTarget` and `depthTarget` begin a render pass on
    /// the textures that keeps what's there (SDL_BeginGPURenderPass(commandBuffer, &colorTarget,
    /// 1, &depthTarget)). End every pass you begin; the UI's passes follow in the same buffer.
    struct SDLGPUSceneRender
    {
        static constexpr GraphicsAPI kAPI = GraphicsAPI::SDLGPU;

        SDL_GPUDevice*                device = nullptr;
        SDL_GPUCommandBuffer*         commandBuffer = nullptr;
        SDL_GPUTexture*               colorTexture = nullptr;
        SDL_GPUTexture*               depthTexture = nullptr;
        SDL_GPUTextureFormat          colorFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
        SDL_GPUTextureFormat          depthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
        uint2                         extent {};
        SDL_GPUColorTargetInfo        colorTarget {};
        SDL_GPUDepthStencilTargetInfo depthTarget {};
    };

    /// How serious a message from SDL GPU's debug mode is.
    enum class DebugSeverity : uint8_t { Info, Warning, Error };

    /// Where SDL GPU's messages go. Without a callback they're logged as SDL logs them, and debug
    /// mode is on in debug builds only. Tests set one, so a message fails them.
    struct DebugConfig
    {
        void (*callback)(DebugSeverity severity, const char* message, void* userData) = nullptr;
        void* userData = nullptr;
    };

    class SDLGPUGraphicsContext : public NullGraphicsContext
    {
    public:
        /// The SDL window of a window backend's window (OSWindowHandle): SDLWindowBackend sets it.
        using WindowResolver = SDL_Window* (*)(OSWindowHandle window);

        SDLGPUGraphicsContext();
        ~SDLGPUGraphicsContext() override;

        GraphicsAPI      GetAPI() const override { return GraphicsAPI::SDLGPU; }
        /// The GPU, and the driver SDL renders with: "NVIDIA GeForce RTX 4070 (vulkan)".
        std::string_view GetDeviceName() const override;
        /// "integrated" or "cpu" ask SDL for its low-power GPU; SDL picks among the others.
        void             SetGpuPreference(std::string_view preference) override;
        /// The driver SDL renders with: "vulkan", "direct3d12" or "metal"; empty, SDL's choice.
        /// Call before Initialize.
        void             SetDriver(std::string_view driver);
        /// The driver in use, after Initialize.
        std::string_view GetDriver() const;
        /// Call before Initialize.
        void             SetDebugConfig(const DebugConfig& config);
        void             SetWindowResolver(WindowResolver resolver);

        /// 4096 by default. Call before Initialize.
        void             SetImageSlotCount(uint32_t slots) override;
        uint32_t         GetImageSlotCount() const override;

        /// Makes the device, on a driver that takes the shaders the GUI has (SPIR-V, DXIL) and,
        /// on Vulkan, has the 16-bit types they use. Fails when SDL finds none.
        Result<void> Initialize(IWindowBackend* windowBackend) override;
        void         Cleanup() override;

        /// R8G8B8 is widened to R8G8B8A8, and R8 and R8G8 in sRGB too (SDL GPU has no such
        /// formats). `mipmapped` makes the base level only, as the samplers read no other. The
        /// pixels go to the GPU with the next window frame.
        Image* CreateImage(void* data, uint2 size, ImageFormat format, Sampler sampler, bool mipmapped = false) override;
        void   DestroyImageAtEnd(Image* img, DeviceScope scope, const std::source_location& loc = std::source_location::current()) override;
        TextAtlasManager* CreateTextAtlasManager() override;

        ImageSet*  GetImageSet() const override;
        ImageIndex RegisterImage(Image* image) override;
        void       UnregisterImage(ImageIndex index) override;

        /// The gradient pages into the image set, the styles uploaded where they changed, and
        /// the images each style reads, for cutting draws (ImageBatcher).
        void PrepareFrame(StyleManager& styleManager, GradientManager& gradientManager, float elapsedTimeSecs) override;
        /// PrepareFrame again with its last arguments: a live resize's redraw changes styles.
        void RePrepareFrame();

        /// An OS window is claimed for the device: SDL makes its swapchain. A layered one
        /// (transparent, on Windows) isn't: its frames are read back and shown with
        /// UpdateLayeredWindow. A render-to-image window's frames are read back.
        IWindowContext* CreateWindowContext(void* nativeWindowHandle, OSWindowHandle osHandle, ID id, int2 size,
                                            WindowFlags windowFlags, OnGuiFunc onGui) override;
        void            DestroyWindowContext(IWindowContext* context) override;

        /// Lets go of the scenes no longer asked for.
        void BeginFrame() override;
        /// The scene's color texture (R8G8B8A8_UNORM, sampled with Sampler::LinearClampToEdge)
        /// and depth (D32_FLOAT), drawn by `record` in `window`'s next frame before its UI.
        ImageIndex RenderScene(ID key, ID window, uint2 size, color4f clear,
                               std::function<void(const ScenePayload& scene)> record) override;

        /// Waits until the GPU has done all work submitted so far.
        void WaitIdle();

        struct Impl;
        /// Library internal: the device and what the window contexts share.
        Impl& Internal() { return *m_impl; }

    private:
        std::unique_ptr<Impl> m_impl;
    };
    /// @}
}
