# Custom theme

The app adds its own CSS classes next to the library's, and switches palette and font size
while it runs.
Full source: [examples/custom_theme/custom_theme.cpp](https://github.com/LogicalError/TachyonUI/blob/main/examples/custom_theme/custom_theme.cpp)
and [examples/custom_theme/custom_theme.css](https://github.com/LogicalError/TachyonUI/blob/main/examples/custom_theme/custom_theme.css).

A TachyonUI theme is made of CSS files:

- The palette is the file with the colors, as CSS variables such as `--accent`.
  - The library has two: `themes/dark.css` and `themes/light.css`.
- Style files hold the rules that style the controls.
  - The library's own rules are in `styles.css`.
  - An app can add its own files, with its own classes.

## Adding a style file

- `SetStyleFiles` sets the list of style files. They load in order, after the palette.
- Keep `styles.css` first in the list.
- A relative path in the list is relative to the palette's folder.
- Call it before `Initialize`.

```cpp
// The library's rules first, then the app's (relative paths are next to the palette).
const std::string_view styleFiles[] = { "styles.css", TACHYON_EXAMPLE_DIR "/custom_theme.css" };
SetStyleFiles(styleFiles);
if (tachyon::Result<void> result = GUIApplication::Initialize(options); !result)
    return result;
```

## The app's classes

- Style files load after the palette, so they can use its variables, such as
  `var(--accent)`.
- A class that uses them changes with the palette.

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

A class can define its own states, such as how it looks under the mouse or while pressed:

```css
.warning-button:hover  { background-color: #7a5018; }
.warning-button:active { background-color: #a06a20; }
```

## Using the classes

- `theme.GetStyle(name)` looks up a class by name and returns its style.
- Call it in `OnGUI`, every frame:
  - don't store the result
  - a theme reload registers the styles again, so a stored one goes stale
- Pass the style to a control that takes one.

```cpp
// Look the classes up every frame: a theme reload registers new styles.
Text("My themed app", theme.GetStyle("banner"));
```

```cpp
Button("Careful!", [] {}, theme.GetStyle("warning-button"));
```

## Switching palettes and font size

- `RequestReloadTheme(palette, fontScale)` loads a palette with all font sizes multiplied
  by `fontScale`.
- The reload happens at the start of the next frame, so it is safe to call from a button
  callback.
- `GetThemePath()` and `GetGlobalFontScale()` return the current palette and font scale,
  to change just one of them.

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

- If a reload fails, the previous theme stays and `ThemeReloadResult()` gives the error.

  ```cpp
  if (!ThemeReloadResult())
      Text("Theme not loaded: " + ThemeReloadResult().GetError().message);
  ```

- Selectors and properties: [CSS and theming](../css.md).

---

[Previous: Docking](docking.md) | [Examples](../examples.md) | [Next: Host device](host-device.md)
