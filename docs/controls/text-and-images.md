# Text and images

## Text

Text the user can read but not click, select or edit. Its default CSS class is
`static-text`.

```cpp
void Text(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
```

```cpp
Text("Hello from TachyonUI!");
Text("Selected object " + std::to_string(selected));
```

To change its look, pass a style made from your own CSS class:

```cpp
Text("Unsaved changes", theme.GetStyle("warning-text"));
```

## Label

A label names the control that comes after it, such as "Name" before a text field.

- Inside a [label group](windows-and-layout.md#label-groups), all labels take the
  group's width, so the controls after them line up.

```cpp
void Label(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
void Label(ID id, std::string_view text, std::string_view caption, LayoutOverride&& constraints = {});
```

```cpp
Label("Name");
Field(name);
```

The caption form is for tall controls, such as lists.

- It puts the label at the top of the row.
- A caption, a second line of smaller text, goes below the label.

```cpp
Label("Items", "3 items");
```

The CSS classes:

- `label`
- in the caption form, `group-label-top`, `group-label-title` and `group-label-caption`

## DragLabel

A label the user can drag sideways to change the number field after it, as an
alternative to typing a value. Its default CSS class is `drag-label`.

```cpp
void DragLabel(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
```

```cpp
DragLabel("Float");
Field(floatValue);
```

## SelectableText

Rich text the user can read, select and copy, but not edit.

- It can hold links.
- The user selects text with the mouse and copies it with Ctrl+C.
- You pass it a span of `ContentRun`s. Each run is a piece of text or an icon.
- A run with `isLink` set and a non-zero `contentId` is a link. Clicking it calls
  `onLinkActivate(contentId)`.
- Give it a stable ID, so the selection is kept from frame to frame. See
  [Static IDs](../overview/ids.md#static-ids).

```cpp
void SelectableText(ID id, const std::span<ContentRun>& runs,
                    std::function<void(intptr_t contentId)> onLinkActivate,
                    StyleRef style, LayoutOverride&& constraints = {});
```

It has no default style: you must pass one.

A link's `linkStyle` sets:

- its color
- its color under the pointer, when it's also underlined
- the pointer's cursor over it: the hand, unless the style says otherwise

`theme.linkStyle` comes from the `.link` class: its `color`, plus the `color` and `cursor`
under `:hover`.

```cpp
ContentRun runs[] =
{
    { .type = ContentRun::Text, .text = "See " },
    { .type = ContentRun::Text, .text = "the docs", .contentId = 1, .isLink = true, .linkStyle = theme.linkStyle },
};
```

```cpp
SelectableText(docsTextID, runs, [this](intptr_t contentId) { if (contentId == 1) showDocs = true; },
               theme.staticTextStyleRef);   // member: ID docsTextID = idManager.CreateStaticID()
```

Pass `theme.linkStyle` again every frame, so the links follow a theme reload.

## Image

Draws an image, stretched to fill its box.

- Register the image first with `graphicsContext->RegisterImage`. It gives you an
  `ImageIndex`.
- Pass `sourceSize`, the image's size in pixels, to keep its aspect ratio.
- `graphicsContext->UnregisterImage` frees its slot for another image.
- Registered images share their slots with the fonts, icons and gradients: 4096 by
  default.
  - For more (or fewer), set `InitializeOptions::imageSlots` before `Initialize`.
  - A device that can't sample that many in one shader stage gives fewer, and a warning
    is logged. `graphicsContext->GetImageSlotCount()` says how many there are.
  - `RegisterImage` returns `ImageIndex::Invalid` once they're all taken.

  ```cpp
  tachyon::GUIApplication::InitializeOptions options;
  options.imageSlots = 16384;   // thumbnails for a big library
  app.Initialize(options);
  ```

```cpp
void Image(ID id, ImageIndex image, StyleRef style, LayoutOverride&& constraints = {});
void Image(ID id, ImageIndex image, uint2 sourceSize, StyleRef style, LayoutOverride&& constraints = {});
```

```cpp
Image(preview, LayoutOverride::FixedSize(96, 96));   // member: ImageIndex preview, from RegisterImage
Image(preview, uint2{ 64, 64 });   // a 64x64 source, aspect ratio kept
```

The default style is `theme.imageStyleRef`. It's built in code, since CSS can't express
how the image fills the box.

---

[Previous: Windows and layout](windows-and-layout.md) | [Controls](../controls.md) | [Next: Buttons and toggles](buttons-and-toggles.md)
