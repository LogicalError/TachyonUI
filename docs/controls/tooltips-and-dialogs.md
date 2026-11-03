# Tooltips and message dialogs

## Tooltips

A tooltip is a small box with a soft shadow and an arrow pointing at its target. It sits
below the target, or above it when there's no room below. Its horizontal position follows
the target's text:

- If the text is aligned to its start (a label), the tooltip's text starts where the
  target's text starts, and the arrow sits a little way in.
- If the text is aligned to its end, the tooltip's text ends where the target's text ends,
  and the arrow sits a little way in from that end.
- If the text is centered (a button), the box is centered on the text with the arrow in
  the middle.
- If there's no text (an icon, a node's pin), or the target is under a canvas's pan and
  zoom, the box is centered under the target with the arrow in the middle.

The tooltip stays on the screen. If it would reach past an edge, it's moved along, and the
arrow still points at the text. It fades in over 100 ms and out over 80 ms, unless reduced
motion is on. The tooltip's window is transparent around the box to leave room for the
arrow and the shadow, and that margin may reach past the edge of the screen, so the box
itself can sit right at the edge.

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

`SetTooltip` shows text (and optionally an icon) when the pointer rests on the target, or
straight away when Tab moves the focus to it. A `shortcut` is the key combination that does
the same thing, and it's shown in a chip after the text. `CommandButton` fills this in from
its command. For any other content, use `BeginTooltip`. It returns `true` while the tooltip
is showing, and you call `EndTooltip` either way. A `GUIContent` with a tooltip sets one
too.

The CSS classes are `tooltip-frame` (the transparent window), `tooltip` (the box),
`tooltip-text` and `tooltip-shortcut`. The colors are `--tooltip-bg`, `--tooltip-border`,
`--tooltip-text`, `--tooltip-chip`, `--tooltip-chip-text` and `--shadow-tooltip`.

You can adjust the timing on `TooltipManager` (`tooltipManager`):

- `delaySeconds` (0.5) is how long the pointer has to rest on a target before its tooltip
  shows.
- `warmSeconds` (0.5): if you move to another target within this time of a tooltip
  showing, the next tooltip shows at once.
- `hideSeconds` (0.1) is how long a tooltip stays after the pointer leaves its target,
  unless the next target has a tooltip of its own.

No tooltip shows while a mouse button is held down (when dragging over targets, say). A
press, a key, a scroll or an open menu hides it.

```cpp
Button(saveIcon, [this] { Save(); });
SetTooltip("Save scene", "Ctrl+S");
```

Without a target, the tooltip goes on the control you just drew. That covers its outer
element and any parts it drew after it (a checkbox's label, a slider's value), but not a
popup it opened. When a control is made of several parts (a color field's swatch, hex and
alpha fields, or a breadcrumb's segments), it takes on their states, so its tooltip shows
over each of them. If the hovered element and a control around it both have a tooltip, the
one declared first wins. So a control's tooltip, set right after the control, shows instead
of its row's.

```cpp
Button(gridIcon, [this] { showGrid = !showGrid; });
SetTooltip(showGrid ? "Grid on" : "Grid off");
```

### Property help

A `PropertyHelp` (`title`, `description`, `footer`) shows in a larger tooltip on the popup
surface, 268 px wide. The title comes first, then the description wrapping under it. The
footer goes under a line in a monospace font, and says where the property lives in the API
and what type it is. Empty parts are left out.

A `Label` or `DragLabel` that has help gets a dotted underline, so the user can see there's
help available. A label that uses your own style is left as it is. The underline sits just
under the text, so a label alone on a line, only as tall as its text, cuts it off. In a row,
the label has the row's height and the underline shows.

The CSS classes are `tooltip-rich`, `tooltip-title`, `tooltip-description` and
`tooltip-footer`. The labels use `label-help`, `group-label-help`, `drag-label-help` and
`group-drag-label-help`, and the shadow is `--shadow-popup`.

```cpp
Label("Cast shadows");
SetTooltip(PropertyHelp{ "Cast shadows",
                         "Whether this mesh casts shadows from directional, spot and point lights.",
                         "MeshRenderer.castShadows (bool)" });
Checkbox(castShadows);
```

### A value that isn't accepted

`SetErrorTooltip` tells the user why a value isn't accepted. It's shown on the popup surface
with a 1 px error border, arrow included. Use it together with `BeginInvalid`, which gives
the control its error border and halo. A field (a text or number field) also gets an error
icon at its end, and its text stops short of the icon. The CSS classes are `tooltip-error`
and `tooltip-error-text`.

```cpp
BeginInvalid();
Field(opacity);
SetErrorTooltip("Value must be between 0 and 1");
EndInvalid();
```

### A disabled control

A disabled control still shows its tooltip, even though it takes no input. When the pointer
is over it, it's the tooltip's target (`InteractionState::GetHoveredDisabledID`) rather than
being hovered. Tooltips on disabled elements around it show too, the same as for an enabled
control's parts.

`SetDisabledTooltip` explains why the control is disabled, on a second, muted line. When
the control is enabled, it's an ordinary tooltip. A tooltip declared inside `BeginDisabled`
(or `BeginSelected`, `BeginInvalid`) isn't disabled itself. The CSS classes are
`tooltip-lines` (the box holding several lines) and `tooltip-note` (the muted line), and the
color is `--tooltip-muted`.

```cpp
BeginDisabled();
Button("Publish", [this] { Publish(); });
SetDisabledTooltip("Publish to asset store", "Sign in to enable publishing");
EndDisabled();
```

A node's pins have tooltips of their own. See [Node view](node-view.md).

### Text cut short

When text doesn't fit, either because its own box cuts it short with an ellipsis or because
a box around it does (a tree or list row in a narrow panel), hovering it shows the whole
text. The tooltip lies right over the cut text, in the same font, at the same position and
height, so it looks as if the text just grew. You don't need to declare anything for this.
It lines up on the side the text is aligned to (its start, its end or its middle), an icon
next to the text comes along, and it stays on the screen. It works for the target's own
text, or for the part of it under the pointer (a row's name, not its type). After Tab, it
works for the focused control's text.

If the target has a tooltip of its own, that one shows instead. Nothing shows for text in a
field (it scrolls), text that wraps, text that's out of sight, or text under a canvas's pan
or zoom. It uses the same timing as other tooltips. The CSS classes are `tooltip-expand`
(the box, with the tooltip's colors, a 4 px radius, 6 px on each side and no arrow) and
`tooltip-expand-text` (the text color; the font, alignment and height come from the cut
text).


## Message dialogs

### MessageDialog

A centered, modal question with buttons named after what they do. Call it every frame for
as long as you're asking. In the frame after the user picks a button, it returns that
button's index once. Otherwise it returns -1.

Esc and the close button choose `cancelButton`. Alt plus a button's first letter chooses
that button, wherever the focus is in the dialog. The default button has the focus to start
with. Tab moves through all the dialog's controls (the close button, the checkboxes,
Details), and the focus stays inside the dialog.

```cpp
int MessageDialog(ID id, const MessageDialogDesc& desc, StyleRef style);
```

`MessageDialogDesc` has these fields:

- `icon`: `MessageIcon::Warning`, `Quit`, `Question`, `Info` or `Error`.
- `title`: the question.
- `message`.
- `buttons`: a span of `MessageButton`, each with a `label`, a `kind` (`Secondary`,
  `Primary` or `Danger`) and `alignLeft`.
- `defaultButton`.
- `cancelButton`: -1 means the default button.
- `items`: the affected items, each with an optional color dot, note and checkbox.
- `dontAskAgain`: a `bool*`. When it's set, the dialog shows the checkbox.
- `details`: technical text the user can expand.

The CSS classes are `message-box` and the `message-box-*` classes. The buttons use
`dialog-button`, `dialog-button-primary` and `dialog-button-danger`.

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

The library's other dialogs are the color picker and the gradient editor, which open from
`ColorField` and `GradientField`.

### Toasts

Toasts let you tell the user something without a dialog. `ShowToast` adds one, and
`Toasts()`, called last in a window, shows them in the window's bottom-right corner, 16 px
from the edges and on top of the window's content. The newest toast is nearest the corner.
Once there are more than four, the older ones fold into a "+N earlier" pill, which shows them
all. A toast slides in from the right (180 ms) and back out (140 ms). With
reduced motion it doesn't move.

An error toast stays until it's dismissed, with its close button or `DismissToast`. Other
toasts go after about 6 s, counted only while the pointer isn't over the toasts. A toast
whose `progress` is below 1 stays until it's done. `FindToast` returns a shown toast's
`ToastDesc` so you can change its progress, body or title, and returns null once the toast
is gone. Clicking an action runs it, and then the toast goes away.

The body wraps onto as many lines as it needs. The title stays on one line; if it's too
long for the toast, it ends in an ellipsis and the full title shows in its tooltip. Set
`age` on a toast about something that happened earlier to show when it happened ("4 min").

```cpp
ToastID    ShowToast(ToastDesc desc);
ToastDesc* FindToast(ToastID toast);
void       DismissToast(ToastID toast);
void       Toasts();
```

`ToastDesc` has these fields:

- `severity`: `Severity::Info`, `Success`, `Warning` or `Error`. It sets the icon and color
  of the toast's circle.
- `title` and `body`.
- `monospaceBody`, for code or file locations.
- `progress`, an optional bar from 0 to 1.
- `actions`, a list of `ToastAction` (`label`, `onClick`). The first one is shown in the
  link color.

The CSS classes are `toast-stack`, `toast`, `toast-circle-<severity>`, `toast-title`,
`toast-time`, `toast-body`, `toast-body-mono`, `toast-progress`, `toast-progress-fill`,
`toast-action`, `toast-action-secondary`, `toast-dismiss` and `toast-earlier`.

```cpp
ShowToast(ToastDesc{ .severity = Severity::Error, .title = "Shader compile failed",
                     .body = "Terrain.shader:118", .monospaceBody = true,
                     .actions = { { "Show log", [this] { ShowLog(); } } } });
```

### Banner

A banner is a message across the top of a panel. It's tinted in the severity's color with a
line along its bottom, and shows the message with its icon (`message.icon`, or the
severity's icon if that's not set). Actions go on the right. Each one is
`BannerActionKind::Primary` (an accent button), `Secondary` (a plain button) or `Link`.

```cpp
void Banner(ID id, Severity severity, const GUIContent& message, std::span<const BannerAction> actions = {},
            LayoutOverride&& constraints = {});
```

The CSS classes are `banner-<severity>`, `banner-message-<severity>`, `banner-primary`,
`banner-secondary` and `banner-link`.

```cpp
const BannerAction actions[] = {
    { "Discard", [this] { Discard(); } },
    { "Save",    [this] { Save(); }, BannerActionKind::Primary },
};
Banner(idManager.AllocateDynamicID("unsaved"), Severity::Warning, "Scene has unsaved changes", actions);
```

---

[Previous: Menus](menus.md) | [Controls](../controls.md) | [Next: Lists and reorderable groups](lists.md)
