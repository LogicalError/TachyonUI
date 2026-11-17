// A view's own state as text, for a panel to keep with its layout (Panel::saveState): a node
// view's pan and zoom, the part of a timeline or curve plot in view, a viewport's camera, the
// settings page open. Each comes back as it was; text that can't be read changes nothing.

#include <gtest/gtest.h>

#include <gui_controls/gui_application.h>
#include <gui_controls/gui_view_state.h>

namespace tachyon
{
    namespace
    {
        TEST(ViewState, ANodeViewsPanAndZoomComeBack)
        {
            NodeView view;
            view.transform.r0 = float3{ 1.5f, 0.0f, 120.0f };
            view.transform.r1 = float3{ 0.0f, 1.5f, -40.0f };
            const std::string saved = SaveViewState(view);

            NodeView again;
            ASSERT_TRUE(RestoreViewState(again, saved));
            EXPECT_EQ(again.transform, view.transform);
        }

        TEST(ViewState, ATimelinesViewComesBack)
        {
            TimelineView view;
            view.viewStart  = 2.5f;
            view.viewLength = 4.0f;
            view.zoom       = 60;
            TimelineView again;
            ASSERT_TRUE(RestoreViewState(again, SaveViewState(view)));
            EXPECT_EQ(again.viewStart, 2.5f);
            EXPECT_EQ(again.viewLength, 4.0f);
            EXPECT_EQ(again.zoom, 60);
        }

        TEST(ViewState, ACurvePlotsShownPartComesBack)
        {
            CurveView view;
            view.shown    = CurveRange{ 0.25f, 0.75f, -1.0f, 2.0f };
            view.shownSet = true;
            CurveView again;
            ASSERT_TRUE(RestoreViewState(again, SaveViewState(view)));
            EXPECT_TRUE(again.shownSet);
            EXPECT_EQ(again.shown, view.shown);

            CurveView never;   // never opened: nothing in view yet, and nothing to keep
            EXPECT_EQ(SaveViewState(never), "");
        }

        TEST(ViewState, AViewportsCameraComesBack)
        {
            ViewportCamera camera;
            camera.target       = float3{ 1.0f, 2.0f, 3.0f };
            camera.distance     = 12.0f;
            camera.yaw          = -0.3f;
            camera.pitch        = -0.8f;
            camera.fovY         = 1.0f;
            camera.orthographic = true;
            ViewportCamera again;
            ASSERT_TRUE(RestoreViewState(again, SaveViewState(camera)));
            EXPECT_EQ(again.target, camera.target);
            EXPECT_EQ(again.distance, 12.0f);
            EXPECT_EQ(again.yaw, -0.3f);
            EXPECT_EQ(again.pitch, -0.8f);
            EXPECT_EQ(again.fovY, 1.0f);
            EXPECT_TRUE(again.orthographic);
        }

        // The page open, by its name: IDs change between runs, names don't. A page that's
        // gone keeps the one open.
        TEST(ViewState, TheSettingsPageOpenComesBackByName)
        {
            GUIApplication::SettingsPanelState settings;
            const ID general  { .value{ .integer = 11 } };
            const ID advanced { .value{ .integer = 12 } };
            settings.AddPage(general, "General", [] {});
            settings.AddPage(advanced, "Advanced", [] {});
            settings.selectedID = advanced;
            const std::string saved = SaveViewState(settings);

            GUIApplication::SettingsPanelState again;
            const ID generalAgain  { .value{ .integer = 21 } };
            const ID advancedAgain { .value{ .integer = 22 } };
            again.AddPage(generalAgain, "General", [] {});
            again.AddPage(advancedAgain, "Advanced", [] {});
            ASSERT_TRUE(RestoreViewState(again, saved));
            EXPECT_EQ(again.selectedID, advancedAgain);

            GUIApplication::SettingsPanelState fewer;
            fewer.AddPage(generalAgain, "General", [] {});
            EXPECT_FALSE(RestoreViewState(fewer, saved));
            EXPECT_EQ(fewer.selectedID, generalAgain);
        }

        TEST(ViewState, TextItCantReadChangesNothing)
        {
            NodeView view;
            view.transform.r0 = float3{ 2.0f, 0.0f, 5.0f };
            const float3x2 before = view.transform;
            for (std::string_view text : { "", "not json", "{}", R"({"transform":[1,2]})", R"({"transform":"x"})" })
            {
                EXPECT_FALSE(RestoreViewState(view, text)) << text;
                EXPECT_EQ(view.transform, before) << text;
            }
            ViewportCamera camera;
            EXPECT_FALSE(RestoreViewState(camera, R"({"distance":-5})")) << "a camera behind its target";
            EXPECT_EQ(camera.distance, ViewportCamera{}.distance);
        }
    }
}
