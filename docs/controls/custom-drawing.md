# Custom drawing

Register the element with `RegisterElement`, then attach a render-time callback with
`elementStorage.AddCustomDraw`:

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

The callback gets a `CustomDrawContext`. Default space is element-local: (0, 0) is the
top-left of the padding box, and ancestors' pan and zoom apply.
`SetSpace(DrawSpace::Screen)` switches to absolute layout pixels;
`ScreenToCurrent(screenPos)` converts a cursor position to the current space.

| Method | Draws or returns |
| --- | --- |
| `ElementRect()`, `ElementInnerRect()` | Border box and padding box, in the current space |
| `GetElementRect(ref)`, `GetInnerRect(ref)` | Another element's rects |
| `RenderBox(rect, boxStyle, state, ...)` | Styled box |
| `RenderQuad(rect, colorStyle, cornerRadius, ...)` | Filled rectangle |
| `RenderLine(p0, p1, lineStyle)` | Line (also a four-point form) |
| `RenderPolyline(points, closed, lineStyle, cornerRadius)` | Polyline |
| `RenderCircle`, `RenderDisc`, `RenderDot` | Circle outline, filled disc, dot. The circle is exactly round; gradients and dashes run clockwise from the right. |
| `RenderTriangle`, `RenderFilledPolygon` | Filled triangle, convex polygon. `hardEdges` (edges shared with another triangle) get no soft edge, so no seam shows. |
| `RenderText(textId, rect)` | Text registered with `textSystem->RegisterText` |

Colors are style indices, e.g. `theme.lineWhiteStyle` or one from
`styleManager.RegisterColorStyle`. `LineStyle` has `colorStyleIndex`, `lineWidth` and dash
settings.

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

The callback runs after `OnGUI` returns: anything it captures must outlive the frame.

---

[Previous: Other controls](other-controls.md) | [Controls](../controls.md) | [Next: Default CSS classes](css-classes.md)
