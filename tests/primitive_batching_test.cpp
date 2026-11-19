// A primitive joins the last pass of its kind when nothing drawn since overlaps it: drawn
// earlier, it blends over and under the same things, so the picture doesn't change, and a
// window's alternating kinds (a row's box, then its text, then the next row's box...) draw in
// a few passes instead of one per change of kind.

#include <gtest/gtest.h>

#include <gui_core/graphics/gui_primitive_renderer.h>
#include <gui_core/graphics/gui_render_primitives.h>

#include <vector>

namespace tachyon
{
    namespace
    {
        constexpr IntRect kClip{ 0, 0, 1000, 1000 };

        void Box(PrimitiveRenderTarget& t, IntRect rect) { t.RenderBox(rect, kClip, 1, 1, 1.0f); }
        void Glyph(PrimitiveRenderTarget& t, float2 pos, float2 size)
        {
            t.RenderGlyph(pos, size, float2{ 0, 0 }, float2{ 1, 1 }, 1.0f, 1, 0, VisualColorStyleIndex{}, kClip);
        }
        // The draw orders of a pass's primitives, as the shaders read them (transform0.w).
        std::vector<float> Orders(const PrimitiveRenderTarget& t, const PrimitiveRenderPass& pass)
        {
            std::vector<float> orders;
            const uint32_t stride = kPrimitiveSpecs[size_t(pass.kind)].stride;
            for (int p = 0; p < pass.primitiveCount; ++p)
                orders.push_back(reinterpret_cast<const float*>(t.streams[size_t(pass.kind)].cpu.data()
                                                                 + size_t(pass.firstPrimitive + p) * stride)[3]);
            return orders;
        }

        TEST(PrimitiveBatching, RowsOfABoxAndItsTextDrawInOnePassOfEach)
        {
            PrimitiveRenderTarget t;
            for (int row = 0; row < 10; ++row)
            {
                Box(t, IntRect{ 0, row * 20, 200, 18 });
                Glyph(t, float2{ 4, float(row * 20 + 2) }, float2{ 8, 12 });   // on its row's box
            }
            t.FinishRenderPass();
            ASSERT_EQ(t.renderPasses.size(), 2u);
            EXPECT_EQ(t.renderPasses[0].kind, PrimitiveKind::Box);
            EXPECT_EQ(t.renderPasses[0].primitiveCount, 10);
            EXPECT_EQ(t.renderPasses[1].kind, PrimitiveKind::Glyph);
            EXPECT_EQ(t.renderPasses[1].primitiveCount, 10);
            // Each keeps its own draw order: the depth test and the blending see the same.
            EXPECT_EQ(Orders(t, t.renderPasses[0]), (std::vector<float>{ 0, 2, 4, 6, 8, 10, 12, 14, 16, 18 }));
            EXPECT_EQ(Orders(t, t.renderPasses[1]), (std::vector<float>{ 1, 3, 5, 7, 9, 11, 13, 15, 17, 19 }));
        }

        TEST(PrimitiveBatching, ABoxOverEarlierTextKeepsItsPlace)
        {
            PrimitiveRenderTarget t;
            Box(t, IntRect{ 0, 0, 100, 20 });
            Glyph(t, float2{ 4, 4 }, float2{ 8, 12 });
            Box(t, IntRect{ 0, 0, 30, 30 });   // a popup over the text
            t.FinishRenderPass();
            ASSERT_EQ(t.renderPasses.size(), 3u);
            EXPECT_EQ(t.renderPasses[0].kind, PrimitiveKind::Box);
            EXPECT_EQ(t.renderPasses[1].kind, PrimitiveKind::Glyph);
            EXPECT_EQ(t.renderPasses[2].kind, PrimitiveKind::Box);
        }

        // A cached scope's replay covers where its primitives were captured, moved with it.
        PrimitiveRenderTarget CapturedBoxAt(IntRect rect)
        {
            PrimitiveRenderTarget captured;
            Box(captured, rect);
            captured.FinishRenderPass();
            return captured;
        }
        ID Scope()
        {
            ID scope;
            scope.value.integer = 7;
            return scope;
        }

        TEST(PrimitiveBatching, APrimitiveJoinsAPassAcrossAReplayItDoesNotOverlap)
        {
            PrimitiveRenderTarget t;
            Box(t, IntRect{ 0, 0, 100, 20 });
            t.AddReplay(Scope(), int2{ 0, 0 }, CapturedBoxAt(IntRect{ 0, 100, 100, 20 }));
            Box(t, IntRect{ 0, 500, 100, 20 });
            t.FinishRenderPass();
            ASSERT_EQ(t.renderPasses.size(), 2u);
            EXPECT_EQ(t.renderPasses[0].primitiveCount, 2);
            EXPECT_EQ(t.renderPasses[1].replay, 0);
        }

        TEST(PrimitiveBatching, NothingJoinsAPassAcrossAReplayWhereTheReplayMovedTo)
        {
            PrimitiveRenderTarget t;
            Box(t, IntRect{ 0, 0, 100, 20 });
            t.AddReplay(Scope(), int2{ 0, 400 }, CapturedBoxAt(IntRect{ 0, 100, 100, 20 }));   // drawn at y 500
            Box(t, IntRect{ 0, 500, 100, 20 });
            t.FinishRenderPass();
            ASSERT_EQ(t.renderPasses.size(), 3u);
            EXPECT_EQ(t.renderPasses[1].replay, 0);
        }

        // A text's glyphs at once are the same primitives, in the same passes, as one by one.
        TEST(PrimitiveBatching, GlyphsAtOnceAreTheGlyphsOneByOne)
        {
            std::vector<GUIRenderer::GlyphInstance> glyphs;
            for (int i = 0; i < 5; ++i)
                glyphs.push_back({ .pos = { float(i * 9), 2.0f }, .size = { 8, 12 }, .uvMin = { float(i), 0 }, .uvMax = { float(i) + 1, 1 },
                                   .scale = 0.5f, .bpp = 1, .mode = 2, .textureIndex = ImageIndex(3), .color = VisualColorStyleIndex(4) });
            const float2 offset{ 100.5f, 40.0f };

            PrimitiveRenderTarget single, batched;
            for (PrimitiveRenderTarget* t : { &single, &batched })
            {
                t->SetTransform(float3x2::Translate(3.0f, 4.0f));
                t->SetOpacity(0.75f);
                Box(*t, IntRect{ 0, 0, 50, 50 });
            }
            for (const auto& g : glyphs)
                single.RenderGlyph(float2{ g.pos.x + offset.x, g.pos.y + offset.y }, g.size, g.uvMin, g.uvMax, g.scale, g.bpp, g.mode,
                                   g.color, kClip, g.textureIndex);
            batched.RenderGlyphs(glyphs, offset, kClip);
            for (PrimitiveRenderTarget* t : { &single, &batched })
            {
                Box(*t, IntRect{ 0, 300, 50, 50 });
                t->FinishRenderPass();
            }
            EXPECT_EQ(single.renderPasses, batched.renderPasses);
            EXPECT_EQ(single.nextOrder, batched.nextOrder);
            for (size_t k = 0; k < kPrimitiveKindCount; ++k)
                EXPECT_EQ(single.streams[k].cpu, batched.streams[k].cpu) << "kind " << k;
        }

        TEST(PrimitiveBatching, DotsAtOnceAreTheDotsOneByOne)
        {
            const std::vector<float2> dots{ { 10, 10 }, { 30, 10 }, { 10, 30 }, { 30, 30 } };
            const color4f color{ 0.5f, 0.25f, 1.0f, 0.75f };
            PrimitiveRenderTarget single, batched;
            for (PrimitiveRenderTarget* t : { &single, &batched })
            {
                t->SetOpacity(0.5f);
                Box(*t, IntRect{ 0, 0, 5, 5 });
            }
            for (const float2& p : dots)
                single.RenderDot(p, kClip, 3.0f, color);
            batched.RenderDots(dots, kClip, 3.0f, color);
            for (PrimitiveRenderTarget* t : { &single, &batched })
            {
                Box(*t, IntRect{ 0, 300, 50, 50 });
                t->FinishRenderPass();
            }
            EXPECT_EQ(single.renderPasses, batched.renderPasses);
            for (size_t k = 0; k < kPrimitiveKindCount; ++k)
                EXPECT_EQ(single.streams[k].cpu, batched.streams[k].cpu) << "kind " << k;
        }

        // What overlaps is where a primitive lands on screen, after its transform.
        TEST(PrimitiveBatching, OverlapIsTestedWhereTheTransformPutsIt)
        {
            PrimitiveRenderTarget t;
            Box(t, IntRect{ 0, 0, 20, 20 });
            t.SetTransform(float3x2::Translate(300.0f, 0.0f));
            Glyph(t, float2{ 0, 0 }, float2{ 10, 10 });   // at 300..310 on screen
            t.SetTransform(float3x2::Identity());
            Box(t, IntRect{ 295, 0, 20, 20 });           // over it
            t.FinishRenderPass();
            EXPECT_EQ(t.renderPasses.size(), 3u);
        }
    }
}
