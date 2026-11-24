#include <gui_core/graphics/metal/metal_window_context.h>

#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/graphics/gui_primitive_renderer.h>
#include <gui_core/windows/gui_window_flags.h>

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>
#include <vector>

static constexpr tachyon::Diagnostics::LogCategory kLogMetal = tachyon::Diagnostics::MakeLogCategory("metal");

// A view over the window's content whose layer is the CAMetalLayer the frames are shown in. It
// takes no input: clicks and keys go to the window's own view, which the window backend reads.
@interface TachyonMetalView : NSView
@end

@implementation TachyonMetalView
- (CALayer*)makeBackingLayer
{
    return [CAMetalLayer layer];
}
- (BOOL)wantsUpdateLayer
{
    return YES;
}
- (NSView*)hitTest:(NSPoint)point
{
    (void)point;
    return nil;
}
- (BOOL)acceptsFirstResponder
{
    return NO;
}
@end

namespace tachyon::metal
{
    MetalWindowContext::MetalWindowContext(MetalGraphicsContext& graphics, OSWindowHandle osHandle, ID id, WindowFlags flags,
                                           OnGuiFunc onGui)
        : m_graphics(graphics), m_device(graphics.Internal()), m_osHandle(osHandle), m_id(id), m_flags(flags),
          m_onGui(std::move(onGui)), m_toImage((flags & WindowFlags::RenderToImage) != WindowFlags::None)
    {
    }

    MetalWindowContext* MetalWindowContext::Create(MetalGraphicsContext& graphics, void* window, OSWindowHandle osHandle, ID id,
                                                   int2 size, WindowFlags flags, OnGuiFunc onGui)
    {
        auto* context = new MetalWindowContext(graphics, osHandle, id, flags, std::move(onGui));
        if (!context->m_toImage)
        {
            NSWindow* nsWindow = (__bridge NSWindow*)window;
            NSView*   content  = nsWindow.contentView;
            if (content == nil)
            {
                TACHYON_LOG_ERROR(kLogMetal, "window: it has no content view to draw in");
                delete context;
                return nullptr;
            }
            TachyonMetalView* view = [[TachyonMetalView alloc] initWithFrame:content.bounds];
            view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
            view.wantsLayer       = YES;
            [content addSubview:view];
            auto* layer = static_cast<CAMetalLayer*>(view.layer);
            layer.device             = context->m_device.device;
            layer.pixelFormat        = kDrawFormat;
            // The frame is copied in, which a framebuffer-only drawable can't take.
            layer.framebufferOnly    = NO;
            layer.opaque             = !context->IsTransparent();
            layer.contentsScale      = nsWindow.backingScaleFactor;
            layer.displaySyncEnabled = YES;   // presents wait for the vsync
            layer.drawableSize       = CGSizeMake(std::max(size.x, 1), std::max(size.y, 1));
            if (context->IsTransparent())
                layer.backgroundColor = CGColorGetConstantColor(kCGColorClear);
            context->m_view  = view;
            context->m_layer = layer;
        }
        if (!context->MakeSizedResources(uint32_t(std::max(size.x, 1)), uint32_t(std::max(size.y, 1))))
        {
            delete context;
            return nullptr;
        }
        return context;
    }

    MetalWindowContext::~MetalWindowContext()
    {
        *m_alive = false;
        // Every frame of the window, presents included.
        m_device.WaitIdle();
        for (Frame& frame : m_frames)
        {
            frame.arena.Reset(m_device);
            m_device.ReleaseBuffer(frame.arena.buffer);
        }
        if (m_view != nil)
            [m_view removeFromSuperview];
        m_view  = nil;
        m_layer = nil;
    }

    bool MetalWindowContext::IsTransparent() const
    {
        return (m_flags & WindowFlags::Transparent) != WindowFlags::None;
    }

    bool MetalWindowContext::MakeSizedResources(uint32_t width, uint32_t height)
    {
        // The frames in flight keep the old textures: their command buffers hold them.
        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:kDrawFormat
                                                                                        width:width
                                                                                       height:height
                                                                                    mipmapped:NO];
        desc.usage       = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        desc.storageMode = MTLStorageModePrivate;
        for (Frame& frame : m_frames)
        {
            frame.drawTexture = [m_device.device newTextureWithDescriptor:desc];
            if (frame.drawTexture == nil)
            {
                TACHYON_LOG_ERROR(kLogMetal, "window: a draw texture of {}x{} failed", width, height);
                return false;
            }
            frame.drawTexture.label = @"Window frame";
        }
        MTLTextureDescriptor* depthDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:kDepthFormat
                                                                                             width:width
                                                                                            height:height
                                                                                         mipmapped:NO];
        depthDesc.usage       = MTLTextureUsageRenderTarget;
        depthDesc.storageMode = MTLStorageModePrivate;
        m_depth = [m_device.device newTextureWithDescriptor:depthDesc];
        if (m_depth == nil)
        {
            TACHYON_LOG_ERROR(kLogMetal, "window: a depth buffer of {}x{} failed", width, height);
            return false;
        }
        if (m_layer != nil)
            m_layer.drawableSize = CGSizeMake(width, height);
        m_width  = width;
        m_height = height;
        m_lastFrame.reset();
        return true;
    }

    void MetalWindowContext::RenderWindowFrame(GraphicsContext*, IntRect rect, bool resizing)
    {
        Record(rect, resizing);
        PresentWindowFrame();
    }

    void MetalWindowContext::RecordWindowFrame(GraphicsContext*, IntRect rect)
    {
        Record(rect, false);
    }

    void MetalWindowContext::Record(IntRect rect, bool resizing)
    {
        @autoreleasepool
        {
            // A frame recorded and not presented yet (a live resize's redraw in between) goes first.
            if (m_presentPending)
                PresentWindowFrame();
            if (rect.w <= 0 || rect.h <= 0)
                return;   // minimized: nothing to show

            // A live resize lays the window out again at its new size, and its styles may change.
            if (resizing && m_onGui && m_graphics.guiRenderer != nullptr)
            {
                // onGui runs a whole app frame, which may close this window and delete this
                // context: only what outlives it is used after it, until `alive` says it's there.
                MetalGraphicsContext&       graphics = m_graphics;
                auto*                       renderer = static_cast<PrimitiveRenderer*>(graphics.guiRenderer);
                const std::shared_ptr<bool> alive    = m_alive;
                const OnGuiFunc             onGui    = m_onGui;   // the context's own copy goes with it
                const bool hasTarget = renderer->StartResizing(m_id);
                renderer->Reset();
                onGui();
                if (*alive)
                    graphics.RePrepareFrame();
                if (hasTarget)
                    renderer->StopResizing();
                if (!*alive)
                    return;
            }

            Frame& frame = m_frames[m_frameIndex];
            m_device.Wait(frame.fenceValue);
            frame.arena.Reset(m_device);

            if (uint32_t(rect.w) != m_width || uint32_t(rect.h) != m_height)
                if (!MakeSizedResources(uint32_t(rect.w), uint32_t(rect.h)))
                    return;
            // The window may have moved to a display of another scale.
            if (m_view != nil && m_view.window != nil && m_layer.contentsScale != m_view.window.backingScaleFactor)
                m_layer.contentsScale = m_view.window.backingScaleFactor;

            id<MTLCommandBuffer> commandBuffer = [m_device.queue commandBuffer];
            commandBuffer.label = @"Window frame";
            m_device.RecordPendingUploads(commandBuffer);
            // The window's scenes first: its UI samples them.
            m_device.RecordScenes(commandBuffer, m_id);
            Draw(frame, commandBuffer);

            frame.fenceValue = m_device.Commit(commandBuffer);
            m_lastFrame      = m_frameIndex;
            m_presentPending = true;
        }
    }

    void MetalWindowContext::Draw(Frame& frame, id<MTLCommandBuffer> commandBuffer)
    {
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture     = frame.drawTexture;
        pass.colorAttachments[0].loadAction  = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor  = MTLClearColorMake(0.0, 0.0, 0.0, ClearAlpha());
        // Depth is cleared to 0: later primitives are nearer.
        pass.depthAttachment.texture     = m_depth;
        pass.depthAttachment.loadAction  = MTLLoadActionClear;
        pass.depthAttachment.storeAction = MTLStoreActionDontCare;
        pass.depthAttachment.clearDepth  = 0.0;
        id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
        encoder.label = @"UI";
        if (m_graphics.guiRenderer == nullptr)
        {
            [encoder endEncoding];
            return;
        }
        [encoder setViewport:MTLViewport{ 0.0, 0.0, double(m_width), double(m_height), 0.0, 1.0 }];
        [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
        [encoder setCullMode:MTLCullModeNone];

        MetalCommandList cmdList(m_device, encoder, frame.arena);

        // The image set as it is now, into the frame's own tables: each slot's sampler, and its
        // texture's MTLResourceID (zero for an empty slot, which the shaders never sample). The
        // textures are made resident for the pass, as the tables only name them.
        const MetalImageSet& images = m_device.imageSet;
        const uint32_t slots = std::max(images.SlotsInUse(), 1u);
        std::vector<uint64_t> handles(slots, 0);
        std::vector<uint32_t> samplers(slots, 0);
        std::vector<id<MTLResource>> resident;
        resident.reserve(images.SlotsInUse());
        for (uint32_t slot = 0; slot < images.SlotsInUse(); ++slot)
        {
            const MetalImage* image = images.Images()[slot];
            if (image == nullptr || image->texture == nil)
                continue;
            const MTLResourceID id = image->texture.gpuResourceID;
            static_assert(sizeof(id) == sizeof(uint64_t));
            std::memcpy(&handles[slot], &id, sizeof(id));
            samplers[slot] = images.SamplerSlots()[slot];
            resident.push_back(image->texture);
        }
        cmdList.Bind(binding::kImageSamplers, cmdList.Push(samplers.data(), samplers.size() * sizeof(uint32_t), 16));
        cmdList.Bind(binding::kImages, cmdList.Push(handles.data(), handles.size() * sizeof(uint64_t), 16));
        if (!resident.empty())
            [encoder useResources:resident.data()
                            count:resident.size()
                            usage:MTLResourceUsageRead
                           stages:MTLRenderStageVertex | MTLRenderStageFragment];
        for (NSUInteger s = 0; s < m_device.samplers.size(); ++s)
        {
            [encoder setVertexSamplerState:m_device.samplers[s] atIndex:binding::kSamplers + s];
            [encoder setFragmentSamplerState:m_device.samplers[s] atIndex:binding::kSamplers + s];
        }

        // Pixels to clip space, y down as Metal's clip space is y up. A primitive's depth is its
        // draw order a step apart, the first a step above the cleared depth. Column-major, as the
        // shaders read it.
        GPUDrawPushConstants push = m_device.pushConstants;
        const float m[16] = {
            2.0f / float(m_width), 0.0f, 0.0f, 0.0f,
            0.0f, -2.0f / float(m_height), 0.0f, 0.0f,
            0.0f, 0.0f, kPrimitiveDepthStep, 0.0f,
            -1.0f, 1.0f, kPrimitiveDepthStep, 1.0f,
        };
        static_assert(sizeof(m) == sizeof(push.worldMatrix));
        std::memcpy(&push.worldMatrix, m, sizeof(m));
        static_cast<PrimitiveRenderer*>(m_graphics.guiRenderer)->Render(m_id, cmdList, m_device.pipelines, push);
        [encoder endEncoding];
    }

    std::optional<double> MetalWindowContext::PresentWindowFrame()
    {
        if (!m_presentPending)
            return std::nullopt;
        m_presentPending = false;
        Frame& frame = m_frames[*m_lastFrame];
        m_frameIndex = (m_frameIndex + 1) % kFrames;
        if (m_toImage || m_layer == nil)
            return std::nullopt;   // nothing to show: its frames are read back

        @autoreleasepool
        {
            // Waits for a drawable when all are on screen or queued: the vsync wait.
            const auto start = std::chrono::steady_clock::now();
            id<CAMetalDrawable> drawable = [m_layer nextDrawable];
            const double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (drawable == nil)
                return waited;
            id<MTLTexture> target = drawable.texture;
            id<MTLCommandBuffer> commandBuffer = [m_device.queue commandBuffer];
            commandBuffer.label = @"Present";
            id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
            // The part both have: the drawable is the frame's size unless the layer was resized
            // since.
            const NSUInteger width  = std::min<NSUInteger>(m_width, target.width);
            const NSUInteger height = std::min<NSUInteger>(m_height, target.height);
            [blit copyFromTexture:frame.drawTexture
                      sourceSlice:0
                      sourceLevel:0
                     sourceOrigin:MTLOriginMake(0, 0, 0)
                       sourceSize:MTLSizeMake(width, height, 1)
                        toTexture:target
                 destinationSlice:0
                 destinationLevel:0
                destinationOrigin:MTLOriginMake(0, 0, 0)];
            [blit endEncoding];
            [commandBuffer presentDrawable:drawable];
            // The slot waits for its copy to the drawable too.
            frame.fenceValue = m_device.Commit(commandBuffer);
            return waited;
        }
    }

    bool MetalWindowContext::ReadPixels(std::span<uint8_t> bgra)
    {
        if (!m_lastFrame || bgra.size() < size_t(m_width) * m_height * 4)
            return false;
        Frame& frame = m_frames[*m_lastFrame];
        if (frame.drawTexture == nil)
            return false;
        @autoreleasepool
        {
            const size_t row   = size_t(m_width) * 4;
            const size_t total = row * m_height;
            if (m_readback == nil || m_readback.length < total)
                m_readback = [m_device.device newBufferWithLength:total options:MTLResourceStorageModeShared];
            if (m_readback == nil)
                return false;
            id<MTLCommandBuffer> commandBuffer = [m_device.queue commandBuffer];
            commandBuffer.label = @"Read pixels";
            id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
            [blit copyFromTexture:frame.drawTexture
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:MTLSizeMake(m_width, m_height, 1)
                            toBuffer:m_readback
                   destinationOffset:0
              destinationBytesPerRow:row
            destinationBytesPerImage:total];
            [blit endEncoding];
            m_device.Wait(m_device.Commit(commandBuffer));
            std::memcpy(bgra.data(), m_readback.contents, total);
        }
        return true;
    }
}
