# Text and images

## Text

Non-interactive text. Default class: `static-text`.

```cpp
void Text(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
```

```cpp
Text("Hello from TachyonUI!");
Text("Selected object " + std::to_string(selected));
```

## Label

A label for the next control. In a label group it takes the group's width.

```cpp
void Label(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
void Label(ID id, std::string_view text, std::string_view caption, LayoutOverride&& constraints = {});
```

```cpp
Label("Name");
Field(name);
```

The caption form, for tall controls like lists, puts the label at the top of the row with a
caption below: `Label("Items", "3 items")`. Classes: `label`; caption form
`group-label-top`, `group-label-title`, `group-label-caption`.

## DragLabel

Dragging it sideways changes the numeric field after it. Default class: `drag-label`.

```cpp
void DragLabel(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
```

```cpp
DragLabel("Float");
Field(floatValue);
```

## SelectableText

Read-only rich text with links, mouse selection and Ctrl+C. Pass a span of `ContentRun`s
(text or icon). A run with `isLink` and a non-zero `contentId` is a link; clicking it calls
`onLinkActivate(contentId)`. Use a stable ID so the selection survives across frames.

```cpp
void SelectableText(ID id, const std::span<ContentRun>& runs,
                    std::function<void(intptr_t contentId)> onLinkActivate,
                    StyleRef style, LayoutOverride&& constraints = {});
```

No default style; you must pass one.

A link's `linkStyle` sets its color, hover color (also underlined on hover) and cursor
(pointer unless the style says otherwise). `theme.linkStyle` comes from the `.link` class
(`color`, plus `color` and `cursor` under `:hover`):

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

Pass `theme.linkStyle` again every frame so links follow a theme reload.

## Image

Draws an image registered with `graphicsContext->RegisterImage`, stretched to fill. Pass
`sourceSize` (pixels) to keep the aspect ratio.

```cpp
void Image(ID id, ImageIndex image, StyleRef style, LayoutOverride&& constraints = {});
void Image(ID id, ImageIndex image, uint2 sourceSize, StyleRef style, LayoutOverride&& constraints = {});
```

```cpp
Image(preview, LayoutOverride::FixedSize(96, 96));   // member: ImageIndex preview, from RegisterImage
Image(preview, uint2{ 64, 64 });   // a 64x64 source, aspect ratio kept
```

Default style: `theme.imageStyleRef` (built in code; CSS can't express the texture fill).

---

[Previous: Windows and layout](windows-and-layout.md) | [Controls](../controls.md) | [Next: Buttons and toggles](buttons-and-toggles.md)
