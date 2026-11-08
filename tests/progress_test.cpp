// Progress and loading: RenderArc, progress bars (determinate, failed, sliding), spinners,
// circular progress, skeletons and the busy button, and how a cached scope holding one that
// moves keeps being rebuilt.

#include <gtest/gtest.h>

#include "headless_gui_app.h"
#include "skribidi_test_fixture.h"

#include <gui_core/graphics/gui_custom_draw_context.h>
#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/elements/gui_element_flags.h>

#include <cmath>
#include <limits>
#include <string>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;
    using GUITest::StubGUIRenderer;

    namespace
    {
        constexpr float kPi = 3.14159265358979f;

        // How far round from the right `p` is around `center`, clockwise: 0 to 1 turns.
        float TurnsAround(float2 center, float2 p)
        {
            float turns = std::atan2(p.y - center.y, p.x - center.x) / (2.0f * kPi);
            return turns < 0.0f ? turns + 1.0f : turns;
        }

        float DistanceBetween(float2 a, float2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

        // Whether a triangle drawn has a corner within half a pixel of `p`.
        bool HasPointNear(const StubGUIRenderer& renderer, float2 p)
        {
            for (const auto& t : renderer.triangles)
                for (const float2& q : t.points)
                    if (DistanceBetween(p, q) < 0.5f)
                        return true;
            return false;
        }

        // ---------------------------------------------------------------------
        // RenderArc
        // ---------------------------------------------------------------------

        // A quarter turn from the right: quads along the ring, a round end on each side, and
        // nothing outside the ring's width or the sweep and its ends.
        TEST(RenderArcTest, AnArcCoversItsSweepAndNoMore)
        {
            StubGUIRenderer renderer;
            const float2 center{ 50.0f, 50.0f };
            renderer.RenderArc(center, 10.0f, 4.0f, 0.0f, 0.25f, IntRect{ 0, 0, 100, 100 }, VisualColorStyleIndex(7));
            ASSERT_EQ(renderer.triangles.size(), 2u * 24u + 2u * 8u) << "two per step, eight per end";
            for (const auto& t : renderer.triangles)
            {
                EXPECT_EQ(t.color, VisualColorStyleIndex(7));
                for (const float2& p : t.points)
                {
                    const float d = DistanceBetween(center, p);
                    EXPECT_GE(d, 8.0f - 1e-3f);
                    EXPECT_LE(d, 12.0f + 1e-3f);
                    const float turns = TurnsAround(center, p);
                    EXPECT_TRUE(turns <= 0.3f || turns >= 0.95f) << "past the sweep and its ends: " << turns;
                }
            }
            EXPECT_TRUE(HasPointNear(renderer, { 62.0f, 50.0f })) << "the outer rim at the start";
            EXPECT_TRUE(HasPointNear(renderer, { 50.0f, 62.0f })) << "... and at the end, a quarter turn on";
            EXPECT_TRUE(HasPointNear(renderer, { 50.0f, 58.0f })) << "the inner rim at the end";
            EXPECT_TRUE(HasPointNear(renderer, { 60.0f, 48.0f })) << "the start's round end, bulging back";
            EXPECT_TRUE(HasPointNear(renderer, { 48.0f, 60.0f })) << "the end's, bulging on";
            const float diagonal = 2.0f * std::sqrt(0.5f);
            EXPECT_TRUE(HasPointNear(renderer, { 60.0f - diagonal, 50.0f - diagonal })) << "the start's round end, half a circle back to the inner rim";

            // The rims are soft, the edges the triangles share hard.
            const auto& first = renderer.triangles[0];
            EXPECT_EQ(first.hardEdges, TriangleEdges::Edge12 | TriangleEdges::Edge20);
            EXPECT_EQ(renderer.triangles[1].hardEdges, TriangleEdges::Edge01 | TriangleEdges::Edge20);
            EXPECT_EQ(renderer.triangles.back().hardEdges, TriangleEdges::Edge01 | TriangleEdges::Edge20);
            EXPECT_FLOAT_EQ(DistanceBetween(center, first.points[0]), 12.0f);
            EXPECT_FLOAT_EQ(DistanceBetween(center, first.points[1]), 12.0f) << "its soft edge on the outer rim";
        }

        TEST(RenderArcTest, ANegativeSweepGoesTheOtherWay)
        {
            StubGUIRenderer renderer;
            const float2 center{ 50.0f, 50.0f };
            renderer.RenderArc(center, 10.0f, 4.0f, 0.0f, -0.25f, IntRect{ 0, 0, 100, 100 }, VisualColorStyleIndex(7));
            EXPECT_TRUE(HasPointNear(renderer, { 50.0f, 38.0f })) << "a quarter turn up";
            EXPECT_FALSE(HasPointNear(renderer, { 50.0f, 62.0f })) << "not down";
            EXPECT_TRUE(HasPointNear(renderer, { 60.0f, 52.0f })) << "the start's round end bulges down, away from the sweep";
        }

        TEST(RenderArcTest, AWholeTurnIsARingWithoutEnds)
        {
            for (const float sweep : { 1.0f, 1.5f, -2.0f })
            {
                StubGUIRenderer renderer;
                renderer.RenderArc({ 50.0f, 50.0f }, 10.0f, 4.0f, 0.3f, sweep, IntRect{ 0, 0, 100, 100 }, VisualColorStyleIndex(7));
                EXPECT_EQ(renderer.triangles.size(), 2u * 96u) << sweep;
                // No ends: every corner on a rim, none at the middle of the ring where an
                // end's fan would start.
                for (const auto& t : renderer.triangles)
                    for (const float2& p : t.points)
                    {
                        const float d = DistanceBetween({ 50.0f, 50.0f }, p);
                        EXPECT_TRUE(std::fabs(d - 8.0f) < 1e-3f || std::fabs(d - 12.0f) < 1e-3f) << sweep << ": " << d;
                    }
            }
        }

        TEST(RenderArcTest, NothingWithoutAWidthARadiusOrASweep)
        {
            StubGUIRenderer renderer;
            const IntRect clip{ 0, 0, 100, 100 };
            renderer.RenderArc({ 50, 50 }, 10.0f, 0.0f, 0.0f, 0.25f, clip, VisualColorStyleIndex(7));
            renderer.RenderArc({ 50, 50 }, 0.0f, 4.0f, 0.0f, 0.25f, clip, VisualColorStyleIndex(7));
            renderer.RenderArc({ 50, 50 }, 10.0f, 4.0f, 0.0f, 0.0f, clip, VisualColorStyleIndex(7));
            renderer.RenderArc({ 50, 50 }, 10.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.25f, clip, VisualColorStyleIndex(7));
            EXPECT_TRUE(renderer.triangles.empty());

            // A tiny sweep still has two steps.
            renderer.RenderArc({ 50, 50 }, 10.0f, 4.0f, 0.0f, 0.001f, clip, VisualColorStyleIndex(7));
            EXPECT_EQ(renderer.triangles.size(), 2u * 2u + 2u * 8u);
        }

        // ---------------------------------------------------------------------
        // The controls
        // ---------------------------------------------------------------------

        class ProgressTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;

            void SetUp()    override { ASSERT_TRUE(app.Initialize()); }
            void TearDown() override { app.Cleanup(); }

            // Runs the custom draw of `id`'s element into a fresh stub renderer.
            StubGUIRenderer Draw(ID id)
            {
                StubGUIRenderer renderer;
                const ElementRef ref = app.elementStorage.FindElementRefByID(id);
                EXPECT_TRUE(ref.IsValid());
                if (!ref.IsValid())
                    return renderer;
                const ElementCustomDraw* draw = app.elementStorage.GetCustomDraw(ref);
                if (draw == nullptr || !draw->callback)
                    return renderer;
                const ElementRect* rects = app.elementStorage.GetElementRectPtr(ref);
                CustomDrawContext ctx{ renderer, app.elementStorage, ref, rects->rect, rects->innerRect,
                                       IntRect{ -4000, -4000, 8000, 8000 }, float3x2{}, int2{ 0, 0 } };
                draw->callback(ctx);
                return renderer;
            }

            bool HasCustomDraw(ID id)
            {
                const ElementCustomDraw* draw = app.elementStorage.GetCustomDraw(app.elementStorage.FindElementRefByID(id));
                return draw != nullptr && draw->callback;
            }

            ElementFlags FlagsOf(ID id)
            {
                return app.elementStorage.GetElementUnsafe(app.elementStorage.FindElementRefByID(id)).flags;
            }

            uint32_t SlotOf(StyleRef style) { return GUIRenderer::StyleIndex(style.visualBoxStyleIndex, ElementState::None); }
        };

        // The part done, from the bar's start; clamped to the bar; none at 0.
        TEST_F(ProgressTest, ABarFillsAsMuchAsIsDone)
        {
            const ID bar = app.idManager.CreateStaticID();
            float value = 0.25f;
            bool failed = false;
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.ProgressBar(bar, value, failed, app.theme.progressBarStyleRef, LayoutOverride{}.Width(Pixels{ 200 }));
                    app.EndRootWindow();
                });
            };
            frame();
            const IntRect track = app.GetElementRect(bar);
            EXPECT_EQ(track.w, 200);
            EXPECT_EQ(track.h, app.styleManager.Px(6));
            EXPECT_EQ(app.elementStorage.GetElementUnsafe(app.elementStorage.FindElementRefByID(bar)).styleRef, app.theme.progressBarStyleRef);
            EXPECT_EQ(FlagsOf(bar) & ElementFlags::Animated, ElementFlags::None) << "it stays still";

            StubGUIRenderer renderer = Draw(bar);
            ASSERT_EQ(renderer.boxes.size(), 1u);
            EXPECT_EQ(renderer.boxes[0].w, 50);
            EXPECT_EQ(renderer.boxes[0].h, track.h);
            EXPECT_EQ(renderer.boxes[0].x, 0) << "from the start";
            EXPECT_EQ(renderer.boxStyles[0], SlotOf(app.theme.progressFillStyleRef));

            for (const auto& [v, width] : { std::pair{ 2.0f, 200 }, std::pair{ 1.0f, 200 }, std::pair{ 0.501f, 100 } })
            {
                value = v;
                frame();
                renderer = Draw(bar);
                ASSERT_EQ(renderer.boxes.size(), 1u) << v;
                EXPECT_EQ(renderer.boxes[0].w, width) << v;
            }
            for (const float v : { 0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), 0.002f })
            {
                value = v;
                frame();
                EXPECT_TRUE(Draw(bar).boxes.empty()) << v;
            }

            value  = 0.73f;
            failed = true;
            frame();
            renderer = Draw(bar);
            ASSERT_EQ(renderer.boxes.size(), 1u);
            EXPECT_EQ(renderer.boxStyles[0], SlotOf(app.theme.progressFillFailedStyleRef)) << "in the error color";
            EXPECT_EQ(renderer.boxes[0].w, 146);
        }

        TEST_F(ProgressTest, TheSmallBarIsLower)
        {
            const ID bar = app.idManager.CreateStaticID();
            app.Frame([&] {
                app.BeginRootWindow();
                app.ProgressBar(bar, 0.5f, false, app.theme.progressBarSmallStyleRef, LayoutOverride{}.Width(Pixels{ 120 }));
                app.EndRootWindow();
            });
            EXPECT_EQ(app.GetElementRect(bar).h, app.styleManager.Px(4));
            EXPECT_EQ(Draw(bar).boxes.at(0).w, 60);
        }

        // The segment slides from just before the bar to just past it, a trip every 1.2 s,
        // drawn over the straight part only.
        TEST_F(ProgressTest, AnIndeterminateBarsSegmentSlidesAlong)
        {
            const ID bar = app.idManager.CreateStaticID();
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.IndeterminateProgressBar(bar, LayoutOverride{}.Width(Pixels{ 200 }));
                    app.EndRootWindow();
                });
            };
            frame();
            EXPECT_NE(FlagsOf(bar) & ElementFlags::Animated, ElementFlags::None);
            const IntRect track = app.GetElementRect(bar);
            const int32_t length = 52;   // 26% of 200
            StubGUIRenderer renderer = Draw(bar);
            ASSERT_EQ(renderer.boxes.size(), 1u);
            EXPECT_EQ(renderer.boxes[0], (IntRect{ -length, 0, length, track.h })) << "just before the start";
            EXPECT_EQ(renderer.boxStyles[0], SlotOf(app.theme.progressSegmentStyleRef));
            const int32_t round = track.h / 2;
            EXPECT_EQ(renderer.boxClips[0], (IntRect{ round, 0, 200 - 2 * round, track.h })) << "the straight part";

            app.AdvanceTime(600);
            frame();
            renderer = Draw(bar);
            EXPECT_EQ(renderer.boxes.at(0).x, -length + (200 + length) / 2) << "half way";

            app.AdvanceTime(600);
            frame();
            EXPECT_EQ(Draw(bar).boxes.at(0).x, -length) << "and round again";
        }

        // A ring in the track color with a quarter arc in the accent, a turn every 0.8 s from
        // the top; 12, 16 or 24px.
        TEST_F(ProgressTest, ASpinnerTurns)
        {
            const ID small = app.idManager.CreateStaticID(), medium = app.idManager.CreateStaticID();
            const ID large = app.idManager.CreateStaticID(), sized = app.idManager.CreateStaticID();
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.Spinner(small, SpinnerSize::Small);
                    app.Spinner(medium);
                    app.Spinner(large, SpinnerSize::Large);
                    app.Spinner(sized, SpinnerSize::Medium, LayoutOverride{}.Size(Pixels{ 32 }, Pixels{ 32 }));
                    app.EndRootWindow();
                });
            };
            frame();
            EXPECT_EQ(app.GetElementRect(small).size, (int2{ app.styleManager.Px(12), app.styleManager.Px(12) }));
            EXPECT_EQ(app.GetElementRect(medium).size, (int2{ app.styleManager.Px(16), app.styleManager.Px(16) }));
            EXPECT_EQ(app.GetElementRect(large).size, (int2{ app.styleManager.Px(24), app.styleManager.Px(24) }));
            EXPECT_EQ(app.GetElementRect(sized).size, (int2{ 32, 32 }));
            EXPECT_NE(FlagsOf(medium) & ElementFlags::Animated, ElementFlags::None);

            const float scale = float(app.styleManager.Px(16)) / 16.0f;
            StubGUIRenderer renderer = Draw(medium);
            ASSERT_EQ(renderer.circles.size(), 1u);
            const float2 center = renderer.circles[0].center;
            EXPECT_FLOAT_EQ(center.x, float(app.styleManager.Px(16)) * 0.5f);
            EXPECT_FLOAT_EQ(renderer.circles[0].radius, 6.5f * scale);
            EXPECT_FLOAT_EQ(renderer.circles[0].style.lineWidth, 2.0f * scale);
            EXPECT_EQ(renderer.circles[0].style.colorStyleIndex, app.theme.progressTrackColor);
            ASSERT_FALSE(renderer.triangles.empty());
            EXPECT_EQ(renderer.triangles[0].color, app.theme.progressArcColor);
            const float outer = (6.5f + 1.0f) * scale;
            EXPECT_TRUE(HasPointNear(renderer, { center.x, center.y - outer })) << "from the top";
            EXPECT_TRUE(HasPointNear(renderer, { center.x + outer, center.y })) << "a quarter round";
            EXPECT_FALSE(HasPointNear(renderer, { center.x, center.y + outer })) << "and no further";

            app.AdvanceTime(200);   // a quarter turn on
            frame();
            renderer = Draw(medium);
            EXPECT_TRUE(HasPointNear(renderer, { center.x + outer, center.y }));
            EXPECT_TRUE(HasPointNear(renderer, { center.x, center.y + outer }));
            EXPECT_FALSE(HasPointNear(renderer, { center.x, center.y - outer })) << "the top is behind it now";

            EXPECT_FLOAT_EQ(Draw(small).circles.at(0).radius, 4.5f * float(app.styleManager.Px(12)) / 12.0f);
            EXPECT_FLOAT_EQ(Draw(large).circles.at(0).style.lineWidth, 2.5f * float(app.styleManager.Px(24)) / 24.0f);
            EXPECT_FLOAT_EQ(Draw(sized).circles.at(0).radius, 13.0f) << "scaled to the room it's given";
        }

        // The ring filled as far round as is done, from the top; the whole percent in it.
        TEST_F(ProgressTest, CircularProgressShowsHowMuchIsDone)
        {
            const ID ring = app.idManager.CreateStaticID();
            float value = 0.684f;
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.CircularProgress(ring, value);
                    app.EndRootWindow();
                });
            };
            frame();
            EXPECT_EQ(app.TextOf(ring), "68");
            EXPECT_EQ(app.GetElementRect(ring).size, (int2{ app.styleManager.Px(40), app.styleManager.Px(40) }));
            StubGUIRenderer renderer = Draw(ring);
            ASSERT_EQ(renderer.circles.size(), 1u);
            const float scale = float(app.styleManager.Px(40)) / 40.0f;
            EXPECT_FLOAT_EQ(renderer.circles[0].radius, 17.0f * scale);
            EXPECT_FLOAT_EQ(renderer.circles[0].style.lineWidth, 3.0f * scale);
            EXPECT_EQ(renderer.triangles.size(), 2u * 66u + 2u * 8u) << "68.4% of the way round";
            const float2 center = renderer.circles[0].center;
            EXPECT_TRUE(HasPointNear(renderer, { center.x, center.y - 18.5f * scale })) << "from the top";

            value = 0.0f;
            frame();
            EXPECT_EQ(app.TextOf(ring), "0");
            renderer = Draw(ring);
            EXPECT_EQ(renderer.circles.size(), 1u);
            EXPECT_TRUE(renderer.triangles.empty());

            value = 0.999f;
            frame();
            EXPECT_EQ(app.TextOf(ring), "99") << "100 only once it's done";
            value = 3.0f;
            frame();
            EXPECT_EQ(app.TextOf(ring), "100");
            EXPECT_EQ(Draw(ring).triangles.size(), 2u * 96u) << "all the way round, without ends";
        }

        // A placeholder bar as wide as it's given, a shine crossing its straight part every
        // 1.5 s; still, and not animated, with reduced motion.
        TEST_F(ProgressTest, ASkeletonShines)
        {
            const ID bar = app.idManager.CreateStaticID();
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.Skeleton(bar, LayoutOverride{}.Width(Pixels{ 140 }));
                    app.EndRootWindow();
                });
            };
            frame();
            const IntRect rect = app.GetElementRect(bar);
            EXPECT_EQ(rect.w, 140);
            EXPECT_EQ(rect.h, app.styleManager.Px(10));
            EXPECT_NE(FlagsOf(bar) & ElementFlags::Animated, ElementFlags::None);
            StubGUIRenderer renderer = Draw(bar);
            ASSERT_EQ(renderer.boxes.size(), 1u);
            const int32_t band = std::max(70, rect.h * 4);
            EXPECT_EQ(renderer.boxes[0], (IntRect{ -band, 0, band, rect.h }));
            EXPECT_EQ(renderer.boxStyles[0], SlotOf(app.theme.skeletonShineStyleRef));
            EXPECT_EQ(renderer.boxClips[0], (IntRect{ rect.h / 2, 0, 140 - rect.h, rect.h }));

            app.AdvanceTime(750);
            frame();
            EXPECT_EQ(Draw(bar).boxes.at(0).x, -band + (140 + band) / 2);

            // A short one's shine is four times its height, not half its length.
            const ID shortBar = app.idManager.CreateStaticID();
            app.Frame([&] {
                app.BeginRootWindow();
                app.Skeleton(shortBar, LayoutOverride{}.Width(Pixels{ 30 }));
                app.EndRootWindow();
            });
            EXPECT_EQ(Draw(shortBar).boxes.at(0).w, app.GetElementRect(shortBar).h * 4);

            app.SetReducedMotion(true);
            frame();
            EXPECT_FALSE(HasCustomDraw(bar));
            EXPECT_EQ(FlagsOf(bar) & ElementFlags::Animated, ElementFlags::None);
            EXPECT_EQ(app.GetElementRect(bar).w, 140);
        }

        // Busy: a spinner frame for its icon, the busy text, the width it had, and a click
        // does nothing; idle again it's a button as before.
        TEST_F(ProgressTest, ABusyButtonKeepsItsWidthAndIgnoresClicks)
        {
            app.runPostLayoutCallbacks = true;
            const ID row = app.idManager.CreateStaticID(), button = app.idManager.CreateStaticID();
            const ID other = app.idManager.CreateStaticID();
            bool busy = false;
            int clicks = 0;
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    // A row it shares with a button that grows into the room left over too.
                    app.BeginHorizontal(row, app.theme.emptyHorzStyleRef, LayoutOverride{}.Width(Pixels{ 600 }));
                    app.BusyButton(button, { app.theme.menuCheckIcon, "Save" }, busy, "Saving everything to disk...", [&] { ++clicks; });
                    app.Button(other, "Cancel", [] {});
                    app.EndHorizontal();
                    app.EndRootWindow();
                });
            };
            auto iconOf = [&] {
                const ElementRef ref = app.elementStorage.FindElementRefByID(button);
                return app.ownedTextSystem.IconOf(app.elementStorage.GetElementUnsafe(ref).textID);
            };
            frame();
            const IntRect idle = app.GetElementRect(button);
            EXPECT_EQ(app.TextOf(button), "Save");
            EXPECT_EQ(iconOf(), app.theme.menuCheckIcon);
            EXPECT_EQ(FlagsOf(button) & ElementFlags::Animated, ElementFlags::None);
            app.ClickAt(app.CenterOf(button));
            frame();
            frame();
            EXPECT_EQ(clicks, 1) << "idle, a click works";

            busy = true;
            frame();
            EXPECT_EQ(app.TextOf(button), "Saving everything to disk...");
            EXPECT_EQ(iconOf(), app.theme.SpinnerIcon(app.guiStateManager.Interaction().GetFrameTime()));
            EXPECT_NE(FlagsOf(button) & ElementFlags::Animated, ElementFlags::None);
            EXPECT_EQ(app.GetElementRect(button).w, idle.w) << "as wide as it was, though its text is longer";
            EXPECT_EQ(app.GetElementRect(button).x, idle.x);
            app.ClickAt(app.CenterOf(button));
            frame();
            frame();
            EXPECT_EQ(clicks, 1) << "busy, a click does nothing";

            const float before = app.guiStateManager.Interaction().GetFrameTime();
            app.AdvanceTime(200);
            frame();
            EXPECT_EQ(iconOf(), app.theme.SpinnerIcon(before + 0.2f)) << "a quarter turn on";
            EXPECT_NE(iconOf(), app.theme.SpinnerIcon(before));

            busy = false;
            frame();
            EXPECT_EQ(app.TextOf(button), "Save");
            EXPECT_EQ(iconOf(), app.theme.menuCheckIcon);
            app.ClickAt(app.CenterOf(button));
            frame();
            frame();
            EXPECT_EQ(clicks, 2);
        }

        // Without busy text it keeps its own; a width it's given wins over the one it had.
        TEST_F(ProgressTest, ABusyButtonKeepsItsTextOrAGivenWidth)
        {
            app.runPostLayoutCallbacks = true;
            const ID button = app.idManager.CreateStaticID();
            bool busy = false;
            auto frame = [&](LayoutOverride constraints) {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.BusyButton(button, "Export", busy, {}, [] {}, std::move(constraints));
                    app.EndRootWindow();
                });
            };
            frame({});
            busy = true;
            frame({});
            EXPECT_EQ(app.TextOf(button), "Export");
            frame(LayoutOverride{}.Width(Pixels{ 300 }).FlexGrow(0));
            EXPECT_EQ(app.GetElementRect(button).w, 300);

            // Never idle: as wide as its content.
            const ID fresh = app.idManager.CreateStaticID();
            app.Frame([&] {
                app.BeginRootWindow();
                app.BusyButton(fresh, "Go", true, "Going...", [] {});
                app.EndRootWindow();
            });
            EXPECT_GT(app.GetElementRect(fresh).w, 0);
        }

        // The spinner frames go round once every 0.8 s.
        TEST_F(ProgressTest, TheSpinnerIconGoesRound)
        {
            const GUITheme& theme = app.theme;
            for (int i = 0; i < GUITheme::kSpinnerFrames; ++i)
                EXPECT_NE(theme.spinnerIcons[i], IconID::Invalid) << i;
            EXPECT_EQ(theme.SpinnerIcon(0.0f), theme.spinnerIcons[0]);
            EXPECT_EQ(theme.SpinnerIcon(0.4f), theme.spinnerIcons[GUITheme::kSpinnerFrames / 2]);
            EXPECT_EQ(theme.SpinnerIcon(0.799f), theme.spinnerIcons[GUITheme::kSpinnerFrames - 1]);
            EXPECT_EQ(theme.SpinnerIcon(0.8f), theme.spinnerIcons[0]);
            EXPECT_EQ(theme.SpinnerIcon(-0.2f), theme.spinnerIcons[GUITheme::kSpinnerFrames * 3 / 4]) << "before 0 too";

            // A quarter arc of radius 9 around the middle of 24: the first frame from the top
            // to the right, the frame a quarter turn on from the right to the bottom.
            auto svgOf = [&](IconID icon) { return app.ownedTextSystem.svgOfIcon.at(static_cast<uint32_t>(icon)); };
            EXPECT_NE(svgOf(theme.spinnerIcons[0]).find(R"(d="M12.000 3.000A9 9 0 0 1 21.000 12.000")"), std::string::npos);
            EXPECT_NE(svgOf(theme.spinnerIcons[GUITheme::kSpinnerFrames / 4]).find(R"(d="M21.000 12.000A9 9 0 0 1 12.000 21.000")"),
                      std::string::npos);
        }

        // A cached scope with something that moves in it is built every frame; one with a
        // bar that stays still is replayed.
        TEST_F(ProgressTest, ACachedScopeWithAnAnimationIsRebuilt)
        {
            app.SetDrawCacheEnabled(true);
            const ID scope = app.idManager.CreateStaticID(), bar = app.idManager.CreateStaticID();
            int runs = 0;
            bool moving = false;
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    if (app.BeginCached(scope, app.theme.emptyVertStyleRef, moving ? 1 : 0, LayoutOverride{}.Width(200).Height(100)))
                    {
                        ++runs;
                        if (moving)
                            app.Spinner(bar);
                        else
                            app.ProgressBar(bar, 0.5f);
                    }
                    app.EndCached();
                    app.EndRootWindow();
                });
            };
            frame();
            frame();
            frame();
            EXPECT_EQ(runs, 1) << "a still bar: replayed";

            moving = true;
            runs = 0;
            frame();
            frame();
            frame();
            EXPECT_EQ(runs, 3) << "a spinner: built every frame";
        }
    }
}
