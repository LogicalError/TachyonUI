# Controls

The common controls in one window, as label/value rows.
Full source: [examples/controls/controls.cpp](https://github.com/LogicalError/TachyonUI/blob/main/examples/controls/controls.cpp).

## A window as a scope

`Window(...)` returns a scope that ends the window when it goes out of scope, so early
returns are safe:

```cpp
// The scope ends the window when it leaves, open or not.
auto window = Window(windowID, "Controls", tachyon::LayoutOverride{}.Size(480, 560));
if (!window)
    return;
BeginVertical(tachyon::LayoutOverride{}.FlexGrow(1));
```

`Menu`, `Horizontal`, `Table`, `Tooltip` and other pairs have scopes too, taking the same
arguments as their `Begin*` (see [Begin and End](../overview/begin-end.md)).

## Label/value rows

In a label group each `Label` lines up with the next control; the label column is
user-resizable. `DragLabel` also changes its field's value when dragged.

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

`Field` picks the editor from the value's type. Text fields take behavior flags; number
fields take an optional range:

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

`BeginMenu` returns true while open; add items then. Always call `EndMenu`. An item's
callback runs when it is picked.

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

A tab bar only tracks the selected tab; you draw each tab's content:

```cpp
BeginTabBar(idManager.AllocateDynamicID("tabs"), &selectedTab, nullptr);
Tab("First");
Tab("Second");
EndTabBar();
Text(selectedTab == 0 ? "Content of the first tab" : "Content of the second tab");
```

Overloads and default CSS classes: [Controls](../controls.md).

---

[Previous: Hello world](hello-world.md) | [Examples](../examples.md) | [Next: Docking](docking.md)
