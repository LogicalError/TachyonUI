# Default CSS classes

This is the class each control uses when you don't pass a style. Parts of a control,
such as a switch's knob or a table's header, have classes of their own, which are listed
on that control's page.

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
| `Button`, `CommandButton` | `button` |
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
| `SetTooltip`, `BeginTooltip` | Window `tooltip-frame`, box `tooltip`, text `tooltip-text`, shortcut `tooltip-shortcut`. Property help uses `tooltip-rich` (with `tooltip-title`, `tooltip-description` and `tooltip-footer`), and its label uses `label-help` (or `group-label-help`, `drag-label-help`, `group-drag-label-help`). Text that was cut short and is shown whole over itself uses `tooltip-expand` (text `tooltip-expand-text`). A value that isn't accepted uses `tooltip-error` (`tooltip-error-text`). A few lines, such as a disabled control's reason or a node pin's tooltip, use `tooltip-lines`, with `tooltip-note` for the muted line and `tooltip-pin-row` (`tooltip-pin-type`, `tooltip-pin-value`) for a pin's first line. |
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
| `CommandSearch` | `command-search` |
| `RenderSettingsPanel` | `settings-page` |
| `RenderNodeView` | `node-view` |
| Links in rich text (`theme.linkStyle`) | `link` (`color`, `:hover` `color`, `cursor`) |

The classes are defined in `workdirectory/themes/styles.css`, and their colors come from
the palette in `dark.css` or `light.css`. [CSS and theming](../css.md) shows how to change
them or add your own.

---

[Previous: Custom drawing](custom-drawing.md) | [Controls](../controls.md)
