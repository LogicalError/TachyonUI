#pragma once

// Inside the Direct3D 12 backend: an OS window's rendering. Each frame draws into a texture of
// the frame slot's and copies it to the swapchain's buffer, as the Vulkan backend does, so the
// last frame can be read back after it's presented.

#include <gui_core/graphics/d3d12/d3d12_device.h>
#include <gui_core/windows/gui_window_backend.h>

#include <array>
#include <optional>
#include <span>

namespace tachyon::d3d12
{
    class D3D12WindowContext final : public IWindowContext
    {
    public:
        /// Null, with the reason logged, when the swapchain can't be made.
        static D3D12WindowContext* Create(D3D12GraphicsContext::Impl& device, HWND window, OSWindowHandle osHandle, ID id,
                                          int2 size, WindowFlags flags);
        ~D3D12WindowContext() override;

        ID             GetID() const override { return m_id; }
        OSWindowHandle GetWindowHandle() const override { return m_osHandle; }
        bool           IsTransparent() const override;

        void RenderWindowFrame(GraphicsContext* graphicsContext, IntRect rect, bool resizing) override;
        void RecordWindowFrame(GraphicsContext* graphicsContext, IntRect rect) override;
        std::optional<double> PresentWindowFrame() override;
        bool ReadPixels(std::span<uint8_t> bgra) override;

    private:
        static constexpr uint32_t    kFrames      = 2;   ///< frames in flight, and swapchain buffers
        static constexpr DXGI_FORMAT kDrawFormat  = DXGI_FORMAT_B8G8R8A8_UNORM;

        struct Frame
        {
            ComPtr<ID3D12CommandAllocator> allocator;
            ComPtr<ID3D12Resource>         drawTexture;   ///< what the frame draws into
            D3D12_RESOURCE_STATES          drawState = D3D12_RESOURCE_STATE_RENDER_TARGET;
            UINT64                         fenceValue = 0;   ///< done once the fence reaches it
        };

        D3D12WindowContext(D3D12GraphicsContext::Impl& device, OSWindowHandle osHandle, ID id, WindowFlags flags);
        bool MakeSizedResources(uint32_t width, uint32_t height);
        void ReleaseSizedResources();

        D3D12GraphicsContext::Impl& m_device;
        OSWindowHandle              m_osHandle;
        ID                          m_id;
        WindowFlags                 m_flags;

        ComPtr<IDXGISwapChain3>           m_swapchain;
        std::array<ComPtr<ID3D12Resource>, kFrames> m_buffers;
        ComPtr<ID3D12DescriptorHeap>      m_rtvHeap;   ///< one view per frame's draw texture
        UINT                              m_rtvSize = 0;
        ComPtr<ID3D12GraphicsCommandList> m_list;
        ComPtr<ID3D12Fence>               m_fence;
        UINT64                            m_fenceValue = 0;
        std::array<Frame, kFrames>        m_frames;
        uint32_t                          m_frameIndex = 0;
        uint32_t                          m_width = 0, m_height = 0;
        bool                              m_presentPending = false;
        std::optional<uint32_t>           m_lastFrame;   ///< the slot of the last frame recorded

        /// ReadPixels' own recording and the buffer it copies into.
        ComPtr<ID3D12CommandAllocator>    m_readAllocator;
        ComPtr<ID3D12GraphicsCommandList> m_readList;
        ComPtr<ID3D12Resource>            m_readback;
        UINT64                            m_readbackSize = 0;
    };
}
