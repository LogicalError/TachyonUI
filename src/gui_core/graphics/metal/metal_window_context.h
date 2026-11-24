#pragma once

// Inside the Metal backend (Objective-C++ only): an OS window's rendering. Each frame draws into
// a texture of the frame slot's and is copied into the CAMetalLayer's drawable when it's
// presented, as the other backends do, so the last frame can be read back after it's shown. A
// render-to-image window has no OS window: its frames are only read back.

#include <gui_core/graphics/metal/metal_device.h>
#include <gui_core/graphics/metal/metal_rhi.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/windows/gui_window_backend.h>

#include <array>
#include <memory>
#include <optional>
#include <span>

@class CAMetalLayer;
@class NSView;

namespace tachyon::metal
{
    class MetalWindowContext final : public IWindowContext
    {
    public:
        /// `window` is the NSWindow, null for a render-to-image window. Null, with the reason
        /// logged, when its layer can't be made.
        static MetalWindowContext* Create(MetalGraphicsContext& graphics, void* window, OSWindowHandle osHandle, ID id,
                                          int2 size, WindowFlags flags, OnGuiFunc onGui);
        ~MetalWindowContext() override;

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
        static constexpr uint32_t kFrames = 2;   ///< frames in flight

        struct Frame
        {
            id<MTLTexture> drawTexture = nil;   ///< what the frame draws into
            FrameArena     arena;               ///< the frame's primitives and image tables
            uint64_t       fenceValue = 0;      ///< done once the commit counted it is
        };

        MetalWindowContext(MetalGraphicsContext& graphics, OSWindowHandle osHandle, ID id, WindowFlags flags, OnGuiFunc onGui);
        void Record(IntRect rect, bool resizing);
        bool MakeSizedResources(uint32_t width, uint32_t height);
        void Draw(Frame& frame, id<MTLCommandBuffer> commandBuffer);
        /// A transparent or render-to-image window's frame starts transparent; others opaque black.
        double ClearAlpha() const { return IsTransparent() || m_toImage ? 0.0 : 1.0; }

        MetalGraphicsContext&       m_graphics;
        MetalGraphicsContext::Impl& m_device;
        OSWindowHandle              m_osHandle;
        ID                          m_id;
        WindowFlags                 m_flags;
        OnGuiFunc                   m_onGui;
        bool                        m_toImage = false;

        NSView*                     m_view  = nil;   ///< the view the layer draws in, over the window's content
        CAMetalLayer*               m_layer = nil;
        id<MTLTexture>              m_depth = nil;   ///< one: the queue runs the frames one after another
        std::array<Frame, kFrames>  m_frames;
        uint32_t                    m_frameIndex = 0;
        uint32_t                    m_width = 0, m_height = 0;
        bool                        m_presentPending = false;
        std::optional<uint32_t>     m_lastFrame;   ///< the slot of the last frame recorded

        id<MTLBuffer>               m_readback = nil;   ///< ReadPixels' copy
        /// False once the context is deleted: a live resize's onGui runs a whole app frame, which
        /// may close this very window (a drag preview whose drag ended).
        std::shared_ptr<bool>       m_alive = std::make_shared<bool>(true);
    };
}
