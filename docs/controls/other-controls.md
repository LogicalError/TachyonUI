# Other controls

## Breadcrumb

Shows a path such as Graphs > Terrain material, where the last segment is the current
one. Clicking any other segment calls `onSelect(index)`, and `Breadcrumb` returns `true`
that frame.

```cpp
bool Breadcrumb(ID id, std::span<const std::string> segments, std::function<void(int)> onSelect, StyleRef style,
                LayoutOverride&& constraints = {});
```

It uses the classes `breadcrumb`, `breadcrumb-segment`, `breadcrumb-current` and
`breadcrumb-separator`.

## Status bar

A strip of short status texts, usually along the bottom edge of a window. To push some
texts to the right, give the `StatusText` before them `FlexGrow(1)`.

```cpp
bool BeginStatusBar(ID id, StyleRef style, LayoutOverride&& constraints = {});
void EndStatusBar();
void StatusText(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
```

It uses the classes `status-bar` and `status-text`.

```cpp
BeginStatusBar();
StatusText("Ready", LayoutOverride::Auto().FlexGrow(1));
StatusText(frameTimeText);
EndStatusBar();
```

## CommandSearch

A search field that finds and runs the commands in `commandManager`. While it has focus,
it lists the commands whose description contains what you typed. Up and Down move
through the list, Enter or a click runs a command, and Escape leaves the field.

`state` has to outlive the frame. `CommandSearch` returns `true` when a command has run
since the last call. `FocusCommandSearch(state)` gives it the keyboard on the next frame,
which is handy to bind to a shortcut such as Ctrl+K.

```cpp
bool CommandSearch(ID id, CommandSearchState& state, StyleRef style, LayoutOverride&& constraints = {});
static void FocusCommandSearch(CommandSearchState& state);
```

It uses the classes `command-search`, `command-search-hint`, `command-search-key`,
`command-search-list` and `command-search-current-row`.

## Settings panel

A settings panel in two panes: a tree of pages on one side, then a sizer, then the
content of the selected page. It renders into the current container. Add pages with
stable IDs, and each page's `render` function builds its content.

```cpp
void RenderSettingsPanel(SettingsPanelState& state);
ID   SettingsPanelState::AddPage(ID id, std::string_view name, std::function<void()> render);
```

It uses the classes `settings-page` and `settings-page-selection`.

---

[Previous: Node view](node-view.md) | [Controls](../controls.md) | [Next: Custom drawing](custom-drawing.md)
