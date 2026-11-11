// CurveEditor laid out as an animation editor (fps set): the channel tree, the frame ruler and
// its playhead, the interpolation and tangent toggles, keys on frames, and no presets.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <cmath>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class CurveEditorTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            CurveView view;
            std::vector<CurveEdit> edits;
            std::vector<float> playheads;
            int32_t width = 1000;   // the editor's, as a panel sizes it

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                app.runPostLayoutCallbacks = true;   // the editor keeps where its plot is
                const float2 rise[] = { { 0.0f, 0.0f }, { 1.0f, 2.0f }, { 2.0f, 1.0f } };
                const float2 flat[] = { { 0.0f, 0.5f }, { 2.0f, 0.5f } };
                view.channels = {
                    CurveChannel{ .name = "X", .path = "Sphere/Position", .color = CurveChannelColor::Red, .curve = MakeCurve(rise) },
                    CurveChannel{ .name = "Y", .path = "Sphere/Position", .color = CurveChannelColor::Green, .curve = MakeCurve(flat) },
                    CurveChannel{ .name = "Y", .path = "Sphere/Rotation", .color = CurveChannelColor::Blue, .curve = MakeCurve(flat) } };
                for (CurveChannel& channel : view.channels)
                    for (CurveKey& k : channel.curve.keys)
                        k.stableID = app.idManager.CreateStaticID();
                view.range = CurveRange{ 0.0f, 2.0f, 0.0f, 2.0f };
                view.fps = 24.0f;
                view.onEdit = [this](const CurveEdit& edit) { edits.push_back(edit); };
                view.onPlayheadChanged = [this](float time) { playheads.push_back(time); };
            }
            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow({ 2000, 600 });
                    app.CurveEditor(view, LayoutOverride::FixedSize(Pixels{ width }, Pixels{ 600 }));   // as a panel sizes it
                    app.EndRootWindow();
                });
            }
            void Frames()
            {
                Frame();
                Frame();
                Frame();
            }

            std::vector<ID> AllWithStyle(StyleRef style)
            {
                std::vector<ID> ids;
                const auto& storage = app.elementStorage;
                for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
                    for (const Element& e : it->GetChildren(storage))
                        if (e.styleRef.layoutStyleIndex == style.layoutStyleIndex
                            && e.styleRef.visualBoxStyleIndex == style.visualBoxStyleIndex)
                            ids.push_back(e.id);
                return ids;
            }
            size_t Rows() { return AllWithStyle(app.theme.curveTreeRowStyleRef).size() + AllWithStyle(app.theme.curveTreeRowHiddenStyleRef).size(); }

            CurveRange Shown() const { return view.shownSet ? view.shown : view.range; }
            int XAt(float time) const
            {
                const CurveRange s = Shown();
                return int(std::lround(float(view.plotRect.x) + (time - s.timeMin) / (s.timeMax - s.timeMin) * float(view.plotRect.w)));
            }
            int2 KeyAt(int k, int channel = 0) const
            {
                const CurveRange s = Shown();
                const CurveKey& key = view.channels[size_t(channel)].curve.keys[size_t(k)];
                return { XAt(key.time),
                         int(std::lround(float(view.plotRect.y + view.plotRect.h) - (key.value - s.valueMin) / (s.valueMax - s.valueMin) * float(view.plotRect.h))) };
            }
            ID KeyID(int k, int channel = 0) const { return view.channels[size_t(channel)].curve.keys[size_t(k)].stableID; }
        };

        // ---------------------------------------------------------------------
        // The channel tree
        // ---------------------------------------------------------------------

        TEST_F(CurveEditorTest, TheTreeGroupsTheChannelsByPath)
        {
            Frames();
            EXPECT_EQ(AllWithStyle(app.theme.curveTreeGroupStyleRef).size(), 3u) << "Sphere, Position, Rotation";
            EXPECT_EQ(app.CountByText("Sphere"), 1);
            EXPECT_EQ(Rows(), 3u);
            EXPECT_TRUE(AllWithStyle(app.theme.curveChannelStyleRef).empty()) << "the tree in place of the chips";

            // A group's row closes it, and the channels in it go; again opens it.
            const ID position = AllWithStyle(app.theme.curveTreeGroupStyleRef)[1];
            app.ClickAt(app.CenterOf(position));
            Frames();
            EXPECT_TRUE(view.collapsedGroups.contains("Sphere/Position"));
            EXPECT_EQ(Rows(), 1u);
            app.ClickAt(app.CenterOf(AllWithStyle(app.theme.curveTreeGroupStyleRef)[0]));
            Frames();
            EXPECT_EQ(Rows(), 0u) << "all under Sphere";
            EXPECT_EQ(AllWithStyle(app.theme.curveTreeGroupStyleRef).size(), 1u);
            app.ClickAt(app.CenterOf(AllWithStyle(app.theme.curveTreeGroupStyleRef)[0]));
            Frames();
            EXPECT_EQ(Rows(), 1u) << "Position stays closed";
        }

        TEST_F(CurveEditorTest, ARowPicksItsChannelAndItsEyeHidesIt)
        {
            Frames();
            app.ClickAt(app.CenterOf(AllWithStyle(app.theme.curveTreeRowStyleRef)[2]));
            Frames();
            EXPECT_EQ(view.activeChannel, 2);

            view.selectedKeys = { KeyID(0, 1) };
            Frames();
            // The timeline's row button: dim, bright when hovered, as the timeline's eyes.
            const std::vector<ID> eyes = AllWithStyle(app.theme.timelineRowButtonStyleRef);
            ASSERT_EQ(eyes.size(), 3u) << "one a row";
            app.ClickAt(app.CenterOf(eyes[1]));
            Frames();
            EXPECT_FALSE(view.channels[1].visible);
            EXPECT_EQ(view.activeChannel, 2) << "the eye, not its row";
            EXPECT_TRUE(view.selectedKeys.empty());
            EXPECT_EQ(AllWithStyle(app.theme.curveTreeRowHiddenStyleRef).size(), 1u);
        }

        TEST_F(CurveEditorTest, TheTreeShowsTheValuesAtThePlayhead)
        {
            view.playhead = 1.0f;
            Frames();
            EXPECT_EQ(app.CountByText("@ f 24"), 1);
            EXPECT_EQ(app.CountByText("2.000"), 1) << "X at frame 24";
            EXPECT_EQ(app.CountByText("0.500"), 2) << "both flat channels";
        }

        // ---------------------------------------------------------------------
        // The ruler and the playhead
        // ---------------------------------------------------------------------

        TEST_F(CurveEditorTest, TheRulerMovesThePlayheadOntoFrames)
        {
            Frames();
            const std::vector<ID> rulers = AllWithStyle(app.theme.curveRulerStyleRef);
            ASSERT_EQ(rulers.size(), 1u);
            const IntRect ruler = app.GetElementRect(rulers[0]);
            const int y = ruler.y + ruler.h / 2;

            // A press there: the playhead on the frame nearest.
            app.ClickAt({ XAt(0.51f), y });
            Frame();
            EXPECT_FLOAT_EQ(view.playhead, 12.0f / 24.0f);
            ASSERT_FALSE(playheads.empty());
            EXPECT_EQ(playheads.back(), view.playhead);

            // A drag follows the pointer, past the ruler too.
            app.InjectMouseMove({ XAt(0.5f), y });
            app.InjectMouseDown({ XAt(0.5f), y });
            Frame();
            app.InjectMouseMove({ XAt(1.5f), y + 200 }, MouseButtons::Left);
            Frame();
            EXPECT_FLOAT_EQ(view.playhead, 1.5f);
            app.InjectMouseUp({ XAt(1.5f), y + 200 });
            Frame();
            EXPECT_FALSE(view.playheadDrag.IsHeld());
            const size_t count = playheads.size();
            app.InjectMouseMove({ XAt(0.25f), y });
            Frame();
            EXPECT_EQ(playheads.size(), count) << "released";
            EXPECT_TRUE(edits.empty()) << "the playhead isn't an edit";
        }

        // ---------------------------------------------------------------------
        // The toolbar and the keys
        // ---------------------------------------------------------------------

        TEST_F(CurveEditorTest, TheToolbarSetsTheSelectedKeys)
        {
            view.selectedKeys = { KeyID(1) };
            Frames();
            app.ClickAt(app.CenterOf(app.FindByText("Constant")));
            Frames();
            EXPECT_EQ(view.channels[0].curve.keys[1].interpolation, CurveInterpolation::Constant);
            EXPECT_EQ(EvaluateCurve(view.channels[0].curve, 1.5f), 2.0f) << "held";
            app.ClickAt(app.CenterOf(app.FindByText("Flat")));
            Frames();
            EXPECT_EQ(view.channels[0].curve.keys[1].tangents, CurveTangentMode::Flat);
            EXPECT_EQ(view.channels[0].curve.keys[0].tangents, CurveTangentMode::Auto) << "only the selected";
            EXPECT_EQ(edits.size(), 2u);
        }

        TEST_F(CurveEditorTest, KeysLandOnFrames)
        {
            Frames();
            const int2 from = KeyAt(1);
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove({ from.x + 7, from.y }, MouseButtons::Left);
            Frame();
            app.InjectMouseMove({ from.x + 13, from.y }, MouseButtons::Left);
            Frame();
            app.InjectMouseUp({ from.x + 13, from.y });
            Frame();
            const float frame = view.channels[0].curve.keys[1].time * view.fps;
            EXPECT_GT(frame, 24.0f) << "moved";
            EXPECT_FLOAT_EQ(frame, std::round(frame));
        }

        TEST_F(CurveEditorTest, TheSelectedKeysFrameIsTyped)
        {
            view.selectedKeys = { KeyID(1) };
            Frames();
            const ID frame = app.FindControlAfterLabel("Frame");
            ASSERT_NE(frame, ID::Invalid());
            EXPECT_EQ(app.FindControlAfterLabel("Time"), ID::Invalid());
            app.ClickAt(app.CenterOf(frame));
            Frame();
            app.InjectKey(KeyCode::End);
            for (int i = 0; i < 6; ++i)
                app.InjectKey(KeyCode::Backspace);
            app.InjectText("30");
            Frame();
            app.InjectKey(KeyCode::Return);
            Frames();
            EXPECT_FLOAT_EQ(view.channels[0].curve.keys[1].time, 30.0f / 24.0f);
        }

        TEST_F(CurveEditorTest, NoPresetsAndNoTimesUnderThePlot)
        {
            Frames();
            EXPECT_TRUE(AllWithStyle(app.theme.curvePresetStyleRef).empty());
            EXPECT_EQ(AllWithStyle(app.theme.curvePlotFramesStyleRef).size(), 1u);
            EXPECT_TRUE(AllWithStyle(app.theme.curvePlotStyleRef).empty());
        }

        // Frame all sizes to its icon and text, whatever room the toolbar has.
        TEST_F(CurveEditorTest, FrameAllKeepsItsSize)
        {
            Frames();
            const ID frameAll = app.FindByText("Frame all");
            ASSERT_NE(frameAll, ID::Invalid());
            const int32_t narrow = app.GetElementRect(frameAll).w;
            width = 1600;
            Frames();
            EXPECT_EQ(app.GetElementRect(frameAll).w, narrow);
        }

        // Paths without fps: the tree in place of the chips, and no ruler.
        TEST_F(CurveEditorTest, ATreeWithoutFps)
        {
            view.fps = 0.0f;
            Frames();
            EXPECT_FALSE(AllWithStyle(app.theme.curveTreeStyleRef).empty());
            EXPECT_TRUE(AllWithStyle(app.theme.curveChannelStyleRef).empty());
            EXPECT_TRUE(AllWithStyle(app.theme.curveRulerStyleRef).empty());
        }

        // A row stays hovered over all of it: its name, value and eye too, not only between them.
        TEST_F(CurveEditorTest, ARowIsHoveredOverItsLabels)
        {
            Frames();
            const ID row = AllWithStyle(app.theme.curveTreeRowStyleRef)[0];
            const IntRect r = app.GetElementRect(row);
            for (int x = r.x + 4; x < r.x + r.w; x += 16)
            {
                app.InjectMouseMove({ x, r.y + r.h / 2 });
                Frame();
                Frame();
                EXPECT_NE(app.guiStateManager.GetCurrentState(row) & ElementState::Hover, ElementState::None) << "at " << x - r.x;
            }
        }

        // Without fps or paths, as it was: chips, presets, no tree, no ruler.
        TEST_F(CurveEditorTest, WithoutFpsOrPathsItsTheFieldsEditor)
        {
            view.fps = 0.0f;
            for (CurveChannel& channel : view.channels)
                channel.path.clear();
            Frames();
            EXPECT_TRUE(AllWithStyle(app.theme.curveTreeStyleRef).empty());
            EXPECT_TRUE(AllWithStyle(app.theme.curveRulerStyleRef).empty());
            EXPECT_EQ(AllWithStyle(app.theme.curveChannelStyleRef).size(), 3u);
            EXPECT_FALSE(AllWithStyle(app.theme.curvePresetStyleRef).empty());
        }
    }
}
