# How a theme loads

A theme is a palette file followed by one or more style files:

| File | Contents |
| --- | --- |
| `themes/dark.css`, `themes/light.css` | The palette: only `:root` custom properties (colors, shadows). |
| `themes/styles.css` | The library's rules: shared dimensions in `:root`, `@font-face`, one class per control part. |
| `themes/showcase.css` | Showcase app only. |

`dark.css`, `light.css`, `styles.css` and their fonts are built into the library. A file
with the same name on disk replaces the built-in one.

The files are joined in the order above and parsed once. `var()` resolves at parse time
against variables defined above it, so the palette must come first. Redefining a variable
later doesn't change rules that already used it.

| Call (`GUIApplication`) | What it does |
| --- | --- |
| `SetResourceRoot(dir)` | Folder for relative theme and font paths; missing files fall back to the built-in copy. Empty means the working directory. Call before `Initialize`. |
| `SetStyleFiles(files)` | Style files loaded after the palette, in order. Default `{ "styles.css" }`. Relative to the palette's folder. Call before `Initialize`, or call `RequestReloadTheme` after. |
| `RequestReloadTheme(path, fontScale)` | Loads another palette and/or font scale at the start of the next frame. Before `Initialize`, picks the starting palette. No-op if nothing changed. Palette switches crossfade over 200 ms (not with reduced motion). |
| `GetThemePath()`, `GetGlobalFontScale()` | Current palette path (default `themes/dark.css`) and font scale. |
| `ThemeReloadResult()` | Result of the last reload. On failure the previous theme stays. |
| `OnStylesReloaded()` | Virtual, called after each reload. Fetch or register your `StyleRef`s again here. |

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
- The theme also reloads on `SetUIScale` and when the main window moves to a monitor with a
  different display scale.
- Parse errors are logged as `file:line:column: message`; the bad declaration or rule is
  skipped and parsing continues.

---

[CSS and theming](../css.md) | [Next: Syntax](syntax.md)
