#pragma once

// Inside the SDL GPU backend: an OS window's rendering. The window is claimed for the device,
// which makes its swapchain. A frame draws into a texture of the window's own and blits it to
// the swapchain's, so the last frame can be read back after it's shown; submitting the frame
// presents it. Its copies (its primitives, and what the device has queued) come first, as SDL
// wants them outside the render pass.
// A layered window (a transparent one, on Windows) isn't claimed, as SDL GPU makes no swapchain
// for a transparent window: its frame is cleared see-through, read back, and shown with
// UpdateLayeredWindow. A render-to-image window has no OS window at all: its frames are only
// read back. The window's scenes (RenderScene) are recorded after the copies, before its UI.

#include <gui_core/gui_base_types.h>
#include <gui_core/graphics/sdlgpu/sdlgpu_device.h>
#include <gui_core/graphics/sdlgpu/sdlgpu_rhi.h>
#include <gui_core/windows/gui_window_backend.h>

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace tachyon::sdlgpu
{
    class SDLGPUWindowContext final : public IWindowContext
    {
    public:
        /// Null, with the reason logged, when the window can't be claimed. `nativeWindow` is the
        /// OS's window (an HWND on Windows), which says whether it's layered. `window` and
        /// `nativeWindow` are null for a render-to-image window.
        static SDLGPUWindowContext* Create(SDLGPUGraphicsContext& graphics, SDL_Window* window, void* nativeWindow,
                                           OSWindowHandle osHandle, ID id, int2 size, WindowFlags flags, OnGuiFunc onGui);
        ~SDLGPUWindowContext() override;

        ID             GetID() const override { return m_id; }
        OSWindowHandle GetWindowHandle() const override { return m_osHandle; }
        bool           IsTransparent() const override;

        /// `resizing`: a live resize's redraw, which lays the window out again (its onGui)
        /// before it's drawn at the new size.
        void RenderWindowFrame(GraphicsContext* graphicsContext, IntRect rect, bool resizing) override;
        void RecordWindowFrame(GraphicsContext* graphicsContext, IntRect rect) override;
        /// The frame was presented when it was submitted: the seconds the swapchain's image took
        /// to come (the vsync wait). A layered window's frame is read back and shown here instead.
        std::optional<double> PresentWindowFrame() override;
        bool ReadPixels(std::span<uint8_t> bgra) override;

    private:
        SDLGPUWindowContext(SDLGPUGraphicsContext& graphics, SDL_Window* window, void* nativeWindow, OSWindowHandle osHandle, ID id,
                            WindowFlags flags, OnGuiFunc onGui);
        void Record(IntRect rect, bool resizing);
        /// The draw and depth textures at the size; false when they can't be made.
        bool EnsureTargets(uint32_t width, uint32_t height);
        /// The frame's primitives into its buffer's staging, the buffer as big as they need.
        bool StageArena();

        SDLGPUGraphicsContext&       m_graphics;
        SDLGPUGraphicsContext::Impl& m_device;
        SDL_Window*                  m_window;
        OSWindowHandle               m_osHandle;
        ID                           m_id;
        WindowFlags                  m_flags;
        OnGuiFunc                    m_onGui;
        /// False once the context is deleted: a live resize's onGui runs a whole app frame,
        /// which may close this very window (a drag preview whose drag ended).
        std::shared_ptr<bool>        m_alive = std::make_shared<bool>(true);
        void*                        m_nativeWindow;
        bool                         m_toImage;   ///< WindowFlags::RenderToImage: no OS window, frames read back
        bool                         m_layered;
        bool                         m_claimed = false;

        /// Layered: the last frame read back, shown again on each present (a layered window
        /// isn't redrawn on the OS's expose events), and whether a new frame waits to be read.
        std::vector<uint8_t> m_layeredPixels;
        uint2                m_layeredSize{ 0, 0 };
        bool                 m_layeredPending = false;

        SDL_GPUTexture*       m_drawTexture = nullptr;    ///< what a frame draws into (kDrawFormat)
        SDL_GPUTexture*       m_depthTexture = nullptr;
        uint32_t              m_width = 0, m_height = 0;
        /// The submitted frames' fences, oldest first. A fence acquired from a submit is the
        /// command buffer's only hold on it: released before it signals, SDL reuses it and takes
        /// work still in flight for done. So each is released once it has signaled.
        std::vector<SDL_GPUFence*> m_fences;
        static constexpr size_t    kFramesInFlight = 2;
        /// Releases the fences that have signaled.
        void ReleaseSignaledFences();
        bool                  m_drawn = false;            ///< a frame was drawn at this size
        std::optional<double> m_acquireSeconds;           ///< for PresentWindowFrame

        /// The frame's primitives: staged here while the renderer runs, then copied into the
        /// arena (cycled, so the frames in flight keep theirs), which the buffer table's entry names.
        std::vector<std::byte>  m_staging;
        SDL_GPUBuffer*          m_arena = nullptr;
        SDL_GPUTransferBuffer*  m_arenaUpload = nullptr;
        uint32_t                m_arenaSize = 0;
        uint32_t                m_arenaEntry = 0;
        /// Each frame's draws, recorded while the renderer runs and replayed after the copies.
        SDLGPUCommandList       m_commands{ m_device, m_staging };

        /// ReadPixels' buffer, kept for the next.
        SDL_GPUTransferBuffer* m_download = nullptr;
        uint32_t               m_downloadSize = 0;
    };
}
