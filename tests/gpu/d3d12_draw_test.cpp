// The Direct3D 12 backend draws the UI: a window with a button, a label and a cached scope looks
// as it does on Vulkan, with nothing from the debug layer.

#include "d3d12_test_harness.h"
#include "gpu_draw_app.h"

#include <memory>

using namespace D3D12TestHarness;
using namespace GpuDrawApp;

TEST(D3D12Draw, TheUIDrawsAsOnVulkanWithNothingFromTheDebugLayer)
{
    DebugSink sink;
    auto backend = std::make_unique<d3d12::D3D12GraphicsContext>();
    backend->SetDebugConfig({ &DebugSink::Record, &sink });

    DrawApp app;
    if (!app.Start(GraphicsAPI::D3D12, std::move(backend)))
    {
        app.Cleanup();
        GTEST_SKIP() << "no window system, or no GPU runs the Direct3D 12 backend";
    }
    ASSERT_TRUE(app.Run());
    const Drawn drawn = Capture(app);
    app.Cleanup();
    EXPECT_TRUE(sink.Empty()) << sink.Dump();
    ExpectDrawnAsOnVulkan(drawn);
}

TEST(D3D12Draw, ARedrawThatClosesItsWindowStopsThere)
{
    ExpectARedrawThatClosesItsWindowStopsThere(GraphicsAPI::D3D12);
}
