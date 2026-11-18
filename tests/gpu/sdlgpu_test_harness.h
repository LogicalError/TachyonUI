#pragma once

// Real-device SDL GPU test harness. Brings up an SDLGPUGraphicsContext on one of SDL's drivers,
// with SDL GPU's debug messages recorded, so a test can assert that a code path makes none.
// Tests are SKIPPED when SDL finds no device for the driver.

#include <gui_core/graphics/sdlgpu/sdlgpu_graphics_context.h>
#include <gui_core/windows/sdl/gui_window_backend_sdl.h>

#include <gtest/gtest.h>

#include <mutex>
#include <string>
#include <vector>

namespace SDLGPUTestHarness
{
    // SDL GPU's warnings and errors. SDL may log from other threads, hence the mutex.
    struct DebugSink
    {
        std::mutex               mutex;
        std::vector<std::string> messages;

        bool Empty() { std::lock_guard lock(mutex); return messages.empty(); }
        std::string Dump()
        {
            std::lock_guard lock(mutex);
            std::string out;
            for (const std::string& m : messages)
                out += "  " + m + "\n";
            return out;
        }

        static void Record(tachyon::sdlgpu::DebugSeverity severity, const char* message, void* user)
        {
            if (severity == tachyon::sdlgpu::DebugSeverity::Info)
                return;
            auto* sink = static_cast<DebugSink*>(user);
            std::lock_guard lock(sink->mutex);
            sink->messages.emplace_back(std::string(severity == tachyon::sdlgpu::DebugSeverity::Error ? "[error] " : "[warning] ") + message);
        }
    };

    // The fixture, on the driver its test parameter names.
    class SDLGPUTest : public ::testing::TestWithParam<const char*>
    {
    protected:
        tachyon::sdlgpu::SDLGPUGraphicsContext* graphics      = nullptr;
        tachyon::SDLWindowBackend*              windowBackend = nullptr;
        DebugSink                               sink;

        void SetUp() override
        {
            graphics      = new tachyon::sdlgpu::SDLGPUGraphicsContext();
            windowBackend = new tachyon::SDLWindowBackend();
            windowBackend->SetGraphicsContext(graphics);
            graphics->SetDebugConfig({ &DebugSink::Record, &sink });
            graphics->SetDriver(GetParam());
            if (!graphics->Initialize(windowBackend))
            {
                TearDown();
                GTEST_SKIP() << "SDL finds no device for its " << GetParam() << " driver";
            }
            ASSERT_TRUE(sink.Empty()) << "messages while starting:\n" << sink.Dump();
        }

        void TearDown() override
        {
            if (graphics != nullptr)
                graphics->Cleanup();
            delete graphics;
            graphics = nullptr;
            delete windowBackend;
            windowBackend = nullptr;
        }
    };

    // SDL's drivers on this platform.
#if defined(_WIN32)
    inline const char* const kDrivers[] = { "vulkan", "direct3d12" };
#elif defined(__APPLE__)
    inline const char* const kDrivers[] = { "metal" };
#else
    inline const char* const kDrivers[] = { "vulkan" };
#endif
}
