// Curves: evaluating them, the slopes each tangent mode works out, the presets, and the
// edits a CurveField makes (with their undo).

#include <gtest/gtest.h>

#include <gui_controls/gui_curve_types.h>

#include <cmath>
#include <limits>

namespace tachyon
{
    namespace
    {
        // A distinct, valid ID for a key.
        ID Id(uint64_t n)
        {
            ID id = ID::Invalid();
            id.value.integer = n << 8;
            return id;
        }

        CurveKey Key(float time, float value, CurveTangentMode mode = CurveTangentMode::ClampedAuto, uint32_t id = 0)
        {
            return CurveKey{ .stableID = id ? Id(id) : ID::Invalid(), .time = time, .value = value, .mode = mode };
        }

        CurveKey Linear(float time, float value)
        {
            return CurveKey{ .time = time, .value = value, .mode = CurveTangentMode::Broken,
                             .inMode = CurveSideMode::Linear, .outMode = CurveSideMode::Linear };
        }

        Curve Updated(std::vector<CurveKey> keys)
        {
            Curve curve{ .keys = std::move(keys) };
            UpdateCurveTangents(curve);
            return curve;
        }

        // ---------------------------------------------------------------------
        // Evaluating
        // ---------------------------------------------------------------------

        TEST(Curve, NoKeysIsZeroAndOneKeyIsItsValue)
        {
            EXPECT_EQ(EvaluateCurve(Curve{}, 0.5f), 0.0f);
            const Curve one = Updated({ Key(0.3f, 0.7f) });
            EXPECT_EQ(EvaluateCurve(one, -10.0f), 0.7f);
            EXPECT_EQ(EvaluateCurve(one, 0.3f), 0.7f);
            EXPECT_EQ(EvaluateCurve(one, 10.0f), 0.7f);
        }

        // Linear sides go straight from key to key; the keys' own values are hit exactly.
        TEST(Curve, LinearSidesAreStraight)
        {
            const Curve curve = Updated({ Linear(0.0f, 0.0f), Linear(1.0f, 2.0f), Linear(3.0f, 0.0f) });
            EXPECT_FLOAT_EQ(EvaluateCurve(curve, 0.0f), 0.0f);
            EXPECT_FLOAT_EQ(EvaluateCurve(curve, 0.25f), 0.5f);
            EXPECT_FLOAT_EQ(EvaluateCurve(curve, 1.0f), 2.0f);
            EXPECT_FLOAT_EQ(EvaluateCurve(curve, 2.0f), 1.0f);
            EXPECT_FLOAT_EQ(EvaluateCurve(curve, 3.0f), 0.0f);
            EXPECT_FLOAT_EQ(curve.keys[1].inSlope, 2.0f);
            EXPECT_FLOAT_EQ(curve.keys[1].outSlope, -1.0f);
        }

        // A Constant side holds the value up to the next key.
        TEST(Curve, ConstantStepsHold)
        {
            CurveKey a = Key(0.0f, 1.0f, CurveTangentMode::Broken);
            a.outMode = CurveSideMode::Constant;
            const Curve curve = Updated({ a, Key(1.0f, 3.0f) });
            EXPECT_EQ(EvaluateCurve(curve, 0.5f), 1.0f);
            EXPECT_EQ(EvaluateCurve(curve, 0.999f), 1.0f);
            EXPECT_EQ(EvaluateCurve(curve, 1.0f), 3.0f);

            // The next key's Constant arriving side holds the one before too.
            CurveKey b = Key(1.0f, 3.0f, CurveTangentMode::Broken);
            b.inMode = CurveSideMode::Constant;
            const Curve held = Updated({ Key(0.0f, 1.0f), b });
            EXPECT_EQ(EvaluateCurve(held, 0.5f), 1.0f);
        }

        // Flat keys ease in and out: halfway at the middle, slow at the ends.
        TEST(Curve, FlatKeysEaseInAndOut)
        {
            const Curve curve = Updated({ Key(0.0f, 0.0f, CurveTangentMode::Flat), Key(1.0f, 1.0f, CurveTangentMode::Flat) });
            EXPECT_FLOAT_EQ(EvaluateCurve(curve, 0.5f), 0.5f);
            EXPECT_NEAR(EvaluateCurve(curve, 0.25f), 0.15625f, 1e-5f);   // 3t^2 - 2t^3
            EXPECT_LT(EvaluateCurve(curve, 0.1f), 0.1f);
        }

        // Each slope sets its own side: a steep start, a level end.
        TEST(Curve, SlopesShapeTheSegment)
        {
            CurveKey a = Key(0.0f, 0.0f, CurveTangentMode::FreeSmooth);
            a.outSlope = 2.0f;
            CurveKey b = Key(1.0f, 1.0f, CurveTangentMode::FreeSmooth);
            const Curve curve = Updated({ a, b });
            EXPECT_NEAR(EvaluateCurve(curve, 0.5f), 0.75f, 1e-5f);   // h10 * 2 + h01 = 0.25 + 0.5
            EXPECT_NEAR(EvaluateCurve(curve, 0.01f), 0.02f, 1e-3f);
        }

        TEST(Curve, WrapModes)
        {
            Curve curve = Updated({ Linear(1.0f, 0.0f), Linear(2.0f, 1.0f) });
            EXPECT_EQ(EvaluateCurve(curve, 0.0f), 0.0f) << "clamp before";
            EXPECT_EQ(EvaluateCurve(curve, 5.0f), 1.0f) << "clamp after";

            curve.preWrap = curve.postWrap = CurveWrapMode::Loop;
            EXPECT_NEAR(EvaluateCurve(curve, 2.25f), 0.25f, 1e-5f);
            EXPECT_NEAR(EvaluateCurve(curve, 3.75f), 0.75f, 1e-5f);
            EXPECT_NEAR(EvaluateCurve(curve, 0.25f), 0.25f, 1e-5f);
            EXPECT_NEAR(EvaluateCurve(curve, -0.5f), 0.5f, 1e-5f);

            curve.preWrap = curve.postWrap = CurveWrapMode::PingPong;
            EXPECT_NEAR(EvaluateCurve(curve, 2.25f), 0.75f, 1e-5f) << "back down";
            EXPECT_NEAR(EvaluateCurve(curve, 3.25f), 0.25f, 1e-5f) << "up again";
            EXPECT_NEAR(EvaluateCurve(curve, 0.75f), 0.25f, 1e-5f) << "mirrored before";
            EXPECT_NEAR(EvaluateCurve(curve, -0.25f), 0.75f, 1e-5f);

            curve.postWrap = CurveWrapMode::Clamp;
            curve.preWrap  = CurveWrapMode::Loop;
            EXPECT_EQ(EvaluateCurve(curve, 9.0f), 1.0f) << "each end its own";
            EXPECT_NEAR(EvaluateCurve(curve, 0.25f), 0.25f, 1e-5f);
        }

        TEST(Curve, BadTimesCountAsZero)
        {
            const Curve curve = Updated({ Linear(0.0f, 2.0f), Linear(1.0f, 4.0f) });
            EXPECT_EQ(EvaluateCurve(curve, std::numeric_limits<float>::quiet_NaN()), 2.0f);
            EXPECT_EQ(EvaluateCurve(curve, std::numeric_limits<float>::infinity()), 2.0f);
        }

        // ---------------------------------------------------------------------
        // Tangents
        // ---------------------------------------------------------------------

        TEST(Curve, AutoIsTheSlopeBetweenTheNeighbors)
        {
            const Curve curve = Updated({ Key(0.0f, 0.0f, CurveTangentMode::Auto), Key(1.0f, 1.0f, CurveTangentMode::Auto),
                                          Key(3.0f, 5.0f, CurveTangentMode::Auto) });
            EXPECT_FLOAT_EQ(curve.keys[1].inSlope, 5.0f / 3.0f);
            EXPECT_FLOAT_EQ(curve.keys[1].outSlope, 5.0f / 3.0f);
            EXPECT_EQ(curve.keys[0].outSlope, 0.0f) << "an end key is level";
            EXPECT_EQ(curve.keys[2].inSlope, 0.0f);
        }

        // Clamped auto: a peak stays level, and a slope is held to three times the gentler side's.
        TEST(Curve, ClampedAutoDoesntOvershoot)
        {
            // Lopsided, so the slope between the neighbors isn't level by itself.
            const Curve peak = Updated({ Key(0.0f, 0.0f), Key(1.0f, 1.0f), Key(3.0f, 0.5f) });
            EXPECT_EQ(peak.keys[1].outSlope, 0.0f);
            const Curve trough = Updated({ Key(0.0f, 1.0f), Key(1.0f, 0.0f), Key(3.0f, 0.5f) });
            EXPECT_EQ(trough.keys[1].inSlope, 0.0f);

            // Rising steeply then barely: the auto slope (5.05) is held to 3 x 0.1.
            const Curve held = Updated({ Key(0.0f, 0.0f), Key(1.0f, 10.0f), Key(2.0f, 10.1f) });
            EXPECT_NEAR(held.keys[1].outSlope, 0.3f, 1e-5f);
            EXPECT_EQ(held.keys[1].inSlope, held.keys[1].outSlope);
            // Gently: the plain auto slope.
            const Curve gentle = Updated({ Key(0.0f, 0.0f), Key(1.0f, 1.0f), Key(2.0f, 2.5f) });
            EXPECT_NEAR(gentle.keys[1].outSlope, 1.25f, 1e-5f);
            // Falling: held the same way.
            const Curve falling = Updated({ Key(0.0f, 10.1f), Key(1.0f, 10.0f), Key(2.0f, 0.0f) });
            EXPECT_NEAR(falling.keys[1].outSlope, -0.3f, 1e-5f);
        }

        TEST(Curve, FlatAndFreeSmooth)
        {
            CurveKey flat = Key(1.0f, 1.0f, CurveTangentMode::Flat);
            flat.inSlope = 4.0f;
            CurveKey smooth = Key(2.0f, 0.0f, CurveTangentMode::FreeSmooth);
            smooth.inSlope = 1.0f;
            smooth.outSlope = -3.0f;
            const Curve curve = Updated({ Key(0.0f, 0.0f), flat, smooth });
            EXPECT_EQ(curve.keys[1].inSlope, 0.0f);
            EXPECT_EQ(curve.keys[1].outSlope, 0.0f);
            EXPECT_EQ(curve.keys[2].inSlope, -3.0f) << "one slope, the leaving one";
            EXPECT_EQ(curve.keys[2].outSlope, -3.0f);
        }

        // A broken key's sides: free keeps its slope, linear points at the neighbor, constant is level.
        TEST(Curve, BrokenSides)
        {
            CurveKey k = Key(1.0f, 1.0f, CurveTangentMode::Broken);
            k.inMode = CurveSideMode::Free;
            k.inSlope = 7.0f;
            k.outMode = CurveSideMode::Linear;
            Curve curve = Updated({ Key(0.0f, 0.0f), k, Key(3.0f, 0.0f) });
            EXPECT_EQ(curve.keys[1].inSlope, 7.0f);
            EXPECT_FLOAT_EQ(curve.keys[1].outSlope, -0.5f);
            curve.keys[1].inMode = CurveSideMode::Constant;
            curve.keys[1].outMode = CurveSideMode::Constant;
            UpdateCurveTangents(curve);
            EXPECT_EQ(curve.keys[1].inSlope, 0.0f);
            EXPECT_EQ(curve.keys[1].outSlope, 0.0f);
            // An end's linear side has no neighbor: level.
            Curve ends = Updated({ Linear(0.0f, 0.0f), Linear(1.0f, 1.0f) });
            EXPECT_EQ(ends.keys[0].inSlope, 0.0f);
            EXPECT_EQ(ends.keys[1].outSlope, 0.0f);
        }

        TEST(Curve, MakeCurveSortsAndWorksOutTheSlopes)
        {
            const float2 points[] = { { 1.0f, 1.0f }, { 0.0f, 0.0f }, { 2.0f, 4.0f } };
            const Curve curve = MakeCurve(points, CurveTangentMode::Auto);
            ASSERT_EQ(curve.keys.size(), 3u);
            EXPECT_EQ(curve.keys[0].time, 0.0f);
            EXPECT_EQ(curve.keys[2].time, 2.0f);
            EXPECT_FLOAT_EQ(curve.keys[1].outSlope, 2.0f);
        }

        TEST(Curve, ThePresets)
        {
            const auto presets = DefaultCurvePresets();
            ASSERT_EQ(presets.size(), 7u);
            EXPECT_EQ(presets[0].name, "Constant");
            EXPECT_EQ(EvaluateCurve(presets[0].curve, 0.5f), 1.0f);
            EXPECT_FLOAT_EQ(EvaluateCurve(presets[1].curve, 0.3f), 0.3f) << "linear";
            EXPECT_FLOAT_EQ(EvaluateCurve(presets[2].curve, 0.3f), 0.7f) << "linear, falling";
            EXPECT_LT(EvaluateCurve(presets[3].curve, 0.5f), 0.5f) << "ease in starts slow";
            EXPECT_GT(EvaluateCurve(presets[4].curve, 0.5f), 0.5f) << "ease out starts fast";
            EXPECT_FLOAT_EQ(EvaluateCurve(presets[5].curve, 0.5f), 0.5f);
            EXPECT_FLOAT_EQ(EvaluateCurve(presets[6].curve, 0.25f), 1.0f - 0.15625f);
            for (const CurvePreset& preset : presets)
            {
                EXPECT_EQ(preset.curve.keys.front().time, 0.0f) << preset.name;
                EXPECT_EQ(preset.curve.keys.back().time, 1.0f) << preset.name;
            }
        }

        // ---------------------------------------------------------------------
        // Edits
        // ---------------------------------------------------------------------

        class CurveEdits : public ::testing::Test
        {
        protected:
            CurveView view;
            std::vector<CurveEdit> edits;
            uint32_t nextID = 100;

            void SetUp() override
            {
                view.channels = { CurveChannel{ .name = "R", .color = CurveChannelColor::Red,
                                                .curve = Updated({ Key(0.0f, 0.0f, CurveTangentMode::ClampedAuto, 1),
                                                                   Key(0.5f, 0.8f, CurveTangentMode::ClampedAuto, 2),
                                                                   Key(1.0f, 1.0f, CurveTangentMode::ClampedAuto, 3) }) },
                                  CurveChannel{ .name = "G", .color = CurveChannelColor::Green,
                                                .curve = Updated({ Key(0.0f, 1.0f, CurveTangentMode::ClampedAuto, 4),
                                                                   Key(1.0f, 0.0f, CurveTangentMode::ClampedAuto, 5) }) } };
                view.onEdit = [this](const CurveEdit& edit) { edits.push_back(edit); };
            }
            ID Make() { return Id(nextID++); }
        };

        TEST_F(CurveEdits, AddingAKey)
        {
            const ID key = Make();
            const CurveEdit add = AddCurveKeyEdit(view, 1, 0.25f, 0.6f, key);
            ASSERT_EQ(add.changes.size(), 1u);
            EXPECT_EQ(add.changes[0].channel, 1);
            CommitCurveEdit(view, add);
            ASSERT_EQ(edits.size(), 1u);
            EXPECT_EQ(view.revision, 1u);
            const auto& keys = view.channels[1].curve.keys;
            ASSERT_EQ(keys.size(), 3u);
            EXPECT_EQ(keys[1].stableID, key) << "in time order";
            EXPECT_EQ(keys[1].value, 0.6f);

            ApplyCurveEdit(view, add, /*undo*/ true);
            EXPECT_EQ(view.channels[1].curve.keys.size(), 2u);
            ApplyCurveEdit(view, add);
            EXPECT_EQ(view.channels[1].curve.keys.size(), 3u);

            EXPECT_TRUE(AddCurveKeyEdit(view, 1, 0.25f + 1e-6f, 0.1f, Make()).empty()) << "one there already";
            EXPECT_TRUE(AddCurveKeyEdit(view, 5, 0.3f, 0.1f, Make()).empty());
            EXPECT_TRUE(AddCurveKeyEdit(view, -1, 0.3f, 0.1f, Make()).empty());
            view.channels[0].visible = false;
            EXPECT_TRUE(AddCurveKeyEdit(view, 0, 0.3f, 0.1f, Make()).empty()) << "a hidden channel";
        }

        // A key is kept in the range's time; in its values too with clampValues.
        TEST_F(CurveEdits, AddedKeysStayInTheRange)
        {
            EXPECT_TRUE(AddCurveKeyEdit(view, 1, 3.0f, 5.0f, Make()).empty()) << "held to 1, where there's a key";
            EXPECT_EQ(AddCurveKeyEdit(view, 1, 0.75f, 5.0f, Make()).changes[0].to.keys[1].value, 5.0f) << "values free";
            view.clampValues = true;
            EXPECT_EQ(AddCurveKeyEdit(view, 1, 0.75f, 5.0f, Make()).changes[0].to.keys[1].value, 1.0f);
            EXPECT_EQ(AddCurveKeyEdit(view, 1, 0.75f, -5.0f, Make()).changes[0].to.keys[1].value, 0.0f);
            view.range = { -2.0f, 2.0f, -1.0f, 3.0f };
            EXPECT_EQ(AddCurveKeyEdit(view, 1, -3.0f, 0.5f, Make()).changes[0].to.keys.front().time, -2.0f);
            EXPECT_EQ(AddCurveKeyEdit(view, 1, 3.0f, 0.5f, Make()).changes[0].to.keys.back().time, 2.0f);
            EXPECT_EQ(AddCurveKeyEdit(view, 1, 0.25f, 5.0f, Make()).changes[0].to.keys[1].value, 3.0f);
        }

        // The selected keys go from every channel; what's gone isn't selected any more.
        TEST_F(CurveEdits, DeletingTheSelectedKeys)
        {
            view.selectedKeys = { Id(2), Id(5) };
            const CurveEdit edit = DeleteSelectedCurveKeysEdit(view);
            ASSERT_EQ(edit.changes.size(), 2u);
            CommitCurveEdit(view, edit);
            EXPECT_EQ(view.channels[0].curve.keys.size(), 2u);
            EXPECT_EQ(view.channels[1].curve.keys.size(), 1u);
            EXPECT_TRUE(view.selectedKeys.empty());
            ApplyCurveEdit(view, edit, true);
            EXPECT_EQ(view.channels[0].curve.keys.size(), 3u);
            EXPECT_TRUE(DeleteSelectedCurveKeysEdit(view).empty()) << "nothing selected";
        }

        TEST_F(CurveEdits, TangentModes)
        {
            view.selectedKeys = { Id(2) };
            CommitCurveEdit(view, SetCurveTangentModeEdit(view, CurveTangentMode::Flat));
            EXPECT_EQ(view.channels[0].curve.keys[1].mode, CurveTangentMode::Flat);
            EXPECT_EQ(view.channels[0].curve.keys[1].outSlope, 0.0f);
            EXPECT_EQ(view.channels[0].curve.keys[0].mode, CurveTangentMode::ClampedAuto) << "only the selected";
            EXPECT_TRUE(SetCurveTangentModeEdit(view, CurveTangentMode::Flat).empty()) << "already";

            // Broken from another mode: each side free, where its slope was.
            view.channels[0].curve.keys[1].inMode = CurveSideMode::Constant;
            CommitCurveEdit(view, SetCurveTangentModeEdit(view, CurveTangentMode::Broken));
            EXPECT_EQ(view.channels[0].curve.keys[1].inMode, CurveSideMode::Free);
            EXPECT_EQ(view.channels[0].curve.keys[1].outMode, CurveSideMode::Free);

            // A side's mode breaks it, and sets that side only.
            view.selectedKeys = { Id(3) };
            view.channels[0].curve.keys[2].outMode = CurveSideMode::Constant;
            CommitCurveEdit(view, SetCurveSideModeEdit(view, CurveSideMode::Linear, /*in*/ true, /*out*/ false));
            const CurveKey& last = view.channels[0].curve.keys[2];
            EXPECT_EQ(last.mode, CurveTangentMode::Broken);
            EXPECT_EQ(last.inMode, CurveSideMode::Linear);
            EXPECT_EQ(last.outMode, CurveSideMode::Free) << "free once broken from another mode";
            EXPECT_FLOAT_EQ(last.inSlope, (1.0f - 0.8f) / 0.5f);
            CommitCurveEdit(view, SetCurveSideModeEdit(view, CurveSideMode::Constant, false, true));
            EXPECT_EQ(view.channels[0].curve.keys[2].inMode, CurveSideMode::Linear) << "the other side stays";
            EXPECT_EQ(view.channels[0].curve.keys[2].outMode, CurveSideMode::Constant);
        }

        // A preset is stretched over the range, its slopes with it, with new keys.
        TEST_F(CurveEdits, APresetStretchedOverTheRange)
        {
            view.range = { 0.0f, 2.0f, 10.0f, 14.0f };
            view.channels[1].curve.postWrap = CurveWrapMode::Loop;
            const CurvePreset& easeOut = DefaultCurvePresets()[4];
            const CurveEdit edit = ApplyCurvePresetEdit(view, 1, easeOut, [this] { return Make(); });
            ASSERT_EQ(edit.changes.size(), 1u);
            const Curve& to = edit.changes[0].to;
            ASSERT_EQ(to.keys.size(), 2u);
            EXPECT_EQ(to.keys[0].time, 0.0f);
            EXPECT_EQ(to.keys[1].time, 2.0f);
            EXPECT_EQ(to.keys[0].value, 10.0f);
            EXPECT_EQ(to.keys[1].value, 14.0f);
            EXPECT_FLOAT_EQ(to.keys[0].outSlope, 2.0f * 4.0f / 2.0f) << "the slope stretched too";
            EXPECT_EQ(to.keys[0].stableID, Id(100));
            EXPECT_EQ(to.keys[1].stableID, Id(101));
            EXPECT_EQ(to.postWrap, CurveWrapMode::Loop) << "the wrap modes stay";
            EXPECT_TRUE(ApplyCurvePresetEdit(view, 7, easeOut, [this] { return Make(); }).empty());
        }

        TEST_F(CurveEdits, WrapModes)
        {
            CommitCurveEdit(view, SetCurveWrapEdit(view, 0, CurveWrapMode::PingPong, CurveWrapMode::Loop));
            EXPECT_EQ(view.channels[0].curve.preWrap, CurveWrapMode::PingPong);
            EXPECT_EQ(view.channels[0].curve.postWrap, CurveWrapMode::Loop);
            EXPECT_EQ(view.channels[1].curve.preWrap, CurveWrapMode::Clamp);
            EXPECT_TRUE(SetCurveWrapEdit(view, 0, CurveWrapMode::PingPong, CurveWrapMode::Loop).empty());
            EXPECT_TRUE(SetCurveWrapEdit(view, 2, CurveWrapMode::Loop, CurveWrapMode::Loop).empty());
        }

        TEST_F(CurveEdits, AnEmptyEditIsntCommitted)
        {
            CommitCurveEdit(view, CurveEdit{});
            EXPECT_TRUE(edits.empty());
            EXPECT_EQ(view.revision, 0u);
        }
    }
}
