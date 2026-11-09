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

## Progress and loading

Controls that show work is under way. None of them take clicks.

- A value is a fraction from 0 to 1. Values outside that range are clamped, and NaN
  counts as 0.
- The moving ones (the indeterminate bar, the spinner and the skeleton's shine) move by
  themselves. You don't need to do anything every frame.
- A [BeginCached](windows-and-layout.md#begincached--endcached) scope with a moving one
  inside is rebuilt every frame, so it keeps moving.
- For a button that shows a spinner while it works, see
  [BusyButton](buttons-and-toggles.md#busybutton).

### ProgressBar

A bar filled `value` of the way, for work whose length you know.

```cpp
void ProgressBar(ID id, float value, bool failed, StyleRef style, LayoutOverride&& constraints = {});
```

- `failed` fills it in the error color instead, for work that stopped part way.
- The theme's bar is 6px high.
  - `theme.progressBarSmallStyleRef` is 4px high, for lists and status bars.
- It fills the width it's given.
  - Give it a width when it shares a row.

```cpp
ProgressBar(float(filesDone) / float(fileCount));
```

```cpp
ProgressBar(bakeID, bakeDone, bakeFailed, theme.progressBarStyleRef);
```

```cpp
ProgressBar(indexID, indexDone, false, theme.progressBarSmallStyleRef, LayoutOverride::Auto().Width(120));
```

Classes:

- the track: `progress-bar`, or `progress-bar-small`
- the fill: `progress-fill`, or `progress-fill-failed`

### IndeterminateProgressBar

A bar for work whose length you don't know.

- A segment with soft ends slides along it from start to end every 1.2 s.
- It takes the same bar styles as ProgressBar.

```cpp
void IndeterminateProgressBar(ID id, StyleRef style, LayoutOverride&& constraints = {});
```

```cpp
Text("Compiling shaders...");
IndeterminateProgressBar();
```

Classes: `progress-bar` (the track), `progress-segment` (the sliding segment).

### Spinner

A faint ring with an arc going round it, a turn every 0.8 s.

```cpp
void Spinner(ID id, SpinnerSize size = SpinnerSize::Medium, LayoutOverride&& constraints = {});
```

- `SpinnerSize::Small`, `Medium` and `Large` are 12, 16 and 24px across.
- A width or height in `constraints` overrides the size.
  - The ring scales to fit the space it gets.
- The colors are `theme.progressTrackColor` (the ring) and `theme.progressArcColor` (the
  arc).
  - These come from `--line-subtle` and `--accent` in the theme.

```cpp
BeginHorizontal();
Spinner(SpinnerSize::Small);
Text("Loading preview");
EndHorizontal();
```

### CircularProgress

A ring that fills clockwise from the top as `value` grows, with the percentage in the
middle.

```cpp
void CircularProgress(ID id, float value, StyleRef style, LayoutOverride&& constraints = {});
```

- The percentage is rounded down, so it only shows 100 once the work is done.
- The theme's is 40px across.

```cpp
CircularProgress(uploadDone);
```

Class: `circular-progress`.

### Skeleton

A grey placeholder bar where content is still loading, with a lighter shine sweeping
across it every 1.5 s.

- Size it like the content it stands for:
  - a line of text: a width
  - an avatar: a square
- Under reduced motion it doesn't shine.

```cpp
void Skeleton(ID id, StyleRef style, LayoutOverride&& constraints = {});
```

```cpp
if (!profileLoaded)
{
    BeginHorizontal();
    Skeleton(LayoutOverride::Auto().Size(14, 14));    // the avatar
    Skeleton(LayoutOverride::Auto().Width(160));      // the name
    EndHorizontal();
}
```

Classes:

- `skeleton`: the bar, in `--skeleton`
- `skeleton-shine`: the shine, in `--skeleton-shine`

### ProgressTask

The controls below show a piece of work described by a `ProgressTask`. You fill one in
every frame from your own state.

| Field | What it's for |
| --- | --- |
| `name` | What the work is: "Importing character.fbx" |
| `value` | How much is done, 0 to 1. Below 0 when you can't tell: the bar slides instead |
| `state` | `TaskState::Pending`, `Running` (the default), `Paused`, `Failed` or `Done` |
| `note` | A short text beside the name: "2.1 s", "1,240 / 3,800". Empty shows the percent while running |
| `detail` | A line under the bar: "12.4 MB of 19.3 MB". Once failed, why it failed |
| `onCancel` | Set it to get a cancel button while the work runs or is paused |
| `onRetry` | Set it to get a Retry button once the work failed |

- The texts are only read during the call, so they can point at temporary strings.
- `Paused` draws the bar in the warning color and says "paused".

```cpp
ProgressTask task{ .name = "Exporting level", .value = exportDone, .detail = exportDetail,
                   .onCancel = [this] { exporter.Cancel(); } };
```

### TaskProgress

A piece of work in full, for a panel or a dialog.

```cpp
void TaskProgress(ID id, const ProgressTask& task, LayoutOverride&& constraints = {});
```

- Running or paused:
  - a row with the name, the percent (or the note) and a cancel button
  - the 6px bar under it
  - the detail under the bar
- Failed:
  - an error icon before the name
  - the bar in the error color
  - the reason in error text
  - a Retry button in place of the percent
- Done: one line with a check, the name and the note, such as how long it took. No bar.

```cpp
TaskProgress(ProgressTask{ .name = "Importing character.fbx", .value = importDone,
                           .detail = "12.4 MB of 19.3 MB", .onCancel = [this] { CancelImport(); } });
```

```cpp
TaskProgress(ProgressTask{ .name = "Bake failed at 73%", .value = 0.73f, .state = TaskState::Failed,
                           .detail = bakeError, .onRetry = [this] { StartBake(); } });
```

```cpp
TaskProgress(ProgressTask{ .name = "Import complete", .state = TaskState::Done, .note = "2.1 s" });
```

Classes:

- the whole: `task-progress`; its top row: `task-progress-header`
- the name: `task-progress-name`, or `task-progress-failed` / `task-progress-done` with
  their icon
- beside it: `task-progress-percent`, or `task-progress-note`
- the buttons: `task-progress-cancel`, `task-progress-retry`
- under the bar: `task-progress-detail`, or `task-progress-error` once failed

### ProgressSteps

The steps of a longer job, one per row.

```cpp
void ProgressSteps(ID id, std::span<const ProgressTask> steps, LayoutOverride&& constraints = {});
```

- Each row starts with a 16px glyph for the step's state:
  - done: a check on a green disc
  - running: a spinner
  - pending: a dashed ring (in the warning color when paused)
  - failed: a cross on a red disc
- A pending step's name is dimmed.
- The running step has a 4px bar under its name.
- The note (or the running step's percent) is at the end of the row.

```cpp
const ProgressTask steps[] = {
    { .name = "Compile scripts", .state = TaskState::Done, .note = "4.2 s" },
    { .name = "Bake lighting",   .value = 0.42f, .state = TaskState::Running },
    { .name = "Package assets",  .state = TaskState::Pending },
};
ProgressSteps(steps);
```

Classes: `progress-steps`, `progress-step`, `progress-step-body`, `progress-step-name`,
`progress-step-pending`, and the glyphs `progress-step-done`, `progress-step-failed`,
`progress-step-waiting`, `progress-step-paused`.

### StatusProgress

A piece of work in a [status bar](#status-bar): a 12px spinner while it runs, its name, a
120px bar and its note or percent.

```cpp
void StatusProgress(ID id, const ProgressTask& task, LayoutOverride&& constraints = {});
```

```cpp
BeginStatusBar();
StatusProgress(ProgressTask{ .name = "Indexing assets", .value = indexed, .note = indexCount });
EndStatusBar();
```

Classes: `status-progress`, `status-text` (the name), `status-text-mono` (the note).

### BackgroundTasks

A pill such as "3 tasks", for a status bar, that opens a popover listing the work running
in the background.

```cpp
bool BackgroundTasks(ID id, std::span<const ProgressTask> tasks, LayoutOverride&& constraints = {});
```

- A click on the pill opens the popover above it, and another click closes it.
  - So does a click anywhere else, or Escape.
- Each row shows the task's name, a 4px bar, its percent or state, and a cancel button
  when `onCancel` is set.
- It shows nothing while `tasks` is empty.
- It returns whether the popover is open.

```cpp
BeginStatusBar();
StatusText("Ready", LayoutOverride::Auto().FlexGrow(1));
BackgroundTasks(backgroundTasksID, backgroundTasks);   // member: std::vector<ProgressTask>
EndStatusBar();
```

Classes: `tasks-pill`, `tasks-popover`, `tasks-popover-header`, `tasks-popover-title`,
`tasks-popover-count`, `tasks-row`, `tasks-row-body`, `tasks-row-state`.

### Moving parts in your own controls

A control you write that moves every frame needs `ElementFlags::Animated` on its element.

- Without it, a `BeginCached` scope around it keeps the first frame's drawing.
- Read the time from `guiStateManager.Interaction().GetFrameTime()`.

```cpp
const ElementRef ref = RegisterElement(Element{ .id = id, .styleRef = style, .flags = ElementFlags::Animated },
                                       std::move(constraints));
const float seconds = guiStateManager.Interaction().GetFrameTime();
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

[Previous: Timeline](timeline.md) | [Controls](../controls.md) | [Next: Custom drawing](custom-drawing.md)
