# Fields

`Field` is overloaded on the value type. Every form returns `true` when the value changed.

## Text fields

```cpp
bool Field(ID id, std::string& value, TextEditBehavior textEditBehavior, StyleRef style,
           LayoutOverride&& constraints = {});
```

`TextEditBehavior` flags combine with `|`:

| Flag | Effect |
| --- | --- |
| `ContinuousUpdate` (default) | Updates `value` on every keystroke, not only on commit |
| `Multiline` | Enter adds a newline instead of committing |
| `AcceptTabs` | Tab inserts a tab instead of moving focus |
| `ReadOnly` | Text can be selected and copied, not edited |
| `None` | Updates on commit (Enter or focus loss) |

Class: `textbox` (`toolbar-textbox` in a toolbar).

```cpp
Field(name);
Field(notes, TextEditBehavior::ContinuousUpdate | TextEditBehavior::Multiline);
```

## SearchField

Text field with an icon at the start, inside the border.

```cpp
bool SearchField(ID id, std::string& value, IconID icon, TextEditBehavior textEditBehavior,
                 StyleRef style, LayoutOverride&& constraints = {});
```

Classes: `search-textbox`, `search-textbox-icon`, `search-textbox-field`.

## AutocompleteField

Text field with a suggestion list below it while focused.

```cpp
bool AutocompleteField(ID id, std::string& value, std::span<const std::string_view> suggestions,
                       StyleRef style, LayoutOverride&& constraints = {});
```

- Shows up to 12 suggestions containing the typed text, case-insensitive. Empty if the
  only match equals the typed text.
- Up / Down highlight; Enter or click puts the suggestion into `value`. Nothing is
  highlighted until Down, so Enter right away keeps the typed text.
- Escape, picking, or clicking elsewhere closes the list; typing reopens it.
- Returns `true` when `value` changed, by typing or picking.

Classes: `textbox`; list `command-search-list`, `menu-item-row`,
`command-search-current-row` (as in command search).

```cpp
static constexpr std::string_view kFruits[] = { "Apple", "Banana", "Cherry" };
AutocompleteField(fruit, kFruits);
```

## Dropdown

Shows the selected option; opens the option list below it.

```cpp
bool Dropdown(ID id, int& selected, std::span<const std::string_view> options, StyleRef style,
              LayoutOverride&& constraints = {});
bool Dropdown(int& selected, std::initializer_list<std::string_view> options,
              LayoutOverride&& constraints = {});
```

- Picking (click, or arrows and Enter) sets `selected` and returns `true` that frame. The
  field shows the new option from the next frame.
- Escape or clicking elsewhere closes without change.
- Keyboard starts on the current option.
- `selected` out of range shows an empty field.

Classes: `menu-trigger`; popup `menu`, rows `menu-item-row`.

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

Shorter overloads omit behavior, style or config. Types: `float`, `double`, 8- to 64-bit
signed and unsigned integers.

- Editable number; drag its label (past the drag distance) to change it. Ctrl drags
  slower, Shift faster, Escape restores the value.
- `NumericFieldConfig`: `minVal`, `maxVal`, `stepPerPixel` (change per pixel dragged).
- NaN shows in a warning color.

Class: `number-field` (`toolbar-number-field` in a toolbar).

```cpp
Field(floatValue);
Field(intValue, NumericFieldConfig<int>{ .minVal = 0, .maxVal = 10 });
```

## Vector fields

`Field` on a vector type gives one number per component, each with a draggable colored
axis label. Types: `int2`-`int4`, `uint2`-`uint4`, `short2`-`short4`, `ushort2`-`ushort4`,
`byte2`-`byte4`, `float2`-`float4` (X, Y, Z, W), `color4b`, `color4f` (R, G, B, A). The
config applies per component.

```cpp
DragLabel("Position");
Field(position);   // float3
```

Classes: `vector-component`; labels `axis-label-x`, `axis-label-y`, `axis-label-z`,
`axis-label-w`.

## TypedField

Number field with its type after the value, e.g. "0.5  f32".

```cpp
template<typename T> bool TypedField(ID id, T& value, LayoutOverride&& constraints = {});
```

Classes: `typed-field`, `typed-field-input`, `typed-field-type`.

## Lists

`Field` on a `std::vector<T>` (any `T` that `Field` takes) gives a reorderable list. Rows:
grip, index, field (numbers show their type), remove button; then an "Add item" button.

- Keep the ID the same every frame, or the reorder state is lost.
- Alt+Up / Down moves the focused row; focus follows.

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

Classes: `collection-area` (around), `collection-list`, `collection-row`,
`collection-index`, `collection-remove`, `collection-add`.

## Dictionaries

Key / value list with `std::string` keys: a header with the types, one row per entry
(key, value, remove button), and an "Add entry" button.

- Vector of pairs keeps row order; `unordered_map` order can change on rehash.
- A renamed key commits on Enter or focus loss. Renaming onto an existing key is ignored.

```cpp
template<typename V>
void Field(ID id, std::vector<std::pair<std::string, V>>& items, StyleRef style, LayoutOverride&& constraints = {});
template<typename V>
void Field(ID id, std::unordered_map<std::string, V>& items, StyleRef style, LayoutOverride&& constraints = {});
```

Both also come without the style. Classes: `collection-area`, `collection-header`,
`collection-header-cell`, `collection-header-caption`, `collection-header-type`,
`collection-entry`, `collection-value`, `collection-remove`, `collection-add-entry`.

---

[Previous: Buttons and toggles](buttons-and-toggles.md) | [Controls](../controls.md) | [Next: Sliders](sliders.md)
