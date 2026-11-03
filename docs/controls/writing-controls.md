# Writing a control

This page lists what every control and field has to do, and what you need to decide for
each optional feature. Users learn how controls work from the ones they already know, so a
control that skips one of these behaves differently from the rest, and in a UI that's a
bug. The checklist applies both to the library's own controls and to controls your
application adds.

It builds on the API shape described in [Common conventions](conventions.md).

## Must have

**Identity**

- [ ] Provide overloads that take an `ID` and that use the call site as the ID
      (`std::source_location`), each with and without a `StyleRef`. The overload with an ID
      and a style does the actual work. See [Common conventions](conventions.md).
- [ ] Give the control's parts (an icon, a caption, a box around it) IDs under the
      control's own with `idManager.PushParent(id)`. That way they stay the same no matter
      what's drawn before the control.
- [ ] Register the control with `RegisterControl`, not `RegisterElement`. That marks it as
      a control, puts it in the Tab order and reports an invalid ID. Register parts that
      aren't controls (captions, icons, tracks) with `RegisterElement` and
      `ElementFlags::NoInput`, so the mouse reaches the control.

**Keyboard**

- [ ] Tab and Shift+Tab reach it in the order it's drawn, unless it's disabled.
- [ ] Once it has the focus, everything the mouse does can also be done with the keyboard.
      Space and Enter activate it (`IsActivationKey`), the arrow keys step a value or move
      a highlight, and Escape cancels. Use the same keys as similar controls: a slider's
      arrows, Page Up / Down and Home / End, or a list's Up / Down.
- [ ] The handler returns `true` for a key the control uses, so the key doesn't also
      trigger the app's shortcuts, and `false` for a key it doesn't use.
- [ ] A text field only takes the keys the text editor uses (`MakeTextBoxHandler`,
      `TextEditorUsesKey`).

**Focus**

- [ ] A click, or a press that starts a drag, gives it the focus with
      `Interaction().SetFocusID(id)`. Set `TextEditorFocus()` for a text field and clear it
      for anything else.
- [ ] It shows when it has the focus. Either its CSS class has a `:focus` rule (a border in
      `--accent`), or it's in the `:focus-visible` ring list in `styles.css`
      (`box-shadow: var(--focus-ring)`). A full-width row or tab uses the inset outline
      instead. `KeyboardFocusTest` fails for any Tab stop that doesn't show its focus.
- [ ] If its border is drawn by a box around it (a field with a caption or a unit), that
      box has `ElementFlags::PropagatesDescendantState` (`BeginFieldBox` in the library)
      and the `:hover`, `:focus` and `:focus-visible` rules, so it shows the field's
      states.

**States**

- [ ] Hover, active (pressed), focused, disabled and, where it makes sense, selected each
      have a style. Get the state from `guiStateManager.GetCurrentState(id)` and set it on
      the element and on every part drawn inside it.
- [ ] When disabled (`BeginDisabled`), it's drawn faded as a whole using the theme's
      `--disabled-opacity`, so you don't need `:disabled` colors. It takes no input and
      isn't a Tab stop. The mouse is handled for you, since a disabled element absorbs
      hits, but a handler that acts on keys or callbacks must check and do nothing while
      disabled.
- [ ] When indeterminate (`BeginIndeterminate`), as in a field showing the values of
      several objects, it shows a dash and returns `false` until the user edits it.

**Values and results**

- [ ] A value control takes a `T&` and writes to it. It returns `true` on the frame the
      user changed the value (not on every commit) and calls
      `guiStateManager.SetModified(changed)`. If a handler writes the value, that happens
      after the control's call has returned. In that case wrap it in `ReportingChanges`,
      and count `Interaction().HasBeenClicked(id)` as a change in the next call.
- [ ] Callbacks run from the input handler, after the frame. They capture copies, or
      references to storage that outlives the frame, and never the caller's temporaries
      (such as a `span` of the caller's strings).
- [ ] Text the user can edit has undo. The text system's editable text gives you this.

**Labels and tooltips**

- [ ] It works after `Label(...)`. Clicking the label focuses the control, and the
      control's focus shows on the label. `Label` binds to the first control in the
      element after it, so the control has to be the next element, or be the first control
      inside it.
- [ ] A numeric field works after `DragLabel(...)`, so dragging the label scrubs the value
      (`LabelDrag*` events).
- [ ] `SetTooltip` and `BeginContextMenu` called right after the control attach to it.
      They take the element last finished at the caller's level (`GetLastItemID()`), so the
      control's outer element must be the one it adds at that level, with its parts inside.
      When the parts take input of their own (a swatch and fields), give the outer element
      `ElementFlags::PropagatesDescendantState` (`BeginFieldBox`) so the tooltip and context
      menu work over each part. Show a `GUIContent` tooltip with `ShowContentTooltip`.
- [ ] Its context menu also opens from the keyboard (Shift+F10, the Menu key).
      `BeginContextMenu` does this for the focused control.

**Popups**

For a control that opens a popup (a list, a picker, a menu):

- [ ] The popup is anchored to the control with `Anchored(id, ...)` and flips at the
      screen edge. When it's a list for the control, it's at least as wide as the control.
- [ ] It closes on Escape, on a press anywhere outside both the popup and the control
      (including the window's title bar), and when the focus leaves. `CloseOnPressOutside`
      handles the press, and `TakeEscapeFor` handles Escape for a picker window. Escape
      cancels and puts the focus back on the control.
- [ ] Keyboard input goes where it belongs: to the field for a suggestion list
      (`WindowNotFocusable`), and to the popup for a menu or picker.
- [ ] Tab from the control moves on to the next control, not into the popup.
- [ ] It opens on the current value. A dropdown, for example, opens on the selected
      option, scrolled into view.

**Theming and layout**

- [ ] Each part has its own CSS class, registered in `GUITheme` and listed in
      [Default CSS classes](css-classes.md). The classes use the dark and light palettes'
      variables rather than literal colors. If the control looks different in a toolbar,
      add a toolbar variant (`theme.controls->`).
- [ ] Sizes are in CSS pixels (use `styleManager.Px` for anything computed), so the
      control follows the UI scale.
- [ ] `LayoutOverride&& constraints = {}` is the last parameter and is applied to the
      control's outer element. The flex defaults are sensible: a field grows in a row, a
      button doesn't.
- [ ] In a node row (`nodeRowFieldMinWidth`), a field fills the row like the others do.

**Frame and cache**

- [ ] Begin / End pairs follow [Begin and End](../overview/begin-end.md). End is called
      whatever Begin returned, and misuse is reported with `ReportMisuse` rather than being
      fatal.
- [ ] If something changes how the control is drawn without an input or a size change (an
      animation, a value set from outside, its own size learned after layout), it asks for
      a redraw with `Invalidate`, or `RebuildCachedScopesAbove` from layout. Otherwise a
      cached panel keeps showing the old drawing.
- [ ] Nothing it keeps between frames depends on the order other controls are called in.
      Per-ID state goes in the element data store or in a map keyed by the ID.

## Press and drag

A control that can be pressed and dragged keeps a `PointerDrag` (`gui_core/input/gui_pointer_drag.h`)
in its state and passes it the element's events. That way every drag in the library
behaves the same:

- Call `Press` when the button goes down to record where. Return true from the handler, and
  if the press arrived without the capture, set it, so the moves, the release and Escape
  reach the element wherever the pointer goes.
- `Move` returns true for a drag move. That's once the pointer has travelled
  `Px(kDragStartDistance)` in a straight line (`IsDragTravel`), or right from the press for
  a drag that's never a click, such as a sizer or a scrollbar thumb
  (`Press(..., atOnce = true)`).
- `Release` tells you whether the press ended as a click or a drag.
- On `IsDragCancelKey` (Escape going down), `Cancel` drops the press. Put back the value
  the control saved at the press. Any moves and the release that follow are ignored.
- `LocalDelta` is the distance travelled in the element's layout space, which follows a
  zoomed canvas. `ScreenDelta` is in screen pixels, for moving an OS window.

If you track a drag outside an element's events (for a whole group, or app-wide), drop it
when `InputManager::IsDragAbandoned` says its release won't arrive. For double-clicks, use
`DoubleClickTracker::IsDoubleClick`, which uses the OS's double-click time.

## Files from other applications

Files and text dragged in from another application (a file manager, a browser) go to the
elements under the pointer. While the drag is over an element, its handler gets
`EventType::FileDragOver` and returns true to accept it. `FileDragTargetID()` then returns
that element, so you can draw it as the drop target.

A drop arrives as `EventType::FileDrop`, with the paths in `state.droppedFiles` and any
text in `state.textInput`. The handler returns true if it took them. Both events bubble up
from the element under the pointer to its parents, and a disabled element gets neither. You
only find out what's being dragged once it's dropped.

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

A drop that no element takes goes to the application's handler, which is the place to open
the files:

```cpp
app.SetFileDropHandler([&](const FileDrop& drop) {
    for (const std::string& path : drop.files)
        Open(path);
});
```

None of the built-in controls accept drops by themselves.

## Decide for each control

These features don't apply to every control. Decide on each one deliberately, and do it
the same way as similar controls do:

| Feature | When | How |
| --- | --- | --- |
| Drag to change | Numbers, sliders, colors | A `PointerDrag` (above). Press, then drag past the drag distance. Escape during the drag restores the value. |
| Edit as text | A value that also has a text form (a slider's number) | Click to type. Enter commits and Escape cancels. |
| Context menu | Anything with actions beyond the main one | `BeginContextMenu(id)`, opened by right-click, Shift+F10 or the Menu key |
| Move in a list | Items the user orders (rows, tabs, columns) | Drag, and Alt + an arrow key on the focused item. A reorderable group does both. |
| Close | Items that close (tabs) | A "Close" context menu item, and Ctrl+W / Ctrl+F4 on the focused one |
| Drag and drop | Values that move between controls | A drag source and a drop target with a payload type |
| Files dropped on it | Controls that import or open files (an image well, an asset list) | `FileDragOver` and `FileDrop` (above). Draw it as the target while `FileDragTargetID` returns it. |
| Copy and paste | Values with a text form | Ctrl+C / Ctrl+V while focused |
| Placeholder text | Empty text fields | The hint style, shown while the field is empty |
| Validation | Text that must be a number, a name, ... | A validator on the editable text |
| Units and limits | Numbers | `NumericFieldConfig` (`minVal`, `maxVal`, `stepPerPixel`) and a unit label |
| Icon | Buttons, toggles, list items, menu items | `GUIContent`, with `-tachyon-icon-side` |
| Mixed value | Multi-selection editors | Indeterminate (above) |
| Read-only | Values that are shown but not edited | Focusable and selectable, but not editable |
| Transition | State changes worth animating | The style's states. Transitions blend between them. A part that turns on and off (Selected) only blends with `ElementFlags::AnimatesSelected`. |
