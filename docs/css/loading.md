# How a theme loads

A theme is a palette file followed by one or more style files:

| File | Contents |
| --- | --- |
| `themes/dark.css`, `themes/light.css` | The palette. These hold only `:root` custom properties (colors, shadows). |
| `themes/styles.css` | The library's rules: shared dimensions in `:root`, `@font-face`, and one class per control part. |
| `themes/showcase.css` | Classes used only by the showcase app, which is the only thing that loads it. |

`dark.css`, `light.css` and `styles.css` are built into the library, along with the fonts
their `@font-face` rules name, so your app doesn't need to ship any of them. If a file with
the same name exists on disk, it's used instead of the built-in one.

The files are joined into one text in the order above and parsed once. `var()` is resolved
at parse time against the variables defined so far, which means the palette has to come
before the files that use it. The same goes within a file. A rule only sees variables
defined above it, and redefining a variable further down doesn't change rules that already
used it.

Your application picks the files with these `GUIApplication` calls:

| Call (`GUIApplication`) | What it does |
| --- | --- |
| `SetResourceRoot(dir)` | Sets the folder that relative theme and font paths load from. If a file is missing there and the library has a built-in copy, the built-in one is used. An empty path means the working directory. Call it before `Initialize`. |
| `SetStyleFiles(files)` | Sets the style files loaded after the palette, in order. The default is `{ "styles.css" }`. Relative paths are resolved next to the palette file. Call it before `Initialize`, or call `RequestReloadTheme` afterwards. |
| `RequestReloadTheme(path, fontScale)` | Loads another palette, another font scale, or both, at the start of the next frame. Called before `Initialize`, it picks the starting palette. It does nothing when nothing changed. Switching palettes crossfades: the old look fades out over the new one in 200 ms (unless reduced motion is on). |
| `GetThemePath()`, `GetGlobalFontScale()` | Return the current palette path (default `themes/dark.css`) and font scale. |
| `ThemeReloadResult()` | Tells you how the last reload went. If it failed (say a file couldn't be opened), the previous theme stays in place. |
| `OnStylesReloaded()` | A virtual function called after each reload. Fetch or register your `StyleRef`s again here. |

If your application adds its own classes, keep the library's file first:

```cpp
const std::string_view styleFiles[] = { "styles.css", "my_app.css" };
SetStyleFiles(styleFiles);
```

Here's how you switch palettes or text size at runtime (from
`examples/custom_theme/custom_theme.cpp`):

```cpp
Button("Light", [this] { RequestReloadTheme("themes/light.css", GetGlobalFontScale()); });
Button("Larger text", [this] { RequestReloadTheme(GetThemePath(), GetGlobalFontScale() * 1.1f); });
```

The font scale multiplies every CSS `font-size`. The theme also reloads when the UI scale
changes (`SetUIScale`), or when the main window moves to a monitor with a different display
scale.

Parse errors don't stop loading. Each one is logged as `file:line:column: message`. The bad
declaration or rule is skipped, and parsing carries on.

---

[CSS and theming](../css.md) | [Next: Syntax](syntax.md)
