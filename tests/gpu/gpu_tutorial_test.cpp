// The tutorial's finished app (examples/tutorial, docs/tutorial/app.md) starts on a real
// device with no resource files, builds its docked panels, and runs.

#include "gpu_device.h"

#include <gtest/gtest.h>

#include "../../examples/tutorial/my_app.h"

namespace
{
    // The tutorial's app, closing itself after a few frames.
    class ClosingApp : public my_app::MyApp
    {
    public:
        int frames = 0;

        void OnGUI() override
        {
            MyApp::OnGUI();
            if (++frames >= 5)
                applicationQuit = true;
        }
    };

    // docs/tutorial/app.md: Initialize sets up the panels, and Run draws them.
    TEST(GpuTutorial, TheFinishedAppRuns)
    {
        if (!GpuDeviceAvailable())
            GTEST_SKIP() << "no GPU device available";

        ClosingApp app;
        tachyon::Result<void> result = app.Initialize();
        ASSERT_TRUE(result) << result.GetError().message;

        app.SetRedrawMode(tachyon::RedrawMode::Always);   // a frame each pass: the test counts them
        result = app.Run();
        EXPECT_TRUE(result) << result.GetError().message;
        EXPECT_GE(app.frames, 5);
        app.Cleanup();
    }
}
