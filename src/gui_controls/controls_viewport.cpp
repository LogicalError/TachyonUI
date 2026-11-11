#include <gui_controls/gui_application.h>
#include <gui_controls/gui_viewport_types.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

// Viewport: the app's 3D scene, drawn by its onRender into an image the library keeps; the
// camera's mouse and keys; the move, rotate and scale gizmos over the selection; the tools
// down the left and the axes at the top right.

namespace tachyon
{
    namespace
    {
        constexpr float kPi = std::numbers::pi_v<float>;
        constexpr float kAxisGizmoRadius = 26.0f;   // CSS pixels: the axes at the top right
        constexpr float kAxisGizmoMargin = 40.0f;   // from the top right corner to their middle

        float3 AxisDir(int a) { return a == 0 ? float3{ 1, 0, 0 } : a == 1 ? float3{ 0, 1, 0 } : float3{ 0, 0, 1 }; }

        GizmoPart PartOfAxis(int a) { return a == 0 ? GizmoPart::X : a == 1 ? GizmoPart::Y : GizmoPart::Z; }

        // A fly key's bit in ViewportView::flyKeys; 0 for any other key.
        uint8_t FlyBit(KeyCode key)
        {
            switch (key)
            {
                case KeyCode::KeyW: return FlyForward;
                case KeyCode::KeyS: return FlyBack;
                case KeyCode::KeyA: return FlyLeft;
                case KeyCode::KeyD: return FlyRight;
                case KeyCode::KeyQ: return FlyDown;
                case KeyCode::KeyE: return FlyUp;
                default:            return 0;
            }
        }

        float2 Normalize2(float2 v)
        {
            const float length = std::hypot(v.x, v.y);
            return length > 0.0f ? v / length : float2{};
        }

        // Where axis `a`'s end is on the axes at the top right, round `center`.
        float2 AxisGizmoEnd(const ViewportCamera& camera, float2 center, float radius, float3 dir)
        {
            return center + float2{ Dot(dir, camera.Right()), -Dot(dir, camera.Up()) } * radius;
        }

        // The axis end on the top right axes under `pixel`: 0..2 for +x, +y, +z, 3..5 for the
        // negative ends; -1 for none.
        int HitAxisGizmo(const ViewportCamera& camera, float2 pixel, float2 center, float radius, float reach)
        {
            int best = -1;
            float bestDistance = reach;
            for (int i = 0; i < 6; ++i)
            {
                const float3 dir = AxisDir(i % 3) * (i < 3 ? 1.0f : -1.0f);
                const float2 end = AxisGizmoEnd(camera, center, radius, dir);
                const float d = std::hypot(pixel.x - end.x, pixel.y - end.y);
                if (d <= bestDistance) { bestDistance = d; best = i; }
            }
            return best;
        }
    }

    bool GUIApplication::Viewport(ID id, ViewportView& view, LayoutOverride&& constraints)
    {
        ViewportView* v = &view;
        const bool disabled = guiStateManager.Construction().IsDisabled();
        if (disabled && (v->gizmoDrag.IsHeld() || v->cameraDrag.IsHeld()))
        {
            if (v->gizmoDrag.IsHeld()) v->selection = v->gizmoDrag.before;
            v->gizmoDrag.Reset();
            v->cameraDrag.Reset();
            v->cameraMove = ViewportView::CameraMove::None;
        }
        const bool changed = view.revision != view.revisionSeen;
        view.revisionSeen = view.revision;

        // The scene draws every frame, so a cached panel holding the view is built again each
        // frame too: its image is asked for, and goes when it isn't.
        InvalidateCachedScopesAbove(id);

        // Flying: the camera moves while fly keys are held, by the time the frame took.
        if (v->cameraMove == ViewportView::CameraMove::Look)
            FlyCamera(*v, std::min(frameTime, 0.1f));
        else
            v->flyKeys = 0;

        const ElementState state = guiStateManager.GetCurrentState(id);
        if ((state & ElementState::Hover) == ElementState::None && !v->gizmoDrag.IsHeld())
            v->hover.Clear();

        // The app's scene, drawn into the image at the view's size. Its camera is read when the
        // frame is recorded, after this frame's input, as the gizmo drawn over it is.
        ImageIndex image = ImageIndex::Invalid;
        const IntRect last = view.viewRect;
        if (view.onRender && graphicsContext != nullptr && last.w > 0 && last.h > 0)
        {
            const uint2 size{ uint32_t(last.w), uint32_t(last.h) };
            image = graphicsContext->RenderScene(id, windowRegistry.GetCurrentWindowID(), size, view.background,
                [v, size](const void* native) {
                    if (!v->onRender) return;
                    const float aspect = float(size.x) / float(size.y);
                    v->onRender(ViewportRenderContext{ .size = size, .view = v->camera.View(),
                                                       .projection = v->camera.Projection(aspect), .native = native });
                });
        }

        EventHandler handler;
        if (!disabled)
        {
            GUIStateManager& gsm = guiStateManager;
            const ElementStorage& es = elementStorage;
            const WheelMode wheelMode = GetWheelMode();
            const float axisRadius = float(styleManager.Px(int32_t(kAxisGizmoRadius)));
            const float axisMargin = float(styleManager.Px(int32_t(kAxisGizmoMargin)));
            const float axisReach  = float(styleManager.Px(9));
            const float notch      = float(styleManager.Px(40));   // a wheel notch scrolls this far
            const int32_t dragDistance = styleManager.Px(kDragStartDistance);
            handler = [&gsm, &es, v, wheelMode, axisRadius, axisMargin, axisReach, notch, dragDistance](ElementEvent&& event) -> bool
            {
                if (event.rect == nullptr)
                    return false;
                const IntRect inner = event.rect->innerRect;
                const float2 size{ float(inner.w), float(inner.h) };
                const int2 local = event.LocalCursor(es) - inner.position;
                const float2 pixel{ float(local.x), float(local.y) };
                const bool alt   = (event.state.modifiers & KeyModifier::Alt) != KeyModifier::None;
                const bool ctrl  = (event.state.modifiers & KeyModifier::Control) != KeyModifier::None;
                const float2 axisCenter{ size.x - axisMargin, axisMargin };

                switch (event.type)
                {
                    case EventType::MouseDown:
                    {
                        // The camera: the middle button (or Alt + Ctrl + left) pans, Alt + left
                        // orbits, Alt + right dollies, the right button looks round.
                        using Move = ViewportView::CameraMove;
                        Move move = Move::None;
                        MouseButtons button = MouseButtons::None;
                        if (event.state.MiddlePressed())                         { move = Move::Pan;   button = MouseButtons::Middle; }
                        else if (event.state.LeftPressed() && alt && ctrl)       { move = Move::Pan;   button = MouseButtons::Left; }
                        else if (event.state.LeftPressed() && alt)               { move = Move::Orbit; button = MouseButtons::Left; }
                        else if (event.state.RightPressed() && alt)              { move = Move::Dolly; button = MouseButtons::Right; }
                        else if (event.state.RightPressed())                     { move = Move::Look;  button = MouseButtons::Right; }
                        if (move != Move::None)
                        {
                            v->cameraDrag.Press(event, es, gsm, v->camera, { .button = button, .atOnce = true });
                            v->cameraMove = move;
                            return true;
                        }
                        if (!event.state.LeftPressed())
                            return false;
                        TakeFocus(gsm, event.id);

                        // The axes at the top right: look along the one clicked.
                        if (const int axis = HitAxisGizmo(v->camera, pixel, axisCenter, axisRadius, axisReach); axis >= 0)
                        {
                            AlignCamera(v->camera, axis % 3, axis >= 3);
                            return true;
                        }
                        if (const GizmoPart part = HitGizmo(*v, pixel, size); part != GizmoPart::None)
                        {
                            v->gizmoDrag.Press(event, es, gsm, v->selection, { .atOnce = true });
                            v->activePart = part;
                            v->dragFrom = pixel;
                            return true;
                        }
                        v->click.Press(event, es, gsm, {}, { .focus = false, .capture = false });
                        return true;
                    }
                    case EventType::MouseMove:
                    {
                        v->flyFast = (event.state.modifiers & KeyModifier::Shift) != KeyModifier::None;
                        if (v->cameraDrag.Move(event, 0))
                        {
                            const float2 d{ float(event.state.screenDelta.x), float(event.state.screenDelta.y) };
                            switch (v->cameraMove)
                            {
                                case ViewportView::CameraMove::Pan:   PanCamera(v->camera, d, size.y); break;
                                case ViewportView::CameraMove::Orbit: OrbitCamera(v->camera, d); break;
                                case ViewportView::CameraMove::Look:  LookCamera(v->camera, d); break;
                                case ViewportView::CameraMove::Dolly: DollyCamera(v->camera, std::exp((d.x - d.y) * 0.01f)); break;
                                case ViewportView::CameraMove::None:  break;
                            }
                            return true;
                        }
                        if (v->gizmoDrag.Move(event, 0))
                        {
                            v->selection = DragGizmo(v->tool, v->activePart, v->gizmoDrag.before, v->camera, size, v->dragFrom, pixel);
                            return true;
                        }
                        if (v->click.IsPressed(event.id))
                        {
                            v->click.Move(event, dragDistance);
                            return true;
                        }
                        v->hover.Track(AnyHeld(v->gizmoDrag, v->cameraDrag, v->click), [&] { return HitGizmo(*v, pixel, size); });
                        return false;
                    }
                    case EventType::MouseUp:
                    {
                        if (v->cameraDrag.IsPressed(event.id) && v->cameraDrag.Release(event) != PointerDrag::Ended::No)
                        {
                            v->cameraMove = ViewportView::CameraMove::None;
                            v->flyKeys = 0;
                            return true;
                        }
                        if (v->gizmoDrag.IsPressed(event.id) && v->gizmoDrag.Release(event) != PointerDrag::Ended::No)
                        {
                            // Already where it was dropped: the edit, for undo.
                            const ViewportEdit edit{ v->gizmoDrag.before, v->selection };
                            v->activePart = GizmoPart::None;
                            if (!edit.empty())
                            {
                                ++v->revision;
                                if (v->onEdit)
                                    v->onEdit(edit);
                            }
                            return true;
                        }
                        if (v->click.IsPressed(event.id))
                        {
                            if (v->click.Release(event) == PointerDrag::Ended::Click && v->onClick)
                                v->onClick(ViewportRay(v->camera, pixel, size), event.state.modifiers);
                            return true;
                        }
                        return false;
                    }
                    case EventType::MouseScroll:
                    {
                        // Looking round, the wheel sets how fast the fly keys move.
                        if (v->cameraMove == ViewportView::CameraMove::Look && event.state.scroll.y != 0.0f)
                        {
                            v->flySpeed = std::clamp(v->flySpeed * std::pow(1.2f, event.state.scroll.y), 0.05f, 1000.0f);
                            return true;
                        }
                        // Dollies, or slides the camera, as the app's wheel mode says; a pinch dollies.
                        const WheelNavigation nav = NavigateByWheel(event.state, wheelMode, 1.15f);
                        if (!nav.Any())
                            return false;
                        if (nav.zoom != 1.0f)
                            DollyCamera(v->camera, nav.zoom);
                        if (nav.pan.x != 0.0f || nav.pan.y != 0.0f)
                            PanCamera(v->camera, nav.pan * -notch, size.y);
                        return true;
                    }
                    case EventType::KeyDown:
                    {
                        if (gsm.Interaction().GetFocusedID() != event.id || event.state.consumed)
                            return false;
                        const KeyCode key = event.state.lastKey;
                        // Looking round with the right button: W, A, S, D, Q and E fly the camera.
                        if (v->cameraMove == ViewportView::CameraMove::Look)
                        {
                            v->flyFast = (event.state.modifiers & KeyModifier::Shift) != KeyModifier::None;
                            if (const uint8_t bit = FlyBit(key); bit != 0)
                            {
                                v->flyKeys |= bit;
                                return true;
                            }
                        }
                        if (key == KeyCode::Escape)
                        {
                            if (v->gizmoDrag.Cancel(event, gsm, v->selection))
                            {
                                v->activePart = GizmoPart::None;
                                return true;
                            }
                            if (v->cameraDrag.Cancel(event, gsm, v->camera))
                            {
                                v->cameraMove = ViewportView::CameraMove::None;
                                return true;
                            }
                            return false;
                        }
                        if (event.state.modifiers != KeyModifier::None || v->gizmoDrag.IsHeld())
                            return false;
                        switch (key)
                        {
                            case KeyCode::KeyQ: v->tool = ViewportTool::Select; return true;
                            case KeyCode::KeyW: v->tool = ViewportTool::Move;   return true;
                            case KeyCode::KeyE: v->tool = ViewportTool::Rotate; return true;
                            case KeyCode::KeyR: v->tool = ViewportTool::Scale;  return true;
                            case KeyCode::KeyF: FrameSelection(*v);             return true;
                            default: return false;
                        }
                    }
                    case EventType::KeyUp:
                    {
                        v->flyFast = (event.state.modifiers & KeyModifier::Shift) != KeyModifier::None;
                        const uint8_t bit = FlyBit(event.state.lastKey);
                        if (bit == 0 || (v->flyKeys & bit) == 0)
                            return false;
                        v->flyKeys &= uint8_t(~bit);
                        return true;
                    }
                    default:
                        return false;
                }
            };
        }

        Element element{ .id = id, .eventHandler = std::move(handler), .styleRef = theme.viewportStyleRef, .state = state };
        const ElementRef ref = RegisterControl(std::move(element), std::move(constraints));
        if (!ref.IsValid())
            return changed;
        VERIFY(elementStorage.AddPostLayout(ref, [v](PostLayoutContext& ctx) { v->viewRect = ctx.ElementInnerRect(); }));

        // The tools down the left: a button each, the one in use selected.
        VERIFY(elementStorage.BeginChildren());
        const bool pushed = idManager.PushParent(id);
        if (view.showTools)
        {
            BeginVertical(idManager.AllocateDynamicID("viewport_tools"), theme.viewportToolsStyleRef,
                          LayoutOverride{}.Absolute(styleManager.Px(8), styleManager.Px(8)));
            struct ToolButton { ViewportTool tool; IconID icon; const char* tip; const char* name; };
            const ToolButton tools[] = {
                { ViewportTool::Select, theme.viewportSelectIcon, "Select (Q)", "viewport_select" },
                { ViewportTool::Move,   theme.viewportMoveIcon,   "Move (W)",   "viewport_move" },
                { ViewportTool::Rotate, theme.viewportRotateIcon, "Rotate (E)", "viewport_rotate" },
                { ViewportTool::Scale,  theme.viewportScaleIcon,  "Scale (R)",  "viewport_scale" } };
            for (const ToolButton& t : tools)
            {
                const bool active = view.tool == t.tool;
                if (active) BeginSelected();
                Button(idManager.AllocateDynamicID(t.name), GUIContent{ t.icon, {}, t.tip }, [v, tool = t.tool]() { v->tool = tool; },
                       theme.viewportToolStyleRef);
                if (active) EndSelected();
            }
            EndVertical();
        }

        // The axes' letters, for the top right.
        std::array<TextID, 3> axisLabels{};
        const ID labelsID = idManager.AllocateRangedID(3, "viewport_axis_labels");
        for (uint32_t a = 0; a < 3; ++a)
            axisLabels[a] = textSystem->RegisterText(idManager.GetSubID(labelsID, a), a == 0 ? "X" : a == 1 ? "Y" : "Z",
                                                     theme.viewportAxisLabelStyleRef, ElementState::None);
        std::array<int2, 3> labelSizes{};
        for (size_t a = 0; a < 3; ++a)
        {
            const TextMeasurement m = textSystem->MeasureText(axisLabels[a], TextWrapContext{});
            labelSizes[a] = { m.maxWidth, m.maxHeight };
        }
        if (pushed)
            (void)idManager.PopParent();
        VERIFY(elementStorage.EndChildren());

        // ---- Drawing: the scene, the gizmo over it, the axes ----------------------
        const int32_t px1 = std::max(1, styleManager.Px(1));
        const float axisRadius = float(styleManager.Px(int32_t(kAxisGizmoRadius)));
        const float axisMargin = float(styleManager.Px(int32_t(kAxisGizmoMargin)));
        const VisualColorStyleIndex background = styleManager.RegisterColorStyle(ColorStyle::Create(view.background));
        VERIFY(elementStorage.AddCustomDraw(ref, [=, this](CustomDrawContext& ctx)
        {
            const IntRect inner = ctx.ElementInnerRect();
            const IntRect area{ 0, 0, inner.w, inner.h };
            const float2 size{ float(inner.w), float(inner.h) };
            if (image != ImageIndex::Invalid)
                ctx.RenderQuad(area, theme.imageFillColorStyle, 0.0f, image);
            else
                ctx.RenderQuad(area, background);

            const ViewportCamera& camera = v->camera;
            auto project = [&](float3 p, float2& out) { return ViewportProject(camera, size, p, out); };
            auto pt = [](float2 p) { return int2{ int32_t(std::lround(p.x)), int32_t(std::lround(p.y)) }; };
            auto colorOf = [&](GizmoPart part, int axis) {
                const bool lit = part == v->activePart || (v->activePart == GizmoPart::None && part == v->hover.part);
                return lit ? theme.curveChannelColors[0] : theme.curveChannelColors[size_t(1 + axis)];
            };

            // The gizmo over the selection.
            if (v->hasSelection && v->tool != ViewportTool::Select)
            {
                const float3 c = v->selection.position;
                float2 center;
                if (project(c, center))
                {
                    const float length = kGizmoPixels * camera.WorldPerPixel(c, size.y);
                    const float width = float(px1) * 2.5f;
                    std::array<float3, 3> axes{ AxisDir(0), AxisDir(1), AxisDir(2) };
                    if (v->tool == ViewportTool::Scale)
                        for (int a = 0; a < 3; ++a)
                            axes[size_t(a)] = v->selection.rotation.Rotate(AxisDir(a));

                    if (v->tool == ViewportTool::Rotate)
                    {
                        for (int a = 0; a < 3; ++a)
                        {
                            const float3 u = axes[size_t((a + 1) % 3)], w = axes[size_t((a + 2) % 3)];
                            std::vector<int2> ring;
                            for (int i = 0; i <= 64; ++i)
                            {
                                const float angle = 2.0f * kPi * float(i) / 64.0f;
                                float2 p;
                                if (project(c + (u * std::cos(angle) + w * std::sin(angle)) * length, p))
                                    ring.push_back(pt(p));
                            }
                            if (ring.size() > 1)
                                ctx.RenderPolyline(ring, /*closed*/ false, LineStyle{ .colorStyleIndex = colorOf(PartOfAxis(a), a), .lineWidth = width });
                        }
                    }
                    else
                    {
                        // The plane squares (move), then the axes with arrow heads or boxes.
                        if (v->tool == ViewportTool::Move)
                        {
                            constexpr GizmoPart planes[3] = { GizmoPart::YZ, GizmoPart::XZ, GizmoPart::XY };
                            for (int n = 0; n < 3; ++n)
                            {
                                const float3 u = axes[size_t((n + 1) % 3)], w = axes[size_t((n + 2) % 3)];
                                const float3 corners[4] = { u * 0.2f + w * 0.2f, u * 0.5f + w * 0.2f, u * 0.5f + w * 0.5f, u * 0.2f + w * 0.5f };
                                float2 q[4];
                                bool shown = true;
                                for (int k = 0; k < 4; ++k)
                                    shown = project(c + corners[k] * length, q[k]) && shown;
                                if (!shown)
                                    continue;
                                const VisualColorStyleIndex color = colorOf(planes[n], n);
                                const int2 outline[4] = { pt(q[0]), pt(q[1]), pt(q[2]), pt(q[3]) };
                                ctx.RenderPolyline(outline, /*closed*/ true, LineStyle{ .colorStyleIndex = color, .lineWidth = float(px1) });
                                ctx.RenderTriangle(q[0], q[1], q[2], theme.curveChannelFadedColors[size_t(1 + n)]);
                                ctx.RenderTriangle(q[0], q[2], q[3], theme.curveChannelFadedColors[size_t(1 + n)]);
                            }
                        }
                        for (int a = 0; a < 3; ++a)
                        {
                            float2 end;
                            if (!project(c + axes[size_t(a)] * length, end))
                                continue;
                            const VisualColorStyleIndex color = colorOf(PartOfAxis(a), a);
                            ctx.RenderLine(pt(center), pt(end), LineStyle{ .colorStyleIndex = color, .lineWidth = width });
                            const float2 dir = Normalize2(end - center);
                            const float head = float(styleManager.Px(10));
                            if (v->tool == ViewportTool::Move)
                            {
                                const float2 side{ -dir.y, dir.x };
                                ctx.RenderTriangle(end + dir * head, end + side * (head * 0.45f), end - side * (head * 0.45f), color);
                            }
                            else
                            {
                                const int32_t box = int32_t(head * 0.8f);
                                ctx.RenderQuad(IntRect{ int32_t(end.x) - box / 2, int32_t(end.y) - box / 2, box, box }, color);
                            }
                        }
                    }
                    // The middle: a dot to move in the view's plane, or a box to scale all ways.
                    if (v->tool != ViewportTool::Rotate)
                    {
                        const bool lit = v->activePart == GizmoPart::Center || (v->activePart == GizmoPart::None && v->hover.part == GizmoPart::Center);
                        const float dot = float(styleManager.Px(6));
                        ctx.RenderDisc(pt(center), dot, lit ? theme.curveChannelColors[0] : theme.curveSelectedKeyColor);
                    }
                }
            }

            // The axes at the top right: each world axis as the camera sees it; the positive
            // ends lettered, the negative ones faded. A click on one looks along it.
            const float2 axisCenter{ size.x - axisMargin, axisMargin };
            const float dotRadius = float(styleManager.Px(8));
            std::array<int, 6> order{ 0, 1, 2, 3, 4, 5 };
            // The ends farther away first, so the nearer ones are drawn over them.
            std::sort(order.begin(), order.end(), [&](int i, int j) {
                const float3 di = AxisDir(i % 3) * (i < 3 ? 1.0f : -1.0f), dj = AxisDir(j % 3) * (j < 3 ? 1.0f : -1.0f);
                return Dot(di, camera.Forward()) > Dot(dj, camera.Forward());
            });
            for (const int i : order)
            {
                const int a = i % 3;
                const float3 dir = AxisDir(a) * (i < 3 ? 1.0f : -1.0f);
                const float2 end = AxisGizmoEnd(camera, axisCenter, axisRadius, dir);
                if (i < 3)
                {
                    ctx.RenderLine(pt(axisCenter), pt(end), LineStyle{ .colorStyleIndex = theme.curveChannelColors[size_t(1 + a)], .lineWidth = float(px1) * 2.0f });
                    ctx.RenderDisc(pt(end), dotRadius, theme.curveChannelColors[size_t(1 + a)]);
                    const int2 s = labelSizes[size_t(a)];
                    ctx.RenderText(axisLabels[size_t(a)], IntRect{ int32_t(end.x) - s.x / 2, int32_t(end.y) - s.y / 2, s.x, s.y });
                }
                else
                    ctx.RenderDisc(pt(end), dotRadius * 0.7f, theme.curveChannelFadedColors[size_t(1 + a)]);
            }
        }));
        return changed;
    }
}
