#include <gui_controls/gui_application_internal.h>
#include <gui_controls/gui_timeline_internal.h>

#include <gui_core/graphics/gui_custom_draw_context.h>
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

        float LengthOf(const TimelineView& view)
        {
            return std::max(0.0f, Finite(view.duration, 0.0f));
        }

        float FrameOf(const TimelineView& view)
        {
            const float fps = Finite(view.fps, 0.0f);
            return fps > 0.0f ? 1.0f / fps : 0.0f;
        }

        // The part of the timeline in view: its start and length, within the whole.
        TimelineRange ViewRangeOf(const TimelineView& view)
        {
            const float length = LengthOf(view);
            const float shown  = Finite(view.viewLength, 0.0f);
            const float span   = (shown <= 0.0f || shown >= length) ? length : shown;
            const float start  = std::clamp(Finite(view.viewStart, 0.0f), 0.0f, length - span);
            return { start, start + span };
        }

        // The shortest stretch a zoom shows: ten frames, or a two-hundredth of the length.
        float MinViewLength(const TimelineView& view)
        {
            const float length = LengthOf(view);
            return std::min(length, std::max(length / 200.0f, 10.0f * FrameOf(view)));
        }

        // Shows `span` seconds, keeping the time `anchor` (inside the view) where it is.
        void ZoomTo(TimelineView& view, float span, float anchor)
        {
            const float length = LengthOf(view);
            const TimelineRange now = ViewRangeOf(view);
            span = std::clamp(span, MinViewLength(view), length);
            const float part = now.end > now.start ? (anchor - now.start) / (now.end - now.start) : 0.5f;
            view.viewLength = span >= length ? 0.0f : span;
            view.viewStart  = std::clamp(anchor - part * span, 0.0f, length - span);
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
        std::vector<int> ShownRows(const TimelineView& view)
        {
            const int count = (int)view.tracks.size();
            std::vector<bool> kept(size_t(count), view.filter.empty());
            if (!view.filter.empty())
            {
                const std::string needle = Lower(view.filter);
                for (int t = 0; t < count; ++t)
                {
                    if (Lower(view.tracks[size_t(t)].name).find(needle) == std::string::npos)
                        continue;
                    // It, and each group it's in.
                    kept[size_t(t)] = true;
                    int depth = view.tracks[size_t(t)].depth;
                    for (int g = t - 1; g >= 0 && depth > 0; --g)
                        if (view.tracks[size_t(g)].depth < depth)
                        {
                            kept[size_t(g)] = true;
                            depth = view.tracks[size_t(g)].depth;
                        }
                }
            }
            std::vector<int> rows;
            int collapsedAt = INT_MAX;   // the depth of the collapsed group being skipped
            for (int t = 0; t < count; ++t)
            {
                const TimelineTrack& track = view.tracks[size_t(t)];
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
        std::vector<float> SummaryTimes(const TimelineView& view, int group)
        {
            std::vector<float> times;
            for (const TimelineTrack& track : TracksInGroup(view, group))
                for (const TimelineKey& key : track.keys)
                    times.push_back(key.time);
            std::sort(times.begin(), times.end());
            times.erase(std::unique(times.begin(), times.end(), [](float a, float b) { return std::fabs(a - b) < 1e-4f; }), times.end());
            return times;
        }

        // The keys at `time` (within a hair) in the tracks a group holds.
        std::vector<ID> KeysAtInGroup(const TimelineView& view, int group, float time)
        {
            std::vector<ID> ids;
            for (const TimelineTrack& track : TracksInGroup(view, group))
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

        Hit HitOnRow(const TimelineView& view, int track, const Geometry& geometry, float x, float reach)
        {
            Hit hit;
            if (track < 0 || track >= (int)view.tracks.size())
                return hit;
            const TimelineTrack& t = view.tracks[size_t(track)];
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
                    for (const float time : SummaryTimes(view, track)) consider(time, KeysAtInGroup(view, track, time));
                    break;
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

        // `from` with the selected keys and events of unlocked tracks moved by `delta`, kept
        // inside the length.
        std::vector<TimelineTrack> MovedKeys(const TimelineView& view, const std::vector<TimelineTrack>& from, float delta)
        {
            const float length = LengthOf(view);
            std::vector<TimelineTrack> moved = from;
            for (int t = 0; t < (int)moved.size(); ++t)
            {
                if (IsTrackLocked(view, t))
                    continue;
                TimelineTrack& track = moved[size_t(t)];
                bool any = false;
                for (TimelineKey& key : track.keys)
                    if (view.selectedKeys.contains(key.stableID)) { key.time = std::clamp(key.time + delta, 0.0f, length); any = true; }
                for (TimelineEvent& event : track.events)
                    if (view.selectedKeys.contains(event.stableID)) { event.time = std::clamp(event.time + delta, 0.0f, length); any = true; }
                if (any)
                    SortTimes(track);
            }
            return moved;
        }

        // The edit taking the tracks from `before` to how they are now.
        TimelineEdit ChangesSince(const TimelineView& view, const std::vector<TimelineTrack>& before)
        {
            TimelineEdit edit;
            if (before.size() != view.tracks.size())
                return edit;
            for (int t = 0; t < (int)before.size(); ++t)
                if (!(before[size_t(t)] == view.tracks[size_t(t)]))
                    edit.changes.push_back({ t, before[size_t(t)], view.tracks[size_t(t)] });
            return edit;
        }

        // A delta on frames when the view snaps.
        float SnappedDelta(const TimelineView& view, float delta)
        {
            const float frame = FrameOf(view);
            return (view.snap && frame > 0.0f) ? std::round(delta / frame) * frame : delta;
        }

        float SnappedTime(const TimelineView& view, float time)
        {
            const float frame = FrameOf(view);
            return SnapToFrame(time, view.snap ? frame : 0.0f, LengthOf(view));
        }

        // Every key's and event's time, in order: where Previous / Next key go.
        std::vector<float> AllKeyTimes(const TimelineView& view)
        {
            std::vector<float> times;
            for (const TimelineTrack& track : view.tracks)
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
        bool WheelOverTime(TimelineView& view, const ElementEvent& event, const Geometry& g, float x, WheelMode mode)
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
                view.viewStart = std::clamp(after.start + nav.pan.x * shown * 0.1f, 0.0f, std::max(0.0f, length - shown));
            }
            return true;
        }

        // The middle button (or Alt + left) held over the ruler or the lanes: the time follows
        // the pointer across, and the rows (with `body`, the area they scroll in) up and down.
        // Escape puts both back.
        bool DragScroll(TimelineView& view, const ElementEvent& event, const ElementStorage& es, GUIStateManager& gsm,
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
                    view.dragScroll.Press(event, es, button);
                    gsm.Interaction().SetCaptureID(event.id);
                    view.dragScrollStart  = ViewRangeOf(view).start;
                    view.dragScrollOffset = vertical ? scrolling.GetScrollOffset(handle).y : 0;
                    return true;
                }
                case EventType::MouseMove:
                {
                    if (!view.dragScroll.IsPressed(event.id))
                        return false;
                    if (!view.dragScroll.Move(event, dragDistance))
                        return true;
                    // Screen pixels: the rows move under the pointer as they scroll.
                    const int2 d = view.dragScroll.ScreenDelta(event.state);
                    const float length = LengthOf(view);
                    const TimelineRange now = ViewRangeOf(view);
                    const float span = now.end - now.start;
                    if (span < length)
                        view.viewStart = std::clamp(view.dragScrollStart - float(d.x) * span / std::max(1.0f, g.width), 0.0f, length - span);
                    if (vertical)
                        if (const ElementRect* area = es.GetElementRectPtr(es.FindElementRefByID(body)))
                        {
                            const int32_t most = std::max(0, contentHeight - area->innerRect.h);
                            const int2 offset = scrolling.GetScrollOffset(handle);
                            scrolling.SetScrollOffset(handle, int2{ offset.x, std::clamp(view.dragScrollOffset + d.y, -most, 0) });
                        }
                    return true;
                }
                case EventType::MouseUp:
                    return view.dragScroll.IsPressed(event.id) && view.dragScroll.Release(event) != PointerDrag::Ended::No;
                case EventType::KeyDown:
                    if (!IsDragCancelKey(event.state) || !view.dragScroll.Cancel(event.id))
                        return false;
                    view.viewStart = view.dragScrollStart;
                    if (vertical)
                        scrolling.SetScrollOffset(handle, int2{ scrolling.GetScrollOffset(handle).x, view.dragScrollOffset });
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

    void GUIApplication::TimelineEditor(ID id, TimelineView& view, LayoutOverride&& constraints)
    {
        const float length = LengthOf(view);
        const float frame  = FrameOf(view);
        view.playback.time = std::clamp(Finite(view.playback.time, 0.0f), 0.0f, length);
        TimelineView* v = &view;
        const bool disabled = guiStateManager.Construction().IsDisabled();

        // The zoom slider moved last frame: show that much, round the playhead. Then the
        // slider shows the zoom, however it got there.
        {
            const float minimum = MinViewLength(view);
            if (view.zoom != view.zoomShown && length > 0.0f && minimum < length)
            {
                const float part = std::clamp(view.zoom, 0, 100) / 100.0f;
                ZoomTo(view, length * std::pow(minimum / length, part), view.playback.time);
            }
            const TimelineRange range = ViewRangeOf(view);
            const float span = range.end - range.start;
            view.zoom = (length > 0.0f && minimum < length && span > 0.0f)
                ? std::clamp(int(std::lround(100.0f * std::log(span / length) / std::log(minimum / length))), 0, 100) : 0;
            view.zoomShown = view.zoom;
        }
        const TimelineRange range = ViewRangeOf(view);
        const std::vector<int> rows = ShownRows(view);
        const int32_t rowHeight = styleManager.Px(28);

        BeginVertical(id, theme.timelineEditorStyleRef, std::move(constraints));
        [[maybe_unused]] const bool pushed = idManager.PushParent(id);
        auto playbackChanged = [v] { if (v->onPlaybackChanged) v->onPlaybackChanged(); };

        // ---- The toolbar ----------------------------------------------------------
        BeginHorizontal(idManager.AllocateDynamicID("toolbar"), theme.timelineToolbarStyleRef);
        BeginHorizontal(idManager.AllocateDynamicID("transport"), theme.timelineTransportStyleRef);
        Button(idManager.AllocateDynamicID("start"), GUIContent(theme.timelineStartIcon, {}, "Go to the start"),
               [v, playbackChanged] { v->playback.time = 0.0f; playbackChanged(); }, theme.timelineButtonStyleRef);
        Button(idManager.AllocateDynamicID("prev"), GUIContent(theme.timelinePrevKeyIcon, {}, "The previous key"),
               [v, playbackChanged] {
                   const std::vector<float> times = AllKeyTimes(*v);
                   const auto it = std::lower_bound(times.begin(), times.end(), v->playback.time - 1e-4f);
                   v->playback.time = it == times.begin() ? 0.0f : *std::prev(it);
                   v->playback.playing = false;
                   playbackChanged();
               }, theme.timelineButtonStyleRef);
        Button(idManager.AllocateDynamicID("play"),
               view.playback.playing ? GUIContent(theme.timelinePauseIcon, {}, "Pause") : GUIContent(theme.timelinePlayIcon, {}, "Play"),
               [v, length, playbackChanged] { TogglePlay(v->playback, length); playbackChanged(); }, theme.timelinePlayStyleRef);
        Button(idManager.AllocateDynamicID("next"), GUIContent(theme.timelineNextKeyIcon, {}, "The next key"),
               [v, length, playbackChanged] {
                   const std::vector<float> times = AllKeyTimes(*v);
                   const auto it = std::upper_bound(times.begin(), times.end(), v->playback.time + 1e-4f);
                   v->playback.time = it == times.end() ? length : *it;
                   v->playback.playing = false;
                   playbackChanged();
               }, theme.timelineButtonStyleRef);
        Button(idManager.AllocateDynamicID("end"), GUIContent(theme.timelineEndIcon, {}, "Go to the end"),
               [v, length, playbackChanged] { v->playback.time = length; v->playback.playing = false; playbackChanged(); },
               theme.timelineButtonStyleRef);
        Toggle(idManager.AllocateDynamicID("loop"), view.playback.loop, GUIContent(theme.timelineLoopIcon, {}, "Loop"),
               [v](bool loop) { v->playback.loop = loop; }, theme.timelineLoopStyleRef);
        EndHorizontal();
        RegisterElement(Element{ .id = idManager.AllocateDynamicID("divider"), .styleRef = theme.timelineDividerStyleRef,
                                 .flags = ElementFlags::NoInput }, LayoutOverride{});

        BeginHorizontal(idManager.AllocateDynamicID("time"), theme.timelineTimeStyleRef);
        Text(idManager.AllocateDynamicID("now"), ClockText(view.playback.time), theme.timelineTimeCurrentStyleRef);
        Text(idManager.AllocateDynamicID("length"), "/ " + ClockText(length), theme.timelineTimeTotalStyleRef);
        if (frame > 0.0f)
            Text(idManager.AllocateDynamicID("frame"), FrameText(view.playback.time, frame, length), theme.timelineFrameStyleRef);
        EndHorizontal();

        // The frame rate: the usual ones, and the view's own if it's another.
        {
            const std::string shown = std::format("{:g} fps", Finite(view.fps, 0.0f));
            if (BeginMenu(idManager.AllocateDynamicID("fps"), shown, theme.timelineFpsStyleRef, theme.menuStyleRef, theme.menuItemRowStyleRef))
            {
                for (const float fps : { 12.0f, 24.0f, 25.0f, 30.0f, 48.0f, 50.0f, 60.0f })
                    MenuItem(std::format("{:g} fps", fps), [v, fps] { v->fps = fps; });
            }
            EndMenu();
        }
        Toggle(idManager.AllocateDynamicID("snap"), view.snap, GUIContent(theme.timelineSnapIcon, "Snap", "Keys and the loop range land on frames"),
               [v](bool snap) { v->snap = snap; }, theme.timelineSnapStyleRef);

        BeginHorizontal(idManager.AllocateDynamicID("zoom"), theme.timelineZoomStyleRef);
        RegisterElement(Element{ .id = idManager.AllocateDynamicID("zoomOut"), .iconID = theme.timelineZoomOutIcon,
                                 .styleRef = theme.timelineZoomStyleRef, .flags = ElementFlags::NoInput },
                        LayoutOverride{}.FlexGrow(0));
        Slider(idManager.AllocateDynamicID("zoomSlider"), view.zoom, 0, 100, LayoutOverride{}.Width(110));
        SetTooltip("Zoom (the wheel over the lanes)");
        RegisterElement(Element{ .id = idManager.AllocateDynamicID("zoomIn"), .iconID = theme.timelineZoomInIcon,
                                 .styleRef = theme.timelineZoomStyleRef, .flags = ElementFlags::NoInput },
                        LayoutOverride{}.FlexGrow(0));
        EndHorizontal();
        EndHorizontal();

        // ---- The header: the list's, and the ruler ---------------------------------
        const ID lanesID = idManager.AllocateDynamicID("lanes");
        BeginHorizontal(idManager.AllocateDynamicID("header"), theme.timelineHeaderStyleRef);
        BeginHorizontal(idManager.AllocateDynamicID("listHeader"), theme.timelineListHeaderStyleRef);
        Text(idManager.AllocateDynamicID("tracks"), "Tracks", theme.timelineListTitleStyleRef);
        SearchField(idManager.AllocateDynamicID("filter"), view.filter, theme.searchIcon, TextEditBehavior::ContinuousUpdate,
                    theme.timelineFilterStyleRef);
        if (view.onAddTrack)
            Button(idManager.AllocateDynamicID("addTrack"), GUIContent(theme.addIcon, {}, "Add a track"),
                   [v] { if (v->onAddTrack) v->onAddTrack(); }, theme.timelineAddTrackStyleRef);
        EndHorizontal();

        // The labels of the ruler's major ticks, for the width it had last frame.
        const float lastWidth = float(view.lanesWidth > 0 ? view.lanesWidth : styleManager.Px(600));
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
        const TextID pillText = textSystem->RegisterText(pillID, std::format("{:.2f}", view.playback.time),
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
                const Geometry g{ ViewRangeOf(*v), float(inner.x), float(v->lanesWidth > 0 ? v->lanesWidth : inner.w) };
                const float x = float(event.LocalCursor(es).x);
                if (DragScroll(*v, event, es, gsm, g, ID::Invalid(), 0, dragDistance))
                    return true;
                switch (event.type)
                {
                    case EventType::MouseDown:
                    {
                        if (!event.state.LeftPressed())
                            return false;
                        gsm.Interaction().SetFocusID(lanesID);
                        gsm.TextEditorFocus().Clear();
                        const bool shift = (event.state.modifiers & KeyModifier::Shift)   != KeyModifier::None;
                        const bool ctrl  = (event.state.modifiers & KeyModifier::Control) != KeyModifier::None;
                        const TimelineRange loop = v->loopRange;
                        if (ctrl)
                        {
                            if (!loop.empty())
                                CommitTimelineEdit(*v, TimelineEdit{ .loopFrom = loop, .loopTo = TimelineRange{} });
                            return true;
                        }
                        int edge = shift ? 0 : -1;
                        if (!loop.empty() && !shift)
                        {
                            if (std::fabs(x - g.XAt(loop.start)) <= float(edgeReach)) edge = 1;
                            else if (std::fabs(x - g.XAt(loop.end)) <= float(edgeReach)) edge = 2;
                        }
                        if (edge >= 0)
                        {
                            v->loopDrag.Press(event, es, MouseButtons::Left, /*atOnce*/ true);
                            gsm.Interaction().SetCaptureID(event.id);   // the moves, release and Escape come here
                            v->loopBefore = loop;
                            v->loopEdge = edge;
                            v->loopPressTime = SnappedTime(*v, g.TimeAt(x));
                            return true;
                        }
                        v->scrub.Press(event, es, MouseButtons::Left, /*atOnce*/ true);
                        gsm.Interaction().SetCaptureID(event.id);
                        v->scrubBefore = v->playback.time;
                        v->playback.time = SnappedTime(*v, g.TimeAt(x));
                        playbackChanged();
                        return true;
                    }
                    case EventType::MouseMove:
                    {
                        if (v->loopDrag.Move(event, 0))
                        {
                            const float t = SnappedTime(*v, g.TimeAt(x));
                            TimelineRange r = v->loopBefore;
                            if (v->loopEdge == 0) r = { std::min(v->loopPressTime, t), std::max(v->loopPressTime, t) };
                            else if (v->loopEdge == 1) r.start = std::min(t, r.end);
                            else r.end = std::max(t, r.start);
                            v->loopRange = r;
                            return true;
                        }
                        if (v->scrub.Move(event, 0))
                        {
                            v->playback.time = SnappedTime(*v, g.TimeAt(x));
                            playbackChanged();
                            return true;
                        }
                        return false;
                    }
                    case EventType::MouseUp:
                    {
                        if (v->loopDrag.Release(event) != PointerDrag::Ended::No)
                        {
                            // A Shift + click (no drag) leaves no range: as it was.
                            if (v->loopRange.empty() && v->loopEdge == 0)
                                v->loopRange = v->loopBefore;
                            const TimelineRange now = v->loopRange;
                            if (!(now == v->loopBefore))
                            {
                                v->loopRange = v->loopBefore;
                                CommitTimelineEdit(*v, TimelineEdit{ .loopFrom = v->loopBefore, .loopTo = now });
                            }
                            return true;
                        }
                        return v->scrub.Release(event) != PointerDrag::Ended::No;
                    }
                    case EventType::KeyDown:
                        if (IsDragCancelKey(event.state) && v->loopDrag.Cancel(event.id))
                        {
                            v->loopRange = v->loopBefore;
                            return true;
                        }
                        if (IsDragCancelKey(event.state) && v->scrub.Cancel(event.id))
                        {
                            v->playback.time = v->scrubBefore;
                            playbackChanged();
                            return true;
                        }
                        return false;
                    case EventType::MouseScroll:
                        return WheelOverTime(*v, event, g, x, wheelMode);
                    default: return false;
                }
            };
        }
        const ElementRef rulerRef = RegisterElement(
            Element{ .id = rulerID, .eventHandler = std::move(rulerHandler), .styleRef = theme.timelineRulerStyleRef,
                     .flags = view.playback.playing ? ElementFlags::Animated : ElementFlags::None },
            LayoutOverride{});
        {
            const TimelineRange loop = view.loopRange;
            const float time = view.playback.time;
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
                const Geometry g{ range, float(inner.x), float(v->lanesWidth > 0 ? v->lanesWidth : inner.w) };
                if (!loop.empty())
                {
                    const int32_t a = int32_t(g.XAt(loop.start)), b = int32_t(g.XAt(loop.end));
                    ctx.RenderBox(IntRect{ a, outer.y, b - a, outer.h }, outer, band);
                    ctx.RenderBox(IntRect{ a, outer.y, px2, outer.h }, outer, edge);
                    ctx.RenderBox(IntRect{ b - px2, outer.y, px2, outer.h }, outer, edge);
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
                    const int32_t w = std::max(pillMin, pillTextSize.x + pillPad);
                    const IntRect p{ x - w / 2, outer.y + px4, w, pillHeight };
                    ctx.RenderBox(p, outer, pill);
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
            const TimelineTrack& track = view.tracks[size_t(t)];
            const ID rowID = track.stableID;
            const bool locked = IsTrackLocked(view, t);
            const ElementState rowState = guiStateManager.GetCurrentState(rowID)
                                        | (view.selectedTrack == rowID ? ElementState::Selected : ElementState::None);
            EventHandler rowHandler;
            if (!disabled)
                rowHandler = [this, v, rowID, lanesID](ElementEvent&& event) -> bool {
                    if (event.type != EventType::MouseDown || !event.state.LeftPressed())
                        return false;
                    v->selectedTrack = rowID;
                    guiStateManager.Interaction().SetFocusID(lanesID);
                    guiStateManager.TextEditorFocus().Clear();
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
                    if (t >= (int)v->tracks.size()) return;
                    TimelineTrack to = v->tracks[size_t(t)];
                    change(to);
                    CommitTimelineEdit(*v, TimelineEdit{ .changes = { { t, v->tracks[size_t(t)], std::move(to) } } });
                };
            };
            switch (track.kind)
            {
                case TimelineTrackKind::Group:
                {
                    Button(idManager.AllocateDynamicID("chevron"),
                           GUIContent(track.expanded ? theme.treeArrowDown : theme.treeArrowRight, {}, track.expanded ? "Collapse" : "Expand"),
                           [v, t] { if (t < (int)v->tracks.size()) v->tracks[size_t(t)].expanded = !v->tracks[size_t(t)].expanded; },
                           theme.timelineChevronStyleRef);
                    const VisualColorStyleIndex chip = track.color.a > 0.0f
                        ? styleManager.RegisterColorStyle(ColorStyle::Create(track.color)) : VisualColorStyleIndex{};
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
                           [this, v, t] { CommitTimelineEdit(*v, AddKeyEdit(*v, t, v->playback.time, idManager.CreateStaticID())); },
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
            if (view.onTrackContextMenu && BeginContextMenu(rowID))
            {
                v->selectedTrack = rowID;
                view.onTrackContextMenu(t);
            }
            if (view.onTrackContextMenu)
                EndContextMenu();
        }
        EndVertical();

        // The event names, for the draw.
        struct EventLabel { int row; float time; TextID text; int2 size; bool selected; };
        std::vector<EventLabel> eventLabels;
        for (int r = 0; r < (int)rows.size(); ++r)
        {
            const TimelineTrack& track = view.tracks[size_t(rows[size_t(r)])];
            if (track.kind != TimelineTrackKind::Events)
                continue;
            [[maybe_unused]] const bool inTrack = idManager.PushParent(track.stableID);
            const ID labelsID = track.events.empty() ? ID::Invalid() : idManager.AllocateRangedID(uint32_t(track.events.size()), "events");
            for (size_t e = 0; e < track.events.size(); ++e)
            {
                const TimelineEvent& event = track.events[e];
                const TextID text = textSystem->RegisterText(idManager.GetSubID(labelsID, uint32_t(e)), event.name,
                                                             theme.timelineEventLabelStyleRef, ElementState::None);
                const TextMeasurement m = textSystem->MeasureText(text, TextWrapContext{});
                eventLabels.push_back({ r, event.time, text, { m.maxWidth, m.maxHeight }, view.selectedKeys.contains(event.stableID) });
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
            lanesHandler = [this, &gsm, &es, v, rows, rowHeight, reach, dragDistance, bodyID, playbackChanged, wheelMode = GetWheelMode()](ElementEvent&& event) -> bool
            {
                if (event.rect == nullptr)
                    return false;
                const IntRect inner = event.rect->innerRect;
                const Geometry g{ ViewRangeOf(*v), float(inner.x), float(inner.w) };
                const int2 at = event.LocalCursor(es);
                const int row = (at.y - event.rect->rect.y) / std::max(1, rowHeight);
                const int track = row >= 0 && row < (int)rows.size() ? rows[size_t(row)] : -1;
                const bool shift = (event.state.modifiers & KeyModifier::Shift)   != KeyModifier::None;
                const bool ctrl  = (event.state.modifiers & KeyModifier::Control) != KeyModifier::None;
                const float length = LengthOf(*v);
                auto commitMove = [&](float delta) {
                    const std::vector<TimelineTrack> before = v->tracks;
                    v->tracks = MovedKeys(*v, before, delta);
                    const TimelineEdit edit = ChangesSince(*v, before);
                    v->tracks = before;
                    CommitTimelineEdit(*v, edit);
                };
                if (DragScroll(*v, event, es, gsm, g, bodyID, event.rect->rect.h, dragDistance))
                    return true;
                switch (event.type)
                {
                    case EventType::MouseDown:
                    {
                        if (!event.state.LeftPressed())
                            return false;
                        gsm.Interaction().SetFocusID(event.id);
                        gsm.TextEditorFocus().Clear();
                        if (track >= 0)
                            v->selectedTrack = v->tracks[size_t(track)].stableID;
                        const Hit hit = HitOnRow(*v, track, g, float(at.x), reach);
                        if (!hit.ids.empty())
                        {
                            const bool allSelected = std::all_of(hit.ids.begin(), hit.ids.end(), [v](ID id) { return v->selectedKeys.contains(id); });
                            if (ctrl)
                            {
                                for (const ID id : hit.ids)
                                    if (allSelected) v->selectedKeys.erase(id); else v->selectedKeys.insert(id);
                            }
                            else if (shift)
                                v->selectedKeys.insert(hit.ids.begin(), hit.ids.end());
                            else if (!allSelected)
                                v->selectedKeys = std::unordered_set<ID>(hit.ids.begin(), hit.ids.end());
                            if (!ctrl || !allSelected)
                            {
                                v->keyDrag.press.Press(event, es);
                                v->keyDrag.pressTime = hit.time;
                                v->keyDrag.before = v->tracks;
                                v->keyDrag.moved = false;
                            }
                            return true;
                        }
                        v->boxSelect.Press(event, es);
                        v->boxFrom = v->boxTo = g.TimeAt(float(at.x));
                        v->boxFromRow = v->boxToRow = std::clamp(row, 0, std::max(0, (int)rows.size() - 1));
                        v->boxBase = (shift || ctrl) ? v->selectedKeys : std::unordered_set<ID>{};
                        return true;
                    }
                    case EventType::MouseMove:
                    {
                        if (v->keyDrag.press.IsPressed(event.id))
                        {
                            if (!v->keyDrag.press.Move(event, dragDistance))
                                return true;
                            if (v->keyDrag.before.size() != v->tracks.size())
                            {
                                v->keyDrag = {};
                                return true;
                            }
                            const float delta = SnappedDelta(*v, g.TimeAt(float(at.x)) - v->keyDrag.pressTime);
                            v->tracks = MovedKeys(*v, v->keyDrag.before, delta);
                            v->keyDrag.moved = true;
                            return true;
                        }
                        if (v->boxSelect.IsPressed(event.id))
                        {
                            if (!v->boxSelect.Move(event, dragDistance))
                                return true;
                            v->boxTo = g.TimeAt(float(at.x));
                            v->boxToRow = std::clamp(row, 0, std::max(0, (int)rows.size() - 1));
                            const float t0 = std::min(v->boxFrom, v->boxTo), t1 = std::max(v->boxFrom, v->boxTo);
                            const int r0 = std::min(v->boxFromRow, v->boxToRow), r1 = std::max(v->boxFromRow, v->boxToRow);
                            std::unordered_set<ID> selection = v->boxBase;
                            auto take = [&](ID id) { if (ctrl && v->boxBase.contains(id)) selection.erase(id); else selection.insert(id); };
                            for (int r = r0; r <= r1 && r < (int)rows.size(); ++r)
                            {
                                const int t = rows[size_t(r)];
                                const TimelineTrack& tr = v->tracks[size_t(t)];
                                for (const TimelineKey& key : tr.keys) if (key.time >= t0 && key.time <= t1) take(key.stableID);
                                for (const TimelineEvent& e : tr.events) if (e.time >= t0 && e.time <= t1) take(e.stableID);
                                if (tr.kind == TimelineTrackKind::Group)
                                    for (const TimelineTrack& held : TracksInGroup(*v, t))
                                        for (const TimelineKey& key : held.keys) if (key.time >= t0 && key.time <= t1) take(key.stableID);
                            }
                            v->selectedKeys = std::move(selection);
                            return true;
                        }
                        return false;
                    }
                    case EventType::MouseUp:
                    {
                        if (v->keyDrag.press.IsPressed(event.id))
                        {
                            v->keyDrag.press.Release(event);
                            if (v->keyDrag.moved)
                            {
                                // Already where it's dropped: the edit, for undo.
                                const TimelineEdit edit = ChangesSince(*v, v->keyDrag.before);
                                if (!edit.empty() && v->onEdit)
                                    v->onEdit(edit);
                            }
                            v->keyDrag = {};
                            return true;
                        }
                        if (const PointerDrag::Ended ended = v->boxSelect.Release(event); ended != PointerDrag::Ended::No)
                        {
                            // A click on the empty lanes clears the selection.
                            if (ended == PointerDrag::Ended::Click && !shift && !ctrl)
                                v->selectedKeys.clear();
                            return true;
                        }
                        return false;
                    }
                    case EventType::MouseScroll:
                        return WheelOverTime(*v, event, g, float(at.x), wheelMode);
                    case EventType::KeyDown:
                    {
                        if (IsDragCancelKey(event.state))
                        {
                            if (v->keyDrag.press.Cancel(event.id))
                            {
                                if (v->keyDrag.before.size() == v->tracks.size())
                                    v->tracks = v->keyDrag.before;
                                v->keyDrag = {};
                                return true;
                            }
                            if (v->boxSelect.Cancel(event.id))
                            {
                                v->selectedKeys = v->boxBase;
                                return true;
                            }
                        }
                        if (gsm.Interaction().GetFocusedID() != event.id || event.state.consumed)
                            return false;
                        const float frame = FrameOf(*v);
                        const float step = frame > 0.0f ? frame : length / 100.0f;
                        switch (event.state.lastKey)
                        {
                            case KeyCode::Delete: case KeyCode::Backspace:
                                CommitTimelineEdit(*v, DeleteSelectedKeysEdit(*v));
                                return true;
                            case KeyCode::Escape:
                                if (v->selectedKeys.empty())
                                    return false;
                                v->selectedKeys.clear();
                                return true;
                            case KeyCode::KeyA:
                                if (!ctrl)
                                    return false;
                                for (const TimelineTrack& tr : v->tracks)
                                {
                                    for (const TimelineKey& key : tr.keys) v->selectedKeys.insert(key.stableID);
                                    for (const TimelineEvent& e : tr.events) v->selectedKeys.insert(e.stableID);
                                }
                                return true;
                            case KeyCode::KeyK:
                            {
                                for (int t = 0; t < (int)v->tracks.size(); ++t)
                                    if (v->tracks[size_t(t)].stableID == v->selectedTrack)
                                        CommitTimelineEdit(*v, AddKeyEdit(*v, t, v->playback.time, idManager.CreateStaticID()));
                                return true;
                            }
                            case KeyCode::KeyI: case KeyCode::KeyO:
                            {
                                // The loop range's start (I) or end (O) at the playhead.
                                const TimelineRange before = v->loopRange;
                                TimelineRange r = before.empty() ? TimelineRange{ 0.0f, length } : before;
                                if (event.state.lastKey == KeyCode::KeyI) r.start = v->playback.time; else r.end = v->playback.time;
                                if (r.empty())
                                    return true;
                                CommitTimelineEdit(*v, TimelineEdit{ .loopFrom = before, .loopTo = r });
                                return true;
                            }
                            case KeyCode::Left: case KeyCode::Right:
                            {
                                const float delta = event.state.lastKey == KeyCode::Left ? -step : step;
                                if (!v->selectedKeys.empty())
                                    commitMove(delta);
                                else
                                {
                                    v->playback.time = std::clamp(v->playback.time + delta, 0.0f, length);
                                    v->playback.playing = false;
                                    playbackChanged();
                                }
                                return true;
                            }
                            case KeyCode::Space:
                                TogglePlay(v->playback, length);
                                playbackChanged();
                                return true;
                            case KeyCode::Home:
                                v->playback.time = 0.0f;
                                playbackChanged();
                                return true;
                            case KeyCode::End:
                                v->playback.time = length;
                                v->playback.playing = false;
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
                     .flags = view.playback.playing ? ElementFlags::Animated : ElementFlags::None,
                     .state = guiStateManager.GetCurrentState(lanesID) },
            LayoutOverride{}.MinHeight(Pixels{ lanesHeight }).FlexGrow(1));
        VERIFY(elementStorage.AddPostLayout(lanesRef, [v](PostLayoutContext& ctx) { v->lanesWidth = ctx.ElementInnerRect().w; }));
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
            };
            std::vector<RowDraw> drawn;
            drawn.reserve(rows.size());
            for (const int t : rows)
            {
                const TimelineTrack& track = view.tracks[size_t(t)];
                RowDraw row{ track.kind, view.selectedTrack == track.stableID, {}, {}, track.clip, {} };
                for (const TimelineKey& key : track.keys) row.keys.push_back({ key.time, view.selectedKeys.contains(key.stableID) });
                for (const TimelineEvent& e : track.events) row.events.push_back({ e.time, view.selectedKeys.contains(e.stableID) });
                if (track.kind == TimelineTrackKind::Group)
                    row.summary = SummaryTimes(view, t);
                drawn.push_back(std::move(row));
            }
            const bool boxing = view.boxSelect.IsDragging(lanesID);
            const float boxT0 = std::min(view.boxFrom, view.boxTo), boxT1 = std::max(view.boxFrom, view.boxTo);
            const int boxR0 = std::min(view.boxFromRow, view.boxToRow), boxR1 = std::max(view.boxFromRow, view.boxToRow);
            const TimelineRange loop = view.loopRange;
            const float time = view.playback.time;
            const GUITheme& th = theme;
            const VisualBoxStyleIndex laneSelected = th.timelineLaneSelectedStyleRef.visualBoxStyleIndex, rowLine = th.timelineRowLineStyleRef.visualBoxStyleIndex;
            const VisualBoxStyleIndex outside = th.timelineOutsideStyleRef.visualBoxStyleIndex, clipBox = th.timelineClipStyleRef.visualBoxStyleIndex;
            const VisualBoxStyleIndex eventLine = th.timelineEventStyleRef.visualBoxStyleIndex, eventChip = th.timelineEventChipStyleRef.visualBoxStyleIndex;
            const VisualBoxStyleIndex selectedLine = th.timelinePlayheadStyleRef.visualBoxStyleIndex, boxStyle = th.timelineBoxSelectStyleRef.visualBoxStyleIndex;
            const VisualColorStyleIndex keyFill = th.timelineKeyColor, keyEdge = th.timelineKeyEdgeColor, keySelected = th.timelineKeySelectedColor,
                                        keySelectedEdge = th.timelineKeySelectedEdgeColor, summary = th.timelineSummaryKeyColor, wave = th.timelineWaveColor;
            const float keyHalf = float(styleManager.Px(10)) * 0.7071f, selectedHalf = float(styleManager.Px(11)) * 0.7071f;
            const float summaryHalf = float(styleManager.Px(8)) * 0.7071f, edgeWidth = float(std::max(1, styleManager.Px(1))) * 1.4142f;
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
                        ctx.RenderBox(box, outer, clipBox);
                        // The waveform: a bar a few pixels, each the loudest of the peaks it covers.
                        const std::vector<float>& peaks = row.clip->peaks;
                        const int32_t room = box.w - 2 * px4;
                        const int bars = std::min((int)peaks.size(), room / (barWidth + barGap));
                        for (int i = 0; i < bars; ++i)
                        {
                            const size_t from = size_t(i) * peaks.size() / size_t(bars), to = std::max(from + 1, size_t(i + 1) * peaks.size() / size_t(bars));
                            float peak = 0.0f;
                            for (size_t p = from; p < to; ++p) peak = std::max(peak, Finite(peaks[p], 0.0f));
                            const int32_t h = std::max(px1, int32_t(std::clamp(peak, 0.0f, 1.0f) * float(box.h - 2 * px3)));
                            const IntRect bar{ box.x + px4 + i * (barWidth + barGap), y + (rowHeight - h) / 2, barWidth, h };
                            ctx.RenderQuad(bar, outer, wave, 1.0f);
                        }
                    }
                    for (const auto& [t, selected] : row.events)
                    {
                        const int32_t x = int32_t(g.XAt(t));
                        ctx.RenderBox(IntRect{ x, y + px4, px2, rowHeight - 2 * px4 }, outer, selected ? selectedLine : eventLine);
                    }
                    for (const float t : row.summary)
                    {
                        const float x = g.XAt(t);
                        if (!shown(x)) continue;
                        Diamond(ctx, x, cy, summaryHalf + edgeWidth, keyEdge);
                        Diamond(ctx, x, cy, summaryHalf, summary);
                    }
                    for (const auto& [t, selected] : row.keys)
                    {
                        const float x = g.XAt(t);
                        if (!shown(x)) continue;
                        const float half = selected ? selectedHalf : keyHalf;
                        Diamond(ctx, x, cy, half + edgeWidth, selected ? keySelectedEdge : keyEdge);
                        Diamond(ctx, x, cy, half, selected ? keySelected : keyFill);
                    }
                }
                for (const EventLabel& label : eventLabels)
                {
                    const int32_t x = int32_t(g.XAt(label.time)) + px2 + px4;
                    const int32_t h = label.size.y + 2 * px1, y = outer.y + label.row * rowHeight + (rowHeight - h) / 2;
                    ctx.RenderBox(IntRect{ x, y, label.size.x + 2 * px5, h }, outer, eventChip);
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
            scrollHandler = [&es, v](ElementEvent&& event) -> bool
            {
                if (event.rect == nullptr)
                    return false;
                const IntRect inner = event.rect->innerRect;
                const float length = LengthOf(*v);
                const TimelineRange now = ViewRangeOf(*v);
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
                            v->viewStart = std::clamp((x - float(inner.x)) * perPixel - span * 0.5f, 0.0f, length - span);
                        if (v->viewLength <= 0.0f) v->viewLength = span;
                        v->pan.Press(event, es, MouseButtons::Left, /*atOnce*/ true);
                        v->panBefore = v->viewStart;
                        return true;
                    }
                    case EventType::MouseMove:
                        if (!v->pan.Move(event, 0))
                            return false;
                        v->viewStart = std::clamp(v->panBefore + float(v->pan.LocalDelta(event, es).x) * perPixel, 0.0f, length - span);
                        return true;
                    case EventType::MouseUp:
                        return v->pan.Release(event) != PointerDrag::Ended::No;
                    case EventType::KeyDown:
                        if (IsDragCancelKey(event.state) && v->pan.Cancel(event.id))
                        {
                            v->viewStart = v->panBefore;
                            return true;
                        }
                        return false;
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
            VERIFY(elementStorage.AddCustomDraw(scrollRef, [=](CustomDrawContext& ctx)
            {
                const IntRect inner = ctx.ElementInnerRect();
                if (length <= 0.0f || inner.w <= 0)
                    return;
                const int32_t a = inner.x + int32_t(range.start / length * float(inner.w));
                const int32_t b = inner.x + int32_t(range.end / length * float(inner.w));
                ctx.RenderBox(IntRect{ a, inner.y + (inner.h - thick) / 2, std::max(thick, b - a), thick }, inner, thumb);
            }));
        }

        if (pushed)
            (void)idManager.PopParent();
        EndVertical();
    }
}
