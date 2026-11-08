# Custom drawing

To draw your own shapes and text, register an element and attach a callback to it:

- An element is one box in the frame's layout tree. Every control is made of elements.
- `RegisterElement` adds an element and returns an `ElementRef`, a handle to it for this
  frame.
- `elementStorage.AddCustomDraw` attaches a callback that runs at render time, after
  layout, when the element's final size and position are known.

```cpp
ElementRef RegisterElement(Element&& element, LayoutOverride&& constraints);
ElementRef RegisterElement(Element&& element, std::string_view text, LayoutOverride&& constraints);

// ElementStorage:
ElementStorageResult AddCustomDraw(const ElementRef& ref, CustomDrawCallback callback);
ElementStorageResult AddCustomDrawAfter(const ElementRef& ref, CustomDrawCallback callback);
ElementStorageResult AddPostLayout(const ElementRef& ref, PostLayoutCallback callback);
```

| Function | Runs |
| --- | --- |
| `AddCustomDraw` | Before the element's children are drawn |
| `AddCustomDrawAfter` | After the whole subtree; call it once the children are closed |
| `AddPostLayout` | After layout, before rendering. Can't draw; sees final geometry |

`AddCustomDrawAfter` draws over the element's children, for example to strike them out:

```cpp
const ElementRef ref = RegisterElement(Element{ .id = panelID }, LayoutOverride{}.FlexGrow(1));
VERIFY(elementStorage.BeginChildren());
Text("No longer used");
VERIFY(elementStorage.EndChildren());   // close the children first
VERIFY(elementStorage.AddCustomDrawAfter(ref, [this](CustomDrawContext& ctx)
{
    const IntRect r = ctx.ElementRect();
    const LineStyle line{ .colorStyleIndex = theme.lineWhiteStyle, .lineWidth = styleManager.Px(1.0f) };
    ctx.RenderLine(r.position, r.position + r.size, line);   // drawn over the text
}));
```

`AddPostLayout` only reads the final geometry, for example to keep a size for the next
frame:

```cpp
VERIFY(elementStorage.AddPostLayout(ref, [this](PostLayoutContext& ctx)
{
    canvasSize = ctx.ElementInnerRect().size;   // member: int2 canvasSize
}));
```

The callback gets a `CustomDrawContext`:

- The default space is element-local:
  - (0, 0) is the top-left of the padding box
  - the pan and zoom of its ancestors apply
- `SetSpace(DrawSpace::Screen)` switches to absolute layout pixels.
- `ScreenToCurrent(screenPos)` converts a cursor position to the current space.

```cpp
Element canvas{ .id = canvasID, .styleRef = theme.GetStyle("my-canvas") };
canvas.eventHandler = [this](ElementEvent&& event)
{
    if (event.type == EventType::MouseMove)
        pointerScreenPos = event.state.screenPos;   // member: int2 pointerScreenPos
    return false;
};
const ElementRef ref = RegisterElement(std::move(canvas), LayoutOverride::FixedSize(200, 100));
VERIFY(elementStorage.AddCustomDraw(ref, [this](CustomDrawContext& ctx)
{
    ctx.RenderDisc(ctx.ScreenToCurrent(pointerScreenPos), styleManager.Px(4.0f), theme.lineWhiteStyle);
}));
```

| Method | Draws or returns |
| --- | --- |
| `ElementRect()`, `ElementInnerRect()` | Border box and padding box, in the current space |
| `GetElementRect(ref)`, `GetInnerRect(ref)` | Another element's rects |
| `RenderBox(rect, boxStyle, state, ...)` | Styled box |
| `RenderQuad(rect, colorStyle, cornerRadius, image, uv)` | Filled rectangle; with an image and `theme.imageFillColorStyle`, the part `uv` of the image stretched over it |
| `RenderLine(p0, p1, lineStyle)` | Line (also a four-point form) |
| `RenderPolyline(points, closed, lineStyle, cornerRadius)` | Polyline |
| `RenderCircle`, `RenderDisc`, `RenderDot` | Circle outline, filled disc, dot. The circle is exactly round; gradients and dashes run clockwise from the right. |
| `RenderTriangle`, `RenderFilledPolygon` | Filled triangle, convex polygon. `hardEdges` (edges shared with another triangle) get no soft edge, so no seam shows. |
| `RenderText(textId, rect)` | Text registered with `textSystem->RegisterText` |

Colors are style indices:

- for example `theme.lineWhiteStyle`
- or one from `styleManager.RegisterColorStyle`
- `LineStyle` has `colorStyleIndex`, `lineWidth` and dash settings

```cpp
const ElementRef ref = RegisterElement(Element{ .id = canvasID, .styleRef = theme.GetStyle("my-canvas") },
                                       LayoutOverride::FixedSize(200, 100));
VERIFY(elementStorage.AddCustomDraw(ref, [this](CustomDrawContext& ctx)
{
    const IntRect r = ctx.ElementInnerRect();
    const LineStyle line{ .colorStyleIndex = theme.lineWhiteStyle, .lineWidth = styleManager.Px(2.0f) };
    ctx.RenderLine(r.position, r.position + r.size, line);
}));
```

An image, or part of one, is a quad with an image and a texture color style:

- `theme.imageFillColorStyle` stretches the image over the quad.
- `uv`, a `UVRect`, picks the part: the whole image by default. See
  [Image](text-and-images.md#image) for UV and `UVRect::FromPixels`.
- `display`, an `ImageDisplay`, says how it's shown: a tint, its channels, sRGB-encoded.
  See [Image](text-and-images.md#image). The tint applies to a quad of any color.

```cpp
VERIFY(elementStorage.AddCustomDraw(ref, [this](CustomDrawContext& ctx)
{
    // The 2D view's visible part of the texture, panned and zoomed, filling the element.
    const IntRect r = ctx.ElementInnerRect();
    ctx.RenderQuad(IntRect{ 0, 0, r.w, r.h }, theme.imageFillColorStyle, 0.0f, texture, visibleUV);
}));
```

The callback runs after `OnGUI` returns, so anything it captures must outlive the frame.

---

[Previous: Other controls](other-controls.md) | [Controls](../controls.md) | [Next: Default CSS classes](css-classes.md)
