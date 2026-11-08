# How a theme loads

A theme is made of two kinds of CSS file:

- A palette: a file that gives names to colors and shadows, and nothing else.
  - Switching to another palette changes every color at once, for example from dark to
    light.
- Style files: the rules that say how each part of each control looks.
  - They use the palette's colors by name.

A theme is a palette file followed by one or more style files:

| File | Contents |
| --- | --- |
| `themes/dark.css`, `themes/light.css` | The palette: only `:root` custom properties (colors, shadows). |
| `themes/styles.css` | The library's rules: shared dimensions in `:root`, `@font-face`, one class per control part. |
| `themes/showcase.css` | Showcase app only. |

A custom property is a named value, such as `--accent`. It is defined inside a `:root { }`
block and used in a rule with `var(--accent)` (see
[Custom properties](syntax.md#custom-properties)). From `themes/dark.css` and
`themes/styles.css`:

```css
:root {
    --text-link: #8FB3FF;
    --text-link-hover: #B5CDFF;
}

.link       { color: var(--text-link); cursor: pointer; }
.link:hover { color: var(--text-link-hover); }
```

## Built-in files

- `dark.css`, `light.css`, `styles.css` and their fonts are built into the library.
- A file with the same name on disk replaces the built-in one.

## Load order

- The files are joined in the order of the table above and parsed once.
- `var()` is resolved at parse time, against the variables defined above it.
  - So the palette must come first.
- Redefining a variable later doesn't change rules that already used it.

## Calls

These are members of `GUIApplication`, the class your app derives from.

- `SetResourceRoot(dir)`: the folder that relative theme and font paths start from.
  - A file missing from that folder falls back to the built-in copy.
  - Empty means the working directory.
  - Call it before `Initialize`.

  For example, in your app's `Initialize`:

  ```cpp
  tachyon::Result<void> Initialize(const InitializeOptions& options = {}) override
  {
      SetResourceRoot(TACHYON_RESOURCE_DIR);   // defined by the TachyonUI package
      return GUIApplication::Initialize(options);
  }
  ```

- `SetStyleFiles(files)`: the style files loaded after the palette, in order.
  - The default is `{ "styles.css" }`.
  - Relative paths start from the palette's folder.
  - Call it before `Initialize`, or call `RequestReloadTheme` after it.
- `RequestReloadTheme(path, fontScale)`: loads another palette, another font scale, or
  both.
  - It happens at the start of the next frame.
  - Before `Initialize`, it picks the starting palette.
  - It does nothing if neither changed.
  - A palette switch crossfades over 200 ms: the old look fades out over the new one.
  - There is no crossfade with reduced motion (see
    [Changing state](syntax.md#changing-state)).
- `GetThemePath()`: the current palette path.
  - The default is `themes/dark.css`.
- `GetGlobalFontScale()`: the current font scale.
- `ThemeReloadResult()`: the result of the last reload.
  - On failure the previous theme stays.

  For example, to show why a reload failed:

  ```cpp
  if (!ThemeReloadResult())
      Text(ThemeReloadResult().GetError().message);
  ```

- `OnStylesReloaded()`: a virtual function, called after each reload.
  - A reload clears every registered style, so a `StyleRef` you kept from before no longer
    works. A `StyleRef` is the handle a control takes to know which style to draw with.
  - Fetch or register your `StyleRef`s again here.

  For example, with a `tachyon::StyleRef bannerStyle;` member:

  ```cpp
  void OnStylesReloaded() override
  {
      bannerStyle = theme.GetStyle("banner");
  }
  ```

Your own classes go after the library's file:

```cpp
const std::string_view styleFiles[] = { "styles.css", "my_app.css" };
SetStyleFiles(styleFiles);
```

Switching palette or text size at runtime (`examples/custom_theme/custom_theme.cpp`):

```cpp
Button("Light", [this] { RequestReloadTheme("themes/light.css", GetGlobalFontScale()); });
Button("Larger text", [this] { RequestReloadTheme(GetThemePath(), GetGlobalFontScale() * 1.1f); });
```

- The font scale multiplies every CSS `font-size`.
  - With a scale of `1.1f`, a 12px `font-size` draws at 13.2px.
- The theme also reloads:
  - on `SetUIScale`
  - when the main window moves to a monitor with a different display scale
  - on `SetAccent` (see [The accent color](#the-accent-color))
- Parse errors are logged as `file:line:column: message`.
  - The bad declaration or rule is skipped.
  - Parsing continues after it.

## The accent color

The accent is the one color the UI uses for what's active or chosen: selected rows, toggles
that are on, slider fills, progress bars, the focus ring, the text caret. A user can pick
another one without a new palette file.

- `SetAccent(colors)` replaces the palette's accent.
  - `colors` is an `AccentColors`: one color for dark palettes, one for light ones.
  - The theme uses the one that suits the palette loaded, so a switch from dark to light
    keeps the user's choice.
  - It applies at the start of the next frame, with the same 200 ms crossfade as a palette
    switch.
  - It stays through palette and font changes.
- `SetAccent(std::nullopt)` goes back to the palette's own accent.
- `GetAccent()` returns the accent set, or none.
- `kAccentPresets` has the design's four, each with a `name` and its `colors`: Blue (the
  palettes' own), Teal, Violet and Orange.

```cpp
SetAccent(kAccentPresets[1].colors);   // teal
```

```cpp
SetAccent(AccentColors{ .dark = { 0xF4, 0x72, 0xB6, 0xFF }, .light = { 0xB8, 0x30, 0x6F, 0xFF } });   // your own pink
```

A settings page with the presets in a dropdown (from the showcase's Appearance page):

```cpp
std::string_view names[std::size(kAccentPresets)];
for (size_t i = 0; i < std::size(kAccentPresets); ++i)
    names[i] = kAccentPresets[i].name;
if (Dropdown(accentMenuID, accentIndex, names))   // member: int accentIndex
    SetAccent(accentIndex == 0 ? std::nullopt : std::optional(kAccentPresets[accentIndex].colors));
```

The palette variables that follow the accent:

| Variable | What it is | Dark | Light |
| --- | --- | --- | --- |
| `--accent`, `--caret`, `--divider-hover` | The accent itself | | |
| `--selection` | Selected text | 35% | 25% |
| `--accent-soft` | Selected rows and items, over `--bg-panel` | 16% | 12% |
| `--bg-menu-hover` | The highlighted menu row, over `--bg-popup` | 16% | 12% |
| `--focus-ring` | The focus ring's 3px halo | 30% | 25% |
| `--selected-ring` | A selected node's halo | 16% | 16% |

- These are the amounts the palettes use for their own blue, so Blue gives the palettes'
  colors back.
- `--info`, the link colors and the document colors keep the palette's values. They're
  other meanings that happen to be blue too.
- `--on-accent`, the text on accent fills, keeps the palette's value: dark on the dark
  palette, white on the light one. That suits the four presets; with a color of your own,
  check that text on it reads well.
- `theme.IsLight()` says whether the palette loaded is a light one.

---

[CSS and theming](../css.md) | [Next: Syntax](syntax.md)
