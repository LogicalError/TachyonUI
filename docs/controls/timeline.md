# Timeline

Two controls for time:

- `Timeline`: a row for playing something back, such as a simulation or a recording.
- `TimelineEditor`: an animation editor, with tracks of keys, sounds and events over time.
  It's further down this page, under [TimelineEditor](#timelineeditor).

## Timeline

A row for playing something back over time: an animation, a simulation, a recording.

```cpp
bool Timeline(ID id, TimelineState& state, const TimelineOptions& options, LayoutOverride&& constraints = {});
```

The row has, from left to right:

- the transport buttons:
  - go to the start
  - a frame back
  - play / pause, filled in the accent color
  - a frame on
  - go to the end
  - loop, a toggle
- the time now and the length, such as `00:04.47 / 00:08.00`, and the frame, such as `f 134`
- the scrubber, which takes the rest of the row:
  - ticks, every second when there's room, with three smaller ones between
  - a fill for how far the content is ready (`progress`)
  - a line for each marker
  - the playhead

It returns true on the frame the user changed `state`.

### The state and the options

`TimelineState` is yours. Keep it from frame to frame.

| Field | What it's for |
| --- | --- |
| `time` | Where the playhead is, in seconds from the start |
| `playing` | Whether it moves on by itself, with `AdvanceTimeline` |
| `loop` | At the end, go on from the start instead of stopping |

- The Timeline keeps `time` between 0 and the length.
  - For example, a `time` of 12 on an 8-second timeline becomes 8.

`TimelineOptions` says what the timeline covers. Fill it in every frame.

| Field | What it's for |
| --- | --- |
| `duration` | The length, in seconds: the right end of the scrubber |
| `fps` | Frames per second: how far a step goes, and the frame number shown. 0 or less: no frames |
| `progress` | How far the content is ready, in seconds, drawn as a fill. Below 0 (the default): no fill |
| `markers` | Checkpoints, in seconds: a line each on the scrubber |
| `loopRange` | Where looping goes round, from its `end` back to its `start`. Empty (the default): the whole length |

- The markers are only read during the call, so they can point at a temporary array.
  - Markers outside the length are left out.
- Without frames (`fps` of 0), a step is a hundredth of the length and the frame number
  isn't shown.
  - For example, a step on a 10-second timeline without frames is 0.1 s.

```cpp
const float checkpoints[] = { 2.5f, 6.0f, 8.25f };
TimelineOptions options{ .duration = 10.0f, .fps = 30.0f, .progress = solver.ComputedSeconds(),
                         .markers = checkpoints };
```

### Playing

The Timeline doesn't move the playhead by itself. Call `AdvanceTimeline` once a frame, with
the frame's time:

```cpp
bool AdvanceTimeline(TimelineState& state, const TimelineOptions& options, float deltaSecs);
```

- While `playing`, it moves `time` on by `deltaSecs`.
- At the end:
  - looping, it goes on from the start
  - not looping, it stops at the end and sets `playing` to false
- With a `loopRange`, looping goes round that range instead:
  - played into it, it goes from its end back to its start
  - played past it (started after it), the timeline's end goes back to the range's start
  - for example, with a range of 2 s to 4 s, 3.5 s plus 1 s is 2.5 s
- It returns whether the time moved, so you know when to show another frame.

```cpp
AdvanceTimeline(playback, options, frameTime);
if (Timeline(playback, options) || playback.playing)
    ShowFrameAt(playback.time);
```

- Play at the end starts over from the start.
- A Timeline in a cached panel (`BeginCached`, a docked panel) is drawn anew every frame while
  it plays.
  - If you change `progress` while it's paused, invalidate the panel yourself, or keep it
    uncached.

### Mouse

- A press on the scrubber puts the playhead there, on the nearest frame.
- Dragging moves it along.
  - Past either end, it stays at that end.
  - Escape during the drag puts it back where it was.
- Stepping, with the buttons or the keys, pauses.
  - Go to the start doesn't: playing from the start goes on.

### Keys

On the focused scrubber:

| Key | Does |
| --- | --- |
| Space | Play or pause |
| Left / Right | A frame back / on |
| Page Down / Page Up | To the marker before / after the playhead |
| Home / End | To the start / the end |
| Escape | During a drag: put the playhead back |

- Tab reaches each button and the scrubber.
- Keys it doesn't use go on to the app's shortcuts.

### Example

A simulation computed in the background, played back as far as it's ready:

```cpp
TimelineState playback{ .loop = true };   // a member: kept from frame to frame

void OnGUI()
{
    const TimelineOptions options{ .duration = 10.0f, .fps = 30.0f,
                                   .progress = solver.ComputedSeconds(), .markers = checkpoints };
    AdvanceTimeline(playback, options, frameTime);
    if (Timeline(playback, options))
        solver.ShowFrame(playback.time);
}
```

### Classes

- the row: `timeline`
- the buttons' group: `timeline-transport`
- the buttons:
  - start, frame back, frame on, end: `timeline-button`
  - play / pause: `timeline-play`
  - loop: `timeline-loop`, selected while looping
- the time's well: `timeline-time`, with:
  - the time now: `timeline-time-current`
  - the length: `timeline-time-total`
  - the frame chip: `timeline-frame`
- the scrubber: `timeline-scrubber`. Its padding box runs from the start to the end, so its
  side padding keeps the playhead whole at either end.
- drawn on the scrubber:
  - the progress fill: `timeline-ready`
  - the ticks: `timeline-tick`, `timeline-tick-major`
  - a marker: `timeline-marker`
  - the playhead: `timeline-playhead`

The palette sets `--timeline-ready`, `--timeline-tick` and `--timeline-marker`.


## TimelineEditor

An animation editor: a toolbar over the track list and, beside it, the ruler and the lanes.

```cpp
void TimelineEditor(ID id, TimelineView& view, LayoutOverride&& constraints = {});
```

- The toolbar:
  - the transport: go to the start, the previous key, play / pause, the next key, go to the
    end, loop
  - the time now, the length and the frame, as on the Timeline row
  - the frame rate, a dropdown of the usual ones (12 to 60 fps)
  - Snap: dragged keys and the loop range land on frames while it's on
  - zoom, a slider
- The track list, one row a track, with a filter and an add button over it.
- The ruler: ticks with their seconds, the loop range, and the playhead with its time.
- The lanes, one a track: keys, sounds, events.
- A scroll bar under the lanes, for the part in view when zoomed in.

It fills the room it's given: put it in a panel and it takes the panel.

### The view

`TimelineView` is yours: keep it from frame to frame, as you would a `NodeView`.

| Field | What it's for |
| --- | --- |
| `tracks` | The rows, top to bottom |
| `playback` | The playhead, as a `TimelineState` |
| `duration`, `fps` | The length in seconds, and the frame rate |
| `loopRange` | Where looping goes round; empty: the whole length |
| `snap` | Whether drags land on frames |
| `viewStart`, `viewLength` | The part in view; a `viewLength` of 0 shows it all |
| `selectedTrack`, `selectedKeys` | What's selected: a track by `stableID`, keys and events by theirs |
| `filter` | The track list shows the tracks whose name has it, with their groups |

And its callbacks:

| Callback | When |
| --- | --- |
| `onEdit` | A change was made (already applied): put it on your undo history |
| `onAddTrack` | The "+" over the track list; without it there's no "+" |
| `onTrackContextMenu` | A right click on a row: build the menu with `MenuItem` and the like |
| `onPlaybackChanged` | The user moved the playhead, or started or stopped playing |

```cpp
TimelineView animation;   // a member

animation.onEdit = [this](const TimelineEdit& edit) {
    history.Push({ .description = "Edit animation",
                   .undo = [this, edit] { ApplyTimelineEdit(animation, edit, /*undo*/ true); },
                   .redo = [this, edit] { ApplyTimelineEdit(animation, edit); } });
};
```

### Tracks

Each `TimelineTrack` has a `stableID` you give it once, its `name`, and a `kind`:

| Kind | Its row | Its lane |
| --- | --- | --- |
| `Group` | A chevron, its color chip, its name, eye and lock | Its tracks' keys, summed up: one small key per time |
| `Property` | Its name, and a button to key it at the playhead | Its `keys` |
| `Audio` | Its name, mute (M) and solo (S) | Its `clip`, with its waveform from `peaks` |
| `Events` | Its name | Its `events`: a line and a name each |

- A group holds the tracks after it that are indented further (`depth`), up to the next one
  that isn't.
  - For example, a group at depth 0 holds the depth-1 tracks after it.
  - `TracksInGroup(view, track)` gives them.
- A collapsed group (`expanded` false) hides them; its lane still shows their keys.
- A locked group (`locked`) keeps its tracks' keys where they are: they can't be moved, added
  or deleted.
  - `IsTrackLocked(view, track)` says whether a track is, itself or through a group.
- Give each key and event a `stableID` too. The selection goes by them.
- An audio track's `clip` plays part of a sound:
  - `start` and `length`: where it sits on the timeline and how long it plays, in seconds
  - `offset`: how far into the sound it starts; trimming its start moves this on
  - `sourceLength`: the whole sound's length, which `peaks` cover; 0 means `offset + length`
  - For example, `{ .start = 2.0f, .length = 3.0f, .offset = 1.0f, .sourceLength = 6.0f }`
    plays seconds 1 to 4 of a 6-second sound, from 2 s on the timeline.

```cpp
TimelineTrack position{ .stableID = ids.Create(), .name = "Position", .depth = 1 };
position.keys = { { ids.Create(), 0.0f }, { ids.Create(), 2.5f } };
animation.tracks = { TimelineTrack{ .stableID = ids.Create(), .name = "Sphere", .kind = TimelineTrackKind::Group },
                     position };
```

### Edits and undo

Everything the user changes in the tracks or the loop range comes to `onEdit` as a
`TimelineEdit`, already done:

- `changes`: each track changed, as it was (`from`) and is (`to`)
- `tracks`: tracks added or removed
- `loopFrom`, `loopTo`: the loop range, when it changed

`ApplyTimelineEdit(view, edit, undo)` does it again, or undoes it. To change the tracks from
your own code as one undo step, make an edit and commit it:

| Helper | The edit it makes |
| --- | --- |
| `AddKeyEdit(view, track, time, keyID)` | A key on a property, on the nearest frame while snapping; none if one's there or the track is locked |
| `DeleteSelectedKeysEdit(view)` | The selected keys and events gone, except on locked tracks |
| `AddTrackEdit(view, track, index)` | A track added at `index` (the end if out of range) |
| `RemoveTrackEdit(view, track)` | A track removed; a group with its tracks |

```cpp
CommitTimelineEdit(animation, AddKeyEdit(animation, track, animation.playback.time, ids.Create()));
```

- `CommitTimelineEdit` applies the edit and hands it to `onEdit`.
- The playhead, the selection, the zoom and a group's `expanded` aren't edits: they change in
  the view directly.

### Playing it

As with the Timeline row, the editor doesn't move the playhead by itself:

```cpp
AdvanceTimeline(animation.playback,
                TimelineOptions{ .duration = animation.duration, .fps = animation.fps,
                                 .loopRange = animation.loopRange },
                frameTime);
TimelineEditor(animation);
```

### Mouse

- On the ruler:
  - a press or drag moves the playhead, onto a frame
  - Shift + a drag sets the loop range
  - a drag on either end of the loop range moves that end
  - Ctrl + a click clears the loop range
- On the lanes:
  - a click on a key selects it; Shift adds it, Ctrl toggles it
  - a click on a group's key selects its tracks' keys at that time
  - a click on the empty lanes clears the selection
  - a drag on the empty lanes selects the keys in a box
  - a drag on a selected key moves all the selected keys, by whole frames while snapping
  - an event's label counts as the event: a click selects it, a drag moves it
  - a drag on a clip moves it; a drag on either of its ends trims it, no further than the
    sound goes (an unlocked track only)
  - Escape during a drag puts them back
- Whatever a press would take shows it's hovered first: a key, an event and its label, a
  clip or its end, a loop range's end, the playhead's time while over the ruler, the scroll
  bar's thumb.
- Over the ruler and the lanes:
  - the wheel zooms round the pointer, or scrolls along the time, as `SetWheelMode` says;
    a pinch zooms
  - Shift + the wheel scrolls along the time
  - with the wheel set to scroll, up and down scrolls the rows
  - a drag with the middle button (or Alt + left) moves along the time and scrolls the rows
  - see Panning and zooming in [Common conventions](conventions.md)
- On the track list:
  - a click on a row selects its track
  - the wheel scrolls the rows

### Keys

On the focused lanes (a click on them or on the ruler focuses them):

| Key | Does |
| --- | --- |
| Delete, Backspace | Delete the selected keys |
| Left / Right | Move the selected keys a frame; with none selected, the playhead |
| Ctrl+A | Select every key and event |
| K | Key the selected track at the playhead |
| I / O | Set the loop range's start / end at the playhead |
| Space | Play or pause |
| Home / End | To the start / the end |
| Escape | Clear the selection; during a drag, put it back |

### Example

The tracks set up once, the edits on the undo history, played round the loop range:

```cpp
void OnGUI()
{
    if (animation.tracks.empty())
        SetUpTracks(animation);
    AdvanceTimeline(animation.playback,
                    TimelineOptions{ .duration = animation.duration, .fps = animation.fps,
                                     .loopRange = animation.loopRange },
                    frameTime);
    TimelineEditor(animation);
    scene.Pose(animation.playback.time);
}
```

### Classes

- the whole: `timeline-editor`
- the toolbar: `timeline-toolbar`, its divider `timeline-divider`
  - the transport and the time as on the Timeline row (`timeline-button`, `timeline-play`,
    `timeline-loop`, `timeline-time` and the rest)
  - the frame rate: `timeline-fps`
  - Snap: `timeline-snap`, selected while snapping
  - zoom: `timeline-zoom`
- over the track list and the lanes: `timeline-header`
  - the track list's: `timeline-list-header`, with `timeline-list-title`, `timeline-filter`
    and `timeline-add-track`
  - the ruler: `timeline-ruler`. Its side padding is how far the lanes are inset, so a key at
    either end shows whole.
  - drawn on it: `timeline-ruler-label`, `timeline-playhead-label`, `timeline-loop-range`,
    `timeline-loop-edge`
- the track list and the lanes, which scroll together: `timeline-body`
  - the list: `timeline-list`; a row: `timeline-row`, selected for the selected track
  - in a row: `timeline-chevron`, `timeline-chip`, `timeline-group-name`,
    `timeline-track-name`, `timeline-row-button` (eye, lock, key), `timeline-mute`,
    `timeline-solo`
  - the lanes: `timeline-lanes`, with the same side padding as the ruler
  - drawn on them: `timeline-lane-selected`, `timeline-row-line`, `timeline-outside` (outside
    the loop range), `timeline-clip`, `timeline-event`, `timeline-event-chip`,
    `timeline-event-label`, `timeline-box-select`, and the playhead's `timeline-playhead`
- the scroll bar: `timeline-scroll`, its thumb `timeline-scroll-thumb`

The palette sets:

- the keys' colors: `--timeline-key`, a selected one's `--accent`, a group's
  `--timeline-summary`
- `--timeline-row-line` and `--timeline-outside`
- the clips': `--timeline-clip`, `--timeline-clip-border`, `--timeline-wave`
- an event's name chip: `--timeline-event-soft`

---

[Previous: Node view](node-view.md) | [Controls](../controls.md) | [Next: Other controls](other-controls.md)
