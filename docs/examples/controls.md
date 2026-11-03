# Controls

This example puts the common controls in one window, laid out as label/value rows.
Full source: [examples/controls/controls.cpp](https://github.com/LogicalError/TachyonUI/blob/main/examples/controls/controls.cpp).

## A window as a scope

Instead of calling `BeginWindow` and `EndWindow`, this example uses the scope form. The
scope ends the window when it goes out of scope, so returning early is safe:

```cpp
// The scope ends the window when it leaves, open or not.
auto window = Window(windowID, "Controls", tachyon::LayoutOverride{}.Size(480, 560));
if (!window)
    return;
BeginVertical(tachyon::LayoutOverride{}.FlexGrow(1));
```

Other container pairs have scopes like this too, such as `Menu`, `Horizontal`, `Table`
and `Tooltip`. Each one takes the same arguments as its `Begin*` call (see
[Begin and End](../overview/begin-end.md)).

## Label/value rows

Inside a label group, each `Label` lines up with the control that follows it. The labels
sit in a column the user can resize. `DragLabel` works the same way, and the user can
also drag it to change the value of the field it labels.

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

```cpp
Label("Toggles");
BeginHorizontal();
Toggle(bold, "B");
Toggle(italic, "I");
Toggle(underline, "U");
EndHorizontal();
```

## Text and numbers

`Field` chooses its editor based on the type of the value. You can pass behavior flags
to a text field and an optional range to a number field:

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

```cpp
Label("Slider");
SliderField(volume, 0.0f, 100.0f, "Volume");

Label("Color");
ColorField(color);
```

## A menu

`BeginMenu` returns true while the menu is open, and that's when you add its items.
Call `EndMenu` either way. An item's callback runs when the user picks it.

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

A tab bar only keeps track of which tab is selected. What you show for each tab is up
to you:

```cpp
BeginTabBar(idManager.AllocateDynamicID("tabs"), &selectedTab, nullptr);
Tab("First");
Tab("Second");
EndTabBar();
Text(selectedTab == 0 ? "Content of the first tab" : "Content of the second tab");
```

Each of these controls has its own page in the [Controls](../controls.md) guide, which
lists all its overloads and its default CSS class.

---

[Previous: Hello world](hello-world.md) | [Examples](../examples.md) | [Next: Docking](docking.md)
