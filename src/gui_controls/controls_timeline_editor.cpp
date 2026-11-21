#include <gui_controls/gui_application_internal.h>
#include <gui_controls/gui_timeline_internal.h>

#include <gui_core/graphics/gui_custom_draw_context.h>
#include <gui_core/input/gui_command_manager.h>
#include <gui_core/input/gui_pointer_drag.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace tachyon
{

    // ---------------------------------------------------------------------------
    // TimelineEditor: a toolbar; the track list beside the ruler and the lanes, scrolled
    // together; and a scroll bar. The ruler's and the lanes' padding boxes are the part of
    // the timeline in view, from its start to its end.
    // ---------------------------------------------------------------------------

    using namespace timeline_internal;

    namespace
    {
        constexpr float kWheelZoom = 1.25f;   // a wheel notch zooms by this much

        float LengthOf(ConstTimelineViewRef view)
        {
            return std::max(0.0f, Finite(view.data.duration, 0.0f));
        }

        float FrameOf(ConstTimelineViewRef view)
        {
            const float fps = Finite(view.data.fps, 0.0f);
            return fps > 0.0f ? 1.0f / fps : 0.0f;
        }

        // The part of the timeline in view: its start and length, within the whole.
        TimelineRange ViewRangeOf(ConstTimelineViewRef view)
        {
            const float length = LengthOf(view);
            const float shown  = Finite(view.state.viewLength, 0.0f);
            const float span   = (shown <= 0.0f || shown >= length) ? length : shown;
            const float start  = std::clamp(Finite(view.state.viewStart, 0.0f), 0.0f, length - span);
            return { start, start + span };
        }

        // The shortest stretch a zoom shows: ten frames, or a two-hundredth of the length.
        float MinViewLength(ConstTimelineViewRef view)
        {
            const float length = LengthOf(view);
            return std::min(length, std::max(length / 200.0f, 10.0f * FrameOf(view)));
        }

        // Shows `span` seconds, keeping the time `anchor` (inside the view) where it is.
        void ZoomTo(TimelineViewRef view, float span, float anchor)
        {
            const float length = LengthOf(view);
            const TimelineRange now = ViewRangeOf(view);
            span = std::clamp(span, MinViewLength(view), length);
            const float part = now.end > now.start ? (anchor - now.start) / (now.end - now.start) : 0.5f;
            view.state.viewLength = span >= length ? 0.0f : span;
            view.state.viewStart  = std::clamp(anchor - part * span, 0.0f, length - span);
        }

        // Pixels and seconds across a padding box showing `range`.
        struct Geometry
        {
            TimelineRange range;
            float         x0 = 0.0f, width = 1.0f;

            float XAt(float t) const { return x0 + (t - range.start) / std::max(1e-6f, range.end - range.start) * width; }
            float TimeAt(float x) const { return range.start + (x - x0) / std::max(1.0f, width) * (range.end - range.start); }
        };

        // Lower-case, for the filter.
        std::string Lower(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
                c = char(std::tolower((unsigned char)c));
            return out;
        }

        // The rows shown, by track: those the filter keeps (with the groups they're in),
        // and of those, the ones not inside a collapsed group.
        std::vector<int> ShownRows(ConstTimelineViewRef view)
        {
            const int count = (int)view.data.tracks.size();
            std::vector<bool> kept(size_t(count), view.state.filter.empty());
            if (!view.state.filter.empty())
            {
                const std::string needle = Lower(view.state.filter);
                for (int t = 0; t < count; ++t)
                {
                    if (Lower(view.data.tracks[size_t(t)].name).find(needle) == std::string::npos)
                        continue;
                    // It, and each group it's in.
                    kept[size_t(t)] = true;
                    int depth = view.data.tracks[size_t(t)].depth;
                    for (int g = t - 1; g >= 0 && depth > 0; --g)
                        if (view.data.tracks[size_t(g)].depth < depth)
                        {
                            kept[size_t(g)] = true;
                            depth = view.data.tracks[size_t(g)].depth;
                        }
                }
            }
            std::vector<int> rows;
            int collapsedAt = INT_MAX;   // the depth of the collapsed group being skipped
            for (int t = 0; t < count; ++t)
            {
                const TimelineTrack& track = view.data.tracks[size_t(t)];
                if (track.depth <= collapsedAt)
                    collapsedAt = INT_MAX;
                else
                    continue;
                if (!kept[size_t(t)])
                    continue;
                rows.push_back(t);
                if (track.kind == TimelineTrackKind::Group && !track.expanded)
                    collapsedAt = track.depth;
            }
            return rows;
        }

        // A group's summary: the times of the keys in the tracks it holds, each once.
        std::vector<float> SummaryTimes(ConstTimelineViewRef view, int group)
        {
            std::vector<float> times;
            for (const TimelineTrack& track : TracksInGroup(view.data, group))
                for (const TimelineKey& key : track.keys)
                    times.push_back(key.time);
            std::sort(times.begin(), times.end());
            times.erase(std::unique(times.begin(), times.end(), [](float a, float b) { return std::fabs(a - b) < 1e-4f; }), times.end());
            return times;
        }

        // The keys at `time` (within a hair) in the tracks a group holds.
        std::vector<ID> KeysAtInGroup(ConstTimelineViewRef view, int group, float time)
        {
            std::vector<ID> ids;
            for (const TimelineTrack& track : TracksInGroup(view.data, group))
                for (const TimelineKey& key : track.keys)
                    if (std::fabs(key.time - time) < 1e-4f)
                        ids.push_back(key.stableID);
            return ids;
        }

        // What's under the pointer on a row: a key, an event, or a group's summary key.
        struct Hit
        {
            std::vector<ID> ids;     // what a click there selects
            float           time = 0.0f;
        };

        Hit HitOnRow(ConstTimelineViewRef view, int track, const Geometry& geometry, float x, float reach)
        {
            Hit hit;
            if (track < 0 || track >= (int)view.data.tracks.size())
                return hit;
            const TimelineTrack& t = view.data.tracks[size_t(track)];
            float best = reach + 1.0f;
            auto consider = [&](float time, std::vector<ID> ids) {
                const float d = std::fabs(geometry.XAt(time) - x);
                if (d <= reach && d < best)
                {
                    best = d;
                    hit = Hit{ std::move(ids), time };
                }
            };
            switch (t.kind)
            {
                case TimelineTrackKind::Property:
                    for (const TimelineKey& key : t.keys) consider(key.time, { key.stableID });
                    break;
                case TimelineTrackKind::Events:
                    for (const TimelineEvent& e : t.events) consider(e.time, { e.stableID });
                    break;
                case TimelineTrackKind::Group:
                {
                    // The nearest summary key is the nearest key in the tracks held; its keys
                    // are gathered once, for it alone.
                    float nearest = best;
                    float time    = 0.0f;
                    for (const TimelineTrack& held : TracksInGroup(view.data, track))
                        for (const TimelineKey& key : held.keys)
                            if (const float d = std::fabs(geometry.XAt(key.time) - x); d < nearest)
                            {
                                nearest = d;
                                time    = key.time;
                            }
                    if (nearest <= reach)
                        consider(time, KeysAtInGroup(view, track, time));
                    break;
                }
                default: break;
            }
            return hit;
        }

        // The keys and events of every track, in time order again after a move.
        void SortTimes(TimelineTrack& track)
        {
            std::stable_sort(track.keys.begin(), track.keys.end(), [](const TimelineKey& a, const TimelineKey& b) { return a.time < b.time; });
            std::stable_sort(track.events.begin(), track.events.end(), [](const TimelineEvent& a, const TimelineEvent& b) { return a.time < b.time; });
        }

        // The view's tracks as `from` (the same tracks) with the selected keys and events of
        // unlocked tracks moved by `delta`, kept inside the length. Only the tracks holding
        // selected ones are rewritten, their keys and events alone: a drag calls it each move,
        // and the others (an audio clip's waveform) stay as they are.
        void MoveSelectedKeys(TimelineViewRef view, const std::vector<TimelineTrack>& from, float delta)
        {
            const float length = LengthOf(view);
            auto selected = [&view](const auto& items) {
                return std::any_of(items.begin(), items.end(), [&view](const auto& item) { return view.state.selectedKeys.contains(item.stableID); });
            };
            for (int t = 0; t < (int)from.size(); ++t)
            {
                const TimelineTrack& was = from[size_t(t)];
                if (IsTrackLocked(view.data, t) || (!selected(was.keys) && !selected(was.events)))
                    continue;
                TimelineTrack& track = view.data.tracks[size_t(t)];
                track.keys   = was.keys;     // into the storage it has
                track.events = was.events;
                for (TimelineKey& key : track.keys)
                    if (view.state.selectedKeys.contains(key.stableID)) key.time = std::clamp(key.time + delta, 0.0f, length);
                for (TimelineEvent& event : track.events)
                    if (view.state.selectedKeys.contains(event.stableID)) event.time = std::clamp(event.time + delta, 0.0f, length);
                SortTimes(track);
            }
        }

        // The edit taking the tracks from `before` to how they are now.
        TimelineEdit ChangesSince(ConstTimelineViewRef view, const std::vector<TimelineTrack>& before)
        {
            TimelineEdit edit;
            if (before.size() != view.data.tracks.size())
                return edit;
            for (int t = 0; t < (int)before.size(); ++t)
                if (!(before[size_t(t)] == view.data.tracks[size_t(t)]))
                    edit.changes.push_back({ t, before[size_t(t)], view.data.tracks[size_t(t)] });
            return edit;
        }

        // A delta on frames when the view snaps.
        float SnappedDelta(ConstTimelineViewRef view, float delta)
        {
            const float frame = FrameOf(view);
            return (view.data.snap && frame > 0.0f) ? std::round(delta / frame) * frame : delta;
        }

        float SnappedTime(ConstTimelineViewRef view, float time)
        {
            const float frame = FrameOf(view);
            return SnapToFrame(time, view.data.snap ? frame : 0.0f, LengthOf(view));
        }

        // Every key's and event's time, in order: where Previous / Next key go.
        std::vector<float> AllKeyTimes(ConstTimelineViewRef view)
        {
            std::vector<float> times;
            for (const TimelineTrack& track : view.data.tracks)
            {
                for (const TimelineKey& key : track.keys) times.push_back(key.time);
                for (const TimelineEvent& e : track.events) times.push_back(e.time);
            }
            std::sort(times.begin(), times.end());
            return times;
        }

        // A filled diamond centred on (x, y), `half` from its centre to a corner.
        void Diamond(CustomDrawContext& ctx, float x, float y, float half, VisualColorStyleIndex color)
        {
            const float2 points[4] = { { x, y - half }, { x + half, y }, { x, y + half }, { x - half, y } };
            ctx.RenderFilledPolygon(points, color);
        }

        // The wheel over the ruler or the lanes, by `mode`: zooms round the time under the
        // pointer, or scrolls along the time. A scroll up or down is left to the rows' scroll
        // area. A pinch zooms.
        bool WheelOverTime(TimelineViewRef view, const ElementEvent& event, const Geometry& g, float x, WheelMode mode)
        {
            const float length = LengthOf(view);
            const WheelNavigation nav = NavigateByWheel(event.state, mode, kWheelZoom);
            if (length <= 0.0f || (nav.zoom == 1.0f && nav.pan.x == 0.0f))
                return false;
            const TimelineRange now = ViewRangeOf(view);
            const float span = now.end - now.start;
            if (nav.zoom != 1.0f)
                ZoomTo(view, span / nav.zoom, std::clamp(g.TimeAt(x), now.start, now.end));
            if (nav.pan.x != 0.0f)
            {
                const TimelineRange after = ViewRangeOf(view);
                const float shown = after.end - after.start;
                view.state.viewStart = std::clamp(after.start + nav.pan.x * shown * 0.1f, 0.0f, std::max(0.0f, length - shown));
            }
            return true;
        }

        // The middle button (or Alt + left) held over the ruler or the lanes: the time follows
        // the pointer across, and the rows (with `body`, the area they scroll in) up and down.
        // Escape puts both back.
        bool DragScroll(TimelineViewRef view, const ElementEvent& event, const ElementStorage& es, GUIStateManager& gsm,
                        const Geometry& g, ID body, int32_t contentHeight, int32_t dragDistance)
        {
            ScrollManagement& scrolling = gsm.GetScrollManagement();
            const ScrollOffsetHandle handle = body != ID::Invalid() ? scrolling.GetOrCreateScrollOffsetHandle(body) : ScrollOffsetHandle{};
            const bool vertical = body != ID::Invalid() && scrolling.IsScrollOffsetHandleValid(handle);
            switch (event.type)
            {
                case EventType::MouseDown:
                {
                    const MouseButtons button = PanDragButton(event.state);
                    if (button == MouseButtons::None)
                        return false;
                    view.state.dragScroll.Press(event, es, gsm,
                                          { ViewRangeOf(view).start, vertical ? scrolling.GetScrollOffset(handle).y : 0 },
                                          { .button = button, .focus = false });
                    return true;
                }
                case EventType::MouseMove:
                {
                    if (!view.state.dragScroll.IsPressed(event.id))
                        return false;
                    if (!view.state.dragScroll.Move(event, dragDistance))
                        return true;
                    // Screen pixels: the rows move under the pointer as they scroll.
                    const int2 d = view.state.dragScroll.drag.ScreenDelta(event.state);
                    const TimelineViewState::DragScrollFrom& from = view.state.dragScroll.before;
                    const float length = LengthOf(view);
                    const TimelineRange now = ViewRangeOf(view);
                    const float span = now.end - now.start;
                    if (span < length)
                        view.state.viewStart = std::clamp(from.start - float(d.x) * span / std::max(1.0f, g.width), 0.0f, length - span);
                    if (vertical)
                        if (const ElementRect* area = es.GetElementRectPtr(es.FindElementRefByID(body)))
                        {
                            const int32_t most = std::max(0, contentHeight - area->innerRect.h);
                            const int2 offset = scrolling.GetScrollOffset(handle);
                            scrolling.SetScrollOffset(handle, int2{ offset.x, std::clamp(from.offset + d.y, -most, 0) });
                        }
                    return true;
                }
                case EventType::MouseUp:
                    return view.state.dragScroll.IsPressed(event.id) && view.state.dragScroll.Release(event) != PointerDrag::Ended::No;
                case EventType::KeyDown:
                    if (!view.state.dragScroll.Cancel(event, gsm))
                        return false;
                    view.state.viewStart = view.state.dragScroll.before.start;
                    if (vertical)
                        scrolling.SetScrollOffset(handle, int2{ scrolling.GetScrollOffset(handle).x, view.state.dragScroll.before.offset });
                    return true;
                default: return false;
            }
        }

        // A label's text and where it goes, for the draw.
        struct TickLabel
        {
            TextID text = TextID::Invalid;
            float  time = 0.0f;
            int2   size {};
        };
    }

    void GUIApplication::TimelineEditor(ID id, TimelineViewRef view, LayoutOverride&& constraints)
    {
        // The tracks changed since this view last built (an edit here, or in another view of
        // them): let go of what's gone.
        PruneTimelineSelection(view);
        view.state.editorID = id;
        const float length = LengthOf(view);
        const float frame  = FrameOf(view);
        view.data.playback.time = std::clamp(Finite(view.data.playback.time, 0.0f), 0.0f, length);
        TimelineViewRef v = view;
        const bool disabled = guiStateManager.Construction().IsDisabled();

        // The zoom slider moved last frame: show that much, round the playhead. Then the
        // slider shows the zoom, however it got there.
        {
            const float minimum = MinViewLength(view);
            if (view.state.zoom != view.state.zoomShown && length > 0.0f && minimum < length)
            {
                const float part = std::clamp(view.state.zoom, 0, 100) / 100.0f;
                ZoomTo(view, length * std::pow(minimum / length, part), view.data.playback.time);
            }
            const TimelineRange range = ViewRangeOf(view);
            const float span = range.end - range.start;
            view.state.zoom = (length > 0.0f && minimum < length && span > 0.0f)
                ? std::clamp(int(std::lround(100.0f * std::log(span / length) / std::log(minimum / length))), 0, 100) : 0;
            view.state.zoomShown = view.state.zoom;
        }
        const TimelineRange range = ViewRangeOf(view);
        const std::vector<int> rows = ShownRows(view);
        const int32_t rowHeight = styleManager.Px(28);

        BeginVertical(id, theme.timelineEditorStyleRef, std::move(constraints));
        [[maybe_unused]] const bool pushed = idManager.PushParent(id);
        auto playbackChanged = [v] { if (v.data.onPlaybackChanged) v.data.onPlaybackChanged(); };

        // ---- The toolbar ----------------------------------------------------------
        BeginHorizontal(idManager.AllocateDynamicID("toolbar"), theme.timelineToolbarStyleRef);
        BeginHorizontal(idManager.AllocateDynamicID("transport"), theme.timelineTransportStyleRef);
        Button(idManager.AllocateDynamicID("start"), GUIContent(theme.timelineStartIcon, {}, "Go to the start"),
               [v, playbackChanged] { v.data.playback.time = 0.0f; playbackChanged(); }, theme.timelineButtonStyleRef);
        Button(idManager.AllocateDynamicID("prev"), GUIContent(theme.timelinePrevKeyIcon, {}, "The previous key"),
               [v, playbackChanged] {
                   const std::vector<float> times = AllKeyTimes(v);
                   const auto it = std::lower_bound(times.begin(), times.end(), v.data.playback.time - 1e-4f);
                   v.data.playback.time = it == times.begin() ? 0.0f : *std::prev(it);
                   v.data.playback.playing = false;
                   playbackChanged();
               }, theme.timelineButtonStyleRef);
        Button(idManager.AllocateDynamicID("play"),
               view.data.playback.playing ? GUIContent(theme.timelinePauseIcon, {}, "Pause") : GUIContent(theme.timelinePlayIcon, {}, "Play"),
               [v, length, playbackChanged] { TogglePlay(v.data.playback, length); playbackChanged(); }, theme.timelinePlayStyleRef);
        Button(idManager.AllocateDynamicID("next"), GUIContent(theme.timelineNextKeyIcon, {}, "The next key"),
               [v, length, playbackChanged] {
                   const std::vector<float> times = AllKeyTimes(v);
                   const auto it = std::upper_bound(times.begin(), times.end(), v.data.playback.time + 1e-4f);
                   v.data.playback.time = it == times.end() ? length : *it;
                   v.data.playback.playing = false;
                   playbackChanged();
               }, theme.timelineButtonStyleRef);
        Button(idManager.AllocateDynamicID("end"), GUIContent(theme.timelineEndIcon, {}, "Go to the end"),
               [v, length, playbackChanged] { v.data.playback.time = length; v.data.playback.playing = false; playbackChanged(); },
               theme.timelineButtonStyleRef);
        Toggle(idManager.AllocateDynamicID("loop"), view.data.playback.loop, GUIContent(theme.timelineLoopIcon, {}, "Loop"),
               [v](bool loop) { v.data.playback.loop = loop; }, theme.timelineLoopStyleRef);
        EndHorizontal();
        RegisterElement(Element{ .id = idManager.AllocateDynamicID("divider"), .styleRef = theme.timelineDividerStyleRef,
                                 .flags = ElementFlags::NoInput }, LayoutOverride{});

        BeginHorizontal(idManager.AllocateDynamicID("time"), theme.timelineTimeStyleRef);
        Text(idManager.AllocateDynamicID("now"), ClockText(view.data.playback.time), theme.timelineTimeCurrentStyleRef);
        Text(idManager.AllocateDynamicID("length"), "/ " + ClockText(length), theme.timelineTimeTotalStyleRef);
        if (frame > 0.0f)
            Text(idManager.AllocateDynamicID("frame"), FrameText(view.data.playback.time, frame, length), theme.timelineFrameStyleRef);
        EndHorizontal();

        // The frame rate: the usual ones, and the view's own if it's another.
        {
            const std::string shown = std::format("{:g} fps", Finite(view.data.fps, 0.0f));
            if (BeginMenu(idManager.AllocateDynamicID("fps"), shown, theme.timelineFpsStyleRef, theme.menuStyleRef, theme.menuItemRowStyleRef))
            {
                for (const float fps : { 12.0f, 24.0f, 25.0f, 30.0f, 48.0f, 50.0f, 60.0f })
                    MenuItem(std::format("{:g} fps", fps), [v, fps] { v.data.fps = fps; });
            }
            EndMenu();
        }
        Toggle(idManager.AllocateDynamicID("snap"), view.data.snap, GUIContent(theme.timelineSnapIcon, "Snap", "Keys and the loop range land on frames"),
               [v](bool snap) { v.data.snap = snap; }, theme.timelineSnapStyleRef);

        BeginHorizontal(idManager.AllocateDynamicID("zoom"), theme.timelineZoomStyleRef);
        RegisterElement(Element{ .id = idManager.AllocateDynamicID("zoomOut"), .iconID = theme.timelineZoomOutIcon,
                                 .styleRef = theme.timelineZoomIconStyleRef, .flags = ElementFlags::NoInput }, LayoutOverride{});
        Slider(idManager.AllocateDynamicID("zoomSlider"), view.state.zoom, 0, 100, LayoutOverride{}.Width(110));
        SetTooltip("Zoom (the wheel over the lanes)");
        RegisterElement(Element{ .id = idManager.AllocateDynamicID("zoomIn"), .iconID = theme.timelineZoomInIcon,
                                 .styleRef = theme.timelineZoomIconStyleRef, .flags = ElementFlags::NoInput }, LayoutOverride{});
        EndHorizontal();
        EndHorizontal();

        // ---- The header: the list's, and the ruler ---------------------------------
        const ID lanesID = idManager.AllocateDynamicID("lanes");
        BeginHorizontal(idManager.AllocateDynamicID("header"), theme.timelineHeaderStyleRef);
        BeginHorizontal(idManager.AllocateDynamicID("listHeader"), theme.timelineListHeaderStyleRef);
        Text(idManager.AllocateDynamicID("tracks"), "Tracks", theme.timelineListTitleStyleRef);
        SearchField(idManager.AllocateDynamicID("filter"), view.state.filter, theme.searchIcon, TextEditBehavior::ContinuousUpdate,
                    theme.timelineFilterStyleRef);
        if (view.state.onAddTrack)
            Button(idManager.AllocateDynamicID("addTrack"), GUIContent(theme.addIcon, {}, "Add a track"),
                   [v] { if (v.state.onAddTrack) v.state.onAddTrack(); }, theme.timelineAddTrackStyleRef);
        EndHorizontal();

        // The labels of the ruler's major ticks, for the width it had last frame.
        const float lastWidth = float(view.state.lanesWidth > 0 ? view.state.lanesWidth : styleManager.Px(600));
        const float minLabelGap = float(styleManager.Px(56));
        const float labelStep = MajorTickStep(lastWidth / std::max(1e-6f, range.end - range.start), minLabelGap);
        std::vector<TickLabel> rulerLabels;
        if (labelStep > 0.0f)
        {
            const int first = int(std::ceil(range.start / labelStep - 1e-3f)), last = int(std::floor(range.end / labelStep + 1e-3f));
            const ID labelsID = last >= first ? idManager.AllocateRangedID(uint32_t(last - first + 1), "rulerLabels") : ID::Invalid();
            for (int i = first; i <= last; ++i)
            {
                const float t = float(i) * labelStep;
                const ID labelID = idManager.GetSubID(labelsID, uint32_t(i - first));
                const TextID text = textSystem->RegisterText(labelID, std::format("{:g}s", t), theme.timelineRulerLabelStyleRef, ElementState::None);
                const TextMeasurement m = textSystem->MeasureText(text, TextWrapContext{});
                rulerLabels.push_back({ text, t, { m.maxWidth, m.maxHeight } });
            }
        }
        const ID pillID = idManager.AllocateDynamicID("playheadLabel");
        const TextID pillText = textSystem->RegisterText(pillID, std::format("{:.2f}", view.data.playback.time),
                                                         theme.timelinePlayheadLabelStyleRef, ElementState::None);
        const TextMeasurement pillSize = textSystem->MeasureText(pillText, TextWrapContext{});

        // The ruler: a press moves the playhead, Shift + a drag sets the loop range, its ends
        // drag, Ctrl + a click clears it.
        const ID rulerID = idManager.AllocateDynamicID("ruler");
        EventHandler rulerHandler;
        if (!disabled)
        {
            GUIStateManager& gsm = guiStateManager;
            const ElementStorage& es = elementStorage;
            const int32_t edgeReach = styleManager.Px(4);
            const int32_t dragDistance = styleManager.Px(kDragStartDistance);
            rulerHandler = [&gsm, &es, v, lanesID, edgeReach, dragDistance, playbackChanged, wheelMode = GetWheelMode()](ElementEvent&& event) -> bool
            {
                if (event.rect == nullptr)
                    return false;
                const IntRect inner = event.rect->innerRect;
                const Geometry g{ ViewRangeOf(v), float(inner.x), float(v.state.lanesWidth > 0 ? v.state.lanesWidth : inner.w) };
                const float x = float(event.LocalCursor(es).x);
                if (DragScroll(v, event, es, gsm, g, ID::Invalid(), 0, dragDistance))
                    return true;
                // The loop edge within reach of the pointer: 1 its start, 2 its end, 0 none.
                auto loopEdgeAt = [&]() {
                    const TimelineRange loop = v.data.loopRange;
                    if (loop.empty()) return 0;
                    if (std::fabs(x - g.XAt(loop.start)) <= float(edgeReach)) return 1;
                    if (std::fabs(x - g.XAt(loop.end)) <= float(edgeReach)) return 2;
                    return 0;
                };
                switch (event.type)
                {
                    case EventType::MouseDown:
                    {
                        if (!event.state.LeftPressed())
                            return false;
                        TakeFocus(gsm, lanesID);
                        const bool shift = (event.state.modifiers & KeyModifier::Shift)   != KeyModifier::None;
                        const bool ctrl  = (event.state.modifiers & KeyModifier::Control) != KeyModifier::None;
                        const TimelineRange loop = v.data.loopRange;
                        if (ctrl)
                        {
                            if (!loop.empty())
                                CommitTimelineEdit(v.data, TimelineEdit{ .loopFrom = loop, .loopTo = TimelineRange{} });
                            return true;
                        }
                        int edge = shift ? 0 : -1;
                        if (!shift && loopEdgeAt() != 0)
                            edge = loopEdgeAt();
                        if (edge >= 0)
                        {
                            // Captured: the moves, release and Escape come here.
                            v.state.loopDrag.Press(event, es, gsm, loop, { .atOnce = true, .focus = false });
                            v.state.loopEdge = edge;
                            v.state.loopPressTime = SnappedTime(v, g.TimeAt(x));
                            return true;
                        }
                        v.state.scrub.Press(event, es, gsm, v.data.playback.time, { .atOnce = true, .focus = false });
                        v.data.playback.time = SnappedTime(v, g.TimeAt(x));
                        playbackChanged();
                        return true;
                    }
                    case EventType::MouseMove:
                    {
                        if (v.state.loopDrag.Move(event, 0))
                        {
                            const float t = SnappedTime(v, g.TimeAt(x));
                            TimelineRange r = v.state.loopDrag.before;
                            if (v.state.loopEdge == 0) r = { std::min(v.state.loopPressTime, t), std::max(v.state.loopPressTime, t) };
                            else if (v.state.loopEdge == 1) r.start = std::min(t, r.end);
                            else r.end = std::max(t, r.start);
                            v.data.loopRange = r;
                            return true;
                        }
                        if (v.state.scrub.Move(event, 0))
                        {
                            v.data.playback.time = SnappedTime(v, g.TimeAt(x));
                            playbackChanged();
                            return true;
                        }
                        // What's under the pointer, for its hover look; not while dragging.
                        v.state.loopEdgeHover.Track(AnyHeld(v.state.loopDrag, v.state.scrub, v.state.dragScroll), loopEdgeAt);
                        return false;
                    }
                    case EventType::MouseUp:
                    {
                        if (v.state.loopDrag.Release(event) != PointerDrag::Ended::No)
                        {
                            // A Shift + click (no drag) leaves no range: as it was.
                            const TimelineRange before = v.state.loopDrag.before;
                            if (v.data.loopRange.empty() && v.state.loopEdge == 0)
                                v.data.loopRange = before;
                            const TimelineRange now = v.data.loopRange;
                            if (!(now == before))
                            {
                                v.data.loopRange = before;
                                CommitTimelineEdit(v.data, TimelineEdit{ .loopFrom = before, .loopTo = now });
                            }
                            return true;
                        }
                        return v.state.scrub.Release(event) != PointerDrag::Ended::No;
                    }
                    case EventType::KeyDown:
                        if (v.state.loopDrag.Cancel(event, gsm, v.data.loopRange))
                            return true;
                        if (v.state.scrub.Cancel(event, gsm, v.data.playback.time))
                        {
                            playbackChanged();
                            return true;
                        }
                        return false;
                    case EventType::MouseScroll:
                        return WheelOverTime(v, event, g, x, wheelMode);
                    default: return false;
                }
            };
        }
        const ElementRef rulerRef = RegisterElement(
            Element{ .id = rulerID, .eventHandler = std::move(rulerHandler), .styleRef = theme.timelineRulerStyleRef,
                     .flags = view.data.playback.playing ? ElementFlags::Animated : ElementFlags::None },
            LayoutOverride{});
        {
            const TimelineRange loop = view.data.loopRange;
            const float time = view.data.playback.time;
            // A press anywhere on the ruler moves the playhead, so its pill lights up while
            // the ruler is hovered; a loop edge within reach, or being dragged, does too.
            const bool rulerHovered = (guiStateManager.GetCurrentState(rulerID) & ElementState::Hover) != ElementState::None;
            const bool scrubbing    = view.state.scrub.IsHeld();
            const int  litEdge      = view.state.loopDrag.IsHeld() ? view.state.loopEdge : view.state.loopEdgeHover.Shown(rulerHovered);
            const ElementState pillState = (rulerHovered && litEdge == 0) || scrubbing ? ElementState::Hover : ElementState::None;
            const VisualBoxStyleIndex tick = theme.timelineTickStyleRef.visualBoxStyleIndex, major = theme.timelineTickMajorStyleRef.visualBoxStyleIndex;
            const VisualBoxStyleIndex band = theme.timelineLoopRangeStyleRef.visualBoxStyleIndex, edge = theme.timelineLoopEdgeStyleRef.visualBoxStyleIndex;
            const VisualBoxStyleIndex line = theme.timelinePlayheadStyleRef.visualBoxStyleIndex, pill = theme.timelinePlayheadLabelStyleRef.visualBoxStyleIndex;
            const int32_t px1 = std::max(1, styleManager.Px(1)), px2 = std::max(1, styleManager.Px(2)), px4 = styleManager.Px(4);
            const int32_t minorHeight = styleManager.Px(5), majorHeight = styleManager.Px(10);
            const int32_t pillHeight = styleManager.Px(18), pillMin = styleManager.Px(40), pillPad = styleManager.Px(12);
            const int2 pillTextSize{ pillSize.maxWidth, pillSize.maxHeight };
            VERIFY(elementStorage.AddCustomDraw(rulerRef, [=](CustomDrawContext& ctx)
            {
                const IntRect outer = ctx.ElementRect(), inner = ctx.ElementInnerRect();
                const Geometry g{ range, float(inner.x), float(v.state.lanesWidth > 0 ? v.state.lanesWidth : inner.w) };
                if (!loop.empty())
                {
                    const int32_t a = int32_t(g.XAt(loop.start)), b = int32_t(g.XAt(loop.end));
                    ctx.RenderBox(IntRect{ a, outer.y, b - a, outer.h }, outer, band);
                    // A lit edge is drawn wider too: two pixels of a lighter color are easy to miss.
                    const int32_t wa = litEdge == 1 ? px4 : px2, wb = litEdge == 2 ? px4 : px2;
                    ctx.RenderBox(IntRect{ a, outer.y, wa, outer.h }, outer, edge, litEdge == 1 ? ElementState::Hover : ElementState::None);
                    ctx.RenderBox(IntRect{ b - wb, outer.y, wb, outer.h }, outer, edge, litEdge == 2 ? ElementState::Hover : ElementState::None);
                }
                // A major tick every labelled step, three minor ones between.
                if (labelStep > 0.0f)
                {
                    const float minor = labelStep / 4.0f;
                    const int first = int(std::ceil(range.start / minor - 1e-3f)), last = int(std::floor(range.end / minor + 1e-3f));
                    for (int i = first; i <= last; ++i)
                    {
                        const bool isMajor = i % 4 == 0;
                        const int32_t h = isMajor ? majorHeight : minorHeight;
                        ctx.RenderBox(IntRect{ int32_t(g.XAt(float(i) * minor)), outer.y + outer.h - h, px1, h }, outer, isMajor ? major : tick);
                    }
                }
                for (const TickLabel& label : rulerLabels)
                    ctx.RenderText(label.text, IntRect{ int32_t(g.XAt(label.time)) + px4, outer.y + px4, label.size.x, label.size.y });
                // The playhead, and its time in a pill over it.
                if (time >= range.start && time <= range.end)
                {
                    const int32_t x = int32_t(g.XAt(time));
                    ctx.RenderBox(IntRect{ x, outer.y, px1, outer.h }, outer, line);
                    // Centered on the playhead, but kept on the ruler at the ends of the view.
                    const int32_t w = std::max(pillMin, pillTextSize.x + pillPad);
                    const int32_t pillX = std::max(outer.x, std::min(x - w / 2, outer.x + outer.w - w));
                    const IntRect p{ pillX, outer.y + px4, w, pillHeight };
                    ctx.RenderBox(p, outer, pill, pillState);
                    ctx.RenderText(pillText, IntRect{ p.x + (w - pillTextSize.x) / 2, p.y + (pillHeight - pillTextSize.y) / 2,
                                                       pillTextSize.x, pillTextSize.y });
                }
            }));
        }
        EndHorizontal();

        // ---- The body: the track list and the lanes, scrolled together ------------
        const ID bodyID = idManager.AllocateDynamicID("body");
        BeginArea(bodyID, theme.timelineBodyStyleRef, LayoutOverride{}.FlexGrow(1));
        BeginVertical(idManager.AllocateDynamicID("list"), theme.timelineListStyleRef);
        const int32_t indent = styleManager.Px(24);
        for (const int t : rows)
        {
            const TimelineTrack& track = view.data.tracks[size_t(t)];
            // Scoped to the editor: other views of the tracks have rows of their own.
            const ID trackID = track.stableID;
            const ID rowID = IDManager::ScopedID(id, trackID);
            const bool locked = IsTrackLocked(view.data, t);
            const ElementState rowState = guiStateManager.GetCurrentState(rowID)
                                        | (view.state.selectedTrack == trackID ? ElementState::Selected : ElementState::None);
            EventHandler rowHandler;
            if (!disabled)
                rowHandler = [this, v, trackID, lanesID](ElementEvent&& event) -> bool {
                    if (event.type != EventType::MouseDown || !event.state.LeftPressed())
                        return false;
                    v.state.selectedTrack = trackID;
                    TakeFocus(guiStateManager, lanesID);
                    return true;
                };
            RegisterElement(Element{ .id = rowID, .eventHandler = std::move(rowHandler), .styleRef = theme.timelineRowStyleRef,
                                     .flags = ElementFlags::PropagatesDescendantState, .state = rowState },
                            LayoutOverride{}.Height(Pixels{ rowHeight }));
            VERIFY(elementStorage.BeginChildren());
            [[maybe_unused]] const bool inRow = idManager.PushParent(rowID);
            if (track.depth > 0)
                RegisterElement(Element{ .id = idManager.AllocateDynamicID("indent"), .styleRef = theme.emptyHorzStyleRef,
                                         .flags = ElementFlags::NoInput }, LayoutOverride{}.Width(Pixels{ indent * track.depth }).FlexGrow(0));

            // A change to this track, as an edit: what it was and what it'll be.
            auto changeTrack = [v, t](auto change) {
                return [v, t, change] {
                    if (t >= (int)v.data.tracks.size()) return;
                    TimelineTrack to = v.data.tracks[size_t(t)];
                    change(to);
                    CommitTimelineEdit(v.data, TimelineEdit{ .changes = { { t, v.data.tracks[size_t(t)], std::move(to) } } });
                };
            };
            switch (track.kind)
            {
                case TimelineTrackKind::Group:
                {
                    Button(idManager.AllocateDynamicID("chevron"),
                           GUIContent(track.expanded ? theme.treeArrowDown : theme.treeArrowRight, {}, track.expanded ? "Collapse" : "Expand"),
                           [v, t] { if (t < (int)v.data.tracks.size()) v.data.tracks[size_t(t)].expanded = !v.data.tracks[size_t(t)].expanded; },
                           theme.timelineChevronStyleRef);
                    const VisualColorStyleIndex chip = track.color.a > 0.0f
                        ? AppColorStyle(idManager.AllocateDynamicID("chip-color"), track.color) : VisualColorStyleIndex{};
                    RegisterElement(Element{ .id = idManager.AllocateDynamicID("chip"), .styleRef = theme.timelineChipStyleRef,
                                             .flags = ElementFlags::NoInput, .colorStyleIndex = chip }, LayoutOverride{});
                    Text(idManager.AllocateDynamicID("name"), track.name, theme.timelineGroupNameStyleRef);
                    Button(idManager.AllocateDynamicID("eye"),
                           GUIContent(track.visible ? theme.timelineEyeIcon : theme.timelineEyeOffIcon, {}, track.visible ? "Hide" : "Show"),
                           changeTrack([](TimelineTrack& to) { to.visible = !to.visible; }), theme.timelineRowButtonStyleRef);
                    Button(idManager.AllocateDynamicID("lock"),
                           GUIContent(track.locked ? theme.timelineLockIcon : theme.timelineUnlockIcon, {}, track.locked ? "Unlock" : "Lock"),
                           changeTrack([](TimelineTrack& to) { to.locked = !to.locked; }), theme.timelineRowButtonStyleRef);
                    break;
                }
                case TimelineTrackKind::Property:
                {
                    Text(idManager.AllocateDynamicID("name"), track.name, theme.timelineTrackNameStyleRef);
                    if (locked) BeginDisabled();
                    Button(idManager.AllocateDynamicID("key"), GUIContent(theme.timelineKeyIcon, {}, "Key at the playhead"),
                           [this, v, t] { CommitTimelineEdit(v.data, AddKeyEdit(v.data, t, v.data.playback.time, idManager.CreateStaticID())); },
                           theme.timelineRowButtonStyleRef);
                    if (locked) EndDisabled();
                    break;
                }
                case TimelineTrackKind::Audio:
                {
                    const ID nameID = idManager.AllocateDynamicID("name");
                    RegisterElement(Element{ .id = nameID,
                                             .textID = textSystem->RegisterText(nameID, theme.timelineAudioIcon, track.name, theme.timelineTrackNameStyleRef, ElementState::None),
                                             .styleRef = theme.timelineTrackNameStyleRef, .flags = ElementFlags::NoInput }, LayoutOverride{});
                    Toggle(idManager.AllocateDynamicID("mute"), track.muted, GUIContent("M", "Mute"),
                           [changeTrack](bool) { changeTrack([](TimelineTrack& to) { to.muted = !to.muted; })(); }, theme.timelineMuteStyleRef);
                    Toggle(idManager.AllocateDynamicID("solo"), track.solo, GUIContent("S", "Solo"),
                           [changeTrack](bool) { changeTrack([](TimelineTrack& to) { to.solo = !to.solo; })(); }, theme.timelineSoloStyleRef);
                    break;
                }
                case TimelineTrackKind::Events:
                {
                    const ID nameID = idManager.AllocateDynamicID("name");
                    RegisterElement(Element{ .id = nameID,
                                             .textID = textSystem->RegisterText(nameID, theme.timelineEventsIcon, track.name, theme.timelineTrackNameStyleRef, ElementState::None),
                                             .styleRef = theme.timelineTrackNameStyleRef, .flags = ElementFlags::NoInput }, LayoutOverride{});
                    break;
                }
            }
            if (inRow)
                (void)idManager.PopParent();
            VERIFY(elementStorage.EndChildren());
            if (view.state.onTrackContextMenu && BeginContextMenu(rowID))
            {
                v.state.selectedTrack = trackID;
                view.state.onTrackContextMenu(t);
            }
            if (view.state.onTrackContextMenu)
                EndContextMenu();
        }
        EndVertical();

        // The event names, for the draw.
        struct EventLabel { int row; float time; TextID text; int2 size; bool selected; };
        std::vector<EventLabel> eventLabels;
        // Where each label is beside its event's line, for the lanes to take it as the event.
        struct LabelSpan { int track; ID event; float time; int32_t from, to; };
        std::vector<LabelSpan> labelSpans;
        const int32_t labelFrom = std::max(1, styleManager.Px(2)) + styleManager.Px(4), labelPad = 2 * styleManager.Px(5);
        for (int r = 0; r < (int)rows.size(); ++r)
        {
            const TimelineTrack& track = view.data.tracks[size_t(rows[size_t(r)])];
            if (track.kind != TimelineTrackKind::Events)
                continue;
            [[maybe_unused]] const bool inTrack = idManager.PushParent(IDManager::ScopedID(id, track.stableID));
            const ID labelsID = track.events.empty() ? ID::Invalid() : idManager.AllocateRangedID(uint32_t(track.events.size()), "events");
            for (size_t e = 0; e < track.events.size(); ++e)
            {
                const TimelineEvent& event = track.events[e];
                const TextID text = textSystem->RegisterText(idManager.GetSubID(labelsID, uint32_t(e)), event.name,
                                                             theme.timelineEventLabelStyleRef, ElementState::None);
                const TextMeasurement m = textSystem->MeasureText(text, TextWrapContext{});
                eventLabels.push_back({ r, event.time, text, { m.maxWidth, m.maxHeight }, view.state.selectedKeys.contains(event.stableID) });
                labelSpans.push_back({ rows[size_t(r)], event.stableID, event.time, labelFrom, labelFrom + m.maxWidth + labelPad });
            }
            if (inTrack)
                (void)idManager.PopParent();
        }

        // The lanes: keys, clips and events by row. A press on a key selects it and drags
        // the selection; on the empty lanes it starts a box.
        EventHandler lanesHandler;
        if (!disabled)
        {
            GUIStateManager& gsm = guiStateManager;
            const ElementStorage& es = elementStorage;
            const float reach = float(styleManager.Px(6));
            const int32_t dragDistance = styleManager.Px(kDragStartDistance);
            const float edgeReach = float(styleManager.Px(5));
            lanesHandler = [this, &gsm, &es, v, rows, rowHeight, reach, edgeReach, dragDistance, bodyID, playbackChanged, labelSpans,
                            wheelMode = GetWheelMode()](ElementEvent&& event) -> bool
            {
                if (event.rect == nullptr)
                    return false;
                const IntRect inner = event.rect->innerRect;
                const Geometry g{ ViewRangeOf(v), float(inner.x), float(inner.w) };
                const int2 at = event.LocalCursor(es);
                const int row = (at.y - event.rect->rect.y) / std::max(1, rowHeight);
                // The rows are this frame's; the app may have removed tracks since (from a menu).
                int track = row >= 0 && row < (int)rows.size() ? rows[size_t(row)] : -1;
                if (track >= (int)v.data.tracks.size())
                    track = -1;
                const bool shift = (event.state.modifiers & KeyModifier::Shift)   != KeyModifier::None;
                const bool ctrl  = (event.state.modifiers & KeyModifier::Control) != KeyModifier::None;
                const float length = LengthOf(v);
                auto commitMove = [&](float delta) {
                    std::vector<TimelineTrack> before = v.data.tracks;
                    MoveSelectedKeys(v, before, delta);
                    const TimelineEdit edit = ChangesSince(v, before);
                    v.data.tracks = std::move(before);
                    CommitTimelineEdit(v.data, edit);
                };
                if (DragScroll(v, event, es, gsm, g, bodyID, event.rect->rect.h, dragDistance))
                    return true;
                // A key or event under the pointer, an event's label too.
                auto hitAt = [&](float x) {
                    Hit hit = HitOnRow(v, track, g, x, reach);
                    if (hit.ids.empty())
                        for (const LabelSpan& span : labelSpans)
                            if (span.track == track && x >= g.XAt(span.time) + float(span.from) && x <= g.XAt(span.time) + float(span.to))
                                return Hit{ { span.event }, span.time };
                    return hit;
                };
                // The part of the row's clip under the pointer: its ends first, they're narrow.
                auto clipPartAt = [&](float x) {
                    if (track < 0 || !v.data.tracks[size_t(track)].clip)
                        return TimelineViewState::ClipPart::None;
                    const TimelineClip& clip = *v.data.tracks[size_t(track)].clip;
                    const float a = g.XAt(clip.start), b = g.XAt(clip.start + clip.length);
                    if (std::fabs(x - a) <= edgeReach) return TimelineViewState::ClipPart::Start;
                    if (std::fabs(x - b) <= edgeReach) return TimelineViewState::ClipPart::End;
                    return x > a && x < b ? TimelineViewState::ClipPart::Body : TimelineViewState::ClipPart::None;
                };
                switch (event.type)
                {
                    case EventType::MouseDown:
                    {
                        // A right press: what the menu it opens is for. It selects the key under
                        // it as a click does, keeping a selection it's part of.
                        if (event.state.RightPressed())
                        {
                            const Hit hit = hitAt(float(at.x));
                            v.state.menuKeys      = hit.ids;
                            v.state.menuTrack     = track;
                            v.state.menuTime      = g.TimeAt(float(at.x));
                            v.state.menuByPointer = true;
                            if (track >= 0)
                                v.state.selectedTrack = v.data.tracks[size_t(track)].stableID;
                            if (!hit.ids.empty() && !std::all_of(hit.ids.begin(), hit.ids.end(), [v](ID id) { return v.state.selectedKeys.contains(id); }))
                                v.state.selectedKeys = std::unordered_set<ID>(hit.ids.begin(), hit.ids.end());
                            return false;   // the context menu opens on it
                        }
                        if (!event.state.LeftPressed())
                            return false;
                        TakeFocus(gsm, event.id);
                        if (track >= 0)
                            v.state.selectedTrack = v.data.tracks[size_t(track)].stableID;
                        // A clip: its body moves it, its ends trim it (not on a locked track).
                        if (const TimelineViewState::ClipPart part = clipPartAt(float(at.x));
                            part != TimelineViewState::ClipPart::None && !IsTrackLocked(v.data, track))
                        {
                            v.state.clipDrag.Press(event, es, gsm, v.data.tracks, { .focus = false });
                            v.state.clipTrack     = track;
                            v.state.clipPart      = part;
                            v.state.clipPressTime = g.TimeAt(float(at.x));
                            v.state.clipMoved     = false;
                            return true;
                        }
                        const Hit hit = hitAt(float(at.x));
                        if (!hit.ids.empty())
                        {
                            const bool allSelected = std::all_of(hit.ids.begin(), hit.ids.end(), [v](ID id) { return v.state.selectedKeys.contains(id); });
                            if (ctrl)
                            {
                                for (const ID id : hit.ids)
                                    if (allSelected) v.state.selectedKeys.erase(id); else v.state.selectedKeys.insert(id);
                            }
                            else if (shift)
                                v.state.selectedKeys.insert(hit.ids.begin(), hit.ids.end());
                            else if (!allSelected)
                                v.state.selectedKeys = std::unordered_set<ID>(hit.ids.begin(), hit.ids.end());
                            if (!ctrl || !allSelected)
                            {
                                v.state.keyDrag.Press(event, es, gsm, v.data.tracks, { .focus = false, .capture = false });
                                v.state.keyPressTime = g.TimeAt(float(at.x));
                                v.state.keyMoved = false;
                            }
                            return true;
                        }
                        v.state.boxSelect.Press(event, es, gsm, (shift || ctrl) ? v.state.selectedKeys : std::unordered_set<ID>{},
                                           { .focus = false, .capture = false });
                        v.state.boxFrom = v.state.boxTo = g.TimeAt(float(at.x));
                        v.state.boxFromRow = v.state.boxToRow = std::clamp(row, 0, std::max(0, (int)rows.size() - 1));
                        return true;
                    }
                    case EventType::MouseMove:
                    {
                        // What's under the pointer, for its hover look; not while dragging.
                        v.state.hover.Track(AnyHeld(v.state.keyDrag, v.state.boxSelect, v.state.dragScroll, v.state.clipDrag), [&] {
                            TimelineViewState::LaneHover under;
                            const Hit hovered = hitAt(float(at.x));
                            under.track    = hovered.ids.empty() ? -1 : track;
                            under.time     = hovered.time;
                            under.clipPart = clipPartAt(float(at.x));
                            under.clipTrack = under.clipPart != TimelineViewState::ClipPart::None ? track : -1;
                            return under;
                        });
                        if (v.state.clipDrag.IsPressed(event.id))
                        {
                            if (!v.state.clipDrag.Move(event, dragDistance))
                                return true;
                            const int t = v.state.clipTrack;
                            if (v.state.clipDrag.before.size() != v.data.tracks.size() || t < 0 || !v.state.clipDrag.before[size_t(t)].clip)
                            {
                                v.state.clipDrag = {};
                                return true;
                            }
                            const TimelineClip& from = *v.state.clipDrag.before[size_t(t)].clip;
                            TimelineClip clip = from;
                            const float delta     = SnappedDelta(v, g.TimeAt(float(at.x)) - v.state.clipPressTime);
                            const float source    = SourceLengthOf(from);
                            const float minLength = std::max(FrameOf(v), 0.01f);
                            switch (v.state.clipPart)
                            {
                                case TimelineViewState::ClipPart::Body:
                                    clip.start = std::max(0.0f, from.start + delta);
                                    break;
                                case TimelineViewState::ClipPart::Start:
                                {
                                    // Plays the sound from further in (or back to its start); the end stays.
                                    // A clip already shorter than the minimum can't be trimmed shorter.
                                    const float d = std::clamp(delta, -from.offset, std::max(-from.offset, from.length - minLength));
                                    clip.start  = from.start + d;
                                    clip.offset = from.offset + d;
                                    clip.length = from.length - d;
                                    clip.sourceLength = source;
                                    break;
                                }
                                case TimelineViewState::ClipPart::End:
                                    clip.length = std::clamp(from.length + delta, minLength, std::max(minLength, source - from.offset));
                                    clip.sourceLength = source;
                                    break;
                                default: break;
                            }
                            v.data.tracks[size_t(t)].clip = clip;
                            v.state.clipMoved = true;
                            return true;
                        }
                        if (v.state.keyDrag.IsPressed(event.id))
                        {
                            if (!v.state.keyDrag.Move(event, dragDistance))
                                return true;
                            if (v.state.keyDrag.before.size() != v.data.tracks.size())
                            {
                                v.state.keyDrag = {};
                                return true;
                            }
                            const float delta = SnappedDelta(v, g.TimeAt(float(at.x)) - v.state.keyPressTime);
                            MoveSelectedKeys(v, v.state.keyDrag.before, delta);
                            v.state.keyMoved = true;
                            return true;
                        }
                        if (v.state.boxSelect.IsPressed(event.id))
                        {
                            if (!v.state.boxSelect.Move(event, dragDistance))
                                return true;
                            v.state.boxTo = g.TimeAt(float(at.x));
                            v.state.boxToRow = std::clamp(row, 0, std::max(0, (int)rows.size() - 1));
                            const float t0 = std::min(v.state.boxFrom, v.state.boxTo), t1 = std::max(v.state.boxFrom, v.state.boxTo);
                            const int r0 = std::min(v.state.boxFromRow, v.state.boxToRow), r1 = std::max(v.state.boxFromRow, v.state.boxToRow);
                            std::unordered_set<ID> selection = v.state.boxSelect.before;
                            auto take = [&](ID id) { if (ctrl && v.state.boxSelect.before.contains(id)) selection.erase(id); else selection.insert(id); };
                            for (int r = r0; r <= r1 && r < (int)rows.size(); ++r)
                            {
                                const int t = rows[size_t(r)];
                                if (t < 0 || t >= (int)v.data.tracks.size())
                                    continue;
                                const TimelineTrack& tr = v.data.tracks[size_t(t)];
                                for (const TimelineKey& key : tr.keys) if (key.time >= t0 && key.time <= t1) take(key.stableID);
                                for (const TimelineEvent& e : tr.events) if (e.time >= t0 && e.time <= t1) take(e.stableID);
                                if (tr.kind == TimelineTrackKind::Group)
                                    for (const TimelineTrack& held : TracksInGroup(v.data, t))
                                        for (const TimelineKey& key : held.keys) if (key.time >= t0 && key.time <= t1) take(key.stableID);
                            }
                            v.state.selectedKeys = std::move(selection);
                            return true;
                        }
                        return false;
                    }
                    case EventType::MouseUp:
                    {
                        if (v.state.clipDrag.IsPressed(event.id))
                        {
                            v.state.clipDrag.Release(event);
                            if (v.state.clipMoved)
                            {
                                // Already where it's dropped: the edit, for undo.
                                const TimelineEdit edit = ChangesSince(v, v.state.clipDrag.before);
                                if (!edit.empty())
                                {
                                    ++v.data.revision;
                                    if (v.data.onEdit)
                                        v.data.onEdit(edit);
                                }
                            }
                            v.state.clipDrag = {};
                            return true;
                        }
                        if (v.state.keyDrag.IsPressed(event.id))
                        {
                            v.state.keyDrag.Release(event);
                            if (v.state.keyMoved)
                            {
                                // Already where it's dropped: the edit, for undo.
                                const TimelineEdit edit = ChangesSince(v, v.state.keyDrag.before);
                                if (!edit.empty())
                                {
                                    ++v.data.revision;
                                    if (v.data.onEdit)
                                        v.data.onEdit(edit);
                                }
                            }
                            v.state.keyDrag = {};
                            return true;
                        }
                        if (const PointerDrag::Ended ended = v.state.boxSelect.Release(event); ended != PointerDrag::Ended::No)
                        {
                            // A click on the empty lanes clears the selection.
                            if (ended == PointerDrag::Ended::Click && !shift && !ctrl)
                                v.state.selectedKeys.clear();
                            return true;
                        }
                        return false;
                    }
                    case EventType::MouseScroll:
                        return WheelOverTime(v, event, g, float(at.x), wheelMode);
                    case EventType::KeyDown:
                    {
                        for (Gesture<std::vector<TimelineTrack>>* drag : { &v.state.clipDrag, &v.state.keyDrag })
                            if (drag->Cancel(event, gsm))
                            {
                                if (drag->before.size() == v.data.tracks.size())
                                    v.data.tracks = drag->before;
                                *drag = {};
                                return true;
                            }
                        if (v.state.boxSelect.Cancel(event, gsm, v.state.selectedKeys))
                            return true;
                        if (gsm.Interaction().GetFocusedID() != event.id || event.state.consumed)
                            return false;
                        const float frame = FrameOf(v);
                        const float step = frame > 0.0f ? frame : length / 100.0f;
                        switch (event.state.lastKey)
                        {
                            case KeyCode::Delete: case KeyCode::Backspace:
                                CommitTimelineEdit(v.data, DeleteSelectedKeysEdit(v));
                                return true;
                            case KeyCode::Escape:
                                if (v.state.selectedKeys.empty())
                                    return false;
                                v.state.selectedKeys.clear();
                                return true;
                            case KeyCode::KeyA:
                                if (!ctrl)
                                    return false;
                                for (const TimelineTrack& tr : v.data.tracks)
                                {
                                    for (const TimelineKey& key : tr.keys) v.state.selectedKeys.insert(key.stableID);
                                    for (const TimelineEvent& e : tr.events) v.state.selectedKeys.insert(e.stableID);
                                }
                                return true;
                            case KeyCode::KeyK:
                            {
                                for (int t = 0; t < (int)v.data.tracks.size(); ++t)
                                    if (v.data.tracks[size_t(t)].stableID == v.state.selectedTrack)
                                        CommitTimelineEdit(v.data, AddKeyEdit(v.data, t, v.data.playback.time, idManager.CreateStaticID()));
                                return true;
                            }
                            case KeyCode::KeyI: case KeyCode::KeyO:
                            {
                                // The loop range's start (I) or end (O) at the playhead.
                                const TimelineRange before = v.data.loopRange;
                                TimelineRange r = before.empty() ? TimelineRange{ 0.0f, length } : before;
                                if (event.state.lastKey == KeyCode::KeyI) r.start = v.data.playback.time; else r.end = v.data.playback.time;
                                if (r.empty())
                                    return true;
                                CommitTimelineEdit(v.data, TimelineEdit{ .loopFrom = before, .loopTo = r });
                                return true;
                            }
                            case KeyCode::Left: case KeyCode::Right:
                            {
                                const float delta = event.state.lastKey == KeyCode::Left ? -step : step;
                                if (!v.state.selectedKeys.empty())
                                    commitMove(delta);
                                else
                                {
                                    v.data.playback.time = std::clamp(v.data.playback.time + delta, 0.0f, length);
                                    v.data.playback.playing = false;
                                    playbackChanged();
                                }
                                return true;
                            }
                            case KeyCode::Space:
                                TogglePlay(v.data.playback, length);
                                playbackChanged();
                                return true;
                            case KeyCode::Home:
                                v.data.playback.time = 0.0f;
                                playbackChanged();
                                return true;
                            case KeyCode::End:
                                v.data.playback.time = length;
                                v.data.playback.playing = false;
                                playbackChanged();
                                return true;
                            default: return false;
                        }
                    }
                    default: return false;
                }
            };
        }
        const int lanesHeight = rowHeight * std::max(1, (int)rows.size());
        const ElementRef lanesRef = RegisterControl(
            Element{ .id = lanesID, .eventHandler = std::move(lanesHandler), .styleRef = theme.timelineLanesStyleRef,
                     .flags = view.data.playback.playing ? ElementFlags::Animated : ElementFlags::None,
                     .state = guiStateManager.GetCurrentState(lanesID) },
            LayoutOverride{}.MinHeight(Pixels{ lanesHeight }).FlexGrow(1));
        VERIFY(elementStorage.AddPostLayout(lanesRef, [v](PostLayoutContext& ctx) { v.state.lanesWidth = ctx.ElementInnerRect().w; }));

        // The lanes' menu: on a key or event, delete it (the selection, when it's in it); on a
        // lane, a key there, the playhead there, select all.
        if (!disabled && BeginContextMenu(lanesID))
        {
            if (view.state.menuOpening != ContextMenuOpenings())
            {
                view.state.menuOpening = ContextMenuOpenings();
                if (!view.state.menuByPointer)
                {
                    // From the keyboard: the selection, or the selected track at the playhead.
                    view.state.menuKeys.assign(view.state.selectedKeys.begin(), view.state.selectedKeys.end());
                    view.state.menuTrack = -1;
                    for (int t = 0; t < (int)view.data.tracks.size(); ++t)
                        if (view.data.tracks[size_t(t)].stableID == view.state.selectedTrack)
                            view.state.menuTrack = t;
                    view.state.menuTime = view.data.playback.time;
                }
                view.state.menuByPointer = false;
            }
            if (!view.state.menuKeys.empty())
            {
                const size_t count = std::max(view.state.menuKeys.size(), view.state.selectedKeys.size());
                bool isEvent = false;
                for (const TimelineTrack& tr : view.data.tracks)
                    for (const TimelineEvent& e : tr.events)
                        isEvent |= e.stableID == view.state.menuKeys[0];
                const std::string label = count > 1 ? std::format("Delete {} keys", count) : isEvent ? "Delete event" : "Delete key";
                MenuItem(label, CommandManager::FormatShortcut(KeyCode::Delete, KeyModifier::None), [v] { CommitTimelineEdit(v.data, DeleteSelectedKeysEdit(v)); });
            }
            else
            {
                const int t = view.state.menuTrack;
                if (t >= 0 && t < (int)view.data.tracks.size() && view.data.tracks[size_t(t)].kind == TimelineTrackKind::Property)
                {
                    const bool locked = IsTrackLocked(view.data, t);   // shown, but it can't add one
                    if (locked) BeginDisabled();
                    MenuItem("Add key", [this, v, t] {
                        const ID keyID = idManager.CreateStaticID();
                        const TimelineEdit edit = AddKeyEdit(v.data, t, v.state.menuTime, keyID);
                        if (edit.empty())
                            return;
                        CommitTimelineEdit(v.data, edit);
                        v.state.selectedKeys = { keyID };
                    });
                    if (locked) EndDisabled();
                }
                MenuItem("Move playhead here", [v, playbackChanged] {
                    v.data.playback.time = std::clamp(SnappedTime(v, v.state.menuTime), 0.0f, LengthOf(v));
                    v.data.playback.playing = false;
                    playbackChanged();
                });
                MenuSeparator();
                MenuItem("Select all", CommandManager::FormatShortcut(KeyCode::KeyA, KeyModifier::Control), [v] {
                    for (const TimelineTrack& tr : v.data.tracks)
                    {
                        for (const TimelineKey& key : tr.keys) v.state.selectedKeys.insert(key.stableID);
                        for (const TimelineEvent& e : tr.events) v.state.selectedKeys.insert(e.stableID);
                    }
                });
            }
        }
        if (!disabled)
            EndContextMenu();
        {
            // What the draw needs, by row: the track, whether it's selected, its keys.
            struct RowDraw
            {
                TimelineTrackKind kind;
                bool              selected;
                std::vector<std::pair<float, bool>> keys;   // time, selected
                std::vector<float> summary;
                std::optional<TimelineClip> clip;
                std::vector<std::pair<float, bool>> events;
                std::optional<float> hovered;               // the time of what's under the pointer
                TimelineViewState::ClipPart clipHovered = TimelineViewState::ClipPart::None;
            };
            std::vector<RowDraw> drawn;
            drawn.reserve(rows.size());
            const bool lanesHovered = (guiStateManager.GetCurrentState(lanesID) & ElementState::Hover) != ElementState::None;
            const TimelineViewState::LaneHover laneHover = view.state.hover.Shown(lanesHovered);
            for (const int t : rows)
            {
                const TimelineTrack& track = view.data.tracks[size_t(t)];
                RowDraw row{ track.kind, view.state.selectedTrack == track.stableID, {}, {}, track.clip, {}, std::nullopt };
                if (t == laneHover.track)
                    row.hovered = laneHover.time;
                if (t == laneHover.clipTrack)
                    row.clipHovered = laneHover.clipPart;
                for (const TimelineKey& key : track.keys) row.keys.push_back({ key.time, view.state.selectedKeys.contains(key.stableID) });
                for (const TimelineEvent& e : track.events) row.events.push_back({ e.time, view.state.selectedKeys.contains(e.stableID) });
                if (track.kind == TimelineTrackKind::Group)
                    row.summary = SummaryTimes(view, t);
                drawn.push_back(std::move(row));
            }
            const bool boxing = view.state.boxSelect.IsDragging(lanesID);
            const float boxT0 = std::min(view.state.boxFrom, view.state.boxTo), boxT1 = std::max(view.state.boxFrom, view.state.boxTo);
            const int boxR0 = std::min(view.state.boxFromRow, view.state.boxToRow), boxR1 = std::max(view.state.boxFromRow, view.state.boxToRow);
            const TimelineRange loop = view.data.loopRange;
            const float time = view.data.playback.time;
            const GUITheme& th = theme;
            const VisualBoxStyleIndex laneSelected = th.timelineLaneSelectedStyleRef.visualBoxStyleIndex, rowLine = th.timelineRowLineStyleRef.visualBoxStyleIndex;
            const VisualBoxStyleIndex outside = th.timelineOutsideStyleRef.visualBoxStyleIndex, clipBox = th.timelineClipStyleRef.visualBoxStyleIndex;
            const VisualBoxStyleIndex clipEdge = th.timelineLoopEdgeStyleRef.visualBoxStyleIndex;
            const VisualBoxStyleIndex eventLine = th.timelineEventStyleRef.visualBoxStyleIndex, eventChip = th.timelineEventChipStyleRef.visualBoxStyleIndex;
            const VisualBoxStyleIndex selectedLine = th.timelinePlayheadStyleRef.visualBoxStyleIndex, boxStyle = th.timelineBoxSelectStyleRef.visualBoxStyleIndex;
            const VisualColorStyleIndex keyFill = th.timelineKeyColor, keyEdge = th.timelineKeyEdgeColor, keySelected = th.timelineKeySelectedColor,
                                        keySelectedEdge = th.timelineKeySelectedEdgeColor, summary = th.timelineSummaryKeyColor, wave = th.timelineWaveColor;
            const float keyHalf = float(styleManager.Px(10)) * 0.7071f, selectedHalf = float(styleManager.Px(11)) * 0.7071f;
            const float summaryHalf = float(styleManager.Px(8)) * 0.7071f, edgeWidth = float(std::max(1, styleManager.Px(1))) * 1.4142f;
            const float hoverGrow = float(std::max(1, styleManager.Px(2))) * 0.7071f;   // a hovered diamond, 2px larger across
            const int32_t px1 = std::max(1, styleManager.Px(1)), px2 = std::max(1, styleManager.Px(2)), px3 = styleManager.Px(3), px4 = styleManager.Px(4);
            const int32_t px5 = styleManager.Px(5), barGap = std::max(1, styleManager.Px(1)), barWidth = std::max(1, styleManager.Px(2));
            VERIFY(elementStorage.AddCustomDraw(lanesRef, [=](CustomDrawContext& ctx)
            {
                const IntRect outer = ctx.ElementRect(), inner = ctx.ElementInnerRect();
                const Geometry g{ range, float(inner.x), float(inner.w) };
                const float left = float(inner.x) - float(px5), right = float(inner.x + inner.w) + float(px5);
                auto shown = [&](float x) { return x >= left && x <= right; };
                for (int r = 0; r < (int)drawn.size(); ++r)
                {
                    const RowDraw& row = drawn[size_t(r)];
                    const int32_t y = outer.y + r * rowHeight;
                    const float cy = float(y) + float(rowHeight) * 0.5f;
                    if (row.selected)
                        ctx.RenderBox(IntRect{ outer.x, y, outer.w, rowHeight }, outer, laneSelected);
                    ctx.RenderBox(IntRect{ outer.x, y + rowHeight - px1, outer.w, px1 }, outer, rowLine);
                    if (row.clip && row.clip->length > 0.0f)
                    {
                        const int32_t a = int32_t(g.XAt(row.clip->start)), b = int32_t(g.XAt(row.clip->start + row.clip->length));
                        const IntRect box{ a, y + px3, b - a, rowHeight - 2 * px3 };
                        ctx.RenderBox(box, outer, clipBox, row.clipHovered != TimelineViewState::ClipPart::None ? ElementState::Hover : ElementState::None);
                        // An end under the pointer: a bar there, as the loop range's.
                        if (row.clipHovered == TimelineViewState::ClipPart::Start || row.clipHovered == TimelineViewState::ClipPart::End)
                            ctx.RenderBox(IntRect{ row.clipHovered == TimelineViewState::ClipPart::Start ? a : b - px2, box.y, px2, box.h }, outer, clipEdge);
                        // The waveform: a bar a few pixels, each the loudest of the peaks it covers, of
                        // the part of the sound the clip plays.
                        const std::vector<float>& peaks = row.clip->peaks;
                        const float source = std::max(SourceLengthOf(*row.clip), 1e-6f);
                        const float shownFrom = row.clip->offset / source, shownSpan = row.clip->length / source;
                        const int32_t room = box.w - 2 * px4;
                        const int bars = std::min(int(float(peaks.size()) * std::min(1.0f, shownSpan)) + 1, room / (barWidth + barGap));
                        for (int i = 0; i < bars && !peaks.empty(); ++i)
                        {
                            const float n = float(peaks.size());
                            const size_t from = std::min(peaks.size() - 1, size_t((shownFrom + shownSpan * float(i) / float(bars)) * n));
                            const size_t to   = std::clamp(size_t((shownFrom + shownSpan * float(i + 1) / float(bars)) * n), from + 1, peaks.size());
                            float peak = 0.0f;
                            for (size_t p = from; p < to; ++p) peak = std::max(peak, Finite(peaks[p], 0.0f));
                            const int32_t h = std::max(px1, int32_t(std::clamp(peak, 0.0f, 1.0f) * float(box.h - 2 * px3)));
                            const IntRect bar{ box.x + px4 + i * (barWidth + barGap), y + (rowHeight - h) / 2, barWidth, h };
                            ctx.RenderQuad(bar, outer, wave, 1.0f);
                        }
                    }
                    // What's under the pointer: a little larger, with the light edge.
                    auto isHovered = [&row](float t) { return row.hovered && std::fabs(*row.hovered - t) < 1e-4f; };
                    for (const auto& [t, selected] : row.events)
                    {
                        const int32_t x = int32_t(g.XAt(t));
                        const int32_t w = isHovered(t) ? px3 : px2;
                        ctx.RenderBox(IntRect{ x - (w - px2) / 2, y + px4, w, rowHeight - 2 * px4 }, outer, selected ? selectedLine : eventLine);
                    }
                    for (const float t : row.summary)
                    {
                        const float x = g.XAt(t);
                        if (!shown(x)) continue;
                        const bool hovered = isHovered(t);
                        const float half = summaryHalf + (hovered ? hoverGrow : 0.0f);
                        Diamond(ctx, x, cy, half + edgeWidth, hovered ? keySelectedEdge : keyEdge);
                        Diamond(ctx, x, cy, half, summary);
                    }
                    for (const auto& [t, selected] : row.keys)
                    {
                        const float x = g.XAt(t);
                        if (!shown(x)) continue;
                        const bool hovered = isHovered(t);
                        const float half = (selected ? selectedHalf : keyHalf) + (hovered ? hoverGrow : 0.0f);
                        Diamond(ctx, x, cy, half + edgeWidth, selected || hovered ? keySelectedEdge : keyEdge);
                        Diamond(ctx, x, cy, half, selected ? keySelected : keyFill);
                    }
                }
                for (const EventLabel& label : eventLabels)
                {
                    const int32_t x = int32_t(g.XAt(label.time)) + px2 + px4;
                    const int32_t h = label.size.y + 2 * px1, y = outer.y + label.row * rowHeight + (rowHeight - h) / 2;
                    // Its event under the pointer (the line or the label): the chip shows it.
                    const auto& hovered = drawn[size_t(label.row)].hovered;
                    const bool hover = hovered && std::fabs(*hovered - label.time) < 1e-4f;
                    ctx.RenderBox(IntRect{ x, y, label.size.x + 2 * px5, h }, outer, eventChip, hover ? ElementState::Hover : ElementState::None);
                    ctx.RenderText(label.text, IntRect{ x + px5, y + px1, label.size.x, label.size.y });
                }
                // Outside the loop range, dimmed.
                if (!loop.empty())
                {
                    const int32_t a = int32_t(g.XAt(loop.start)), b = int32_t(g.XAt(loop.end));
                    if (a > outer.x) ctx.RenderBox(IntRect{ outer.x, outer.y, a - outer.x, outer.h }, outer, outside);
                    if (b < outer.x + outer.w) ctx.RenderBox(IntRect{ b, outer.y, outer.x + outer.w - b, outer.h }, outer, outside);
                }
                if (boxing)
                {
                    const int32_t a = int32_t(g.XAt(boxT0)), b = int32_t(g.XAt(boxT1));
                    ctx.RenderBox(IntRect{ a, outer.y + boxR0 * rowHeight + px2, std::max(px1, b - a), (boxR1 - boxR0 + 1) * rowHeight - 2 * px2 },
                                  outer, boxStyle);
                }
                if (time >= range.start && time <= range.end)
                    ctx.RenderBox(IntRect{ int32_t(g.XAt(time)), outer.y, px1, outer.h }, outer, selectedLine);
            }));
        }
        EndArea();

        // ---- The scroll bar: the part in view, along the whole ----------------------
        const ID scrollID = idManager.AllocateDynamicID("scroll");
        EventHandler scrollHandler;
        if (!disabled)
        {
            const ElementStorage& es = elementStorage;
            GUIStateManager& gsm = guiStateManager;
            scrollHandler = [&gsm, &es, v](ElementEvent&& event) -> bool
            {
                if (event.rect == nullptr)
                    return false;
                const IntRect inner = event.rect->innerRect;
                const float length = LengthOf(v);
                const TimelineRange now = ViewRangeOf(v);
                const float span = now.end - now.start;
                if (length <= 0.0f || span >= length)
                    return false;
                const float x = float(event.LocalCursor(es).x);
                const float perPixel = length / float(std::max(1, inner.w));
                switch (event.type)
                {
                    case EventType::MouseDown:
                    {
                        if (!event.state.LeftPressed())
                            return false;
                        // Off the thumb, the thumb comes to the pointer first.
                        const float thumbFrom = float(inner.x) + now.start / perPixel, thumbTo = float(inner.x) + now.end / perPixel;
                        if (x < thumbFrom || x > thumbTo)
                            v.state.viewStart = std::clamp((x - float(inner.x)) * perPixel - span * 0.5f, 0.0f, length - span);
                        if (v.state.viewLength <= 0.0f) v.state.viewLength = span;
                        v.state.pan.Press(event, es, gsm, v.state.viewStart, { .atOnce = true, .focus = false, .capture = false });
                        return true;
                    }
                    case EventType::MouseMove:
                        if (!v.state.pan.Move(event, 0))
                            return false;
                        v.state.viewStart = std::clamp(v.state.pan.before + float(v.state.pan.drag.LocalDelta(event, es).x) * perPixel, 0.0f, length - span);
                        return true;
                    case EventType::MouseUp:
                        return v.state.pan.Release(event) != PointerDrag::Ended::No;
                    case EventType::KeyDown:
                        return v.state.pan.Cancel(event, gsm, v.state.viewStart);
                    default: return false;
                }
            };
        }
        const ElementRef scrollRef = RegisterElement(
            Element{ .id = scrollID, .eventHandler = std::move(scrollHandler), .styleRef = theme.timelineScrollStyleRef },
            LayoutOverride{});
        {
            const VisualBoxStyleIndex thumb = theme.timelineScrollThumbStyleRef.visualBoxStyleIndex;
            const int32_t thick = styleManager.Px(6);
            // A press anywhere on the bar takes the thumb, so it lights up while the bar is hovered.
            const ElementState thumbState = view.state.pan.IsHeld() || (guiStateManager.GetCurrentState(scrollID) & ElementState::Hover) != ElementState::None
                                          ? ElementState::Hover : ElementState::None;
            VERIFY(elementStorage.AddCustomDraw(scrollRef, [=](CustomDrawContext& ctx)
            {
                const IntRect inner = ctx.ElementInnerRect();
                if (length <= 0.0f || inner.w <= 0)
                    return;
                const int32_t a = inner.x + int32_t(range.start / length * float(inner.w));
                const int32_t b = inner.x + int32_t(range.end / length * float(inner.w));
                ctx.RenderBox(IntRect{ a, inner.y + (inner.h - thick) / 2, std::max(thick, b - a), thick }, inner, thumb, thumbState);
            }));
        }

        if (pushed)
            (void)idManager.PopParent();
        EndVertical();
    }
}
