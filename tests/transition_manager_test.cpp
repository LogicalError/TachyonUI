// TransitionManager: each channel (hover, press, focus, selected, invalid) runs on its own
// timing, and a change mid-transition carries on from what is on screen. A box mixes the
// styles of the states its running channels are between. Toggles blend their on / off;
// reduced motion makes it all instant.

#include <gtest/gtest.h>

#include "headless_gui_app.h"
#include "skribidi_test_fixture.h"

#include <gui_core/gui_easing.h>
#include <gui_controls/gui_transition_manager.h>
#include <gui_core/graphics/gui_custom_draw_context.h>
#include <gui_core/style/gui_style.h>

#include <optional>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        constexpr VisualBoxStyleIndex kBox = VisualBoxStyleIndex(1);
        const ID kElement = [] { ID id; id.value.integer = 1; return id; }();
        constexpr float kHoverIn  = TransitionManager::kHoverIn.duration;
        constexpr float kHoverOut = TransitionManager::kHoverOut.duration;
        using C = TransitionChannel;

        // How much of `state`'s style the blend shows: 0 none of it, 1 only it.
        float Amount(const TransitionState& r, ElementState state)
        {
            return r.styles.WeightOf(ComputeGPUStyleIndex(kBox, state));
        }
        float Total(const TransitionState& r)
        {
            float total = 0.0f;
            for (int i = 0; i < r.styles.count; ++i)
                total += r.styles.weights[size_t(i)];
            return total;
        }

        TEST(Easing, CurvesRunFromStartToEnd)
        {
            for (const CubicBezier& curve : { Easing::Linear, Easing::EaseOut, Easing::EaseInOut })
            {
                EXPECT_FLOAT_EQ(curve(0.0f), 0.0f);
                EXPECT_FLOAT_EQ(curve(1.0f), 1.0f);
                float last = 0.0f;
                for (float t = 0.05f; t < 1.0f; t += 0.05f)
                {
                    EXPECT_GE(curve(t), last) << "never goes back, at " << t;
                    last = curve(t);
                    EXPECT_NEAR(curve.TimeAt(curve(t)), t, 1e-3f) << "TimeAt undoes it, at " << t;
                }
            }
            EXPECT_NEAR(Easing::Linear(0.3f), 0.3f, 1e-4f);
            EXPECT_GT(Easing::EaseOut(0.5f), 0.8f) << "most of the way by half time";
            EXPECT_LT(Easing::EaseInOut(0.2f), 0.2f) << "slow to start";
            EXPECT_GT(Easing::EaseInOut(0.8f), 0.8f) << "slow to end";
        }

        TEST(TransitionManager, EachChannelHasItsTiming)
        {
            auto timing = [](C channel, bool on) { return TransitionManager::TimingFor(channel, on); };
            EXPECT_EQ(timing(C::Hover, true), TransitionManager::kHoverIn);
            EXPECT_EQ(timing(C::Hover, false), TransitionManager::kHoverOut);
            EXPECT_EQ(timing(C::Active, true).duration, 0.0f) << "a press";
            EXPECT_EQ(timing(C::Active, false), TransitionManager::kRelease);
            EXPECT_EQ(timing(C::Focused, true), TransitionManager::kFocus);
            EXPECT_EQ(timing(C::Focused, false), TransitionManager::kFocus);
            EXPECT_EQ(timing(C::Selected, true).duration, 0.0f) << "a selection, unless it's a toggle's";
            EXPECT_EQ(timing(C::Invalid, true), TransitionManager::kInvalid) << "the halo grows in";
            EXPECT_EQ(timing(C::Invalid, false).duration, 0.0f) << "and goes at once";
            EXPECT_EQ(TransitionManager::kToggle.easing, Easing::EaseInOut);
            EXPECT_EQ(TransitionManager::kHoverIn.easing, Easing::EaseOut);
        }

        TEST(TransitionManager, AStateChangeBlendsToTheNewState)
        {
            TransitionManager transitions;
            transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover, 0.0f);
            EXPECT_FLOAT_EQ(Amount(transitions.Resolve(kElement, kBox, ElementState::Hover, 0.0f), ElementState::Hover), 0.0f);
            const TransitionState halfway = transitions.Resolve(kElement, kBox, ElementState::Hover, kHoverIn / 2);
            EXPECT_NEAR(Amount(halfway, ElementState::Hover), Easing::EaseOut(0.5f), 1e-4f);
            EXPECT_NEAR(Total(halfway), 1.0f, 1e-5f);
            EXPECT_EQ(halfway.current, ComputeGPUStyleIndex(kBox, ElementState::Hover)) << "the state it ends in";
            EXPECT_FLOAT_EQ(Amount(transitions.Resolve(kElement, kBox, ElementState::Hover, kHoverIn), ElementState::Hover), 1.0f);
        }

        // Hover out takes twice as long as hover in.
        TEST(TransitionManager, HoverOutTakesLongerThanHoverIn)
        {
            TransitionManager transitions;
            transitions.OnStateChange(kElement, ElementState::Hover, ElementState::None, 0.0f);
            const float at = kHoverIn;
            EXPECT_LT(Amount(transitions.Resolve(kElement, kBox, ElementState::None, at), ElementState::None), 1.0f)
                << "not done after the hover-in time";
            EXPECT_FLOAT_EQ(Amount(transitions.Resolve(kElement, kBox, ElementState::None, kHoverOut), ElementState::None), 1.0f);
        }

        // A press shows at once, mid-hover too; the hover carries on under it.
        TEST(TransitionManager, APressShowsAtOnceAndTheHoverCarriesOn)
        {
            TransitionManager transitions;
            const ElementState pressed = ElementState::Hover | ElementState::Active;
            transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover, 0.0f);
            const float now = kHoverIn * 0.3f;
            const float hovered = Amount(transitions.Resolve(kElement, kBox, ElementState::Hover, now), ElementState::Hover);
            transitions.OnStateChange(kElement, ElementState::Hover, pressed, now);
            const TransitionState r = transitions.Resolve(kElement, kBox, pressed, now);
            EXPECT_NEAR(Amount(r, pressed) + Amount(r, ElementState::Active), 1.0f, 1e-5f) << "all pressed";
            EXPECT_NEAR(Amount(r, pressed), hovered, 1e-4f) << "the hover where it was";
            EXPECT_FALSE(transitions.Value(kElement, C::Active, now).has_value());
            EXPECT_TRUE(transitions.Value(kElement, C::Hover, now).has_value());
        }

        // Hovering out before the hover blend finished runs back from where it got to,
        // in the hover-out timing, instead of starting from the full hover style.
        TEST(TransitionManager, ReversingMidBlendCarriesOnFromWhereItIs)
        {
            TransitionManager transitions;
            transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover, 0.0f);
            const float now = kHoverIn * 0.3f;
            const float shown = Amount(transitions.Resolve(kElement, kBox, ElementState::Hover, now), ElementState::Hover);
            ASSERT_GT(shown, 0.0f);
            ASSERT_LT(shown, 1.0f);

            transitions.OnStateChange(kElement, ElementState::Hover, ElementState::None, now);
            EXPECT_NEAR(Amount(transitions.Resolve(kElement, kBox, ElementState::None, now), ElementState::Hover), shown, 1e-4f)
                << "no jump when it turns around";

            // It runs back down, and is done sooner than a whole hover out.
            const float later = Amount(transitions.Resolve(kElement, kBox, ElementState::None, now + kHoverOut * 0.1f), ElementState::Hover);
            EXPECT_LT(later, shown);
            const float left = (1.0f - Easing::EaseOut.TimeAt(1.0f - shown)) * kHoverOut;
            ASSERT_LT(left, kHoverOut);
            EXPECT_GT(Amount(transitions.Resolve(kElement, kBox, ElementState::None, now + left * 0.9f), ElementState::Hover), 0.0f);
            EXPECT_FLOAT_EQ(Amount(transitions.Resolve(kElement, kBox, ElementState::None, now + left + 1e-4f), ElementState::Hover), 0.0f);
        }

        // The same change again while it runs (the hover seen twice): it carries on, not from the start.
        TEST(TransitionManager, TheSameChangeAgainCarriesOn)
        {
            TransitionManager transitions;
            transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover, 0.0f);
            const float now = kHoverIn * 0.5f;
            transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover, now);
            EXPECT_NEAR(*transitions.Value(kElement, C::Hover, now), Easing::EaseOut(0.5f), 1e-4f);
        }

        // In and out several times quickly: never a jump.
        TEST(TransitionManager, RepeatedReversalsNeverJump)
        {
            TransitionManager transitions;
            ElementState state = ElementState::None;
            float now = 0.0f;
            float shown = 0.0f;
            for (int i = 0; i < 6; ++i)
            {
                const ElementState next = state == ElementState::None ? ElementState::Hover : ElementState::None;
                transitions.OnStateChange(kElement, state, next, now);
                EXPECT_NEAR(Amount(transitions.Resolve(kElement, kBox, next, now), ElementState::Hover), shown, 1e-4f) << "reversal " << i;
                state = next;
                now += kHoverIn * 0.35f;
                shown = Amount(transitions.Resolve(kElement, kBox, state, now), ElementState::Hover);
            }
        }

        // Focus coming in mid-hover: each runs on its own, and the box mixes the four states
        // between them, each by how far both are.
        TEST(TransitionManager, TwoChannelsBlendOnTheirOwn)
        {
            TransitionManager transitions;
            const ElementState both = ElementState::Hover | ElementState::Focused;
            transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover, 0.0f);
            const float start = kHoverIn * 0.3f;
            const float h0 = Easing::EaseOut(0.3f);
            transitions.OnStateChange(kElement, ElementState::Hover, both, start);
            const TransitionState r = transitions.Resolve(kElement, kBox, both, start);
            EXPECT_NEAR(Amount(r, ElementState::Hover), h0, 1e-4f) << "the hover where it was, no focus yet";
            EXPECT_NEAR(Amount(r, ElementState::None), 1.0f - h0, 1e-4f);

            const float now = start + TransitionManager::kFocus.duration / 2;
            const float h = Easing::EaseOut(std::min(1.0f, now / kHoverIn));
            const float f = Easing::EaseOut(0.5f);
            const TransitionState mid = transitions.Resolve(kElement, kBox, both, now);
            EXPECT_NEAR(Amount(mid, both), h * f, 1e-4f);
            EXPECT_NEAR(Amount(mid, ElementState::Focused), (1.0f - h) * f, 1e-4f);
            EXPECT_NEAR(Amount(mid, ElementState::Hover), h * (1.0f - f), 1e-4f);
            EXPECT_NEAR(Total(mid), 1.0f, 1e-5f);

            EXPECT_FLOAT_EQ(Amount(transitions.Resolve(kElement, kBox, both, start + 1.0f), both), 1.0f) << "both done";
        }

        // Three channels at once would mix eight states: the four with the most share are
        // drawn, scaled to add up to all of it.
        TEST(TransitionManager, AtMostFourStyles)
        {
            TransitionManager transitions;
            const ElementState all = ElementState::Hover | ElementState::Focused | ElementState::Selected;
            transitions.OnStateSeen(kElement, ElementState::None, ElementState::Selected, 0.0f);
            transitions.Purge(0.0f);
            transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover | ElementState::Focused, 0.01f);
            transitions.OnStateSeen(kElement, all, ElementState::Selected, 0.01f);
            const TransitionState r = transitions.Resolve(kElement, kBox, all, 0.04f);
            EXPECT_EQ(r.styles.count, StyleBlend::kMaxStyles);
            EXPECT_NEAR(Total(r), 1.0f, 1e-5f);
            for (int i = 1; i < r.styles.count; ++i)
                EXPECT_LE(r.styles.weights[size_t(i)], r.styles.weights[0]) << "the heaviest first";
        }

        // Turning disabled ends what runs: it shows its disabled look at once.
        TEST(TransitionManager, DisablingEndsWhatRuns)
        {
            TransitionManager transitions;
            transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover, 0.0f);
            transitions.OnStateChange(kElement, ElementState::Hover, ElementState::Hover | ElementState::Disabled, 0.01f);
            EXPECT_FALSE(transitions.IsRunning(kElement));
        }

        // One in the middle done: the others keep their own, the one moved into its place too.
        TEST(TransitionManager, LettingOneGoInTheMiddleKeepsTheOthers)
        {
            TransitionManager transitions;
            auto element = [](uint64_t n) { ID id; id.value.integer = n; return id; };
            transitions.OnStateChange(element(1), ElementState::None, ElementState::Focused, 0.0f);   // 100 ms
            transitions.OnStateChange(element(2), ElementState::None, ElementState::Hover, 0.0f);     // 70 ms
            transitions.OnStateChange(element(3), ElementState::None, ElementState::Hover, 0.05f);
            transitions.Purge(0.08f);
            EXPECT_FALSE(transitions.IsRunning(element(2)));
            EXPECT_NEAR(*transitions.Value(element(1), C::Focused, 0.08f), Easing::EaseOut(0.8f), 1e-4f);
            EXPECT_NEAR(*transitions.Value(element(3), C::Hover, 0.08f), Easing::EaseOut(0.03f / kHoverIn), 1e-4f);
            EXPECT_FALSE(transitions.Value(element(3), C::Focused, 0.08f).has_value());
        }

        // Disabled is one style whatever else it's in: nothing to mix.
        TEST(TransitionManager, DisabledIsOneStyle)
        {
            TransitionManager transitions;
            transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover, 0.0f);
            const TransitionState r = transitions.Resolve(kElement, kBox, ElementState::Hover | ElementState::Disabled, kHoverIn / 2);
            EXPECT_EQ(r.styles.count, 1);
            EXPECT_FLOAT_EQ(Amount(r, ElementState::Disabled), 1.0f);
        }

        TEST(TransitionManager, ReducedMotionMakesEveryChangeInstant)
        {
            TransitionManager transitions;
            transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover, 0.0f);
            ASSERT_TRUE(transitions.IsRunning(kElement));
            transitions.SetReducedMotion(true);
            EXPECT_FALSE(transitions.IsRunning(kElement)) << "the running one ends";
            transitions.OnStateChange(kElement, ElementState::Hover, ElementState::None, 0.02f);
            EXPECT_FALSE(transitions.IsRunning(kElement));
            transitions.OnStateSeen(kElement, ElementState::None, ElementState::Selected, 0.03f);
            transitions.Purge(0.03f);
            transitions.OnStateSeen(kElement, ElementState::Selected, ElementState::Selected, 0.04f);
            EXPECT_FALSE(transitions.IsRunning(kElement)) << "a toggle too";

            transitions.SetReducedMotion(false);
            transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover, 0.05f);
            EXPECT_TRUE(transitions.IsRunning(kElement));
        }

        // Done: the element has nothing running, and Purge lets its slot go.
        TEST(TransitionManager, AFinishedTransitionIsLetGo)
        {
            TransitionManager transitions;
            transitions.OnStateChange(kElement, ElementState::None, ElementState::Hover, 0.0f);
            EXPECT_TRUE(transitions.HasRunning());
            transitions.Purge(kHoverIn / 2);
            EXPECT_TRUE(transitions.HasRunning());
            transitions.Purge(kHoverIn);
            EXPECT_FALSE(transitions.HasRunning());
            EXPECT_FALSE(transitions.IsRunning(kElement));
        }

        // An element that animates its Selected blends when it flips between frames; the first
        // frame it's seen, or after a frame it wasn't built, it shows as it is.
        TEST(TransitionManager, ASelectedFlipBlendsLikeAToggle)
        {
            TransitionManager transitions;
            transitions.OnStateSeen(kElement, ElementState::Selected, ElementState::Selected, 0.0f);
            EXPECT_FALSE(transitions.IsRunning(kElement)) << "first seen";
            transitions.Purge(0.0f);

            transitions.OnStateSeen(kElement, ElementState::None, ElementState::Selected, 0.1f);
            const auto on = transitions.Value(kElement, C::Selected, 0.1f + TransitionManager::kToggle.duration / 2);
            ASSERT_TRUE(on.has_value());
            EXPECT_NEAR(*on, 1.0f - Easing::EaseInOut(0.5f), 1e-4f) << "half way off";
            transitions.Purge(0.1f);

            transitions.OnStateSeen(kElement, ElementState::None, ElementState::Selected, 0.12f);
            EXPECT_TRUE(transitions.IsRunning(kElement)) << "same value: carries on";
            transitions.Purge(0.12f);
            transitions.Purge(0.13f);   // a frame it wasn't built
            transitions.Purge(0.5f);
            transitions.OnStateSeen(kElement, ElementState::Selected, ElementState::Selected, 0.5f);
            EXPECT_FALSE(transitions.IsRunning(kElement)) << "not built the frame before";
        }

        // Invalid is watched for every element: turning invalid runs kInvalid, turning valid
        // ends at once. A Selected flip on an element that doesn't animate it isn't watched.
        TEST(TransitionManager, AnInvalidFlipGrowsItsHalo)
        {
            TransitionManager transitions;
            transitions.OnStateSeen(kElement, ElementState::Hover, ElementState::Invalid, 0.0f);
            transitions.Purge(0.0f);
            transitions.OnStateSeen(kElement, ElementState::Hover | ElementState::Invalid, ElementState::Invalid, 0.1f);
            const auto grown = transitions.Value(kElement, C::Invalid, 0.1f + TransitionManager::kInvalid.duration / 2);
            ASSERT_TRUE(grown.has_value());
            EXPECT_NEAR(*grown, Easing::EaseOut(0.5f), 1e-4f);
            EXPECT_FALSE(transitions.Value(kElement, C::Hover, 0.1f).has_value()) << "only invalid changed";
            EXPECT_TRUE(transitions.IsWatched(kElement));
            transitions.Purge(0.1f);

            transitions.OnStateSeen(kElement, ElementState::Hover, ElementState::Invalid, 0.12f);
            EXPECT_FALSE(transitions.IsRunning(kElement)) << "valid again: at once";
            transitions.Purge(0.12f);

            transitions.OnStateSeen(kElement, ElementState::Selected, ElementState::Invalid, 0.13f);
            EXPECT_FALSE(transitions.IsRunning(kElement)) << "Selected isn't watched here";
            transitions.Purge(0.13f);
            transitions.Purge(0.14f);
            EXPECT_FALSE(transitions.IsWatched(kElement)) << "forgotten once not built";
        }

        // Many elements: each keeps its own, and letting some go leaves the others as they were.
        TEST(TransitionManager, ManyElementsKeepTheirOwn)
        {
            TransitionManager transitions;
            auto element = [](uint64_t n) { ID id; id.value.integer = n; return id; };
            for (uint64_t n = 1; n <= 20; ++n)
                transitions.OnStateChange(element(n), ElementState::None, ElementState::Hover, float(n) * 0.005f);
            transitions.Purge(0.071f);   // the first few done
            for (uint64_t n = 1; n <= 20; ++n)
            {
                const float start = float(n) * 0.005f;
                EXPECT_EQ(transitions.IsRunning(element(n)), 0.071f - start < kHoverIn) << n;
                if (transitions.IsRunning(element(n)))
                    EXPECT_NEAR(Amount(transitions.Resolve(element(n), kBox, ElementState::Hover, 0.071f), ElementState::Hover),
                                Easing::EaseOut((0.071f - start) / kHoverIn), 1e-4f) << n;
            }
        }

        // ---- In the controls ----

        // The elements of the last frame marked AnimatesSelected.
        std::vector<ID> TogglingParts(const HeadlessGUIApp& app)
        {
            std::vector<ID> ids;
            for (uint32_t depth = 0; app.elementStorage.IsValidRef(ElementRef{ .depth = depth, .index = 0 }); ++depth)
                for (uint32_t index = 0; app.elementStorage.IsValidRef(ElementRef{ .depth = depth, .index = index }); ++index)
                {
                    const Element& e = app.elementStorage.GetElementUnsafe(ElementRef{ .depth = depth, .index = index });
                    if ((e.flags & ElementFlags::AnimatesSelected) != ElementFlags::None)
                        ids.push_back(e.id);
                }
            return ids;
        }

        class ToggleTransitionTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            ID control{};
            bool value = false;
            enum class Kind { Switch, Checkbox, Toggle } kind = Kind::Switch;

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                app.transitionManager.SetEnabled(true);
                control = app.idManager.CreateStaticID();
            }
            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow();
                    switch (kind)
                    {
                        case Kind::Switch:   app.Switch(control, value, "Switch", [](bool) {}); break;
                        case Kind::Checkbox: app.Checkbox(control, value, "Check", [](bool) {}); break;
                        case Kind::Toggle:   app.Toggle(control, value, GUIContent{ "Toggle" }, [](bool) {}); break;
                    }
                    app.EndRootWindow();
                });
            }

            float Now() const { return app.guiStateManager.Interaction().GetFrameTime(); }

            // How far on the part that turns on or off is, while it's turning.
            std::optional<float> PartOn()
            {
                const std::vector<ID> parts = TogglingParts(app);
                EXPECT_EQ(parts.size(), 1u);
                return parts.empty() ? std::nullopt : app.transitionManager.Value(parts[0], TransitionChannel::Selected, Now());
            }
        };

        TEST_F(ToggleTransitionTest, TurningOnBlendsFor120ms)
        {
            for (const Kind k : { Kind::Switch, Kind::Checkbox, Kind::Toggle })
            {
                kind = k;
                value = false;
                Frame();
                Frame();
                EXPECT_FALSE(PartOn().has_value()) << int(k);

                value = true;
                Frame();
                auto on = PartOn();
                ASSERT_TRUE(on.has_value()) << int(k);
                EXPECT_FLOAT_EQ(*on, 0.0f);

                app.AdvanceTime(60);
                Frame();
                on = PartOn();
                ASSERT_TRUE(on.has_value()) << int(k);
                EXPECT_GT(*on, 0.0f);
                EXPECT_LT(*on, 1.0f);

                app.AdvanceTime(61);
                Frame();
                EXPECT_FALSE(PartOn().has_value()) << int(k) << ": done after 120 ms";
                app.AdvanceTime(1000);
            }
        }

        // Records each box with the styles it mixes.
        class BlendRecorder : public GUITest::StubGUIRenderer
        {
        public:
            struct Drawn { IntRect rect; StyleBlend blend; };
            std::vector<Drawn> drawn;
            void RenderBox(IntRect rect, IntRect, const StyleBlend& blend, VisualColorStyleIndex, ImageIndex, IntRect) override
            {
                drawn.push_back({ rect, blend });
            }
        };

        // The switch's knob, drawn by its track: where it is and how its two styles mix.
        TEST_F(ToggleTransitionTest, TheSwitchKnobSlidesAndChangesColorWithTheTrack)
        {
            auto knob = [&]() -> BlendRecorder::Drawn
            {
                const ID track = TogglingParts(app).at(0);
                const ElementRef ref = app.elementStorage.FindElementRefByID(track);
                const ElementCustomDraw* draw = app.elementStorage.GetCustomDraw(ref);
                EXPECT_TRUE(draw && draw->callback);
                if (!draw || !draw->callback) return {};
                const IntRect rect = app.GetElementRect(track);
                BlendRecorder r;
                CustomDrawContext ctx{ r, app.elementStorage, ref, rect, rect, IntRect{ 0, 0, 4000, 4000 },
                                       float3x2{}, int2{ 0, 0 } };
                draw->callback(ctx);
                EXPECT_EQ(r.drawn.size(), 1u);
                return r.drawn.empty() ? BlendRecorder::Drawn{} : r.drawn[0];
            };
            const VisualBoxStyleIndex knobStyle = app.theme.switchKnobStyleRef.visualBoxStyleIndex;
            const uint32_t offStyle = GUIRenderer::StyleIndex(knobStyle, ElementState::None);
            const uint32_t onStyle  = GUIRenderer::StyleIndex(knobStyle, ElementState::Selected);

            Frame();
            Frame();
            const BlendRecorder::Drawn off = knob();
            EXPECT_EQ(off.blend.count, 1);
            EXPECT_FLOAT_EQ(off.blend.WeightOf(offStyle), 1.0f);

            value = true;
            Frame();
            app.AdvanceTime(60);
            Frame();
            const BlendRecorder::Drawn midway = knob();
            EXPECT_EQ(midway.blend.count, 2);
            const float t = midway.blend.WeightOf(onStyle);
            EXPECT_GT(t, 0.0f);
            EXPECT_LT(t, 1.0f);
            EXPECT_NEAR(midway.blend.WeightOf(offStyle), 1.0f - t, 1e-5f);

            app.AdvanceTime(100);
            Frame();
            const BlendRecorder::Drawn on = knob();
            EXPECT_EQ(on.blend.count, 1);
            EXPECT_FLOAT_EQ(on.blend.WeightOf(onStyle), 1.0f);
            EXPECT_GT(on.rect.x, off.rect.x);
            EXPECT_GT(midway.rect.x, off.rect.x) << "on its way";
            EXPECT_LT(midway.rect.x, on.rect.x);
            const float travelled = float(midway.rect.x - off.rect.x) / float(on.rect.x - off.rect.x);
            EXPECT_NEAR(travelled, t, 0.15f) << "the knob moves with the color";
        }

        TEST_F(ToggleTransitionTest, AControlShownOnDoesNotBlend)
        {
            value = true;
            Frame();
            EXPECT_FALSE(PartOn().has_value());
        }

        TEST_F(ToggleTransitionTest, ReducedMotionTurnsOnAtOnce)
        {
            app.SetReducedMotion(true);
            EXPECT_TRUE(app.IsReducedMotion());
            Frame();
            value = true;
            Frame();
            EXPECT_FALSE(PartOn().has_value());

            app.SetReducedMotion(false);
            EXPECT_FALSE(app.IsReducedMotion());
            value = false;
            Frame();
            EXPECT_TRUE(PartOn().has_value());

            app.SetReducedMotion(std::nullopt);
            EXPECT_FALSE(app.IsReducedMotion()) << "a headless app has no OS setting";
        }
        // Inside BeginInvalid every element is invalid; the field's box is watched, so turning
        // it invalid grows the halo in.
        TEST_F(ToggleTransitionTest, AFieldTurningInvalidBlends)
        {
            std::string text = "abc", after = "def";
            const ID field = app.idManager.CreateStaticID();
            const ID next  = app.idManager.CreateStaticID();
            bool invalid = false;
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (invalid) app.BeginInvalid();
                    app.Field(field, text, TextEditBehavior::None);
                    if (invalid) app.EndInvalid();
                    app.Field(next, after, TextEditBehavior::None);
                    app.EndRootWindow();
                });
            };
            frame();
            frame();
            EXPECT_EQ(app.guiStateManager.GetCurrentState(field) & ElementState::Invalid, ElementState::None);
            invalid = true;
            frame();
            const ElementRef ref = app.elementStorage.FindElementRefByID(field);
            ASSERT_TRUE(ref.IsValid());
            EXPECT_NE(app.elementStorage.GetElementUnsafe(ref).state & ElementState::Invalid, ElementState::None);
            const ElementRef nextRef = app.elementStorage.FindElementRefByID(next);
            ASSERT_TRUE(nextRef.IsValid());
            EXPECT_EQ(app.elementStorage.GetElementUnsafe(nextRef).state & ElementState::Invalid, ElementState::None)
                << "valid again after EndInvalid";
            EXPECT_TRUE(app.transitionManager.Value(field, TransitionChannel::Invalid, Now()).has_value());
            app.AdvanceTime(151);
            frame();
            EXPECT_FALSE(app.transitionManager.Value(field, TransitionChannel::Invalid, Now()).has_value());
        }
    }
}
