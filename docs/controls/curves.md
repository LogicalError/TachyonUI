# Curves

A curve maps a number to another, through keys you place: a falloff over a distance, an
easing over time, a color's channels in a Curves node.

- `CurveField`: the curve in a small box. A click opens the curve editor in a window under
  it.
- `CurveEditor`: that editor, as a control of its own. Give it `fps` and it becomes an
  animation curve editor, for a panel beside a timeline.
- `EvaluateCurve`: a curve's value at a time, for your app to use.

## CurveField

```cpp
bool CurveField(ID id, CurveView& view, LayoutOverride&& constraints = {});
```

- It draws the curves over `view.range`, the active channel's on top.
- A click opens the editor under it; another click, Escape, or a click outside closes it.
  - Escape gives the keyboard back to the field.
- It returns true on the frame after a change, as a field does.
- Disabled, it doesn't open, and an open editor closes.

```cpp
CurveView falloff;   // a member

Label("Falloff");
if (CurveField(idManager.AllocateDynamicID("falloff"), falloff))
    RebuildLight();
```

## The view

`CurveView` is yours: keep it from frame to frame, as you would a `TimelineView`.

| Field | What it's for |
| --- | --- |
| `channels` | The curves: one for a plain curve, four for a color's R, G, B and A |
| `range` | What the field shows and the editor opens on; keys stay in its time span |
| `clampValues` | Keys stay in the range's value span too |
| `activeChannel` | The channel whose keys are added to |
| `selectedKeys` | The selected keys, by `stableID` |
| `presets` | The shapes under the plot; empty: the built-in ones |
| `fps` | Frames a second, for animation curves; 0 (the default): plain times |
| `playhead` | With `fps`: the time the ruler and the tree show, in seconds |
| `onPlayheadChanged` | With `fps`: the user moved the playhead (already set) |
| `onEdit` | A change was made (already applied): put it on your undo history |

Each `CurveChannel` has:

- a `name`, shown when there are several: "R", "Alpha"
- a `color`: `Value` (the warning color), `Red`, `Green`, `Blue` or `Alpha` (the axis colors
  and the primary text color)
- `visible`: drawn; a hidden channel's keys can't be picked or added to
- a `path`: the groups it's in, split by `/`, such as "Sphere/Position". With paths, the
  editor lists the channels as a tree.
- its `curve`

```cpp
const float2 points[] = { { 0.0f, 1.0f }, { 1.0f, 0.0f } };
falloff.channels = { CurveChannel{ .name = "Falloff", .curve = MakeCurve(points) } };
for (CurveKey& key : falloff.channels[0].curve.keys)
    key.stableID = idManager.CreateStaticID();
falloff.clampValues = true;
falloff.onEdit = [this](const CurveEdit& edit) {
    history.Push({ .description = "Edit falloff",
                   .undo = [this, edit] { ApplyCurveEdit(falloff, edit, /*undo*/ true); },
                   .redo = [this, edit] { ApplyCurveEdit(falloff, edit); } });
};
```

## Curves and keys

A `Curve` is its `keys`, in time order, joined by cubic segments that take each key's slopes,
as an animation curve does.

- Each `CurveKey` has its `time` and `value`, its `inSlope` and `outSlope`, and a `stableID`
  you give it once.
- `EvaluateCurve(curve, time)` gives the value at any time.
  - No keys: 0. One key: its value.
  - A NaN or infinite time counts as 0.

```cpp
const float strength = EvaluateCurve(falloff.channels[0].curve, distance / radius);
```

- `MakeCurve(points, tangents, interpolation)` makes a curve from (time, value) points, in
  time order, with its slopes worked out. The last two are optional: `Auto` and `Bezier`.

```cpp
const float2 points[] = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
Curve steps = MakeCurve(points, CurveTangentMode::Auto, CurveInterpolation::Constant);
```

### Interpolation

A key's `interpolation` says how the curve gets from it to the next key:

| Interpolation | From the key to the next |
| --- | --- |
| `Bezier` (the default) | A smooth curve that takes both keys' slopes |
| `Linear` | A straight line |
| `Constant` | A step: the key's value holds, then jumps to the next key's |

- For example, keys at 0 (value 0) and 1 (value 1):
  - `Bezier` with level slopes: 0.5 at 0.5, easing in and out
  - `Linear`: 0.25 at 0.25
  - `Constant`: 0 all the way to 1
- Tangent handles show only where a `Bezier` segment uses them: a key's left handle when the
  key before it is `Bezier`, its right handle when the key itself is.

### Tangent modes

A key's `tangents` says how its slopes are set:

| Mode | The slopes |
| --- | --- |
| `Auto` (the default) | Smooth through the key, without overshooting its neighbors: level at a peak or a trough |
| `Aligned` | One slope you set, the same on both sides |
| `Free` | Each side its own slope |
| `Flat` | Level on both sides |

- An end key's automatic slope is level.
- For example, a bouncing ball: `Auto` keys at the top of each bounce, and `Free` keys where
  it hits the ground, so it leaves at a different angle than it arrived.
- `UpdateCurveTangents(curve)` works the slopes out again; the edits call it. Call it after
  changing keys yourself.

### Before the first key and after the last

`preWrap` and `postWrap`:

- `Clamp`: holds the end key's value.
- `Loop`: repeats the curve.
- `PingPong`: repeats it, every other time backwards.

For example, keys at 1 and 2 going from 0 to 1, with `postWrap = Loop`: at 2.25 the value is
0.25 again; with `PingPong`, 0.75 on its way back down.

## The editor

- Over the plot:
  - with several channels, a chip for each (a click makes it the active one) and an eye that
    hides it
  - with one key selected, its Time and Value
  - Frame all
- The plot:
  - a grid with its numbers, the lines at zero brighter
  - each visible curve; the others thinner and faded, the active one on top
  - dashed before the first key and after the last, as the wrap modes repeat it
  - the keys; a selected one larger, with its tangent handles and its time and value beside it
- Under the plot: the presets, and Before / After for the wrap modes.

### As an animation curve editor

Set `fps` and the editor lays out for animation, as a graph editor beside a timeline:

- Over the plot:
  - Interpolation: Constant, Linear and Bezier, for the selected keys
  - Tangents: Auto, Aligned, Free and Flat, for the selected keys
  - the first selected key's interpolation and tangents are lit
  - Frame all
  - with one key selected, its Frame and Value
- A ruler over the plot with the frame numbers and the playhead.
  - A press or drag on it moves the playhead to the frame under the pointer.
  - The playhead also runs as a line through the plot.
- Keys land on frames: dragged, added, or typed.
- No presets or wrap modes under the plot.

With channel `path`s, a tree down the left takes the place of the chips:

- a row for each group, which closes and opens it
- a row for each channel: its color, name, value at the playhead, and an eye that hides it
- a click on a channel's row makes it the active one

The playhead is the editor's own: copy your timeline's into it each frame, and copy it back
when the user moves it.

```cpp
curves.fps = 30.0f;
curves.channels = {
    CurveChannel{ .name = "X", .path = "Sphere/Position", .color = CurveChannelColor::Red,   .curve = x },
    CurveChannel{ .name = "Y", .path = "Sphere/Position", .color = CurveChannelColor::Green, .curve = y } };
curves.onPlayheadChanged = [this](float time) { timeline.playback.time = time; };

// Each frame:
curves.playhead = timeline.playback.time;
CurveEditor(idManager.AllocateDynamicID("curves"), curves, LayoutOverride{}.FlexGrow(1));
```

### Mouse

- On a key:
  - a click selects it; Shift adds it, Ctrl toggles it
  - a drag moves the selected keys; Escape during it puts them back
  - a right click: its menu
- On a selected key's handle: a drag sets the slope.
  - A `Free` key: that side only.
  - Any other key becomes `Aligned`, the same slope both sides.
- On the empty plot:
  - a click clears the selection
  - a drag selects the keys in a box; Shift adds them, Ctrl toggles them
  - a double click adds a key to the active channel, on its curve when near it
  - a right click: Add key, Frame all
- The wheel zooms round the pointer, or scrolls the view, as `SetWheelMode` says; a pinch
  zooms.
  - Alt while zooming: along the time only.
  - Shift + the wheel scrolls along the time.
- A drag with the middle button (or Alt + left) drags the view.
- See Panning and zooming in [Common conventions](conventions.md).

The key's menu:

- Constant, Linear, Bezier: the key's interpolation checked
- Auto, Aligned, Free, Flat: the key's tangents checked
- Delete key

### Keys

On the focused plot:

| Key | Does |
| --- | --- |
| Delete, Backspace | Delete the selected keys |
| Ctrl+A | Select the active channel's keys |
| A | Frame all the keys |
| Escape | During a drag, put it back; else clear the selection |

## Edits and undo

Every change comes to `onEdit` as a `CurveEdit`, already done: each changed channel's curve as
it was (`from`) and is (`to`).

- `ApplyCurveEdit(view, edit, undo)` does it again, or undoes it.
- `CommitCurveEdit(view, edit)` applies an edit and hands it to `onEdit`.

To change the curves from your own code as one undo step, make an edit and commit it:

| Helper | The edit it makes |
| --- | --- |
| `AddCurveKeyEdit(view, channel, time, value, keyID)` | A key, kept in the range; none on a hidden channel or where there's one already |
| `DeleteSelectedCurveKeysEdit(view)` | The selected keys gone |
| `SetCurveTangentModeEdit(view, mode)` | The selected keys' tangents |
| `SetCurveInterpolationEdit(view, interpolation)` | The selected keys' interpolation |
| `ApplyCurvePresetEdit(view, channel, preset, makeID)` | A preset stretched over the range |
| `SetCurveWrapEdit(view, channel, pre, post)` | A channel's wrap modes |

```cpp
CommitCurveEdit(falloff, AddCurveKeyEdit(falloff, 0, 0.5f, 0.8f, idManager.CreateStaticID()));
```

- With `fps` set, `AddCurveKeyEdit` puts the key on the nearest frame.
- `SnapToFrame(view, time)` does the same for your own times: 0.43 at 10 fps is 0.4.

## Presets

`DefaultCurvePresets()` has seven, over 0 to 1 both ways: Constant, Linear, Linear falling,
Ease in, Ease out, Ease in and out, and Ease in and out falling.

- A click on one makes the active channel's curve that shape, stretched over `range`.
- Set `view.presets` to offer your own instead:

```cpp
falloff.presets = { CurvePreset{ .name = "Sharp", .curve = MakeCurve(sharpPoints) } };
```

## Classes

- the field: `curve-field`, selected while its editor is open
- the editor's window: `curve-window`
- the rows over and under the plot: `curve-toolbar`; a channel's chip: `curve-channel`,
  selected for the active one
- the plot: `curve-plot`. Its padding holds the grid's numbers.
- drawn on it: `curve-label` (a grid number), `curve-readout` (the selected key's time and
  value), `curve-box-select`
- a preset: `curve-preset`
- with `fps`:
  - the plot: `curve-plot-frames` (no numbers under it); "Interpolation" and "Tangents":
    `curve-toolbar-label`
  - the ruler: `curve-ruler`; drawn on it, `curve-ruler-label` (a frame number) and
    `curve-playhead` (the playhead's pill)
- the tree: `curve-tree`, its header `curve-tree-header`
  - a group's row: `curve-tree-group`
  - a channel's row: `curve-tree-row`, selected for the active one; `curve-tree-row-hidden`
    when hidden
  - in a row: `curve-tree-swatch`, `curve-tree-name`, `curve-tree-value`

The palette sets:

- the plot and the field: `--curve-plot`
- the grid: `--curve-grid`, `--curve-grid-major`, and `--curve-zero` for the lines at zero
- a channel's color: `--warn`, `--axis-x`, `--axis-y`, `--axis-z`, `--text-1`
- a selected key and its handles: `--text-1`, `--accent`

---

[Previous: Color and gradient fields](color-and-gradient.md) | [Controls](../controls.md) | [Next: Plots](plots.md)
