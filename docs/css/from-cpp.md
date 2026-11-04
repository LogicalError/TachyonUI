# Using styles from C++

## Classes from the theme

`theme` is the `GUIApplication` member that holds the loaded theme.
`theme.GetStyle("class")` returns the `StyleRef` for a class from any loaded file. A
`StyleRef` is the handle a control takes to know which style to draw with:

```cpp
Text("My themed app", theme.GetStyle("banner"));
Button("Careful!", [] {}, theme.GetStyle("warning-button"));
```

- For an undefined class, `GetStyle` returns an empty `StyleRef` and logs a warning once.
- `theme.FindStyle("class")` returns a `Result<StyleRef>` with an error instead.

  ```cpp
  if (tachyon::Result<tachyon::StyleRef> banner = theme.FindStyle("banner"))
      Text("My themed app", *banner);
  else
      Text(banner.GetError().message);
  ```

- `theme.GetColor("--accent", fallback)` reads a palette color as a `color4f`.
  - It returns `fallback` when the palette doesn't define the color.

  For example, with a `tachyon::StyleRef accentStyle;` member:

  ```cpp
  tachyon::Style accentBox;
  accentBox.states[tachyon::ElementState::None].backgroundColor =
      tachyon::ColorStyle::Create(theme.GetColor("--accent", tachyon::color4f{ 0.4f, 0.6f, 1.0f, 1.0f }));
  accentStyle = styleManager.Register(accentBox);
  ```

- Library controls use styles registered by name, such as:
  - `theme.labelStyleRef`
  - `theme.windowStyleRef`
  - `theme.regularControls.button`
- To restyle them, edit their classes: `.label`, `.window`, `.button`.
- A reload clears every registered style, making old `StyleRef`s stale.
  - Look classes up each frame, or store them again in `OnStylesReloaded` (see
    [How a theme loads](loading.md)).

## Styles built in code

Fill in a style and register it with the `StyleManager`, the `styleManager` member of
`GUIApplication`:

```cpp
tachyon::Style s;
s.padding      = tachyon::BoxEdges(4, 8);
s.borderWidths = tachyon::BoxEdges(1);
s.borderRadius = tachyon::BoxCorners(4);
s.states[tachyon::ElementState::None].backgroundColor  = tachyon::ColorStyle::Create(tachyon::color4b{ 30, 33, 40, 255 });
s.states[tachyon::ElementState::Hover].backgroundColor = tachyon::ColorStyle::Create(tachyon::color4b{ 35, 39, 47, 255 });
tachyon::StyleRef ref = styleManager.Register(s);
```

- `Style::states` has 33 slots, indexed by `ElementState`.
  - Each slot is set separately.
  - A slot is any combination of `Active`, `Hover`, `Focused`, `Selected` and `Selection`,
    for example `ElementState::Active | ElementState::Hover`.
  - `Disabled` is a slot of its own, alone.
- Register them again in `OnStylesReloaded`.
- A style built in code doesn't get the font scale.

Text settings:

| Where | Holds |
| --- | --- |
| `Style::textLayoutStyle` (all states) | Direction, wrapping, alignment, baseline, overflow |
| state `textStyle` | Font, size, spacing, line height, language, decoration lines (`underLine`, `overLine`, ...) with thickness, offset, line style |
| state `textColor` | CSS `color`. Unset means transparent text. |
| state `decorationColors` | Indexed by `DecorationLine`. Unset takes the text color. |

- A state that changes only colors keeps the text layout.
- A state that changes `textStyle` lays the text out again.

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

The UI scale is how much larger than CSS pixels the UI is drawn: the main window's display
scale times `SetUIScale`.

- Styles are written in CSS pixels.
- They are stored in device pixels, the pixels of the screen.
- `StyleManager::Register` scales them by the UI scale:
  - Font sizes, letter and word spacing, and px line heights scale exactly.
  - Everything else rounds to whole pixels: margins, padding, sizes, gaps, radii, shadows,
    icon sizes, fixed grid tracks.
  - A set border or text decoration stays at least 1 px.
  - `auto` stays `auto`.

| Call | Use |
| --- | --- |
| `Register(style, tachyon::StyleUnits::Pixels)` | Style already in device pixels |
| `styleManager.ToPixels(style)` | Scaled copy of a style |
| `styleManager.Px(n)` | CSS px to whole device px; non-zero stays at least 1 |

For example, a style built in device pixels, with a `tachyon::StyleRef padded;` member:

```cpp
tachyon::Style p;
p.padding = tachyon::BoxEdges(uint16_t(styleManager.Px(4)), uint16_t(styleManager.Px(8)));
padded = styleManager.Register(p, tachyon::StyleUnits::Pixels);
```

---

[Previous: Properties](properties.md) | [CSS and theming](../css.md)
