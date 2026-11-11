// TimelineEditor: the track list, the ruler and the lanes; selecting, moving and deleting keys;
// the loop range; zoom and pan; and the edits, which undo.

#include <gtest/gtest.h>

#include "headless_gui_app.h"
#include "skribidi_test_fixture.h"

#include <gui_core/graphics/gui_custom_draw_context.h>
#include <gui_core/elements/gui_element_storage.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;
    using GUITest::StubGUIRenderer;

    namespace
    {
        class TimelineEditorTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp            app;
            TimelineView              view;
            ID                        editorID{};
            std::vector<TimelineEdit> edits;

            // Sphere (Position, Scale), Light (collapsed: Intensity), music, Events.
            ID sphere{}, position{}, scale{}, light{}, intensity{}, music{}, events{};
            std::vector<ID> positionKeys, scaleKeys;
            ID spawn{}, explode{};

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                editorID = app.idManager.CreateStaticID();
                auto make = [&] { return app.idManager.CreateStaticID(); };
                sphere = make(); position = make(); scale = make(); light = make(); intensity = make(); music = make(); events = make();
                spawn = make(); explode = make();
                view.duration = 8.0f;
                view.fps = 30.0f;
                TimelineTrack p{ .stableID = position, .name = "Position", .depth = 1 };
                for (const float t : { 0.0f, 2.0f, 4.0f }) { positionKeys.push_back(make()); p.keys.push_back({ positionKeys.back(), t }); }
                TimelineTrack s{ .stableID = scale, .name = "Scale", .depth = 1 };
                for (const float t : { 1.0f, 4.0f }) { scaleKeys.push_back(make()); s.keys.push_back({ scaleKeys.back(), t }); }
                TimelineTrack i{ .stableID = intensity, .name = "Intensity", .depth = 1, .keys = { { make(), 3.0f } } };
                view.tracks = {
                    TimelineTrack{ .stableID = sphere, .name = "Sphere", .kind = TimelineTrackKind::Group },
                    p, s,
                    TimelineTrack{ .stableID = light, .name = "Light", .kind = TimelineTrackKind::Group, .expanded = false },
                    i,
                    TimelineTrack{ .stableID = music, .name = "music.wav", .kind = TimelineTrackKind::Audio,
                                   .clip = TimelineClip{ .start = 0.5f, .length = 6.0f, .peaks = { 0.2f, 0.8f, 0.5f, 1.0f } } },
                    TimelineTrack{ .stableID = events, .name = "Events", .kind = TimelineTrackKind::Events,
                                   .events = { { spawn, 1.5f, "Spawn" }, { explode, 5.0f, "Explode" } } },
                };
                view.onEdit = [this](const TimelineEdit& edit) { edits.push_back(edit); };
            }
            void TearDown() override { app.Cleanup(); }

            bool disabled = false;

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow({ 1600, 900 }, ElementFlags::None, app.theme.mainWindowStyleRef);
                    if (disabled) app.BeginDisabled();
                    app.TimelineEditor(editorID, view);
                    if (disabled) app.EndDisabled();
                    app.EndRootWindow();
                });
            }
            void Settle() { Frame(); Frame(); }

            // The element in `style` (the first, in layout order).
            ID WithStyle(StyleRef style) const
            {
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                    for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                    {
                        const ElementRef ref{ d, i };
                        if (app.elementStorage.IsValidRef(ref) && app.elementStorage.GetElementUnsafe(ref).styleRef == style)
                            return app.elementStorage.GetElementUnsafe(ref).id;
                    }
                return ID::Invalid();
            }
            IntRect Rect(StyleRef style) const { return app.GetElementRect(WithStyle(style)); }
            std::vector<ID> AllWithStyle(StyleRef style) const
            {
                std::vector<ID> out;
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                    for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                    {
                        const ElementRef ref{ d, i };
                        if (app.elementStorage.IsValidRef(ref) && app.elementStorage.GetElementUnsafe(ref).styleRef == style)
                            out.push_back(app.elementStorage.GetElementUnsafe(ref).id);
                    }
                return out;
            }
            ID One(StyleRef style) const
            {
                const std::vector<ID> ids = AllWithStyle(style);
                return ids.size() == 1 ? ids[0] : ID::Invalid();
            }
            // The `n`th element in `style` inside row `row`.
            int2 InRow(ID row, StyleRef style, int n = 0) const
            {
                int seen = 0;
                for (const ID id : AllWithStyle(style))
                    for (ElementRef ref = app.elementStorage.FindElementRefByID(id); ref.IsValid(); ref = app.elementStorage.GetParentRef(ref))
                        if (app.elementStorage.GetElementUnsafe(ref).id == row)
                        {
                            if (seen++ == n) return app.CenterOf(id);
                            break;
                        }
                ADD_FAILURE() << "no such element in the row";
                return {};
            }
            ID Focused() const { return app.guiStateManager.Interaction().GetFocusedID(); }
            // A point on the ruler at `time`.
            int2 RulerPoint(float time) const
            {
                const ElementRect* r = app.elementStorage.GetElementRectPtr(
                    app.elementStorage.FindElementRefByID(WithStyle(app.theme.timelineRulerStyleRef)));
                const TimelineRange shown = InView();
                const float x = float(r->innerRect.x) + (time - shown.start) / (shown.end - shown.start) * float(r->innerRect.w);
                return { int32_t(std::lround(x)), r->innerRect.y + r->innerRect.h / 2 };
            }

            void Click(int2 at, KeyModifier mods = KeyModifier::None)
            {
                app.ClickAt(at, MouseButtons::Left, mods);
                Frame();
            }
            void Click(IntRect r) { Click(int2{ r.x + r.w / 2, r.y + r.h / 2 }); }
            void Drag(int2 from, int2 to, KeyModifier mods = KeyModifier::None)
            {
                app.InjectMouseMove(from, MouseButtons::None, mods);
                app.InjectMouseDown(from, MouseButtons::Left, mods);
                Frame();
                app.InjectMouseMove({ (from.x + to.x) / 2, (from.y + to.y) / 2 }, MouseButtons::Left, mods);
                Frame();
                app.InjectMouseMove(to, MouseButtons::Left, mods);
                Frame();
                app.InjectMouseUp(to, MouseButtons::Left, mods);
                Frame();
            }
            void Key(KeyCode key, KeyModifier mods = KeyModifier::None)
            {
                app.InjectKey(key, KeyAction::KeyDown, mods);
                Frame();
                app.InjectKey(key, KeyAction::KeyUp, mods);
                Frame();
            }

            // The lanes' padding box: the time in view, across.
            IntRect LanesRange() const
            {
                const ElementRect* r = app.elementStorage.GetElementRectPtr(
                    app.elementStorage.FindElementRefByID(WithStyle(app.theme.timelineLanesStyleRef)));
                return r ? r->innerRect : IntRect{};
            }
            TimelineRange InView() const
            {
                const float span = view.viewLength > 0.0f ? view.viewLength : view.duration;
                return { view.viewStart, view.viewStart + span };
            }
            // A point on the lanes: at `time`, in the middle of shown row `row`.
            int2 LanesPoint(float time, int row) const
            {
                const IntRect r = LanesRange();
                const TimelineRange shown = InView();
                const float x = float(r.x) + (time - shown.start) / (shown.end - shown.start) * float(r.w);
                return { int32_t(std::lround(x)), r.y + row * app.styleManager.Px(28) + app.styleManager.Px(14) };
            }
            float TimeAtLanes(int32_t x) const
            {
                const IntRect r = LanesRange();
                const TimelineRange shown = InView();
                return shown.start + float(x - r.x) / float(r.w) * (shown.end - shown.start);
            }
        };

        // In a panel, it takes the panel's height, as a node view does: the body grows.
        TEST_F(TimelineEditorTest, ItFillsAPanel)
        {
            const ID panel = app.idManager.CreateStaticID();
            for (int i = 0; i < 2; ++i)
                app.Frame([&] {
                    app.BeginRootWindow({ 1600, 900 }, ElementFlags::None, app.theme.mainWindowStyleRef);
                    app.BeginArea(panel, app.theme.dockContentStyleRef);
                    app.TimelineEditor(editorID, view, LayoutOverride{}.FlexGrow(1));
                    app.EndArea();
                    app.EndRootWindow();
                });
            const IntRect inner = app.elementStorage.GetElementRectPtr(app.elementStorage.FindElementRefByID(panel))->innerRect;
            EXPECT_GT(inner.h, 800) << "test setup: the panel fills the window";
            EXPECT_EQ(app.GetElementRect(editorID).h, inner.h);
            // The scroll bar at its bottom: the body between grows.
            const IntRect e = app.GetElementRect(editorID), s = app.GetElementRect(WithStyle(app.theme.timelineScrollStyleRef));
            EXPECT_NEAR(s.y + s.h, e.y + e.h, 2);
            // The lanes and the list run down to it, however few the rows.
            const IntRect lanes = app.GetElementRect(WithStyle(app.theme.timelineLanesStyleRef));
            const IntRect list  = app.GetElementRect(WithStyle(app.theme.timelineListStyleRef));
            EXPECT_NEAR(lanes.y + lanes.h, s.y, 2);
            EXPECT_NEAR(list.y + list.h, s.y, 2);
        }

        // The wheel over the lanes zooms round the time under the pointer; with Shift it pans.
        // Over the ruler too.
        TEST_F(TimelineEditorTest, TheWheelZoomsAndShiftPans)
        {
            Settle();
            const int2 at = LanesPoint(2.0f, 1);
            app.InjectMouseScroll(at, { 0, 1 });
            Frame();
            ASSERT_GT(view.viewLength, 0.0f);
            EXPECT_NEAR(view.viewLength, 8.0f / 1.25f, 1e-3f) << "in by a step";
            Frame();
            EXPECT_NEAR(TimeAtLanes(at.x), 2.0f, 0.02f) << "the time under the pointer stays there";
            const float zoomed = view.viewLength;

            app.InjectMouseScroll(at, { 0, 1 }, KeyModifier::Shift);
            Frame();
            EXPECT_EQ(view.viewLength, zoomed) << "Shift pans, not zooms";
            const float startAfterPan = view.viewStart;
            app.InjectMouseScroll(at, { 0, -1 }, KeyModifier::Shift);
            Frame();
            EXPECT_GT(view.viewStart, startAfterPan) << "the other way";

            // Out again, past the whole length: all of it.
            for (int i = 0; i < 10; ++i)
            {
                app.InjectMouseScroll(at, { 0, -1 });
                Frame();
            }
            EXPECT_EQ(view.viewLength, 0.0f);
            EXPECT_EQ(view.viewStart, 0.0f);

            // Over the ruler, the same.
            const IntRect ruler = Rect(app.theme.timelineRulerStyleRef);
            app.InjectMouseScroll({ ruler.x + ruler.w / 2, ruler.y + ruler.h / 2 }, { 0, 1 });
            Frame();
            EXPECT_GT(view.viewLength, 0.0f);
        }

        // A drag with the right button pans along the time and scrolls the rows; Escape puts
        // both back; a right click that doesn't move does neither.
        TEST_F(TimelineEditorTest, TheMiddleButtonDragsTheViewAround)
        {
            // Enough rows to scroll.
            for (int i = 0; i < 40; ++i)
                view.tracks.push_back(TimelineTrack{ .stableID = app.idManager.CreateStaticID(), .name = "More" });
            view.viewStart = 2.0f;
            view.viewLength = 2.0f;
            Settle();
            auto& scrolling = app.guiStateManager.GetScrollManagement();
            const ScrollOffsetHandle body = scrolling.GetOrCreateScrollOffsetHandle(WithStyle(app.theme.timelineBodyStyleRef));
            const int2 from = LanesPoint(3.0f, 3);

            app.InjectMouseMove(from);
            app.InjectMouseDown(from, MouseButtons::Middle);
            Frame();
            app.InjectMouseMove({ from.x - 50, from.y - 30 }, MouseButtons::Middle);
            Frame();
            app.InjectMouseMove({ from.x - 100, from.y - 60 }, MouseButtons::Middle);
            Frame();
            const float perPixel = 2.0f / float(LanesRange().w);
            EXPECT_NEAR(view.viewStart, 2.0f + 100.0f * perPixel, 1e-3f) << "the time follows the pointer";
            EXPECT_EQ(scrolling.GetScrollOffset(body).y, -60) << "the rows follow it up";
            app.InjectKey(KeyCode::Escape, KeyAction::KeyDown);
            Frame();
            EXPECT_EQ(view.viewStart, 2.0f);
            EXPECT_EQ(scrolling.GetScrollOffset(body).y, 0);
            app.InjectMouseUp({ from.x - 100, from.y - 60 }, MouseButtons::Middle);
            Frame();

            // Not past the ends: the rows don't scroll down past the top.
            app.InjectMouseMove(from);
            app.InjectMouseDown(from, MouseButtons::Middle);
            Frame();
            app.InjectMouseMove({ from.x + 2000, from.y + 200 }, MouseButtons::Middle);
            Frame();
            EXPECT_EQ(view.viewStart, 0.0f);
            EXPECT_EQ(scrolling.GetScrollOffset(body).y, 0);
            app.InjectMouseUp({ from.x + 2000, from.y + 200 }, MouseButtons::Middle);
            Frame();

            // A middle click that doesn't move.
            app.ClickAt(from, MouseButtons::Middle);
            Frame();
            EXPECT_EQ(view.viewStart, 0.0f);
            EXPECT_TRUE(edits.empty());
        }

        // Alt + left drags the view as the middle button does; a right drag doesn't.
        TEST_F(TimelineEditorTest, AltLeftDragsTheViewAndARightDragDoesnt)
        {
            view.viewStart = 2.0f;
            view.viewLength = 2.0f;
            Settle();
            const int2 from = LanesPoint(3.0f, 1);
            app.DragFrom(from, { from.x - 100, from.y }, MouseButtons::Right);
            Frame();
            EXPECT_EQ(view.viewStart, 2.0f) << "the right button doesn't drag the view";

            app.InjectMouseMove(from, MouseButtons::None, KeyModifier::Alt);
            app.InjectMouseDown(from, MouseButtons::Left, KeyModifier::Alt);
            Frame();
            app.InjectMouseMove({ from.x - 100, from.y }, MouseButtons::Left, KeyModifier::Alt);
            Frame();
            app.InjectMouseUp({ from.x - 100, from.y }, MouseButtons::Left, KeyModifier::Alt);
            Frame();
            const float perPixel = 2.0f / float(LanesRange().w);
            EXPECT_NEAR(view.viewStart, 2.0f + 100.0f * perPixel, 1e-3f);
            EXPECT_TRUE(edits.empty()) << "no key moved";
        }

        // With the wheel set to scroll: it scrolls along the time sideways, Ctrl zooms; up and
        // down is the rows' scroll area's. A pinch zooms.
        TEST_F(TimelineEditorTest, TheWheelScrollsInPanMode)
        {
            app.SetWheelMode(WheelMode::Pan);
            view.viewStart = 2.0f;
            view.viewLength = 2.0f;
            Settle();
            const int2 at = LanesPoint(3.0f, 1);
            app.InjectMouseMove(at);
            app.InjectMouseScroll(at, { 0, -1 }, KeyModifier::Shift);
            Frame();
            EXPECT_GT(view.viewStart, 2.0f) << "Shift: later";
            EXPECT_FLOAT_EQ(view.viewLength, 2.0f) << "no zoom";
            app.InjectMouseScroll(at, { 0, 1 }, KeyModifier::Control);
            Frame();
            EXPECT_LT(view.viewLength, 2.0f) << "Ctrl zooms in";
            const float zoomed = view.viewLength;
            app.InjectPinch(0.5f);
            Frame();
            EXPECT_GT(view.viewLength, zoomed) << "a pinch together zooms out";
        }

        TEST_F(TimelineEditorTest, InPanModeUpAndDownScrollsTheRows)
        {
            app.SetWheelMode(WheelMode::Pan);
            for (int i = 0; i < 40; ++i)
                view.tracks.push_back(TimelineTrack{ .stableID = app.idManager.CreateStaticID(), .name = "More" });
            view.viewStart = 2.0f;
            view.viewLength = 2.0f;
            Settle();
            auto& scrolling = app.guiStateManager.GetScrollManagement();
            const ScrollOffsetHandle body = scrolling.GetOrCreateScrollOffsetHandle(WithStyle(app.theme.timelineBodyStyleRef));
            const int2 at = LanesPoint(3.0f, 2);
            app.InjectMouseMove(at);
            app.InjectMouseScroll(at, { 0, -1 });
            Frame();
            EXPECT_LT(scrolling.GetScrollOffset(body).y, 0) << "toward the user: the rows scroll down";
            EXPECT_EQ(view.viewStart, 2.0f) << "the time stays";
            EXPECT_FLOAT_EQ(view.viewLength, 2.0f);
        }

        // ---------------------------------------------------------------------
        // The edits
        // ---------------------------------------------------------------------

        // A key goes in on the nearest frame, in time order; not twice on a frame, not on a
        // locked track, only on a property.
        TEST_F(TimelineEditorTest, AddKeyEdit)
        {
            const ID key = app.idManager.CreateStaticID();
            const TimelineEdit add = AddKeyEdit(view, 1, 3.01f, key);
            ASSERT_EQ(add.changes.size(), 1u);
            ApplyTimelineEdit(view, add);
            ASSERT_EQ(view.tracks[1].keys.size(), 4u);
            EXPECT_EQ(view.tracks[1].keys[2].stableID, key) << "between 2 s and 4 s";
            EXPECT_FLOAT_EQ(view.tracks[1].keys[2].time, 3.0f) << "on a frame";
            EXPECT_TRUE(AddKeyEdit(view, 1, 3.005f, app.idManager.CreateStaticID()).empty()) << "a key there already";
            ApplyTimelineEdit(view, add, /*undo*/ true);
            EXPECT_EQ(view.tracks[1].keys.size(), 3u);

            view.snap = false;
            ApplyTimelineEdit(view, AddKeyEdit(view, 1, 3.01f, key));
            EXPECT_FLOAT_EQ(view.tracks[1].keys[2].time, 3.01f) << "where it's asked, without snapping";

            EXPECT_TRUE(AddKeyEdit(view, 0, 1.0f, key).empty()) << "a group";
            EXPECT_TRUE(AddKeyEdit(view, 5, 1.0f, key).empty()) << "audio";
            EXPECT_TRUE(AddKeyEdit(view, 99, 1.0f, key).empty());
            view.tracks[0].locked = true;
            EXPECT_TRUE(AddKeyEdit(view, 1, 6.0f, key).empty()) << "in a locked group";
        }

        // The selected keys and events go, but not those of a locked track; undone, they're back.
        TEST_F(TimelineEditorTest, DeleteSelectedKeysEdit)
        {
            view.selectedKeys = { positionKeys[1], scaleKeys[0], spawn };
            view.tracks[2].locked = true;   // Scale
            const std::vector<TimelineTrack> before = view.tracks;
            const TimelineEdit edit = DeleteSelectedKeysEdit(view);
            ASSERT_EQ(edit.changes.size(), 2u) << "Position and Events, not the locked Scale";
            ApplyTimelineEdit(view, edit);
            EXPECT_EQ(view.tracks[1].keys.size(), 2u);
            EXPECT_EQ(view.tracks[2].keys.size(), 2u);
            EXPECT_EQ(view.tracks[6].events.size(), 1u);
            EXPECT_EQ(view.selectedKeys, std::unordered_set<ID>{ scaleKeys[0] }) << "what's gone isn't selected";
            ApplyTimelineEdit(view, edit, /*undo*/ true);
            EXPECT_EQ(view.tracks, before);
            view.selectedKeys.clear();
            EXPECT_TRUE(DeleteSelectedKeysEdit(view).empty());
        }

        // A group goes with the tracks it holds, and comes back in its place.
        TEST_F(TimelineEditorTest, AddingAndRemovingTracks)
        {
            const std::vector<TimelineTrack> before = view.tracks;
            const TimelineEdit remove = RemoveTrackEdit(view, 0);
            ApplyTimelineEdit(view, remove);
            ASSERT_EQ(view.tracks.size(), 4u);
            EXPECT_EQ(view.tracks[0].stableID, light);
            ApplyTimelineEdit(view, remove, /*undo*/ true);
            EXPECT_EQ(view.tracks, before);

            const ID added = app.idManager.CreateStaticID();
            const TimelineEdit add = AddTrackEdit(view, TimelineTrack{ .stableID = added, .name = "Color", .depth = 1 }, 3);
            ApplyTimelineEdit(view, add);
            EXPECT_EQ(view.tracks[3].stableID, added);
            EXPECT_EQ(TracksInGroup(view, 0).size(), 3u) << "in Sphere";
            ApplyTimelineEdit(view, add, /*undo*/ true);
            EXPECT_EQ(view.tracks, before);
            EXPECT_EQ(AddTrackEdit(view, TimelineTrack{}, -1).tracks[0].index, 7) << "no place: the end";
            EXPECT_TRUE(RemoveTrackEdit(view, 99).empty());
        }

        // An edit that adds a track and changes one after it: the change is at the index the
        // track has with the new one in place, so undo reverts it before taking the new one out.
        TEST_F(TimelineEditorTest, AddAndChangeInOneEdit)
        {
            const std::vector<TimelineTrack> before = view.tracks;
            TimelineTrack renamed = view.tracks[0];
            renamed.name = "Ball";
            TimelineEdit edit = AddTrackEdit(view, TimelineTrack{ .stableID = app.idManager.CreateStaticID(), .name = "Root" }, 0);
            edit.changes.push_back({ .track = 1, .from = view.tracks[0], .to = renamed });

            ApplyTimelineEdit(view, edit);
            ASSERT_EQ(view.tracks.size(), before.size() + 1);
            EXPECT_EQ(view.tracks[0].name, "Root");
            EXPECT_EQ(view.tracks[1].name, "Ball");
            EXPECT_EQ(view.tracks[2].stableID, before[1].stableID);
            ApplyTimelineEdit(view, edit, /*undo*/ true);
            EXPECT_EQ(view.tracks, before);
        }

        TEST_F(TimelineEditorTest, GroupsAndLocks)
        {
            EXPECT_EQ(TracksInGroup(view, 0).size(), 2u);
            EXPECT_EQ(TracksInGroup(view, 3).size(), 1u);
            EXPECT_TRUE(TracksInGroup(view, 1).empty()) << "not a group";
            EXPECT_FALSE(IsTrackLocked(view, 1));
            view.tracks[0].locked = true;
            EXPECT_TRUE(IsTrackLocked(view, 0));
            EXPECT_TRUE(IsTrackLocked(view, 2)) << "in a locked group";
            EXPECT_FALSE(IsTrackLocked(view, 4)) << "in another group";
            EXPECT_FALSE(IsTrackLocked(view, 5));
            view.tracks[0].locked = false;
            view.tracks[5].locked = true;
            EXPECT_TRUE(IsTrackLocked(view, 5)) << "locked itself";
        }

        // The loop range is an edit too.
        TEST_F(TimelineEditorTest, TheLoopRangeUndoes)
        {
            const TimelineEdit edit{ .loopFrom = TimelineRange{}, .loopTo = TimelineRange{ 1.0f, 3.0f } };
            CommitTimelineEdit(view, edit);
            EXPECT_EQ(view.loopRange, (TimelineRange{ 1.0f, 3.0f }));
            ASSERT_EQ(edits.size(), 1u);
            ApplyTimelineEdit(view, edit, /*undo*/ true);
            EXPECT_TRUE(view.loopRange.empty());
            CommitTimelineEdit(view, TimelineEdit{});
            EXPECT_EQ(edits.size(), 1u) << "an empty one isn't reported";
        }

        // ---------------------------------------------------------------------
        // The track list
        // ---------------------------------------------------------------------

        // A collapsed group hides what it holds; its chevron opens it. The filter keeps the
        // tracks whose name has it, with their groups.
        TEST_F(TimelineEditorTest, CollapsedGroupsAndTheFilter)
        {
            Settle();
            EXPECT_TRUE(app.HasElement(sphere));
            EXPECT_TRUE(app.HasElement(position));
            EXPECT_FALSE(app.HasElement(intensity)) << "inside a collapsed group";
            EXPECT_TRUE(app.HasElement(music));
            EXPECT_TRUE(app.HasElement(events));

            Click(InRow(light, app.theme.timelineChevronStyleRef));
            Settle();
            EXPECT_TRUE(view.tracks[3].expanded);
            EXPECT_TRUE(app.HasElement(intensity));
            EXPECT_TRUE(edits.empty()) << "how it's shown, not a change to undo";

            // A collapsed group hides only its own tracks: the next group's show.
            Click(InRow(sphere, app.theme.timelineChevronStyleRef));
            Settle();
            EXPECT_FALSE(view.tracks[0].expanded);
            EXPECT_FALSE(app.HasElement(position));
            EXPECT_TRUE(app.HasElement(light));
            EXPECT_TRUE(app.HasElement(intensity)) << "in Light, which is open";
            view.tracks[0].expanded = true;

            view.filter = "SCA";
            Settle();
            EXPECT_TRUE(app.HasElement(sphere)) << "the group of a match";
            EXPECT_TRUE(app.HasElement(scale));
            EXPECT_FALSE(app.HasElement(position));
            EXPECT_FALSE(app.HasElement(light));
            EXPECT_FALSE(app.HasElement(music));
        }

        // The row buttons: eye, lock, mute and solo as edits; a property's key at the playhead.
        TEST_F(TimelineEditorTest, TheRowButtons)
        {
            Settle();
            const std::vector<TimelineTrack> before = view.tracks;
            Click(InRow(sphere, app.theme.timelineRowButtonStyleRef, 0));
            EXPECT_FALSE(view.tracks[0].visible);
            Click(InRow(sphere, app.theme.timelineRowButtonStyleRef, 1));
            EXPECT_TRUE(view.tracks[0].locked);
            Click(InRow(music, app.theme.timelineMuteStyleRef));
            EXPECT_TRUE(view.tracks[5].muted);
            Click(InRow(music, app.theme.timelineSoloStyleRef));
            EXPECT_TRUE(view.tracks[5].solo);
            ASSERT_EQ(edits.size(), 4u);
            for (auto it = edits.rbegin(); it != edits.rend(); ++it)
                ApplyTimelineEdit(view, *it, /*undo*/ true);
            EXPECT_EQ(view.tracks, before);

            view.playback.time = 3.0f;
            Settle();
            Click(InRow(scale, app.theme.timelineRowButtonStyleRef));
            ASSERT_EQ(view.tracks[2].keys.size(), 3u);
            EXPECT_FLOAT_EQ(view.tracks[2].keys[1].time, 3.0f);

            // Locked, a property's key button does nothing.
            view.tracks[0].locked = true;
            Settle();
            view.playback.time = 5.0f;
            Click(InRow(scale, app.theme.timelineRowButtonStyleRef));
            EXPECT_EQ(view.tracks[2].keys.size(), 3u);
        }

        // A click on a row selects its track; "+" asks the app for a track; a right click
        // opens the app's menu for the row.
        TEST_F(TimelineEditorTest, RowsSelectAddAndOpenTheirMenu)
        {
            int added = 0, menuFor = -1;
            view.onAddTrack = [&] { ++added; };
            view.onTrackContextMenu = [&](int track) { menuFor = track; app.MenuItem("Delete track", [] {}); };
            Settle();
            Click(app.CenterOf(music));
            EXPECT_EQ(view.selectedTrack, music);
            Click(Rect(app.theme.timelineAddTrackStyleRef));
            EXPECT_EQ(added, 1);
            app.InjectMouseMove(app.CenterOf(scale));
            Frame();
            app.ClickAt(app.CenterOf(scale), MouseButtons::Right);
            Settle();
            EXPECT_EQ(menuFor, 2);
            EXPECT_EQ(view.selectedTrack, scale);
        }

        // ---------------------------------------------------------------------
        // The lanes
        // ---------------------------------------------------------------------

        // The key, event or summary key under the pointer is the hovered one, for its hover look.
        TEST_F(TimelineEditorTest, TheKeyUnderThePointerIsHovered)
        {
            Settle();
            app.InjectMouseMove(LanesPoint(2.0f, 1));
            Frame();
            ASSERT_GE(view.hover.part.track, 0);
            EXPECT_EQ(view.tracks[size_t(view.hover.part.track)].stableID, position);
            EXPECT_FLOAT_EQ(view.hover.part.time, 2.0f);
            app.InjectMouseMove(LanesPoint(3.0f, 1));
            Frame();
            EXPECT_EQ(view.hover.part.track, -1) << "the empty lane";
            app.InjectMouseMove(LanesPoint(5.0f, 5));
            Frame();
            EXPECT_FLOAT_EQ(view.hover.part.time, 5.0f) << "an event, as a key";
        }

        // ---------------------------------------------------------------------
        // The sound's clip and the events' labels
        // ---------------------------------------------------------------------

        const TimelineClip& Clip(const TimelineView& view) { return *view.tracks[5].clip; }

        // Its body drags it along, onto frames, as one edit; Escape puts it back.
        TEST_F(TimelineEditorTest, DraggingTheClipMovesIt)
        {
            Settle();
            Drag(LanesPoint(3.0f, 4), LanesPoint(4.0f, 4));
            EXPECT_NEAR(Clip(view).start, 1.5f, 0.05f);
            EXPECT_FLOAT_EQ(Clip(view).start * 30.0f, std::round(Clip(view).start * 30.0f)) << "on a frame";
            EXPECT_FLOAT_EQ(Clip(view).length, 6.0f) << "as long";
            EXPECT_EQ(edits.size(), 1u);

            const float before = Clip(view).start;
            app.InjectMouseMove(LanesPoint(3.0f, 4));
            app.InjectMouseDown(LanesPoint(3.0f, 4));
            Frame();
            app.InjectMouseMove(LanesPoint(4.0f, 4), MouseButtons::Left);
            Frame();
            app.InjectKey(KeyCode::Escape);
            Frame();
            app.InjectMouseUp(LanesPoint(4.0f, 4));
            Frame();
            EXPECT_FLOAT_EQ(Clip(view).start, before) << "put back";
            EXPECT_EQ(edits.size(), 1u);
        }

        // Its ends trim it: the end shortens it, the start plays the sound from further in.
        // Neither goes past the sound's own ends.
        TEST_F(TimelineEditorTest, DraggingTheClipsEndsTrimsIt)
        {
            Settle();
            Drag(LanesPoint(6.5f, 4), LanesPoint(5.5f, 4));
            EXPECT_FLOAT_EQ(Clip(view).start, 0.5f);
            EXPECT_NEAR(Clip(view).length, 5.0f, 0.05f);

            Drag(LanesPoint(0.5f, 4), LanesPoint(1.5f, 4));
            EXPECT_NEAR(Clip(view).start, 1.5f, 0.05f);
            EXPECT_NEAR(Clip(view).offset, 1.0f, 0.05f) << "the sound from a second in";
            EXPECT_NEAR(Clip(view).length, 4.0f, 0.05f) << "the end stays";
            EXPECT_FLOAT_EQ(Clip(view).sourceLength, 6.0f) << "the whole sound, remembered";

            const float end = Clip(view).start + Clip(view).length;
            Drag(LanesPoint(end, 4), LanesPoint(7.9f, 4));
            EXPECT_NEAR(Clip(view).length, 5.0f, 0.05f) << "no further than the sound goes";
            Drag(LanesPoint(Clip(view).start, 4), LanesPoint(0.1f, 4));
            EXPECT_NEAR(Clip(view).offset, 0.0f, 0.05f) << "no earlier than the sound starts";
            EXPECT_NEAR(Clip(view).start, 0.5f, 0.05f);
            EXPECT_EQ(edits.size(), 4u);
        }

        // What part of the clip is under the pointer, for its hover look.
        TEST_F(TimelineEditorTest, TheClipsPartsShowHover)
        {
            Settle();
            app.InjectMouseMove(LanesPoint(3.0f, 4));
            Frame();
            EXPECT_EQ(view.hover.part.clipTrack, 5);
            EXPECT_EQ(view.hover.part.clipPart, TimelineView::ClipPart::Body);
            app.InjectMouseMove(LanesPoint(0.5f, 4));
            Frame();
            EXPECT_EQ(view.hover.part.clipPart, TimelineView::ClipPart::Start);
            app.InjectMouseMove(LanesPoint(6.5f, 4));
            Frame();
            EXPECT_EQ(view.hover.part.clipPart, TimelineView::ClipPart::End);
            app.InjectMouseMove(LanesPoint(7.5f, 4));
            Frame();
            EXPECT_EQ(view.hover.part.clipPart, TimelineView::ClipPart::None);
        }

        // An event's label is the event too: hovering it, and dragging it by it.
        TEST_F(TimelineEditorTest, AnEventsLabelIsTheEvent)
        {
            Settle();
            const int2 label = LanesPoint(5.0f, 5) + int2{ app.styleManager.Px(20), 0 };
            app.InjectMouseMove(label);
            Frame();
            EXPECT_EQ(view.hover.part.track, 6);
            EXPECT_FLOAT_EQ(view.hover.part.time, 5.0f);
            Drag(label, label + int2{ LanesPoint(6.0f, 5).x - LanesPoint(5.0f, 5).x, 0 });
            EXPECT_NEAR(view.tracks[6].events[1].time, 6.0f, 0.05f);
        }

        // A click selects a key; Shift adds, Ctrl toggles; on the empty lanes it clears. A
        // group's summary key selects the keys at its time.
        TEST_F(TimelineEditorTest, ClicksSelectKeys)
        {
            Settle();
            Click(LanesPoint(2.0f, 1));
            EXPECT_EQ(view.selectedKeys, std::unordered_set<ID>{ positionKeys[1] });
            EXPECT_EQ(view.selectedTrack, position);
            Click(LanesPoint(1.0f, 2), KeyModifier::Shift);
            EXPECT_EQ(view.selectedKeys, (std::unordered_set<ID>{ positionKeys[1], scaleKeys[0] }));
            Click(LanesPoint(2.0f, 1), KeyModifier::Control);
            EXPECT_EQ(view.selectedKeys, std::unordered_set<ID>{ scaleKeys[0] });
            Click(LanesPoint(5.0f, 5));
            EXPECT_EQ(view.selectedKeys, std::unordered_set<ID>{ explode }) << "an event, as a key";
            Click(LanesPoint(3.0f, 1));
            EXPECT_TRUE(view.selectedKeys.empty()) << "the empty lanes";

            Click(LanesPoint(4.0f, 0));
            EXPECT_EQ(view.selectedKeys, (std::unordered_set<ID>{ positionKeys[2], scaleKeys[1] })) << "the group's keys at 4 s";
            EXPECT_TRUE(edits.empty());
        }

        // A drag on the empty lanes selects the keys in its box (with Shift, adds them).
        TEST_F(TimelineEditorTest, ABoxSelects)
        {
            Settle();
            Drag(LanesPoint(0.5f, 1), LanesPoint(4.5f, 2));
            EXPECT_EQ(view.selectedKeys, (std::unordered_set<ID>{ positionKeys[1], positionKeys[2], scaleKeys[0], scaleKeys[1] }));
            Drag(LanesPoint(1.0f, 5), LanesPoint(2.0f, 5), KeyModifier::Shift);
            EXPECT_EQ(view.selectedKeys.size(), 5u) << "and Spawn";
            EXPECT_TRUE(view.selectedKeys.contains(spawn));
        }

        // A drag on a selected key moves the selection by whole frames, as one edit; a locked
        // track's keys stay; Escape puts them back.
        TEST_F(TimelineEditorTest, DraggingMovesTheSelection)
        {
            view.selectedKeys = { positionKeys[1], scaleKeys[0] };
            Settle();
            const int2 from = LanesPoint(2.0f, 1);
            Drag(from, { LanesPoint(2.51f, 1).x, from.y });
            ASSERT_EQ(edits.size(), 1u);
            EXPECT_EQ(edits[0].changes.size(), 2u);
            EXPECT_NEAR(view.tracks[1].keys[1].time, 2.5f, 1e-4f);
            EXPECT_NEAR(view.tracks[2].keys[0].time, 1.5f, 1e-4f);
            EXPECT_NEAR(std::fmod(view.tracks[1].keys[1].time * 30.0f + 0.5f, 1.0f), 0.5f, 1e-3f) << "on a frame";
            ApplyTimelineEdit(view, edits[0], /*undo*/ true);
            EXPECT_EQ(view.tracks[1].keys[1].time, 2.0f);

            // Past the next key: they stay in time order.
            Settle();
            Drag(from, LanesPoint(4.5f, 1));
            EXPECT_EQ(view.tracks[1].keys[2].stableID, positionKeys[1]);

            // Locked: Scale's stays. Escape mid-drag: back where they were.
            ApplyTimelineEdit(view, edits.back(), /*undo*/ true);
            view.tracks[0].locked = true;
            view.tracks[0].locked = false;
            view.tracks[2].locked = true;
            Settle();
            const size_t before = edits.size();
            Drag(from, LanesPoint(3.0f, 1));
            EXPECT_NEAR(view.tracks[1].keys[1].time, 3.0f, 1e-4f);
            EXPECT_EQ(view.tracks[2].keys[0].time, 1.0f) << "locked";
            ApplyTimelineEdit(view, edits.back(), /*undo*/ true);
            view.tracks[2].locked = false;
            Settle();
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove(LanesPoint(5.0f, 1), MouseButtons::Left);
            Frame();
            EXPECT_NEAR(view.tracks[1].keys[2].time, 5.0f, 1e-4f) << "moved while dragged";
            app.InjectKey(KeyCode::Escape, KeyAction::KeyDown);
            Frame();
            EXPECT_EQ(view.tracks[1].keys[1].time, 2.0f);
            app.InjectMouseUp(LanesPoint(5.0f, 1));
            Frame();
            EXPECT_EQ(edits.size(), before + 1) << "no edit for the cancelled drag";
        }

        // Keys on the focused lanes.
        TEST_F(TimelineEditorTest, TheLanesTakeKeys)
        {
            Settle();
            Click(LanesPoint(2.0f, 1));
            ASSERT_EQ(Focused(), WithStyle(app.theme.timelineLanesStyleRef));

            // Right / Left move the selected keys a frame, as edits.
            Key(KeyCode::Right);
            EXPECT_NEAR(view.tracks[1].keys[1].time, 2.0f + 1.0f / 30.0f, 1e-4f);
            Key(KeyCode::Left);
            EXPECT_NEAR(view.tracks[1].keys[1].time, 2.0f, 1e-4f);
            EXPECT_EQ(edits.size(), 2u);

            // Delete; Ctrl+A; Escape.
            Key(KeyCode::Delete);
            EXPECT_EQ(view.tracks[1].keys.size(), 2u);
            Key(KeyCode::KeyA, KeyModifier::Control);
            EXPECT_EQ(view.selectedKeys.size(), 2u + 2u + 1u + 2u) << "every key and event";
            Key(KeyCode::Escape);
            EXPECT_TRUE(view.selectedKeys.empty());

            // With none selected, Left / Right move the playhead; K keys the selected track there.
            view.playback.time = 3.0f;
            Key(KeyCode::Right);
            EXPECT_NEAR(view.playback.time, 3.0f + 1.0f / 30.0f, 1e-4f);
            Key(KeyCode::Left);
            Key(KeyCode::KeyK);
            ASSERT_EQ(view.tracks[1].keys.size(), 3u);
            EXPECT_NEAR(view.tracks[1].keys[1].time, 3.0f, 1e-4f);

            // I and O set the loop range at the playhead.
            view.playback.time = 1.0f;
            Key(KeyCode::KeyI);
            view.playback.time = 5.0f;
            Key(KeyCode::KeyO);
            EXPECT_EQ(view.loopRange, (TimelineRange{ 1.0f, 5.0f }));

            // Space, Home, End.
            Key(KeyCode::Space);
            EXPECT_TRUE(view.playback.playing);
            Key(KeyCode::Home);
            EXPECT_EQ(view.playback.time, 0.0f);
            Key(KeyCode::End);
            EXPECT_EQ(view.playback.time, 8.0f);
            EXPECT_FALSE(view.playback.playing);
        }

        // ---------------------------------------------------------------------
        // The ruler, the scroll bar and the toolbar
        // ---------------------------------------------------------------------

        // A press on the ruler puts the playhead there, on a frame, and a drag moves it;
        // Escape puts it back.
        TEST_F(TimelineEditorTest, TheRulerMovesThePlayhead)
        {
            int changes = 0;
            view.onPlaybackChanged = [&] { ++changes; };
            Settle();
            app.InjectMouseMove(RulerPoint(3.01f));
            app.InjectMouseDown(RulerPoint(3.01f));
            Frame();
            EXPECT_NEAR(view.playback.time, 3.0f, 1.0f / 60.0f);
            EXPECT_NEAR(view.playback.time * 30.0f, std::round(view.playback.time * 30.0f), 1e-3f);
            app.InjectMouseMove(RulerPoint(6.0f), MouseButtons::Left);
            Frame();
            EXPECT_NEAR(view.playback.time, 6.0f, 1.0f / 60.0f);
            app.InjectKey(KeyCode::Escape, KeyAction::KeyDown);
            Frame();
            EXPECT_EQ(view.playback.time, 0.0f);
            app.InjectMouseUp(RulerPoint(6.0f));
            Frame();
            EXPECT_GE(changes, 3);
            EXPECT_TRUE(edits.empty()) << "the playhead isn't an edit";
        }

        // Shift + a drag on the ruler sets the loop range, as one edit; its ends drag; Ctrl +
        // a click clears it.
        TEST_F(TimelineEditorTest, TheRulerSetsTheLoopRange)
        {
            Settle();
            Drag(RulerPoint(1.0f), RulerPoint(5.0f), KeyModifier::Shift);
            EXPECT_NEAR(view.loopRange.start, 1.0f, 1.0f / 60.0f);
            EXPECT_NEAR(view.loopRange.end, 5.0f, 1.0f / 60.0f);
            ASSERT_EQ(edits.size(), 1u);
            EXPECT_TRUE(edits[0].loopFrom->empty());
            EXPECT_EQ(view.playback.time, 0.0f) << "not a scrub";

            Settle();
            Drag(RulerPoint(view.loopRange.end), RulerPoint(6.0f));
            EXPECT_NEAR(view.loopRange.end, 6.0f, 1.0f / 60.0f);
            EXPECT_NEAR(view.loopRange.start, 1.0f, 1.0f / 60.0f);
            EXPECT_EQ(edits.size(), 2u);

            Settle();
            Click(RulerPoint(3.0f), KeyModifier::Control);
            EXPECT_TRUE(view.loopRange.empty());
            EXPECT_EQ(edits.size(), 3u);
            ApplyTimelineEdit(view, edits[2], /*undo*/ true);
            EXPECT_NEAR(view.loopRange.end, 6.0f, 1.0f / 60.0f);
        }

        // The loop edge under the pointer on the ruler, for its hover look.
        TEST_F(TimelineEditorTest, TheRulersLoopEdgesShowHover)
        {
            view.loopRange = { 2.0f, 5.0f };
            Settle();
            app.InjectMouseMove(RulerPoint(2.0f));
            Frame();
            EXPECT_EQ(view.loopEdgeHover.part, 1);
            app.InjectMouseMove(RulerPoint(5.0f));
            Frame();
            EXPECT_EQ(view.loopEdgeHover.part, 2);
            app.InjectMouseMove(RulerPoint(3.5f));
            Frame();
            EXPECT_EQ(view.loopEdgeHover.part, 0);
        }

        // Zoomed in, the scroll bar's thumb drags the view along; a press beside it brings it there.
        TEST_F(TimelineEditorTest, TheScrollBarPans)
        {
            view.viewStart = 0.0f;
            view.viewLength = 2.0f;
            Settle();
            const ElementRect* bar = app.elementStorage.GetElementRectPtr(app.elementStorage.FindElementRefByID(WithStyle(app.theme.timelineScrollStyleRef)));
            ASSERT_NE(bar, nullptr);
            const IntRect track = bar->innerRect;
            const int y = track.y + track.h / 2;
            const int thumb = track.x + track.w / 8;   // the thumb's middle: 1 s of 8
            Drag({ thumb, y }, { thumb + track.w / 4, y });
            EXPECT_NEAR(view.viewStart, 2.0f, 0.05f) << "a quarter of the bar is 2 s";
            Settle();
            Click(int2{ track.x + track.w * 7 / 8, y });
            EXPECT_NEAR(view.viewStart, 6.0f, 0.05f) << "the thumb comes to the press, at the end";
            EXPECT_TRUE(edits.empty());
        }

        // Previous and next key, from the playhead; the end and the start.
        TEST_F(TimelineEditorTest, TheTransportGoesFromKeyToKey)
        {
            view.playback.time = 2.0f;
            Settle();
            const std::vector<ID> buttons = AllWithStyle(app.theme.timelineButtonStyleRef);
            ASSERT_EQ(buttons.size(), 4u) << "start, previous, next, end";
            Click(app.CenterOf(buttons[2]));
            EXPECT_FLOAT_EQ(view.playback.time, 3.0f) << "Intensity's, in the collapsed Light";
            Click(app.CenterOf(buttons[2]));
            EXPECT_FLOAT_EQ(view.playback.time, 4.0f) << "Position's and Scale's";
            Click(app.CenterOf(buttons[2]));
            EXPECT_FLOAT_EQ(view.playback.time, 5.0f) << "Explode";
            Click(app.CenterOf(buttons[1]));
            EXPECT_FLOAT_EQ(view.playback.time, 4.0f);
            Click(app.CenterOf(buttons[3]));
            EXPECT_FLOAT_EQ(view.playback.time, 8.0f);
            Click(app.CenterOf(buttons[2]));
            EXPECT_FLOAT_EQ(view.playback.time, 8.0f) << "none after";
            Click(app.CenterOf(buttons[0]));
            EXPECT_FLOAT_EQ(view.playback.time, 0.0f);
            Click(app.CenterOf(One(app.theme.timelinePlayStyleRef)));
            EXPECT_TRUE(view.playback.playing);
            Click(app.CenterOf(One(app.theme.timelineSnapStyleRef)));
            EXPECT_FALSE(view.snap);
        }

        // Disabled, it takes no clicks or keys.
        TEST_F(TimelineEditorTest, DisabledItDoesNothing)
        {
            disabled = true;
            Settle();
            Click(LanesPoint(2.0f, 1));
            Click(RulerPoint(3.0f));
            app.guiStateManager.Interaction().SetFocusID(WithStyle(app.theme.timelineLanesStyleRef));
            Frame();
            Key(KeyCode::KeyA, KeyModifier::Control);
            Key(KeyCode::Delete);
            EXPECT_TRUE(view.selectedKeys.empty());
            EXPECT_EQ(view.playback.time, 0.0f);
            EXPECT_TRUE(edits.empty());
        }

        // What the lanes draw: a diamond (two of them: its edge and its fill) a key, selected
        // ones in the accent; a group's summary; the clip and its waveform; the events.
        TEST_F(TimelineEditorTest, TheLanesDraw)
        {
            view.selectedKeys = { positionKeys[0] };
            Settle();
            GUITest::StubGUIRenderer drawn;
            const ElementRef ref = app.elementStorage.FindElementRefByID(WithStyle(app.theme.timelineLanesStyleRef));
            const ElementCustomDraw* draw = app.elementStorage.GetCustomDraw(ref);
            ASSERT_NE(draw, nullptr);
            const ElementRect* rects = app.elementStorage.GetElementRectPtr(ref);
            CustomDrawContext ctx{ drawn, app.elementStorage, ref, rects->rect, rects->innerRect,
                                   IntRect{ -4000, -4000, 8000, 8000 }, float3x2{}, int2{ 0, 0 } };
            draw->callback(ctx);
            auto trianglesIn = [&](VisualColorStyleIndex color) {
                return std::count_if(drawn.triangles.begin(), drawn.triangles.end(), [color](const auto& t) { return t.color == color; });
            };
            // A diamond is two triangles; Position 3 keys + Scale 2; the summaries of Sphere (0, 1,
            // 2, 4 s) and of the collapsed Light (3 s).
            EXPECT_EQ(trianglesIn(app.theme.timelineKeySelectedColor), 2);
            EXPECT_EQ(trianglesIn(app.theme.timelineKeyColor), 2 * 4);
            EXPECT_EQ(trianglesIn(app.theme.timelineSummaryKeyColor), 2 * 5);
            EXPECT_EQ(std::count(drawn.boxStyles.begin(), drawn.boxStyles.end(),
                                 GUIRenderer::StyleIndex(app.theme.timelineClipStyleRef.visualBoxStyleIndex, ElementState::None)), 1);
            EXPECT_EQ(std::count(drawn.boxStyles.begin(), drawn.boxStyles.end(),
                                 GUIRenderer::StyleIndex(app.theme.timelineEventStyleRef.visualBoxStyleIndex, ElementState::None)), 2);
            EXPECT_GE(drawn.quads.size(), 4u) << "the waveform's bars";
        }
    }
}
