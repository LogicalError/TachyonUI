# Default CSS classes

A control's look comes from a CSS class of the loaded theme. Each control takes an
optional `StyleRef`, a handle to a class that you get from `theme.GetStyle`. When you pass
no style, the control uses the class in the table below.

```cpp
Button("Save", onSave);                                         // class "button"
Button("Delete", onDelete, theme.GetStyle("my-danger-button"));  // your own class instead
```

- This table lists the class of each control's outer element.
- The classes of its parts, such as a switch's knob or a table's header, are on the
  control's page.

| Control | Class |
| --- | --- |
| `BeginWindow` | `window` |
| `BeginGhostWindow`, `AddGhostIcon` | `ghost-window`, `ghost-icon` |
| `BeginHorizontal`, `BeginVertical` and Wrap variants | none (plain flex box built in code) |
| `BeginArea`, `BeginCached` | `collection` |
| `BeginButtonGroup`, `BeginToggleGroup` | `button-group`, `toggle-group`; items `group-button`, `group-toggle` (`button`, `toggle` when alone) |
| `Text` | `static-text` |
| `Label`, `DragLabel` | `label`, `drag-label` (`group-label`, `group-drag-label` in a resizable label group) |
| `Image` | none (built in code) |
| `Button`, `CommandButton`, `BusyButton` | `button` |
| `Toggle`, `Field(bool&)` | `toggle` |
| `Switch` | `switch` |
| `Checkbox` | `checkbox` |
| `Field(std::string&)` | `textbox` |
| `SearchField` | `search-textbox` |
| `Dropdown` | `menu-trigger`, popup `menu` |
| `AutocompleteField` | `textbox`; list `command-search-list`, rows `menu-item-row`, highlighted row `command-search-current-row` |
| `Field` (numbers) | `number-field` |
| `Field` (vectors) | `vector-component`, `axis-label-x` / `-y` / `-z` / `-w` |
| `TypedField` | `typed-field` |
| `Field` (lists, dictionaries) | `collection-area` |
| `SliderField` | `slider-field-track` |
| `Slider` | `slider` |
| `ColorField` | `color-field` (picker: `color-picker`) |
| `GradientField` | `gradient-swatch` (editor: `gradient-editor`) |
| `BeginMenu` | `menu-trigger`, popup `menu` |
| `MenuItem` | `menu-item-row` |
| `MenuSeparator`, `MenuHeader` | `menu-separator`, `menu-header` |
| `BeginMenuBar`, `BeginTitleBar` | `menu-bar`, `title-bar` |
| `BeginContextMenu` | `menu` |
| `SetTooltip`, `BeginTooltip` | `tooltip-frame`, `tooltip`, `tooltip-text`, and more; see [Tooltip classes](#tooltip-classes) |
| `MessageDialog` | `message-box` |
| `Toasts` | `toast-stack`, `toast`, `toast-circle-<severity>` |
| `Banner` | `banner-<severity>`, `banner-message-<severity>` |
| `BeginListView`, `ListItem` | `list-view`, `list-item` |
| `BeginReorderableGroup` | `horzReorderableGroup`, `vertReorderableGroup` (`...Wrap` when wrapped) |
| `BeginReorderableItem` | `reorderableItem` |
| `BeginTable`, `VirtualTable` | `table` (cells: `table-cell`) |
| `VirtualTreeView` | `tree-view` |
| `BeginGridView`, `GridViewItem` | `grid-view`, `grid-view-item` |
| `VirtualGridView` | `grid-view-scroll`, `grid-view-row` |
| `BeginTabBar`, `Tab` | `tab-bar`, `tab`, `tab-overflow` |
| `BeginDockingContainer` | `dock-container` |
| `Documents` | `documents`, `document-bar`, `document-tab-<kind>`, `document-name-<kind>`, `document-line-<kind>` (plain: `document-tab`, `document-name`, `document-line`) |
| `BeginToolbar` | controls inside use `toolbar-*` variants |
| `Breadcrumb` | `breadcrumb` |
| `BeginStatusBar`, `StatusText` | `status-bar`, `status-text` |
| `ProgressBar`, `IndeterminateProgressBar` | `progress-bar` (small: `progress-bar-small`) |
| `Spinner` | none (drawn in code, in the theme's track and accent colors) |
| `CircularProgress` | `circular-progress` |
| `Skeleton` | `skeleton` |
| `TaskProgress` | `task-progress` |
| `ProgressSteps` | `progress-steps` |
| `StatusProgress` | `status-progress` |
| `BackgroundTasks` | `tasks-pill`, popover `tasks-popover` |
| `CommandSearch` | `command-search` |
| `RenderSettingsPanel` | `settings-page` |
| `RenderNodeView` | `node-view`; frames `node-frame`, `node-frame-title` |
| `Timeline` | `timeline`; see [Timeline](timeline.md) |
| `TimelineEditor` | `timeline-editor`; see [Timeline](timeline.md) |
| Links in rich text (`theme.linkStyle`) | `link` (`color`, `:hover` `color`, `cursor`) |

## Tooltip classes

- A tooltip:
  - the window: `tooltip-frame`
  - the box: `tooltip`
  - the text: `tooltip-text`
  - the shortcut: `tooltip-shortcut`
- Property help:
  - the tooltip: `tooltip-rich`, with `tooltip-title`, `tooltip-description` and
    `tooltip-footer`
  - its label: `label-help`
  - its label in a label group: `group-label-help`
  - its drag label: `drag-label-help`
  - its drag label in a label group: `group-drag-label-help`
- Truncated text shown whole: `tooltip-expand`, with `tooltip-expand-text`.
- A rejected value: `tooltip-error`, with `tooltip-error-text`.
- Short lines, such as a disabled reason or a node pin:
  - the tooltip: `tooltip-lines`
  - a muted line: `tooltip-note`
  - a pin's first line: `tooltip-pin-row`, with `tooltip-pin-type` and `tooltip-pin-value`

## Where the classes are defined

- The classes are defined in `workdirectory/themes/styles.css`.
- Their colors come from the palette in `dark.css` or `light.css`.
- See [CSS and theming](../css.md).

---

[Previous: Custom drawing](custom-drawing.md) | [Controls](../controls.md)
