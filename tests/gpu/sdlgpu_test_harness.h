#pragma once

// Real-device SDL GPU test harness. Brings up an SDLGPUGraphicsContext on one of SDL's drivers,
// with SDL GPU's debug messages recorded, so a test can assert that a code path makes none.
// Tests are SKIPPED when SDL finds no device for the driver.

#include <gui_core/graphics/sdlgpu/sdlgpu_graphics_context.h>
#include <gui_core/windows/sdl/gui_window_backend_sdl.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace SDLGPUTestHarness
{
    // SDL GPU's warnings and errors. SDL may log from other threads, hence the mutex.
    // SDL's Vulkan driver turns the validation layer on without a messenger of its own, so the
    // layer would only print to stdout: the sink sends the layer's log to a file of its own and
    // counts what lands there too. Make the sink before the device, as the layer reads where to
    // log when its instance starts.
    struct DebugSink
    {
        std::mutex               mutex;
        std::vector<std::string> messages;
        std::filesystem::path    layerLog;

        DebugSink()
        {
            static std::atomic<uint32_t> next{ 0 };
            layerLog = std::filesystem::temp_directory_path() /
                       std::format("tachyon_sdlgpu_layer_{}_{}.log", CurrentProcessId(), next++);
            std::error_code ignored;
            std::filesystem::remove(layerLog, ignored);
            SetEnv("VK_KHRONOS_VALIDATION_LOG_FILENAME", layerLog.string().c_str());
        }
        ~DebugSink()
        {
            SetEnv("VK_KHRONOS_VALIDATION_LOG_FILENAME", "");
            std::error_code ignored;
            std::filesystem::remove(layerLog, ignored);
        }
        DebugSink(const DebugSink&)            = delete;
        DebugSink& operator=(const DebugSink&) = delete;

        bool Empty() { return All().empty(); }
        std::string Dump()
        {
            std::string out;
            for (const std::string& m : All())
                out += "  " + m + "\n";
            return out;
        }

        // SDL's messages, then each of the layer's: a message starts with "Validation" and runs
        // to the next one.
        std::vector<std::string> All()
        {
            std::vector<std::string> all;
            {
                std::lock_guard lock(mutex);
                all = messages;
            }
            std::ifstream file(layerLog);
            for (std::string line; std::getline(file, line);)
            {
                if (line.starts_with("Validation"))
                    all.push_back("[layer] " + line);
                else if (!line.empty() && !all.empty() && all.back().starts_with("[layer] "))
                    all.back() += " " + line;
            }
            return all;
        }

        static uint32_t CurrentProcessId()
        {
#if defined(_WIN32)
            return static_cast<uint32_t>(_getpid());
#else
            return static_cast<uint32_t>(getpid());
#endif
        }
        static void SetEnv(const char* name, const char* value)
        {
#if defined(_WIN32)
            _putenv_s(name, value);
#else
            if (value[0] == '\0')
                unsetenv(name);
            else
                setenv(name, value, 1);
#endif
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
