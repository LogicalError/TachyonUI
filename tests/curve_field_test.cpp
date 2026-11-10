// CurveField and its editor: opening and closing it, picking, dragging and adding keys, the
// tangent handles, the keys, the wheel, the channels, the presets and the menu.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <cmath>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class CurveFieldTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            CurveView view;
            ID fieldID {};
            std::vector<CurveEdit> edits;
            bool returned = false;

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                app.runPostLayoutCallbacks = true;   // the editor keeps where its plot is
                fieldID = app.idManager.CreateStaticID();
                const float2 points[] = { { 0.0f, 0.0f }, { 0.5f, 0.5f }, { 1.0f, 1.0f } };
                view.channels = { CurveChannel{ .name = "Value", .curve = MakeCurve(points) } };
                for (CurveKey& k : view.channels[0].curve.keys)
                    k.stableID = app.idManager.CreateStaticID();
                view.onEdit = [this](const CurveEdit& edit) { edits.push_back(edit); };
            }
            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow({ 1000, 800 });
                    returned = app.CurveField(fieldID, view, LayoutOverride::FixedWidth(200));
                    app.EndRootWindow();
                });
            }

            void Open()
            {
                Frame();
                app.ClickAt(app.CenterOf(fieldID));
                Frame();
                Frame();
                ASSERT_TRUE(view.popupOpen);
                ASSERT_GT(view.plotRect.w, 0);
            }

            ID FindByStyle(StyleRef style)
            {
                const auto& storage = app.elementStorage;
                for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
                    for (const Element& e : it->GetChildren(storage))
                        if (e.styleRef.layoutStyleIndex == style.layoutStyleIndex
                            && e.styleRef.visualBoxStyleIndex == style.visualBoxStyleIndex)
                            return e.id;
                return ID::Invalid();
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

            CurveRange Shown() const { return view.shownSet ? view.shown : view.range; }

            // Where a time and value are on the plot, in pixels.
            int2 At(float time, float value) const
            {
                const CurveRange s = Shown();
                const IntRect r = view.plotRect;
                return { int32_t(std::lround(float(r.x) + (time - s.timeMin) / (s.timeMax - s.timeMin) * float(r.w))),
                         int32_t(std::lround(float(r.y + r.h) - (value - s.valueMin) / (s.valueMax - s.valueMin) * float(r.h))) };
            }
            int2 KeyAt(int k, int channel = 0) const
            {
                const CurveKey& key = view.channels[size_t(channel)].curve.keys[size_t(k)];
                return At(key.time, key.value);
            }
            float TimeAt(int x) const
            {
                const CurveRange s = Shown();
                return s.timeMin + float(x - view.plotRect.x) / float(view.plotRect.w) * (s.timeMax - s.timeMin);
            }
            // The end of a key's handle on a side.
            int2 HandleAt(int k, bool out)
            {
                const CurveKey& key = view.channels[0].curve.keys[size_t(k)];
                const CurveRange s = Shown();
                const float sx = float(view.plotRect.w) / (s.timeMax - s.timeMin), sy = float(view.plotRect.h) / (s.valueMax - s.valueMin);
                const float slope = out ? key.outSlope : key.inSlope;
                float dx = 1.0f, dy = -slope * sy / sx;
                const float len = std::hypot(dx, dy);
                const float length = float(app.styleManager.Px(40)) * (out ? 1.0f : -1.0f);
                const int2 p = KeyAt(k);
                return { p.x + int32_t(std::lround(dx / len * length)), p.y + int32_t(std::lround(dy / len * length)) };
            }

            void Drag(int2 from, int2 to, MouseButtons button = MouseButtons::Left, KeyModifier mods = KeyModifier::None)
            {
                app.InjectMouseMove(from, MouseButtons::None, mods);
                app.InjectMouseDown(from, button, mods);
                Frame();
                app.InjectMouseMove({ (from.x + to.x) / 2, (from.y + to.y) / 2 }, button, mods);
                Frame();
                app.InjectMouseMove(to, button, mods);
                Frame();
                app.InjectMouseUp(to, button, mods);
                Frame();
            }

            void RightClick(int2 at)
            {
                app.InjectMouseMove(at);
                Frame();
                app.ClickAt(at, MouseButtons::Right);
                Frame();
                Frame();
            }

            // A menu item: the menu is a window of its own, over the editor's.
            void ClickMenuItem(std::string_view text)
            {
                const ID item = app.FindByText(text);
                ASSERT_NE(item, ID::Invalid()) << text;
                app.mouseWindow = app.menuManager.levels[0].windowID;
                app.InjectMouseMove(app.CenterOf(item));
                Frame();
                app.ClickAt(app.CenterOf(item));
                Frame();
                app.mouseWindow = ID::Invalid();
                Frame();
            }

            ID KeyID(int k, int channel = 0) const { return view.channels[size_t(channel)].curve.keys[size_t(k)].stableID; }
        };

        // ---------------------------------------------------------------------
        // The field and its window
        // ---------------------------------------------------------------------

        TEST_F(CurveFieldTest, AClickOpensTheEditorAndAnotherClosesIt)
        {
            Frame();
            EXPECT_EQ(FindByStyle(app.theme.curvePlotStyleRef), ID::Invalid());
            app.ClickAt(app.CenterOf(fieldID));
            Frame();
            Frame();
            EXPECT_TRUE(view.popupOpen);
            EXPECT_NE(FindByStyle(app.theme.curvePlotStyleRef), ID::Invalid());
            app.ClickAt(app.CenterOf(fieldID));
            Frame();
            Frame();
            EXPECT_FALSE(view.popupOpen);
            EXPECT_EQ(FindByStyle(app.theme.curvePlotStyleRef), ID::Invalid());
        }

        TEST_F(CurveFieldTest, EscapeClosesTheEditor)
        {
            Open();
            app.InjectKey(KeyCode::Escape);
            Frame();
            Frame();
            EXPECT_FALSE(view.popupOpen);
            EXPECT_EQ(app.guiStateManager.Interaction().GetFocusedID(), fieldID) << "the keyboard back on the field";
        }

        // The field says so on the frame after a change, once.
        TEST_F(CurveFieldTest, TheFieldReportsAChange)
        {
            Frame();
            EXPECT_FALSE(returned);
            CommitCurveEdit(view, SetCurveWrapEdit(view, 0, CurveWrapMode::Loop, CurveWrapMode::Loop));
            Frame();
            EXPECT_TRUE(returned);
            Frame();
            EXPECT_FALSE(returned);
        }

        TEST_F(CurveFieldTest, ADisabledFieldDoesntOpen)
        {
            auto disabledFrame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow({ 1000, 800 });
                    app.BeginDisabled();
                    app.CurveField(fieldID, view, LayoutOverride::FixedWidth(200));
                    app.EndDisabled();
                    app.EndRootWindow();
                });
            };
            disabledFrame();
            app.ClickAt(app.CenterOf(fieldID));
            disabledFrame();
            disabledFrame();
            EXPECT_FALSE(view.popupOpen);

            // Disabled while open: it closes.
            view.popupOpen = true;
            disabledFrame();
            EXPECT_FALSE(view.popupOpen);
        }

        // ---------------------------------------------------------------------
        // Keys
        // ---------------------------------------------------------------------

        TEST_F(CurveFieldTest, AClickSelectsAKeyAndTheEmptyPlotClearsIt)
        {
            Open();
            app.ClickAt(KeyAt(1));
            Frame();
            EXPECT_EQ(view.selectedKeys, std::unordered_set<ID>{ KeyID(1) });
            app.ClickAt(KeyAt(2), MouseButtons::Left, KeyModifier::Shift);
            Frame();
            EXPECT_EQ(view.selectedKeys.size(), 2u) << "Shift adds";
            app.ClickAt(KeyAt(1), MouseButtons::Left, KeyModifier::Control);
            Frame();
            EXPECT_EQ(view.selectedKeys, std::unordered_set<ID>{ KeyID(2) }) << "Ctrl toggles";
            app.ClickAt(At(0.2f, 0.9f));
            Frame();
            EXPECT_TRUE(view.selectedKeys.empty());
            EXPECT_TRUE(edits.empty());
        }

        // A dragged key moves with the pointer; the edit comes once, on the release.
        TEST_F(CurveFieldTest, DraggingAKeyMovesIt)
        {
            Open();
            Drag(KeyAt(1), At(0.6f, 0.3f));
            const CurveKey& k = view.channels[0].curve.keys[1];
            EXPECT_NEAR(k.time, 0.6f, 0.02f);
            EXPECT_NEAR(k.value, 0.3f, 0.02f);
            ASSERT_EQ(edits.size(), 1u);
            EXPECT_EQ(edits[0].changes[0].from.keys[1].time, 0.5f);
            EXPECT_EQ(view.selectedKeys, std::unordered_set<ID>{ k.stableID });
        }

        // A key dragged past its neighbor: the keys in time order again.
        TEST_F(CurveFieldTest, DraggingPastANeighborReorders)
        {
            Open();
            const ID first = KeyID(0);
            Drag(KeyAt(0), At(0.75f, 0.2f));
            const auto& keys = view.channels[0].curve.keys;
            EXPECT_EQ(keys[1].stableID, first);
            EXPECT_LT(keys[0].time, keys[1].time);
            EXPECT_LT(keys[1].time, keys[2].time);
        }

        // Where two channels' keys meet, the active channel's is picked.
        TEST_F(CurveFieldTest, TheActiveChannelsKeyComesFirst)
        {
            const float2 points[] = { { 0.0f, 0.2f }, { 0.5f, 0.5f }, { 1.0f, 0.2f } };
            view.channels.push_back(CurveChannel{ .name = "G", .color = CurveChannelColor::Green, .curve = MakeCurve(points) });
            for (CurveKey& k : view.channels[1].curve.keys)
                k.stableID = app.idManager.CreateStaticID();
            view.activeChannel = 1;
            Open();
            app.ClickAt(KeyAt(1, 1));
            Frame();
            EXPECT_EQ(view.selectedKeys, std::unordered_set<ID>{ KeyID(1, 1) });
            view.activeChannel = 0;
            Frame();
            app.ClickAt(KeyAt(1, 0));
            Frame();
            EXPECT_EQ(view.selectedKeys, std::unordered_set<ID>{ KeyID(1, 0) });
        }

        // Keys stay in the range's time; in its values too with clampValues.
        TEST_F(CurveFieldTest, DraggedKeysStayInTheRange)
        {
            view.clampValues = true;
            Open();
            const ID dragged = KeyID(1);
            Drag(KeyAt(1), { view.plotRect.x + view.plotRect.w + 200, view.plotRect.y - 200 });
            const auto& keys = view.channels[0].curve.keys;
            const auto k = std::find_if(keys.begin(), keys.end(), [dragged](const CurveKey& key) { return key.stableID == dragged; });
            ASSERT_NE(k, keys.end());
            EXPECT_EQ(k->time, 1.0f);
            EXPECT_EQ(k->value, 1.0f);
        }

        TEST_F(CurveFieldTest, EscapePutsADraggedKeyBack)
        {
            Open();
            const Curve before = view.channels[0].curve;
            const int2 from = KeyAt(1), to = At(0.7f, 0.1f);
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove(to, MouseButtons::Left);
            Frame();
            EXPECT_FALSE(view.channels[0].curve == before);
            app.InjectKey(KeyCode::Escape);
            Frame();
            app.InjectMouseUp(to);
            Frame();
            EXPECT_TRUE(view.channels[0].curve == before);
            EXPECT_TRUE(edits.empty());
            EXPECT_TRUE(view.popupOpen) << "the drag ends, the editor stays";
        }

        TEST_F(CurveFieldTest, ADoubleClickAddsAKey)
        {
            Open();
            const int2 at = At(0.25f, 0.8f);
            app.ClickAt(at);
            Frame();
            app.ClickAt(at);
            Frame();
            ASSERT_EQ(view.channels[0].curve.keys.size(), 4u);
            const CurveKey& added = view.channels[0].curve.keys[1];
            EXPECT_NEAR(added.time, 0.25f, 0.01f);
            EXPECT_NEAR(added.value, 0.8f, 0.01f);
            EXPECT_EQ(view.selectedKeys, std::unordered_set<ID>{ added.stableID });
            EXPECT_EQ(edits.size(), 1u);

            // Near the curve: on it.
            const Curve before = view.channels[0].curve;
            const float onCurve = EvaluateCurve(before, 0.8f);
            const int2 near = At(0.8f, onCurve);
            app.ClickAt({ near.x, near.y + 3 });
            Frame();
            app.ClickAt({ near.x, near.y + 3 });
            Frame();
            ASSERT_EQ(view.channels[0].curve.keys.size(), 5u);
            const CurveKey& onIt = view.channels[0].curve.keys[3];
            EXPECT_NEAR(onIt.value, EvaluateCurve(before, onIt.time), 1e-5f);
        }

        TEST_F(CurveFieldTest, ABoxSelects)
        {
            Open();
            Drag(At(-0.05f, -0.05f), At(0.6f, 0.6f));
            EXPECT_EQ(view.selectedKeys, (std::unordered_set<ID>{ KeyID(0), KeyID(1) }));
            Drag(At(0.85f, 0.85f), At(1.05f, 1.05f), MouseButtons::Left, KeyModifier::Shift);
            EXPECT_EQ(view.selectedKeys.size(), 3u) << "Shift adds";
            Drag(At(0.4f, 0.4f), At(0.6f, 0.6f), MouseButtons::Left, KeyModifier::Control);
            EXPECT_EQ(view.selectedKeys, (std::unordered_set<ID>{ KeyID(0), KeyID(2) })) << "Ctrl toggles";
        }

        TEST_F(CurveFieldTest, TheKeys)
        {
            Open();
            app.ClickAt(KeyAt(1));
            Frame();
            app.InjectKey(KeyCode::Delete);
            Frame();
            EXPECT_EQ(view.channels[0].curve.keys.size(), 2u);
            EXPECT_EQ(edits.size(), 1u);

            app.InjectKey(KeyCode::KeyA, KeyAction::KeyDown, KeyModifier::Control);
            Frame();
            EXPECT_EQ(view.selectedKeys.size(), 2u);
            app.InjectKey(KeyCode::Escape);
            Frame();
            EXPECT_TRUE(view.selectedKeys.empty());
            EXPECT_TRUE(view.popupOpen) << "Escape cleared the selection first";

            view.shown = { 0.4f, 0.5f, 0.4f, 0.5f };
            view.shownSet = true;
            app.InjectKey(KeyCode::KeyA);
            Frame();
            EXPECT_LE(view.shown.timeMin, 0.0f) << "A frames all the keys";
            EXPECT_GE(view.shown.timeMax, 1.0f);
            EXPECT_LT(view.shown.valueMin, 0.0f) << "with a margin";
            EXPECT_GT(view.shown.valueMax, 1.0f);
        }

        // A handle's drag sets the slope: both sides of a smooth key, one side of a broken one.
        TEST_F(CurveFieldTest, DraggingAHandleSetsTheSlope)
        {
            Open();
            app.ClickAt(KeyAt(1));
            Frame();
            const int2 handle = HandleAt(1, true);
            Drag(handle, { handle.x, handle.y - 40 });
            const CurveKey& k = view.channels[0].curve.keys[1];
            EXPECT_EQ(k.mode, CurveTangentMode::FreeSmooth);
            EXPECT_GT(k.outSlope, 1.5f);
            EXPECT_EQ(k.inSlope, k.outSlope);
            ASSERT_EQ(edits.size(), 1u);

            view.channels[0].curve.keys[1].mode = CurveTangentMode::Broken;
            const float out = k.outSlope;
            const int2 in = HandleAt(1, false);
            Drag(in, { in.x, in.y + 30 });
            EXPECT_EQ(view.channels[0].curve.keys[1].outSlope, out) << "the other side stays";
            EXPECT_NE(view.channels[0].curve.keys[1].inSlope, out);
            EXPECT_EQ(view.channels[0].curve.keys[1].inMode, CurveSideMode::Free);
        }

        // ---------------------------------------------------------------------
        // The view
        // ---------------------------------------------------------------------

        TEST_F(CurveFieldTest, TheWheelZoomsAndTheMiddleButtonPans)
        {
            Open();
            const int2 center = At(0.5f, 0.5f);
            app.InjectMouseScroll(center, { 0, 1 });
            Frame();
            ASSERT_TRUE(view.shownSet);
            EXPECT_LT(view.shown.timeMax - view.shown.timeMin, 1.0f) << "in";
            EXPECT_LT(view.shown.valueMax - view.shown.valueMin, 1.0f);
            EXPECT_NEAR(view.shown.timeMin + view.shown.timeMax, 1.0f, 0.02f) << "round the pointer";
            app.InjectMouseScroll(center, { 0, -1 });
            app.InjectMouseScroll(center, { 0, -1 });
            Frame();
            EXPECT_GT(view.shown.timeMax - view.shown.timeMin, 1.0f) << "out";

            const CurveRange before = view.shown;
            app.InjectMouseScroll(center, { 0, 1 }, KeyModifier::Alt);
            Frame();
            EXPECT_EQ(view.shown.valueMin, before.valueMin) << "Alt: the time only";
            EXPECT_LT(view.shown.timeMax - view.shown.timeMin, before.timeMax - before.timeMin);

            const CurveRange zoomed = view.shown;
            app.InjectMouseScroll(center, { 0, -1 }, KeyModifier::Shift);
            Frame();
            EXPECT_GT(view.shown.timeMin, zoomed.timeMin) << "Shift scrolls along the time";
            EXPECT_FLOAT_EQ(view.shown.timeMax - view.shown.timeMin, zoomed.timeMax - zoomed.timeMin) << "no zoom";

            const CurveRange panned = view.shown;
            Drag(center, { center.x - 50, center.y + 40 }, MouseButtons::Middle);
            EXPECT_GT(view.shown.timeMin, panned.timeMin) << "dragged left: later times";
            EXPECT_GT(view.shown.valueMin, panned.valueMin) << "dragged down: higher values";
            EXPECT_TRUE(edits.empty());

            // Alt + left drags it too, and leaves the keys alone.
            const CurveRange again = view.shown;
            app.InjectMouseMove(center, MouseButtons::None, KeyModifier::Alt);
            app.InjectMouseDown(center, MouseButtons::Left, KeyModifier::Alt);
            Frame();
            app.InjectMouseMove({ center.x - 50, center.y }, MouseButtons::Left, KeyModifier::Alt);
            Frame();
            app.InjectMouseUp({ center.x - 50, center.y }, MouseButtons::Left, KeyModifier::Alt);
            Frame();
            EXPECT_GT(view.shown.timeMin, again.timeMin) << "Alt + left";
            EXPECT_TRUE(edits.empty());
            EXPECT_TRUE(view.selectedKeys.empty());
        }

        // With the wheel set to scroll, it scrolls the view; Ctrl+wheel and a pinch zoom.
        TEST_F(CurveFieldTest, TheWheelScrollsInPanMode)
        {
            app.SetWheelMode(WheelMode::Pan);
            Open();
            const int2 center = At(0.5f, 0.5f);
            app.InjectMouseScroll(center, { 0, -1 });
            Frame();
            ASSERT_TRUE(view.shownSet);
            EXPECT_FLOAT_EQ(view.shown.timeMax - view.shown.timeMin, 1.0f) << "no zoom";
            EXPECT_LT(view.shown.valueMin, 0.0f) << "toward the user: lower values";
            app.InjectMouseScroll(center, { 0, 1 }, KeyModifier::Control);
            Frame();
            EXPECT_LT(view.shown.timeMax - view.shown.timeMin, 1.0f) << "Ctrl zooms in";
            const float span = view.shown.timeMax - view.shown.timeMin;
            app.InjectPinch(0.5f);
            Frame();
            EXPECT_GT(view.shown.timeMax - view.shown.timeMin, span) << "a pinch together zooms out";
        }

        // ---------------------------------------------------------------------
        // Channels, presets, wrap, menu
        // ---------------------------------------------------------------------

        TEST_F(CurveFieldTest, ChannelsPickAndHide)
        {
            const float2 flat[] = { { 0.0f, 0.2f }, { 1.0f, 0.2f } };
            view.channels.push_back(CurveChannel{ .name = "G", .color = CurveChannelColor::Green, .curve = MakeCurve(flat) });
            for (CurveKey& k : view.channels[1].curve.keys)
                k.stableID = app.idManager.CreateStaticID();
            Open();
            const std::vector<ID> chips = AllWithStyle(app.theme.curveChannelStyleRef);
            ASSERT_EQ(chips.size(), 2u);
            app.ClickAt(app.CenterOf(chips[1]));
            Frame();
            EXPECT_EQ(view.activeChannel, 1);

            // A click on a key makes its channel the active one.
            app.ClickAt(KeyAt(1, 0));
            Frame();
            EXPECT_EQ(view.activeChannel, 0);

            // Its eye hides it: its keys no longer selected, nor picked, nor added to.
            view.selectedKeys = { KeyID(0, 1) };
            Frame();
            const std::vector<ID> eyes = AllWithStyle(app.theme.iconButtonStyleRef);   // an eye after each chip, then frame all
            ASSERT_EQ(eyes.size(), 3u);
            app.ClickAt(app.CenterOf(eyes[1]));
            Frame();
            Frame();
            EXPECT_FALSE(view.channels[1].visible);
            EXPECT_TRUE(view.selectedKeys.empty());
            app.ClickAt(KeyAt(0, 1));
            Frame();
            EXPECT_FALSE(view.selectedKeys.contains(KeyID(0, 1)));
            EXPECT_TRUE(AddCurveKeyEdit(view, 1, 0.5f, 0.5f, app.idManager.CreateStaticID()).empty());
        }

        TEST_F(CurveFieldTest, APresetReplacesTheActiveCurve)
        {
            Open();
            const std::vector<ID> presets = AllWithStyle(app.theme.curvePresetStyleRef);
            ASSERT_EQ(presets.size(), DefaultCurvePresets().size());
            app.ClickAt(app.CenterOf(presets[0]));   // constant
            Frame();
            ASSERT_EQ(edits.size(), 1u);
            EXPECT_EQ(view.channels[0].curve.keys.size(), 2u);
            EXPECT_EQ(EvaluateCurve(view.channels[0].curve, 0.3f), 1.0f);

            view.presets = { CurvePreset{ .name = "Mine", .curve = DefaultCurvePresets()[2].curve } };
            Frame();
            Frame();
            const std::vector<ID> own = AllWithStyle(app.theme.curvePresetStyleRef);
            ASSERT_EQ(own.size(), 1u) << "the app's own in place of the built-in ones";
            app.ClickAt(app.CenterOf(own[0]));
            Frame();
            EXPECT_FLOAT_EQ(EvaluateCurve(view.channels[0].curve, 0.25f), 0.75f);
        }

        // A right click on a key: its menu, which sets its mode or deletes it.
        TEST_F(CurveFieldTest, TheKeysMenu)
        {
            Open();
            const int2 key = KeyAt(1);
            RightClick(key);
            EXPECT_EQ(view.menuKey, KeyID(1));
            EXPECT_EQ(view.selectedKeys, std::unordered_set<ID>{ KeyID(1) });
            ClickMenuItem("Flat");
            EXPECT_EQ(view.channels[0].curve.keys[1].mode, CurveTangentMode::Flat);
            EXPECT_EQ(view.channels[0].curve.keys[1].outSlope, 0.0f);

            RightClick(key);
            ClickMenuItem("Delete key");
            EXPECT_EQ(view.channels[0].curve.keys.size(), 2u);
            EXPECT_EQ(edits.size(), 2u);
        }

        // A right click on the empty plot: Add key, there.
        TEST_F(CurveFieldTest, ThePlotsMenu)
        {
            Open();
            RightClick(At(0.25f, 0.9f));
            EXPECT_EQ(view.menuKey, ID::Invalid());
            ClickMenuItem("Add key");
            ASSERT_EQ(view.channels[0].curve.keys.size(), 4u);
            EXPECT_NEAR(view.channels[0].curve.keys[1].time, 0.25f, 0.01f);
            EXPECT_NEAR(view.channels[0].curve.keys[1].value, 0.9f, 0.01f);
        }

        // The selected key's time and value, typed.
        TEST_F(CurveFieldTest, TheSelectedKeysFields)
        {
            Open();
            const ID key = KeyID(0);
            app.ClickAt(KeyAt(0));
            Frame();
            Frame();
            const ID time = app.FindControlAfterLabel("Time");
            ASSERT_NE(time, ID::Invalid());
            app.ClickAt(app.CenterOf(time));
            Frame();
            app.InjectKey(KeyCode::End);
            app.InjectText("2");   // after its own text: a later time
            Frame();
            app.InjectKey(KeyCode::Return);
            Frame();
            Frame();
            const auto& keys = view.channels[0].curve.keys;
            const auto it = std::find_if(keys.begin(), keys.end(), [key](const CurveKey& k) { return k.stableID == key; });
            ASSERT_NE(it, keys.end());
            EXPECT_GT(it->time, 0.0f) << "moved";
            EXPECT_LE(it->time, 1.0f) << "in the range";
            EXPECT_EQ(it->value, 0.0f) << "the value stays";
            EXPECT_EQ(edits.size(), 1u);
        }
    }
}
