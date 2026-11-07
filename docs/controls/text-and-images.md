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
void Image(ID id, ImageIndex image, const UVRect& uv, StyleRef style, LayoutOverride&& constraints = {});
void Image(ID id, ImageIndex image, uint2 sourceSize, const UVRect& uv, StyleRef style, LayoutOverride&& constraints = {});
```

```cpp
Image(preview, LayoutOverride::FixedSize(96, 96));   // member: ImageIndex preview, from RegisterImage
Image(preview, uint2{ 64, 64 });   // a 64x64 source, aspect ratio kept
```

**Part of an image.** Pass a `UVRect` to draw only part of it, stretched over the box.

- UV runs from 0,0 at the image's top-left corner to 1,1 at its bottom-right.
  - `UVRect{}` is the whole image.
  - `UVRect{ { 0.5f, 0.0f }, { 1.0f, 1.0f } }` is its right half.
  - A `min` past `max` flips it.
- `UVRect::FromPixels(cell, imageSize)` turns a rect in pixels into UV, for example a
  cell of a thumbnail atlas: many thumbnails in one image take one image slot.

  ```cpp
  // The thumbnail in column 2, row 1 of a 1024x1024 atlas of 128x128 cells.
  const UVRect cell = UVRect::FromPixels(IntRect{ 2 * 128, 1 * 128, 128, 128 }, uint2{ 1024, 1024 });
  Image(atlas, cell, LayoutOverride::FixedSize(64, 64));
  ```

- It also zooms and pans a big image without drawing it bigger than its box.

  ```cpp
  // Zoomed in 4x around (cx, cy), in UV.
  Image(photo, UVRect{ { cx - 0.125f, cy - 0.125f }, { cx + 0.125f, cy + 0.125f } }, LayoutOverride::FixedSize(256, 256));
  ```

- With `sourceSize`, the image's size in pixels, the box takes the aspect of the part
  shown: a quarter of a 200x100 image's width is 50x100.
- The part fills the box's padding box, over a clear background, and its border shows
  as usual.

The default style is `theme.imageStyleRef`. It's built in code, since CSS can't express
how the image fills the box.

---

[Previous: Windows and layout](windows-and-layout.md) | [Controls](../controls.md) | [Next: Buttons and toggles](buttons-and-toggles.md)
