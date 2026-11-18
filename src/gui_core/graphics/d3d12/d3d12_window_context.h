#pragma once

// Inside the Direct3D 12 backend: an OS window's rendering. Each frame draws into a texture of
// the frame slot's and copies it to the swapchain's buffer, as the Vulkan backend does, so the
// last frame can be read back after it's presented. A layered window (a transparent one on
// Windows) has no swapchain: its frame is read back and shown with UpdateLayeredWindow. A
// render-to-image window has no OS window at all: its frames are only read back.

#include <gui_core/graphics/d3d12/d3d12_device.h>
#include <gui_core/graphics/d3d12/d3d12_rhi.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/windows/gui_window_backend.h>

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace tachyon::d3d12
{
    class D3D12WindowContext final : public IWindowContext
    {
    public:
        /// `window` is null for a render-to-image window. Null, with the reason logged, when the
        /// swapchain can't be made.
        static D3D12WindowContext* Create(D3D12GraphicsContext& graphics, HWND window, OSWindowHandle osHandle, ID id,
                                          int2 size, WindowFlags flags, OnGuiFunc onGui);
        ~D3D12WindowContext() override;

        ID             GetID() const override { return m_id; }
        OSWindowHandle GetWindowHandle() const override { return m_osHandle; }
        bool           IsTransparent() const override;

        /// `resizing`: a live resize's redraw, which lays the window out again (its onGui)
        /// before it's drawn at the new size.
        void RenderWindowFrame(GraphicsContext* graphicsContext, IntRect rect, bool resizing) override;
        void RecordWindowFrame(GraphicsContext* graphicsContext, IntRect rect) override;
        std::optional<double> PresentWindowFrame() override;
        bool ReadPixels(std::span<uint8_t> bgra) override;

    private:
        static constexpr uint32_t kFrames = 2;   ///< frames in flight, and swapchain buffers

        struct Frame
        {
            ComPtr<ID3D12CommandAllocator> allocator;
            ComPtr<ID3D12Resource>         drawTexture;   ///< what the frame draws into
            D3D12_RESOURCE_STATES          drawState = D3D12_RESOURCE_STATE_RENDER_TARGET;
            /// The image set's views, copied in each time: what the frame's shaders index.
            ComPtr<ID3D12DescriptorHeap>   images;
            FrameArena                     arena;          ///< the frame's primitives and sampler table
            UINT64                         fenceValue = 0; ///< done once the submit fence reaches it
        };

        D3D12WindowContext(D3D12GraphicsContext& graphics, HWND window, OSWindowHandle osHandle, ID id, WindowFlags flags, OnGuiFunc onGui);
        void Record(IntRect rect, bool resizing);
        bool MakeSizedResources(uint32_t width, uint32_t height);
        void ReleaseSizedResources();
        void Draw(Frame& frame, D3D12_CPU_DESCRIPTOR_HANDLE rtv);
        /// A layered or render-to-image window's frame starts transparent; a swapchain's opaque black.
        float ClearAlpha() const { return m_layered || m_toImage ? 0.0f : 1.0f; }

        D3D12GraphicsContext&       m_graphics;
        D3D12GraphicsContext::Impl& m_device;
        HWND                        m_window;
        OSWindowHandle              m_osHandle;
        ID                          m_id;
        WindowFlags                 m_flags;
        OnGuiFunc                   m_onGui;

        /// Render-to-image (WindowFlags::RenderToImage): no OS window; its frames are read back
        /// (ReadPixels).
        bool                              m_toImage = false;
        /// Layered: no swapchain; each frame is read back into `m_layeredPixels` and shown.
        bool                              m_layered = false;
        std::vector<uint8_t>              m_layeredPixels;
        uint2                             m_layeredSize {};
        ComPtr<IDXGISwapChain3>           m_swapchain;
        std::array<ComPtr<ID3D12Resource>, kFrames> m_buffers;
        ComPtr<ID3D12DescriptorHeap>      m_rtvHeap;   ///< one view per frame's draw texture
        UINT                              m_rtvSize = 0;
        /// One depth buffer: the queue runs the frames one after another.
        ComPtr<ID3D12Resource>            m_depth;
        ComPtr<ID3D12DescriptorHeap>      m_dsvHeap;
        ComPtr<ID3D12GraphicsCommandList> m_list;
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
