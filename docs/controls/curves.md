# Curves

A curve maps a number to another, through keys you place: a falloff over a distance, an
easing over time, a color's channels in a Curves node.

- `CurveField`: the curve in a small box. A click opens the curve editor in a window under
  it.
- `CurveEditor`: that editor, as a control of its own.
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
| `onEdit` | A change was made (already applied): put it on your undo history |

Each `CurveChannel` has:

- a `name`, shown when there are several: "R", "Alpha"
- a `color`: `Value` (the warning color), `Red`, `Green`, `Blue` or `Alpha` (the axis colors
  and the primary text color)
- `visible`: drawn; a hidden channel's keys can't be picked or added to
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

- `MakeCurve(points, mode)` makes a curve from (time, value) points, in time order, with its
  slopes worked out.

### Tangent modes

A key's `mode` says how its slopes are set:

| Mode | The slopes |
| --- | --- |
| `ClampedAuto` (the default) | Smooth through the key, without overshooting its neighbors: level at a peak or a trough |
| `Auto` | Smooth through the key, the slope between its neighbors; may overshoot |
| `FreeSmooth` | One slope you set, the same on both sides |
| `Flat` | Level on both sides |
| `Broken` | Each side its own, as `inMode` and `outMode` say |

A `Broken` key's sides:

| Side mode | That side |
| --- | --- |
| `Free` | The slope you set |
| `Linear` | Straight to the neighbor |
| `Constant` | A step: the value holds up to the next key |

- An end key's automatic slope is level.
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

### Mouse

- On a key:
  - a click selects it; Shift adds it, Ctrl toggles it
  - a drag moves the selected keys; Escape during it puts them back
  - a right click: its menu
- On a selected key's handle: a drag sets that side's slope.
  - A key that isn't `Broken` becomes `FreeSmooth`, the same slope both sides.
  - A `Broken` key's side becomes `Free`.
- On the empty plot:
  - a click clears the selection
  - a drag selects the keys in a box; Shift adds them, Ctrl toggles them
  - a double click adds a key to the active channel, on its curve when near it
  - a right click: Add key, Frame all
- The wheel zooms round the pointer: Shift along the time only, Ctrl the values only.
- A drag with the middle button pans.

The key's menu:

- Delete key
- Clamped auto, Auto, Free smooth, Flat, Broken; the key's mode checked
- Left tangent, Right tangent, Both tangents: Free, Linear or Constant (making it Broken)

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
| `SetCurveTangentModeEdit(view, mode)` | The selected keys' mode |
| `SetCurveSideModeEdit(view, side, in, out)` | The selected keys broken, and a side or both set |
| `ApplyCurvePresetEdit(view, channel, preset, makeID)` | A preset stretched over the range |
| `SetCurveWrapEdit(view, channel, pre, post)` | A channel's wrap modes |

```cpp
CommitCurveEdit(falloff, AddCurveKeyEdit(falloff, 0, 0.5f, 0.8f, idManager.CreateStaticID()));
```

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

The palette sets:

- the plot and the field: `--curve-plot`
- the grid: `--curve-grid`, `--curve-grid-major`, and `--curve-zero` for the lines at zero
- a channel's color: `--warn`, `--axis-x`, `--axis-y`, `--axis-z`, `--text-1`
- a selected key and its handles: `--text-1`, `--accent`

---

[Previous: Color and gradient fields](color-and-gradient.md) | [Controls](../controls.md) | [Next: Menus](menus.md)
