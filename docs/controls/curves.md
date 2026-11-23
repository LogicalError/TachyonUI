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
bool CurveField(ID id, CurveViewRef view, LayoutOverride&& constraints = {});   // a CurveView, or { data, state }
```

- It draws the curves over `view.range`, in a 32px plot, the active channel's on top.
  - A single value (`CurveChannelColor::Value`) in the accent color.
  - A color's red, green and blue channels in the axis colors, over a dashed line where the
    output equals the input, to compare against.
  - A line at 0 when the range goes below and above it.
- Hovered, it shows a small open button in its top-right corner.
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

`CurveView` is yours: keep it from frame to frame, as you would a `TimelineView`. It's two
parts in one, which you can also keep apart to show the curves in several places (see
[Several views of the same curves](#several-views-of-the-same-curves)):

- `CurveData`: the curves themselves, their range and presets, and their edits.
- `CurveViewState`: how one field or editor shows them, the active channel and the selection.

| Field | On | What it's for |
| --- | --- | --- |
| `channels` | `CurveData` | The curves: one for a plain curve, four for a color's R, G, B and A |
| `range` | `CurveData` | What the field shows and the editor opens on; keys stay in its time span |
| `clampValues` | `CurveData` | Keys stay in the range's value span too |
| `presets` | `CurveData` | The shapes under the plot; empty: the built-in ones |
| `fps` | `CurveData` | Frames a second, for animation curves; 0 (the default): plain times |
| `playhead` | `CurveData` | With `fps`: the time the ruler and the tree show, in seconds |
| `onPlayheadChanged` | `CurveData` | With `fps`: the user moved the playhead (already set) |
| `onEdit` | `CurveData` | A change was made (already applied): put it on your undo history |
| `activeChannel` | `CurveViewState` | The channel whose keys are added to |
| `selectedKeys` | `CurveViewState` | The selected keys, by `stableID` |

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
                   .redo = [this, edit] { ApplyCurveEdit(falloff, edit); },
                   .drop = [this, edit](bool undone) { ReleaseCurveEditIDs(idManager, edit, undone); } });
};
```

- A deleted key keeps its static ID while its edit can still be undone.
  - The step's `drop` runs when the history lets go of the step: a new edit replaces it after
    an undo, or the history is cleared.
  - `ReleaseCurveEditIDs` then gives back the IDs that only the edit still holds, such as the
    keys a preset replaced.

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
  - Frame all (its tooltip gives its shortcut, A)
  - with one key selected, its Frame and Value
- A key's interpolation and tangents are in its menu (see The key's menu).
- Values show two decimals: in the tree, and beside the selected key ("X · f 24 · 2.00").
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
- On a curve, away from its keys:
  - it's drawn in full color and a little thicker while the pointer is on it
  - a press makes it the active channel; a drag from there still selects in a box
  - a right click: Add key puts the key on that curve
- The key or handle under the pointer is drawn larger, with a light edge.
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

- Delete key
- Interpolation: Constant, Linear, Bezier, each with its curve glyph; the key's own checked
- Tangents: Auto, Aligned, Free, Flat; the key's own checked
- They change all the selected keys. A right click on a key that isn't selected selects it
  alone first.
- A right click while the menu is open opens it again, for what's under the pointer now.

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

- `ApplyCurveEdit(data, edit, undo)` does it again, or undoes it. It takes the curves alone,
  a `CurveView` or a `CurveData`.
- `CommitCurveEdit(view, edit)` applies an edit, lets go of the selected keys it removed,
  and hands it to `onEdit`.

To change the curves from your own code as one undo step, make an edit and commit it:

| Helper | The edit it makes |
| --- | --- |
| `AddCurveKeyEdit(data, channel, time, value, keyID)` | A key, kept in the range; none on a hidden channel or where there's one already |
| `DeleteSelectedCurveKeysEdit(view)` | A view's selected keys gone |
| `SetCurveTangentModeEdit(view, mode)` | A view's selected keys' tangents |
| `SetCurveInterpolationEdit(view, interpolation)` | A view's selected keys' interpolation |
| `ApplyCurvePresetEdit(data, channel, preset, makeID)` | A preset stretched over the range |
| `SetCurveWrapEdit(data, channel, pre, post)` | A channel's wrap modes |

```cpp
CommitCurveEdit(falloff, AddCurveKeyEdit(falloff, 0, 0.5f, 0.8f, idManager.CreateStaticID()));
```

- With `fps` set, `AddCurveKeyEdit` puts the key on the nearest frame.
- `SnapToFrame(data, time)` does the same for your own times: 0.43 at 10 fps is 0.4.

## Several views of the same curves

The curves can show in more than one place at once, for example a field in each of two
inspectors, or two curve editor panels. Keep them once, as a `CurveData`, and a
`CurveViewState` for each field or editor of them:

```cpp
CurveData      falloff;         // the curves, their range, onEdit
CurveViewState left, right;     // each inspector's: the selection, the zoom, its editor window

// Each frame, in each inspector:
CurveField(idManager.AllocateDynamicID("falloff"), { falloff, left });
```

- Each opens, zooms and selects on its own.
  - For example, a click on the left field opens its editor; the right one's stays shut.
- An edit made in one shows in all of them.
  - For example, a key dragged in the left editor moves in the right one too.
  - A key deleted in one leaves the others' selections the next time they draw.
- A field returns true after a change made in it; the others don't.
- `SaveViewState` and `RestoreViewState` take a `CurveViewState`, to keep an editor panel's
  zoom with its layout.

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
  - the plot: `curve-plot-frames` (no numbers under it)
  - Frame all: `curve-frame-all`, a flat button
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
