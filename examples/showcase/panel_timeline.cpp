#include "application.h"

#include <algorithm>
#include <cmath>

//
// The Timeline panel: the timeline editor with tracks of keys, a sound and events. The Curves
// panel: the sphere's animation curves, on the timeline's playhead.
//

namespace tachyon
{

    void Application::SeedAnimation()
    {
        animationSeeded = true;
        TimelineView& view = animation;
        view.duration  = 8.0f;
        view.fps       = 30.0f;
        view.loopRange = { 1.0f, 6.0f };
        view.playback.loop = true;

        auto group = [this](const char* name, color4f color, bool expanded = true) {
            return TimelineTrack{ .stableID = idManager.CreateStaticID(), .name = name, .kind = TimelineTrackKind::Group,
                                  .expanded = expanded, .color = color };
        };
        auto property = [this](const char* name, std::initializer_list<float> times) {
            TimelineTrack track{ .stableID = idManager.CreateStaticID(), .name = name, .kind = TimelineTrackKind::Property, .depth = 1 };
            for (const float t : times)
                track.keys.push_back({ idManager.CreateStaticID(), t });
            return track;
        };

        view.tracks.push_back(group("Sphere", { 0.42f, 0.61f, 1.0f, 1.0f }));
        view.tracks.push_back(property("Position", { 0.0f, 1.0f, 2.5f, 4.0f, 6.0f }));
        view.tracks.push_back(property("Rotation", { 0.0f, 3.0f, 6.0f }));
        view.tracks.push_back(property("Scale", { 2.0f, 5.0f }));
        view.tracks.push_back(group("Camera", { 0.66f, 0.55f, 1.0f, 1.0f }));
        view.tracks.push_back(property("Field of view", { 1.0f, 4.5f }));
        view.tracks.push_back(group("Light", { 0.9f, 0.71f, 0.33f, 1.0f }, /*expanded*/ false));
        view.tracks.push_back(property("Intensity", { 0.5f, 3.5f, 7.0f }));

        // A sound with a made-up waveform: louder and quieter stretches, and some noise.
        TimelineClip clip{ .start = 0.4f, .length = 7.0f };
        for (int i = 0; i < 400; ++i)
        {
            const float swell = 0.35f + 0.65f * std::fabs(std::sin(float(i) * 0.018f));
            const float noise = std::fmod(std::fabs(std::sin(float(i) * 12.9898f) * 43758.5453f), 1.0f);
            clip.peaks.push_back(std::min(0.95f, (0.25f + 0.75f * noise) * swell));
        }
        view.tracks.push_back(TimelineTrack{ .stableID = idManager.CreateStaticID(), .name = "music.wav",
                                             .kind = TimelineTrackKind::Audio, .clip = std::move(clip) });

        TimelineTrack events{ .stableID = idManager.CreateStaticID(), .name = "Events", .kind = TimelineTrackKind::Events };
        events.events.push_back({ idManager.CreateStaticID(), 1.5f, "Spawn" });
        events.events.push_back({ idManager.CreateStaticID(), 5.2f, "Explode" });
        view.tracks.push_back(std::move(events));
        view.selectedTrack = view.tracks[1].stableID;

        // Each change goes on the app's undo history.
        view.onEdit = [this](const TimelineEdit& edit)
        {
            commandManager.History().Push({ .description = "Edit animation",
                .undo = [this, edit] { ApplyTimelineEdit(animation, edit, /*undo*/ true); },
                .redo = [this, edit] { ApplyTimelineEdit(animation, edit, /*undo*/ false); } });
            commandManager.History().EndMerge();
        };
        // "+": a property in the selected track's group (or at the end).
        view.onAddTrack = [this]
        {
            int at = (int)animation.tracks.size(), depth = 0;
            for (int t = 0; t < (int)animation.tracks.size(); ++t)
                if (animation.tracks[size_t(t)].stableID == animation.selectedTrack)
                {
                    const TimelineTrack& selected = animation.tracks[size_t(t)];
                    const int group = selected.kind == TimelineTrackKind::Group ? t : -1;
                    if (group >= 0)
                    {
                        at = group + 1 + (int)TracksInGroup(animation, group).size();
                        depth = selected.depth + 1;
                    }
                    else
                    {
                        at = t + 1;
                        depth = selected.depth;
                    }
                }
            TimelineTrack track{ .stableID = idManager.CreateStaticID(), .name = "New property",
                                 .kind = TimelineTrackKind::Property, .depth = depth };
            animation.selectedTrack = track.stableID;
            CommitTimelineEdit(animation, AddTrackEdit(animation, std::move(track), at));
        };
        SeedAnimationCurves();
        view.onTrackContextMenu = [this](int track)
        {
            MenuItem("Key at the playhead", [this, track] {
                CommitTimelineEdit(animation, AddKeyEdit(animation, track, animation.playback.time, idManager.CreateStaticID()));
            });
            MenuItem("Delete the selected keys", [this] { CommitTimelineEdit(animation, DeleteSelectedKeysEdit(animation)); });
            MenuSeparator();
            MenuItem("Delete track", [this, track] { CommitTimelineEdit(animation, RemoveTrackEdit(animation, track)); });
        };
    }

    void Application::SeedAnimationCurves()
    {
        CurveView& view = animationCurves;
        view.fps = animation.fps;
        view.range = { 0.0f, animation.duration, -2.0f, 4.0f };
        auto channel = [this](const char* path, const char* name, CurveChannelColor color, std::initializer_list<float2> points) {
            CurveChannel c{ .name = name, .path = path, .color = color,
                            .curve = MakeCurve(std::span<const float2>(points.begin(), points.size())) };
            for (CurveKey& k : c.curve.keys)
                k.stableID = idManager.CreateStaticID();
            return c;
        };
        // The keys on the Timeline's Position, Rotation and Scale tracks, with values.
        view.channels = {
            channel("Sphere/Position", "X", CurveChannelColor::Red,   { { 0.0f, 0.0f }, { 1.0f, 1.5f }, { 2.5f, -1.0f }, { 4.0f, 0.5f }, { 6.0f, 0.0f } }),
            channel("Sphere/Position", "Y", CurveChannelColor::Green, { { 0.0f, 0.0f }, { 1.0f, 2.0f }, { 2.5f, 0.0f }, { 4.0f, 1.2f }, { 6.0f, 0.0f } }),
            channel("Sphere/Position", "Z", CurveChannelColor::Blue,  { { 0.0f, -1.0f }, { 2.5f, 0.5f }, { 6.0f, -1.0f } }),
            channel("Sphere/Rotation", "Y", CurveChannelColor::Value, { { 0.0f, 0.0f }, { 3.0f, 1.8f }, { 6.0f, 3.6f } }),
            channel("Sphere/Scale", "Uniform", CurveChannelColor::Alpha, { { 2.0f, 1.0f }, { 5.0f, 1.6f } }) };
        // The rotation turns at an even rate; the scale pops.
        for (CurveKey& k : view.channels[3].curve.keys)
            k.interpolation = CurveInterpolation::Linear;
        view.channels[4].curve.keys[0].interpolation = CurveInterpolation::Constant;
        for (CurveChannel& c : view.channels)
            UpdateCurveTangents(c.curve);

        view.onEdit = [this](const CurveEdit& edit)
        {
            commandManager.History().Push({ .description = "Edit curves",
                .undo = [this, edit] { ApplyCurveEdit(animationCurves, edit, /*undo*/ true); },
                .redo = [this, edit] { ApplyCurveEdit(animationCurves, edit, /*undo*/ false); } });
            commandManager.History().EndMerge();
        };
        // Scrubbing the ruler moves the timeline's playhead, as scrubbing the timeline's does.
        view.onPlayheadChanged = [this](float time) { animation.playback.time = time; };
    }

    void Application::RenderTimelinePanel()
    {
        // The editor, the whole panel: tracks of keys, a sound and events, played round the
        // loop range (in OnGUI).
        TimelineEditor(idManager.AllocateDynamicID("editor"), animation, LayoutOverride{}.FlexGrow(1));
    }

    void Application::RenderCurvesPanel()
    {
        // The curve editor, the whole panel, its playhead the timeline's.
        animationCurves.playhead = animation.playback.time;
        CurveEditor(idManager.AllocateDynamicID("curves"), animationCurves, LayoutOverride{}.FlexGrow(1));
    }
}
