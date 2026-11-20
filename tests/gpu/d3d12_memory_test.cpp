// Where the Direct3D 12 backend keeps what every frame reads: the styles and the quads' index
// buffer are copied into the GPU's own memory, not read across the bus each frame; and a frame
// copies only the resident buffers still waiting.

#include "allocation_count.h"
#include "d3d12_test_harness.h"

#include <gui_core/graphics/d3d12/d3d12_device.h>

using namespace D3D12TestHarness;

namespace
{
    D3D12_HEAP_TYPE HeapType(ID3D12Resource* resource)
    {
        D3D12_HEAP_PROPERTIES properties{};
        D3D12_HEAP_FLAGS      flags{};
        if (resource == nullptr || FAILED(resource->GetHeapProperties(&properties, &flags)))
            return D3D12_HEAP_TYPE(0);
        return properties.Type;
    }

    ID3D12Resource* ResidentResource(tachyon::d3d12::D3D12GraphicsContext::Impl& d, tachyon::RHI::ResidentBufferId id)
    {
        const auto it = d.residents.find(id);
        return it != d.residents.end() ? it->second.gpu.Resource() : nullptr;
    }
}

// The box and color styles are in GPU memory, and a style changed later is copied there too.
TEST_F(D3D12ImageWindowTest, TheStylesAreInGpuMemory)
{
    const tachyon::VisualColorStyleIndex red = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(tachyon::color4b{ 255, 0, 0, 255 }));
    graphics->BeginFrame();
    RenderFrame([&](tachyon::GUIRenderer* r) { r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x, kSize.y }, kFullClip, red, 0.0f); });
    auto& d = graphics->Internal();
    EXPECT_EQ(HeapType(ResidentResource(d, d.boxStyles)), D3D12_HEAP_TYPE_DEFAULT) << "the box styles";
    EXPECT_EQ(HeapType(ResidentResource(d, d.colorStyles)), D3D12_HEAP_TYPE_DEFAULT) << "the color styles";
    EXPECT_EQ(Rgba(kSize.x / 2, kSize.y / 2), (std::array<int, 4>{ 255, 0, 0, 255 }));

    const tachyon::VisualColorStyleIndex green = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(tachyon::color4b{ 0, 255, 0, 255 }));
    graphics->BeginFrame();
    RenderFrame([&](tachyon::GUIRenderer* r) { r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x, kSize.y }, kFullClip, green, 0.0f); });
    EXPECT_EQ(HeapType(ResidentResource(d, d.colorStyles)), D3D12_HEAP_TYPE_DEFAULT) << "the color styles, changed";
    EXPECT_EQ(Rgba(kSize.x / 2, kSize.y / 2), (std::array<int, 4>{ 0, 255, 0, 255 })) << "the new style draws";
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
}

// A frame copies the resident buffers made since the one before, and no others: once copied,
// one is Ready and off the list. One let go of before its copy is left out.
TEST_F(D3D12ImageWindowTest, AFrameCopiesOnlyTheResidentBuffersWaiting)
{
    auto& d = graphics->Internal();
    const uint32_t data[4] = { 1, 2, 3, 4 };
    const tachyon::RHI::UploadRequest request{ data, sizeof(data), 16 };
    tachyon::DeviceAddress address{};
    const tachyon::RHI::ResidentBufferId kept    = d.CreateResidentBuffer(std::span(&request, 1), std::span(&address, 1));
    const tachyon::RHI::ResidentBufferId dropped = d.CreateResidentBuffer(std::span(&request, 1), std::span(&address, 1));
    ASSERT_NE(kept, 0u);
    ASSERT_NE(dropped, 0u);
    EXPECT_EQ(d.ResidentState(kept), tachyon::RHI::ResidentState::Pending);
    d.ReleaseResident(dropped);

    graphics->BeginFrame();
    RenderFrame([](tachyon::GUIRenderer*) {});
    EXPECT_EQ(d.ResidentState(kept), tachyon::RHI::ResidentState::Ready);
    EXPECT_EQ(d.ResidentState(dropped), tachyon::RHI::ResidentState::Gone);
    EXPECT_TRUE(d.pendingResidents.empty()) << "nothing left waiting";

    graphics->BeginFrame();
    RenderFrame([](tachyon::GUIRenderer*) {});
    EXPECT_EQ(d.ResidentState(kept), tachyon::RHI::ResidentState::Ready);
    d.ReleaseResident(kept);
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
}

// A frame like the one before reuses that frame's storage: recording and submitting it allocates
// nothing.
TEST_F(D3D12ImageWindowTest, ASteadyFrameAllocatesNothing)
{
    if (!AllocationCount::ScopedAllocationCount::Supported())
        GTEST_SKIP() << "allocations are counted in Debug builds with MSVC's CRT";
    const tachyon::VisualColorStyleIndex white = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 255 }));
    tachyon::GUIRenderer* r = graphics->guiRenderer;
    const auto frame = [&] {
        graphics->BeginFrame();
        graphics->PrepareFrame(styleManager, gradientManager, 0.0f);
        r->Reset();
        if (r->BeginWindow(window->GetID()))
        {
            for (int i = 0; i < 64; ++i)
                r->RenderQuad(tachyon::IntRect{ i * 4, 0, 4, 4 }, kFullClip, white, 0.0f);
            r->EndWindow();
        }
    };
    for (int i = 0; i < 4; ++i)   // what the first frames make is kept
    {
        frame();
        window->RenderWindowFrame(graphics, kFullClip, false);
    }
    frame();
    uint64_t allocations = 0;
    {
        AllocationCount::ScopedAllocationCount count;
        window->RenderWindowFrame(graphics, kFullClip, false);
        allocations = count.Count();
    }
    EXPECT_EQ(allocations, 0u) << "allocations recording and submitting a steady frame";
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
}

// The quads' index buffer is in GPU memory. A frame with more quads than it holds makes a bigger
// one in the middle of its drawing, and draws them all.
TEST_F(D3D12ImageWindowTest, TheQuadIndicesAreInGpuMemoryAndGrowMidFrame)
{
    const tachyon::VisualColorStyleIndex white = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 255 }));
    const tachyon::VisualColorStyleIndex red   = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(tachyon::color4b{ 255, 0, 0, 255 }));
    graphics->BeginFrame();
    RenderFrame([&](tachyon::GUIRenderer* r) { r->RenderQuad(tachyon::IntRect{ 0, 0, 8, 8 }, kFullClip, white, 0.0f); });
    auto& d = graphics->Internal();
    const uint32_t first = d.quadCapacity;
    ASSERT_GT(first, 0u);
    EXPECT_EQ(HeapType(d.quadIndices.Resource()), D3D12_HEAP_TYPE_DEFAULT);

    // More quads than the buffer holds; the last one covers the right half.
    const int many = int(first) + 1000;
    graphics->BeginFrame();
    RenderFrame([&](tachyon::GUIRenderer* r) {
        for (int i = 0; i < many - 1; ++i)
            r->RenderQuad(tachyon::IntRect{ 0, 0, 8, 8 }, kFullClip, white, 0.0f);
        r->RenderQuad(tachyon::IntRect{ kSize.x / 2, 0, kSize.x / 2, kSize.y }, kFullClip, red, 0.0f);
    });
    EXPECT_GE(d.quadCapacity, uint32_t(many)) << "grown";
    EXPECT_EQ(HeapType(d.quadIndices.Resource()), D3D12_HEAP_TYPE_DEFAULT);
    EXPECT_EQ(Rgba(kSize.x * 3 / 4, kSize.y / 2), (std::array<int, 4>{ 255, 0, 0, 255 })) << "the last quad draws";
    EXPECT_EQ(Rgba(2, 2), (std::array<int, 4>{ 255, 255, 255, 255 })) << "and the first";
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
}
