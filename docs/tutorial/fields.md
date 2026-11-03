# 3. Fields

The greeting panel has one row for each field. In `my_app.cpp`:

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

`Field` picks the right editor for the type of value you give it. You get a text field
for the `std::string`, a number field for the `int` and a toggle for the `bool`. Each one
edits the value in place.

`NumericFieldConfig` sets limits on a number, so here `times` stays between 1 and 5.

The label group lines up each `Label` with the control that follows it, in a label
column the user can resize. Clicking a label focuses its field.

The toggle's text is worked out again every frame, so it switches between "Yes" and "No"
as the value changes.

Below the fields, draw the greeting they make:

<!-- snippet: examples/tutorial/my_app.cpp [greeting-text] -->
```cpp
const std::string text = "Hello, " + name + (excited ? "!" : ".");
for (int i = 0; i < times; ++i)
    ui.Text(text);
```

When you type a name, the greeting changes on the next frame. You don't have to tell the
text to update. The loop draws the line `times` times. Even though those `Text` calls
share a call site, each one gets its own ID because they're made in a loop.

Close `GreetingPanel::Draw` with `}`.

---

[Previous: 2. Buttons](buttons.md) | [Tutorial](../tutorial.md) | [Next: 4. The app](app.md)
