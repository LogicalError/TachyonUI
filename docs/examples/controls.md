# Controls

The common controls in one window, laid out as rows of a label and a value.
Full source: [examples/controls/controls.cpp](https://github.com/LogicalError/TachyonUI/blob/main/examples/controls/controls.cpp).

## A window as a scope

- `Window(...)` takes the same arguments as `BeginWindow` and returns a scope:
  - a scope is an object that calls the matching `End*` function when it goes out of
    scope
  - so `EndWindow` runs even on an early `return`
- Test the scope like the `bool` that `BeginWindow` returns.

```cpp
// The scope ends the window when it leaves, open or not.
auto window = Window(windowID, "Controls", tachyon::LayoutOverride{}.Size(480, 560));
if (!window)
    return;
BeginVertical(tachyon::LayoutOverride{}.FlexGrow(1));
```

- `Menu`, `Horizontal`, `Table`, `Tooltip` and other `Begin*` / `End*` pairs have scopes
  too.
  - Each takes the same arguments as its `Begin*` function.
  - See [Begin and End](../overview/begin-end.md).

```cpp
{
    auto row = Horizontal();   // EndHorizontal runs at the closing brace
    Toggle(bold, "B");
    Toggle(italic, "I");
}
```

## Label/value rows

- A label group lays out rows of a label and the control it names:
  - each `Label` lines up with the control after it
  - all labels share one column, which the user can resize
  - `BeginLabelGroup(140)` starts that column 140 CSS px wide
- `DragLabel` is a label that also changes its field's value when dragged.

```cpp
BeginLabelGroup(140);

Label("Button");
Button("Click me", [this] { ++clicks; });

Label("Clicks");
Text(std::to_string(clicks));

Label("Checkbox");
Field(enabled, enabled ? "On" : "Off");
```

## Toggles in a row

`BeginHorizontal` puts several controls in one row, here three toggles after one label.

```cpp
Label("Toggles");
BeginHorizontal();
Toggle(bold, "B");
Toggle(italic, "I");
Toggle(underline, "U");
EndHorizontal();
```

## Text and numbers

- `Field` picks the editor from the value's type.
- Text fields take behavior flags, such as `Multiline`.
- Number fields take an optional range in a `NumericFieldConfig`.

```cpp
Label("Text");
Field(text);

Label("Multiline");
Field(notes, tachyon::TextEditBehavior::ContinuousUpdate | tachyon::TextEditBehavior::Multiline);

DragLabel("Float");
Field(floatValue);

DragLabel("Int [0, 10]");
Field(intValue, tachyon::NumericFieldConfig<int>{ .minVal = 0, .maxVal = 10 });

DragLabel("Vector");
Field(position);
```

## Slider and color

- `SliderField` edits a number between a minimum and a maximum.
- `ColorField` edits a `color4f`.

```cpp
Label("Slider");
SliderField(volume, 0.0f, 100.0f, "Volume");

Label("Color");
ColorField(color);
```

## A menu

- `BeginMenu` returns true while the menu is open. Add the items only then.
- Always call `EndMenu`, open or not.
- An item's callback runs when the user picks it.

```cpp
Label("Menu");
if (BeginMenu(menuID, "Choose...", tachyon::LayoutOverride{}.NoGrow().NoShrink()))
{
    for (const char* item : { "Apple", "Banana", "Cherry" })
        MenuItem(item, [this, item] { choice = item; });
}
EndMenu();
```

## Tabs

- A tab bar only keeps track of which tab is selected, in `selectedTab`.
- You draw each tab's content yourself, after `EndTabBar`.

```cpp
BeginTabBar(idManager.AllocateDynamicID("tabs"), &selectedTab, nullptr);
Tab("First");
Tab("Second");
EndTabBar();
Text(selectedTab == 0 ? "Content of the first tab" : "Content of the second tab");
```

Every control's overloads and default CSS classes: [Controls](../controls.md).

---

[Previous: Hello world](hello-world.md) | [Examples](../examples.md) | [Next: Docking](docking.md)
