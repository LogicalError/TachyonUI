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
- its color under the pointer
- the pointer's cursor over it: the hand, unless the style says otherwise
- its underline at rest (`underline`): thickness and offset under the text in CSS px, solid or dotted
  - a thickness of 0 underlines the link only while the pointer is over it

`theme.linkStyle` comes from the `.link` class: its `color`, its `text-underline-*` properties, plus the
`color` and `cursor` under `:hover`.

- The default theme underlines links at rest: 1px, 3px under the text.
- `theme.internalLinkStyle` is for a link that stays inside the app (opens a panel, selects an object).
  - It comes from the `.link-internal` class: the same colors, a dotted underline.
  - A theme without that class uses `theme.linkStyle` for both.

```cpp
{ .type = ContentRun::Text, .text = "the source", .contentId = 2, .isLink = true, .linkStyle = theme.internalLinkStyle },
```

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

### A run's own look

Rich text drawn with `ITextSystem::RegisterRichText` can give each text run its own look:

- `textStyleOverride`: a whole `TextStyle` in place of the element's (font, size, weight, italic)
  - you own it: keep it alive as long as the text, a `static` for example
- `color`: the run's text color in place of the element's
  - alpha 0, the default, keeps the element's color
  - a link takes its `linkStyle`'s colors instead
- `background`: a box with rounded corners under the run, in this color
  - for inline code on a well, say
  - alpha 0, the default, draws none

```cpp
static const TextStyle bold { .fontSize = 12.0f, .weight = FontWeight::SemiBold };
static const TextStyle code { .fontSize = 11.0f, .family = FontFamily::Monospace };
const color4b text1 = ToBytes(theme.GetColor("--text-1"));   // your color4f -> color4b helper
const color4b well  = ToBytes(theme.GetColor("--bg-well"));

ContentRun runs[] =
{
    { .type = ContentRun::Text, .text = "Bold", .textStyleOverride = &bold, .color = text1 },
    { .type = ContentRun::Text, .text = ", then " },
    { .type = ContentRun::Text, .text = " inline code ", .textStyleOverride = &code, .background = well },
};
```

`SelectableText` keeps the links of its runs; the other looks above are for text drawn with
`RegisterRichText`.

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

**How it's shown.** Pass an `ImageDisplay` to change how the image looks, without changing
the image. It works in this order:

1. Each channel shown (`red`, `green`, `blue`, `alpha`) is taken from one of the image's
   channels, or is a constant (`ImageChannel::Zero`, `ImageChannel::One`).
   - `ImageDisplay::Channel(ImageChannel::Alpha)` shows the alpha as grey, opaque: a mask.
   - The same with `Red` shows a roughness map packed in the red channel.
   - `display.alpha = ImageChannel::One` shows an image without its transparency.
2. With `srgbEncode`, the color is sRGB-encoded: for an image that holds linear values,
   such as a normal map or a render target, which would look too dark otherwise.
3. The color is multiplied by `tint`.

The default `ImageDisplay{}` shows the image as it is.

```cpp
void Image(ID id, ImageIndex image, const ImageDisplay& display, LayoutOverride&& constraints = {});
void Image(ID id, ImageIndex image, uint2 sourceSize, const ImageDisplay& display, LayoutOverride&& constraints = {});
void Image(ID id, ImageIndex image, const UVRect& uv, const ImageDisplay& display, StyleRef style, LayoutOverride&& constraints = {});
void Image(ID id, ImageIndex image, uint2 sourceSize, const UVRect& uv, const ImageDisplay& display, StyleRef style, LayoutOverride&& constraints = {});
```

```cpp
// A texture's alpha, as a grey mask.
Image(maskID, texture, ImageDisplay::Channel(ImageChannel::Alpha), LayoutOverride::FixedSize(96, 96));

// A linear render target, encoded for the screen and dimmed.
ImageDisplay preview;
preview.srgbEncode = true;
preview.tint = color4f{ 0.5f, 0.5f, 0.5f, 1.0f };
Image(previewID, renderTarget, uint2{ 512, 256 }, preview);
```

The default style is `theme.imageStyleRef`. It's built in code, since CSS can't express
how the image fills the box.

---

[Previous: Windows and layout](windows-and-layout.md) | [Controls](../controls.md) | [Next: Buttons and toggles](buttons-and-toggles.md)
