// Viewport: the camera, the gizmos and their drags, and the control's mouse and keys.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_controls/gui_viewport_types.h>

#include <cmath>
#include <numbers>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        ::testing::AssertionResult Near(float3 a, float3 b, float eps = 1e-3f)
        {
            if (std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps)
                return ::testing::AssertionSuccess();
            return ::testing::AssertionFailure() << "(" << a.x << ", " << a.y << ", " << a.z << ") vs ("
                                                 << b.x << ", " << b.y << ", " << b.z << ")";
        }

        constexpr float2 kSize { 800, 600 };

        ViewportCamera FrontCamera()
        {
            ViewportCamera camera;
            camera.yaw = 0.0f;
            camera.pitch = 0.0f;
            camera.distance = 10.0f;
            return camera;
        }

        float2 Project(const ViewportCamera& camera, float3 p)
        {
            float2 pixel;
            EXPECT_TRUE(ViewportProject(camera, kSize, p, pixel));
            return pixel;
        }

        // ---------------------------------------------------------------------
        // The camera
        // ---------------------------------------------------------------------

        TEST(ViewportCamera, ItLooksAtItsTarget)
        {
            const ViewportCamera camera = FrontCamera();
            EXPECT_TRUE(Near(camera.Forward(), { 0, 0, -1 }));
            EXPECT_TRUE(Near(camera.Position(), { 0, 0, 10 }));
            EXPECT_TRUE(Near(camera.Right(), { 1, 0, 0 }));
            EXPECT_TRUE(Near(camera.Up(), { 0, 1, 0 }));
            EXPECT_EQ(Project(camera, { 0, 0, 0 }), (float2{ 400, 300 })) << "the target in the middle";
            EXPECT_GT(Project(camera, { 1, 0, 0 }).x, 400.0f) << "+x to the right";
            EXPECT_LT(Project(camera, { 0, 1, 0 }).y, 300.0f) << "+y up";
            float2 pixel;
            EXPECT_FALSE(ViewportProject(camera, kSize, { 0, 0, 20 }, pixel)) << "behind it";

            const float expected = 2.0f * 10.0f * std::tan(camera.fovY * 0.5f) / 600.0f;
            EXPECT_NEAR(camera.WorldPerPixel({ 0, 0, 0 }, 600.0f), expected, 1e-6f);
        }

        TEST(ViewportCamera, ARayGoesThroughThePixel)
        {
            ViewportCamera camera;
            camera.target = { 1, 2, 3 };
            const float3 point{ 2.0f, 2.5f, 1.0f };
            const Ray ray = ViewportRay(camera, Project(camera, point), kSize);
            EXPECT_TRUE(Near(ray.origin, camera.Position()));
            const float t = Dot(point - ray.origin, ray.direction);
            EXPECT_TRUE(Near(ray.At(t), point, 2e-3f));

            camera.orthographic = true;
            const Ray straight = ViewportRay(camera, Project(camera, point), kSize);
            EXPECT_TRUE(Near(straight.direction, camera.Forward())) << "orthographic: all along the view";
            EXPECT_TRUE(Near(straight.At(Dot(point - straight.origin, straight.direction)), point, 2e-3f));
            EXPECT_FLOAT_EQ(camera.WorldPerPixel({ 50, 0, 0 }, 600.0f), camera.WorldPerPixel(camera.target, 600.0f))
                << "the same everywhere";
        }

        TEST(ViewportCamera, OrbitLookPanDolly)
        {
            ViewportCamera camera = FrontCamera();
            OrbitCamera(camera, { 100, 0 });
            EXPECT_LT(camera.yaw, 0.0f) << "dragged right: the view turns round to the right";
            EXPECT_TRUE(Near(camera.target, { 0, 0, 0 })) << "round the target";
            OrbitCamera(camera, { 0, 100000 });
            EXPECT_GT(camera.pitch, -1.58f) << "not over the top";

            camera = FrontCamera();
            const float3 standing = camera.Position();
            LookCamera(camera, { 50, 30 });
            EXPECT_TRUE(Near(camera.Position(), standing)) << "looking round, it stays put";
            EXPECT_FALSE(Near(camera.target, { 0, 0, 0 }));

            camera = FrontCamera();
            const float3 point{ 0, 0, 0 };
            const float2 before = Project(camera, point);
            PanCamera(camera, { 40, -25 }, kSize.y);
            const float2 after = Project(camera, point);
            EXPECT_NEAR(after.x - before.x, 40.0f, 0.05f) << "what's at the target follows the pointer";
            EXPECT_NEAR(after.y - before.y, -25.0f, 0.05f);

            camera = FrontCamera();
            DollyCamera(camera, 2.0f);
            EXPECT_FLOAT_EQ(camera.distance, 5.0f);
            DollyCamera(camera, 1e9f);
            EXPECT_FLOAT_EQ(camera.distance, camera.nearZ * 2.0f) << "never through the target";
            DollyCamera(camera, 1e-9f);
            EXPECT_FLOAT_EQ(camera.distance, camera.farZ * 0.5f) << "never past the far end";
            camera.distance = 5.0f;
            DollyCamera(camera, 0.0f);
            EXPECT_FLOAT_EQ(camera.distance, 5.0f) << "a broken factor does nothing";
            DollyCamera(camera, std::nanf(""));
            EXPECT_FLOAT_EQ(camera.distance, 5.0f);
        }

        TEST(ViewportCamera, FlyingMovesAlongTheViewAndUp)
        {
            ViewportView view;
            view.camera = FrontCamera();
            view.flySpeed = 2.0f;
            FlyCamera(view, 1.0f);
            EXPECT_TRUE(Near(view.camera.target, { 0, 0, 0 })) << "no keys held: still";
            view.flyKeys = FlyForward;
            FlyCamera(view, 0.5f);
            EXPECT_TRUE(Near(view.camera.target, { 0, 0, -1 })) << "forward, two units a second";
            EXPECT_TRUE(Near(view.camera.Position(), { 0, 0, 9 })) << "the camera with it";
            view.flyKeys = FlyRight | FlyUp;
            view.flyFast = true;
            FlyCamera(view, 1.0f);
            const float step = 6.0f / std::sqrt(2.0f);
            EXPECT_TRUE(Near(view.camera.target, { step, step, -1 })) << "right and up at once, three times as fast";
            view.flyKeys = FlyLeft | FlyRight;
            const float3 before = view.camera.target;
            FlyCamera(view, 1.0f);
            EXPECT_TRUE(Near(view.camera.target, before)) << "opposite keys cancel";
        }

        TEST(ViewportCamera, AlignAndFrame)
        {
            ViewportCamera camera;
            AlignCamera(camera, 2, false);
            EXPECT_TRUE(Near(camera.Forward(), { 0, 0, -1 })) << "along z, from the front";
            AlignCamera(camera, 0, false);
            EXPECT_TRUE(Near(camera.Forward(), { -1, 0, 0 })) << "along x, from the right";
            AlignCamera(camera, 0, true);
            EXPECT_TRUE(Near(camera.Forward(), { 1, 0, 0 })) << "from the left";
            AlignCamera(camera, 1, false);
            EXPECT_LT(camera.Forward().y, -0.99f) << "from above";

            ViewportView view;
            view.hasSelection = true;
            view.selection.position = { 3, 1, -2 };
            view.selectionRadius = 2.0f;
            FrameSelection(view);
            EXPECT_TRUE(Near(view.camera.target, { 3, 1, -2 }));
            const float distance = view.camera.distance;
            view.selectionRadius = 4.0f;
            FrameSelection(view);
            EXPECT_NEAR(view.camera.distance, distance * 2.0f, 1e-3f) << "twice as big, twice as far";
        }

        // ---------------------------------------------------------------------
        // The gizmo
        // ---------------------------------------------------------------------

        ViewportView Selected(ViewportTool tool)
        {
            ViewportView view;
            view.camera.yaw = 0.6f;
            view.camera.pitch = -0.45f;
            view.camera.distance = 10.0f;
            view.tool = tool;
            view.hasSelection = true;
            return view;
        }

        // A point along an axis of the gizmo, `fraction` of its length out.
        float2 OnAxis(const ViewportView& view, float3 axis, float fraction)
        {
            const float length = kGizmoPixels * view.camera.WorldPerPixel(view.selection.position, kSize.y);
            return Project(view.camera, view.selection.position + axis * (length * fraction));
        }

        TEST(ViewportGizmo, TheMoveGizmosParts)
        {
            ViewportView view = Selected(ViewportTool::Move);
            const float2 center = Project(view.camera, view.selection.position);
            EXPECT_EQ(HitGizmo(view, center, kSize), GizmoPart::Center);
            EXPECT_EQ(HitGizmo(view, OnAxis(view, { 1, 0, 0 }, 0.85f), kSize), GizmoPart::X);
            EXPECT_EQ(HitGizmo(view, OnAxis(view, { 0, 1, 0 }, 0.85f), kSize), GizmoPart::Y);
            EXPECT_EQ(HitGizmo(view, OnAxis(view, { 0, 0, 1 }, 0.85f), kSize), GizmoPart::Z);
            const float length = kGizmoPixels * view.camera.WorldPerPixel(view.selection.position, kSize.y);
            EXPECT_EQ(HitGizmo(view, Project(view.camera, float3{ 0.35f, 0.35f, 0 } * length), kSize), GizmoPart::XY);
            EXPECT_EQ(HitGizmo(view, Project(view.camera, float3{ 0.35f, 0, 0.35f } * length), kSize), GizmoPart::XZ);
            EXPECT_EQ(HitGizmo(view, { 5, 5 }, kSize), GizmoPart::None) << "far off";

            view.tool = ViewportTool::Select;
            EXPECT_EQ(HitGizmo(view, center, kSize), GizmoPart::None) << "Select has no gizmo";
            view.tool = ViewportTool::Move;
            view.hasSelection = false;
            EXPECT_EQ(HitGizmo(view, center, kSize), GizmoPart::None) << "nothing selected";
        }

        TEST(ViewportGizmo, MovingAlongAnAxisOrAPlane)
        {
            const ViewportView view = Selected(ViewportTool::Move);
            const ViewportTransform before = view.selection;
            const float2 from = OnAxis(view, { 1, 0, 0 }, 0.8f);
            const float2 to   = OnAxis(view, { 1, 0, 0 }, 1.6f);
            const ViewportTransform x = DragGizmo(ViewportTool::Move, GizmoPart::X, before, view.camera, kSize, from, to);
            const float length = kGizmoPixels * view.camera.WorldPerPixel(before.position, kSize.y);
            EXPECT_NEAR(x.position.x, length * 0.8f, 0.01f);
            EXPECT_FLOAT_EQ(x.position.y, 0.0f);
            EXPECT_FLOAT_EQ(x.position.z, 0.0f);
            const ViewportTransform sideways = DragGizmo(ViewportTool::Move, GizmoPart::X, before, view.camera, kSize, from, from + float2{ 0, 80 });
            EXPECT_EQ(sideways.position.y, 0.0f) << "only along x";

            const ViewportTransform floor = DragGizmo(ViewportTool::Move, GizmoPart::XZ, before, view.camera, kSize,
                                                      Project(view.camera, { 0, 0, 0 }), Project(view.camera, { 1, 0, 2 }));
            EXPECT_TRUE(Near(floor.position, { 1, 0, 2 }, 0.01f)) << "on the floor, under the pointer";

            const ViewportTransform free = DragGizmo(ViewportTool::Move, GizmoPart::Center, before, view.camera, kSize,
                                                     Project(view.camera, { 0, 0, 0 }), Project(view.camera, { 0, 0, 0 }) + float2{ 30, 0 });
            EXPECT_NEAR(Dot(free.position, view.camera.Forward()), 0.0f, 1e-3f) << "in the view's plane";
            EXPECT_GT(Dot(free.position, view.camera.Right()), 0.0f);
            EXPECT_EQ(free.rotation, before.rotation);
            EXPECT_EQ(free.scale, before.scale);
        }

        TEST(ViewportGizmo, RotatingRoundAnAxis)
        {
            ViewportView view = Selected(ViewportTool::Rotate);
            view.camera.pitch = -1.2f;   // from above, the y ring seen nearly round
            const float length = kGizmoPixels * view.camera.WorldPerPixel(view.selection.position, kSize.y);
            const float2 onRing = Project(view.camera, float3{ 1, 0, 0 } * length);
            EXPECT_EQ(HitGizmo(view, onRing, kSize), GizmoPart::Y) << "the ring round y";
            const float2 quarter = Project(view.camera, float3{ 0, 0, -1 } * length);
            const ViewportTransform turned = DragGizmo(ViewportTool::Rotate, GizmoPart::Y, view.selection, view.camera, kSize, onRing, quarter);
            const float3 x = turned.rotation.Rotate({ 1, 0, 0 });
            EXPECT_NEAR(x.y, 0.0f, 1e-3f) << "round y only";
            EXPECT_TRUE(Near(x, { 0, 0, -1 }, 0.02f)) << "the ring's point follows the pointer: a quarter turn";
            EXPECT_EQ(turned.position, view.selection.position);
        }

        TEST(ViewportGizmo, ScalingAlongAnAxisOrAllWays)
        {
            ViewportView view = Selected(ViewportTool::Scale);
            view.selection.rotation = quat::AxisAngle({ 0, 1, 0 }, 0.5f);   // along its own axes
            const float3 localX = view.selection.rotation.Rotate({ 1, 0, 0 });
            EXPECT_EQ(HitGizmo(view, OnAxis(view, localX, 0.85f), kSize), GizmoPart::X);
            const ViewportTransform wider = DragGizmo(ViewportTool::Scale, GizmoPart::X, view.selection, view.camera, kSize,
                                                      OnAxis(view, localX, 0.5f), OnAxis(view, localX, 1.0f));
            EXPECT_NEAR(wider.scale.x, 2.0f, 0.01f) << "twice as far out: twice as wide";
            EXPECT_FLOAT_EQ(wider.scale.y, 1.0f);
            const ViewportTransform flat = DragGizmo(ViewportTool::Scale, GizmoPart::X, view.selection, view.camera, kSize,
                                                     OnAxis(view, localX, 0.5f), OnAxis(view, localX, -0.5f));
            EXPECT_GT(flat.scale.x, 0.0f) << "never flipped through zero";

            const float2 center = Project(view.camera, view.selection.position);
            const ViewportTransform all = DragGizmo(ViewportTool::Scale, GizmoPart::Center, view.selection, view.camera, kSize,
                                                    center + float2{ 20, 0 }, center + float2{ 40, 0 });
            EXPECT_NEAR(all.scale.x, 2.0f, 0.01f);
            EXPECT_FLOAT_EQ(all.scale.x, all.scale.y);
            EXPECT_FLOAT_EQ(all.scale.x, all.scale.z);
        }

        TEST(ViewportGizmo, EditsUndoAndReachTheApp)
        {
            ViewportView view = Selected(ViewportTool::Move);
            std::vector<ViewportEdit> seen;
            view.onEdit = [&](const ViewportEdit& e) { seen.push_back(e); };
            ViewportTransform moved = view.selection;
            moved.position = { 1, 2, 3 };
            const ViewportEdit edit{ view.selection, moved };
            CommitViewportEdit(view, edit);
            EXPECT_EQ(view.selection, moved);
            EXPECT_EQ(seen.size(), 1u);
            EXPECT_EQ(view.revision, 1u);
            ApplyViewportEdit(view, edit, /*undo*/ true);
            EXPECT_EQ(view.selection, edit.from);
            CommitViewportEdit(view, ViewportEdit{});
            EXPECT_EQ(seen.size(), 1u) << "an empty edit isn't reported";
        }

        // ---------------------------------------------------------------------
        // The control
        // ---------------------------------------------------------------------

        class ViewportControlTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            ViewportView   view;
            ID             viewID{};
            int            changes = 0;
            std::vector<ViewportEdit> edits;
            std::vector<Ray> clicks;

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                app.runPostLayoutCallbacks = true;
                viewID = app.idManager.CreateStaticID();
                view.camera.yaw = 0.6f;
                view.camera.pitch = -0.45f;
                view.camera.distance = 10.0f;
                view.hasSelection = true;
                view.onEdit = [this](const ViewportEdit& e) { edits.push_back(e); };
                view.onClick = [this](const Ray& r, KeyModifier) { clicks.push_back(r); };
                Settle();
            }
            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow({ 1000, 700 }, ElementFlags::None, app.theme.mainWindowStyleRef);
                    if (app.Viewport(viewID, view, LayoutOverride{}.Size(800, 600)))
                        ++changes;
                    app.EndRootWindow();
                });
            }
            void Settle() { Frame(); Frame(); }

            float2 Size() const { return { float(view.viewRect.w), float(view.viewRect.h) }; }
            // A pixel of the view (from its top left) on the screen.
            int2 Screen(float2 pixel) const
            {
                return { view.viewRect.x + int32_t(std::lround(pixel.x)), view.viewRect.y + int32_t(std::lround(pixel.y)) };
            }
            float2 Pixel(float3 p) const
            {
                float2 pixel;
                EXPECT_TRUE(ViewportProject(view.camera, Size(), p, pixel));
                return pixel;
            }
            void DragWith(int2 from, int2 to, MouseButtons button, KeyModifier mods = KeyModifier::None)
            {
                app.InjectMouseMove(from, MouseButtons::None, mods);
                app.InjectMouseDown(from, button, mods);
                Frame();
                app.InjectMouseMove(to, button, mods);
                Frame();
                app.InjectMouseUp(to, button, mods);
                Frame();
            }
            std::vector<ID> ToolButtons()
            {
                std::vector<ID> buttons;
                const auto& storage = app.elementStorage;
                for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
                    for (const Element& e : it->GetChildren(storage))
                        if (e.styleRef.visualBoxStyleIndex == app.theme.viewportToolStyleRef.visualBoxStyleIndex)
                            buttons.push_back(e.id);
                return buttons;
            }
        };

        // In a cached panel the view is built every frame: its scene is drawn again each
        // frame, at the size the view has. Resized, it takes the new size at once rather than
        // staying stretched at the old one until the panel is built for another reason.
        TEST_F(ViewportControlTest, InACachedPanelItIsBuiltEveryFrameAtItsSize)
        {
            app.SetDrawCacheEnabled(true);
            const ID scope = app.idManager.CreateStaticID();
            int builds = 0;
            int width = 800;
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow({ 1000, 700 }, ElementFlags::None, app.theme.mainWindowStyleRef);
                    if (app.BeginCached(scope, app.theme.emptyVertStyleRef, 0, LayoutOverride{}.Size(width, 600)))
                    {
                        ++builds;
                        app.Viewport(viewID, view, LayoutOverride{}.FlexGrow(1));
                    }
                    app.EndCached();
                    app.EndRootWindow();
                });
            };
            // The pointer elsewhere: a scope under it is rebuilt for that alone.
            app.InjectMouseMove({ 990, 690 }, MouseButtons::None);
            for (int i = 0; i < 4; ++i) frame();
            builds = 0;
            for (int i = 0; i < 4; ++i) frame();
            EXPECT_EQ(builds, 4) << "built every frame, not replayed";

            width = 500;   // the panel narrows, as when a panel beside it goes
            frame();
            frame();
            EXPECT_EQ(view.viewRect.w, 500);
            builds = 0;
            frame();
            EXPECT_EQ(builds, 1) << "and still built at the new size";
        }

        TEST_F(ViewportControlTest, TheMouseMovesTheCamera)
        {
            ASSERT_GT(view.viewRect.w, 0);
            const int2 middle = Screen(Size() * 0.5f);
            ViewportCamera before = view.camera;
            DragWith(middle, middle + int2{ 40, 0 }, MouseButtons::Middle);
            EXPECT_NE(view.camera.target, before.target) << "middle: pans";
            EXPECT_EQ(view.camera.yaw, before.yaw);

            before = view.camera;
            DragWith(middle, middle + int2{ 40, 0 }, MouseButtons::Left, KeyModifier::Alt);
            EXPECT_NE(view.camera.yaw, before.yaw) << "Alt + left: orbits";
            EXPECT_EQ(view.camera.target, before.target);

            before = view.camera;
            DragWith(middle, middle + int2{ 40, 0 }, MouseButtons::Left, KeyModifier::Alt | KeyModifier::Control);
            EXPECT_NE(view.camera.target, before.target) << "Alt + Ctrl + left: pans";
            EXPECT_EQ(view.camera.yaw, before.yaw);

            before = view.camera;
            DragWith(middle, middle + int2{ 0, -40 }, MouseButtons::Right, KeyModifier::Alt);
            EXPECT_LT(view.camera.distance, before.distance) << "Alt + right up: dollies in";

            before = view.camera;
            DragWith(middle, middle + int2{ 40, 0 }, MouseButtons::Right);
            EXPECT_TRUE(Near(view.camera.Position(), before.Position())) << "right: looks round where it stands";
            EXPECT_NE(view.camera.yaw, before.yaw);
            EXPECT_TRUE(edits.empty());
            EXPECT_TRUE(clicks.empty()) << "none of them is a click";
        }

        TEST_F(ViewportControlTest, EscapePutsTheCameraBack)
        {
            const int2 middle = Screen(Size() * 0.5f);
            const ViewportCamera before = view.camera;
            app.InjectMouseMove(middle, MouseButtons::None, KeyModifier::Alt);
            app.InjectMouseDown(middle, MouseButtons::Left, KeyModifier::Alt);
            Frame();
            app.InjectMouseMove(middle + int2{ 60, 20 }, MouseButtons::Left, KeyModifier::Alt);
            Frame();
            ASSERT_NE(view.camera.yaw, before.yaw);
            app.InjectKey(KeyCode::Escape);
            Frame();
            EXPECT_EQ(view.camera.yaw, before.yaw);
            EXPECT_EQ(view.camera.pitch, before.pitch);
            app.InjectMouseUp(middle + int2{ 60, 20 }, MouseButtons::Left, KeyModifier::Alt);
            Frame();
        }

        TEST_F(ViewportControlTest, TheWheelAndAPinch)
        {
            const int2 middle = Screen(Size() * 0.5f);
            app.InjectMouseMove(middle);
            app.InjectMouseScroll(middle, { 0, 1 });
            Frame();
            EXPECT_LT(view.camera.distance, 10.0f) << "the wheel dollies in";

            app.SetWheelMode(WheelMode::Pan);
            const float distance = view.camera.distance;
            const float3 target = view.camera.target;
            app.InjectMouseScroll(middle, { 0, -1 });
            Frame();
            EXPECT_EQ(view.camera.distance, distance) << "set to scroll, it doesn't dolly";
            EXPECT_NE(view.camera.target, target) << "it slides the camera";
            app.InjectPinch(2.0f);
            Frame();
            EXPECT_LT(view.camera.distance, distance) << "a pinch dollies";
        }

        TEST_F(ViewportControlTest, DraggingTheGizmoMovesTheSelection)
        {
            const float length = kGizmoPixels * view.camera.WorldPerPixel({}, Size().y);
            const int2 onX = Screen(Pixel(float3{ 1, 0, 0 } * (length * 0.8f)));
            const int2 further = Screen(Pixel(float3{ 1, 0, 0 } * (length * 1.6f)));
            app.InjectMouseMove(onX);
            Frame();
            EXPECT_EQ(view.hover.part, GizmoPart::X) << "the arrow under the pointer";
            DragWith(onX, further, MouseButtons::Left);
            EXPECT_GT(view.selection.position.x, length * 0.5f);
            EXPECT_NEAR(view.selection.position.y, 0.0f, 1e-4f);
            ASSERT_EQ(edits.size(), 1u) << "one edit, at the drop";
            EXPECT_EQ(edits[0].from.position, (float3{}));
            Frame();
            EXPECT_EQ(changes, 1) << "reported once";
            EXPECT_TRUE(clicks.empty());

            // Escape during a drag puts it back, and makes no edit.
            const ViewportTransform moved = view.selection;
            const int2 onY = Screen(Pixel(moved.position + float3{ 0, 1, 0 } * (length * 0.8f)));
            app.InjectMouseMove(onY);
            app.InjectMouseDown(onY);
            Frame();
            app.InjectMouseMove(onY + int2{ 0, -50 }, MouseButtons::Left);
            Frame();
            ASSERT_NE(view.selection.position.y, moved.position.y);
            app.InjectKey(KeyCode::Escape);
            Frame();
            EXPECT_EQ(view.selection, moved);
            app.InjectMouseUp(onY + int2{ 0, -50 });
            Frame();
            EXPECT_EQ(edits.size(), 1u);
        }

        TEST_F(ViewportControlTest, AClickOffTheGizmoPicks)
        {
            const int2 corner = Screen({ 120, 500 });
            app.ClickAt(corner);
            Frame();
            ASSERT_EQ(clicks.size(), 1u);
            const Ray expected = ViewportRay(view.camera, { 120, 500 }, Size());
            EXPECT_TRUE(Near(clicks[0].direction, expected.direction, 2e-3f));
            DragWith(corner, corner + int2{ 60, 0 }, MouseButtons::Left);
            EXPECT_EQ(clicks.size(), 1u) << "a drag doesn't pick";
        }

        TEST_F(ViewportControlTest, KeysPickTheToolAndFrame)
        {
            app.ClickAt(Screen({ 120, 500 }));   // the focus
            Frame();
            const std::pair<KeyCode, ViewportTool> keys[] = { { KeyCode::KeyE, ViewportTool::Rotate }, { KeyCode::KeyR, ViewportTool::Scale },
                                                              { KeyCode::KeyQ, ViewportTool::Select }, { KeyCode::KeyW, ViewportTool::Move } };
            for (const auto& [key, tool] : keys)
            {
                app.InjectKey(key);
                Frame();
                EXPECT_EQ(view.tool, tool);
            }
            view.selection.position = { 4, 1, -2 };
            app.InjectKey(KeyCode::KeyF);
            Frame();
            EXPECT_TRUE(Near(view.camera.target, { 4, 1, -2 })) << "F frames the selection";
        }

        TEST_F(ViewportControlTest, LookingRoundTheKeysFly)
        {
            const int2 middle = Screen(Size() * 0.5f);
            app.InjectMouseMove(middle);
            app.InjectMouseDown(middle, MouseButtons::Right);
            Frame();
            const float3 start = view.camera.Position();
            app.InjectKey(KeyCode::KeyW);
            app.InjectKey(KeyCode::KeyE);
            Frame();
            EXPECT_EQ(view.tool, ViewportTool::Move) << "W and E fly while looking, not pick a tool";
            EXPECT_EQ(view.flyKeys, uint8_t(FlyForward | FlyUp));
            app.frameTime = 0.5f;   // a long frame counts as a tenth of a second, so a hitch can't throw the camera
            Frame();
            EXPECT_NEAR(Length(view.camera.Position() - start), 0.4f, 1e-3f) << "four units a second, for 0.1 s";
            EXPECT_GT(Dot(view.camera.Position() - start, view.camera.Forward()), 0.1f) << "forward";
            EXPECT_GT(view.camera.Position().y, start.y) << "and up";
            app.InjectKey(KeyCode::KeyW, KeyAction::KeyUp);
            Frame();
            EXPECT_EQ(view.flyKeys, uint8_t(FlyUp));
            app.InjectMouseScroll(middle, { 0, 1 });
            Frame();
            EXPECT_GT(view.flySpeed, 4.0f) << "the wheel speeds it up while looking";
            app.InjectMouseUp(middle, MouseButtons::Right);
            Frame();
            EXPECT_EQ(view.flyKeys, 0) << "letting go stops it";
            app.frameTime = 0.0f;
        }

        TEST_F(ViewportControlTest, TheAxesAtTheTopRightLookAlongOne)
        {
            // The +y end, straight up from the axes' middle while the camera looks level.
            view.camera.pitch = 0.0f;
            Settle();
            const float margin = float(app.styleManager.Px(40)), radius = float(app.styleManager.Px(26));
            app.ClickAt(Screen({ Size().x - margin, margin - radius }));
            Frame();
            EXPECT_LT(view.camera.Forward().y, -0.99f) << "looking down from above";
            EXPECT_TRUE(clicks.empty());
        }

        TEST_F(ViewportControlTest, TheToolButtons)
        {
            std::vector<ID> buttons = ToolButtons();
            ASSERT_EQ(buttons.size(), 4u);
            app.ClickAt(app.CenterOf(buttons[3]));
            Frame();
            EXPECT_EQ(view.tool, ViewportTool::Scale) << "the fourth: Scale";
            EXPECT_TRUE(clicks.empty()) << "a button's click isn't the view's";

            view.showTools = false;
            Settle();
            EXPECT_TRUE(ToolButtons().empty()) << "off: no buttons";
        }

        TEST_F(ViewportControlTest, DisabledItTakesNoInput)
        {
            auto disabled = [&] {
                app.Frame([&] {
                    app.BeginRootWindow({ 1000, 700 }, ElementFlags::None, app.theme.mainWindowStyleRef);
                    app.BeginDisabled();
                    app.Viewport(viewID, view, LayoutOverride{}.Size(800, 600));
                    app.EndDisabled();
                    app.EndRootWindow();
                });
            };
            disabled();
            const ViewportCamera before = view.camera;
            const int2 middle = Screen(Size() * 0.5f);
            app.InjectMouseMove(middle, MouseButtons::None, KeyModifier::Alt);
            app.InjectMouseDown(middle, MouseButtons::Left, KeyModifier::Alt);
            disabled();
            app.InjectMouseMove(middle + int2{ 50, 0 }, MouseButtons::Left, KeyModifier::Alt);
            disabled();
            app.InjectMouseUp(middle + int2{ 50, 0 }, MouseButtons::Left, KeyModifier::Alt);
            disabled();
            EXPECT_EQ(view.camera.yaw, before.yaw);
        }
    }
}
