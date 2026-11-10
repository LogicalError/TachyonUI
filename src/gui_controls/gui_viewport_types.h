#pragma once

// Viewport: a 3D view the app renders, with a camera to move round it and gizmos to move,
// rotate and scale the selected object.

#include <gui_core/gui_math3d.h>
#include <gui_core/input/gui_input_enums.h>
#include <gui_core/input/gui_pointer_drag.h>
#include <gui_core/gui_base_types.h>

#include <functional>

namespace tachyon
{
    /// @addtogroup controls
    /// @{

    /// What a drag on the view does to the selected object.
    enum class ViewportTool : uint8_t
    {
        Select,   ///< nothing: a click picks
        Move,
        Rotate,
        Scale
    };

    /// A camera orbiting `target`, `distance` away.
    struct ViewportCamera
    {
        float3 target {};
        float  distance = 8.0f;
        float  yaw = 0.6f;      ///< radians round y; 0 looks down -z, positive turns left
        float  pitch = -0.45f;  ///< radians; negative looks down
        float  fovY = 0.8727f;  ///< radians, top to bottom (50 degrees)
        bool   orthographic = false;
        float  nearZ = 0.05f, farZ = 1000.0f;

        /// The way it looks, length 1.
        float3 Forward() const;
        float3 Position() const { return target - Forward() * distance; }
        float3 Right() const;
        float3 Up() const { return Cross(Right(), Forward()); }
        float4x4 View() const { return LookAt(Position(), target, { 0, 1, 0 }); }
        /// For a view `aspect` wide over high. Orthographic, it's as high as the perspective
        /// view is at the target.
        float4x4 Projection(float aspect) const;
        /// How many world units a pixel is at `at`, in a view `heightPixels` high.
        float WorldPerPixel(float3 at, float heightPixels) const;
    };

    /// The ray from the camera through `pixel` (from the view's top left) of a view `size` pixels.
    Ray ViewportRay(const ViewportCamera& camera, float2 pixel, float2 size);
    /// Where `point` shows in the view, from its top left; false when it's behind the camera.
    bool ViewportProject(const ViewportCamera& camera, float2 size, float3 point, float2& pixel);

    /// The selected object's place in the world.
    struct ViewportTransform
    {
        float3 position {};
        quat   rotation {};
        float3 scale { 1, 1, 1 };

        bool operator==(const ViewportTransform&) const = default;
        float4x4 Matrix() const { return TRS(position, rotation, scale); }
    };

    /// A gizmo's drag: the selection as it was and is.
    struct ViewportEdit
    {
        ViewportTransform from, to;
        bool empty() const { return from == to; }
    };

    /// A part of the gizmo.
    enum class GizmoPart : uint8_t
    {
        None,
        X, Y, Z,          ///< an arrow, a ring or a scale handle
        XY, YZ, XZ,       ///< a plane square (move)
        Center            ///< the middle: move in the view's plane, or scale all three ways
    };

    /// What the app's onRender gets each frame: the target's size and the camera's matrices.
    /// The backend's own (Vulkan: VulkanViewportRender, with the command buffer and formats,
    /// the rendering begun) is in `native`.
    struct ViewportRenderContext
    {
        uint2       size {};        ///< pixels
        float4x4    view;
        float4x4    projection;
        const void* native = nullptr;
    };

    /// A viewport: what it shows and how. Keep it from frame to frame, as a CurveView.
    struct ViewportView
    {
        ViewportCamera camera;
        ViewportTool   tool = ViewportTool::Move;
        /// The tools' buttons down the view's left. Off for an app with a toolbar of its own
        /// that sets `tool` (Q, W, E and R still do).
        bool           showTools = true;
        /// The selected object, which the gizmo moves; none, no gizmo.
        bool              hasSelection = false;
        ViewportTransform selection;
        float             selectionRadius = 1.0f;   ///< how big it is, for framing it (F)
        /// How fast W, A, S, D, Q and E move the camera while the right button looks round,
        /// in world units a second; the wheel changes it then, and Shift triples it.
        float             flySpeed = 4.0f;
        /// Behind what onRender draws; and all there is without it.
        color4f           background { 0.07f, 0.08f, 0.09f, 1.0f };

        /// Records the app's drawing into the view each frame (see ViewportRenderContext).
        std::function<void(const ViewportRenderContext&)> onRender;
        /// A gizmo's drag ended (already applied): put it in the undo history.
        std::function<void(const ViewportEdit&)> onEdit;
        /// A click that didn't drag, off the gizmo: the ray through it, to pick with.
        std::function<void(const Ray& ray, KeyModifier modifiers)> onClick;

        /// @name Internal: kept between frames while the user works
        ///@{
        PointerDrag       gizmoDrag;
        GizmoPart         activePart = GizmoPart::None;   ///< the part dragged
        GizmoPart         hoverPart  = GizmoPart::None;   ///< the part under the pointer
        ViewportTransform before;                         ///< the selection as the drag began
        float2            dragFrom {};                    ///< the press, in view pixels
        PointerDrag       cameraDrag;
        enum class CameraMove : uint8_t { None, Pan, Orbit, Look, Dolly } cameraMove = CameraMove::None;
        ViewportCamera    cameraBefore;                   ///< for Escape
        PointerDrag       click;                          ///< a left press off the gizmo: a pick if it doesn't move
        uint8_t           flyKeys = 0;                    ///< the fly keys held, by FlyKey bit
        bool              flyFast = false;                ///< Shift held while flying
        IntRect           viewRect {};                    ///< the view last frame, in screen pixels
        uint32_t          revision = 0, revisionSeen = 0;
        ///@}
    };

    /// @name Viewport helpers
    ///@{

    /// The gizmo's arrows, rings and handles are this many pixels long.
    constexpr float kGizmoPixels = 90.0f;

    /// The part of `view`'s gizmo under `pixel` in a view `size` pixels, by its tool; None
    /// without a selection or with Select.
    GizmoPart HitGizmo(const ViewportView& view, float2 pixel, float2 size);
    /// The selection a drag of `part` from `from` to `to` (view pixels) makes of `before`.
    ViewportTransform DragGizmo(ViewportTool tool, GizmoPart part, const ViewportTransform& before,
                                const ViewportCamera& camera, float2 size, float2 from, float2 to);

    /// Turns the camera round its target by a drag of `delta` pixels.
    void OrbitCamera(ViewportCamera& camera, float2 delta);
    /// Turns the camera where it stands (looks round) by a drag of `delta` pixels.
    void LookCamera(ViewportCamera& camera, float2 delta);
    /// Slides the camera and its target so what was under the pointer follows a drag of
    /// `delta` pixels, in a view `heightPixels` high.
    void PanCamera(ViewportCamera& camera, float2 delta, float heightPixels);
    /// Moves the camera toward its target by `factor` (above 1: closer).
    void DollyCamera(ViewportCamera& camera, float factor);
    /// Looks along `axis` (0 x, 1 y, 2 z) toward its negative end, or its positive with `fromBelow`.
    void AlignCamera(ViewportCamera& camera, int axis, bool fromNegative);
    /// Centers the selection, near enough to see all of it.
    void FrameSelection(ViewportView& view);
    /// Moves the camera by the fly keys held (ViewportView::flyKeys) for `seconds`: forward,
    /// back and sideways along its view, down and up along the world's y.
    void FlyCamera(ViewportView& view, float seconds);

    /// The fly keys' bits in ViewportView::flyKeys.
    enum FlyKey : uint8_t { FlyForward = 1, FlyBack = 2, FlyLeft = 4, FlyRight = 8, FlyDown = 16, FlyUp = 32 };

    /// Does (or with `undo`, undoes) `edit` on the selection.
    void ApplyViewportEdit(ViewportView& view, const ViewportEdit& edit, bool undo = false);
    /// Applies `edit` and hands it to `view.onEdit`; does nothing for an empty one.
    void CommitViewportEdit(ViewportView& view, const ViewportEdit& edit);
    ///@}

    /// @}
}
