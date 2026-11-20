// Render-to-image windows and the app's scenes on SDL GPU, on each of SDL's drivers: a
// render-to-image window draws and reads back, and a scene's drawing shows where the UI puts it,
// with nothing from SDL GPU's debug mode.

#include "allocation_count.h"
#include "sdlgpu_test_harness.h"

#include <gui_core/graphics/sdlgpu/sdlgpu_device.h>

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_stdinc.h>

#include <cstring>

using namespace SDLGPUTestHarness;

// A render-to-image window has no OS window: it draws, and what it draws is read back, the
// rest left transparent.
TEST_P(SDLGPUImageWindowTest, ARenderToImageWindowDrawsAndReadsBack)
{
    const tachyon::VisualColorStyleIndex white = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 255 }));
    graphics->BeginFrame();
    RenderFrame([&](tachyon::GUIRenderer* r) { r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x / 2, kSize.y }, kFullClip, white, 0.0f); });
    EXPECT_EQ(Rgba(kSize.x / 4, kSize.y / 2), (std::array<int, 4>{ 255, 255, 255, 255 })) << "the quad";
    EXPECT_EQ(Rgba(kSize.x * 3 / 4, kSize.y / 2), (std::array<int, 4>{ 0, 0, 0, 0 })) << "transparent where nothing is drawn";
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
}

// A color made after the gradient page is on the GPU is written into it, not a page made again:
// both colors draw, and the page keeps its image.
TEST_P(SDLGPUImageWindowTest, AColorMadeLaterIsWrittenIntoThePage)
{
    const tachyon::VisualColorStyleIndex red = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(tachyon::color4b{ 255, 0, 0, 255 }));
    graphics->BeginFrame();
    RenderFrame([&](tachyon::GUIRenderer* r) { r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x / 2, kSize.y }, kFullClip, red, 0.0f); });
    tachyon::Image* const page = gradientManager.GetGradientImage(graphics, 0);

    const tachyon::VisualColorStyleIndex green = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(tachyon::color4b{ 0, 255, 0, 255 }));
    graphics->BeginFrame();
    RenderFrame([&](tachyon::GUIRenderer* r) {
        r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x / 2, kSize.y }, kFullClip, red, 0.0f);
        r->RenderQuad(tachyon::IntRect{ kSize.x / 2, 0, kSize.x / 2, kSize.y }, kFullClip, green, 0.0f);
    });
    EXPECT_EQ(Rgba(kSize.x / 4, kSize.y / 2), (std::array<int, 4>{ 255, 0, 0, 255 })) << "red, still";
    EXPECT_EQ(Rgba(kSize.x * 3 / 4, kSize.y / 2), (std::array<int, 4>{ 0, 255, 0, 255 })) << "the new green";
    EXPECT_EQ(gradientManager.GetGradientImage(graphics, 0), page) << "written into, not made again";
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
}

// A frame copies the resident buffers made since the one before, and no others: once copied,
// one is Ready and off the list. One let go of before its copy is left out.
TEST_P(SDLGPUImageWindowTest, AFrameCopiesOnlyTheResidentBuffersWaiting)
{
    auto& d = graphics->Internal();
    const uint32_t data[4] = { 1, 2, 3, 4 };
    const tachyon::RHI::UploadRequest request{ data, sizeof(data), 16 };
    tachyon::DeviceAddress address{};
    const tachyon::RHI::ResidentBufferId kept    = d.CreateResident(std::span(&request, 1), std::span(&address, 1));
    const tachyon::RHI::ResidentBufferId dropped = d.CreateResident(std::span(&request, 1), std::span(&address, 1));
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

namespace
{
    // SDL's own allocations, which its GPU calls make as it pleases, left out of the count: SDL
    // allocates through these, which call its originals with the count paused.
    SDL_malloc_func  g_sdlMalloc  = nullptr;
    SDL_calloc_func  g_sdlCalloc  = nullptr;
    SDL_realloc_func g_sdlRealloc = nullptr;
    SDL_free_func    g_sdlFree    = nullptr;

    void* SDLCALL UncountedMalloc(size_t size)
    {
        AllocationCount::Ignored ignored;
        return g_sdlMalloc(size);
    }
    void* SDLCALL UncountedCalloc(size_t count, size_t size)
    {
        AllocationCount::Ignored ignored;
        return g_sdlCalloc(count, size);
    }
    void* SDLCALL UncountedRealloc(void* memory, size_t size)
    {
        AllocationCount::Ignored ignored;
        return g_sdlRealloc(memory, size);
    }
    void SDLCALL UncountedFree(void* memory) { g_sdlFree(memory); }

    struct SDLAllocationsUncounted
    {
        SDLAllocationsUncounted()
        {
            SDL_GetMemoryFunctions(&g_sdlMalloc, &g_sdlCalloc, &g_sdlRealloc, &g_sdlFree);
            SDL_SetMemoryFunctions(&UncountedMalloc, &UncountedCalloc, &UncountedRealloc, &UncountedFree);
        }
        ~SDLAllocationsUncounted() { SDL_SetMemoryFunctions(g_sdlMalloc, g_sdlCalloc, g_sdlRealloc, g_sdlFree); }
    };
}

// A frame like the one before reuses that frame's storage: recording and submitting it allocates
// nothing outside SDL.
TEST_P(SDLGPUImageWindowTest, ASteadyFrameAllocatesNothing)
{
    if (!AllocationCount::ScopedAllocationCount::Supported())
        GTEST_SKIP() << "allocations are counted in Debug builds with MSVC's CRT";
    const SDLAllocationsUncounted sdlUncounted;
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

// Frames drawn back to back, none waited for (no present holds them either): each frame's work
// is kept until the GPU is done with it, so nothing is reused or freed while still in flight.
TEST_P(SDLGPUImageWindowTest, FramesNotWaitedForAreKeptUntilTheGPUIsDone)
{
    const tachyon::VisualColorStyleIndex white = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 255 }));
    for (int frame = 0; frame < 40; ++frame)
    {
        graphics->BeginFrame();
        graphics->PrepareFrame(styleManager, gradientManager, 0.0f);
        tachyon::GUIRenderer* r = graphics->guiRenderer;
        if (r->BeginWindow(window->GetID()))
        {
            for (int i = 0; i < 200; ++i)
                r->RenderQuad(tachyon::IntRect{ i % kSize.x, (i * 7) % kSize.y, 40, 40 }, kFullClip, white, 0.0f);
            r->EndWindow();
        }
        window->RenderWindowFrame(graphics, kFullClip, false);
    }
    graphics->DestroyWindowContext(window);
    window = nullptr;
    graphics->WaitIdle();
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
}

// A scene the app draws (RenderScene, a viewport's): its drawing goes into the scene's own
// texture before the UI's, which shows it where the UI puts it. Made again when its size
// changes, and let go when it's no longer asked for.
TEST_P(SDLGPUImageWindowTest, AnAppsSceneShowsWhereTheUIDrawsIt)
{
    const tachyon::VisualColorStyleIndex stretched = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(
        tachyon::color4b{ 255, 255, 255, 255 }, tachyon::color4b{ 255, 255, 255, 255 }, tachyon::ColorType::StretchedTexture));
    tachyon::ID key;
    key.value.dynamicParts.mode      = tachyon::IDMode::Static;
    key.value.dynamicParts.hashValue = 0x5CE4E;
    int recorded = 0;
    tachyon::uint2 lastExtent{};
    std::vector<SDL_GPUTransferBuffer*> uploads;
    SDL_GPUDevice* device = nullptr;

    // The scene clears to blue; the app fills its left half green, with an upload of its own.
    const auto frame = [&](tachyon::uint2 size) {
        graphics->BeginFrame();
        const tachyon::ImageIndex index = graphics->RenderScene(key, window->GetID(), size, tachyon::color4f{ 0, 0, 1, 1 },
            [&](const tachyon::ScenePayload& payload) {
                const auto* scene = payload.As<tachyon::sdlgpu::SDLGPUSceneRender>();
                ASSERT_NE(scene, nullptr) << "an SDL GPU scene";
                ++recorded;
                lastExtent = scene->extent;
                device     = scene->device;
                EXPECT_EQ(scene->colorFormat, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
                EXPECT_EQ(scene->depthFormat, SDL_GPU_TEXTUREFORMAT_D32_FLOAT);
                EXPECT_EQ(scene->colorTarget.texture, scene->colorTexture);
                EXPECT_EQ(scene->colorTarget.load_op, SDL_GPU_LOADOP_LOAD) << "cleared already: a pass of the app's keeps it";

                const uint32_t w = scene->extent.x / 2, h = scene->extent.y;
                const SDL_GPUTransferBufferCreateInfo info{ SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, w * h * 4, 0 };
                SDL_GPUTransferBuffer* upload = SDL_CreateGPUTransferBuffer(scene->device, &info);
                ASSERT_NE(upload, nullptr);
                uploads.push_back(upload);
                auto* green = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(scene->device, upload, false));
                for (uint32_t i = 0; i < w * h; ++i)
                    std::memcpy(green + i * 4, "\x00\xff\x00\xff", 4);   // RGBA
                SDL_UnmapGPUTransferBuffer(scene->device, upload);
                SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(scene->commandBuffer);
                SDL_GPUTextureTransferInfo from{};
                from.transfer_buffer = upload;
                SDL_GPUTextureRegion to{};
                to.texture = scene->colorTexture;
                to.w       = w;
                to.h       = h;
                to.d       = 1;
                SDL_UploadToGPUTexture(copy, &from, &to, false);
                SDL_EndGPUCopyPass(copy);
            });
        EXPECT_NE(index, tachyon::ImageIndex::Invalid);
        // The scene over the right half of the window.
        RenderFrame([&](tachyon::GUIRenderer* r) {
            r->RenderQuad(tachyon::IntRect{ kSize.x / 2, 0, kSize.x / 2, kSize.y }, kFullClip, stretched, 0.0f, index);
        });
        return index;
    };

    const tachyon::ImageIndex first = frame({ 64, 32 });
    EXPECT_EQ(recorded, 1) << "drawn once a frame";
    EXPECT_EQ(lastExtent, (tachyon::uint2{ 64, 32 }));
    const auto left = Rgba(kSize.x / 2 + 20, kSize.y / 2), right = Rgba(kSize.x - 20, kSize.y / 2);
    EXPECT_GT(left[1], 200) << "the app's green, in the scene's left half";
    EXPECT_LT(left[2], 50);
    EXPECT_GT(right[2], 200) << "the clear's blue in its right";
    EXPECT_LT(right[1], 50);
    EXPECT_LT(Rgba(20, kSize.y / 2)[1], 50) << "nothing where the UI didn't draw it";

    EXPECT_EQ(frame({ 64, 32 }), first) << "the same texture while the size stays";
    frame({ 80, 40 });
    EXPECT_EQ(lastExtent, (tachyon::uint2{ 80, 40 })) << "made again at the new size";
    EXPECT_GT(Rgba(kSize.x / 2 + 20, kSize.y / 2)[1], 200);

    // Not asked for, it goes after a few frames; asked for again, it's made again.
    for (int i = 0; i < 6; ++i)
    {
        graphics->BeginFrame();
        RenderFrame([](tachyon::GUIRenderer*) {});
    }
    const int before = recorded;
    frame({ 80, 40 });
    EXPECT_EQ(recorded, before + 1);
    EXPECT_GT(Rgba(kSize.x / 2 + 20, kSize.y / 2)[1], 200);

    // SDL frees them once the frames that copied from them are done.
    for (SDL_GPUTransferBuffer* upload : uploads)
        SDL_ReleaseGPUTransferBuffer(device, upload);
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
}

INSTANTIATE_TEST_SUITE_P(Drivers, SDLGPUImageWindowTest, ::testing::ValuesIn(kDrivers),
                         [](const ::testing::TestParamInfo<const char*>& info) { return std::string(info.param); });
