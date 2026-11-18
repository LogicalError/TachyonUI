#pragma once

// Real-device Direct3D 12 test harness. Brings up a D3D12GraphicsContext with the debug layer's
// messages recorded, so a test can assert that a code path makes none. Tests are SKIPPED when no
// GPU runs the backend (Shader Model 6.6 with resource binding tier 3).

#include <gui_core/graphics/d3d12/d3d12_graphics_context.h>
#include <gui_core/windows/sdl/gui_window_backend_sdl.h>

#include <gtest/gtest.h>

#include <mutex>
#include <string>
#include <vector>

namespace D3D12TestHarness
{
    // The debug layer's warnings and errors. It may call from other threads, hence the mutex.
    struct DebugSink
    {
        std::mutex               mutex;
        std::vector<std::string> messages;

        void Clear() { std::lock_guard lock(mutex); messages.clear(); }
        bool Empty() { std::lock_guard lock(mutex); return messages.empty(); }
        std::string Dump()
        {
            std::lock_guard lock(mutex);
            std::string out;
            for (const std::string& m : messages)
                out += "  " + m + "\n";
            return out;
        }

        static void Record(tachyon::d3d12::DebugSeverity severity, const char* message, void* user)
        {
            if (severity == tachyon::d3d12::DebugSeverity::Info)
                return;
            auto* sink = static_cast<DebugSink*>(user);
            std::lock_guard lock(sink->mutex);
            sink->messages.emplace_back(std::string(severity == tachyon::d3d12::DebugSeverity::Error ? "[error] " : "[warning] ") + message);
        }
    };

    class D3D12Test : public ::testing::Test
    {
    protected:
        tachyon::d3d12::D3D12GraphicsContext* graphics      = nullptr;
        tachyon::SDLWindowBackend*            windowBackend = nullptr;
        DebugSink                             sink;

        // The GPU preference (ChooseGpu) the device is made with; empty: the backend's choice.
        virtual std::string Gpu() const { return {}; }

        void SetUp() override
        {
            graphics      = new tachyon::d3d12::D3D12GraphicsContext();
            windowBackend = new tachyon::SDLWindowBackend();
            windowBackend->SetGraphicsContext(graphics);
            graphics->SetDebugConfig({ &DebugSink::Record, &sink });
            graphics->SetGpuPreference(Gpu());
            if (!graphics->Initialize(windowBackend))
            {
                TearDown();
                GTEST_SKIP() << "no GPU runs the Direct3D 12 backend";
            }
            ASSERT_TRUE(sink.Empty()) << "debug layer messages while starting:\n" << sink.Dump();
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
}
