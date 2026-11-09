# Timeline

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

## The state and the options

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

## Playing

The Timeline doesn't move the playhead by itself. Call `AdvanceTimeline` once a frame, with
the frame's time:

```cpp
bool AdvanceTimeline(TimelineState& state, const TimelineOptions& options, float deltaSecs);
```

- While `playing`, it moves `time` on by `deltaSecs`.
- At the end:
  - looping, it goes on from the start
  - not looping, it stops at the end and sets `playing` to false
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

## Mouse

- A press on the scrubber puts the playhead there, on the nearest frame.
- Dragging moves it along.
  - Past either end, it stays at that end.
  - Escape during the drag puts it back where it was.
- Stepping, with the buttons or the keys, pauses.
  - Go to the start doesn't: playing from the start goes on.

## Keys

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

## Example

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

## Classes

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

---

[Previous: Node view](node-view.md) | [Controls](../controls.md) | [Next: Other controls](other-controls.md)
