// The SDL GPU backend on a host's own device, on each of SDL's drivers: the GUI renders on it
// and leaves it to the host, shows the host's textures, and copies a render-to-image window's
// frames into a texture of the host's, with nothing from SDL GPU's debug mode.

#include "sdlgpu_test_harness.h"

#include <string>

using namespace SDLGPUTestHarness;

// The GUI renders on the host's device, and Cleanup leaves it running for the host.
TEST_P(SDLGPUHostDeviceTest, TheGUIRendersOnTheHostsDeviceAndLeavesItToTheHost)
{
    std::string problems;
    EXPECT_TRUE(tachyon::sdlgpu::SDLGPUGraphicsContext::CheckDeviceSupport(hostDevice, &problems)) << problems;
    EXPECT_EQ(graphics->GetDriver(), GetParam());

    const tachyon::VisualColorStyleIndex white = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 255 }));
    graphics->BeginFrame();
    RenderFrame([&](tachyon::GUIRenderer* r) { r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x / 2, kSize.y }, kFullClip, white, 0.0f); });
    EXPECT_EQ(Rgba(kSize.x / 4, kSize.y / 2), (std::array<int, 4>{ 255, 255, 255, 255 }));

    graphics->DestroyWindowContext(window);
    window = nullptr;
    gradientManager.ReleaseAll(graphics);
    graphics->Cleanup();
    // Still the host's to use.
    SDL_GPUTexture* texture = HostTexture({ 4, 4 }, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_FColor{ 1, 0, 0, 1 });
    ASSERT_NE(texture, nullptr) << "the device still works after the GUI's Cleanup";
    SDL_ReleaseGPUTexture(hostDevice, texture);
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
}

// A device that isn't there is no device to render on: the check says so.
TEST(SDLGPUHostDevice, ANullDeviceIsNotSupported)
{
    std::string problems;
    EXPECT_FALSE(tachyon::sdlgpu::SDLGPUGraphicsContext::CheckDeviceSupport(nullptr, &problems));
    EXPECT_FALSE(problems.empty());
}

// A texture the host renders shows where the UI draws it, and again after it's replaced.
TEST_P(SDLGPUHostDeviceTest, AHostRenderedTextureShowsAndCanBeReplaced)
{
    const tachyon::VisualColorStyleIndex stretched = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(
        tachyon::color4b{ 255, 255, 255, 255 }, tachyon::color4b{ 255, 255, 255, 255 }, tachyon::ColorType::StretchedTexture));
    SDL_GPUTexture* red   = HostTexture({ 16, 16 }, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_FColor{ 1, 0, 0, 1 });
    SDL_GPUTexture* green = HostTexture({ 32, 8 },  SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_FColor{ 0, 1, 0, 1 });
    ASSERT_NE(red, nullptr);
    ASSERT_NE(green, nullptr);

    graphics->BeginFrame();
    const tachyon::ImageIndex index = graphics->RegisterExternalImage(red, tachyon::Sampler::LinearClampToEdge);
    ASSERT_NE(index, tachyon::ImageIndex::Invalid);
    const auto quad = [&](tachyon::GUIRenderer* r) { r->RenderQuad(tachyon::IntRect{ 40, 40, 100, 100 }, kFullClip, stretched, 0.0f, index); };
    RenderFrame(quad);
    EXPECT_EQ(Rgba(90, 90), (std::array<int, 4>{ 255, 0, 0, 255 })) << "the host's red";
    EXPECT_EQ(Rgba(200, 90), (std::array<int, 4>{ 0, 0, 0, 0 })) << "nothing beside it";

    graphics->WaitIdle();   // no frame in flight samples the old one
    graphics->BeginFrame();
    graphics->UpdateExternalImage(index, green, tachyon::Sampler::LinearClampToEdge);
    RenderFrame(quad);
    EXPECT_EQ(Rgba(90, 90), (std::array<int, 4>{ 0, 255, 0, 255 })) << "the replacement's green";

    graphics->UnregisterImage(index);
    graphics->WaitIdle();
    SDL_ReleaseGPUTexture(hostDevice, red);
    SDL_ReleaseGPUTexture(hostDevice, green);
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
}

// Each frame of a render-to-image window is copied into the host's texture, which the host
// reads after the GUI's frame. Set to null, the copies stop.
TEST_P(SDLGPUHostDeviceTest, ARenderToImageWindowsFrameReachesTheHostsTexture)
{
    const tachyon::uint2 size{ uint32_t(kSize.x), uint32_t(kSize.y) };
    SDL_GPUTexture* target = HostTexture(size, SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, SDL_FColor{ 0, 0, 1, 1 });
    ASSERT_NE(target, nullptr);
    graphics->SetWindowImageTarget(window->GetID(), target, size);

    const tachyon::VisualColorStyleIndex white = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 255 }));
    graphics->BeginFrame();
    RenderFrame([&](tachyon::GUIRenderer* r) { r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x / 2, kSize.y }, kFullClip, white, 0.0f); });
    {
        const std::vector<uint8_t> host = Download(target, size);
        const auto at = [&](int x, int y) {
            const uint8_t* p = host.data() + (size_t(y) * kSize.x + size_t(x)) * 4;
            return std::array<int, 4>{ p[2], p[1], p[0], p[3] };
        };
        EXPECT_EQ(at(kSize.x / 4, kSize.y / 2), (std::array<int, 4>{ 255, 255, 255, 255 })) << "the window's white";
        EXPECT_EQ(at(kSize.x * 3 / 4, kSize.y / 2), (std::array<int, 4>{ 0, 0, 0, 0 })) << "transparent where it drew nothing";
    }

    // No more copies: the host's next clear stays.
    graphics->SetWindowImageTarget(window->GetID(), nullptr, {});
    SDL_ReleaseGPUTexture(hostDevice, target);
    target = HostTexture(size, SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, SDL_FColor{ 0, 0, 1, 1 });
    graphics->BeginFrame();
    RenderFrame([&](tachyon::GUIRenderer* r) { r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x / 2, kSize.y }, kFullClip, white, 0.0f); });
    const std::vector<uint8_t> host = Download(target, size);
    EXPECT_EQ(host[(size_t(kSize.y / 2) * kSize.x + size_t(kSize.x / 4)) * 4], 255) << "the host's blue, not copied over";
    SDL_ReleaseGPUTexture(hostDevice, target);
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
}

INSTANTIATE_TEST_SUITE_P(Drivers, SDLGPUHostDeviceTest, ::testing::ValuesIn(kDrivers),
                         [](const ::testing::TestParamInfo<const char*>& info) { return std::string(info.param); });
