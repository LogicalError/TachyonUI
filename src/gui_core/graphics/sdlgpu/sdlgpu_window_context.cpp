#include <gui_core/graphics/sdlgpu/sdlgpu_window_context.h>

#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/graphics/gui_primitive_renderer.h>
#include <gui_core/graphics/sdlgpu/sdlgpu_rhi.h>
#include <gui_core/windows/gui_layered_window.h>
#include <gui_core/windows/gui_window_flags.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <utility>

static constexpr tachyon::Diagnostics::LogCategory kLogSDLGPU = tachyon::Diagnostics::MakeLogCategory("sdlgpu");

namespace tachyon::sdlgpu
{
    namespace
    {
        SDL_GPUTexture* Texture(SDL_GPUDevice* device, SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, uint32_t width, uint32_t height)
        {
            SDL_GPUTextureCreateInfo info{};
            info.type                 = SDL_GPU_TEXTURETYPE_2D;
            info.format               = format;
            info.usage                = usage;
            info.width                = width;
            info.height               = height;
            info.layer_count_or_depth = 1;
            info.num_levels           = 1;
            info.sample_count         = SDL_GPU_SAMPLECOUNT_1;
            return SDL_CreateGPUTexture(device, &info);
        }
    }

    SDLGPUWindowContext::SDLGPUWindowContext(SDLGPUGraphicsContext& graphics, SDL_Window* window, void* nativeWindow,
                                             OSWindowHandle osHandle, ID id, WindowFlags flags, OnGuiFunc onGui)
        : m_graphics(graphics), m_device(graphics.Internal()), m_window(window), m_osHandle(osHandle), m_id(id), m_flags(flags),
          m_onGui(std::move(onGui)), m_nativeWindow(nativeWindow), m_layered(IsLayeredWindow(nativeWindow))
    {
    }

    SDLGPUWindowContext* SDLGPUWindowContext::Create(SDLGPUGraphicsContext& graphics, SDL_Window* window, void* nativeWindow,
                                                     OSWindowHandle osHandle, ID id, int2 size, WindowFlags flags, OnGuiFunc onGui)
    {
        auto* context = new SDLGPUWindowContext(graphics, window, nativeWindow, osHandle, id, flags, std::move(onGui));
        SDLGPUGraphicsContext::Impl& d = context->m_device;
        if (!context->m_layered)
        {
            if (!SDL_ClaimWindowForGPUDevice(d.device, window))
            {
                TACHYON_LOG_ERROR(kLogSDLGPU, "window: SDL_ClaimWindowForGPUDevice failed ({})", SDL_GetError());
                delete context;
                return nullptr;
            }
            context->m_claimed = true;
        }
        context->m_arenaEntry = d.AddBuffer(nullptr);
        if (!context->EnsureTargets(uint32_t(std::max(size.x, 1)), uint32_t(std::max(size.y, 1))))
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
        for (SDL_GPUTexture* texture : { m_drawTexture, m_depthTexture })
            if (texture != nullptr)
                SDL_ReleaseGPUTexture(device, texture);
        for (SDL_GPUTransferBuffer* buffer : { m_arenaUpload, m_download })
            if (buffer != nullptr)
                SDL_ReleaseGPUTransferBuffer(device, buffer);
        if (m_arena != nullptr)
            SDL_ReleaseGPUBuffer(device, m_arena);
        m_device.RemoveBuffer(m_arenaEntry);
        if (m_claimed)
            SDL_ReleaseWindowFromGPUDevice(device, m_window);
    }

    bool SDLGPUWindowContext::IsTransparent() const
    {
        return (m_flags & WindowFlags::Transparent) != WindowFlags::None;
    }

    bool SDLGPUWindowContext::EnsureTargets(uint32_t width, uint32_t height)
    {
        if (m_drawTexture != nullptr && width == m_width && height == m_height)
            return true;
        SDL_GPUDevice* device = m_device.device;
        for (SDL_GPUTexture*& texture : { std::ref(m_drawTexture), std::ref(m_depthTexture) })
            if (texture != nullptr)
            {
                SDL_ReleaseGPUTexture(device, texture);   // freed once the frames using it are done
                texture = nullptr;
            }
        m_drawTexture  = Texture(device, kDrawFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, width, height);
        m_depthTexture = Texture(device, kDepthFormat, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, width, height);
        if (m_drawTexture == nullptr || m_depthTexture == nullptr)
        {
            TACHYON_LOG_ERROR(kLogSDLGPU, "window: targets of {}x{} failed ({})", width, height, SDL_GetError());
            m_width = m_height = 0;
            return false;
        }
        m_width  = width;
        m_height = height;
        m_drawn  = false;
        return true;
    }

    bool SDLGPUWindowContext::StageArena()
    {
        SDL_GPUDevice* device = m_device.device;
        const uint32_t bytes = uint32_t(m_staging.size());
        if (bytes > m_arenaSize)
        {
            const uint32_t size = std::bit_ceil(std::max(bytes, 64u * 1024u));
            if (m_arena != nullptr)
                SDL_ReleaseGPUBuffer(device, m_arena);
            if (m_arenaUpload != nullptr)
                SDL_ReleaseGPUTransferBuffer(device, m_arenaUpload);
            const SDL_GPUBufferCreateInfo arena{ SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, size, 0 };
            const SDL_GPUTransferBufferCreateInfo upload{ SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size, 0 };
            m_arena       = SDL_CreateGPUBuffer(device, &arena);
            m_arenaUpload = SDL_CreateGPUTransferBuffer(device, &upload);
            m_arenaSize   = m_arena != nullptr && m_arenaUpload != nullptr ? size : 0;
            m_device.buffers[m_arenaEntry] = m_arena;
            if (m_arenaSize == 0)
            {
                TACHYON_LOG_ERROR(kLogSDLGPU, "window: a buffer of {} bytes failed ({})", size, SDL_GetError());
                return false;
            }
        }
        // Cycled: a frame in flight keeps the one it reads.
        void* mapped = SDL_MapGPUTransferBuffer(device, m_arenaUpload, true);
        if (mapped == nullptr)
            return false;
        std::memcpy(mapped, m_staging.data(), bytes);
        SDL_UnmapGPUTransferBuffer(device, m_arenaUpload);
        return true;
    }

    void SDLGPUWindowContext::RenderWindowFrame(GraphicsContext*, IntRect rect, bool resizing)
    {
        Record(rect, resizing);
        PresentWindowFrame();
    }

    void SDLGPUWindowContext::RecordWindowFrame(GraphicsContext*, IntRect rect)
    {
        Record(rect, false);
    }

    void SDLGPUWindowContext::Record(IntRect rect, bool resizing)
    {
        if (rect.w <= 0 || rect.h <= 0)
            return;   // minimized: nothing to show

        // A live resize lays the window out again at its new size, and its styles may change.
        auto* renderer = static_cast<PrimitiveRenderer*>(m_graphics.guiRenderer);
        if (resizing && m_onGui && renderer != nullptr)
        {
            const bool hasTarget = renderer->StartResizing(m_id);
            renderer->Reset();
            m_onGui();
            m_graphics.RePrepareFrame();
            if (hasTarget)
                renderer->StopResizing();
        }
        if (!EnsureTargets(uint32_t(rect.w), uint32_t(rect.h)))
            return;
        SDL_GPUDevice* device = m_device.device;

        // The frame's draws, kept, and its primitives, staged: SDL copies outside render passes.
        m_staging.clear();
        SDLGPUCommandList commands(m_device, m_staging, m_arenaEntry);
        if (renderer != nullptr)
        {
            // Pixels to clip space, y down as SDL GPU's clip space is y up. A primitive's depth is
            // its draw order a step apart, the first a step above the cleared depth. Column-major,
            // as the shaders read it.
            GPUDrawPushConstants push{};
            const float m[16] = {
                2.0f / float(m_width), 0.0f, 0.0f, 0.0f,
                0.0f, -2.0f / float(m_height), 0.0f, 0.0f,
                0.0f, 0.0f, kPrimitiveDepthStep, 0.0f,
                -1.0f, 1.0f, kPrimitiveDepthStep, 1.0f,
            };
            static_assert(sizeof(m) == sizeof(push.worldMatrix));
            std::memcpy(&push.worldMatrix, m, sizeof(m));
            push.elapsedTime = m_device.elapsedTime;
            renderer->Render(m_id, commands, m_device.pipelines, push);
        }
        if (!m_device.EnsureQuadIndices(commands.MostQuads()) || (!m_staging.empty() && !StageArena()))
            return;

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr)
        {
            TACHYON_LOG_ERROR(kLogSDLGPU, "window: SDL_AcquireGPUCommandBuffer failed ({})", SDL_GetError());
            return;
        }

        // The copies first: what the device queued (images, styles, cached scopes), then the
        // frame's primitives.
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
        m_device.RecordCopies(copy);
        if (!m_staging.empty())
        {
            const SDL_GPUTransferBufferLocation from{ m_arenaUpload, 0 };
            const SDL_GPUBufferRegion to{ m_arena, 0, uint32_t(m_staging.size()) };
            SDL_UploadToGPUBuffer(copy, &from, &to, true);
        }
        SDL_EndGPUCopyPass(copy);

        // The frame's drawing, on opaque black, or see-through for a layered window. Cycled, so a
        // frame in flight keeps the textures it drew into.
        SDL_GPUColorTargetInfo color{};
        color.texture     = m_drawTexture;
        color.clear_color = SDL_FColor{ 0.0f, 0.0f, 0.0f, m_layered ? 0.0f : 1.0f };
        color.load_op     = SDL_GPU_LOADOP_CLEAR;
        color.store_op    = SDL_GPU_STOREOP_STORE;
        color.cycle       = true;
        // Depth is cleared to 0: later primitives are nearer.
        SDL_GPUDepthStencilTargetInfo depth{};
        depth.texture          = m_depthTexture;
        depth.clear_depth      = 0.0f;
        depth.load_op          = SDL_GPU_LOADOP_CLEAR;
        depth.store_op         = SDL_GPU_STOREOP_DONT_CARE;
        depth.stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE;
        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        depth.cycle            = true;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color, 1, &depth);
        commands.Replay(cmd, pass);
        SDL_EndGPURenderPass(pass);

        // Into the swapchain's image, which the submit presents. None while the window is hidden,
        // nor for a layered window, which PresentWindowFrame reads back.
        const auto start = std::chrono::steady_clock::now();
        SDL_GPUTexture* swapchain = nullptr;
        Uint32 width = 0, height = 0;
        if (m_claimed)
        {
            if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, m_window, &swapchain, &width, &height))
                TACHYON_LOG_ERROR(kLogSDLGPU, "window: SDL_WaitAndAcquireGPUSwapchainTexture failed ({})", SDL_GetError());
            m_acquireSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        }
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
        m_drawn          = m_lastFence != nullptr;
        m_layeredPending = m_layered && m_drawn;
    }

    std::optional<double> SDLGPUWindowContext::PresentWindowFrame()
    {
        if (m_layered)
        {
            if (std::exchange(m_layeredPending, false))
            {
                m_layeredSize = { m_width, m_height };
                m_layeredPixels.resize(size_t(m_width) * m_height * 4);
                if (!ReadPixels(m_layeredPixels))
                    m_layeredPixels.clear();
            }
            if (!m_layeredPixels.empty())
                ShowLayeredPixels(m_nativeWindow, m_layeredPixels, m_layeredSize);
            return std::nullopt;
        }
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

        // The draw texture is BGRA8 (kDrawFormat), whatever the swapchain's format.
        const auto* pixels = static_cast<const uint8_t*>(SDL_MapGPUTransferBuffer(device, m_download, false));
        if (pixels == nullptr)
            return false;
        std::memcpy(bgra.data(), pixels, bytes);
        SDL_UnmapGPUTransferBuffer(device, m_download);
        return true;
    }
}
