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

        BeginHorizontalWrap();
        Button(idManager.AllocateDynamicID("sketch-clear"), "Clear", [this] { sketchStrokes.clear(); });
        Label(idManager.AllocateDynamicID("sketch-pointer"),
              std::format("{}   Pressure {:.2f}   Tilt {:.0f}, {:.0f}   Rotation {:.0f}", PointerName(pen), pen.pressure,
                          pen.tilt.x, pen.tilt.y, pen.rotation));
        Label(idManager.AllocateDynamicID("sketch-axes"),
              std::format("Distance {:.2f}   Slider {:.2f}   Barrel pressure {:.2f}   Buttons {}{}{}", pen.distance,
                          pen.slider, pen.tangentialPressure, pen.buttons & 1 ? "1" : "-", pen.buttons & 2 ? "2" : "-",
                          pen.touching ? "   Touching" : ""));
        EndHorizontal();

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
            if (sketchDrag.Press(e, elementStorage, MouseButtons::Left, /*atOnce*/ true))
            {
                if (!sketchPen.eraser)
                    sketchStrokes.push_back({});
            }
            else if (e.type == EventType::MouseUp)
            {
                sketchDrag.Release(e);
                return true;
            }
            else if (!sketchDrag.Move(e, 0))
                return e.type == EventType::MouseMove;

            if (sketchPen.eraser)
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
                // Where the eraser end would take strokes out.
                if (hovered && sketchPen.eraser)
                    ctx.RenderCircle(sketchCursor, kEraserRadius * styleManager.Px(1.0f),
                                     LineStyle{ .colorStyleIndex = theme.accentColor, .lineWidth = styleManager.Px(1.0f) });
            }));
        }
        EndVertical();
    }
}
