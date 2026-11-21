// One timeline shown in two places: a TimelineData and a TimelineViewState for each editor of
// it (TimelineEditor({ data, state })). Each editor zooms and selects on its own; an edit made
// in one shows in both.

#include <gtest/gtest.h>

#include "headless_gui_app.h"
#include "misuse_reports.h"

#include <gui_controls/gui_timeline_types.h>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class TimelineTwoViewsTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp    app;
            TimelineData      data;
            TimelineViewState top, bottom;
            ID                topID{}, bottomID{};
            ID                position{}, scale{}, music{};
            ID                scaleKey{};

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                auto make = [&] { return app.idManager.CreateStaticID(); };
                topID = make(); bottomID = make();
                position = make(); scale = make(); music = make(); scaleKey = make();
                data.duration = 8.0f;
                data.tracks = {
                    TimelineTrack{ .stableID = position, .name = "Position", .keys = { { make(), 1.0f }, { make(), 3.0f } } },
                    TimelineTrack{ .stableID = scale, .name = "Scale", .keys = { { scaleKey, 2.0f } } },
                    TimelineTrack{ .stableID = music, .name = "music.wav", .kind = TimelineTrackKind::Audio,
                                   .clip = TimelineClip{ .start = 0.5f, .length = 6.0f, .peaks = { 0.2f, 0.8f } } },
                };
            }

            void TearDown() override { app.Cleanup(); }

            // Both editors, one over the other.
            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow({ 1600, 1000 }, ElementFlags::None, app.theme.mainWindowStyleRef);
                    app.TimelineEditor(topID, { data, top }, LayoutOverride{}.FlexGrow(1));
                    app.TimelineEditor(bottomID, { data, bottom }, LayoutOverride{}.FlexGrow(1));
                    app.EndRootWindow();
                });
            }
            void Settle() { Frame(); Frame(); }
        };

        // Each editor has its own rows for the tracks: none share an ID, and each shows every
        // track inside itself.
        TEST_F(TimelineTwoViewsTest, EachEditorBuildsItsOwnRows)
        {
            GUITest::MisuseReports reports;
            Settle();
            EXPECT_FALSE(reports.Contains("share an ID")) << reports.All();
            const IntRect t = app.GetElementRect(topID), b = app.GetElementRect(bottomID);
            for (const ID track : { position, scale, music })
            {
                const IntRect inTop = app.GetElementRect(TimelineRowID(top, track));
                const IntRect inBottom = app.GetElementRect(TimelineRowID(bottom, track));
                EXPECT_GT(inTop.w, 0);
                EXPECT_GT(inBottom.w, 0);
                EXPECT_TRUE(t.Contains(inTop.position)) << "in the top editor";
                EXPECT_TRUE(b.Contains(inBottom.position)) << "in the bottom editor";
            }
        }

        // The wheel over one editor zooms that one; the other shows all of it still.
        TEST_F(TimelineTwoViewsTest, EachEditorZoomsOnItsOwn)
        {
            Settle();
            const IntRect t = app.GetElementRect(topID);
            app.InjectMouseScroll({ t.x + t.w * 3 / 4, t.y + t.h / 2 }, { 0, 1 });
            Settle();
            EXPECT_GT(top.viewLength, 0.0f) << "zoomed in";
            EXPECT_EQ(bottom.viewLength, 0.0f) << "the other as it was";
        }

        // A click on a row selects its track in that editor; the other keeps its own.
        TEST_F(TimelineTwoViewsTest, EachEditorSelectsOnItsOwn)
        {
            Settle();
            app.ClickAt(app.CenterOf(TimelineRowID(top, music)));
            Settle();
            EXPECT_EQ(top.selectedTrack, music);
            EXPECT_EQ(bottom.selectedTrack, ID::Invalid());

            app.ClickAt(app.CenterOf(TimelineRowID(bottom, scale)));
            Settle();
            EXPECT_EQ(bottom.selectedTrack, scale);
            EXPECT_EQ(top.selectedTrack, music) << "unchanged by the other editor";
        }

        // A track removed through one editor goes from the other's selection, with its keys,
        // on that one's next build.
        TEST_F(TimelineTwoViewsTest, ATrackRemovedInOneLeavesTheOthersSelection)
        {
            bottom.selectedTrack = scale;
            bottom.selectedKeys = { scaleKey };
            Settle();
            CommitTimelineEdit(data, RemoveTrackEdit(data, 1));
            Settle();
            ASSERT_EQ(data.tracks.size(), 2u);
            EXPECT_EQ(bottom.selectedTrack, ID::Invalid()) << "nothing left of it to select";
            EXPECT_TRUE(bottom.selectedKeys.empty());
            EXPECT_FALSE(app.HasElement(TimelineRowID(bottom, scale)));
            EXPECT_GT(app.GetElementRect(TimelineRowID(bottom, position)).w, 0) << "the rest still shown";
        }
    }
}
