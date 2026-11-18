#include <gui_core/graphics/d3d12/d3d12_window_context.h>

#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/graphics/gui_primitive_renderer.h>
#include <gui_core/windows/gui_layered_window.h>
#include <gui_core/windows/gui_window_flags.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

static constexpr tachyon::Diagnostics::LogCategory kLogD3D12 = tachyon::Diagnostics::MakeLogCategory("d3d12");

namespace tachyon::d3d12
{
    namespace
    {
        D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = resource;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = from;
            barrier.Transition.StateAfter  = to;
            return barrier;
        }

        D3D12_RESOURCE_DESC TextureDesc(uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags)
        {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width            = width;
            desc.Height           = height;
            desc.DepthOrArraySize = 1;
            desc.MipLevels        = 1;
            desc.Format           = format;
            desc.SampleDesc       = { 1, 0 };
            desc.Flags            = flags;
            return desc;
        }
    }

    D3D12WindowContext::D3D12WindowContext(D3D12GraphicsContext& graphics, HWND window, OSWindowHandle osHandle, ID id, WindowFlags flags,
                                           OnGuiFunc onGui)
        : m_graphics(graphics), m_device(graphics.Internal()), m_window(window), m_osHandle(osHandle), m_id(id), m_flags(flags),
          m_onGui(std::move(onGui)), m_toImage((flags & WindowFlags::RenderToImage) != WindowFlags::None),
          m_layered(!m_toImage && IsLayeredWindow(window))
    {
    }

    D3D12WindowContext* D3D12WindowContext::Create(D3D12GraphicsContext& graphics, HWND window, OSWindowHandle osHandle, ID id,
                                                   int2 size, WindowFlags flags, OnGuiFunc onGui)
    {
        auto* context = new D3D12WindowContext(graphics, window, osHandle, id, flags, std::move(onGui));
        D3D12GraphicsContext::Impl& device = context->m_device;
        ID3D12Device* d = device.device.Get();
        const auto fail = [&](const char* what, HRESULT hr) -> D3D12WindowContext*
        {
            TACHYON_LOG_ERROR(kLogD3D12, "window: {} failed ({})", what, HResultText(hr));
            delete context;
            return nullptr;
        };

        const uint32_t width  = uint32_t(std::max(size.x, 1));
        const uint32_t height = uint32_t(std::max(size.y, 1));
        if (!context->m_layered && !context->m_toImage)
        {
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width       = width;
            desc.Height      = height;
            desc.Format      = kDrawFormat;
            desc.SampleDesc  = { 1, 0 };
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = kFrames;
            desc.Scaling     = DXGI_SCALING_NONE;
            desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.AlphaMode   = DXGI_ALPHA_MODE_IGNORE;
            desc.Flags       = device.tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
            ComPtr<IDXGISwapChain1> swapchain1;
            if (HRESULT hr = device.factory->CreateSwapChainForHwnd(device.queue.Get(), window, &desc, nullptr, nullptr, &swapchain1); FAILED(hr))
                return fail("CreateSwapChainForHwnd", hr);
            // The window backend handles Alt+Enter (and exclusive fullscreen) itself.
            device.factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
            if (HRESULT hr = swapchain1.As(&context->m_swapchain); FAILED(hr))
                return fail("IDXGISwapChain3", hr);
        }

        const D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{ D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kFrames, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
        if (HRESULT hr = d->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&context->m_rtvHeap)); FAILED(hr))
            return fail("CreateDescriptorHeap", hr);
        context->m_rtvSize = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        const D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{ D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
        if (HRESULT hr = d->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&context->m_dsvHeap)); FAILED(hr))
            return fail("CreateDescriptorHeap", hr);

        const D3D12_DESCRIPTOR_HEAP_DESC imagesDesc{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, device.imageSet.Capacity(),
                                                     D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
        for (Frame& frame : context->m_frames)
        {
            if (HRESULT hr = d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator)); FAILED(hr))
                return fail("CreateCommandAllocator", hr);
            if (HRESULT hr = d->CreateDescriptorHeap(&imagesDesc, IID_PPV_ARGS(&frame.images)); FAILED(hr))
                return fail("CreateDescriptorHeap", hr);
        }
        if (HRESULT hr = d->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, context->m_frames[0].allocator.Get(), nullptr,
                                              IID_PPV_ARGS(&context->m_list)); FAILED(hr))
            return fail("CreateCommandList", hr);
        context->m_list->Close();

        if (!context->MakeSizedResources(width, height))
        {
            delete context;
            return nullptr;
        }
        return context;
    }

    D3D12WindowContext::~D3D12WindowContext()
    {
        // The whole queue, not this window's last submit: a present still uses its buffer after it.
        m_device.WaitIdle();
        ReleaseSizedResources();
        for (Frame& frame : m_frames)
            m_device.DestroyAfterGpu(std::move(frame.arena.buffer.allocation), 0);
    }

    bool D3D12WindowContext::IsTransparent() const
    {
        return (m_flags & WindowFlags::Transparent) != WindowFlags::None;
    }

    bool D3D12WindowContext::MakeSizedResources(uint32_t width, uint32_t height)
    {
        ID3D12Device* d = m_device.device.Get();
        for (uint32_t i = 0; m_swapchain && i < kFrames; ++i)
            if (HRESULT hr = m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_buffers[i])); FAILED(hr))
            {
                TACHYON_LOG_ERROR(kLogD3D12, "window: GetBuffer failed ({})", HResultText(hr));
                return false;
            }

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        const D3D12_RESOURCE_DESC desc = TextureDesc(width, height, kDrawFormat, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        D3D12_CLEAR_VALUE clear{};
        clear.Format   = kDrawFormat;   // opaque black, or transparent for a layered window
        clear.Color[3] = ClearAlpha();
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (Frame& frame : m_frames)
        {
            if (HRESULT hr = d->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear,
                                                        IID_PPV_ARGS(&frame.drawTexture)); FAILED(hr))
            {
                TACHYON_LOG_ERROR(kLogD3D12, "window: draw texture failed ({})", HResultText(hr));
                return false;
            }
            frame.drawState = D3D12_RESOURCE_STATE_RENDER_TARGET;
            d->CreateRenderTargetView(frame.drawTexture.Get(), nullptr, rtv);
            rtv.ptr += m_rtvSize;
        }

        // Depth is cleared to 0: later primitives are nearer.
        const D3D12_RESOURCE_DESC depthDesc = TextureDesc(width, height, kDepthFormat, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                                                                                       | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
        D3D12_CLEAR_VALUE depthClear{};
        depthClear.Format = kDepthFormat;
        if (HRESULT hr = d->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear,
                                                    IID_PPV_ARGS(&m_depth)); FAILED(hr))
        {
            TACHYON_LOG_ERROR(kLogD3D12, "window: depth buffer failed ({})", HResultText(hr));
            return false;
        }
        d->CreateDepthStencilView(m_depth.Get(), nullptr, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

        m_width  = width;
        m_height = height;
        m_lastFrame.reset();
        return true;
    }

    void D3D12WindowContext::ReleaseSizedResources()
    {
        for (ComPtr<ID3D12Resource>& buffer : m_buffers)
            buffer.Reset();
        for (Frame& frame : m_frames)
            frame.drawTexture.Reset();
        m_depth.Reset();
    }

    void D3D12WindowContext::RenderWindowFrame(GraphicsContext*, IntRect rect, bool resizing)
    {
        Record(rect, resizing);
        PresentWindowFrame();
    }

    void D3D12WindowContext::RecordWindowFrame(GraphicsContext*, IntRect rect)
    {
        Record(rect, false);
    }

    void D3D12WindowContext::Record(IntRect rect, bool resizing)
    {
        // A frame recorded and not presented yet (a live resize's redraw in between) goes first.
        if (m_presentPending)
            PresentWindowFrame();
        if (rect.w <= 0 || rect.h <= 0)
            return;   // minimized: nothing to show

        // A live resize lays the window out again at its new size, and its styles may change.
        if (resizing && m_onGui && m_graphics.guiRenderer != nullptr)
        {
            auto* renderer = static_cast<PrimitiveRenderer*>(m_graphics.guiRenderer);
            const bool hasTarget = renderer->StartResizing(m_id);
            renderer->Reset();
            m_onGui();
            m_graphics.RePrepareFrame();
            if (hasTarget)
                renderer->StopResizing();
        }

        Frame& frame = m_frames[m_frameIndex];
        m_device.Wait(frame.fenceValue);

        // A new size. A swapchain's frames in flight and their presents finish with the old
        // buffers first; without one, the old textures are freed once the GPU is past them.
        if (uint32_t(rect.w) != m_width || uint32_t(rect.h) != m_height)
        {
            if (m_swapchain)
            {
                m_device.WaitIdle();
                ReleaseSizedResources();
                const UINT flags = m_device.tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
                if (HRESULT hr = m_swapchain->ResizeBuffers(kFrames, UINT(rect.w), UINT(rect.h), kDrawFormat, flags); FAILED(hr))
                {
                    TACHYON_LOG_ERROR(kLogD3D12, "window: ResizeBuffers failed ({})", HResultText(hr));
                    return;
                }
            }
            else
            {
                for (Frame& f : m_frames)
                    m_device.DestroyAfterGpu(std::move(f.drawTexture), m_device.submitted + 1);
                m_device.DestroyAfterGpu(std::move(m_depth), m_device.submitted + 1);
            }
            if (!MakeSizedResources(uint32_t(rect.w), uint32_t(rect.h)))
                return;
        }

        frame.allocator->Reset();
        m_list->Reset(frame.allocator.Get(), nullptr);
        m_device.RecordPendingUploads(m_list.Get());
        // The window's scenes first: its UI samples them.
        m_device.RecordScenes(m_list.Get(), m_id);

        if (frame.drawState != D3D12_RESOURCE_STATE_RENDER_TARGET)
        {
            const D3D12_RESOURCE_BARRIER toTarget = Transition(frame.drawTexture.Get(), frame.drawState, D3D12_RESOURCE_STATE_RENDER_TARGET);
            m_list->ResourceBarrier(1, &toTarget);
        }
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += SIZE_T(m_frameIndex) * m_rtvSize;
        Draw(frame, rtv);

        // Into the swapchain's buffer, which presents it; a layered window's is read back instead.
        const D3D12_RESOURCE_BARRIER toSource = Transition(frame.drawTexture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                           D3D12_RESOURCE_STATE_COPY_SOURCE);
        if (m_swapchain)
        {
            ID3D12Resource* buffer = m_buffers[m_swapchain->GetCurrentBackBufferIndex()].Get();
            const D3D12_RESOURCE_BARRIER toCopy[2] = {
                toSource,
                Transition(buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST),
            };
            m_list->ResourceBarrier(2, toCopy);
            m_list->CopyResource(buffer, frame.drawTexture.Get());
            const D3D12_RESOURCE_BARRIER toPresent = Transition(buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
            m_list->ResourceBarrier(1, &toPresent);
        }
        else
            m_list->ResourceBarrier(1, &toSource);
        frame.drawState = D3D12_RESOURCE_STATE_COPY_SOURCE;

        m_list->Close();
        frame.fenceValue = m_device.Submit(m_list.Get());
        m_lastFrame      = m_frameIndex;
        m_presentPending = true;
    }

    void D3D12WindowContext::Draw(Frame& frame, D3D12_CPU_DESCRIPTOR_HANDLE rtv)
    {
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
        const float background[4] = { 0.0f, 0.0f, 0.0f, ClearAlpha() };
        m_list->ClearRenderTargetView(rtv, background, 0, nullptr);
        m_list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
        if (m_graphics.guiRenderer == nullptr)
            return;

        // The image set's views as they are now; a frame in flight keeps its own copy.
        const D3D12ImageSet& images = m_device.imageSet;
        if (images.SlotsInUse() > 0)
            m_device.device->CopyDescriptorsSimple(images.SlotsInUse(), frame.images->GetCPUDescriptorHandleForHeapStart(),
                                                   images.CpuStart(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        ID3D12DescriptorHeap* heaps[] = { frame.images.Get() };
        m_list->SetDescriptorHeaps(1, heaps);
        m_list->SetGraphicsRootSignature(m_device.rootSignature.Get());
        m_list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        const D3D12_VIEWPORT viewport{ 0.0f, 0.0f, float(m_width), float(m_height), 0.0f, 1.0f };
        const D3D12_RECT     scissor{ 0, 0, LONG(m_width), LONG(m_height) };
        m_list->RSSetViewports(1, &viewport);
        m_list->RSSetScissorRects(1, &scissor);
        m_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        frame.arena.offset = 0;
        D3D12CommandList cmdList(m_device, m_list.Get(), frame.arena);
        // Each image's sampler, as t7 holds them (one entry at least: a root SRV needs a buffer).
        const std::vector<uint32_t>& samplers = images.SamplerSlots();
        const uint32_t none = 0;
        cmdList.SetSamplerTable(samplers.empty() ? cmdList.Push(&none, sizeof(none), 16)
                                                 : cmdList.Push(samplers.data(), samplers.size() * sizeof(uint32_t), 16));

        // Pixels to clip space, y down as Direct3D's clip space is y up. A primitive's depth is
        // its draw order a step apart, the first a step above the cleared depth. Column-major,
        // as the shaders read it.
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
    }

    std::optional<double> D3D12WindowContext::PresentWindowFrame()
    {
        if (m_toImage)
        {
            // Nothing to show: the next frame takes the other slot.
            if (std::exchange(m_presentPending, false))
                m_frameIndex = (m_frameIndex + 1) % kFrames;
            return std::nullopt;
        }
        if (m_layered)
        {
            // Read back once the frame is drawn. A layered window doesn't redraw on the OS's
            // expose events, so it's shown again from the last picture each time.
            if (m_presentPending)
            {
                m_presentPending = false;
                m_layeredSize = { m_width, m_height };
                m_layeredPixels.resize(size_t(m_width) * m_height * 4);
                if (!ReadPixels(m_layeredPixels))
                    m_layeredPixels.clear();
                m_frameIndex = (m_frameIndex + 1) % kFrames;
            }
            if (!m_layeredPixels.empty())
                ShowLayeredPixels(m_window, m_layeredPixels, m_layeredSize);
            return std::nullopt;
        }
        if (!m_presentPending)
            return std::nullopt;
        m_presentPending = false;
        const auto start = std::chrono::steady_clock::now();
        if (HRESULT hr = m_swapchain->Present(1, 0); FAILED(hr))
            TACHYON_LOG_ERROR(kLogD3D12, "window: Present failed ({})", HResultText(hr));
        m_frameIndex = (m_frameIndex + 1) % kFrames;
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }

    bool D3D12WindowContext::ReadPixels(std::span<uint8_t> bgra)
    {
        if (!m_lastFrame || bgra.size() < size_t(m_width) * m_height * 4)
            return false;
        Frame& frame = m_frames[*m_lastFrame];
        m_device.Wait(frame.fenceValue);
        ID3D12Device* d = m_device.device.Get();

        const D3D12_RESOURCE_DESC desc = frame.drawTexture->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT64 total = 0;
        d->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
        if (!m_readAllocator)
        {
            if (FAILED(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_readAllocator)))
                || FAILED(d->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_readAllocator.Get(), nullptr, IID_PPV_ARGS(&m_readList))))
                return false;
            m_readList->Close();
        }
        if (m_readbackSize < total)
        {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC buffer{};
            buffer.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            buffer.Width            = total;
            buffer.Height           = 1;
            buffer.DepthOrArraySize = 1;
            buffer.MipLevels        = 1;
            buffer.SampleDesc       = { 1, 0 };
            buffer.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            m_readback.Reset();
            if (FAILED(d->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  IID_PPV_ARGS(&m_readback))))
                return false;
            m_readbackSize = total;
        }

        // The frame's texture is left as a copy source once it's in the swapchain.
        m_readAllocator->Reset();
        m_readList->Reset(m_readAllocator.Get(), nullptr);
        D3D12_TEXTURE_COPY_LOCATION from{};
        from.pResource        = frame.drawTexture.Get();
        from.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        from.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION to{};
        to.pResource       = m_readback.Get();
        to.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        to.PlacedFootprint = footprint;
        m_readList->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        m_readList->Close();
        m_device.Wait(m_device.Submit(m_readList.Get()));

        void* mapped = nullptr;
        const D3D12_RANGE read{ 0, SIZE_T(total) };
        if (FAILED(m_readback->Map(0, &read, &mapped)))
            return false;
        const size_t row = size_t(m_width) * 4;
        for (uint32_t y = 0; y < m_height; ++y)
            std::memcpy(bgra.data() + y * row, static_cast<const uint8_t*>(mapped) + footprint.Offset + size_t(y) * footprint.Footprint.RowPitch, row);
        const D3D12_RANGE written{ 0, 0 };
        m_readback->Unmap(0, &written);
        return true;
    }
}
