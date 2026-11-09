#include <gui_controls/gui_application_internal.h>

#include <gui_core/graphics/gui_custom_draw_context.h>
#include <gui_core/input/gui_pointer_drag.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace tachyon
{

    // ---------------------------------------------------------------------------
    // Timeline: playback on one row. The transport buttons, the time and frame, and a
    // scrubber whose padding box is the time from 0 to the length.
    // ---------------------------------------------------------------------------

    namespace
    {
        // Per-scrubber state of a press on it (element data store).
        struct ScrubState
        {
            PointerDrag drag;
            float       startTime = 0.0f;   // put back by Escape
        };

        float Finite(float value, float fallback)
        {
            return std::isfinite(value) ? value : fallback;
        }

        // The length, 0 when it isn't a length.
        float LengthOf(const TimelineOptions& options)
        {
            return std::max(0.0f, Finite(options.duration, 0.0f));
        }

        // A frame's length, 0 without frames.
        float FrameOf(const TimelineOptions& options)
        {
            const float fps = Finite(options.fps, 0.0f);
            return fps > 0.0f ? 1.0f / fps : 0.0f;
        }

        // What a step moves: a frame, or a hundredth of the length without frames.
        float StepOf(const TimelineOptions& options)
        {
            const float frame = FrameOf(options);
            return frame > 0.0f ? frame : LengthOf(options) / 100.0f;
        }

        // `time` on the nearest frame (unchanged without frames), within the length.
        float SnapToFrame(float time, float frame, float length)
        {
            if (frame > 0.0f)
                time = std::round(time / frame) * frame;
            return std::clamp(time, 0.0f, length);
        }

        // The frame `time` is in, counted from 0.
        int FrameNumber(float time, float frame)
        {
            return frame > 0.0f ? int(std::floor(time / frame + 1e-3f)) : 0;
        }

        // "f 7", padded to as many digits as the last frame has ("f   7" of 300 frames): the
        // chip keeps its width while the playhead moves, and the scrubber after it its place.
        std::string FrameText(float time, float frame, float length)
        {
            const int digits = int(std::to_string(FrameNumber(length, frame)).size());
            return std::format("f {:>{}}", FrameNumber(time, frame), digits);
        }

        // "01:04.47": minutes, seconds and hundredths.
        std::string ClockText(float secs)
        {
            const long long hundredths = std::llround(double(std::max(0.0f, secs)) * 100.0);
            return std::format("{:02}:{:02}.{:02}", hundredths / 6000, (hundredths / 100) % 60, hundredths % 100);
        }

        // Plays or pauses; played from the end, it starts over.
        void TogglePlay(TimelineState& state, float length)
        {
            if (!state.playing && state.time >= length)
                state.time = 0.0f;
            state.playing = !state.playing;
        }

        // The seconds between major ticks: the shortest of the usual steps at least `minPixels`
        // apart at `pixelsPerSecond`.
        float MajorTickStep(float pixelsPerSecond, float minPixels)
        {
            static constexpr float kSteps[] = { 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 15.0f, 30.0f,
                                                60.0f, 120.0f, 300.0f, 600.0f, 1800.0f, 3600.0f };
            for (const float step : kSteps)
                if (step * pixelsPerSecond >= minPixels)
                    return step;
            return 0.0f;   // not even an hour apart: none
        }
    }

    bool AdvanceTimeline(TimelineState& state, const TimelineOptions& options, float deltaSecs)
    {
        const float length = LengthOf(options);
        if (!state.playing || !(deltaSecs > 0.0f) || !std::isfinite(deltaSecs))
            return false;
        const float before = state.time;
        float time = Finite(state.time, 0.0f) + deltaSecs;
        if (time >= length)
        {
            if (state.loop && length > 0.0f)
                time = std::fmod(time, length);
            else
            {
                time = length;
                state.playing = false;
            }
        }
        state.time = time;
        return state.time != before;
    }

    bool GUIApplication::Timeline(ID id, TimelineState& state, const TimelineOptions& options, LayoutOverride&& constraints)
    {
        const float length = LengthOf(options);
        const float frame  = FrameOf(options);
        const float step   = StepOf(options);
        state.time = std::clamp(Finite(state.time, 0.0f), 0.0f, length);

        // Run from the input, after this call: the caller's state, which outlives the frame.
        TimelineState* s = &state;
        bool changed = false;

        BeginHorizontal(id, theme.timelineStyleRef, std::move(constraints));
        [[maybe_unused]] const bool pushed = idManager.PushParent(id);

        BeginHorizontal(idManager.AllocateDynamicID("transport"), theme.timelineTransportStyleRef);
        changed |= Button(idManager.AllocateDynamicID("start"), GUIContent(theme.timelineStartIcon, {}, "Go to the start"),
                          [s] { s->time = 0.0f; }, theme.timelineButtonStyleRef);
        changed |= Button(idManager.AllocateDynamicID("back"), GUIContent(theme.timelineStepBackIcon, {}, "A frame back"),
                          [s, step] { s->time = std::max(0.0f, s->time - step); s->playing = false; }, theme.timelineButtonStyleRef);
        changed |= Button(idManager.AllocateDynamicID("play"),
                          state.playing ? GUIContent(theme.timelinePauseIcon, {}, "Pause") : GUIContent(theme.timelinePlayIcon, {}, "Play"),
                          [s, length] { TogglePlay(*s, length); }, theme.timelinePlayStyleRef);
        changed |= Button(idManager.AllocateDynamicID("step"), GUIContent(theme.timelineStepIcon, {}, "A frame on"),
                          [s, step, length] { s->time = std::min(length, s->time + step); s->playing = false; }, theme.timelineButtonStyleRef);
        changed |= Button(idManager.AllocateDynamicID("end"), GUIContent(theme.timelineEndIcon, {}, "Go to the end"),
                          [s, length] { s->time = length; s->playing = false; }, theme.timelineButtonStyleRef);
        changed |= Toggle(idManager.AllocateDynamicID("loop"), state.loop, GUIContent(theme.timelineLoopIcon, {}, "Loop"),
                          [s](bool loop) { s->loop = loop; }, theme.timelineLoopStyleRef);
        EndHorizontal();

        // The time now, of the length, and the frame.
        BeginHorizontal(idManager.AllocateDynamicID("time"), theme.timelineTimeStyleRef);
        Text(idManager.AllocateDynamicID("now"), ClockText(state.time), theme.timelineTimeCurrentStyleRef);
        Text(idManager.AllocateDynamicID("length"), "/ " + ClockText(length), theme.timelineTimeTotalStyleRef);
        if (frame > 0.0f)
            Text(idManager.AllocateDynamicID("frame"), FrameText(state.time, frame, length), theme.timelineFrameStyleRef);
        EndHorizontal();

        // The scrubber. Its handler runs from the input, after this call: copies of the markers.
        std::vector<float> markers;
        markers.reserve(options.markers.size());
        for (const float marker : options.markers)
            if (std::isfinite(marker) && marker >= 0.0f && marker <= length)
                markers.push_back(marker);
        std::sort(markers.begin(), markers.end());

        const ID scrubberID = idManager.AllocateDynamicID("scrubber");
        const ElementState scrubberState = guiStateManager.GetCurrentState(scrubberID);
        EventHandler scrub;
        if (!guiStateManager.Construction().IsDisabled())
        {
            GUIStateManager& gsm = guiStateManager;
            const ElementStorage& es = elementStorage;
            scrub = ReportingChanges(gsm, s,
                [&gsm, &es, s, length, frame, step, markers](ElementEvent&& event) -> bool
                {
                    auto& store = gsm.GetElementDataStore();
                    // The pointer's time: across the padding box, from 0 to the length.
                    auto timeAtPointer = [&] {
                        if (event.rect == nullptr)
                            return s->time;
                        const IntRect inner = event.rect->innerRect;
                        const float   along = float(event.LocalCursor(es).x - inner.x) / float(std::max(1, inner.w));
                        return SnapToFrame(std::clamp(along, 0.0f, 1.0f) * length, frame, length);
                    };
                    switch (event.type)
                    {
                        case EventType::MouseDown:
                            if (!event.state.LeftPressed())
                                break;
                            {
                                gsm.Interaction().SetFocusID(event.id);
                                gsm.TextEditorFocus().Clear();
                                auto& scrubState = store.GetOrCreate<ScrubState>(event.id);
                                scrubState.drag.Press(event, es, MouseButtons::Left, /*atOnce*/ true);
                                scrubState.startTime = s->time;
                                s->time = timeAtPointer();
                            }
                            return true;
                        case EventType::MouseMove:
                            if (auto* scrubState = store.Get<ScrubState>(event.id); scrubState && scrubState->drag.Move(event, 0))
                            {
                                s->time = timeAtPointer();
                                return true;
                            }
                            break;
                        case EventType::MouseUp:
                            if (auto* scrubState = store.Get<ScrubState>(event.id); scrubState && scrubState->drag.Release(event) != PointerDrag::Ended::No)
                                return true;
                            break;
                        case EventType::KeyDown:
                        {
                            if (auto* scrubState = store.Get<ScrubState>(event.id);
                                scrubState && IsDragCancelKey(event.state) && scrubState->drag.Cancel(event.id))
                            {
                                s->time = scrubState->startTime;
                                return true;
                            }
                            if (gsm.Interaction().GetFocusedID() != event.id || event.state.consumed)
                                break;
                            switch (event.state.lastKey)
                            {
                                case KeyCode::Space:
                                    TogglePlay(*s, length);
                                    return true;
                                case KeyCode::Left:
                                    s->time = std::max(0.0f, s->time - step);
                                    s->playing = false;
                                    return true;
                                case KeyCode::Right:
                                    s->time = std::min(length, s->time + step);
                                    s->playing = false;
                                    return true;
                                case KeyCode::Home:
                                    s->time = 0.0f;
                                    return true;
                                case KeyCode::End:
                                    s->time = length;
                                    s->playing = false;
                                    return true;
                                case KeyCode::PageDown:
                                {
                                    // The last marker before the playhead (a little before, so a
                                    // playhead on a marker goes on to the one before it).
                                    const float now = s->time - frame * 0.5f - 1e-4f;
                                    const auto it = std::lower_bound(markers.begin(), markers.end(), now);
                                    if (it == markers.begin())
                                        return true;
                                    s->time = *std::prev(it);
                                    return true;
                                }
                                case KeyCode::PageUp:
                                {
                                    const float now = s->time + frame * 0.5f + 1e-4f;
                                    const auto it = std::upper_bound(markers.begin(), markers.end(), now);
                                    if (it != markers.end())
                                        s->time = *it;
                                    return true;
                                }
                                default: break;
                            }
                            break;
                        }
                        default: break;
                    }
                    return false;
                });
        }
        // Playing, the app moves the playhead on every frame: a cached scope holding it is
        // rebuilt every frame.
        const ElementRef scrubberRef = RegisterControl(
            Element{ .id = scrubberID, .eventHandler = std::move(scrub), .styleRef = theme.timelineScrubberStyleRef,
                     .flags = state.playing ? ElementFlags::Animated : ElementFlags::None, .state = scrubberState },
            LayoutOverride{}.FlexGrow(1));
        changed |= guiStateManager.Interaction().HasBeenClicked(scrubberID);

        const float progress = Finite(options.progress, -1.0f);
        const float time     = state.time;
        const VisualBoxStyleIndex readyStyle    = theme.timelineReadyStyleRef.visualBoxStyleIndex;
        const VisualBoxStyleIndex tickStyle     = theme.timelineTickStyleRef.visualBoxStyleIndex;
        const VisualBoxStyleIndex majorStyle    = theme.timelineTickMajorStyleRef.visualBoxStyleIndex;
        const VisualBoxStyleIndex markerStyle   = theme.timelineMarkerStyleRef.visualBoxStyleIndex;
        const VisualBoxStyleIndex playheadStyle = theme.timelinePlayheadStyleRef.visualBoxStyleIndex;
        const int32_t px1 = std::max(1, styleManager.Px(1)), px2 = std::max(1, styleManager.Px(2));
        const int32_t minorHeight = styleManager.Px(4), majorHeight = styleManager.Px(8);
        const int32_t headWidth = styleManager.Px(9), headHeight = styleManager.Px(5);
        const float   majorGap = float(styleManager.Px(48)), minorGap = float(styleManager.Px(6));
        VERIFY(elementStorage.AddCustomDraw(scrubberRef,
            [=](CustomDrawContext& ctx)
            {
                const IntRect outer = ctx.ElementRect(), inner = ctx.ElementInnerRect();
                if (inner.w <= 0 || inner.h <= 0)
                    return;
                const float perSecond = length > 0.0f ? float(inner.w) / length : 0.0f;
                auto xAt = [&](float t) { return inner.x + int32_t(std::lround(t * perSecond)); };

                // What's ready, from the scrubber's start.
                if (progress > 0.0f && length > 0.0f)
                {
                    const int32_t right = xAt(std::min(progress, length));
                    ctx.RenderBox(IntRect{ outer.x, inner.y, std::max(0, right - outer.x), inner.h }, outer, readyStyle);
                }

                // Ticks up from the bottom: a major one every step, minor ones between.
                if (perSecond > 0.0f)
                {
                    const float major = MajorTickStep(perSecond, majorGap);
                    const float minor = major / 4.0f;
                    if (major > 0.0f)
                    {
                        const bool minors = minor * perSecond >= minorGap;
                        const int  count  = int(std::floor(length / (minors ? minor : major) + 1e-3f));
                        for (int i = 0; i <= count; ++i)
                        {
                            const bool    isMajor = !minors || i % 4 == 0;
                            const int32_t height  = isMajor ? majorHeight : minorHeight;
                            const float   t       = float(i) * (minors ? minor : major);
                            ctx.RenderBox(IntRect{ xAt(t), inner.y + inner.h - height, px1, height }, outer,
                                          isMajor ? majorStyle : tickStyle);
                        }
                    }
                }

                for (const float marker : markers)
                    ctx.RenderBox(IntRect{ xAt(marker) - px2 / 2, inner.y, px2, inner.h }, outer, markerStyle);

                // The playhead: a line through, with a head at the top.
                const int32_t x = xAt(time);
                ctx.RenderBox(IntRect{ x - px2 / 2, inner.y, px2, inner.h }, outer, playheadStyle);
                ctx.RenderBox(IntRect{ x - headWidth / 2, inner.y, headWidth, headHeight }, outer, playheadStyle);
            }));

        if (pushed)
            (void)idManager.PopParent();
        EndHorizontal();

        guiStateManager.SetModified(changed);
        return changed;
    }
}
