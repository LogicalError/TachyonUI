// An invalid control on a real device: its border is drawn in the error color over its own,
// with a halo of the error color around it; a valid one beside it has neither.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>

#include <string>
#include <vector>

namespace
{
    using namespace tachyon;

    class InvalidApp : public GUIApplicationInternals
    {
    public:
        ID windowID = ID::Invalid(), validID = ID::Invalid(), invalidID = ID::Invalid();
        IntRect validRect{}, invalidRect{}, windowRect{};
        std::string validText = "valid", invalidText = "invalid";
        int frame = 0;

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Invalid", theme.windowStyleRef, LayoutOverride{}.Size(360, 100),
                            ElementFlags::WindowRenderToImage))
            {
                BeginHorizontal(LayoutOverride{}.FlexGrow(1));
                BeginHorizontal(LayoutOverride{}.Width(20));   // room for the halo on the left
                EndHorizontal();
                Field(validID, validText, TextEditBehavior::None, LayoutOverride{}.Size(120, 26));
                BeginHorizontal(LayoutOverride{}.Width(40));
                EndHorizontal();
                BeginInvalid();
                Field(invalidID, invalidText, TextEditBehavior::None, LayoutOverride{}.Size(120, 26));
                EndInvalid();
                EndHorizontal();
            }
            EndWindow();
            if (++frame >= 4)
                applicationQuit = true;
        }

        bool Start()
        {
            if (!Initialize())
                return false;
            windowID  = idManager.CreateStaticID();
            validID   = idManager.CreateStaticID();
            invalidID = idManager.CreateStaticID();
            m_frame.OnPostLayout([this](FrameContext&)
            {
                auto rectOf = [this](ID id) {
                    const ElementRef ref = elementStorage.FindElementRefByID(id);
                    return ref.IsValid() ? elementStorage.GetElementRectPtr(ref)->rect : IntRect{};
                };
                validRect   = rectOf(validID);
                invalidRect = rectOf(invalidID);
                windowRect  = rectOf(windowID);
            });
            return true;
        }
    };

    struct Rgb { int r, g, b; };

    TEST(GpuInvalid, AnInvalidControlHasTheErrorBorderAndHalo)
    {
        InvalidApp app;
        if (!app.Start())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        ASSERT_TRUE(app.Run());

        const IntRect rect = app.windowManager->GetWindowRect(app.windowID);
        std::vector<uint8_t> pixels(size_t(rect.w) * rect.h * 4);
        ASSERT_TRUE(app.ReadWindowPixels(app.windowID, pixels));
        auto at = [&](int x, int y) {   // layout position -> window pixel, BGRA
            x -= app.windowRect.x; y -= app.windowRect.y;
            const size_t o = (size_t(y) * rect.w + size_t(x)) * 4;
            return Rgb{ pixels[o + 2], pixels[o + 1], pixels[o] };
        };
        ASSERT_GT(app.invalidRect.w, 0);
        const color4b error = app.theme.invalidColor;
        const Rgb window = at(app.windowRect.x + app.windowRect.w - 4, app.windowRect.y + app.windowRect.h - 4);

        // The left border, half way down.
        const int midInvalid = app.invalidRect.y + app.invalidRect.h / 2;
        const Rgb border = at(app.invalidRect.x, midInvalid);
        EXPECT_NEAR(border.r, error.r, 12);
        EXPECT_NEAR(border.g, error.g, 12);
        EXPECT_NEAR(border.b, error.b, 12);
        // The halo, two pixels out: the window tinted towards the error color.
        const Rgb halo = at(app.invalidRect.x - 2, midInvalid);
        EXPECT_GT(halo.r, window.r + 4);
        EXPECT_LT(halo.r, error.r);
        // Nothing four pixels out, past the 3px halo.
        const Rgb past = at(app.invalidRect.x - 4, midInvalid);
        EXPECT_NEAR(past.r, window.r, 2);

        // The valid field: its own border, no halo.
        const int midValid = app.validRect.y + app.validRect.h / 2;
        const Rgb validBorder = at(app.validRect.x, midValid);
        EXPECT_GT(std::abs(validBorder.r - int(error.r)) + std::abs(validBorder.g - int(error.g)), 40);
        const Rgb validOutside = at(app.validRect.x - 2, midValid);
        EXPECT_NEAR(validOutside.r, window.r, 2);
        app.Cleanup();
    }
}
