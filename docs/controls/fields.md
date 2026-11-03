# Fields

`Field` is overloaded on the type of the value you pass: text, bool, numbers, vectors,
lists and dictionaries. Every form returns `true` when the value changed.

## Text fields

```cpp
bool Field(ID id, std::string& value, TextEditBehavior textEditBehavior, StyleRef style,
           LayoutOverride&& constraints = {});
```

You can combine `TextEditBehavior` flags with `|`:

| Flag | Effect |
| --- | --- |
| `ContinuousUpdate` (default) | Updates `value` on every keystroke, not only on commit |
| `Multiline` | Enter adds a newline instead of committing |
| `AcceptTabs` | Tab inserts a tab instead of moving focus |
| `ReadOnly` | The text can be selected and copied, but not edited |
| `None` | Updates on commit (Enter or focus loss) |

The default class is `textbox`, or `toolbar-textbox` in a toolbar.

```cpp
Field(name);
Field(notes, TextEditBehavior::ContinuousUpdate | TextEditBehavior::Multiline);
```

## SearchField

A text field with an icon at the start, inside its border.

```cpp
bool SearchField(ID id, std::string& value, IconID icon, TextEditBehavior textEditBehavior,
                 StyleRef style, LayoutOverride&& constraints = {});
```

It uses the classes `search-textbox`, `search-textbox-icon` and `search-textbox-field`.

## AutocompleteField

A text field that shows a list of matching suggestions below it while it has the
keyboard.

```cpp
bool AutocompleteField(ID id, std::string& value, std::span<const std::string_view> suggestions,
                       StyleRef style, LayoutOverride&& constraints = {});
```

Once you type something, the list shows up to 12 suggestions that contain it, ignoring
case. If the only match is exactly what you typed, the list stays empty. Up and Down
highlight a suggestion, and Enter or a click puts it into `value`. Nothing is highlighted
until you press Down, so pressing Enter right away keeps what you typed. Escape, picking a
suggestion or clicking elsewhere closes the list, and typing opens it again.

It returns `true` when `value` changed, whether by typing or by picking a suggestion. The
field uses the class `textbox`. The list uses `command-search-list`, `menu-item-row` and
`command-search-current-row`, the same as the command search.

```cpp
static constexpr std::string_view kFruits[] = { "Apple", "Banana", "Cherry" };
AutocompleteField(fruit, kFruits);
```

## Dropdown

A field that shows the selected option and opens the list of options below it.

```cpp
bool Dropdown(ID id, int& selected, std::span<const std::string_view> options, StyleRef style,
              LayoutOverride&& constraints = {});
bool Dropdown(int& selected, std::initializer_list<std::string_view> options,
              LayoutOverride&& constraints = {});
```

When you pick an option, by clicking it or with the arrow keys and Enter, `selected` is
set and `Dropdown` returns `true` on that frame. The field shows the new option from the
next frame on. Escape or a click elsewhere closes the list without changing anything.
When the list opens, the keyboard starts on the current option. If `selected` is outside
the range of options, the field shows up empty. The default class is `menu-trigger`, with
`menu` for the popup and `menu-item-row` for the rows.

```cpp
static constexpr std::string_view kThemes[] = { "Dark", "Light" };
if (Dropdown(themeIndex, kThemes))
    ApplyTheme(themeIndex);

Dropdown(quality, { "Low", "Medium", "High" });
```

## Numeric fields

```cpp
template<typename T, typename ComponentT = typename TypeDimensions<T>::ComponentType>
bool Field(ID id, T& value, TextEditBehavior textEditBehavior, StyleRef style,
           NumericFieldConfig<ComponentT> config = {}, LayoutOverride&& constraints = {});
```

Shorter overloads leave out the behavior, the style or the config. This works for
`float`, `double` and the 8- to 64-bit signed and unsigned integers. You get an editable
number that you can also change by dragging its label, once the pointer has moved the
drag distance. Hold Ctrl to drag slower or Shift to drag faster, and press Escape to put
the value back.

`NumericFieldConfig` sets `minVal`, `maxVal` and `stepPerPixel`, which is how much the
value changes per pixel dragged. A NaN shows in a warning color. The default class is
`number-field`, or `toolbar-number-field` in a toolbar.

```cpp
Field(floatValue);
Field(intValue, NumericFieldConfig<int>{ .minVal = 0, .maxVal = 10 });
```

## Vector fields

Call the same `Field` on a vector type and you get one number per component. Each number
has a colored axis label in front of it that you can drag. The supported types are
`int2`-`int4`, `uint2`-`uint4`, `short2`-`short4`, `ushort2`-`ushort4`, `byte2`-`byte4`
and `float2`-`float4` (labeled X, Y, Z, W), plus `color4b` and `color4f` (labeled R, G,
B, A). The config applies to each component.

```cpp
DragLabel("Position");
Field(position);   // float3
```

The numbers use the class `vector-component`, and the labels use `axis-label-x`,
`axis-label-y`, `axis-label-z` and `axis-label-w`.

## TypedField

A number field that shows its type after the value, for example "0.5  f32".

```cpp
template<typename T> bool TypedField(ID id, T& value, LayoutOverride&& constraints = {});
```

It uses the classes `typed-field`, `typed-field-input` and `typed-field-type`.

## Lists

Pass a `std::vector<T>` and you get a list you can reorder. Each row has a grip, its
index, the field (numbers show their type) and a remove button, and an "Add item" button
follows the rows. `T` can be any type that `Field` takes. Keep the ID the same every
frame, or the reorder state is lost. Alt+Up / Down moves the row that has the focus, and
the focus moves with it.

```cpp
template<typename T>
void Field(ID id, std::vector<T>& items, StyleRef style, LayoutOverride&& constraints = {});
template<typename T>
void Field(ID id, std::vector<T>& items, LayoutOverride&& constraints = {});
```

```cpp
Label("Names", std::to_string(names.size()) + " items");
Field(namesListID, names, LayoutOverride{}.FlexGrow(1));
```

It uses `collection-area` around the list, plus `collection-list`, `collection-row`,
`collection-index`, `collection-remove` and `collection-add`.

## Dictionaries

A key / value list with `std::string` keys. It has a header showing the types, one row
per entry (key, value and a remove button) and an "Add entry" button. Over a vector of
pairs, the rows keep their order. Over an `unordered_map`, the order can change when the
map rehashes. A renamed key is committed on Enter or when the field loses focus, and
renaming onto a key that already exists is ignored.

```cpp
template<typename V>
void Field(ID id, std::vector<std::pair<std::string, V>>& items, StyleRef style, LayoutOverride&& constraints = {});
template<typename V>
void Field(ID id, std::unordered_map<std::string, V>& items, StyleRef style, LayoutOverride&& constraints = {});
```

Both also have a form without the style. They use the classes `collection-area`,
`collection-header`, `collection-header-cell`, `collection-header-caption`,
`collection-header-type`, `collection-entry`, `collection-value`, `collection-remove`
and `collection-add-entry`.

---

[Previous: Buttons and toggles](buttons-and-toggles.md) | [Controls](../controls.md) | [Next: Sliders](sliders.md)
