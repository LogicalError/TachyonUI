# Using styles from C++

## Classes from the theme

`theme.GetStyle("class")` returns the `StyleRef` registered for a class from any loaded
file. You pass it to a control:

```cpp
Text("My themed app", theme.GetStyle("banner"));
Button("Careful!", [] {}, theme.GetStyle("warning-button"));
```

If no file defines the class, you get an empty `StyleRef` and a warning is logged once. If
you'd rather handle that yourself, `theme.FindStyle("class")` returns a `Result<StyleRef>`
with an error instead. To read a palette color as a `color4f`, use
`theme.GetColor("--accent", fallback)`.

The library's own controls use styles that the theme registers by name, for example
`theme.labelStyleRef`, `theme.windowStyleRef` and `theme.regularControls.button`. Change the
matching class (`.label`, `.window`, `.button`) in a style file and they're restyled.

A reload clears every registered style, so any `StyleRef` you got before it is stale. Either
look classes up each frame, or store them again in `OnStylesReloaded`.

## Styles built in code

You can also fill in a style in code and register it with the `StyleManager`:

```cpp
tachyon::Style s;
s.padding      = tachyon::BoxEdges(4, 8);
s.borderWidths = tachyon::BoxEdges(1);
s.borderRadius = tachyon::BoxCorners(4);
s.states[tachyon::ElementState::None].backgroundColor  = tachyon::ColorStyle::Create(tachyon::color4b{ 30, 33, 40, 255 });
s.states[tachyon::ElementState::Hover].backgroundColor = tachyon::ColorStyle::Create(tachyon::color4b{ 35, 39, 47, 255 });
tachyon::StyleRef ref = styleManager.Register(s);
```

`Style::states` has 33 state slots, and you set each one separately. They're indexed by
`ElementState`, which is any combination of `Active`, `Hover`, `Focused`, `Selected` and
`Selection` (for example `ElementState::Active | ElementState::Hover`), plus `Disabled` on
its own. Register these styles again in `OnStylesReloaded`. Note that a style built in code
doesn't get the font scale.

Text settings live in three places:

- `Style::textLayoutStyle` applies to all states. It holds the direction, wrapping,
  alignment, baseline and overflow.
- Each state's `textStyle` holds the font, its size and spacing, the line height, the
  language, and the decoration lines (`underLine`, `overLine`, ...) with their thickness,
  offset and line style.
- Each state's colors are `textColor` (CSS `color`; text without one is transparent) and
  `decorationColors`, indexed by `DecorationLine`. A line without its own color takes the
  text color.

A state that only changes colors keeps the text's layout. A state that changes its
`textStyle` lays the text out again.

```cpp
for (tachyon::StyleState& state : s.states)
{
    state.textStyle.fontSize            = 14.0f;
    state.textStyle.underLine.thickness = 1;
    state.textColor = tachyon::ColorStyle::Create(tachyon::color4b{ 220, 223, 228, 255 });
}
s.states[tachyon::ElementState::Hover].decorationColors[size_t(tachyon::DecorationLine::Under)] =
    tachyon::ColorStyle::Create(tachyon::color4b{ 106, 156, 255, 255 });
```

## UI scale

You write styles in CSS pixels, and they're stored in device pixels. `StyleManager::Register`
scales a style by the UI scale, which is the main window's display scale multiplied by the
value you pass to `SetUIScale`:

- Font sizes, letter and word spacing, and pixel line heights are scaled exactly.
- Everything else (margins, padding, sizes, gaps, radii, shadows, icon sizes, fixed grid
  tracks) is rounded to whole pixels. A border or text decoration that's set stays at least
  1 pixel wide. `auto` sizes stay `auto`.

If your style is already in device pixels, register it with
`Register(style, tachyon::StyleUnits::Pixels)`. `styleManager.ToPixels(style)` gives you the
scaled copy. For sizes in code, `styleManager.Px(n)` converts CSS pixels to whole device
pixels, and a non-zero size stays at least 1.

---

[Previous: Properties](properties.md) | [CSS and theming](../css.md)
