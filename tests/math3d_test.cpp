// 3D math for a viewport: rotations, the camera's matrices, rays.

#include <gtest/gtest.h>

#include <gui_core/gui_math3d.h>

#include <numbers>

namespace tachyon
{
    namespace
    {
        ::testing::AssertionResult Near(float3 a, float3 b, float eps = 1e-4f)
        {
            if (std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps)
                return ::testing::AssertionSuccess();
            return ::testing::AssertionFailure() << "(" << a.x << ", " << a.y << ", " << a.z << ") vs ("
                                                 << b.x << ", " << b.y << ", " << b.z << ")";
        }

        constexpr float kQuarter = std::numbers::pi_v<float> * 0.5f;

        TEST(Math3D, VectorsBasics)
        {
            EXPECT_EQ(Dot({ 1, 2, 3 }, { 4, 5, 6 }), 32.0f);
            EXPECT_TRUE(Near(Cross({ 1, 0, 0 }, { 0, 1, 0 }), { 0, 0, 1 }));
            EXPECT_FLOAT_EQ(Length({ 3, 4, 0 }), 5.0f);
            EXPECT_TRUE(Near(Normalize({ 0, 0, 7 }), { 0, 0, 1 }));
            EXPECT_TRUE(Near(Normalize({}), {}));
            EXPECT_TRUE(Near(Scale({ 1, 2, 3 }, { 2, 3, 4 }), { 2, 6, 12 }));
        }

        TEST(Math3D, QuaternionsRotate)
        {
            const quat yaw = quat::AxisAngle({ 0, 1, 0 }, kQuarter);
            EXPECT_TRUE(Near(yaw.Rotate({ 1, 0, 0 }), { 0, 0, -1 })) << "counterclockwise looking down y";
            const quat pitch = quat::AxisAngle({ 1, 0, 0 }, kQuarter);
            EXPECT_TRUE(Near((yaw * pitch).Rotate({ 0, 1, 0 }), yaw.Rotate(pitch.Rotate({ 0, 1, 0 })))) << "the right one first";
            EXPECT_TRUE(Near(quat{}.Rotate({ 1, 2, 3 }), { 1, 2, 3 }));
            const quat n = quat{ 0, 0, 0, 2 }.Normalized();
            EXPECT_FLOAT_EQ(n.w, 1.0f);
            EXPECT_EQ((quat{ 0, 0, 0, 0 }.Normalized()), quat{});
        }

        TEST(Math3D, TransformsComposeAndInvert)
        {
            const float4x4 m = TRS({ 1, 2, 3 }, quat::AxisAngle({ 0, 0, 1 }, kQuarter), { 2, 2, 2 });
            EXPECT_TRUE(Near(TransformPoint(m, { 1, 0, 0 }), { 1, 4, 3 })) << "scaled, turned to +y, moved";
            EXPECT_TRUE(Near(TransformPoint(Inverse(m), { 1, 4, 3 }), { 1, 0, 0 }));
            const float4x4 identity = m * Inverse(m);
            EXPECT_NEAR(identity.r0.x, 1.0f, 1e-5f);
            EXPECT_NEAR(identity.r1.w, 0.0f, 1e-5f);
            float4x4 singular{};
            singular.r0 = singular.r1;
            EXPECT_EQ(Inverse(singular), float4x4{}) << "none: the identity";
            EXPECT_TRUE(Near(TransformPoint(Translation({ 5, 0, 0 }), { 1, 1, 1 }), { 6, 1, 1 }));
        }

        TEST(Math3D, TheCameraLooksDownMinusZAndProjectsToVulkan)
        {
            const float4x4 view = LookAt({ 0, 0, 10 }, { 0, 0, 0 }, { 0, 1, 0 });
            EXPECT_TRUE(Near(TransformPoint(view, { 0, 0, 0 }), { 0, 0, -10 })) << "in front is -z";
            EXPECT_TRUE(Near(TransformPoint(view, { 1, 0, 0 }), { 1, 0, -10 })) << "+x is right";
            const float4x4 straightDown = LookAt({ 0, 10, 0 }, { 0, 0, 0 }, { 0, 1, 0 });
            EXPECT_TRUE(Near(TransformPoint(straightDown, { 0, 0, 0 }), { 0, 0, -10 })) << "looking straight down still works";

            const float4x4 proj = Perspective(kQuarter, 2.0f, 1.0f, 100.0f);
            EXPECT_NEAR(TransformPoint(proj, { 0, 0, -1 }).z, 0.0f, 1e-5f) << "near: depth 0";
            EXPECT_NEAR(TransformPoint(proj, { 0, 0, -100 }).z, 1.0f, 1e-5f) << "far: depth 1";
            EXPECT_LT(TransformPoint(proj, { 0, 1, -5 }).y, 0.0f) << "up is -y in Vulkan's clip space";
            EXPECT_NEAR(TransformPoint(proj, { 2, 0, -2 }).x, 0.5f, 1e-5f) << "x over the aspect";

            const float4x4 ortho = Orthographic(4.0f, 2.0f, 1.0f, 11.0f);
            EXPECT_TRUE(Near(TransformPoint(ortho, { 4, 2, -1 }), { 1, -1, 0 }));
            EXPECT_NEAR(TransformPoint(ortho, { 0, 0, -11 }).z, 1.0f, 1e-5f);
        }

        TEST(Math3D, RaysMeetPlanesAndLines)
        {
            const Ray down{ { 0, 5, 0 }, { 0, -1, 0 } };
            EXPECT_FLOAT_EQ(IntersectPlane(down, { 0, 0, 0 }, { 0, 1, 0 }), 5.0f);
            EXPECT_TRUE(std::isnan(IntersectPlane(down, { 0, 0, 0 }, { 1, 0, 0 }))) << "parallel, in the plane";
            EXPECT_TRUE(std::isnan(IntersectPlane(Ray{ { 1, 5, 0 }, { 0, -1, 0 } }, { 0, 0, 0 }, { 1, 0, 0 }))) << "parallel, beside it";
            EXPECT_TRUE(Near(down.At(2.0f), { 0, 3, 0 }));

            // A ray along -z at x = 3 passes the x axis closest at x = 3.
            const Ray across{ { 3, 1, 10 }, { 0, 0, -1 } };
            EXPECT_NEAR(ClosestOnLine(across, { 0, 0, 0 }, { 1, 0, 0 }), 3.0f, 1e-5f);
            EXPECT_TRUE(std::isnan(ClosestOnLine(across, { 0, 0, 0 }, { 0, 0, 1 }))) << "parallel";
        }
    }
}
