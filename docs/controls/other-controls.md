# Other controls

## Breadcrumb

A path such as Graphs > Terrain material.

- The last segment is the current one.
- Clicking another segment calls `onSelect(index)`, a callback you pass, with that
  segment's index.
- It returns `true` that frame.

```cpp
bool Breadcrumb(ID id, std::span<const std::string> segments, std::function<void(int)> onSelect, StyleRef style,
                LayoutOverride&& constraints = {});
```

```cpp
Breadcrumb(path, [this](int segment) { path.resize(size_t(segment) + 1); });   // member: std::vector<std::string> path
```

```cpp
if (Breadcrumb(path, [this](int segment) { path.resize(size_t(segment) + 1); }))
    ReloadFolder();
```

Classes: `breadcrumb`, `breadcrumb-segment`, `breadcrumb-current`, `breadcrumb-separator`.

## Status bar

A strip of short texts. To push texts to the right, give the `StatusText` before them
`FlexGrow(1)`.

```cpp
bool BeginStatusBar(ID id, StyleRef style, LayoutOverride&& constraints = {});
void EndStatusBar();
void StatusText(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
```

Classes: `status-bar`, `status-text`.

```cpp
BeginStatusBar();
StatusText("Ready", LayoutOverride::Auto().FlexGrow(1));
StatusText(frameTimeText);
EndStatusBar();
```

## CommandSearch

A search field that finds and runs the commands of `commandManager`.

- While focused, it lists the commands whose description contains the typed text.
- Up / Down move through them.
- Enter or a click runs one.
- Escape leaves.

```cpp
bool CommandSearch(ID id, CommandSearchState& state, StyleRef style, LayoutOverride&& constraints = {});
static void FocusCommandSearch(CommandSearchState& state);
```

```cpp
CommandSearch(commandSearch, LayoutOverride{}.Width(300));   // member: CommandSearchState commandSearch
```

- `state` must outlive the frame.
- Returns `true` when a command ran since the last call.
- `FocusCommandSearch(state)` focuses it in the next frame, for example from a command
  bound to Ctrl+K.

```cpp
// Once, in Initialize:
cmdSearch = commandManager.RegisterCommand(searchIcon, KeyCode::KeyK, KeyModifier::Control,
                                           [this] { FocusCommandSearch(commandSearch); }, "Search Commands");
```

Classes: `command-search`, `command-search-hint`, `command-search-key`,
`command-search-list`, `command-search-current-row`.

## Settings panel

Two panes: a tree of pages and the selected page's content, with a sizer between them.

- It renders into the current container.
- Add pages with stable IDs: IDs made once with `idManager.CreateStaticID()`.
- Each page's `render` function builds its content.

```cpp
void RenderSettingsPanel(SettingsPanelState& state);
ID   SettingsPanelState::AddPage(ID id, std::string_view name, std::function<void()> render);
```

```cpp
if (settings.pages.empty())   // member: SettingsPanelState settings
{
    settings.AddPage(idManager.CreateStaticID(), "General",    [this] { Switch(autosave, "Autosave"); });
    settings.AddPage(idManager.CreateStaticID(), "Appearance", [this] { Dropdown(themeIndex, { "Dark", "Light" }); });
}
RenderSettingsPanel(settings);
```

Classes: `settings-page`, `settings-page-selection`.

---

[Previous: Node view](node-view.md) | [Controls](../controls.md) | [Next: Custom drawing](custom-drawing.md)
