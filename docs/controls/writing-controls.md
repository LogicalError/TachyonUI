# Writing a control

What every control must do, and what to decide for optional features. Applies to library
controls and your own. Builds on [Common conventions](conventions.md).

## Must have

**Identity**

- [ ] Overloads with an `ID` and with the call site as ID (`std::source_location`), each
      with and without a `StyleRef`. The ID + style overload does the work. See
      [Common conventions](conventions.md).
- [ ] Parts (icon, caption, surrounding box) get IDs under the control's with
      `idManager.PushParent(id)`, so they don't depend on what was drawn before.
- [ ] Register with `RegisterControl`, not `RegisterElement` (marks it a control, adds it to
      Tab order, reports an invalid ID). Non-control parts (captions, icons, tracks) use
      `RegisterElement` with `ElementFlags::NoInput` so the mouse reaches the control.

**Keyboard**

- [ ] Tab and Shift+Tab reach it in draw order, unless disabled.
- [ ] Everything the mouse does works from the keyboard when focused: Space and Enter
      activate (`IsActivationKey`), arrows step a value or move a highlight, Escape cancels.
      Match similar controls (a slider's arrows, Page Up / Down, Home / End; a list's Up /
      Down).
- [ ] The handler returns `true` for keys it uses (so app shortcuts don't fire) and `false`
      otherwise.
- [ ] A text field takes only the keys the text editor uses (`MakeTextBoxHandler`,
      `TextEditorUsesKey`).

**Focus**

- [ ] A click, or a press starting a drag, focuses it: `Interaction().SetFocusID(id)`. Set
      `TextEditorFocus()` for a text field, clear it otherwise.
- [ ] Focus is visible: a `:focus` rule (border in `--accent`) or an entry in the
      `:focus-visible` ring list in `styles.css` (`box-shadow: var(--focus-ring)`).
      Full-width rows and tabs use the inset outline. `KeyboardFocusTest` fails for any Tab
      stop without visible focus.
- [ ] If a surrounding box draws the border (a field with caption or unit), that box has
      `ElementFlags::PropagatesDescendantState` (`BeginFieldBox`) and `:hover`, `:focus`,
      `:focus-visible` rules.

**States**

- [ ] Hover, active, focused, disabled and (where it fits) selected each have a style. Get
      the state from `guiStateManager.GetCurrentState(id)` and set it on the element and
      every part inside.
- [ ] Disabled (`BeginDisabled`): faded as a whole via `--disabled-opacity` (no `:disabled`
      colors needed), no input, not a Tab stop. Mouse is handled (disabled elements absorb
      hits), but key handlers and callbacks must check and do nothing.
- [ ] Indeterminate (`BeginIndeterminate`, e.g. multi-object values): shows a dash and
      returns `false` until the user edits it.

**Values and results**

- [ ] A value control takes a `T&` and writes to it. Returns `true` on the frame the user
      changed the value (not on every commit) and calls `guiStateManager.SetModified(changed)`.
      If a handler writes the value (after the call returned), wrap it in `ReportingChanges`
      and count `Interaction().HasBeenClicked(id)` as a change in the next call.
- [ ] Callbacks run from the input handler after the frame. Capture copies or references to
      storage that outlives the frame, never the caller's temporaries (e.g. a `span` of the
      caller's strings).
- [ ] Editable text has undo (the text system's editable text provides it).

**Labels and tooltips**

- [ ] Works after `Label(...)`: clicking the label focuses the control, and focus shows on
      the label. `Label` binds to the first control in the next element, so the control
      must be that element or its first control.
- [ ] Numeric fields work after `DragLabel(...)` (dragging scrubs the value; `LabelDrag*`
      events).
- [ ] `SetTooltip` and `BeginContextMenu` right after the control attach to it. They use the
      element last finished at the caller's level (`GetLastItemID()`), so the control adds
      one outer element there with its parts inside. If parts take input (a swatch and
      fields), give the outer element `ElementFlags::PropagatesDescendantState`
      (`BeginFieldBox`). Show a `GUIContent` tooltip with `ShowContentTooltip`.
- [ ] The context menu also opens from the keyboard (Shift+F10, Menu key).
      `BeginContextMenu` handles this for the focused control.

**Popups**

For a control that opens a popup (list, picker, menu):

- [ ] Anchored with `Anchored(id, ...)`, flips at the screen edge. A list for the control is
      at least as wide as the control.
- [ ] Closes on Escape, on a press outside both popup and control (title bar included), and
      when focus leaves. `CloseOnPressOutside` handles the press, `TakeEscapeFor` Escape for
      a picker window. Escape cancels and refocuses the control.
- [ ] Keyboard goes to the field for a suggestion list (`WindowNotFocusable`), to the popup
      for a menu or picker.
- [ ] Tab from the control goes to the next control, not into the popup.
- [ ] Opens on the current value (a dropdown on the selected option, scrolled into view).

**Theming and layout**

- [ ] Each part has its own CSS class, registered in `GUITheme` and listed in
      [Default CSS classes](css-classes.md), using palette variables, not literal colors. A
      different toolbar look gets a toolbar variant (`theme.controls->`).
- [ ] Sizes in CSS pixels (`styleManager.Px` for computed ones), so the UI scale applies.
- [ ] Last parameter is `LayoutOverride&& constraints = {}`, applied to the outer element.
      Sensible flex defaults: a field grows in a row, a button doesn't.
- [ ] In a node row (`nodeRowFieldMinWidth`), a field fills the row like the others.

**Frame and cache**

- [ ] Begin / End pairs follow [Begin and End](../overview/begin-end.md): End is called
      whatever Begin returned; misuse goes to `ReportMisuse`, not fatal.
- [ ] Drawing changes without input or size change (animation, value set from outside, size
      learned after layout) call `Invalidate`, or `RebuildCachedScopesAbove` from layout.
      Otherwise a cached panel shows the old drawing.
- [ ] State kept between frames doesn't depend on other controls' call order. Per-ID state
      goes in the element data store or a map keyed by ID.

## Press and drag

Keep a `PointerDrag` (`gui_core/input/gui_pointer_drag.h`) in the control's state and pass
it the element's events:

- `Press` on button down records the position. Return true; if the press came without
  capture, set it so moves, release and Escape still arrive.
- `Move` returns true for a drag move: after `Px(kDragStartDistance)` straight-line travel
  (`IsDragTravel`), or from the press for drag-only elements like sizers and scrollbar
  thumbs (`Press(..., atOnce = true)`).
- `Release` says whether it was a click or a drag.
- On `IsDragCancelKey` (Escape down), `Cancel` drops the press. Restore the value saved at
  the press. Later moves and the release are ignored.
- `LocalDelta`: distance in the element's layout space (follows a zoomed canvas).
  `ScreenDelta`: screen pixels, for moving an OS window.

Tracking a drag outside element events (a group, app-wide): drop it when
`InputManager::IsDragAbandoned` says the release won't arrive. Double-clicks:
`DoubleClickTracker::IsDoubleClick` (uses the OS double-click time).

## Files from other applications

- While files or text are dragged in over an element, its handler gets
  `EventType::FileDragOver`; return true to accept. `FileDragTargetID()` then returns that
  element, to draw it as the drop target.
- A drop arrives as `EventType::FileDrop`: paths in `state.droppedFiles`, text in
  `state.textInput`. Return true if taken.
- Both events bubble from the element under the pointer to its parents. Disabled elements
  get neither. Content is only known at drop.

```cpp
element.eventHandler = [&](ElementEvent&& event) {
    if (event.type == EventType::FileDragOver)
        return true;
    if (event.type == EventType::FileDrop)
    {
        for (const std::string& path : event.state.droppedFiles)
            Import(path);
        return true;
    }
    return false;
};
```

A drop no element takes goes to the application's handler:

```cpp
app.SetFileDropHandler([&](const FileDrop& drop) {
    for (const std::string& path : drop.files)
        Open(path);
});
```

No built-in control accepts drops.

## Decide for each control

Do it the same way as similar controls:

| Feature | When | How |
| --- | --- | --- |
| Drag to change | Numbers, sliders, colors | A `PointerDrag` (above). Escape during the drag restores the value. |
| Edit as text | A value with a text form (a slider's number) | Click to type. Enter commits, Escape cancels. |
| Context menu | Actions beyond the main one | `BeginContextMenu(id)`: right-click, Shift+F10 or Menu key |
| Move in a list | User-ordered items (rows, tabs, columns) | Drag, and Alt + arrow on the focused item. A reorderable group does both. |
| Close | Closable items (tabs) | "Close" context menu item, and Ctrl+W / Ctrl+F4 on the focused one |
| Drag and drop | Values that move between controls | Drag source and drop target with a payload type |
| Files dropped on it | Controls that import or open files | `FileDragOver` and `FileDrop` (above). Draw as target while `FileDragTargetID` returns it. |
| Copy and paste | Values with a text form | Ctrl+C / Ctrl+V while focused |
| Placeholder text | Empty text fields | The hint style, while empty |
| Validation | Text that must be a number, a name, ... | A validator on the editable text |
| Units and limits | Numbers | `NumericFieldConfig` (`minVal`, `maxVal`, `stepPerPixel`) and a unit label |
| Icon | Buttons, toggles, list items, menu items | `GUIContent`, with `-tachyon-icon-side` |
| Mixed value | Multi-selection editors | Indeterminate (above) |
| Read-only | Shown, not edited | Focusable and selectable, not editable |
| Transition | State changes worth animating | Style states blend. An on/off part (Selected) blends only with `ElementFlags::AnimatesSelected`. |
