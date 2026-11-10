// Plot: series drawn as lines, bars or areas, and handles dragged along the x axis.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_controls/gui_plot_types.h>

#include <cmath>
#include <limits>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        // ---------------------------------------------------------------------
        // The helpers
        // ---------------------------------------------------------------------

        TEST(PlotHelpers, HistogramBinsCountTheSamplesInEach)
        {
            const float nan = std::numeric_limits<float>::quiet_NaN();
            const float samples[] = { 0.0f, 0.1f, 0.24f, 0.25f, 0.5f, 0.99f, 1.0f, -0.1f, 1.5f, nan };
            const std::vector<float> bins = HistogramBins(samples, 4);
            EXPECT_EQ(bins, (std::vector<float>{ 3.0f, 1.0f, 1.0f, 2.0f })) << "1.0 in the last; outside and NaN in none";

            const float wide[] = { 10.0f, 15.0f, 19.0f, 20.0f };
            EXPECT_EQ(HistogramBins(wide, 2, 10.0f, 20.0f), (std::vector<float>{ 1.0f, 3.0f }));
            EXPECT_TRUE(HistogramBins(samples, 0).empty());
            EXPECT_EQ(HistogramBins(samples, 2, 1.0f, 1.0f), (std::vector<float>{ 0.0f, 0.0f })) << "an empty range counts nothing";
        }

        PlotView Levels()
        {
            PlotView view;
            view.xMin = 0.0f;
            view.xMax = 255.0f;
            view.handles = { { "Black", 0.0f }, { "Gray", 128.0f }, { "White", 255.0f } };
            return view;
        }

        TEST(PlotHelpers, AHandleStaysBetweenItsNeighboursAndInTheRange)
        {
            PlotView view = Levels();
            PlotEdit edit = MovePlotHandleEdit(view, 0, 60.0f);
            EXPECT_EQ(edit.from, (std::vector<float>{ 0.0f, 128.0f, 255.0f }));
            EXPECT_EQ(edit.to, (std::vector<float>{ 60.0f, 128.0f, 255.0f }));
            EXPECT_EQ(MovePlotHandleEdit(view, 0, 200.0f).to[0], 128.0f) << "not past the gray point";
            EXPECT_EQ(MovePlotHandleEdit(view, 0, -20.0f).to[0], 0.0f);
            EXPECT_TRUE(MovePlotHandleEdit(view, 0, -20.0f).empty()) << "already there";
            EXPECT_EQ(MovePlotHandleEdit(view, 2, 300.0f).to[2], 255.0f);
            EXPECT_EQ(MovePlotHandleEdit(view, 2, 100.0f).to[2], 128.0f) << "not before the gray point";
            EXPECT_EQ(MovePlotHandleEdit(view, 1, 10.0f).to[1], 10.0f);
            EXPECT_TRUE(MovePlotHandleEdit(view, 5, 10.0f).empty()) << "no such handle";
            EXPECT_TRUE(MovePlotHandleEdit(view, 1, std::nanf("")).empty());
        }

        TEST(PlotHelpers, EditsUndoAndReachTheApp)
        {
            PlotView view = Levels();
            std::vector<PlotEdit> seen;
            view.onEdit = [&](const PlotEdit& e) { seen.push_back(e); };
            const PlotEdit edit = MovePlotHandleEdit(view, 2, 200.0f);
            CommitPlotEdit(view, edit);
            EXPECT_EQ(view.handles[2].x, 200.0f);
            ASSERT_EQ(seen.size(), 1u);
            EXPECT_EQ(view.revision, 1u);
            ApplyPlotEdit(view, edit, /*undo*/ true);
            EXPECT_EQ(view.handles[2].x, 255.0f);
            ApplyPlotEdit(view, edit);
            EXPECT_EQ(view.handles[2].x, 200.0f);

            CommitPlotEdit(view, PlotEdit{});
            EXPECT_EQ(seen.size(), 1u) << "an empty edit isn't reported";
            EXPECT_EQ(view.revision, 1u);
            view.handles.pop_back();
            ApplyPlotEdit(view, edit);
            EXPECT_EQ(view.handles.size(), 2u) << "an edit for other handles is left alone";
            EXPECT_EQ(view.handles[1].x, 128.0f);
        }

        TEST(PlotHelpers, TheYRangeFitsWhatsShown)
        {
            PlotView view;
            view.series = { PlotSeries{ .name = "R", .values = { 1.0f, 8.0f } },
                            PlotSeries{ .name = "G", .values = { 20.0f, 2.0f } } };
            EXPECT_EQ(PlotShownY(view).max, 20.0f);
            view.series[1].visible = false;
            EXPECT_EQ(PlotShownY(view).max, 8.0f) << "a hidden series doesn't count";
            view.series[1].visible = true;
            view.shownSeries = 0;
            EXPECT_EQ(PlotShownY(view).max, 8.0f) << "only the one shown";
            view.series[0].values = { 0.0f, 0.0f };
            EXPECT_GT(PlotShownY(view).max, 0.0f) << "room for nothing";

            view.fitY = false;
            view.yMin = -1.0f;
            view.yMax = 3.0f;
            EXPECT_EQ(PlotShownY(view).min, -1.0f);
            EXPECT_EQ(PlotShownY(view).max, 3.0f);

            view.yMin = 0.0f;
            view.yMax = 99.0f;
            view.yScale = PlotScale::Log;
            EXPECT_FLOAT_EQ(PlotShownY(view).max, 2.0f) << "log10(1 + 99)";
            EXPECT_FLOAT_EQ(PlotScaleY(view, 9.0f), 1.0f);
            EXPECT_EQ(PlotScaleY(view, -5.0f), 0.0f) << "nothing below zero on a log scale";
            view.yScale = PlotScale::Linear;
            EXPECT_EQ(PlotScaleY(view, 9.0f), 9.0f);
        }

        // ---------------------------------------------------------------------
        // The control
        // ---------------------------------------------------------------------

        class PlotTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            PlotView       view;
            ID             plotID{};
            bool           changed = false;
            int            changes = 0;
            std::vector<PlotEdit> edits;

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                app.runPostLayoutCallbacks = true;
                plotID = app.idManager.CreateStaticID();
                view = Levels();
                view.series = { PlotSeries{ .name = "R", .color = CurveChannelColor::Red,   .values = { 1, 4, 9, 4, 1 } },
                                PlotSeries{ .name = "G", .color = CurveChannelColor::Green, .values = { 2, 2, 2, 2, 2 } } };
                view.onEdit = [this](const PlotEdit& e) { edits.push_back(e); };
                Settle();
            }
            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow({ 800, 600 }, ElementFlags::None, app.theme.mainWindowStyleRef);
                    changed = app.Plot(plotID, view, LayoutOverride{}.Size(510, 300));
                    if (changed) ++changes;
                    app.EndRootWindow();
                });
            }
            void Settle() { Frame(); Frame(); }

            // The series chips, in order: All, then each series.
            std::vector<ID> Chips()
            {
                std::vector<ID> ids;
                const StyleRef style = app.theme.plotChannelStyleRef;
                const auto& storage = app.elementStorage;
                for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
                    for (const Element& e : it->GetChildren(storage))
                        if (e.styleRef.layoutStyleIndex == style.layoutStyleIndex
                            && e.styleRef.visualBoxStyleIndex == style.visualBoxStyleIndex)
                            ids.push_back(e.id);
                return ids;
            }

            // Where a handle is drawn: under the plot, at its x.
            int2 HandleAt(int handle) const
            {
                const IntRect r = view.plotRect;
                const PlotRange shown = PlotShownX(view);
                const float x = float(r.x) + (view.handles[size_t(handle)].x - shown.min) / (shown.max - shown.min) * float(r.w);
                return { int32_t(std::lround(x)), r.y + r.h + app.styleManager.Px(5) };
            }
            int2 AtX(float x) const
            {
                const IntRect r = view.plotRect;
                const PlotRange shown = PlotShownX(view);
                return { int32_t(std::lround(float(r.x) + (x - shown.min) / (shown.max - shown.min) * float(r.w))), r.y + r.h / 2 };
            }
        };

        TEST_F(PlotTest, DraggingAHandleMovesIt)
        {
            ASSERT_GT(view.plotRect.w, 0);
            const int2 from = HandleAt(2);
            app.DragFrom(from, AtX(200.0f) + int2{ 0, from.y - AtX(200.0f).y });
            Frame();
            EXPECT_NEAR(view.handles[2].x, 200.0f, 1.0f);
            ASSERT_EQ(edits.size(), 1u) << "one edit, at the drop";
            EXPECT_EQ(edits[0].from[2], 255.0f);
            Frame();
            EXPECT_EQ(changes, 1) << "reported once";
        }

        TEST_F(PlotTest, AHandleIsKeptBetweenItsNeighbours)
        {
            const int2 from = HandleAt(0);
            app.DragFrom(from, { view.plotRect.x + view.plotRect.w, from.y });
            Frame();
            EXPECT_EQ(view.handles[0].x, 128.0f);
        }

        TEST_F(PlotTest, EscapeDuringADragPutsItBack)
        {
            const int2 from = HandleAt(1);
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            app.InjectMouseMove(AtX(60.0f) + int2{ 0, from.y - AtX(60.0f).y }, MouseButtons::Left);
            Frame();
            EXPECT_NEAR(view.handles[1].x, 60.0f, 1.0f) << "follows the drag";
            app.InjectKey(KeyCode::Escape);
            Frame();
            EXPECT_EQ(view.handles[1].x, 128.0f);
            app.InjectMouseUp(AtX(60.0f));
            Frame();
            EXPECT_EQ(view.handles[1].x, 128.0f);
            EXPECT_TRUE(edits.empty());
        }

        TEST_F(PlotTest, TheArrowKeysMoveTheHandleDraggedLast)
        {
            const int2 from = HandleAt(1);
            app.ClickAt(from);
            Frame();
            EXPECT_EQ(view.activeHandle, 1);
            app.InjectKey(KeyCode::Right);
            Frame();
            EXPECT_NEAR(view.handles[1].x, 128.0f + 2.55f, 0.01f) << "a hundredth of the range";
            app.InjectKey(KeyCode::Left, KeyAction::KeyDown, KeyModifier::Shift);
            Frame();
            EXPECT_NEAR(view.handles[1].x, 128.0f + 2.55f - 25.5f, 0.01f) << "a tenth with Shift";
            EXPECT_EQ(edits.size(), 2u);
        }

        TEST_F(PlotTest, AChipShowsOneSeries)
        {
            const std::vector<ID> chips = Chips();
            ASSERT_EQ(chips.size(), 3u);
            EXPECT_EQ(app.TextOf(chips[0]), "All");
            app.ClickAt(app.CenterOf(chips[2]));
            Frame();
            EXPECT_EQ(view.shownSeries, 1);
            app.ClickAt(app.CenterOf(chips[0]));
            Frame();
            EXPECT_EQ(view.shownSeries, -1);
        }

        TEST_F(PlotTest, OneSeriesHasNoChips)
        {
            view.series.pop_back();
            Settle();
            EXPECT_TRUE(Chips().empty());
        }

        TEST_F(PlotTest, ThePointerIsTracked)
        {
            app.InjectMouseMove(AtX(51.0f));
            Frame();
            EXPECT_TRUE(view.hovered);
            EXPECT_NEAR(view.hoverX, 51.0f, 1.0f);
        }

        TEST(PlotHelpers, TheShownRangeIsKeptInsideTheData)
        {
            PlotView view = Levels();
            EXPECT_EQ(PlotShownX(view).min, 0.0f) << "all of it at first";
            EXPECT_EQ(PlotShownX(view).max, 255.0f);
            view.shownXMin = -50.0f;
            view.shownXMax = 50.0f;
            view.shownSet = true;
            EXPECT_EQ(PlotShownX(view).min, 0.0f) << "slid back inside";
            EXPECT_EQ(PlotShownX(view).max, 100.0f);
            view.shownXMin = 10.0f;
            view.shownXMax = 900.0f;
            EXPECT_EQ(PlotShownX(view).min, 0.0f) << "no wider than the data";
            EXPECT_EQ(PlotShownX(view).max, 255.0f);

            // The y range fits the slots in view.
            view.series = { PlotSeries{ .values = { 50.0f, 2.0f, 3.0f, 4.0f } } };
            view.shownXMin = 128.0f;
            view.shownXMax = 255.0f;
            EXPECT_EQ(PlotShownY(view).max, 4.0f) << "the tall first slot is out of view";
        }

        TEST_F(PlotTest, TheWheelZoomsRoundThePointer)
        {
            const int2 at = AtX(64.0f);
            app.InjectMouseMove(at);
            app.InjectMouseScroll(at, { 0, 2 });
            Frame();
            const PlotRange shown = PlotShownX(view);
            EXPECT_LT(shown.max - shown.min, 255.0f) << "in";
            EXPECT_NEAR((64.0f - shown.min) / (shown.max - shown.min), 64.0f / 255.0f, 0.02f) << "64 stays under the pointer";

            app.InjectMouseScroll(at, { 0, -5 });
            Frame();
            EXPECT_EQ(PlotShownX(view).min, 0.0f) << "out, no further than all of it";
            EXPECT_EQ(PlotShownX(view).max, 255.0f);
        }

        TEST_F(PlotTest, TheMiddleButtonOrAltLeftDragsTheView)
        {
            const int2 at = AtX(128.0f);
            app.InjectMouseScroll(at, { 0, 3 });
            Frame();
            const PlotRange zoomed = PlotShownX(view);
            ASSERT_GT(zoomed.min, 0.0f);

            app.DragFrom(at, { at.x - 60, at.y }, MouseButtons::Middle);
            Frame();
            EXPECT_GT(PlotShownX(view).min, zoomed.min) << "dragged left: further right";
            EXPECT_FLOAT_EQ(PlotShownX(view).max - PlotShownX(view).min, zoomed.max - zoomed.min);

            const PlotRange before = PlotShownX(view);
            const int2 onHandle = HandleAt(1);
            app.InjectMouseMove(onHandle, MouseButtons::None, KeyModifier::Alt);
            app.InjectMouseDown(onHandle, MouseButtons::Left, KeyModifier::Alt);
            Frame();
            app.InjectMouseMove({ onHandle.x + 60, onHandle.y }, MouseButtons::Left, KeyModifier::Alt);
            Frame();
            app.InjectMouseUp({ onHandle.x + 60, onHandle.y }, MouseButtons::Left, KeyModifier::Alt);
            Frame();
            EXPECT_LT(PlotShownX(view).min, before.min) << "Alt + left drags the view, over a handle too";
            EXPECT_EQ(view.handles[1].x, 128.0f) << "the handle stays";
            EXPECT_TRUE(edits.empty());
        }

        TEST_F(PlotTest, AShowsAllOfIt)
        {
            const int2 at = AtX(64.0f);
            app.InjectMouseScroll(at, { 0, 2 });
            Frame();
            app.ClickAt(AtX(64.0f));
            Frame();
            app.InjectKey(KeyCode::KeyA);
            Frame();
            EXPECT_EQ(PlotShownX(view).min, 0.0f);
            EXPECT_EQ(PlotShownX(view).max, 255.0f);
        }

        // With the wheel set to scroll: sideways it scrolls the plot, up and down is left to
        // what it's in (an inspector's scroll); Ctrl+wheel and a pinch zoom.
        TEST_F(PlotTest, TheWheelScrollsInPanMode)
        {
            app.SetWheelMode(WheelMode::Pan);
            const int2 at = AtX(64.0f);
            app.InjectMouseMove(at);
            app.InjectMouseScroll(at, { 0, 2 }, KeyModifier::Control);
            Frame();
            const PlotRange zoomed = PlotShownX(view);
            ASSERT_LT(zoomed.max - zoomed.min, 255.0f) << "Ctrl zooms";
            app.InjectMouseScroll(at, { 0, -1 }, KeyModifier::Shift);
            Frame();
            EXPECT_GT(PlotShownX(view).min, zoomed.min) << "Shift: sideways";
            const PlotRange scrolled = PlotShownX(view);
            app.InjectMouseScroll(at, { 0, -1 });
            Frame();
            EXPECT_EQ(PlotShownX(view).min, scrolled.min) << "up and down isn't the plot's";
            app.InjectPinch(0.25f);
            Frame();
            EXPECT_EQ(PlotShownX(view).max - PlotShownX(view).min, 255.0f) << "a pinch zooms out";
        }

        // With the wheel set to scroll, up and down reaches what the plot is in (an inspector
        // that scrolls on); a zoom or a sideways scroll stays the plot's.
        TEST_F(PlotTest, AnUpAndDownScrollReachesWhatItsIn)
        {
            app.SetWheelMode(WheelMode::Pan);
            const ID outerID = app.idManager.CreateStaticID();
            int reached = 0;
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow({ 800, 600 }, ElementFlags::None, app.theme.mainWindowStyleRef);
                    Element outer{ .id = outerID, .styleRef = app.theme.emptyVertStyleRef };
                    outer.eventHandler = [&](ElementEvent&& event) -> bool {
                        if (event.type != EventType::MouseScroll) return false;
                        ++reached;
                        return true;
                    };
                    app.RegisterElement(std::move(outer), LayoutOverride{}.Size(600, 400));
                    VERIFY(app.elementStorage.BeginChildren());
                    app.Plot(plotID, view, LayoutOverride{}.Size(510, 300));
                    VERIFY(app.elementStorage.EndChildren());
                    app.EndRootWindow();
                });
            };
            frame();
            frame();
            const int2 at = AtX(64.0f);
            app.InjectMouseMove(at);
            app.InjectMouseScroll(at, { 0, -1 });
            frame();
            EXPECT_EQ(reached, 1) << "up and down: the container's";
            app.InjectMouseScroll(at, { 0, 1 }, KeyModifier::Control);
            app.InjectMouseScroll(at, { 0, -1 }, KeyModifier::Shift);
            frame();
            EXPECT_EQ(reached, 1) << "a zoom and a sideways scroll: the plot's";
        }

        TEST_F(PlotTest, DisabledItTakesNoDrag)
        {
            app.Frame([&] {
                app.BeginRootWindow({ 800, 600 }, ElementFlags::None, app.theme.mainWindowStyleRef);
                app.BeginDisabled();
                app.Plot(plotID, view, LayoutOverride{}.Size(510, 300));
                app.EndDisabled();
                app.EndRootWindow();
            });
            const int2 from = HandleAt(2);
            app.DragFrom(from, { view.plotRect.x, from.y });
            app.Frame([&] {
                app.BeginRootWindow({ 800, 600 }, ElementFlags::None, app.theme.mainWindowStyleRef);
                app.BeginDisabled();
                app.Plot(plotID, view, LayoutOverride{}.Size(510, 300));
                app.EndDisabled();
                app.EndRootWindow();
            });
            EXPECT_EQ(view.handles[2].x, 255.0f);
        }
    }
}
