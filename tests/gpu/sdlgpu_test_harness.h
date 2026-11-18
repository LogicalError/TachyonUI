#pragma once

// Real-device SDL GPU test harness. Brings up an SDLGPUGraphicsContext on one of SDL's drivers,
// with SDL GPU's debug messages recorded, so a test can assert that a code path makes none.
// Tests are SKIPPED when SDL finds no device for the driver.

#include <gui_core/graphics/gui_renderer.h>
#include <gui_core/graphics/sdlgpu/sdlgpu_graphics_context.h>
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style_manager.h>
#include <gui_core/windows/gui_window_flags.h>
#include <gui_core/windows/sdl/gui_window_backend_sdl.h>

#include <gtest/gtest.h>

#include <array>
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

    // A render-to-image window on the device, with the styles a frame draws with.
    class SDLGPUImageWindowTest : public SDLGPUTest
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
            SDLGPUTest::SetUp();
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
            SDLGPUTest::TearDown();
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

    // SDL's drivers on this platform.
#if defined(_WIN32)
    inline const char* const kDrivers[] = { "vulkan", "direct3d12" };
#elif defined(__APPLE__)
    inline const char* const kDrivers[] = { "metal" };
#else
    inline const char* const kDrivers[] = { "vulkan" };
#endif
}
