#include <gui_core/graphics/sdlgpu/sdlgpu_window_context.h>

#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/windows/gui_window_flags.h>

#include <chrono>
#include <cstring>
#include <utility>

static constexpr tachyon::Diagnostics::LogCategory kLogSDLGPU = tachyon::Diagnostics::MakeLogCategory("sdlgpu");

namespace tachyon::sdlgpu
{
    SDLGPUWindowContext::SDLGPUWindowContext(SDLGPUGraphicsContext::Impl& device, SDL_Window* window, OSWindowHandle osHandle, ID id,
                                             WindowFlags flags)
        : m_device(device), m_window(window), m_osHandle(osHandle), m_id(id), m_flags(flags)
    {
    }

    SDLGPUWindowContext* SDLGPUWindowContext::Create(SDLGPUGraphicsContext::Impl& device, SDL_Window* window, OSWindowHandle osHandle,
                                                     ID id, int2 size, WindowFlags flags)
    {
        auto* context = new SDLGPUWindowContext(device, window, osHandle, id, flags);
        if (!SDL_ClaimWindowForGPUDevice(device.device, window))
        {
            TACHYON_LOG_ERROR(kLogSDLGPU, "window: SDL_ClaimWindowForGPUDevice failed ({})", SDL_GetError());
            delete context;
            return nullptr;
        }
        context->m_claimed = true;
        context->m_format  = SDL_GetGPUSwapchainTextureFormat(device.device, window);
        if (!context->EnsureDrawTexture(uint32_t(std::max(size.x, 1)), uint32_t(std::max(size.y, 1))))
        {
            delete context;
            return nullptr;
        }
        return context;
    }

    SDLGPUWindowContext::~SDLGPUWindowContext()
    {
        SDL_GPUDevice* device = m_device.device;
        if (m_lastFence != nullptr)
        {
            SDL_WaitForGPUFences(device, true, &m_lastFence, 1);
            SDL_ReleaseGPUFence(device, m_lastFence);
        }
        // SDL frees these once the GPU is done with them.
        if (m_drawTexture != nullptr)
            SDL_ReleaseGPUTexture(device, m_drawTexture);
        if (m_download != nullptr)
            SDL_ReleaseGPUTransferBuffer(device, m_download);
        if (m_claimed)
            SDL_ReleaseWindowFromGPUDevice(device, m_window);
    }

    bool SDLGPUWindowContext::IsTransparent() const
    {
        return (m_flags & WindowFlags::Transparent) != WindowFlags::None;
    }

    bool SDLGPUWindowContext::EnsureDrawTexture(uint32_t width, uint32_t height)
    {
        if (m_drawTexture != nullptr && width == m_width && height == m_height)
            return true;
        if (m_drawTexture != nullptr)
            SDL_ReleaseGPUTexture(m_device.device, m_drawTexture);   // freed once the frames using it are done
        SDL_GPUTextureCreateInfo info{};
        info.type                 = SDL_GPU_TEXTURETYPE_2D;
        info.format               = m_format;
        info.usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width                = width;
        info.height               = height;
        info.layer_count_or_depth = 1;
        info.num_levels           = 1;
        info.sample_count         = SDL_GPU_SAMPLECOUNT_1;
        m_drawTexture = SDL_CreateGPUTexture(m_device.device, &info);
        if (m_drawTexture == nullptr)
        {
            TACHYON_LOG_ERROR(kLogSDLGPU, "window: draw texture of {}x{} failed ({})", width, height, SDL_GetError());
            m_width = m_height = 0;
            return false;
        }
        m_width  = width;
        m_height = height;
        m_drawn  = false;
        return true;
    }

    void SDLGPUWindowContext::RenderWindowFrame(GraphicsContext* graphicsContext, IntRect rect, bool)
    {
        RecordWindowFrame(graphicsContext, rect);
        PresentWindowFrame();
    }

    void SDLGPUWindowContext::RecordWindowFrame(GraphicsContext*, IntRect rect)
    {
        if (rect.w <= 0 || rect.h <= 0)
            return;   // minimized: nothing to show
        if (!EnsureDrawTexture(uint32_t(rect.w), uint32_t(rect.h)))
            return;
        SDL_GPUDevice* device = m_device.device;
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr)
        {
            TACHYON_LOG_ERROR(kLogSDLGPU, "window: SDL_AcquireGPUCommandBuffer failed ({})", SDL_GetError());
            return;
        }

        // The frame's drawing: so far the background alone. Cycled, so a frame in flight keeps
        // the texture it drew into.
        SDL_GPUColorTargetInfo target{};
        target.texture     = m_drawTexture;
        target.clear_color = SDL_FColor{ 0.0f, 0.0f, 0.0f, 1.0f };
        target.load_op     = SDL_GPU_LOADOP_CLEAR;
        target.store_op    = SDL_GPU_STOREOP_STORE;
        target.cycle       = true;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
        SDL_EndGPURenderPass(pass);

        // Into the swapchain's image, which the submit presents. None while the window is hidden.
        const auto start = std::chrono::steady_clock::now();
        SDL_GPUTexture* swapchain = nullptr;
        Uint32 width = 0, height = 0;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, m_window, &swapchain, &width, &height))
            TACHYON_LOG_ERROR(kLogSDLGPU, "window: SDL_WaitAndAcquireGPUSwapchainTexture failed ({})", SDL_GetError());
        m_acquireSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (swapchain != nullptr)
        {
            SDL_GPUBlitInfo blit{};
            blit.source.texture      = m_drawTexture;
            blit.source.w            = m_width;
            blit.source.h            = m_height;
            blit.destination.texture = swapchain;
            blit.destination.w       = width;
            blit.destination.h       = height;
            blit.load_op             = SDL_GPU_LOADOP_DONT_CARE;
            blit.filter              = SDL_GPU_FILTER_NEAREST;
            SDL_BlitGPUTexture(cmd, &blit);
        }

        if (m_lastFence != nullptr)
            SDL_ReleaseGPUFence(device, m_lastFence);
        m_lastFence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        m_drawn     = m_lastFence != nullptr;
    }

    std::optional<double> SDLGPUWindowContext::PresentWindowFrame()
    {
        return std::exchange(m_acquireSeconds, std::nullopt);
    }

    bool SDLGPUWindowContext::ReadPixels(std::span<uint8_t> bgra)
    {
        const uint32_t bytes = m_width * m_height * 4;
        if (!m_drawn || bgra.size() < bytes)
            return false;
        SDL_GPUDevice* device = m_device.device;
        if (m_download == nullptr || m_downloadSize < bytes)
        {
            if (m_download != nullptr)
                SDL_ReleaseGPUTransferBuffer(device, m_download);
            const SDL_GPUTransferBufferCreateInfo info{ SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, bytes, 0 };
            m_download     = SDL_CreateGPUTransferBuffer(device, &info);
            m_downloadSize = m_download != nullptr ? bytes : 0;
            if (m_download == nullptr)
                return false;
        }

        // The last frame's texture (submitted already: SDL orders the copy after it).
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr)
            return false;
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureRegion region{};
        region.texture = m_drawTexture;
        region.w       = m_width;
        region.h       = m_height;
        region.d       = 1;
        SDL_GPUTextureTransferInfo to{};
        to.transfer_buffer = m_download;
        to.pixels_per_row  = m_width;
        to.rows_per_layer  = m_height;
        SDL_DownloadFromGPUTexture(copy, &region, &to);
        SDL_EndGPUCopyPass(copy);
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (fence == nullptr)
            return false;
        SDL_WaitForGPUFences(device, true, &fence, 1);
        SDL_ReleaseGPUFence(device, fence);

        const auto* pixels = static_cast<const uint8_t*>(SDL_MapGPUTransferBuffer(device, m_download, false));
        if (pixels == nullptr)
            return false;
        std::memcpy(bgra.data(), pixels, bytes);
        SDL_UnmapGPUTransferBuffer(device, m_download);
        // An RGBA swapchain: its red and blue swap places.
        if (m_format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM || m_format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB)
            for (uint32_t i = 0; i < bytes; i += 4)
                std::swap(bgra[i], bgra[i + 2]);
        return true;
    }
}
