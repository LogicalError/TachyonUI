// A host's own Direct3D 12 device: the host (a game engine) makes its device and queue and hands
// them to the GUI. The GUI renders on them, leaves them as it found them, rejects what it can't
// use, and shares textures with the host under the documented contract, with nothing from the
// debug layer. Tests SKIP themselves when no GPU runs the backend.

#include "d3d12_test_harness.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <chrono>
#include <cstring>
#include <thread>

using namespace D3D12TestHarness;
using Microsoft::WRL::ComPtr;

namespace
{
    // Stands in for a game engine: its own device and queue, and command lists it runs itself.
    struct Host
    {
        ComPtr<ID3D12Device>       device;
        ComPtr<ID3D12CommandQueue> queue;

        bool Create()
        {
            // The host turns the debug layer on, before its device is made.
            ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
                debug->EnableDebugLayer();
            if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))
                || !tachyon::d3d12::D3D12GraphicsContext::CheckDeviceSupport(device.Get()))
                return false;
            const D3D12_COMMAND_QUEUE_DESC desc{ D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0 };
            return SUCCEEDED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)));
        }

        // Records with `record` into a list of the host's, runs it on the shared queue, and waits.
        template <class Record>
        void Run(Record&& record)
        {
            ComPtr<ID3D12CommandAllocator>    allocator;
            ComPtr<ID3D12GraphicsCommandList> list;
            ASSERT_TRUE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
            ASSERT_TRUE(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))));
            record(list.Get());
            list->Close();
            ID3D12CommandList* lists[] = { list.Get() };
            queue->ExecuteCommandLists(1, lists);
            Finish();
        }

        // Waits until the queue is done with everything submitted to it.
        void Finish()
        {
            ComPtr<ID3D12Fence> fence;
            ASSERT_TRUE(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
            queue->Signal(fence.Get(), 1);
            const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            fence->SetEventOnCompletion(1, event);
            WaitForSingleObject(event, 10000);
            CloseHandle(event);
        }

        ComPtr<ID3D12Resource> Texture(tachyon::uint2 size, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state,
                                       const D3D12_CLEAR_VALUE* clear = nullptr)
        {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width            = size.x;
            desc.Height           = size.y;
            desc.DepthOrArraySize = 1;
            desc.MipLevels        = 1;
            desc.Format           = format;
            desc.SampleDesc       = { 1, 0 };
            desc.Flags            = flags;
            ComPtr<ID3D12Resource> texture;
            device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, clear, IID_PPV_ARGS(&texture));
            return texture;
        }

        // How many references the device has.
        ULONG References() const
        {
            device->AddRef();
            return device->Release();
        }
    };

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

    class D3D12ExternalDeviceTest : public D3D12ImageWindowTest
    {
    protected:
        Host  host;
        ULONG referencesBefore = 0;

        bool BeforeInitialize() override
        {
            if (!host.Create())
                return false;
            referencesBefore = host.References();
            graphics->SetExternalDevice({ host.device.Get(), host.queue.Get() });
            return true;
        }

        // The GUI's objects go now, before the test looks at the host's device.
        void CleanUpTheGUI()
        {
            graphics->DestroyWindowContext(window);
            window = nullptr;
            gradientManager.ReleaseAll(graphics);
            graphics->Cleanup();
        }
    };
}

// The GUI renders on the host's device and queue, and gives back what it took: the device's
// references are the host's alone after Cleanup, and the queue still runs the host's work.
TEST_F(D3D12ExternalDeviceTest, RendersOnTheHostsDeviceAndLeavesItIntact)
{
    EXPECT_FALSE(graphics->GetDeviceName().empty());
    const tachyon::VisualColorStyleIndex white = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 255 }));
    graphics->BeginFrame();
    RenderFrame([&](tachyon::GUIRenderer* r) { r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x / 2, kSize.y }, kFullClip, white, 0.0f); });
    EXPECT_EQ(Rgba(kSize.x / 4, kSize.y / 2), (std::array<int, 4>{ 255, 255, 255, 255 })) << "drawn on the host's device";
    EXPECT_TRUE(sink.Empty()) << sink.Dump();

    CleanUpTheGUI();
    EXPECT_EQ(host.References(), referencesBefore) << "the GUI let go of the device";
    host.Run([](ID3D12GraphicsCommandList*) {});   // the queue still runs
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
}

// A device without its queue, or a queue of the wrong kind, fails Initialize with the reason.
TEST(D3D12ExternalDevice, RejectsWhatItCantUse)
{
    Host host;
    if (!host.Create())
        GTEST_SKIP() << "no GPU runs the Direct3D 12 backend";
    tachyon::SDLWindowBackend windows;
    {
        tachyon::d3d12::D3D12GraphicsContext graphics;
        windows.SetGraphicsContext(&graphics);
        graphics.SetExternalDevice({ host.device.Get(), nullptr });
        const tachyon::Result<void> result = graphics.Initialize(&windows);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.GetError().code, tachyon::ErrorCode::InvalidArgument);
    }
    {
        ComPtr<ID3D12CommandQueue> copyQueue;
        const D3D12_COMMAND_QUEUE_DESC desc{ D3D12_COMMAND_LIST_TYPE_COPY, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0 };
        ASSERT_TRUE(SUCCEEDED(host.device->CreateCommandQueue(&desc, IID_PPV_ARGS(&copyQueue))));
        tachyon::d3d12::D3D12GraphicsContext graphics;
        windows.SetGraphicsContext(&graphics);
        graphics.SetExternalDevice({ host.device.Get(), copyQueue.Get() });
        const tachyon::Result<void> result = graphics.Initialize(&windows);
        ASSERT_FALSE(result);
        EXPECT_NE(result.GetError().message.find("direct"), std::string::npos) << result.GetError().message;
    }
    windows.SetGraphicsContext(nullptr);
}

// The 3D-viewport case: the host renders into a texture of its own, and the GUI samples it in a
// primitive. The host leaves it in ALL_SHADER_RESOURCE in work submitted before the GUI's frame.
TEST_F(D3D12ExternalDeviceTest, SamplesAHostRenderedTexture)
{
    const tachyon::uint2 size{ 64, 64 };
    const D3D12_CLEAR_VALUE green{ DXGI_FORMAT_R8G8B8A8_UNORM, { 0.0f, 1.0f, 0.0f, 1.0f } };
    ComPtr<ID3D12Resource> texture = host.Texture(size, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                                  D3D12_RESOURCE_STATE_RENDER_TARGET, &green);
    ASSERT_TRUE(texture);
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    const D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{ D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
    ASSERT_TRUE(SUCCEEDED(host.device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap))));
    host.device->CreateRenderTargetView(texture.Get(), nullptr, rtvHeap->GetCPUDescriptorHandleForHeapStart());
    host.Run([&](ID3D12GraphicsCommandList* list) {
        list->ClearRenderTargetView(rtvHeap->GetCPUDescriptorHandleForHeapStart(), green.Color, 0, nullptr);
        const D3D12_RESOURCE_BARRIER toRead = Transition(texture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
        list->ResourceBarrier(1, &toRead);
    });

    // Only texture-typed color styles sample the image passed to a primitive.
    const tachyon::VisualColorStyleIndex stretched = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(
        tachyon::color4b{ 255, 255, 255, 255 }, tachyon::color4b{ 255, 255, 255, 255 }, tachyon::ColorType::StretchedTexture));
    graphics->BeginFrame();
    const tachyon::ImageIndex index = graphics->RegisterExternalImage(texture.Get(), tachyon::Sampler::LinearClampToEdge);
    ASSERT_NE(index, tachyon::ImageIndex::Invalid);
    RenderFrame([&](tachyon::GUIRenderer* r) {
        r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x / 2, kSize.y }, kFullClip, stretched, 0.0f, index);
    });
    const auto inside = Rgba(kSize.x / 4, kSize.y / 2);
    EXPECT_GT(inside[1], 200) << "the host's green";
    EXPECT_LT(inside[0], 50);
    EXPECT_EQ(Rgba(kSize.x * 3 / 4, kSize.y / 2)[3], 0) << "nothing beside it";

    graphics->UnregisterImage(index);
    graphics->WaitIdle();
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
}

// A render-to-image window's frame copied into the host's texture, which the host reads after
// the GUI's frame.
TEST_F(D3D12ExternalDeviceTest, CopiesTheWindowIntoTheHostsTexture)
{
    ComPtr<ID3D12Resource> texture = host.Texture({ uint32_t(kSize.x), uint32_t(kSize.y) }, DXGI_FORMAT_B8G8R8A8_UNORM,
                                                  D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    ASSERT_TRUE(texture);
    graphics->SetWindowImageTarget(window->GetID(), texture.Get());
    const tachyon::VisualColorStyleIndex white = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 255 }));
    graphics->BeginFrame();
    RenderFrame([&](tachyon::GUIRenderer* r) { r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x / 2, kSize.y }, kFullClip, white, 0.0f); });

    // The host reads its texture back, in work after the GUI's frame.
    const D3D12_RESOURCE_DESC desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes = 0;
    host.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width            = bytes;
    buffer.Height           = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels        = 1;
    buffer.SampleDesc       = { 1, 0 };
    buffer.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    ASSERT_TRUE(SUCCEEDED(host.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST,
                                                               nullptr, IID_PPV_ARGS(&readback))));
    host.Run([&](ID3D12GraphicsCommandList* list) {
        const D3D12_RESOURCE_BARRIER toCopy = Transition(texture.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1, &toCopy);
        D3D12_TEXTURE_COPY_LOCATION to{ readback.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT };
        to.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION from{ texture.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX };
        from.SubresourceIndex = 0;
        list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        const D3D12_RESOURCE_BARRIER back = Transition(texture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
        list->ResourceBarrier(1, &back);
    });
    void* mapped = nullptr;
    const D3D12_RANGE all{ 0, SIZE_T(bytes) };
    ASSERT_TRUE(SUCCEEDED(readback->Map(0, &all, &mapped)));
    auto at = [&](int x, int y) {   // BGRA8
        const uint8_t* p = static_cast<const uint8_t*>(mapped) + footprint.Offset + size_t(y) * footprint.Footprint.RowPitch + size_t(x) * 4;
        return std::array<int, 4>{ p[2], p[1], p[0], p[3] };
    };
    EXPECT_EQ(at(kSize.x / 4, kSize.y / 2), (std::array<int, 4>{ 255, 255, 255, 255 })) << "the window's quad, in the host's texture";
    EXPECT_EQ(at(kSize.x * 3 / 4, kSize.y / 2), (std::array<int, 4>{ 0, 0, 0, 0 })) << "transparent beside it";
    const D3D12_RANGE none{ 0, 0 };
    readback->Unmap(0, &none);

    graphics->SetWindowImageTarget(window->GetID(), nullptr);
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
}

// The GUI's frame waits on the queue for the host's fence: for work on another queue the GUI
// shows. Until the fence is reached, nothing after the frame's submit runs.
TEST_F(D3D12ExternalDeviceTest, AFrameWaitsForTheHostsFence)
{
    ComPtr<ID3D12Fence> computeDone, after;
    ASSERT_TRUE(SUCCEEDED(host.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&computeDone))));
    ASSERT_TRUE(SUCCEEDED(host.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&after))));

    graphics->BeginFrame();
    graphics->AddFrameWait(computeDone.Get(), 1);
    graphics->PrepareFrame(styleManager, gradientManager, 0.0f);
    window->RenderWindowFrame(graphics, kFullClip, false);
    host.queue->Signal(after.Get(), 1);   // queued behind the GUI's frame
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(after->GetCompletedValue(), 0u) << "the frame waits for the host's fence";

    computeDone->Signal(1);   // the other queue's work is done
    host.Finish();
    EXPECT_EQ(after->GetCompletedValue(), 1u);

    // Waited on once: the next frame runs at once.
    graphics->BeginFrame();
    RenderFrame([](tachyon::GUIRenderer*) {});
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
}
