#pragma once

#include <gui_core/gui_base_types.h>     // color4f
#include <gui_core/gui_id.h>
#include <gui_core/input/gui_pointer_drag.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace tachyon
{
    /// @addtogroup app
    /// @{

    /// Where playback is: what a Timeline shows and edits. Keep it in the app, from frame to
    /// frame; AdvanceTimeline moves it on while it plays.
    struct TimelineState
    {
        float time    = 0.0f;    ///< the playhead, in seconds from the start
        bool  playing = false;   ///< moving on by itself (AdvanceTimeline)
        bool  loop    = false;   ///< at the end, back to the start instead of stopping

        bool operator==(const TimelineState&) const = default;
    };

    /// A stretch of time, in seconds: from `start` up to `end`. Empty when `end` isn't after
    /// `start`.
    struct TimelineRange
    {
        float start = 0.0f;
        float end   = 0.0f;

        bool empty() const { return !(end > start); }
        bool operator==(const TimelineRange&) const = default;
    };

    /// What a Timeline covers. The markers are only read during the call.
    struct TimelineOptions
    {
        float duration = 0.0f;   ///< the length, in seconds: the scrubber's right end
        float fps      = 30.0f;  ///< frames per second: a step's length, and the frame number shown; 0 or less: no frames
        /// How far the content is ready, in seconds: a simulation's frames computed so far,
        /// drawn as a fill along the scrubber. Below 0: no fill.
        float progress = -1.0f;
        std::span<const float> markers;   ///< checkpoints, in seconds: a line each on the scrubber
        /// Where looping playback goes round: from its end back to its start. Empty (the
        /// default): the whole length.
        TimelineRange loopRange;
    };

    /// Moves `state` on by `deltaSecs` while it plays: call it once a frame with the frame's
    /// time. At the end it goes on from the start when looping, else it stops there. Looping
    /// within `options.loopRange`, it goes round that instead, once the playhead is in it.
    /// Returns whether the time moved.
    ///
    ///     AdvanceTimeline(playback, options, app.frameTime);
    bool AdvanceTimeline(TimelineState& state, const TimelineOptions& options, float deltaSecs);

    // ------------------------------------------------------------------------
    // The timeline editor: tracks of keys, audio and events over time.
    // ------------------------------------------------------------------------

    /// What a track holds, and how its row looks.
    enum class TimelineTrackKind : uint8_t
    {
        Group,      ///< a heading for the tracks after it that are indented further: a chevron, a color chip, eye and lock; its lane sums their keys up
        Property,   ///< an animated value: keys on its lane, and a button to key it at the playhead
        Audio,      ///< a sound: its clip and waveform on its lane, mute and solo
        Events,     ///< named moments: a flag and a label each on its lane
    };

    /// A key on a property track: a time where the value is set.
    struct TimelineKey
    {
        /// Assigned once on creation: the key's ID for its selection.
        ID    stableID = ID::Invalid();
        float time     = 0.0f;   ///< seconds from the start

        bool operator==(const TimelineKey&) const = default;
    };

    /// A named moment on an events track: "Spawn", "Explode".
    struct TimelineEvent
    {
        ID          stableID = ID::Invalid();   ///< assigned once on creation, as a key's
        float       time     = 0.0f;
        std::string name;

        bool operator==(const TimelineEvent&) const = default;
    };

    /// An audio track's sound on its lane.
    struct TimelineClip
    {
        float              start  = 0.0f;   ///< where it starts, in seconds
        float              length = 0.0f;   ///< how long it plays, in seconds
        /// Its loudness over its length, 0 to 1, evenly spaced: the waveform's bars.
        std::vector<float> peaks;

        bool operator==(const TimelineClip&) const = default;
    };

    /// A row of the timeline: its name and controls in the track list, its lane beside them.
    struct TimelineTrack
    {
        /// Assigned once on creation: the ID of its row, lane and selection.
        ID                stableID = ID::Invalid();
        std::string       name;
        TimelineTrackKind kind  = TimelineTrackKind::Property;
        /// How far it's indented: a group holds the tracks after it that are indented
        /// further, up to the next one that isn't.
        int               depth = 0;
        bool              expanded = true;   ///< a group: its tracks shown
        bool              visible  = true;   ///< a group's eye: what it animates shown in the app
        bool              locked   = false;  ///< a group's lock: its tracks' keys can't be moved, added or deleted
        bool              muted    = false;  ///< an audio track: not heard
        bool              solo     = false;  ///< an audio track: heard alone (with the others solo)
        color4f           color = {};    ///< a group's chip; alpha 0 uses the theme's accent
        std::vector<TimelineKey>   keys;     ///< a property's keys, in time order
        std::optional<TimelineClip> clip;    ///< an audio track's sound
        std::vector<TimelineEvent> events;   ///< an events track's events, in time order

        bool operator==(const TimelineTrack&) const = default;
    };

    /// A change to a TimelineView, for undo: what each changed track was before and after,
    /// the tracks added or removed, and the loop range. Every change the editor makes is one;
    /// ApplyTimelineEdit does or undoes it.
    struct TimelineEdit
    {
        /// A track changed: its keys, clip, events or flags.
        struct Change { int track = -1; TimelineTrack from, to; };
        /// A track added (at `index`) or removed (from it).
        struct Track  { int index = -1; bool added = false; TimelineTrack track; };

        std::vector<Change>         changes;
        std::vector<Track>          tracks;   ///< applied in order; undone in reverse
        std::optional<TimelineRange> loopFrom, loopTo;   ///< the loop range, when it changed

        bool empty() const { return changes.empty() && tracks.empty() && !loopTo.has_value(); }
    };

    /// A timeline editor's tracks and how they're shown, kept by the app from frame to frame
    /// (TimelineEditor). Everything the user changes comes to the app as a TimelineEdit
    /// (onEdit), already applied, for its undo history.
    struct TimelineView
    {
        std::vector<TimelineTrack> tracks;
        TimelineState  playback;          ///< the playhead; AdvanceTimeline moves it on
        float          duration = 8.0f;   ///< the length, in seconds
        float          fps      = 30.0f;  ///< frames per second: keys snap to frames, steps go a frame
        TimelineRange  loopRange;         ///< where looping playback goes round; empty: the whole length
        bool           snap = true;       ///< dragged keys and the loop range land on frames

        /// The part of the timeline in view: from `viewStart`, `viewLength` seconds wide.
        /// A `viewLength` of 0 or less shows the whole length.
        float          viewStart  = 0.0f;
        float          viewLength = 0.0f;

        ID                     selectedTrack = ID::Invalid();   ///< highlighted in the list and on its lane
        std::unordered_set<ID> selectedKeys;   ///< keys and events, by stableID
        std::string            filter;          ///< the track list shows the tracks whose name has it (and their groups)

        /// A change the user made, already applied: put it in the undo history.
        std::function<void(const TimelineEdit&)> onEdit;
        /// The "+" over the track list: add a track.
        std::function<void()>                    onAddTrack;
        /// Builds the menu for a track's row (right click, or the Menu key on the selected
        /// one), with MenuItem and the like, as onCanvasContextMenu does in a node view.
        std::function<void(int track)>           onTrackContextMenu;
        /// The playhead was moved, or playback started or stopped, by the user.
        std::function<void()>                    onPlaybackChanged;

        /// @name Internal: kept between frames while the user works
        ///@{
        struct KeyDrag
        {
            PointerDrag press;
            float       pressTime = 0.0f;              ///< the time under the press
            std::vector<TimelineTrack> before;          ///< the tracks as they were (all of them)
            bool        moved = false;
        };
        KeyDrag           keyDrag;
        PointerDrag       boxSelect;
        float             boxFrom = 0.0f;              ///< the box's anchor: time and row
        int               boxFromRow = 0;
        float             boxTo = 0.0f;
        int               boxToRow = 0;
        std::unordered_set<ID> boxBase;                 ///< the selection before a Shift / Ctrl box
        PointerDrag       loopDrag;
        TimelineRange     loopBefore;
        int               loopEdge = 0;                 ///< 0 a new range, 1 its start, 2 its end, 3 the whole of it
        float             loopPressTime = 0.0f;
        PointerDrag       scrub;                        ///< on the ruler
        float             scrubBefore = 0.0f;
        PointerDrag       pan;                          ///< the scroll bar's thumb
        float             panBefore = 0.0f;
        PointerDrag       dragScroll;                   ///< the middle button (or Alt + left) held over the ruler or lanes
        float             dragScrollStart = 0.0f;       ///< the view's start at its press
        int               dragScrollOffset = 0;         ///< the rows' scroll at its press
        int               zoom = 0;                     ///< the zoom slider, 0 (all of it) to 100
        int               zoomShown = 0;                ///< the slider as last drawn, to tell its moves
        int               lanesWidth = 0;               ///< the lanes' width last frame, in pixels
        ///@}
    };

    /// Does (or with `undo`, undoes) `edit` on `view`.
    void ApplyTimelineEdit(TimelineView& view, const TimelineEdit& edit, bool undo = false);
    /// Applies `edit` and hands it to `view.onEdit`; does nothing for an empty one.
    void CommitTimelineEdit(TimelineView& view, const TimelineEdit& edit);

    /// The edit that sets a key on `track` at `time` (on a frame when the view snaps), or
    /// none when there's one there already or the track isn't a property. `keyID` names it.
    TimelineEdit AddKeyEdit(const TimelineView& view, int track, float time, ID keyID);
    /// The edit that deletes the selected keys and events from the tracks that aren't
    /// locked; none when none is selected.
    TimelineEdit DeleteSelectedKeysEdit(const TimelineView& view);
    /// The edit that adds `track` at `index` (the end when out of range).
    TimelineEdit AddTrackEdit(const TimelineView& view, TimelineTrack track, int index = -1);
    /// The edit that removes `track` and, for a group, the tracks it holds.
    TimelineEdit RemoveTrackEdit(const TimelineView& view, int track);

    /// The tracks a group at `track` holds: the ones after it indented further, up to the
    /// next one that isn't. Empty for any other track.
    std::span<const TimelineTrack> TracksInGroup(const TimelineView& view, int track);
    /// Whether `track` is locked: it, or a group holding it, is.
    bool IsTrackLocked(const TimelineView& view, int track);

    /// @}
}
