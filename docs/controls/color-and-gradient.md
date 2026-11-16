# Color and gradient fields

## ColorField

A swatch, the hex value and the alpha in percent. It edits a `color4f` (four floats) or a
`color4b` (four 8-bit values). Clicking the swatch opens the picker:

- OK keeps the color.
- Cancel, Escape and the close button restore it.
  - Escape also returns focus to the swatch.
- In the square and the hue and alpha strips:
  - arrow keys step 1%, or 10% with Shift
  - Page Up / Down and Home / End also work
  - Escape in the middle of a drag restores the color from before the press and keeps
    the picker open
- `allowHDR` allows values above 1.
- The picker's title names what it edits: the label drawn just before the field.
  - A field after `DragLabel("Tint")` opens "Color  Tint".
  - A label that only repeats the title ("Color") is left out.
- New sits over Original in the preview, each at its alpha, named beside its half.
- The footer says the format and the range the values show in, "sRGB · 8-bit · 0–255".
  - A click on the range switches it to 0–1 and back.

The swatches and the recent colors:

- The swatches start as a palette of twelve. The user adds the current color with "+",
  and removes one with a right-click.
  - `SetColorSwatches(colors)` sets them, for the app's own palette.
  - `ColorSwatches()` reads them, to keep them for the next run.
- Recent holds the last 8 colors kept with OK, newest first.
  - `AddRecentColor(color)` adds one, for colors the app brings back from its last run.

```cpp
const color4f brand[] = { { 0.93f, 0.26f, 0.21f, 1 }, { 0.13f, 0.59f, 0.95f, 1 } };
SetColorSwatches(brand);
for (const color4f& c : savedRecent)   // oldest first, so the newest ends up first
    AddRecentColor(c);
```

```cpp
bool ColorField(ID id, color4f& value, bool allowHDR, StyleRef style, LayoutOverride&& constraints = {});
bool ColorField(ID id, color4b& value, StyleRef style, LayoutOverride&& constraints = {});
```

- Returns `true` when the value changed.
- Classes: `color-field`, `color-swatch`, `color-hex`, `color-alpha`.
- The picker's classes: `color-picker` and `color-picker-*`.

```cpp
DragLabel("Color");
ColorField(color);
```

```cpp
DragLabel("Light");
if (ColorField(lightColor, true))   // color4f; true allows values above 1
    UpdateLighting();
```

```cpp
DragLabel("Tint");
ColorField(tint);   // member: color4b tint
```

## GradientField

A gradient swatch. Clicking it opens the gradient editor:

- OK, Cancel and Escape work as in the color picker.
- A stop dragged off the bar is removed on release.
- Escape in the middle of a drag restores the gradient.

```cpp
bool GradientField(ID id, Gradient& value, StyleRef style, LayoutOverride&& constraints = {});
```

```cpp
DragLabel("Gradient");
GradientField(gradient);   // member: Gradient gradient
```

```cpp
if (GradientField(gradient))   // true when the gradient changed
    RebuildRamp();
```

- The swatch shows the gradient as a color swatch shows a color:
  - its top half has the colors without their alpha
  - its bottom half has them as they are, over a checkerboard
  - For example, a gradient that fades out has its faded end solid on top and see-through
    below.
- The stops show in the editor, where they can be moved.
- The editor's title names what it edits, as the color picker's does: "Gradient  Sky ramp".
- Its table lists each stop's color (a swatch and the hex), its alpha and its location.
  - A `GradientF`'s table adds each stop's blend to the next.
- The preset the gradient is, if any, has an accent ring.
  - An 8-bit `Gradient` matches its preset though its stops are kept a little off.
- Class: `gradient-swatch`.
- The editor's classes: `gradient-editor` and `gradient-editor-*`.

### Float gradients (GradientF)

A `Gradient` has at most 8 stops of 8-bit colors: what styles and CSS use. For gradients
that are data, such as a material editor's gradient map, use a `GradientF`:

- Up to 64 stops (`GradientF::kMaxStops`).
- Float colors (`color4f`): red, green and blue can go past 1 (HDR).
- Each stop says how it blends to the next stop (`StopInterpolation`):
  - `Linear`: a straight blend.
  - `Smooth`: eases out of the stop and into the next.
  - `Constant`: the stop's color all the way to the next stop, a step.
- `colorSpace` is the space the stops blend in, as for a `Gradient` (sRGB, linear RGB,
  HSL, HSV, Lab, Oklab).

```cpp
GradientF heat;
heat.colorSpace = ColorSpace::Oklab;
heat.AddStop(color4f{ 0.05f, 0.0f, 0.1f, 1.0f }, 0.0f, StopInterpolation::Smooth);
heat.AddStop(color4f{ 1.0f, 0.8f, 0.3f, 1.0f }, 0.75f, StopInterpolation::Constant);
heat.AddStop(color4f{ 3.0f, 2.6f, 2.0f, 1.0f }, 0.9f);   // brighter than white
```

Reading it:

- `Sample(t)` is the color at `t`, 0 to 1, with values over 1 kept.
- `WriteTo(span<color4f>)` samples it evenly into a row, for a lookup texture say.
- `WriteTo(span<color4b>)` does the same clamped to 0..1, for display.
- The stops can be in any order; they're sorted when it's sampled.

Converting:

- `GradientF::From(gradient)` takes a `Gradient`'s stops, blending linearly.
- `ToGradient()` gives the first 8 stops as a `Gradient`, colors clamped to 0..1.

`GradientField` edits a `GradientF` too:

- The editor allows its 64 stops and colors over 1.
- The selected stop has a "To next" choice: how it blends to the next stop.
- The swatch and the bar show it clamped to 0..1, as the screen can't show more.
- Where it's brighter than 1, a thin band runs over the swatch.
- The swatch's tooltip says how bright it gets: "Up to 8.0 × white" for a stop 8 times as
  bright as white.

```cpp
bool GradientField(ID id, GradientF& value, StyleRef style, LayoutOverride&& constraints = {});
```

```cpp
DragLabel("Gradient map");
if (GradientField(gradientMap))   // member: GradientF gradientMap
    UploadRamp(gradientMap);
```

---

[Previous: Sliders](sliders.md) | [Controls](../controls.md) | [Next: Curves](curves.md)
