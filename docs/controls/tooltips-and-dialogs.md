# Tooltips and message dialogs

## Tooltips

A tooltip is a small box of text that appears next to a control when the pointer rests on
it, to say what the control does. The control it belongs to is its target.

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

- `SetTooltip` shows text, and an optional icon.
  - It shows when the pointer rests on the target.
  - It shows at once when Tab moves the keyboard focus to the target.
- `shortcut` is the key that does the same thing, shown in a small box after the text.
  - `CommandButton` fills it in from its command.
- A `GUIContent` with a tooltip also sets one.
- No tooltip shows while a mouse button is held down. A press, a key, scrolling or an
  open menu hides it.

```cpp
Button(saveIcon, [this] { Save(); });
SetTooltip("Save scene", "Ctrl+S");
```

```cpp
Button({ saveIcon, "Save", "Write the file to disk" }, [this] { Save(); });   // the third part is the tooltip
```

`BeginTooltip` is for a tooltip with any content, not only text.

- It returns `true` while the tooltip is showing. Build the content only then.
- Always call `EndTooltip`.

```cpp
Image(preview, LayoutOverride::FixedSize(48, 48));
if (BeginTooltip())
{
    Text("forest_floor.png");
    Image(preview, LayoutOverride::FixedSize(256, 256));
}
EndTooltip();
```

Without a target, the tooltip belongs to the control just drawn:

- its outer element
- the parts drawn after it, such as a checkbox's label or a slider's value
- but not a popup it opened
- Controls made of several parts, such as a color field or a breadcrumb, show it over
  every part.

When the hovered element and a control around it both have a tooltip, the one declared
first wins. So a control's tooltip beats the tooltip of the row it's in.

```cpp
Button(gridIcon, [this] { showGrid = !showGrid; });
SetTooltip(showGrid ? "Grid on" : "Grid off");
```

Where it appears:

- below the target, or above it if there's no room below
- kept on screen, with its arrow still pointing at the target's text
- lined up sideways with the target's text:
  - start-aligned text: at the text's start
  - end-aligned text: at the text's end
  - centered text, no text, or a target on a canvas that is panned or zoomed: centered

It fades in over 100 ms and out over 80 ms, but not with reduced motion.

How long the tooltips wait is set on `tooltipManager`, a member of `GUIApplication`:

| `TooltipManager` (`tooltipManager`) | Default | Meaning |
| --- | --- | --- |
| `delaySeconds` | 0.5 | Rest time before a tooltip shows |
| `warmSeconds` | 0.5 | Moving to another target within this time shows its tooltip at once |
| `hideSeconds` | 0.1 | Time a tooltip stays after the pointer leaves, unless the next target has one |

```cpp
tooltipManager.delaySeconds = 0.25f;   // show tooltips sooner
```

The CSS classes:

- `tooltip-frame` for the see-through window that holds it
- `tooltip` for the box
- `tooltip-text`
- `tooltip-shortcut`

The colors: `--tooltip-bg`, `--tooltip-border`, `--tooltip-text`, `--tooltip-chip`,
`--tooltip-chip-text` and `--shadow-tooltip`.

### Property help

Property help is a larger tooltip that explains a setting, such as a field in an
inspector. A `PropertyHelp` has three parts, and empty ones are left out:

- `title`
- `description`
- `footer`, for the property's API path and type. It's in a monospace font, under a line.

It shows in a 268 px wide tooltip, on the popup surface.

A `Label` or `DragLabel` with help gets a dotted underline, unless you gave it your own
style.

- The underline sits just below the text.
- A label alone on its line is only as tall as its text, so the underline is cut off.
- In a row with a control, it shows.

The CSS classes:

- `tooltip-rich`, `tooltip-title`, `tooltip-description` and `tooltip-footer` for the
  tooltip
- `label-help`, `group-label-help`, `drag-label-help` and `group-drag-label-help` for the
  labels
- The shadow color is `--shadow-popup`.

```cpp
Label("Cast shadows");
SetTooltip(PropertyHelp{ "Cast shadows",
                         "Whether this mesh casts shadows from directional, spot and point lights.",
                         "MeshRenderer.castShadows (bool)" });
Checkbox(castShadows);
```

### A value that isn't accepted

`SetErrorTooltip` says why a value is rejected.

- It shows on the popup surface, with a 1 px border in the error color.
- Pair it with `BeginInvalid`, which gives the control an error border and a glow around
  it.
- Text and number fields also get an error icon at their end.
- The CSS classes: `tooltip-error` and `tooltip-error-text`.

```cpp
BeginInvalid();
Field(opacity);
SetErrorTooltip("Value must be between 0 and 1");
EndInvalid();
```

### A disabled control

- A disabled control still shows its tooltip.
  - Under the pointer it isn't hovered: it's `InteractionState::GetHoveredDisabledID`.
- `SetDisabledTooltip` adds the reason it's disabled, on a second, muted line.
  - When the control is enabled, it's an ordinary tooltip.
- A tooltip declared inside `BeginDisabled` isn't itself disabled. The same goes for
  `BeginSelected` and `BeginInvalid`.
- The CSS classes: `tooltip-lines` for the box with several lines, `tooltip-note` for the
  muted line.
- The muted line's color is `--tooltip-muted`.

```cpp
BeginDisabled();
Button("Publish", [this] { Publish(); });
SetDisabledTooltip("Publish to asset store", "Sign in to enable publishing");
EndDisabled();
```

Node pins have their own tooltips: see [Node view](node-view.md).

### Text cut short

When text doesn't fit and is cut short, hovering it shows the whole text. This is
automatic: there's nothing to declare.

- The text can be cut by its own ellipsis, or by a box around it, such as a narrow row.
- The full text shows right over the cut text, in the same font, position and alignment.
  - An icon beside the text is included.
  - Under or over an icon, like a grid item's name, it's just the text's line, without
    the icon.
- It works for the part under the pointer, and for the focused control after Tab.
- The target's own tooltip takes precedence.
- It's not shown for:
  - text in fields
  - wrapping text
  - text out of sight
  - text on a canvas that is panned or zoomed
- The CSS classes:
  - `tooltip-expand` for the box: tooltip colors, 4 px radius, 6 px on each side, no arrow
  - `tooltip-expand-text`, for the text color only


## Message dialogs

### MessageDialog

A message dialog asks the user a question, with a button for each answer, such as
"Cancel" and "Quit".

```cpp
int MessageDialog(ID id, const MessageDialogDesc& desc, StyleRef style);
```

- It's centered, and modal: the user can't use the rest of the app until they answer.
- Call it every frame for as long as you're asking.
- It returns the index of the chosen button once, in the frame after the click. Otherwise
  it returns -1.

Keyboard:

- Esc and the close button choose `cancelButton`.
- Alt plus a button's first letter chooses that button.
- The default button starts with the focus. Tab cycles through the dialog.

| `MessageDialogDesc` field | Meaning |
| --- | --- |
| `icon` | `MessageIcon::Warning`, `Quit`, `Question`, `Info` or `Error` |
| `title` | The question |
| `message` | One or two sentences on what happens; a long one wraps beside the icon |
| `buttons` | Span of `MessageButton`: `label`, `kind` (`Secondary`, `Primary`, `Danger`), `alignLeft` |
| `defaultButton` | |
| `cancelButton` | -1 means the default button |
| `items` | Affected items, each with an optional color dot, note and checkbox |
| `dontAskAgain` | `bool*`; when set, the dialog shows the checkbox |
| `details` | Expandable technical text |

The CSS classes:

- `message-box`, and `message-box-*` for its parts
- `dialog-button`, `dialog-button-primary` and `dialog-button-danger` for the buttons

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

A dialog that lists the affected files and offers "Don't ask again":

```cpp
// members: bool asking; bool dontAskAgain; bool saveScene = true
const MessageDialogItem affected[] = {
    { .name = "Forest.level", .meta = "12 edits", .checked = &saveScene },
};
static constexpr MessageButton kSaveButtons[] = {
    { .label = "Don't save", .alignLeft = true },
    { .label = "Cancel" },
    { .label = "Save", .kind = MessageButtonKind::Primary },
};
const int answer = MessageDialog(MessageDialogDesc{
    .icon = MessageIcon::Warning, .title = "Save changes before closing?",
    .buttons = kSaveButtons, .defaultButton = 2, .cancelButton = 1,
    .items = affected, .dontAskAgain = &dontAskAgain });
if (answer >= 0) asking = false;
```

The color picker and gradient editor dialogs open from `ColorField` and `GradientField`.

### Toasts

A toast is a short message that pops up in a corner of the window and goes away by
itself, such as "Build succeeded". It doesn't interrupt the user.

```cpp
ToastID    ShowToast(ToastDesc desc);
ToastDesc* FindToast(ToastID toast);
void       DismissToast(ToastID toast);
void       Toasts();
```

- `ShowToast` adds a toast.
- Call `Toasts()` last in a window. It draws the toasts:
  - in the bottom-right corner, 16 px from the edges
  - over the content
  - with the newest nearest the corner
- Past four toasts, the older ones fold into a "+N earlier" pill.
- How long a toast stays:
  - An error toast stays until it's dismissed, by its close button or by `DismissToast`.
  - Others go after about 6 s, counted only while the pointer isn't over them.
  - A toast with a `progress` below 1 stays.
- `FindToast` returns the toast's `ToastDesc`, to update its progress, body or title. It
  returns null once the toast is gone.
- Clicking an action runs it, and dismisses the toast.
- The body wraps. The title is one line, cut short with an ellipsis, with the full text in
  a tooltip.
- Set `age` to show how long ago it happened ("4 min").

| `ToastDesc` field | Meaning |
| --- | --- |
| `severity` | `Severity::Info`, `Success`, `Warning`, `Error`: icon and circle color |
| `title`, `body` | |
| `monospaceBody` | For code or file locations |
| `progress` | Optional bar, 0 to 1 |
| `actions` | `ToastAction` list (`label`, `onClick`); the first uses the link color |

The CSS classes:

- `toast-stack`
- `toast`
- `toast-circle-<severity>`
- `toast-title`, `toast-time`, `toast-body` and `toast-body-mono`
- `toast-progress` and `toast-progress-fill`
- `toast-action` and `toast-action-secondary`
- `toast-dismiss`
- `toast-earlier`

```cpp
ShowToast(ToastDesc{ .severity = Severity::Error, .title = "Shader compile failed",
                     .body = "Terrain.shader:118", .monospaceBody = true,
                     .actions = { { "Show log", [this] { ShowLog(); } } } });
```

A toast with a progress bar, updated every frame, and drawn last in the window:

```cpp
// Once: member ToastID uploadToast
uploadToast = ShowToast(ToastDesc{ .title = "Uploading build", .progress = 0.0f });

// Each frame, inside the window:
if (ToastDesc* upload = FindToast(uploadToast))
{
    upload->progress = uploadDone;   // member: float uploadDone, 0 to 1
    if (uploadDone >= 1.0f)
        upload->title = "Uploaded build";
}
Toasts();
```

```cpp
Button("Cancel upload", [this] { DismissToast(uploadToast); });
```

### Banner

A banner is a message across the top of a panel, tinted in the color of its severity.

```cpp
void Banner(ID id, Severity severity, const GUIContent& message, std::span<const BannerAction> actions = {},
            LayoutOverride&& constraints = {});
```

- Its icon is `message.icon`, or else the severity's icon.
- Its actions sit on the right. Each is one of:
  - `BannerActionKind::Primary`: an accent button
  - `Secondary`: a plain button
  - `Link`
- The CSS classes:
  - `banner-<severity>`
  - `banner-message-<severity>`
  - `banner-primary`, `banner-secondary` and `banner-link`

```cpp
const BannerAction actions[] = {
    { "Discard", [this] { Discard(); } },
    { "Save",    [this] { Save(); }, BannerActionKind::Primary },
};
Banner(idManager.AllocateDynamicID("unsaved"), Severity::Warning, "Scene has unsaved changes", actions);
```

```cpp
const BannerAction links[] = { { "Learn more", [this] { showHelp = true; }, BannerActionKind::Link } };
Banner(Severity::Info, "This project uses an older file format.", links);
```

---

[Previous: Menus](menus.md) | [Controls](../controls.md) | [Next: Lists and reorderable groups](lists.md)
