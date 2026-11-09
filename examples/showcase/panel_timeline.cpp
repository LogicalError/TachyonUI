#include "application.h"

#include <algorithm>
#include <cmath>

//
// The Timeline panel: the timeline editor with tracks of keys, a sound and events.
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

    void Application::RenderTimelinePanel()
    {
        // The editor, the whole panel: tracks of keys, a sound and events, played round the
        // loop range.
        if (!animationSeeded)
            SeedAnimation();
        AdvanceTimeline(animation.playback,
                        TimelineOptions{ .duration = animation.duration, .fps = animation.fps, .loopRange = animation.loopRange },
                        frameTime);
        TimelineEditor(idManager.AllocateDynamicID("editor"), animation, LayoutOverride{}.FlexGrow(1));
    }
}
