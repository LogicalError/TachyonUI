// Render-to-image windows and the app's scenes on Direct3D 12: a render-to-image window draws
// and reads back, and a scene's drawing shows where the UI puts it, with nothing from the debug
// layer.

#include "d3d12_test_harness.h"

#include <gui_core/graphics/gui_renderer.h>
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style_manager.h>
#include <gui_core/windows/gui_window_flags.h>

#include <d3d12.h>

#include <array>
#include <cstring>
#include <vector>

using namespace D3D12TestHarness;

namespace
{
    class D3D12ImageWindowTest : public D3D12Test
    {
    protected:
        static constexpr tachyon::int2    kSize{ 320, 240 };
        static constexpr tachyon::IntRect kFullClip{ 0, 0, kSize.x, kSize.y };

        tachyon::GradientManager gradientManager{ 256 };
        tachyon::StyleManager    styleManager{ gradientManager };
        tachyon::IWindowContext* window = nullptr;
        std::vector<uint8_t>     pixels = std::vector<uint8_t>(size_t(kSize.x) * kSize.y * 4);

        void SetUp() override
        {
            D3D12Test::SetUp();
            if (IsSkipped() || HasFatalFailure())
                return;
            // With no styles PrepareFrame would have nothing to upload.
            styleManager.Register(tachyon::Style{});
            tachyon::ID id;
            id.value.dynamicParts.mode      = tachyon::IDMode::Static;
            id.value.dynamicParts.hashValue = 0x1A6E;
            window = graphics->CreateWindowContext(nullptr, nullptr, id, kSize, tachyon::WindowFlags::RenderToImage, {});
            ASSERT_NE(window, nullptr);
        }

        void TearDown() override
        {
            if (window != nullptr)
                graphics->DestroyWindowContext(window);
            window = nullptr;
            if (graphics != nullptr)
                gradientManager.ReleaseAll(graphics);
            D3D12Test::TearDown();
        }

        // One frame of what `emit` draws, read back into `pixels`.
        template <class Emit>
        void RenderFrame(Emit&& emit)
        {
            graphics->PrepareFrame(styleManager, gradientManager, 0.0f);
            tachyon::GUIRenderer* r = graphics->guiRenderer;
            if (r->BeginWindow(window->GetID()))
            {
                emit(r);
                r->EndWindow();
            }
            window->RenderWindowFrame(graphics, kFullClip, false);
            ASSERT_TRUE(window->ReadPixels(pixels));
        }

        std::array<int, 4> Rgba(int x, int y) const   // the frame is BGRA8
        {
            const uint8_t* p = pixels.data() + (size_t(y) * kSize.x + size_t(x)) * 4;
            return { p[2], p[1], p[0], p[3] };
        }
    };
}

// A render-to-image window has no OS window: it draws, and what it draws is read back, the
// rest left transparent.
TEST_F(D3D12ImageWindowTest, ARenderToImageWindowDrawsAndReadsBack)
{
    const tachyon::VisualColorStyleIndex white = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(tachyon::color4b{ 255, 255, 255, 255 }));
    graphics->BeginFrame();
    RenderFrame([&](tachyon::GUIRenderer* r) { r->RenderQuad(tachyon::IntRect{ 0, 0, kSize.x / 2, kSize.y }, kFullClip, white, 0.0f); });
    EXPECT_EQ(Rgba(kSize.x / 4, kSize.y / 2), (std::array<int, 4>{ 255, 255, 255, 255 })) << "the quad";
    EXPECT_EQ(Rgba(kSize.x * 3 / 4, kSize.y / 2), (std::array<int, 4>{ 0, 0, 0, 0 })) << "transparent where nothing is drawn";
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
}

// A scene the app draws (RenderScene, a viewport's): its drawing goes into the scene's own
// texture before the UI's, which shows it where the UI puts it. Made again when its size
// changes, and let go when it's no longer asked for.
TEST_F(D3D12ImageWindowTest, AnAppsSceneShowsWhereTheUIDrawsIt)
{
    const tachyon::VisualColorStyleIndex stretched = styleManager.RegisterColorStyle(tachyon::ColorStyle::Create(
        tachyon::color4b{ 255, 255, 255, 255 }, tachyon::color4b{ 255, 255, 255, 255 }, tachyon::ColorType::StretchedTexture));
    tachyon::ID key;
    key.value.dynamicParts.mode      = tachyon::IDMode::Static;
    key.value.dynamicParts.hashValue = 0x5CE4E;
    int recorded = 0;
    tachyon::uint2 lastExtent{};
    // The app's own clear of half the scene to green isn't the scene's optimized clear (blue):
    // a performance hint about the app's call, not the backend's.
    sink.Ignore("The clear values do not match those passed to resource creation");

    // The scene clears to blue; the app fills its left half green.
    const auto frame = [&](tachyon::uint2 size) {
        graphics->BeginFrame();
        const tachyon::ImageIndex index = graphics->RenderScene(key, window->GetID(), size, tachyon::color4f{ 0, 0, 1, 1 },
            [&](const tachyon::ScenePayload& payload) {
                const auto* scene = payload.As<tachyon::d3d12::D3D12SceneRender>();
                ASSERT_NE(scene, nullptr) << "a Direct3D 12 scene";
                ++recorded;
                lastExtent = scene->extent;
                EXPECT_EQ(scene->colorFormat, DXGI_FORMAT_R8G8B8A8_UNORM);
                EXPECT_EQ(scene->depthFormat, DXGI_FORMAT_D32_FLOAT);
                const float green[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
                const D3D12_RECT half{ 0, 0, LONG(scene->extent.x / 2), LONG(scene->extent.y) };
                scene->commandList->ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE{ scene->renderTargetView }, green, 1, &half);
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
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
}
