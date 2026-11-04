# 3. Fields

In `my_app.cpp`:

```cpp
void GreetingPanel::Draw(tachyon::GUIApplication& ui)
{
```

<!-- snippet: examples/tutorial/my_app.cpp [greeting-fields] -->
```cpp
ui.BeginLabelGroup();

ui.Label("Name");
ui.Field(name);

ui.Label("Times");
ui.Field(times, tachyon::NumericFieldConfig<int>{ .minVal = 1, .maxVal = 5 });

ui.Label("Excited");
ui.Field(excited, excited ? "Yes" : "No");

ui.EndLabelGroup();
```

- `Field` picks the editor from the type:
  - text for `std::string`
  - number for `int`
  - toggle for `bool`
- Each field edits the value in place.
- `NumericFieldConfig` limits `times` to 1..5.
- The label group lines each `Label` up with the next control:
  - the labels go in a label column, which the user can resize
  - clicking a label focuses its field
- The toggle's text is recomputed every frame, so it follows the value.

Below the fields, the greeting:

<!-- snippet: examples/tutorial/my_app.cpp [greeting-text] -->
```cpp
const std::string text = "Hello, " + name + (excited ? "!" : ".");
for (int i = 0; i < times; ++i)
    ui.Text(text);
```

- The greeting updates on the next frame after typing.
- The `Text` calls in the loop share a call site, but each gets its own ID.

Close `GreetingPanel::Draw` with `}`.

---

[Previous: 2. Buttons](buttons.md) | [Tutorial](../tutorial.md) | [Next: 4. The app](app.md)
