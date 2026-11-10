# Writing a control

The checklist for adding a control to the library. It builds on
[Common conventions](../controls/conventions.md).

## Must have

**Identity**

- [ ] Two overloads:
  - one with an explicit `ID`
  - one with an implicit dynamic ID

  For example, `Checkbox` (simplified from `gui_application.h`):

  ```cpp
  // Explicit ID
  bool Checkbox(ID id, bool& value, std::string_view text = {}, LayoutOverride&& constraints = {});

  // Implicit dynamic ID, made from the place it's called from
  bool Checkbox(bool& value, std::string_view text = {}, LayoutOverride&& constraints = {},
                std::source_location site = std::source_location::current())
  {
      return Checkbox(idManager.AllocateDynamicID(site), value, text, std::move(constraints));
  }
  ```

- [ ] A control is often built from several elements: a field can have a caption, an icon,
  or a box drawn around it. Each of these elements needs an ID of its own. Make those IDs
  from the ID the control was given:
  - call `idManager.PushParent(id)` with the control's ID
  - get an ID for each element with `idManager.AllocateDynamicID("caption")`, using a
    different name for each element
  - call `idManager.PopParent()` (only if `PushParent` returned true)
  - an element's ID then only depends on the control's ID and its name, so it stays the
    same every frame, whatever is drawn before the control

  For example, `Checkbox` draws a box and a label inside itself:

  ```cpp
  const bool pushed = idManager.PushParent(id);   // id: the checkbox's own ID

  tachyon::Element box{};
  box.id = idManager.AllocateDynamicID("checkboxBox");
  // ...
  tachyon::Element label{};
  label.id = idManager.AllocateDynamicID("checkboxLabel");
  // ...

  if (pushed)
      (void)idManager.PopParent();
  ```

- [ ] Register the control with `RegisterControl`, not `RegisterElement`:
  - it marks it as a control
  - it adds it to the Tab order
  - it reports an invalid ID
- [ ] Its elements that aren't controls themselves (captions, icons, tracks) use
  `RegisterElement` with `ElementFlags::NoInput`:
  - `NoInput` makes the mouse ignore that element
  - a click on the caption goes to the control it sits in, as if the caption weren't there

  For example, in `Checkbox`:

  ```cpp
  // The checkbox itself: a control, so it takes clicks and is a Tab stop
  tachyon::Element control{};
  control.id           = id;
  control.state        = guiStateManager.GetCurrentState(id);
  control.styleRef     = style;
  control.eventHandler = tachyon::MakeButtonHandler(guiStateManager, [value, onToggle]() { onToggle(!value); });
  RegisterControl(std::move(control), std::move(constraints));

  // Its label: not a control, so clicks on it go through to the checkbox
  tachyon::Element label{};
  label.id       = idManager.AllocateDynamicID("checkboxLabel");
  label.styleRef = theme.checkboxLabelStyleRef;
  label.flags    = tachyon::ElementFlags::NoInput;
  label.textID   = textSystem->RegisterText(label.id, text, label.styleRef, label.state);
  RegisterElement(std::move(label), LayoutOverride{});
  ```


**Keyboard**

- [ ] Tab and Shift+Tab reach the control in draw order, unless it's disabled.
  - a Tab stop is a control that Tab can move the keyboard focus to
  - `RegisterControl` makes the control a Tab stop, in the order the controls are drawn
  - a disabled control is left out, so Tab skips it

  For example, `RegisterControl` calls `RegisterTabStop`:

  ```cpp
  // Tab skips a disabled control, as the mouse does.
  void GUIApplication::RegisterTabStop(ID id)
  {
      if (!guiStateManager.Construction().IsDisabled())
          tabChainManager.RegisterTabTarget(id);
  }
  ```

- [ ] Everything the mouse does also works from the keyboard while the control has the
  focus. The focused control is the one that gets the key presses.
  - Space and Enter activate it; `IsActivationKey` checks for those keys
  - arrow keys step a value or move a highlight
  - Escape cancels
  - use the same keys as similar controls:
    - a slider: the arrow keys, Page Up / Page Down and Home / End
    - a list: Up / Down

  For example, the button that opens a `Dropdown` opens it on Space, Enter or Down:

  ```cpp
  case EventType::KeyDown:
  {
      if ((guiStateManager.GetCurrentState(event.id) & ElementState::Focused) == ElementState::None)
          break;

      if (IsActivationKey(event))
      {
          guiStateManager.Interaction().SetClicked(event.id);
          menuManager.ToggleOrOpenMenu(guiStateManager, menuID, /*focusIntoMenu*/ !isMenuBar);
          // ...
          return true;
      }
      // ...
      if (event.state.lastKey == KeyCode::Down)
      {
          // ...
          menuManager.OpenMenu(guiStateManager, menuID);
          // ...
          return true;
      }
      // ...
  }
  ```

- [ ] The key handler returns `true` for the keys it uses and `false` for every other key.
  - the key handler is the control's `eventHandler` when it gets a `KeyDown` event
  - `true` means the key is used, so an app shortcut on the same key doesn't fire as well
  - `false` leaves the key to the app's shortcuts

  For example, the keys of the integer `SliderField`:

  ```cpp
  case tachyon::EventType::KeyDown:
  {
      // ...
      // Arrows step 1, Page Up / Down a tenth of the range, Home / End to its ends.
      if (gsm.Interaction().GetFocusedID() != event.id || event.state.consumed)
          break;
      const int bigStep = std::max(1, range / 10);
      int step = 0;
      switch (event.state.lastKey)
      {
          case tachyon::KeyCode::Left:  case tachyon::KeyCode::Down:     step = -1;       break;
          case tachyon::KeyCode::Right: case tachyon::KeyCode::Up:       step = +1;       break;
          case tachyon::KeyCode::PageDown:                               step = -bigStep; break;
          case tachyon::KeyCode::PageUp:                                 step = +bigStep; break;
          case tachyon::KeyCode::Home: *valuePtr = minVal; return true;
          case tachyon::KeyCode::End:  *valuePtr = maxVal; return true;
          default: break;
      }
      if (step == 0)
          break;          // not a slider key: the handler returns false
      *valuePtr = std::clamp(*valuePtr + step, minVal, maxVal);
      return true;
  }
  ```

- [ ] A text field only takes the keys that the text editor uses.
  - `MakeTextBoxHandler` builds a handler that does this
  - in a handler of your own, ask `TextEditorUsesKey` whether the editor uses a key

  For example, in `MakeNumericalBoxHandler`, the handler of the number fields:

  ```cpp
  case EventType::KeyDown:
  case EventType::KeyUp:
  case EventType::TextInput:
      if (guiStateManager.Interaction().GetFocusedID() == event.id && !event.state.consumed)
          return event.type == EventType::TextInput ? !event.state.textInput.empty()
                                                    : TextEditorUsesKey(event.state, /*readOnly*/ false);
      break;
  ```

**Focus**

- [ ] A click on the control, or a press on it that starts a drag, gives it the focus.
  - call `Interaction().SetFocusID(id)` with the control's ID
  - a text field also sets `TextEditorFocus()`, the field that typing goes to
  - other controls clear `TextEditorFocus()`

  For example, a press on the integer `SliderField`:

  ```cpp
  case tachyon::EventType::MouseDown:
      if (event.state.LeftPressed())
      {
          gsm.Interaction().SetFocusID(event.id);
          gsm.TextEditorFocus().Clear();   // not a text field
          // ...
          return true;
      }
      break;
  ```

  And a press on a number field, in `MakeNumericalBoxHandler`:

  ```cpp
  guiStateManager.Interaction().SetFocusID(event.id);
  guiStateManager.TextEditorFocus().Set(event.id);   // a text field
  ```

- [ ] Focus is visible. The control's CSS class has one of these:
  - a `:focus` rule that draws the border in `--accent`
  - an entry in the `:focus-visible` ring list in `styles.css`. That list draws
    `box-shadow: var(--focus-ring)`, a ring that shows only when the focus came from the
    keyboard.

  Also:
  - full-width rows and tabs use the inset outline instead of the ring
  - `KeyboardFocusTest` fails for any Tab stop without visible focus

  For example, in `styles.css`:

  ```css
  /* A :focus rule */
  .textbox:focus, .number-field:focus  { border-color: var(--accent); background-color: var(--bg-input); }

  /* The ring list */
  .checkbox:focus-visible, .switch:focus-visible, .slider:focus-visible, .slider-field-track:focus-visible,
  /* ... */ {
      box-shadow: var(--focus-ring);
  }

  /* The inset outline */
  .list-item:focus-visible, .list-item-row:focus-visible,
  /* ... */ {
      box-shadow: inset 0 0 0 1px var(--accent);
  }
  ```

- [ ] Sometimes the border isn't drawn by the control's own element but by a box element
  around it, for example for a field with a caption or a unit. That box element has:
  - `ElementFlags::PropagatesDescendantState`, so it takes on the hover and focus state of
    the elements inside it (see `BeginFieldBox`)
  - `:hover`, `:focus` and `:focus-visible` rules

  For example, `BeginFieldBox`:

  ```cpp
  void GUIApplication::BeginFieldBox(ID id, StyleRef style, LayoutOverride&& constraints)
  {
      RegisterElement(tachyon::Element{ .id = id, .styleRef = style, .flags = ElementFlags::PropagatesDescendantState },
                      std::move(constraints));
      VERIFY(elementStorage.BeginChildren());
  }
  ```

**States**

- [ ] Hover, active (held down), focused, disabled and, where it fits, selected each have a
  style.
  - get the state from `guiStateManager.GetCurrentState(id)`
  - set it on the control's own element and on every element inside it

  For example, in `Checkbox`:

  ```cpp
  const tachyon::ElementState state = guiStateManager.GetCurrentState(id);
  tachyon::Element control{};
  control.id           = id;
  control.state        = state;   // the checkbox itself
  // ...
  const tachyon::ElementState partState = value ? (state | tachyon::ElementState::Selected) : state;
  tachyon::Element box{};
  box.id       = idManager.AllocateDynamicID("checkboxBox");
  box.state    = partState;      // the box inside it
  ```

- [ ] Disabled. The app disables controls by drawing them between `BeginDisabled()` and
  `EndDisabled()`. A disabled control:
  - fades as a whole through `--disabled-opacity`, so it needs no `:disabled` colors
  - takes no input
  - is not a Tab stop
  - still catches mouse hits on its elements, so a click on them doesn't reach what is
    behind them
  - can still get events, so its key handlers and callbacks must check that it's disabled
    and then do nothing

  For example, in `MenuItem`:

  ```cpp
  // A disabled item shows but can't be chosen, by mouse or keyboard.
  const bool disabled = guiStateManager.Construction().IsDisabled();
  if (disabled) onClick = nullptr;
  ```

- [ ] Indeterminate. One field can edit several selected objects whose values differ. The
  app then draws it between `BeginIndeterminate()` and `EndIndeterminate()`. An
  indeterminate control:
  - shows a dash instead of a value
  - returns `false` until the user edits it

  For example, in `Field`:

  ```cpp
  if (guiStateManager.Construction().IsIndeterminate())
  {
      RegisterControl(tachyon::Element { .id = id, .styleRef = style, .flags = ElementFlags::DoNotMeasureText },
          [this](tachyon::Element& element) { return textSystem->RegisterText(element.id, "\xe2\x80\x94", element.styleRef, element.state); },
          std::move(constraints));
      return false;
  }
  ```

**Values and results**

- [ ] A value control takes a `T&`, a reference to the caller's value, and writes to it.
  - it returns `true` on the frame the user changed the value, not on every commit. The
    text system can report a commit when the text didn't change, for example for an arrow
    key in a one-line field.
  - it calls `guiStateManager.SetModified(changed)`

  For example, the end of `Checkbox`:

  ```cpp
  const bool modified = guiStateManager.Interaction().HasBeenClicked(id);
  guiStateManager.SetModified(modified);
  return modified;
  ```

- [ ] An event handler runs during input, after the control's function has returned for
  this frame. If a handler writes the value then:
  - wrap the handler in `ReportingChanges`, which marks the control clicked when the value
    changed
  - in the next call, count `Interaction().HasBeenClicked(id)` as a change

  For example, in the integer `SliderField`:

  ```cpp
  track.eventHandler = ReportingChanges(guiStateManager, valuePtr,
                                        MakeIntSliderDrag(guiStateManager, elementStorage,
                                                          valuePtr, minVal, maxVal, trackWidth));
  // ...
  const bool changed = value != before || guiStateManager.Interaction().HasBeenClicked(id);
  guiStateManager.SetModified(changed);
  return changed;
  ```

- [ ] Callbacks run from the input handler, after the frame. What they capture must still
  be there then:
  - capture copies, or references to storage that outlives the frame
  - never the caller's temporaries, such as a `span` of the caller's strings

  For example, `AutocompleteField` copies the caller's suggestions:

  ```cpp
  // These run from event handlers, after the caller's suggestions may be gone: copies.
  std::vector<std::string> matchTexts;
  matchTexts.reserve(matches.size());
  for (const int i : matches)
      matchTexts.emplace_back(suggestions[size_t(i)]);
  auto pick = [&state, &value, matchTexts](int row)
  ```

- [ ] Editable text has undo. The text system's editable text provides it.

  For example, `Field` registers its text as editable text:

  ```cpp
  return textSystem->RegisterEditableText(element.id, value, textEditBehavior, element.styleRef, element.state, modified);
  ```

**Labels and tooltips**

- [ ] The control works after `Label(...)`. `Label` draws a caption in front of a control
  and binds to it:
  - clicking the label focuses the control
  - focus shows on the label
  - `Label` binds to the first control in the next element, so the control must be that
    element or its first control

  For example:

  ```cpp
  Label("Visible");
  Checkbox(visible);
  ```

  The label's `:focus` rule in `styles.css` shows the focus of the control:

  ```css
  .label:focus, .label-help:focus  { color: var(--text-1); }
  ```

- [ ] Numeric fields work after `DragLabel(...)`. Dragging the label scrubs the field's
  value: the label sends `LabelDrag*` events to the field.

  For example, in `MakeNumericalBoxHandler`:

  ```cpp
  case EventType::LabelDrag:
  {
      auto* snapshot = store.Get<LabelDragSnapshot>(event.id);
      if (!snapshot) return false;
      T newVal = config.Calculate(static_cast<int32_t>(event.state.scroll.x), LabelDragSpeedScale(event.state), snapshot->startValue);
      value = newVal;
      return true;
  }
  ```

- [ ] `SetTooltip` and `BeginContextMenu`, called right after the control, attach to it.
  - they attach to the last element finished at the caller's level, `GetLastItemID()`.
    That is the last element registered by the code that called the control, not one of
    the elements inside the control.
  - so the control adds one outer element at that level, with its other elements inside it
  - if elements inside take input, such as a color swatch and its fields, the outer element
    gets `ElementFlags::PropagatesDescendantState` (see `BeginFieldBox`)
  - to show a `GUIContent` tooltip, call `ShowContentTooltip`

  For example, `SetTooltip` and `BeginContextMenu` without an ID, in `gui_application.h`:

  ```cpp
  inline void SetTooltip(std::string_view text) { SetTooltip(elementStorage.GetLastItemID(), text); }
  // ...
  inline bool BeginContextMenu() { return BeginContextMenu(elementStorage.GetLastItemID()); }
  ```

  And `Button` shows the tooltip of its `GUIContent`:

  ```cpp
  ShowContentTooltip(id, content);
  ```

- [ ] The context menu also opens from the keyboard, with Shift+F10 or the Menu key.
  `BeginContextMenu` handles this for the focused control.

  For example, in `BeginContextMenu`:

  ```cpp
  if (e.type == tachyon::EventType::KeyDown &&
      (e.state.lastKey == tachyon::KeyCode::Application || e.state.lastKey == tachyon::KeyCode::Menu ||
       (e.state.lastKey == tachyon::KeyCode::F10 && (e.state.modifiers & KeyModifier::Shift) != KeyModifier::None)))
      byKeyboard = true;
  ```

**Popups**

For a control that opens a popup, such as a list, a picker or a menu:

- [ ] The popup is placed with `Anchored(id, ...)`, next to the control, and flips to the
  other side at the screen edge.

  For example, in `ColorField`:

  ```cpp
  LayoutOverride popupPos = LayoutOverride{}.Anchored(swatchID,
      AnchorPlacement::OutsideBottom | AnchorPlacement::InsideLeft,
      {0, int16_t(styleManager.Px(4))}, AutoFlipMode::FlipAvoidTarget);
  ```

- [ ] A list for the control is at least as wide as the control.

  For example, in `OpenMenu`, which `BeginMenu` and `Dropdown` use:

  ```cpp
  pos.MinWidth(Pixels{ triggerRect.w }).MaxHeight(Pixels{ workArea.h });
  ```

- [ ] The popup closes:
  - on Escape. Escape cancels and gives the focus back to the control. For a picker
    window, use `TakeEscapeFor`.
  - on a press outside both the popup and the control, its title bar included. Use
    `CloseOnPressOutside`.
  - when the focus leaves it

  For example, Escape in `ColorField`:

  ```cpp
  // Escape is Cancel, and the keyboard goes back to the swatch.
  if (cp.triggerID == swatchID && cp.closeRequest == ColorPickerState::CloseRequest::None && TakeEscapeFor(cp.windowID, swatchID))
  {
      cp.closeRequest = ColorPickerState::CloseRequest::Cancel;
      guiStateManager.Interaction().SetFocusID(swatchID);
  }
  ```

  And a press outside, in `AutocompleteField`:

  ```cpp
  CloseOnPressOutside(id, state.listWindowID, [&state] { state.dismissed = true; state.current = -1; });
  ```

- [ ] Keys go to the right place:
  - for a suggestion list, to the field. The list's window gets `WindowNotFocusable`.
  - for a menu or a picker, to the popup

  For example, the suggestion list of `AutocompleteField`:

  ```cpp
  const ElementFlags flags = ElementFlags::WindowNoCaption | ElementFlags::WindowIsPopup
                           | ElementFlags::Shadows | ElementFlags::WindowNotFocusable;
  ```

- [ ] Tab from the control goes to the next control, not into the popup.
- [ ] The popup opens on the current value: a dropdown opens on the selected option,
  scrolled into view.

  For example, in `Dropdown`:

  ```cpp
  // The rows' IDs up front, so the list can open on the current option instead
  // of the first one.
  std::vector<ID> rows(options.size());
  for (ID& row : rows)
      row = idManager.AllocateDynamicID("dropdown_option");

  auto& level = menuManager.levels[menuManager.currentDepth];
  if (valid && level.isOpen && level.activeItemID == ID::Invalid() && menuManager.focusSubmenuOnOpen)
  {
      level.activeItemID = rows[size_t(shown)];
      guiStateManager.Interaction().SetFocusID(level.activeItemID);
  }
  ```

**Theming and layout**

- [ ] Each of the control's elements has its own CSS class. That class is:
  - registered in `GUITheme`
  - listed in [Default CSS classes](../controls/css-classes.md)
  - written with palette variables, not literal colors

  For example, `Checkbox`'s classes, registered in `gui_theme.cpp`:

  ```cpp
  checkboxStyleRef                  = registerCSS("checkbox");
  checkboxBoxStyleRef               = registerCSS("checkbox-box");
  checkboxLabelStyleRef             = registerCSS("checkbox-label");
  ```

  And one of them in `styles.css`:

  ```css
  .checkbox-box::highlight(selected) { background-color: var(--accent); border-color: var(--accent); color: var(--on-accent); }
  ```

- [ ] If the control looks different in a toolbar, it gets a toolbar variant. It then takes
  its style from `theme.controls->`, which points to the toolbar styles inside a toolbar.

  For example, `Dropdown` without a style:

  ```cpp
  inline bool Dropdown(ID id, int& selected, std::span<const std::string_view> options, LayoutOverride&& constraints = {}) { return Dropdown(id, selected, options, theme.controls->menuTrigger, std::move(constraints)); }
  ```

  And `BeginToolbar`, which switches to the toolbar styles:

  ```cpp
  theme.controls = &theme.toolbarControls;
  ```

- [ ] Sizes are in CSS pixels, so the UI scale applies. Convert sizes computed in code with
  `styleManager.Px`.

  For example, in `SliderField`:

  ```cpp
  const int kDefaultTrackWidth = styleManager.Px(120);
  ```

- [ ] The last parameter is `LayoutOverride&& constraints = {}`. It is applied to the
  control's outer element.
  - give it sensible flex defaults: a field grows in a row, a button doesn't

  For example, `SliderField` grows in its row:

  ```cpp
  const auto trackRef = RegisterControl(std::move(track), std::move(constraints).FlexGrow(1));
  ```

- [ ] In a node row, a field fills the row like the others. `nodeRowFieldMinWidth` is set
  while a node row is drawn.

  For example, in `Field`:

  ```cpp
  // Inside a node row, fill the body after the label so the node stays grid-aligned.
  if (nodeRowFieldMinWidth > 0 && !constraints.HasFixedWidth())
      constraints.FlexGrow(1).MinWidth(Pixels{ nodeRowFieldMinWidth });
  ```

**Frame and cache**

- [ ] Begin / End pairs follow [Begin and End](../overview/begin-end.md):
  - End is called whatever Begin returned
  - misuse goes to `ReportMisuse`; it isn't fatal

  For example, `Dropdown` calls `EndMenu` whether `BeginMenu` returned true or not:

  ```cpp
  if (BeginMenu(id, valid ? options[size_t(shown)] : std::string_view{}, style,
                theme.menuStyleRef, theme.menuItemRowStyleRef, std::move(constraints)))
  {
      // ...
  }
  EndMenu();
  ```

  And `EndCached` reports a missing `BeginCached` and goes on:

  ```cpp
  if (m_cachedOpen.empty())
  {
      ReportMisuse("GUIApplication: EndCached without BeginCached (ignored)");
      return;
  }
  ```

- [ ] A cached panel keeps its drawing from earlier frames. It is rebuilt when input
  reaches it or its size changes. When the control's drawing changes without either, call
  `Invalidate`, or `RebuildCachedScopesAbove` from layout. Otherwise the cached panel shows
  the old drawing. This happens for:
  - an animation
  - a value set from outside
  - a size learned after layout

  For example, `NodeView` learns its size after layout:

  ```cpp
  // The nodes were centered for last frame's size. A cached panel around the
  // view only rebuilds when its own size changes, so ask for the redraw.
  if (view.lastInnerSize.x != centeredFor.x || view.lastInnerSize.y != centeredFor.y)
      RebuildCachedScopesAbove(canvasID);
  ```

- [ ] State kept between frames doesn't depend on the order in which other controls are
  called.
  - keep per-ID state in the element data store, or in a map keyed by ID

  For example, `SelectableText` keeps its press in the element data store:

  ```cpp
  auto& store = guiStateManager.GetElementDataStore();
  // ...
  store.GetOrCreate<PointerDrag>(id).Press(ev, elementStorage);
  ```

  And `AutocompleteField` keeps its state in a map keyed by ID:

  ```cpp
  AutocompleteState& state = m_autocomplete[id];
  ```

## Press and drag

A press on an element can become a click or a drag. To tell them apart, keep a
`PointerDrag` (`gui_core/input/gui_pointer_drag.h`) in the control's state, and pass it the
events of the element that is pressed:

- `Press` when the button goes down. It records where the pointer is.
  - the handler returns true
  - if the press came without the mouse capture, set the capture, so the moves, the
    release and Escape still reach the element when the pointer leaves it

  For example, a node in `NodeView`:

  ```cpp
  view.nodeDrag.Press(event, elementStorage);
  // The moves, the release and Escape come here wherever the pointer goes.
  guiStateManager.Interaction().SetCaptureID(outerID);
  return true;
  ```

- `Move` returns true for a move that is part of a drag:
  - once the pointer has moved `Px(kDragStartDistance)` in a straight line from the press
    (`IsDragTravel`)
  - or from the press on, for elements that can only be dragged, like sizers and scrollbar
    thumbs. Press those with `Press(..., atOnce = true)`.

  For example, a node in `NodeView`:

  ```cpp
  if (!view.nodeDrag.Move(event, styleManager.Px(kDragStartDistance))) return true;
  ```

  And the integer `SliderField`, which drags from the press on:

  ```cpp
  s.drag.Press(event, es, MouseButtons::Left, /*atOnce*/ true);
  ```

- `Release` says whether the press ended as a click or a drag.

  For example, `SelectableText`:

  ```cpp
  if (press && press->Release(ev) == PointerDrag::Ended::Click)
  {
      // a click on a link, not the end of a text selection drag
  }
  ```

- `IsDragCancelKey` is true for Escape going down. Then `Cancel` drops the press:
  - put back the value saved at the press
  - the moves and the release that follow are ignored

  For example, the integer `SliderField`:

  ```cpp
  if (auto* s = store.Get<IntSliderState>(event.id); s && IsDragCancelKey(event.state) && s->drag.Cancel(event.id))
  {
      *valuePtr = s->startValue;
      return true;
  }
  ```

- `LocalDelta` is the distance moved in the element's layout space. It follows a zoomed
  canvas.

  For example, a node in `NodeView`:

  ```cpp
  // The node is inside the canvas: its layout space is node.pos space.
  const int2 idelta = view.nodeDrag.LocalDelta(event, elementStorage);
  ```

- `ScreenDelta` is the distance moved in screen pixels, for moving an OS window.

  For example, a floating toolbar window:

  ```cpp
  // An OS window: it moves in screen pixels.
  const int2 delta = drag.drag.ScreenDelta(e.state);
  ```

Also:

- A drag can be tracked outside element events, for a group or for the whole app. Drop
  such a drag when `InputManager::IsDragAbandoned` says the release won't arrive.

  For example, the reorderable groups:

  ```cpp
  if (m_drag.IsPressed() && ctx.inputManager != nullptr && ctx.inputManager->IsDragAbandoned())
      m_drag = {};
  ```

- For double-clicks, use `DoubleClickTracker::IsDoubleClick`. It uses the OS double-click
  time.

  For example, a node in `NodeView`:

  ```cpp
  if (view.nodeDoubleClick.IsDoubleClick(outerID))
  ```

## Files from other applications

Files and text can be dragged onto an element from another application.

- While they are dragged over an element, its handler gets `EventType::FileDragOver`.
  - the handler returns true to accept them
  - `FileDragTargetID()` then returns that element, so the control can draw it as the drop
    target
- A drop arrives as `EventType::FileDrop`.
  - the paths are in `state.droppedFiles`, the text in `state.textInput`
  - the handler returns true if it took the drop
- Both events bubble: they go first to the element under the pointer, then to its parents,
  until a handler returns true.
- Disabled elements get neither event.
- What is dropped is only known at the drop, not while it is dragged over.
- Elements in a cached panel get the events too. The panel is built again while the drag
  is over it, as it is under the mouse.

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

## Pen input

A pen on a tablet or pen display drives the pointer as a mouse does.

- Its tip is the left button: a control that works with the mouse works with a pen.
- Every pointer event (moves, presses, releases, the wheel) also carries the pen in
  `event.state.pen`, a `PenState`.
  - `IsPen()`: a pen moved the pointer, not the mouse
  - `eraser`: the pen's eraser end is the one in use
  - `inProximity`, `touching`: near the tablet, and on it
  - `buttons`: the barrel buttons held, bit 0 the first

  ```cpp
  if (event.state.IsPen() && event.state.pen.eraser)
      EraseAt(event.LocalCursor(storage));
  ```

- The pen's values, as far as the pen has them (others stay 0):

  | Field | Range |
  | --- | --- |
  | `pressure` | 0 to 1 |
  | `tilt` | degrees, -90 to 90: x leaning right, y leaning down |
  | `rotation` | the barrel's, degrees -180 to 180, clockwise, 0 up |
  | `distance` | to the tablet, 0 to 1 |
  | `slider` | a finger wheel or slider (an airbrush pen), 0 to 1 |
  | `tangentialPressure` | from squeezing the barrel, 0 to 1 |

- With the mouse, `pressure` is 1 while a button is held and 0 otherwise, so a brush works
  with either:

  ```cpp
  const float width = minWidth + (maxWidth - minWidth) * event.state.pen.pressure;
  ```

- A pressure change with no move reaches the handler with the next pointer event.
  `InputManager::GetPen()` has the latest values at any time.
- Tests give the pen with `InjectPen(pen)` before the pointer events it goes with; a default
  `PenState` hands the pointer back to the mouse.

  ```cpp
  app.InjectPen(PenState{ .pointer = PointerType::Pen, .inProximity = true, .touching = true,
                          .pressure = 0.6f });
  app.InjectMouseDown(at);
  ```

## Decide for each control

Do it the same way as similar controls:

| Feature | When | How |
| --- | --- | --- |
| Drag to change | Numbers, sliders, colors | A `PointerDrag` (see Press and drag). Escape during the drag puts the value back. |
| Edit as text | A value that can also be typed, like a slider's number | A click starts typing. Enter keeps the text, Escape cancels. |
| Context menu | Actions besides the control's main one | `BeginContextMenu(id)`. It opens on a right-click, Shift+F10 or the Menu key. |
| Move in a list | Items the user puts in order: rows, tabs, columns | Drag the item, or press Alt + an arrow key on the focused item. A reorderable group does both. |
| Close | Items that can be closed, like tabs | A "Close" item in the context menu, and Ctrl+W / Ctrl+F4 on the focused item |
| Pan and zoom | A canvas the user moves around in: a graph, a plot, a timeline | `PanDragButton` for the middle button (or Alt + left) and `NavigateByWheel` with `GetWheelMode()` for the wheel and a pinch (see Panning and zooming in Common conventions). Never the right button: it opens menus. |
| Drag and drop | Values that move from one control to another | A drag source and a drop target that agree on a payload type |
| Files dropped on it | Controls that import or open files | `FileDragOver` and `FileDrop` (see Files from other applications). Draw the element as the drop target while `FileDragTargetID` returns it. |
| Copy and paste | Values that can be written as text | Ctrl+C / Ctrl+V while the control has the focus |
| Placeholder text | Empty text fields | A hint in the hint style, shown while the field is empty |
| Validation | Text that must be a number, a name, ... | A validator on the editable text |
| Units and limits | Numbers | `NumericFieldConfig` (`minVal`, `maxVal`, `stepPerPixel`) and a label for the unit |
| Icon | Buttons, toggles, list items, menu items | A `GUIContent` with an icon. `-tachyon-icon-side` in CSS says on which side it goes. |
| Mixed value | Editors for several selected objects | Indeterminate (see States) |
| Read-only | Values that are shown, not edited | The control can be focused and its text selected, but not edited |
| Transition | State changes worth animating | Style states blend into each other. An on/off state such as Selected blends only with `ElementFlags::AnimatesSelected`. |
