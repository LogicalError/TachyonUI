// The Direct3D 12 backend on a real device: it starts, and its windows present and read back,
// with nothing from the debug layer.

#include "d3d12_test_harness.h"

#include <gui_controls/gui_application_internal.h>
#include <gui_core/windows/gui_window_backend.h>

#include <algorithm>
#include <memory>
#include <vector>

using namespace D3D12TestHarness;

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

TEST_F(D3D12Test, TheDeviceStartsWithNothingFromTheDebugLayer)
{
    EXPECT_EQ(graphics->GetAPI(), tachyon::GraphicsAPI::D3D12);
    EXPECT_FALSE(graphics->GetDeviceName().empty());
    graphics->WaitIdle();
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
}

// An OS window records, presents and reads back its frame, at its size and after a resize.
TEST_F(D3D12Test, AWindowPresentsAndReadsBackItsFrame)
{
    const tachyon::OSWindowHandle window =
        windowBackend->CreateWindow("d3d12-test", tachyon::IntRect{ -3000, -3000, 160, 90 }, tachyon::WindowFlags::None, nullptr);
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

// The "cpu" preference picks WARP, Windows' software renderer, which CI runs the tests on.
class D3D12WarpTest : public D3D12Test
{
protected:
    std::string Gpu() const override { return "cpu"; }
};

TEST_F(D3D12WarpTest, TheCpuPreferencePicksWarp)
{
    const std::string name(graphics->GetDeviceName());
    EXPECT_NE(name.find("Basic Render"), std::string::npos) << name;
}

// An app that asks for Direct3D 12 renders with it.
TEST(D3D12Application, AnAppThatAsksForItRendersWithIt)
{
    class App : public tachyon::GUIApplicationInternals
    {
    public:
        tachyon::ID windowID = tachyon::ID::Invalid();
        int         frame    = 0;

        void OnGUI() override
        {
            if (BeginWindow(windowID, "D3D12", theme.windowStyleRef, tachyon::LayoutOverride{}.Size(240, 120)))
                Text(idManager.AllocateDynamicID("text"), "On Direct3D 12");
            EndWindow();
            if (++frame >= 4)
                applicationQuit = true;
        }
    };

    App app;
    tachyon::GUIApplication::InitializeOptions options;
    options.graphicsAPI = tachyon::GraphicsAPI::D3D12;
    if (!app.Initialize(options))
    {
        app.Cleanup();
        GTEST_SKIP() << "no window system";
    }
    if (app.graphicsContext->GetAPI() != tachyon::GraphicsAPI::D3D12)
    {
        app.Cleanup();
        GTEST_SKIP() << "no GPU runs the Direct3D 12 backend: the app fell back to "
                     << tachyon::GraphicsAPIName(app.graphicsContext->GetAPI());
    }
    app.windowID = app.idManager.CreateStaticID();
    app.SetRedrawMode(tachyon::RedrawMode::Always);
    EXPECT_TRUE(app.Run());
    EXPECT_EQ(app.frame, 4);
    app.Cleanup();
}
