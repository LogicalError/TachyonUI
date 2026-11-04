# Fields

A field edits a value of yours. You pass the value by reference, and the field writes the
user's changes into it.

- `Field` is overloaded on the value type: text, numbers, vectors, lists and
  dictionaries each get their own form.
- Every form returns `true` when the value changed.

## Text fields

```cpp
bool Field(ID id, std::string& value, TextEditBehavior textEditBehavior, StyleRef style,
           LayoutOverride&& constraints = {});
```

`TextEditBehavior` says how the field edits. Its flags combine with `|`:

| Flag | Effect |
| --- | --- |
| `ContinuousUpdate` (default) | Updates `value` on every keystroke, not only on commit |
| `Multiline` | Enter adds a newline instead of committing |
| `AcceptTabs` | Tab inserts a tab instead of moving focus |
| `ReadOnly` | Text can be selected and copied, not edited |
| `None` | Updates on commit (Enter or focus loss) |

Class: `textbox`. In a toolbar: `toolbar-textbox`.

```cpp
Field(name);
Field(notes, TextEditBehavior::ContinuousUpdate | TextEditBehavior::Multiline);
```

```cpp
if (Field(objectName, TextEditBehavior::None))   // only on Enter or focus loss
    RenameObject(objectName);

Field(logText, TextEditBehavior::Multiline | TextEditBehavior::ReadOnly);
```

## SearchField

A text field with an icon at the start, inside the border.

```cpp
bool SearchField(ID id, std::string& value, IconID icon, TextEditBehavior textEditBehavior,
                 StyleRef style, LayoutOverride&& constraints = {});
```

```cpp
if (SearchField(filter, searchIcon))   // member: IconID searchIcon
    filterDirty = true;
```

Classes: `search-textbox`, `search-textbox-icon`, `search-textbox-field`.

## AutocompleteField

A text field with a suggestion list below it while it's focused.

```cpp
bool AutocompleteField(ID id, std::string& value, std::span<const std::string_view> suggestions,
                       StyleRef style, LayoutOverride&& constraints = {});
```

- The list shows up to 12 suggestions that contain the typed text, ignoring case.
  - It's empty if the only match equals the typed text.
- Up / Down highlight a suggestion.
- Enter or a click puts the suggestion into `value`.
  - Nothing is highlighted until Down, so Enter right away keeps the typed text.
- Escape, picking, or clicking elsewhere closes the list. Typing opens it again.
- Returns `true` when `value` changed, by typing or by picking.

Classes, the same as in command search:

- the field: `textbox`
- the list: `command-search-list`
- its rows: `menu-item-row`
- the highlighted row: `command-search-current-row`

```cpp
static constexpr std::string_view kFruits[] = { "Apple", "Banana", "Cherry" };
AutocompleteField(fruit, kFruits);
```

```cpp
if (AutocompleteField(fruit, kFruits))   // typed, or a suggestion picked
    UpdateRecipe();
```

## Dropdown

Shows the selected option and opens the option list below it.

```cpp
bool Dropdown(ID id, int& selected, std::span<const std::string_view> options, StyleRef style,
              LayoutOverride&& constraints = {});
bool Dropdown(int& selected, std::initializer_list<std::string_view> options,
              LayoutOverride&& constraints = {});
```

- Picking an option, by a click or by the arrows and Enter:
  - sets `selected`
  - returns `true` that frame
  - the field shows the new option from the next frame
- Escape or clicking elsewhere closes the list without a change.
- The keyboard starts on the current option.
- A `selected` out of range shows an empty field.

Classes: `menu-trigger`. The popup: `menu`, with rows `menu-item-row`.

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

- Shorter overloads leave out the behavior, the style or the config.
- Types: `float`, `double`, and 8- to 64-bit signed and unsigned integers.

The field shows an editable number:

- Drag its label past the drag distance to change the value.
  - Ctrl drags slower.
  - Shift drags faster.
  - Escape restores the value.
- `NumericFieldConfig` sets the range and the drag speed:
  - `minVal` and `maxVal`: the lowest and highest value
  - `stepPerPixel`: the change per pixel dragged
- NaN shows in a warning color.

Class: `number-field`. In a toolbar: `toolbar-number-field`.

```cpp
Field(floatValue);
Field(intValue, NumericFieldConfig<int>{ .minVal = 0, .maxVal = 10 });
```

```cpp
DragLabel("Angle");
Field(angle, NumericFieldConfig<float>{ .stepPerPixel = 0.5, .minVal = 0.0f, .maxVal = 360.0f });
```

## Vector fields

`Field` on a vector type gives one number per component. Each has a colored axis label
that you can drag.

- Types, with components X, Y, Z and W:
  - `int2`-`int4`, `uint2`-`uint4`
  - `short2`-`short4`, `ushort2`-`ushort4`
  - `byte2`-`byte4`
  - `float2`-`float4`
- Types with components R, G, B and A: `color4b`, `color4f`.
- The config applies to each component.

```cpp
DragLabel("Position");
Field(position);   // float3
```

```cpp
DragLabel("Scale");
Field(scale, NumericFieldConfig<float>{ .minVal = 0.0f });   // float3; no component below 0
```

Classes: `vector-component`. The labels: `axis-label-x`, `axis-label-y`, `axis-label-z`,
`axis-label-w`.

## TypedField

A number field with its type after the value, for example "0.5  f32".

```cpp
template<typename T> bool TypedField(ID id, T& value, LayoutOverride&& constraints = {});
```

```cpp
DragLabel("Speed");
TypedField(speed);   // float, shown as f32
```

Classes: `typed-field`, `typed-field-input`, `typed-field-type`.

## Lists

`Field` on a `std::vector<T>` gives a list you can reorder. `T` is any type `Field`
takes.

- Each row has a grip, the index, the field and a remove button.
  - Numbers show their type.
- An "Add item" button follows the rows.
- Keep the ID the same every frame, or the reorder state is lost.
  - Make it once with `idManager.CreateStaticID()`.
- Alt+Up / Down moves the focused row. Focus follows it.

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

Classes:

- around the list: `collection-area`
- inside: `collection-list`, `collection-row`, `collection-index`, `collection-remove`,
  `collection-add`

## Dictionaries

A key / value list with `std::string` keys:

- a header with the types
- one row per entry, with the key, the value and a remove button
- an "Add entry" button

Details:

- A vector of pairs keeps the row order.
- An `unordered_map` can change its order when it rehashes.
- A renamed key commits on Enter or focus loss.
- Renaming onto a key that already exists is ignored.

```cpp
template<typename V>
void Field(ID id, std::vector<std::pair<std::string, V>>& items, StyleRef style, LayoutOverride&& constraints = {});
template<typename V>
void Field(ID id, std::unordered_map<std::string, V>& items, StyleRef style, LayoutOverride&& constraints = {});
```

```cpp
Label("Stats", std::to_string(stats.size()) + " entries");
Field(stats, LayoutOverride{}.FlexGrow(1));   // member: std::vector<std::pair<std::string, float>> stats
```

```cpp
Field(counts, LayoutOverride{}.FlexGrow(1));   // member: std::unordered_map<std::string, int> counts
```

Both also come without the style. Classes: `collection-area`, `collection-header`,
`collection-header-cell`, `collection-header-caption`, `collection-header-type`,
`collection-entry`, `collection-value`, `collection-remove`, `collection-add-entry`.

---

[Previous: Buttons and toggles](buttons-and-toggles.md) | [Controls](../controls.md) | [Next: Sliders](sliders.md)
