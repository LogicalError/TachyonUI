# Tooltips and message dialogs

## Tooltips

```cpp
void SetTooltip(ID targetID, std::string_view text);
void SetTooltip(ID targetID, IconID icon, std::string_view text);
void SetTooltip(ID targetID, std::string_view text, std::string_view shortcut);
void SetTooltip(ID targetID, IconID icon, std::string_view text, std::string_view shortcut);
void SetTooltip(std::string_view text);           // the control just drawn
void SetTooltip(std::string_view text, std::string_view shortcut);
void SetTooltip(ID targetID, const PropertyHelp& help);
void SetTooltip(const PropertyHelp& help);        // the control just drawn
void SetErrorTooltip(ID targetID, std::string_view message);
void SetErrorTooltip(std::string_view message);   // the control just drawn
void SetDisabledTooltip(ID targetID, std::string_view text, std::string_view reason);
void SetDisabledTooltip(std::string_view text, std::string_view reason);
bool BeginTooltip(ID targetID);
bool BeginTooltip();                              // the control just drawn
void EndTooltip();
```

- `SetTooltip` shows text (and an optional icon) when the pointer rests on the target, or at
  once when Tab focuses it.
- `shortcut` is shown in a chip after the text. `CommandButton` fills it from its command.
- `BeginTooltip` is for custom content. It returns `true` while showing; always call
  `EndTooltip`.
- A `GUIContent` with a tooltip also sets one.
- No tooltip shows while a mouse button is held. A press, key, scroll or open menu hides it.

```cpp
Button(saveIcon, [this] { Save(); });
SetTooltip("Save scene", "Ctrl+S");
```

Without a target, the tooltip attaches to the control just drawn: its outer element and the
parts drawn after it (a checkbox's label, a slider's value), not a popup it opened.
Multi-part controls (color field, breadcrumb) show it over every part. When the hovered
element and a control around it both have a tooltip, the one declared first wins, so a
control's tooltip beats its row's.

```cpp
Button(gridIcon, [this] { showGrid = !showGrid; });
SetTooltip(showGrid ? "Grid on" : "Grid off");
```

Placement: below the target (above if no room), kept on screen with the arrow still on the
text. Horizontally it lines up with the target's text: start-aligned text at its start,
end-aligned at its end, centered text (or no text, or under a canvas's pan and zoom)
centered. Fades in 100 ms, out 80 ms (not with reduced motion).

| `TooltipManager` (`tooltipManager`) | Default | Meaning |
| --- | --- | --- |
| `delaySeconds` | 0.5 | Rest time before a tooltip shows |
| `warmSeconds` | 0.5 | Moving to another target within this time shows its tooltip at once |
| `hideSeconds` | 0.1 | Time a tooltip stays after the pointer leaves, unless the next target has one |

CSS: `tooltip-frame` (the transparent window), `tooltip` (the box), `tooltip-text`,
`tooltip-shortcut`. Colors: `--tooltip-bg`, `--tooltip-border`, `--tooltip-text`,
`--tooltip-chip`, `--tooltip-chip-text`, `--shadow-tooltip`.

### Property help

`PropertyHelp` has `title`, `description` and `footer` (empty parts are left out). It shows
in a 268 px wide tooltip on the popup surface; the footer is monospace, under a line, for
the API path and type.

A `Label` or `DragLabel` with help gets a dotted underline (not with your own style). The
underline sits just below the text, so a label alone on a line, only as tall as its text,
cuts it off; in a row it shows.

CSS: `tooltip-rich`, `tooltip-title`, `tooltip-description`, `tooltip-footer`. Labels:
`label-help`, `group-label-help`, `drag-label-help`, `group-drag-label-help`. Shadow:
`--shadow-popup`.

```cpp
Label("Cast shadows");
SetTooltip(PropertyHelp{ "Cast shadows",
                         "Whether this mesh casts shadows from directional, spot and point lights.",
                         "MeshRenderer.castShadows (bool)" });
Checkbox(castShadows);
```

### A value that isn't accepted

`SetErrorTooltip` says why a value is rejected, on the popup surface with a 1 px error
border. Pair it with `BeginInvalid` (error border and halo on the control). Text and number
fields also get an error icon at their end. CSS: `tooltip-error`, `tooltip-error-text`.

```cpp
BeginInvalid();
Field(opacity);
SetErrorTooltip("Value must be between 0 and 1");
EndInvalid();
```

### A disabled control

- A disabled control still shows its tooltip. Under the pointer it's
  `InteractionState::GetHoveredDisabledID`, not hovered.
- `SetDisabledTooltip` adds the reason on a second, muted line; when enabled it's an
  ordinary tooltip.
- A tooltip declared inside `BeginDisabled` (or `BeginSelected`, `BeginInvalid`) isn't
  itself disabled.
- CSS: `tooltip-lines` (the multi-line box), `tooltip-note` (the muted line). Color:
  `--tooltip-muted`.

```cpp
BeginDisabled();
Button("Publish", [this] { Publish(); });
SetDisabledTooltip("Publish to asset store", "Sign in to enable publishing");
EndDisabled();
```

Node pins have their own tooltips: see [Node view](node-view.md).

### Text cut short

Automatic, nothing to declare. Hovering text cut short (by its own ellipsis or by a box
around it, such as a narrow row) shows the full text right over it, same font, position
and alignment, icon included. It works for the part under the pointer, and for the focused
control after Tab.

- The target's own tooltip takes precedence.
- Not shown for text in fields, wrapping text, text out of sight, or text under a canvas's
  pan or zoom.
- CSS: `tooltip-expand` (the box: tooltip colors, 4 px radius, 6 px each side, no arrow),
  `tooltip-expand-text` (text color only).


## Message dialogs

### MessageDialog

```cpp
int MessageDialog(ID id, const MessageDialogDesc& desc, StyleRef style);
```

Centered and modal. Call it every frame while asking. Returns the chosen button's index
once, in the frame after the click; otherwise -1.

- Esc and the close button choose `cancelButton`.
- Alt + a button's first letter chooses it.
- The default button starts focused; Tab cycles inside the dialog.

| `MessageDialogDesc` field | Meaning |
| --- | --- |
| `icon` | `MessageIcon::Warning`, `Quit`, `Question`, `Info` or `Error` |
| `title` | The question |
| `message` | |
| `buttons` | Span of `MessageButton`: `label`, `kind` (`Secondary`, `Primary`, `Danger`), `alignLeft` |
| `defaultButton` | |
| `cancelButton` | -1 means the default button |
| `items` | Affected items, each with an optional color dot, note and checkbox |
| `dontAskAgain` | `bool*`; when set, the dialog shows the checkbox |
| `details` | Expandable technical text |

CSS: `message-box`, `message-box-*`; buttons `dialog-button`, `dialog-button-primary`,
`dialog-button-danger`.

```cpp
static constexpr MessageButton kButtons[] = {
    { .label = "Cancel" },
    { .label = "Quit", .kind = MessageButtonKind::Danger },
};
const int choice = MessageDialog(quitDialogID, MessageDialogDesc{
    .icon = MessageIcon::Quit, .title = "Quit?", .message = "Unsaved edits will be lost.",
    .buttons = kButtons, .defaultButton = 0, .cancelButton = 0 });
if (choice == 1) applicationQuit = true;
if (choice >= 0) asking = false;
```

The color picker and gradient editor dialogs open from `ColorField` and `GradientField`.

### Toasts

```cpp
ToastID    ShowToast(ToastDesc desc);
ToastDesc* FindToast(ToastID toast);
void       DismissToast(ToastID toast);
void       Toasts();
```

- `ShowToast` adds a toast. Call `Toasts()` last in a window; it draws them bottom-right,
  16 px from the edges, over the content, newest nearest the corner.
- Over four, older ones fold into a "+N earlier" pill.
- Error toasts stay until dismissed (close button or `DismissToast`). Others go after about
  6 s, counted only while the pointer isn't over them. A toast with `progress` below 1
  stays.
- `FindToast` returns the `ToastDesc` to update progress, body or title; null once gone.
- Clicking an action runs it and dismisses the toast.
- The body wraps; the title is one line with an ellipsis and full-text tooltip.
- Set `age` to show when it happened ("4 min").

| `ToastDesc` field | Meaning |
| --- | --- |
| `severity` | `Severity::Info`, `Success`, `Warning`, `Error`: icon and circle color |
| `title`, `body` | |
| `monospaceBody` | For code or file locations |
| `progress` | Optional bar, 0 to 1 |
| `actions` | `ToastAction` list (`label`, `onClick`); the first uses the link color |

CSS: `toast-stack`, `toast`, `toast-circle-<severity>`, `toast-title`, `toast-time`,
`toast-body`, `toast-body-mono`, `toast-progress`, `toast-progress-fill`, `toast-action`,
`toast-action-secondary`, `toast-dismiss`, `toast-earlier`.

```cpp
ShowToast(ToastDesc{ .severity = Severity::Error, .title = "Shader compile failed",
                     .body = "Terrain.shader:118", .monospaceBody = true,
                     .actions = { { "Show log", [this] { ShowLog(); } } } });
```

### Banner

```cpp
void Banner(ID id, Severity severity, const GUIContent& message, std::span<const BannerAction> actions = {},
            LayoutOverride&& constraints = {});
```

A severity-tinted message across the top of a panel. Icon: `message.icon`, else the
severity's. Actions sit on the right, each `BannerActionKind::Primary` (accent button),
`Secondary` (plain button) or `Link`.

CSS: `banner-<severity>`, `banner-message-<severity>`, `banner-primary`,
`banner-secondary`, `banner-link`.

```cpp
const BannerAction actions[] = {
    { "Discard", [this] { Discard(); } },
    { "Save",    [this] { Save(); }, BannerActionKind::Primary },
};
Banner(idManager.AllocateDynamicID("unsaved"), Severity::Warning, "Scene has unsaved changes", actions);
```

---

[Previous: Menus](menus.md) | [Controls](../controls.md) | [Next: Lists and reorderable groups](lists.md)
