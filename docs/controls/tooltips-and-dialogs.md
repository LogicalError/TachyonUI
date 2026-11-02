# Tooltips and message dialogs

## Tooltips

A tooltip is a small box below its target, or above it when there's no room below, lined
up with the target's left side (its right side at the right edge of the screen), with an
arrow toward the target near that end, and a soft shadow. It fades in over 100 ms and out
over 80 ms (not with reduced motion). Its window is see-through around the box, room for
the arrow and the shadow that may reach past the edge of the screen, so the box itself
lines up with the target there too.

```cpp
void SetTooltip(ID targetID, std::string_view text);
void SetTooltip(ID targetID, IconID icon, std::string_view text);
void SetTooltip(ID targetID, std::string_view text, std::string_view shortcut);
void SetTooltip(ID targetID, IconID icon, std::string_view text, std::string_view shortcut);
void SetTooltip(std::string_view text);           // the control just drawn
void SetTooltip(std::string_view text, std::string_view shortcut);
void SetTooltip(ID targetID, const PropertyHelp& help);
void SetTooltip(const PropertyHelp& help);        // the control just drawn
bool BeginTooltip(ID targetID);
bool BeginTooltip();                              // the control just drawn
void EndTooltip();
```

`SetTooltip` shows a text (and icon) on hover, or right away when Tab moves the focus to
the target. A `shortcut` (the keys that do the same) goes in a chip after the text;
`CommandButton` gives its command's. `BeginTooltip` builds any content; it returns `true`
while the tooltip shows, and `EndTooltip` is called either way. A `GUIContent` with a
tooltip sets it too. Classes: `tooltip-frame` (the see-through window), `tooltip` (the box),
`tooltip-text`, `tooltip-shortcut`; colors `--tooltip-bg`, `--tooltip-border`,
`--tooltip-text`, `--tooltip-chip`, `--tooltip-chip-text`, `--shadow-tooltip`.

Timing, on `TooltipManager` (`tooltipManager`):

- `delaySeconds` (0.5): how long the pointer rests on a target before its tooltip shows.
- `warmSeconds` (0.5): moving to another target within this long of a tooltip showing
  shows the next one at once.
- `hideSeconds` (0.1): a tooltip stays this long after its target is left, unless the next
  target has one of its own.

None shows while a mouse button is held (a drag over targets), and a press, a key, a
scroll or an open menu takes it away.

```cpp
Button(saveIcon, [this] { Save(); });
SetTooltip("Save scene", "Ctrl+S");
```

Without a target, the tooltip goes on the control just drawn: its outer element, whatever
parts it drew after it (a checkbox's label, a slider's value), and not a popup it opened. A
control made of several (a color field's swatch, hex and alpha fields, a breadcrumb's
segments) takes their states, so its tooltip shows over each of them. When the hovered
element and a control around it both have one, the first declared shows: a control's,
set right after it, over its row's.

```cpp
Button(gridIcon, [this] { showGrid = !showGrid; });
SetTooltip(showGrid ? "Grid on" : "Grid off");
```

### Property help

A `PropertyHelp` (`title`, `description`, `footer`) shows in a larger tooltip on the popup
surface, 268 px wide: the title, the description wrapping under it, and the footer under a
line, in a monospace font, saying where the property lives in the API and its type. Empty
parts are left out. A `Label` or `DragLabel` given help is underlined, dotted, so it's
clear there is help to see; a label in a style of your own keeps it as it is. (The
underline sits just under the text: a label alone on a line, only as tall as its text,
cuts it off; in a row it has the row's height.) Classes: `tooltip-rich`, `tooltip-title`,
`tooltip-description`, `tooltip-footer`; the labels' `label-help`, `group-label-help`,
`drag-label-help`, `group-drag-label-help`; shadow `--shadow-popup`.

```cpp
Label("Cast shadows");
SetTooltip(PropertyHelp{ "Cast shadows",
                         "Whether this mesh casts shadows from directional, spot and point lights.",
                         "MeshRenderer.castShadows (bool)" });
Checkbox(castShadows);
```

### Text cut short

Text that doesn't fit, cut short by its own box (an ellipsis) or by one around it (a tree
or list row in a narrow panel), shows whole on hover, in a tooltip lying right over it: its
text exactly where the cut text is, in the same font and at the same height, so it reads as
the text growing. It comes by itself; nothing to declare. It lines up on the side the text
is aligned to (its start, its end or its middle), an icon beside the text shows with it,
and it stays on the screen. It is for the target's own text, or for the part of it under
the pointer (a row's name, not its type); after Tab, for the focused control's.

A tooltip of the target's own shows instead. Nothing shows for text in a field (it
scrolls), text that wraps, text out of sight, or text under a canvas's pan or zoom. It
takes the tooltip's timing. Classes: `tooltip-expand` (the box: the tooltip's colors,
radius 4 px, 6 px each side, no arrow), `tooltip-expand-text` (its text's color; the font,
alignment and height are the cut text's).


## Message dialogs

### MessageDialog

A modal question with verb buttons, centered. Call it every frame while
asking; it returns the index of the chosen button once, in the frame after, else -1.
Esc and the close button choose `cancelButton`; Alt and a button's first letter choose
that button, wherever the focus is in the dialog. The default button has the focus first;
Tab goes through all the dialog's controls (the close button, the checkboxes, Details) and
the focus stays in the dialog.

```cpp
int MessageDialog(ID id, const MessageDialogDesc& desc, StyleRef style);
```

`MessageDialogDesc` fields: `icon` (`MessageIcon::Warning`, `Quit`, `Question`, `Info`,
`Error`), `title` (a question), `message`, `buttons` (a span of `MessageButton`:
`label`, `kind` = `Secondary`, `Primary` or `Danger`, `alignLeft`), `defaultButton`,
`cancelButton` (-1: the default one), `items` (affected items with an optional color dot,
note and checkbox), `dontAskAgain` (a `bool*`; shows the checkbox when set) and
`details` (collapsible technical text).

Classes: `message-box` and the `message-box-*` classes; the buttons use
`dialog-button`, `dialog-button-primary`, `dialog-button-danger`.

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

The color picker and gradient editor are the library's other dialogs; they open from
`ColorField` and `GradientField`.

### Toasts

Telling the user something without a dialog. `ShowToast` adds a toast; `Toasts()`, called
last in a window, shows them in its bottom-right corner, 16 px from the edges, over what the
window holds. The newest is nearest the corner; past four, the older ones fold into a "+N
earlier" pill, which shows them all. A toast slides in from the right (180 ms) and out
(140 ms), or doesn't move with reduced motion.

An error stays until it's dismissed (its close button, or `DismissToast`); the rest go after
about 6 s, counted while the pointer isn't over the toasts. A toast with a `progress` short
of 1 stays until it's done. `FindToast` returns a shown toast's `ToastDesc` to change (its
progress, body, title); null once it's gone. An action runs, then the toast goes. The body
wraps onto as many lines as it takes; the title stays on one, and one too long for the toast
ends in an ellipsis and shows all of it in its tooltip. `age` gives a toast about something
earlier the time it happened ("4 min").

```cpp
ToastID    ShowToast(ToastDesc desc);
ToastDesc* FindToast(ToastID toast);
void       DismissToast(ToastID toast);
void       Toasts();
```

`ToastDesc`: `severity` (`Severity::Info`, `Success`, `Warning`, `Error`: its circle's icon
and color), `title`, `body`, `monospaceBody` (code or file locations), `progress` (an
optional 0..1 bar), `actions` (`ToastAction`: `label`, `onClick`; the first in the link
color).

Classes: `toast-stack`, `toast`, `toast-circle-<severity>`, `toast-title`, `toast-time`,
`toast-body`, `toast-body-mono`, `toast-progress`, `toast-progress-fill`, `toast-action`,
`toast-action-secondary`, `toast-dismiss`, `toast-earlier`.

```cpp
ShowToast(ToastDesc{ .severity = Severity::Error, .title = "Shader compile failed",
                     .body = "Terrain.shader:118", .monospaceBody = true,
                     .actions = { { "Show log", [this] { ShowLog(); } } } });
```

### Banner

A message across the top of a panel: the severity's tint with a line along its bottom, the
message with its icon (`message.icon`, else the severity's), and actions on the right:
`BannerActionKind::Primary` (an accent button), `Secondary` (a button) or `Link`.

```cpp
void Banner(ID id, Severity severity, const GUIContent& message, std::span<const BannerAction> actions = {},
            LayoutOverride&& constraints = {});
```

Classes: `banner-<severity>`, `banner-message-<severity>`, `banner-primary`,
`banner-secondary`, `banner-link`.

```cpp
const BannerAction actions[] = {
    { "Discard", [this] { Discard(); } },
    { "Save",    [this] { Save(); }, BannerActionKind::Primary },
};
Banner(idManager.AllocateDynamicID("unsaved"), Severity::Warning, "Scene has unsaved changes", actions);
```

---

[← Menus](menus.md) · [Controls](../controls.md) · [Lists and reorderable groups →](lists.md)
