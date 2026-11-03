# Other controls

## Breadcrumb

Path such as Graphs > Terrain material; the last segment is current. Clicking another
segment calls `onSelect(index)` and returns `true` that frame.

```cpp
bool Breadcrumb(ID id, std::span<const std::string> segments, std::function<void(int)> onSelect, StyleRef style,
                LayoutOverride&& constraints = {});
```

Classes: `breadcrumb`, `breadcrumb-segment`, `breadcrumb-current`, `breadcrumb-separator`.

## Status bar

Strip of short texts. To push texts to the right, give the `StatusText` before them
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

Search field that finds and runs `commandManager` commands. While focused it lists
commands whose description contains the typed text. Up / Down move, Enter or click runs,
Escape leaves.

```cpp
bool CommandSearch(ID id, CommandSearchState& state, StyleRef style, LayoutOverride&& constraints = {});
static void FocusCommandSearch(CommandSearchState& state);
```

- `state` must outlive the frame.
- Returns `true` when a command ran since the last call.
- `FocusCommandSearch(state)` focuses it next frame (e.g. bind to Ctrl+K).

Classes: `command-search`, `command-search-hint`, `command-search-key`,
`command-search-list`, `command-search-current-row`.

## Settings panel

Two panes: page tree, sizer, selected page's content. Renders into the current container.
Add pages with stable IDs; each page's `render` builds its content.

```cpp
void RenderSettingsPanel(SettingsPanelState& state);
ID   SettingsPanelState::AddPage(ID id, std::string_view name, std::function<void()> render);
```

Classes: `settings-page`, `settings-page-selection`.

---

[Previous: Node view](node-view.md) | [Controls](../controls.md) | [Next: Custom drawing](custom-drawing.md)
