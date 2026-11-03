# Using styles from C++

## Classes from the theme

`theme.GetStyle("class")` returns the `StyleRef` for a class from any loaded file:

```cpp
Text("My themed app", theme.GetStyle("banner"));
Button("Careful!", [] {}, theme.GetStyle("warning-button"));
```

- Undefined class: empty `StyleRef`, warning logged once. `theme.FindStyle("class")` returns
  a `Result<StyleRef>` with an error instead.
- `theme.GetColor("--accent", fallback)` reads a palette color as a `color4f`.
- Library controls use styles registered by name (`theme.labelStyleRef`,
  `theme.windowStyleRef`, `theme.regularControls.button`); edit `.label`, `.window`,
  `.button` to restyle them.
- A reload clears every registered style, making old `StyleRef`s stale. Look classes up each
  frame, or store them again in `OnStylesReloaded`.

## Styles built in code

Fill in a style and register it with the `StyleManager`:

```cpp
tachyon::Style s;
s.padding      = tachyon::BoxEdges(4, 8);
s.borderWidths = tachyon::BoxEdges(1);
s.borderRadius = tachyon::BoxCorners(4);
s.states[tachyon::ElementState::None].backgroundColor  = tachyon::ColorStyle::Create(tachyon::color4b{ 30, 33, 40, 255 });
s.states[tachyon::ElementState::Hover].backgroundColor = tachyon::ColorStyle::Create(tachyon::color4b{ 35, 39, 47, 255 });
tachyon::StyleRef ref = styleManager.Register(s);
```

- `Style::states` has 33 slots, each set separately, indexed by `ElementState`: any
  combination of `Active`, `Hover`, `Focused`, `Selected`, `Selection` (e.g.
  `ElementState::Active | ElementState::Hover`), plus `Disabled` alone.
- Register them again in `OnStylesReloaded`.
- A style built in code doesn't get the font scale.

Text settings:

| Where | Holds |
| --- | --- |
| `Style::textLayoutStyle` (all states) | Direction, wrapping, alignment, baseline, overflow |
| state `textStyle` | Font, size, spacing, line height, language, decoration lines (`underLine`, `overLine`, ...) with thickness, offset, line style |
| state `textColor` | CSS `color`. Unset means transparent text. |
| state `decorationColors` | Indexed by `DecorationLine`. Unset takes the text color. |

A state that changes only colors keeps the text layout; one that changes `textStyle` lays
the text out again.

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

Styles are written in CSS pixels and stored in device pixels. `StyleManager::Register`
scales by the UI scale (main window's display scale times `SetUIScale`):

- Font sizes, letter and word spacing, and px line heights scale exactly.
- Everything else (margins, padding, sizes, gaps, radii, shadows, icon sizes, fixed grid
  tracks) rounds to whole pixels. A set border or text decoration stays at least 1 px.
  `auto` stays `auto`.

| Call | Use |
| --- | --- |
| `Register(style, tachyon::StyleUnits::Pixels)` | Style already in device pixels |
| `styleManager.ToPixels(style)` | Scaled copy of a style |
| `styleManager.Px(n)` | CSS px to whole device px; non-zero stays at least 1 |

---

[Previous: Properties](properties.md) | [CSS and theming](../css.md)
