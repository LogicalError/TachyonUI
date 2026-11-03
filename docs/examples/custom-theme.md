# Custom theme

Adds the app's own CSS classes next to the library's, and switches palette and font size
at run time.
Full source: [examples/custom_theme/custom_theme.cpp](https://github.com/LogicalError/TachyonUI/blob/main/examples/custom_theme/custom_theme.cpp)
and [examples/custom_theme/custom_theme.css](https://github.com/LogicalError/TachyonUI/blob/main/examples/custom_theme/custom_theme.css).

## Adding a style file

`SetStyleFiles` sets the CSS rule files, loaded in order after the palette. Keep
`styles.css` first. Call it before `Initialize`:

```cpp
// The library's rules first, then the app's (relative paths are next to the palette).
const std::string_view styleFiles[] = { "styles.css", TACHYON_EXAMPLE_DIR "/custom_theme.css" };
SetStyleFiles(styleFiles);
if (tachyon::Result<void> result = GUIApplication::Initialize(options); !result)
    return result;
```

## The app's classes

Style files load after the palette (`themes/dark.css` or `light.css`), so they can use its
variables and follow palette changes.

```css
.banner {
    padding: 12px;
    margin: 4px;
    border-width: 2px;
    border-radius: 8px;
    border-color: var(--accent);
    background-color: var(--accent-soft);
    color: var(--text-1);
    font-size: 20px;
    text-align: center;
}
```

A class can define its own states:

```css
.warning-button:hover  { background-color: #7a5018; }
.warning-button:active { background-color: #a06a20; }
```

## Using the classes

`theme.GetStyle(name)` looks up a class. Call it in `OnGUI`; don't store the result, a
theme reload registers the styles again.

```cpp
// Look the classes up every frame: a theme reload registers new styles.
Text("My themed app", theme.GetStyle("banner"));
```

```cpp
Button("Careful!", [] {}, theme.GetStyle("warning-button"));
```

## Switching palettes and font size

`RequestReloadTheme(palette, fontScale)` reloads at the start of the next frame, so it is
safe in a callback.

```cpp
BeginHorizontal();
Button("Dark",  [this] { RequestReloadTheme("themes/dark.css",  GetGlobalFontScale()); });
Button("Light", [this] { RequestReloadTheme("themes/light.css", GetGlobalFontScale()); });
EndHorizontal();

BeginHorizontal();
Button("Smaller text", [this] { RequestReloadTheme(GetThemePath(), GetGlobalFontScale() * 0.9f); });
Button("Larger text",  [this] { RequestReloadTheme(GetThemePath(), GetGlobalFontScale() * 1.1f); });
EndHorizontal();
```

On failure the previous theme stays and `ThemeReloadResult()` gives the error. Selectors
and properties: [CSS and theming](../css.md).

---

[Previous: Docking](docking.md) | [Examples](../examples.md) | [Next: Host device](host-device.md)
