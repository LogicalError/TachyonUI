#pragma once

// The Metal graphics backend, on macOS (TACHYON_METAL): the UI in OS and render-to-image
// windows, and the app's scenes (RenderScene). This header is plain C++: the Metal objects it
// names are `void*` here, an `id<MTL...>` to Objective-C++ (bridge them with
// `(__bridge id<MTLDevice>)pointer`).

#include <gui_core/graphics/null/gui_null_graphics_context.h>

#include <memory>
#include <string>

namespace tachyon::metal
{
    /// @addtogroup graphics
    /// @{

    /// What GraphicsContext::RenderScene's `record` gets on Metal (ScenePayload::As): the window
    /// frame's command buffer and a render command encoder on the scene's color and depth
    /// textures (color cleared to the scene's clear color, depth to 1), with the viewport set
    /// to all of it. Draw with your own pipeline and depth state, and don't end the encoder:
    /// the backend ends it, and the UI samples the color texture after.
    struct MetalSceneRender
    {
        static constexpr GraphicsAPI kAPI = GraphicsAPI::Metal;

        void*    commandBuffer = nullptr;   ///< id<MTLCommandBuffer>
        void*    encoder       = nullptr;   ///< id<MTLRenderCommandEncoder>
        void*    colorTexture  = nullptr;   ///< id<MTLTexture>
        void*    depthTexture  = nullptr;   ///< id<MTLTexture>
        uint32_t colorFormat   = 0;         ///< MTLPixelFormat: MTLPixelFormatRGBA8Unorm
        uint32_t depthFormat   = 0;         ///< MTLPixelFormat: MTLPixelFormatDepth32Float
        uint2    extent {};
    };

    /// A host's device and queue, for the GUI to render on (SetExternalDevice) instead of making
    /// its own. The GUI holds a reference to each while it runs, and lets go at Cleanup.
    struct ExternalDevice
    {
        void* device = nullptr;   ///< id<MTLDevice>, of the Metal 3 GPU family
        void* queue  = nullptr;   ///< id<MTLCommandQueue> of `device`: every GUI frame is committed to it
    };

    class MetalGraphicsContext : public NullGraphicsContext
    {
    public:
        MetalGraphicsContext();
        ~MetalGraphicsContext() override;

        /// Whether `device` (an id<MTLDevice>) runs the GUI's shaders; what it lacks, one line
        /// each, in `problems`. It needs the Metal 3 GPU family, for its bindless images.
        static bool CheckDeviceSupport(void* device, std::string* problems = nullptr);
        /// Render on the host's device and queue instead of making them. Call before Initialize,
        /// which fails when they can't be used.
        void SetExternalDevice(const ExternalDevice& external);

        GraphicsAPI      GetAPI() const override { return GraphicsAPI::Metal; }
        std::string_view GetDeviceName() const override;
        bool             DeviceLost() const override;
        void             SetGpuPreference(std::string_view preference) override;
        /// 4096 by default, 500,000 at most (an argument buffer's textures). Call before
        /// Initialize.
        void             SetImageSlotCount(uint32_t slots) override;
        uint32_t         GetImageSlotCount() const override;

        /// Picks a GPU of the Metal 3 family (the system's default unless the app or TACHYON_GPU
        /// asks for another), and makes its queue and the GUI's pipelines from the built-in
        /// shaders.
        Result<void> Initialize(IWindowBackend* windowBackend) override;
        void         Cleanup() override;
        /// Frees what the GPU is done with, and resets the renderer.
        void         BeginFrame() override;

        /// R8G8B8 is widened to R8G8B8A8 (Metal has no 3-byte format), and R8 and R8G8 in sRGB
        /// too (macOS has no sRGB format of fewer channels). `mipmapped` makes the base level
        /// only: the samplers read nothing else, as on the other APIs. The pixels go to the GPU
        /// with the next window frame.
        Image* CreateImage(void* data, uint2 size, ImageFormat format, Sampler sampler, bool mipmapped = false) override;
        bool   UpdateImageRows(Image* image, uint32_t firstRow, uint32_t rowCount, const void* data) override;
        void   DestroyImageAtEnd(Image* img, DeviceScope scope, const std::source_location& loc = std::source_location::current()) override;

        TextAtlasManager* CreateTextAtlasManager() override;

        /// `nativeWindowHandle` is the window's NSWindow: the context adds a view with a
        /// CAMetalLayer to it. A render-to-image window's frames are read back.
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

        /// The scene's color texture (MTLPixelFormatRGBA8Unorm, sampled with
        /// Sampler::LinearClampToEdge) and depth (MTLPixelFormatDepth32Float), drawn by `record`
        /// in `window`'s next frame before its UI.
        ImageIndex RenderScene(ID key, ID window, uint2 size, color4f clear,
                               std::function<void(const ScenePayload& scene)> record) override;

        // ---- A host's own textures, on its device (SetExternalDevice) -----------------------

        /// A texture the host renders (an id<MTLTexture>, a 3D viewport) in the image set, for the
        /// Image control or a primitive to sample with `sampler`. The texture stays the host's
        /// and must outlive every GUI frame that samples it; Metal orders the host's work on the
        /// same queue before the GUI's frame. Call it while the UI is built (OnGUI).
        ImageIndex RegisterExternalImage(void* texture, Sampler sampler);
        /// Points an existing index at another texture, for example after a resize. No GUI frame
        /// in flight may still sample the old one (WaitIdle).
        void       UpdateExternalImage(ImageIndex index, void* texture, Sampler sampler);

        /// Waits until the GPU has done all work committed so far.
        void WaitIdle();

        struct Impl;
        /// Library internal: the device, its queue, and what the window contexts draw with.
        Impl& Internal() { return *m_impl; }

    private:
        std::unique_ptr<Impl> m_impl;
    };
    /// @}
}
