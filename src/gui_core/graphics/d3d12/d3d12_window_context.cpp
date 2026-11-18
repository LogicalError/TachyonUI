#include <gui_core/graphics/d3d12/d3d12_window_context.h>

#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/windows/gui_window_flags.h>

#include <algorithm>
#include <chrono>
#include <cstring>

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
    }

    D3D12WindowContext::D3D12WindowContext(D3D12GraphicsContext::Impl& device, OSWindowHandle osHandle, ID id, WindowFlags flags)
        : m_device(device), m_osHandle(osHandle), m_id(id), m_flags(flags)
    {
    }

    D3D12WindowContext* D3D12WindowContext::Create(D3D12GraphicsContext::Impl& device, HWND window, OSWindowHandle osHandle, ID id,
                                                   int2 size, WindowFlags flags)
    {
        auto* context = new D3D12WindowContext(device, osHandle, id, flags);
        ID3D12Device* d = device.device.Get();
        const auto fail = [&](const char* what, HRESULT hr) -> D3D12WindowContext*
        {
            TACHYON_LOG_ERROR(kLogD3D12, "window: {} failed ({})", what, HResultText(hr));
            delete context;
            return nullptr;
        };

        const uint32_t width  = uint32_t(std::max(size.x, 1));
        const uint32_t height = uint32_t(std::max(size.y, 1));
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

        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{ D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kFrames, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
        if (HRESULT hr = d->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&context->m_rtvHeap)); FAILED(hr))
            return fail("CreateDescriptorHeap", hr);
        context->m_rtvSize = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        for (Frame& frame : context->m_frames)
            if (HRESULT hr = d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator)); FAILED(hr))
                return fail("CreateCommandAllocator", hr);
        if (HRESULT hr = d->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, context->m_frames[0].allocator.Get(), nullptr,
                                              IID_PPV_ARGS(&context->m_list)); FAILED(hr))
            return fail("CreateCommandList", hr);
        context->m_list->Close();
        if (HRESULT hr = d->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&context->m_fence)); FAILED(hr))
            return fail("CreateFence", hr);

        if (!context->MakeSizedResources(width, height))
        {
            delete context;
            return nullptr;
        }
        return context;
    }

    D3D12WindowContext::~D3D12WindowContext()
    {
        // The queue, not this window's fence: a present still uses its buffer after the fence.
        m_device.WaitIdle();
        ReleaseSizedResources();
    }

    bool D3D12WindowContext::IsTransparent() const
    {
        return (m_flags & WindowFlags::Transparent) != WindowFlags::None;
    }

    bool D3D12WindowContext::MakeSizedResources(uint32_t width, uint32_t height)
    {
        ID3D12Device* d = m_device.device.Get();
        for (uint32_t i = 0; i < kFrames; ++i)
            if (HRESULT hr = m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_buffers[i])); FAILED(hr))
            {
                TACHYON_LOG_ERROR(kLogD3D12, "window: GetBuffer failed ({})", HResultText(hr));
                return false;
            }

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width            = width;
        desc.Height           = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.Format           = kDrawFormat;
        desc.SampleDesc       = { 1, 0 };
        desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = kDrawFormat;   // cleared to opaque black: the default {0, 0, 0, 0} but alpha
        clear.Color[3] = 1.0f;
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
    }

    void D3D12WindowContext::RenderWindowFrame(GraphicsContext* graphicsContext, IntRect rect, bool)
    {
        RecordWindowFrame(graphicsContext, rect);
        PresentWindowFrame();
    }

    void D3D12WindowContext::RecordWindowFrame(GraphicsContext*, IntRect rect)
    {
        if (rect.w <= 0 || rect.h <= 0)
            return;   // minimized: nothing to show

        Frame& frame = m_frames[m_frameIndex];
        m_device.Wait(m_fence.Get(), frame.fenceValue);

        // A new size: the frames in flight and their presents finish with the old buffers first.
        if (uint32_t(rect.w) != m_width || uint32_t(rect.h) != m_height)
        {
            m_device.WaitIdle();
            ReleaseSizedResources();
            const UINT flags = m_device.tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
            if (HRESULT hr = m_swapchain->ResizeBuffers(kFrames, UINT(rect.w), UINT(rect.h), kDrawFormat, flags); FAILED(hr))
            {
                TACHYON_LOG_ERROR(kLogD3D12, "window: ResizeBuffers failed ({})", HResultText(hr));
                return;
            }
            if (!MakeSizedResources(uint32_t(rect.w), uint32_t(rect.h)))
                return;
        }

        frame.allocator->Reset();
        m_list->Reset(frame.allocator.Get(), nullptr);

        // The frame's drawing: so far the background alone.
        if (frame.drawState != D3D12_RESOURCE_STATE_RENDER_TARGET)
        {
            const D3D12_RESOURCE_BARRIER toTarget = Transition(frame.drawTexture.Get(), frame.drawState, D3D12_RESOURCE_STATE_RENDER_TARGET);
            m_list->ResourceBarrier(1, &toTarget);
        }
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += SIZE_T(m_frameIndex) * m_rtvSize;
        const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        m_list->ClearRenderTargetView(rtv, black, 0, nullptr);

        // Into the swapchain's buffer, which presents it.
        ID3D12Resource* buffer = m_buffers[m_swapchain->GetCurrentBackBufferIndex()].Get();
        const D3D12_RESOURCE_BARRIER toCopy[2] = {
            Transition(frame.drawTexture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
            Transition(buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST),
        };
        m_list->ResourceBarrier(2, toCopy);
        m_list->CopyResource(buffer, frame.drawTexture.Get());
        const D3D12_RESOURCE_BARRIER toPresent = Transition(buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
        m_list->ResourceBarrier(1, &toPresent);
        frame.drawState = D3D12_RESOURCE_STATE_COPY_SOURCE;

        m_list->Close();
        ID3D12CommandList* lists[] = { m_list.Get() };
        m_device.queue->ExecuteCommandLists(1, lists);
        frame.fenceValue = ++m_fenceValue;
        m_device.Signal(m_fence.Get(), frame.fenceValue);
        m_lastFrame      = m_frameIndex;
        m_presentPending = true;
    }

    std::optional<double> D3D12WindowContext::PresentWindowFrame()
    {
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
        m_device.Wait(m_fence.Get(), frame.fenceValue);
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
        ID3D12CommandList* lists[] = { m_readList.Get() };
        m_device.queue->ExecuteCommandLists(1, lists);
        m_device.WaitIdle();

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
