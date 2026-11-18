#pragma once

// Inside the SDL GPU backend: an OS window's rendering. The window is claimed for the device,
// which makes its swapchain. Each frame draws into a texture of the window's own and blits it
// to the swapchain's, so the last frame can be read back after it's shown; submitting the frame
// presents it.

#include <gui_core/graphics/sdlgpu/sdlgpu_device.h>
#include <gui_core/windows/gui_window_backend.h>

#include <optional>
#include <span>

namespace tachyon::sdlgpu
{
    class SDLGPUWindowContext final : public IWindowContext
    {
    public:
        /// Null, with the reason logged, when the window can't be claimed.
        static SDLGPUWindowContext* Create(SDLGPUGraphicsContext::Impl& device, SDL_Window* window, OSWindowHandle osHandle,
                                           ID id, int2 size, WindowFlags flags);
        ~SDLGPUWindowContext() override;

        ID             GetID() const override { return m_id; }
        OSWindowHandle GetWindowHandle() const override { return m_osHandle; }
        bool           IsTransparent() const override;

        void RenderWindowFrame(GraphicsContext* graphicsContext, IntRect rect, bool resizing) override;
        void RecordWindowFrame(GraphicsContext* graphicsContext, IntRect rect) override;
        /// The frame was presented when it was submitted: the seconds the swapchain's image took
        /// to come (the vsync wait).
        std::optional<double> PresentWindowFrame() override;
        bool ReadPixels(std::span<uint8_t> bgra) override;

    private:
        SDLGPUWindowContext(SDLGPUGraphicsContext::Impl& device, SDL_Window* window, OSWindowHandle osHandle, ID id, WindowFlags flags);
        /// The draw texture at the size; false when it can't be made.
        bool EnsureDrawTexture(uint32_t width, uint32_t height);

        SDLGPUGraphicsContext::Impl& m_device;
        SDL_Window*                  m_window;
        OSWindowHandle               m_osHandle;
        ID                           m_id;
        WindowFlags                  m_flags;
        bool                         m_claimed = false;

        SDL_GPUTextureFormat  m_format = SDL_GPU_TEXTUREFORMAT_INVALID;   ///< the swapchain's
        SDL_GPUTexture*       m_drawTexture = nullptr;                    ///< what a frame draws into
        uint32_t              m_width = 0, m_height = 0;
        SDL_GPUFence*         m_lastFence = nullptr;                      ///< the last frame's submit
        bool                  m_drawn = false;                            ///< a frame was drawn at this size
        std::optional<double> m_acquireSeconds;                           ///< for PresentWindowFrame

        /// ReadPixels' buffer, kept for the next.
        SDL_GPUTransferBuffer* m_download = nullptr;
        uint32_t               m_downloadSize = 0;
    };
}
