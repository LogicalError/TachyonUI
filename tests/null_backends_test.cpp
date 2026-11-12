// The full GUIApplication on the null graphics and window backends: no GPU and
// no OS windows; text, layout and input run as usual.
#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>
#include <gui_core/graphics/null/gui_null_graphics_context.h>
#include <gui_core/windows/null/gui_null_window_backend.h>
#include <gui_core/windows/gui_image_window_backend.h>
#include <gui_core/windows/gui_window_flags.h>

#include <functional>
#include <memory>

namespace
{
    // A render-to-image window with a button.
    class NullBackendApp : public tachyon::GUIApplicationInternals
    {
    public:
        tachyon::ID windowID = tachyon::ID::Invalid();
        tachyon::ID buttonID = tachyon::ID::Invalid();
        int     clicks = 0;
        int     frame  = 0;
        int     frames = 3;
        tachyon::int2 buttonCenter{ 0, 0 };   // after last frame's layout
        std::function<void(int)> script;
        tachyon::ElementFlags windowFlags = tachyon::ElementFlags::WindowRenderToImage;
        std::function<void()> inside;   // more of the window, after the button

        void OnGUI() override
        {
            if (script) script(frame);
            if (BeginWindow(windowID, "Null", theme.windowStyleRef, tachyon::LayoutOverride{}.Size(240, 120), windowFlags))
            {
                BeginVertical(tachyon::LayoutOverride{}.FlexGrow(1));
                Button(buttonID, "Click", [this] { ++clicks; });
                EndVertical();
                if (inside) inside();
            }
            EndWindow();
            if (++frame >= frames)
                applicationQuit = true;
        }

        bool Start()
        {
            SetGraphicsBackend(std::make_unique<tachyon::NullGraphicsContext>());
            SetWindowBackend(std::make_unique<tachyon::NullWindowBackend>());
            if (!Initialize())
                return false;
            // A frame each pass: the script counts them.
            SetRedrawMode(tachyon::RedrawMode::Always);
            windowID = idManager.CreateStaticID();
            buttonID = idManager.CreateStaticID();
            m_frame.OnPostLayout([this](tachyon::FrameContext&)
            {
                const tachyon::ElementRef ref = elementStorage.FindElementRefByID(buttonID);
                if (!ref.IsValid()) return;
                const tachyon::IntRect r = elementStorage.GetElementRectPtr(ref)->rect;
                buttonCenter = { r.x + r.w / 2, r.y + r.h / 2 };
            });
            return true;
        }
    };

    TEST(NullBackends, RunsTheApplicationWithoutAGPU)
    {
        NullBackendApp app;
        ASSERT_TRUE(app.Start());
        EXPECT_EQ(app.graphicsContext->GetAPI(), tachyon::GraphicsAPI::None);
        EXPECT_TRUE(app.Run());
        EXPECT_EQ(app.frame, app.frames);
        EXPECT_NE(app.windowManager->GetWindowHandle(app.windowID), nullptr) << "the window exists";
        EXPECT_GT(app.buttonCenter.x, 0) << "the button was laid out";
        app.Cleanup();
        EXPECT_EQ(app.graphicsContext, nullptr) << "Cleanup releases the backend";
    }

    TEST(NullBackends, InjectedClickReachesAButton)
    {
        NullBackendApp app;
        ASSERT_TRUE(app.Start());
        app.frames = 7;
        app.script = [&](int frame)
        {
            const tachyon::int2 center = app.buttonCenter;
            switch (frame)
            {
                case 2: app.InjectMouseMove(app.windowID, center); break;
                case 3: app.InjectMouseButton(app.windowID, center, tachyon::MouseButtons::Left, true); break;
                case 4: app.InjectMouseButton(app.windowID, center, tachyon::MouseButtons::Left, false); break;
                case 5: app.InjectMouseLeave(app.windowID); break;
                default: break;
            }
        };
        EXPECT_TRUE(app.Run());
        EXPECT_EQ(app.clicks, 1);
        app.Cleanup();
    }

    // A popup in a window that renders to an image: an OS window, unless the window
    // asks for its popups as images too (WindowImagePopups), theirs included.
    TEST(NullBackends, PopupsOfAnImageWindowAreImagesWhenAskedFor)
    {
        for (const bool asked : { false, true })
        {
            NullBackendApp app;
            ASSERT_TRUE(app.Start());
            if (asked)
                app.windowFlags |= tachyon::ElementFlags::WindowImagePopups;
            const tachyon::ID popupID = app.idManager.CreateStaticID();
            const tachyon::ID nestedID = app.idManager.CreateStaticID();
            const tachyon::ElementFlags popup = tachyon::ElementFlags::WindowNoCaption | tachyon::ElementFlags::WindowIsPopup;
            app.inside = [&]
            {
                if (app.BeginWindow(popupID, "Popup", app.theme.windowStyleRef, tachyon::LayoutOverride{}.Size(80, 40), popup))
                {
                    if (app.BeginWindow(nestedID, "Nested", app.theme.windowStyleRef, tachyon::LayoutOverride{}.Size(60, 30), popup))
                        app.Text("nested");
                    app.EndWindow();
                }
                app.EndWindow();
            };
            EXPECT_TRUE(app.Run());
            auto* images = static_cast<tachyon::ImageWindowBackend*>(app.windowBackend);
            const tachyon::OSWindowHandle popupWindow = app.windowManager->GetWindowHandle(popupID);
            const tachyon::OSWindowHandle nestedWindow = app.windowManager->GetWindowHandle(nestedID);
            ASSERT_NE(popupWindow, nullptr);
            ASSERT_NE(nestedWindow, nullptr);
            EXPECT_EQ(images->IsImageWindow(popupWindow), asked);
            EXPECT_EQ(images->IsImageWindow(nestedWindow), asked) << "a popup's popup too";
            app.Cleanup();
        }
    }

    // The host's window backend and clipboard are the ones the app uses.
    TEST(NullBackends, UsesTheHostsWindowBackendAndClipboard)
    {
        NullBackendApp app;
        auto clipboard = std::make_unique<tachyon::LocalClipboard>();
        tachyon::IClipboard* hostClipboard = clipboard.get();
        app.SetClipboard(std::move(clipboard));
        ASSERT_TRUE(app.Start());
        EXPECT_EQ(app.clipboard.get(), hostClipboard);

        app.clipboard->Set("pasted");
        EXPECT_EQ(app.clipboard->Get(), "pasted");

        // A window that isn't render-to-image is one of the backend's windows.
        const tachyon::OSWindowHandle window = app.windowBackend->CreateWindow("os", tachyon::int2{ 300, 200 },
                                                                                  tachyon::WindowFlags::None, nullptr);
        ASSERT_NE(window, nullptr);
        EXPECT_EQ(app.windowBackend->GetWindowSize(window), (tachyon::int2{ 300, 200 }));
        EXPECT_TRUE(app.windowBackend->DestroyWindow(window));
        app.Cleanup();
        EXPECT_EQ(app.clipboard, nullptr) << "Cleanup releases the clipboard";
    }
}
