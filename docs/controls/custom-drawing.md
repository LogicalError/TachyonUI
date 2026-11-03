# Custom drawing

Any element can draw its own content with a callback at render time. Register the element
with `RegisterElement`, then attach the callback with `elementStorage.AddCustomDraw`:

```cpp
ElementRef RegisterElement(Element&& element, LayoutOverride&& constraints);
ElementRef RegisterElement(Element&& element, std::string_view text, LayoutOverride&& constraints);

// ElementStorage:
ElementStorageResult AddCustomDraw(const ElementRef& ref, CustomDrawCallback callback);
ElementStorageResult AddCustomDrawAfter(const ElementRef& ref, CustomDrawCallback callback);
ElementStorageResult AddPostLayout(const ElementRef& ref, PostLayoutCallback callback);
```

`AddCustomDraw` runs before the element's children are drawn. `AddCustomDrawAfter` runs
after the whole subtree, so call it once the element's children are closed.
`AddPostLayout` runs after layout and before rendering. It can't draw, but it sees the
final geometry, so use it for state that depends on where things ended up.

The callback gets a `CustomDrawContext`. By default you draw in the element's local
space, where (0, 0) is the top-left corner of its padding box and any pan or zoom on its
ancestors applies. `SetSpace(DrawSpace::Screen)` switches to absolute layout pixels, and
`ScreenToCurrent(screenPos)` converts a cursor position into the current space.

| Method | Draws or returns |
| --- | --- |
| `ElementRect()`, `ElementInnerRect()` | The element's border box and padding box, in the current space |
| `GetElementRect(ref)`, `GetInnerRect(ref)` | Another element's rects |
| `RenderBox(rect, boxStyle, state, ...)` | A styled box |
| `RenderQuad(rect, colorStyle, cornerRadius, ...)` | A filled rectangle |
| `RenderLine(p0, p1, lineStyle)` | A line (there's also a four-point form) |
| `RenderPolyline(points, closed, lineStyle, cornerRadius)` | A polyline |
| `RenderCircle`, `RenderDisc`, `RenderDot` | A circle outline, a filled disc and a dot. The circle is exactly round, and gradients and dashes run clockwise from the right. |
| `RenderTriangle`, `RenderFilledPolygon` | A filled triangle and a convex polygon. A triangle's `hardEdges` (edges it shares with another triangle) get no soft edge, so no seam shows. |
| `RenderText(textId, rect)` | Text registered with `textSystem->RegisterText` |

Colors are style indices, such as `theme.lineWhiteStyle` or one you registered with
`styleManager.RegisterColorStyle`. A `LineStyle` has `colorStyleIndex`, `lineWidth` and
dash settings.

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

The callback runs at render time, after `OnGUI` has returned, so anything it captures
has to outlive the frame.

---

[Previous: Other controls](other-controls.md) | [Controls](../controls.md) | [Next: Default CSS classes](css-classes.md)
