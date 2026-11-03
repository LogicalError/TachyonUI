# Text and images

## Text

Plain text the user can't interact with.

```cpp
void Text(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
```

Default class: `static-text`.

## Label

A label for the control that follows it. Inside a label group, it takes the group's width.

```cpp
void Label(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
void Label(ID id, std::string_view text, std::string_view caption, LayoutOverride&& constraints = {});
```

Use the second form next to a tall control such as a list. It puts the label at the top of
the row with a caption underneath, for example `Label("Items", "3 items")`. The default
class is `label`. The caption form uses `group-label-top`, `group-label-title` and
`group-label-caption`.

## DragLabel

A label that changes the value of the numeric field after it when you drag it sideways.

```cpp
void DragLabel(ID id, std::string_view text, StyleRef style, LayoutOverride&& constraints = {});
```

Default class: `drag-label`.

```cpp
DragLabel("Float");
Field(floatValue);
```

## SelectableText

Read-only rich text that supports links, selecting with the mouse and copying with Ctrl+C.
You pass the text as a span of `ContentRun`s, each of which is either text or an icon. A
run with `isLink` set and a non-zero `contentId` is a link. When the user clicks a link,
`onLinkActivate` gets its `contentId`. Give the control a stable ID so the selection
survives from one frame to the next.

```cpp
void SelectableText(ID id, const std::span<ContentRun>& runs,
                    std::function<void(intptr_t contentId)> onLinkActivate,
                    StyleRef style, LayoutOverride&& constraints = {});
```

There's no default style, so you have to pass one.

A link run's look and behavior come from its `linkStyle`. That sets its color, its hover
color (the link is also underlined on hover) and the cursor over it, which is the pointer
unless the style says otherwise. `theme.linkStyle` holds the theme's link style, taken
from the `.link` class (`color`, plus `color` and `cursor` under `:hover`):

```cpp
ContentRun runs[] =
{
    { .type = ContentRun::Text, .text = "See " },
    { .type = ContentRun::Text, .text = "the docs", .contentId = 1, .isLink = true, .linkStyle = theme.linkStyle },
};
```

A run keeps whatever style you gave it, so pass `theme.linkStyle` again every frame. That
way the links pick up the new theme after a theme reload.

## Image

Draws an image that you registered with `graphicsContext->RegisterImage`, stretched to
fill the element. If you pass `sourceSize` (the image's size in pixels), it keeps the
aspect ratio instead.

```cpp
void Image(ID id, ImageIndex image, StyleRef style, LayoutOverride&& constraints = {});
void Image(ID id, ImageIndex image, uint2 sourceSize, StyleRef style, LayoutOverride&& constraints = {});
```

The default style is `theme.imageStyleRef`. It's built in code because CSS has no way to
express the texture fill.

---

[Previous: Windows and layout](windows-and-layout.md) | [Controls](../controls.md) | [Next: Buttons and toggles](buttons-and-toggles.md)
