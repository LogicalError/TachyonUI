// The SDL GPU backend on a real device, on each of SDL's drivers: it starts, and its windows
// present and read back, with nothing from SDL GPU's debug mode.

#include "sdlgpu_test_harness.h"

#include <gui_controls/gui_application_internal.h>
#include <gui_core/windows/gui_window_backend.h>

#include <vector>

using namespace SDLGPUTestHarness;

namespace
{
    // Whether every pixel of a BGRA8 frame is opaque black, as a frame with nothing drawn is.
    bool AllOpaqueBlack(const std::vector<uint8_t>& bgra)
    {
        for (size_t i = 0; i < bgra.size(); i += 4)
            if (bgra[i] != 0 || bgra[i + 1] != 0 || bgra[i + 2] != 0 || bgra[i + 3] != 255)
                return false;
        return true;
    }
}

TEST_P(SDLGPUTest, TheDeviceStartsOnTheDriverAskedFor)
{
    EXPECT_EQ(graphics->GetAPI(), tachyon::GraphicsAPI::SDLGPU);
    EXPECT_EQ(graphics->GetDriver(), GetParam());
    EXPECT_FALSE(graphics->GetDeviceName().empty());
    graphics->WaitIdle();
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
}

// An OS window is claimed, records, presents and reads back its frame, at its size and after
// a resize.
TEST_P(SDLGPUTest, AWindowPresentsAndReadsBackItsFrame)
{
    const tachyon::OSWindowHandle window =
        windowBackend->CreateWindow("sdlgpu-test", tachyon::IntRect{ -3000, -3000, 160, 90 }, tachyon::WindowFlags::None, nullptr);
    ASSERT_NE(window, nullptr);
    tachyon::IWindowContext* context = graphics->CreateWindowContext(windowBackend->GetNativeHandle(window), window,
                                                                     tachyon::ID{}, { 160, 90 }, tachyon::WindowFlags::None, {});
    ASSERT_NE(context, nullptr);

    for (const tachyon::int2 size : { tachyon::int2{ 160, 90 }, tachyon::int2{ 160, 90 }, tachyon::int2{ 200, 120 } })
    {
        context->RenderWindowFrame(graphics, tachyon::IntRect{ 0, 0, size.x, size.y }, false);
        std::vector<uint8_t> pixels(size_t(size.x) * size.y * 4);
        ASSERT_TRUE(context->ReadPixels(pixels)) << size.x << "x" << size.y;
        EXPECT_TRUE(AllOpaqueBlack(pixels)) << size.x << "x" << size.y;
    }
    std::vector<uint8_t> small(4);
    EXPECT_FALSE(context->ReadPixels(small)) << "a span smaller than the frame";

    graphics->DestroyWindowContext(context);
    windowBackend->DestroyWindow(window);
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
}

INSTANTIATE_TEST_SUITE_P(Drivers, SDLGPUTest, ::testing::ValuesIn(kDrivers),
                         [](const ::testing::TestParamInfo<const char*>& info) { return std::string(info.param); });

// An app that asks for SDL GPU renders with it.
TEST(SDLGPUApplication, AnAppThatAsksForItRendersWithIt)
{
    class App : public tachyon::GUIApplicationInternals
    {
    public:
        tachyon::ID windowID = tachyon::ID::Invalid();
        int         frame    = 0;

        void OnGUI() override
        {
            if (BeginWindow(windowID, "SDL GPU", theme.windowStyleRef, tachyon::LayoutOverride{}.Size(240, 120)))
                Text(idManager.AllocateDynamicID("text"), "On SDL GPU");
            EndWindow();
            if (++frame >= 4)
                applicationQuit = true;
        }
    };

    App app;
    tachyon::GUIApplication::InitializeOptions options;
    options.graphicsAPI = tachyon::GraphicsAPI::SDLGPU;
    if (!app.Initialize(options))
    {
        app.Cleanup();
        GTEST_SKIP() << "no window system";
    }
    // Read before Cleanup, which frees the context.
    if (const tachyon::GraphicsAPI api = app.graphicsContext->GetAPI(); api != tachyon::GraphicsAPI::SDLGPU)
    {
        app.Cleanup();
        GTEST_SKIP() << "SDL finds no GPU device: the app fell back to " << tachyon::GraphicsAPIName(api);
    }
    app.windowID = app.idManager.CreateStaticID();
    app.SetRedrawMode(tachyon::RedrawMode::Always);
    EXPECT_TRUE(app.Run());
    EXPECT_EQ(app.frame, 4);
    app.Cleanup();
}
