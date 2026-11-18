#pragma once

// Real-device Direct3D 12 test harness. Brings up a D3D12GraphicsContext with the debug layer's
// messages recorded, so a test can assert that a code path makes none. Tests are SKIPPED when no
// GPU runs the backend (Shader Model 6.6 with resource binding tier 3).

#include <gui_core/graphics/d3d12/d3d12_graphics_context.h>
#include <gui_core/graphics/gui_renderer.h>
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style_manager.h>
#include <gui_core/windows/gui_window_flags.h>
#include <gui_core/windows/sdl/gui_window_backend_sdl.h>

#include <gtest/gtest.h>

#include <array>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace D3D12TestHarness
{
    // The debug layer's warnings and errors. It may call from other threads, hence the mutex.
    struct DebugSink
    {
        std::mutex               mutex;
        std::vector<std::string> messages;
        /// Messages containing one of these aren't recorded: for a test whose own Direct3D
        /// calls (an app's, not the backend's) draw a known message.
        std::vector<std::string> ignored;

        void Clear() { std::lock_guard lock(mutex); messages.clear(); }
        void Ignore(std::string text) { std::lock_guard lock(mutex); ignored.push_back(std::move(text)); }
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
            for (const std::string& ignore : sink->ignored)
                if (std::string_view(message).find(ignore) != std::string_view::npos)
                    return;
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
        // Sets the backend up before its Initialize (a host's device); false skips the test.
        virtual bool BeforeInitialize() { return true; }

        void SetUp() override
        {
            graphics      = new tachyon::d3d12::D3D12GraphicsContext();
            windowBackend = new tachyon::SDLWindowBackend();
            windowBackend->SetGraphicsContext(graphics);
            graphics->SetDebugConfig({ &DebugSink::Record, &sink });
            graphics->SetGpuPreference(Gpu());
            if (!BeforeInitialize())
            {
                TearDown();
                GTEST_SKIP() << "no GPU runs the Direct3D 12 backend";
            }
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

    // A render-to-image window on the device, with the styles a frame draws with.
    class D3D12ImageWindowTest : public D3D12Test
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
            D3D12Test::SetUp();
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
            D3D12Test::TearDown();
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
}
