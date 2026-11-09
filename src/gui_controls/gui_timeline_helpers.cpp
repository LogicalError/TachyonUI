#include <gui_controls/gui_timeline_types.h>

#include <algorithm>
#include <cmath>

namespace tachyon
{

    // ---------------------------------------------------------------------------
    // The timeline editor's edits: each change as the tracks before and after it, so undo
    // puts the old ones back.
    // ---------------------------------------------------------------------------

    namespace
    {
        bool ValidTrack(const TimelineView& view, int track)
        {
            return track >= 0 && track < (int)view.tracks.size();
        }

        // `time` on the nearest frame when the view snaps, inside the length.
        float SnappedTime(const TimelineView& view, float time)
        {
            const float length = std::isfinite(view.duration) ? std::max(0.0f, view.duration) : 0.0f;
            if (view.snap && std::isfinite(view.fps) && view.fps > 0.0f)
                time = std::round(time * view.fps) / view.fps;
            return std::clamp(std::isfinite(time) ? time : 0.0f, 0.0f, length);
        }
    }

    void ApplyTimelineEdit(TimelineView& view, const TimelineEdit& edit, bool undo)
    {
        auto changes = [&] {
            for (const TimelineEdit::Change& change : edit.changes)
                if (ValidTrack(view, change.track))
                    view.tracks[size_t(change.track)] = undo ? change.from : change.to;
        };
        auto tracks = [&] {
            // Undone in reverse, so each index is where it was when it was done.
            const int count = (int)edit.tracks.size();
            for (int n = 0; n < count; ++n)
            {
                const TimelineEdit::Track& t = edit.tracks[size_t(undo ? count - 1 - n : n)];
                const bool add = t.added != undo;
                if (add)
                {
                    const int at = std::clamp(t.index, 0, (int)view.tracks.size());
                    view.tracks.insert(view.tracks.begin() + at, t.track);
                }
                else if (ValidTrack(view, t.index))
                {
                    const TimelineTrack& gone = view.tracks[size_t(t.index)];
                    if (view.selectedTrack == gone.stableID)
                        view.selectedTrack = ID::Invalid();
                    for (const TimelineKey& key : gone.keys)
                        view.selectedKeys.erase(key.stableID);
                    for (const TimelineEvent& event : gone.events)
                        view.selectedKeys.erase(event.stableID);
                    view.tracks.erase(view.tracks.begin() + t.index);
                }
            }
        };
        // Changes are to tracks as they are with the others in place: done after the adds
        // and removes, undone before them.
        if (undo) { changes(); tracks(); }
        else      { tracks(); changes(); }
        if (edit.loopTo)
            view.loopRange = undo ? edit.loopFrom.value_or(TimelineRange{}) : *edit.loopTo;

        // Keys that are gone aren't selected.
        std::unordered_set<ID> present;
        for (const TimelineTrack& track : view.tracks)
        {
            for (const TimelineKey& key : track.keys) present.insert(key.stableID);
            for (const TimelineEvent& event : track.events) present.insert(event.stableID);
        }
        std::erase_if(view.selectedKeys, [&present](ID id) { return !present.contains(id); });
    }

    void CommitTimelineEdit(TimelineView& view, const TimelineEdit& edit)
    {
        if (edit.empty())
            return;
        ApplyTimelineEdit(view, edit);
        if (view.onEdit)
            view.onEdit(edit);
    }

    TimelineEdit AddKeyEdit(const TimelineView& view, int track, float time, ID keyID)
    {
        if (!ValidTrack(view, track) || IsTrackLocked(view, track))
            return {};
        const TimelineTrack& from = view.tracks[size_t(track)];
        if (from.kind != TimelineTrackKind::Property)
            return {};
        const float at = SnappedTime(view, time);
        // One key a frame: one within half a frame is there already.
        const float near = view.fps > 0.0f && std::isfinite(view.fps) ? 0.5f / view.fps : 1e-4f;
        if (std::any_of(from.keys.begin(), from.keys.end(), [&](const TimelineKey& k) { return std::fabs(k.time - at) < near; }))
            return {};
        TimelineTrack to = from;
        const auto place = std::upper_bound(to.keys.begin(), to.keys.end(), at,
                                            [](float t, const TimelineKey& k) { return t < k.time; });
        to.keys.insert(place, TimelineKey{ .stableID = keyID, .time = at });
        return TimelineEdit{ .changes = { { track, from, std::move(to) } } };
    }

    TimelineEdit DeleteSelectedKeysEdit(const TimelineView& view)
    {
        TimelineEdit edit;
        for (int t = 0; t < (int)view.tracks.size(); ++t)
        {
            const TimelineTrack& from = view.tracks[size_t(t)];
            if (IsTrackLocked(view, t))
                continue;
            TimelineTrack to = from;
            std::erase_if(to.keys,   [&](const TimelineKey& k)   { return view.selectedKeys.contains(k.stableID); });
            std::erase_if(to.events, [&](const TimelineEvent& e) { return view.selectedKeys.contains(e.stableID); });
            if (to.keys.size() != from.keys.size() || to.events.size() != from.events.size())
                edit.changes.push_back({ t, from, std::move(to) });
        }
        return edit;
    }

    TimelineEdit AddTrackEdit(const TimelineView& view, TimelineTrack track, int index)
    {
        const int count = (int)view.tracks.size();
        return TimelineEdit{ .tracks = { { .index = (index < 0 || index > count) ? count : index, .added = true,
                                           .track = std::move(track) } } };
    }

    TimelineEdit RemoveTrackEdit(const TimelineView& view, int track)
    {
        if (!ValidTrack(view, track))
            return {};
        // From the last one held back to the group, so each index is right as it goes.
        const int held = (int)TracksInGroup(view, track).size();
        TimelineEdit edit;
        for (int t = track + held; t >= track; --t)
            edit.tracks.push_back({ .index = t, .added = false, .track = view.tracks[size_t(t)] });
        return edit;
    }

    std::span<const TimelineTrack> TracksInGroup(const TimelineView& view, int track)
    {
        if (!ValidTrack(view, track) || view.tracks[size_t(track)].kind != TimelineTrackKind::Group)
            return {};
        const int depth = view.tracks[size_t(track)].depth;
        int end = track + 1;
        while (end < (int)view.tracks.size() && view.tracks[size_t(end)].depth > depth)
            ++end;
        return std::span<const TimelineTrack>(view.tracks).subspan(size_t(track + 1), size_t(end - track - 1));
    }

    bool IsTrackLocked(const TimelineView& view, int track)
    {
        if (!ValidTrack(view, track))
            return false;
        // It, and each group it's in: walking back, the nearest track indented less each time.
        int depth = view.tracks[size_t(track)].depth + 1;
        for (int t = track; t >= 0; --t)
        {
            const TimelineTrack& at = view.tracks[size_t(t)];
            if (at.depth >= depth)
                continue;
            if (at.locked && (t == track || at.kind == TimelineTrackKind::Group))
                return true;
            depth = at.depth;
        }
        return false;
    }
}
