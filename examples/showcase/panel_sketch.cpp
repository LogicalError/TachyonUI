#include "application.h"

#include <algorithm>
#include <cmath>
#include <format>

//
// The Sketch panel: a pad to draw on with a pen, the line wider where it presses harder, and
// the pen's values as they come.
//

namespace tachyon
{
    namespace
    {
        // A line this wide at full pressure, in CSS pixels; a hair at none.
        constexpr float kMaxWidth = 14.0f;
        constexpr float kMinWidth = 1.0f;
        // The eraser end takes out what's this close, in CSS pixels.
        constexpr float kEraserRadius = 12.0f;
        // Where the pen would draw: a ring this wide across (10px), in CSS pixels.
        constexpr float kHoverRingRadius = 5.0f;
        // A new point at least this far from the last, in pixels.
        constexpr float kPointSpacing = 1.5f;

        int2 Pixel(float2 p) { return { int32_t(std::lround(p.x)), int32_t(std::lround(p.y)) }; }

        const char* PointerName(const PenState& pen)
        {
            if (!pen.IsPen()) return "Mouse";
            return pen.eraser ? "Pen (eraser)" : "Pen";
        }
    }

    void Application::EraseSketchAt(float2 at, float radius)
    {
        // A stroke loses the points under the eraser and splits where they were.
        std::vector<SketchStroke> kept;
        for (SketchStroke& stroke : sketchStrokes)
        {
            SketchStroke piece;
            for (const SketchPoint& p : stroke.points)
            {
                const float2 d = p.pos - at;
                if (d.x * d.x + d.y * d.y <= radius * radius)
                {
                    if (piece.points.size() > 1) kept.push_back(std::move(piece));
                    piece = {};
                    continue;
                }
                piece.points.push_back(p);
            }
            if (piece.points.size() > 1) kept.push_back(std::move(piece));
        }
        sketchStrokes = std::move(kept);
    }

    void Application::RenderSketchPanel()
    {
        const PenState& pen = sketchPen;
        BeginVertical(theme.emptyVertStyleRef, LayoutOverride{}.FlexGrow(1));

        // The tools (Pen and Eraser, one of them on) and Clear, then the pen's values as chips.
        BeginHorizontalWrap(idManager.AllocateDynamicID("sketch-toolbar"), sketchToolbarStyleRef);
        const StyleRef toolStyle = misc.iconButton[1];
        Toggle(idManager.AllocateDynamicID("sketch-pen"), !sketchEraserTool, GUIContent{ showcase.editIcon, {}, "Pen" },
               [this](bool) { sketchEraserTool = false; }, toolStyle);
        Toggle(idManager.AllocateDynamicID("sketch-eraser"), sketchEraserTool, GUIContent{ showcase.eraserIcon, {}, "Eraser" },
               [this](bool) { sketchEraserTool = true; }, toolStyle);
        Button(idManager.AllocateDynamicID("sketch-clear"), GUIContent{ theme.deleteIcon, {}, "Clear" },
               [this] { sketchStrokes.clear(); }, toolStyle);
        Text(idManager.AllocateDynamicID("sketch-divider"), "", stateMachineDividerStyleRef);

        // A chip: its name, then its value (if any) in monospace.
        auto chip = [this](std::string_view key, std::string_view label, std::string_view value)
        {
            BeginHorizontal(idManager.AllocateDynamicID(key), sketchChipStyleRef);
            Text(label, sketchChipLabelStyleRef);
            if (!value.empty())
                Text(value, sketchChipValueStyleRef);
            EndHorizontal();
        };
        chip("sketch-pointer",  PointerName(pen), {});
        chip("sketch-pressure", "Pressure",  std::format("{:.2f}", pen.pressure));
        chip("sketch-tilt",     "Tilt",      std::format("{:.0f}°, {:.0f}°", pen.tilt.x, pen.tilt.y));
        chip("sketch-rotation", "Rotation",  std::format("{:.0f}°", pen.rotation));
        chip("sketch-distance", "Distance",  std::format("{:.2f}", pen.distance));
        chip("sketch-slider",   "Slider",    std::format("{:.2f}", pen.slider));
        chip("sketch-barrel",   "Barrel",    std::format("{:.2f}", pen.tangentialPressure));
        chip("sketch-buttons",  "Buttons",   std::format("{} {}{}", pen.buttons & 1 ? "1" : "-", pen.buttons & 2 ? "2" : "-",
                                                         pen.touching ? ", touching" : ""));
        EndHorizontalWrap();

        const ID padID = idManager.AllocateDynamicID("sketch-pad");
        Element pad{ .id = padID, .styleRef = sketchPadStyleRef };
        pad.state = guiStateManager.GetCurrentState(padID);
        const bool hovered = (pad.state & ElementState::Hover) != ElementState::None;
        pad.eventHandler = [this](ElementEvent&& e) -> bool
        {
            if (e.type != EventType::MouseDown && e.type != EventType::MouseMove && e.type != EventType::MouseUp)
                return false;
            sketchPen    = e.state.pen;
            // The pointer in the pad's padding box, the space its drawing is in.
            const int2 local = e.LocalCursor(elementStorage) - e.rect->innerRect.position;
            sketchCursor = float2{ float(local.x), float(local.y) };
            const float scale = styleManager.Px(1.0f);
            const bool erasing = sketchPen.eraser || sketchEraserTool;
            if (sketchDrag.Press(e, elementStorage, MouseButtons::Left, /*atOnce*/ true))
            {
                if (!erasing)
                    sketchStrokes.push_back({});
            }
            else if (e.type == EventType::MouseUp)
            {
                sketchDrag.Release(e);
                return true;
            }
            else if (!sketchDrag.Move(e, 0))
                return e.type == EventType::MouseMove;

            if (erasing)
            {
                EraseSketchAt(sketchCursor, kEraserRadius * scale);
                return true;
            }
            if (sketchStrokes.empty())
                sketchStrokes.push_back({});
            std::vector<SketchPoint>& points = sketchStrokes.back().points;
            const float width = (kMinWidth + (kMaxWidth - kMinWidth) * std::clamp(sketchPen.pressure, 0.0f, 1.0f)) * scale;
            if (!points.empty())
            {
                const float2 d = sketchCursor - points.back().pos;
                if (d.x * d.x + d.y * d.y < kPointSpacing * kPointSpacing)
                {
                    points.back().width = std::max(points.back().width, width);
                    return true;
                }
            }
            points.push_back({ sketchCursor, width });
            return true;
        };
        const AddResult added = elementStorage.Add(std::move(pad), LayoutOverride{}.FlexGrow(1));
        VERIFY(added.status);
        if (added)
        {
            VERIFY(windowRegistry.Add(added.ref));
            VERIFY(elementStorage.AddCustomDraw(added.ref, [this, hovered](CustomDrawContext& ctx)
            {
                const VisualColorStyleIndex ink = theme.curveChannelColors[4];
                for (const SketchStroke& stroke : sketchStrokes)
                {
                    const std::vector<SketchPoint>& points = stroke.points;
                    if (points.size() == 1)
                        ctx.RenderDisc(Pixel(points[0].pos), points[0].width * 0.5f, ink);
                    for (size_t i = 1; i < points.size(); ++i)
                    {
                        const SketchPoint& a = points[i - 1];
                        const SketchPoint& b = points[i];
                        const float width = (a.width + b.width) * 0.5f;
                        ctx.RenderLine(Pixel(a.pos), Pixel(b.pos), LineStyle{ .colorStyleIndex = ink, .lineWidth = width });
                        // Round joins: a disc at each point hides the gaps between segments.
                        ctx.RenderDisc(Pixel(b.pos), b.width * 0.5f, ink);
                    }
                }
                // Where the pointer is: the eraser's reach, or a small ring where the pen would draw.
                if (hovered)
                {
                    const bool erasing = sketchPen.eraser || sketchEraserTool;
                    const float radius = erasing ? kEraserRadius : kHoverRingRadius;
                    ctx.RenderCircle(sketchCursor, radius * styleManager.Px(1.0f),
                                     LineStyle{ .colorStyleIndex = theme.accentColor, .lineWidth = styleManager.Px(erasing ? 1.0f : 1.5f) });
                }
            }));
        }
        EndVertical();
    }
}
