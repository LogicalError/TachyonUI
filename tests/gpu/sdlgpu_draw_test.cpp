// The SDL GPU backend draws the UI, on each of SDL's drivers: a window with a button, a label and
// a cached scope looks as it does on Vulkan, with nothing from SDL GPU's debug mode.

#include "gpu_draw_app.h"
#include "sdlgpu_test_harness.h"

#include <memory>

using namespace GpuDrawApp;
using namespace SDLGPUTestHarness;

namespace
{
    class SDLGPUDraw : public ::testing::TestWithParam<const char*> {};
}

TEST_P(SDLGPUDraw, TheUIDrawsAsOnVulkanWithNothingFromDebugMode)
{
    DebugSink sink;
    auto backend = std::make_unique<sdlgpu::SDLGPUGraphicsContext>();
    backend->SetDebugConfig({ &DebugSink::Record, &sink });
    backend->SetDriver(GetParam());

    DrawApp app;
    if (!app.Start(GraphicsAPI::SDLGPU, std::move(backend)))
    {
        app.Cleanup();
        GTEST_SKIP() << "no window system, or SDL finds no device for its " << GetParam() << " driver";
    }
    ASSERT_TRUE(app.Run());
    const Drawn drawn = Capture(app);
    app.Cleanup();
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
    ExpectDrawnAsOnVulkan(drawn);
}

TEST_P(SDLGPUDraw, ARedrawThatClosesItsWindowStopsThere)
{
    auto backend = std::make_unique<sdlgpu::SDLGPUGraphicsContext>();
    backend->SetDriver(GetParam());
    ExpectARedrawThatClosesItsWindowStopsThere(GraphicsAPI::SDLGPU, std::move(backend));
}

INSTANTIATE_TEST_SUITE_P(Drivers, SDLGPUDraw, ::testing::ValuesIn(kDrivers),
                         [](const ::testing::TestParamInfo<const char*>& info) { return std::string(info.param); });
