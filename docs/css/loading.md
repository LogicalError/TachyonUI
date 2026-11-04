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
- Parse errors are logged as `file:line:column: message`.
  - The bad declaration or rule is skipped.
  - Parsing continues after it.

---

[CSS and theming](../css.md) | [Next: Syntax](syntax.md)
