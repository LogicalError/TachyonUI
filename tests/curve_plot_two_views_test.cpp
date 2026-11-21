// Curves and a plot, each shown in two places: a CurveData (PlotData) and a CurveViewState
// (PlotViewState) for each view of it. Each view zooms, selects and opens its editor on its
// own; an edit made in one shows in both.

#include <gtest/gtest.h>

#include "headless_gui_app.h"
#include "misuse_reports.h"

#include <gui_controls/gui_plot_types.h>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class CurveTwoViewsTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            CurveData      data;
            CurveViewState left, right;
            ID             middleKey{}, leftField{}, rightField{};

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                app.runPostLayoutCallbacks = true;   // the editors keep where their plots are
                const float2 rise[] = { { 0.0f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 0.5f } };
                data.channels = { CurveChannel{ .name = "X", .curve = MakeCurve(rise) } };
                for (CurveKey& k : data.channels[0].curve.keys)
                    k.stableID = app.idManager.CreateStaticID();
                middleKey = data.channels[0].curve.keys[1].stableID;
                leftField = app.idManager.CreateStaticID();
                rightField = app.idManager.CreateStaticID();
            }
            void TearDown() override { app.Cleanup(); }

            // Two editors side by side, or with `fields`, two fields.
            void Frame(bool fields = false)
            {
                app.Frame([&] {
                    app.BeginRootWindow({ 2000, 700 });
                    app.BeginHorizontal(app.idManager.AllocateDynamicID("views"), app.theme.flexFillRowStyleRef,
                                        LayoutOverride::Auto().FlexGrow(1));
                    if (fields)
                    {
                        app.CurveField(leftField, { data, left }, LayoutOverride::FixedSize(Pixels{ 200 }, Pixels{ 40 }));
                        app.CurveField(rightField, { data, right }, LayoutOverride::FixedSize(Pixels{ 200 }, Pixels{ 40 }));
                    }
                    else
                    {
                        app.CurveEditor({ data, left }, LayoutOverride::FixedSize(Pixels{ 900 }, Pixels{ 600 }));
                        app.CurveEditor({ data, right }, LayoutOverride::FixedSize(Pixels{ 900 }, Pixels{ 600 }));
                    }
                    app.EndHorizontal();
                    app.EndRootWindow();
                });
            }
            void Frames(bool fields = false) { Frame(fields); Frame(fields); Frame(fields); }

            static int2 Middle(const IntRect& r) { return { r.x + r.w / 2, r.y + r.h / 2 }; }
        };

        // Two editors of the same curves build without sharing an ID.
        TEST_F(CurveTwoViewsTest, TwoEditorsShareNoIDs)
        {
            GUITest::MisuseReports reports;
            Frames();
            EXPECT_FALSE(reports.Contains("share an ID")) << reports.All();
            EXPECT_GT(left.plotRect.w, 0);
            EXPECT_GT(right.plotRect.w, 0);
            EXPECT_GE(right.plotRect.x, left.plotRect.x + left.plotRect.w) << "side by side";
        }

        // The wheel over one editor's plot zooms that one; the other shows what it did.
        TEST_F(CurveTwoViewsTest, EachEditorZoomsOnItsOwn)
        {
            Frames();
            const CurveRange leftBefore = left.shown, rightBefore = right.shown;
            app.InjectMouseScroll(Middle(left.plotRect), { 0, 2 });
            Frames();
            EXPECT_NE(left.shown, leftBefore) << "zoomed";
            EXPECT_EQ(right.shown, rightBefore) << "the other as it was";
        }

        // A key deleted through one view goes from the other's selection on its next build.
        TEST_F(CurveTwoViewsTest, AKeyDeletedInOneLeavesTheOthersSelection)
        {
            left.selectedKeys = right.selectedKeys = { middleKey };
            Frames();
            CommitCurveEdit({ data, left }, DeleteSelectedCurveKeysEdit({ data, left }));
            EXPECT_TRUE(left.selectedKeys.empty()) << "the view it was made through, at once";
            EXPECT_EQ(right.selectedKeys.size(), 1u) << "the other, not before it builds";
            Frames();
            ASSERT_EQ(data.channels[0].curve.keys.size(), 2u);
            EXPECT_TRUE(right.selectedKeys.empty()) << "nothing left of it to select";
        }

        // Each field opens its own editor window.
        TEST_F(CurveTwoViewsTest, EachFieldOpensItsOwnEditor)
        {
            GUITest::MisuseReports reports;
            Frames(true);
            app.ClickAt(app.CenterOf(leftField));
            Frames(true);
            EXPECT_TRUE(left.popupOpen);
            EXPECT_FALSE(right.popupOpen) << "the other field's stays shut";
            EXPECT_FALSE(reports.Contains("share an ID")) << reports.All();
        }

        class PlotTwoViewsTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            PlotData       data;
            PlotViewState  left, right;

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                app.runPostLayoutCallbacks = true;   // the plots keep where they are
                data.series  = { PlotSeries{ .name = "R", .values = { 1, 4, 2, 8, 5, 7, 3, 6 } },
                                 PlotSeries{ .name = "G", .values = { 2, 1, 6, 3, 7, 2, 5, 4 } } };
                data.handles = { { "Black", 0.0f }, { "White", 1.0f } };
            }
            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow({ 2000, 700 });
                    app.BeginHorizontal(app.idManager.AllocateDynamicID("views"), app.theme.flexFillRowStyleRef,
                                        LayoutOverride::Auto().FlexGrow(1));
                    app.Plot({ data, left }, LayoutOverride::FixedSize(Pixels{ 900 }, Pixels{ 400 }));
                    app.Plot({ data, right }, LayoutOverride::FixedSize(Pixels{ 900 }, Pixels{ 400 }));
                    app.EndHorizontal();
                    app.EndRootWindow();
                });
            }
            void Frames() { Frame(); Frame(); Frame(); }
        };

        // Two plots of the same data build without sharing an ID, and the wheel over one zooms
        // that one alone.
        TEST_F(PlotTwoViewsTest, EachPlotZoomsOnItsOwn)
        {
            GUITest::MisuseReports reports;
            Frames();
            EXPECT_FALSE(reports.Contains("share an ID")) << reports.All();
            ASSERT_GT(left.plotRect.w, 0);
            app.InjectMouseScroll({ left.plotRect.x + left.plotRect.w / 2, left.plotRect.y + left.plotRect.h / 2 }, { 0, 2 });
            Frames();
            EXPECT_TRUE(left.shownSet) << "zoomed";
            EXPECT_FALSE(right.shownSet) << "the other shows all of it still";
        }

        // A handle moved through one plot is where it went in both, as they show the same data;
        // the plot it was moved through is the one that reports it.
        TEST_F(PlotTwoViewsTest, AHandleMovedInOneIsMovedInBoth)
        {
            Frames();
            CommitPlotEdit({ data, left }, MovePlotHandleEdit(data, 0, 0.25f));
            EXPECT_FLOAT_EQ(data.handles[0].x, 0.25f);
            bool leftReports = false, rightReports = false;
            app.Frame([&] {
                app.BeginRootWindow({ 2000, 700 });
                app.BeginHorizontal(app.idManager.AllocateDynamicID("views"), app.theme.flexFillRowStyleRef,
                                    LayoutOverride::Auto().FlexGrow(1));
                leftReports  = app.Plot({ data, left }, LayoutOverride::FixedSize(Pixels{ 900 }, Pixels{ 400 }));
                rightReports = app.Plot({ data, right }, LayoutOverride::FixedSize(Pixels{ 900 }, Pixels{ 400 }));
                app.EndHorizontal();
                app.EndRootWindow();
            });
            EXPECT_TRUE(leftReports) << "the plot it was moved through";
            EXPECT_FALSE(rightReports) << "the other made no change itself";
        }
    }
}
