// Render-to-image windows and the app's scenes on Direct3D 12: a render-to-image window draws
// and reads back, and a scene's drawing shows where the UI puts it, with nothing from the debug
// layer.

#include "d3d12_test_harness.h"

#include <d3d12.h>

using namespace D3D12TestHarness;

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
