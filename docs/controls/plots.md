# Plots

A plot shows series of numbers over a range, as bars, lines or a filled area: an image's
histogram, a sound's levels, frame times.

- `Plot`: the series over the x range, with handles you drag along it (a Levels control's
  black, gray and white points).
- `HistogramBins`: counts samples into bins, for a histogram's series.

## Plot

```cpp
bool Plot(ID id, PlotView& view, LayoutOverride&& constraints = {});
```

- It fills the room it's given; give it a height.
- With several series, a chip over it for each, and All:
  - a click on a series' chip shows it on its own, at full strength
  - All shows every series overlaid, faded so they show through each other
- The value of each series under the pointer, at its top left.
- It returns true on the frame after a handle moved.
- Disabled, its handles don't move.

```cpp
PlotView levels;   // a member

levels.series  = { PlotSeries{ .name = "Luma", .values = HistogramBins(lumas, 256) } };
levels.handles = { { "Black", 0.0f }, { "White", 1.0f } };

if (Plot(idManager.AllocateDynamicID("levels"), levels, LayoutOverride{}.Height(160)))
    ApplyLevels(levels.handles[0].x, levels.handles[1].x);
```

## The view

`PlotView` is yours: keep it from frame to frame, as you would a `CurveView`.

| Field | What it's for |
| --- | --- |
| `series` | The series drawn |
| `xMin`, `xMax` | The x range the series spread over and the handles stay in |
| `fitY` | The y range from 0 to the largest value in view (the default) |
| `yMin`, `yMax` | The y range when `fitY` is off |
| `yScale` | `Linear`, or `Log`: log(1 + value), so small counts show beside large ones |
| `labels` | The grid's numbers (the y numbers on a linear scale only) |
| `shownSeries` | The series shown on its own, or -1 for all; the chips set it |
| `handles` | The handles under the plot |
| `onEdit` | A handle moved (already applied): put it on your undo history |

## Series

Each `PlotSeries` has:

- a `name`, for its chip and the value under the pointer
- a `kind`: `Bars`, `Line` or `Area` (filled under a line)
- a `color`, as a curve channel's: `Value` (the warning color), `Red`, `Green`, `Blue`, or
  `Alpha` (the primary text color)
- its `values`, spread evenly over the x range: value i fills the i-th of `values.size()`
  equal slots
- `visible`

```cpp
PlotSeries red{ .name = "R", .kind = PlotSeriesKind::Area, .color = CurveChannelColor::Red,
                .values = HistogramBins(reds, 64, 0.0f, 255.0f) };
```

`HistogramBins(samples, bins, min, max)` counts the samples in each of `bins` equal bins over
[min, max]:

- a sample at `max` goes in the last bin
- samples outside, and NaN, go in none

## Handles

Each `PlotHandle` is a marker under the plot, at its `x`, with a line up through the plot.

- `name`: what it sets, for your own labels.
- `color`: its triangle's fill; leave it at alpha 0 for the text color.

```cpp
levels.handles = { { "Black", 0.0f,   { 0.0f, 0.0f, 0.0f, 1.0f } },
                   { "Gray",  128.0f, { 0.5f, 0.5f, 0.5f, 1.0f } },
                   { "White", 255.0f, { 1.0f, 1.0f, 1.0f, 1.0f } } };
```

- A drag moves a handle.
  - It stays between its neighbours, and inside the x range.
  - Escape during the drag puts it back.
- With the plot focused, Left and Right move the handle dragged last by a hundredth of the
  range; with Shift, a tenth.

### Edits and undo

Every move comes to `onEdit` as a `PlotEdit`, already done: where each handle was (`from`) and
is (`to`).

- `ApplyPlotEdit(view, edit, undo)` does it again, or undoes it.
- `MovePlotHandleEdit(view, handle, x)` is the edit that moves one, kept between its
  neighbours.
- `CommitPlotEdit(view, edit)` applies an edit and hands it to `onEdit`.

```cpp
levels.onEdit = [this](const PlotEdit& edit) {
    history.Push({ .description = "Move a level",
                   .undo = [this, edit] { ApplyPlotEdit(levels, edit, /*undo*/ true); },
                   .redo = [this, edit] { ApplyPlotEdit(levels, edit); } });
};
```

## Zooming

A plot zooms and scrolls along x, as the other canvases do (see Panning and zooming in
[Common conventions](conventions.md)):

- the wheel or a pinch zooms round the pointer, or the wheel scrolls along x, as
  `SetWheelMode` says
- with the wheel set to scroll, up and down is left to what the plot is in (an inspector
  scrolls on)
- the middle button (or Alt + left) drags along x
- A, with the plot focused, shows all of it again

The y range follows what's in view: zoomed in, the bins in view fill its height.
`PlotShownX(view)` and `PlotShownY(view)` give the ranges in view.

## Classes

- the chips' row: `plot-toolbar`; a chip: `plot-channel`, selected for the series shown on
  its own
- the plot: `plot`. Its padding holds the y numbers at the left, and under the grid the
  handles and the x numbers.
- drawn on it: `plot-label` (a grid number), `plot-readout` (the values under the pointer)

The palette sets:

- the plot: `--curve-plot`
- the grid: `--curve-grid`, `--curve-grid-major`, `--curve-zero`
- a series' color: `--warn`, `--axis-x`, `--axis-y`, `--axis-z`, `--text-1`
- the focused plot's active handle: `--accent`

---

[Previous: Curves](curves.md) | [Controls](../controls.md) | [Next: Viewport](viewport.md)
