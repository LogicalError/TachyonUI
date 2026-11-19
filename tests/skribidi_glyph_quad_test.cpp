// The glyph quads RenderText draws come from the atlas: the same letter, wherever and however
// often it is drawn, uses the same atlas image, and a text draws the same quads frame after
// frame, across atlas compaction and font changes.

#include "skribidi_test_fixture.h"

#include <vector>

namespace tachyon
{
    using GUITest::SkribidiHeadlessTest;
    using GlyphDraw = GUITest::StubGUIRenderer::GlyphDraw;

    namespace
    {
        class SkribidiGlyphQuadTest : public SkribidiHeadlessTest
        {
        protected:
            const IntRect clip{ -1000, -1000, 4000, 4000 };

            std::vector<GlyphDraw> Render(std::string_view text, IntRect rect, uint64_t seed = 5)
            {
                const TextID id = textSystem->RegisterText(ID{ .value{ .integer = seed } }, IconID::Invalid, text,
                                                           MakeTextStyle(), ElementState::None);
                EXPECT_NE(id, TextID::Invalid);
                auto& draws = graphicsContext.Renderer().glyphDraws;
                draws.clear();
                textSystem->RenderText(id, rect, rect, clip);
                return draws;
            }
            static void ExpectSame(const GlyphDraw& a, const GlyphDraw& b, float2 offset = { 0, 0 })
            {
                EXPECT_EQ(a.pos.x + offset.x, b.pos.x);
                EXPECT_EQ(a.pos.y + offset.y, b.pos.y);
                EXPECT_EQ(a.size.x, b.size.x);
                EXPECT_EQ(a.size.y, b.size.y);
                EXPECT_EQ(a.uvMin.x, b.uvMin.x);
                EXPECT_EQ(a.uvMin.y, b.uvMin.y);
                EXPECT_EQ(a.uvMax.x, b.uvMax.x);
                EXPECT_EQ(a.uvMax.y, b.uvMax.y);
            }
        };

        TEST_F(SkribidiGlyphQuadTest, ARepeatedLetterUsesOneAtlasImage)
        {
            const std::vector<GlyphDraw> draws = Render("abab", IntRect{ 0, 0, 300, 40 });
            ASSERT_EQ(draws.size(), 4u);
            EXPECT_GT(draws[2].pos.x, draws[0].pos.x);
            ExpectSame(draws[0], draws[2], float2{ draws[2].pos.x - draws[0].pos.x, 0 });
            ExpectSame(draws[1], draws[3], float2{ draws[3].pos.x - draws[1].pos.x, 0 });
        }

        TEST_F(SkribidiGlyphQuadTest, ATextMovedDrawsItsGlyphsMovedAsFar)
        {
            const std::vector<GlyphDraw> here  = Render("Moved", IntRect{ 0, 0, 300, 40 });
            const std::vector<GlyphDraw> there = Render("Moved", IntRect{ 37, 120, 300, 40 });
            ASSERT_EQ(here.size(), there.size());
            for (size_t i = 0; i < here.size(); ++i)
                ExpectSame(here[i], there[i], float2{ 37, 120 });
        }

        // Drawn again, a text's glyphs come from what it drew before: as they were, until its
        // color (here by state) changes.
        TEST_F(SkribidiGlyphQuadTest, ATextDrawnAgainKeepsItsGlyphsAndTakesANewColor)
        {
            Style s{};
            s.textLayoutStyle.textWrapping = TextWrap::NoWrap;
            for (int i = 0; i < Style::kStateCount; ++i)
            {
                s.states[i].textStyle.fontSize = 14.0f;
                s.states[i].textColor = ColorStyle::Create(color4b{ 255, 0, 0, 255 });
            }
            s.states[ElementState::Hover].textColor = ColorStyle::Create(color4b{ 0, 0, 255, 255 });
            const StyleRef style = styleManager.Register(s);
            const IntRect rect{ 0, 0, 300, 40 };
            auto render = [&](ElementState state) {
                const TextID id = textSystem->RegisterText(ID{ .value{ .integer = 9 } }, IconID::Invalid, "Recolored", style, state);
                auto& draws = graphicsContext.Renderer().glyphDraws;
                draws.clear();
                textSystem->RenderText(id, rect, rect, clip);
                return draws;
            };
            const std::vector<GlyphDraw> first = render(ElementState::None);
            const std::vector<GlyphDraw> again = render(ElementState::None);
            const std::vector<GlyphDraw> hovered = render(ElementState::Hover);
            ASSERT_FALSE(first.empty());
            ASSERT_EQ(first.size(), again.size());
            ASSERT_EQ(first.size(), hovered.size());
            for (size_t i = 0; i < first.size(); ++i)
            {
                ExpectSame(first[i], again[i]);
                EXPECT_EQ(first[i].color, again[i].color);
                ExpectSame(first[i], hovered[i]);
                EXPECT_NE(first[i].color, hovered[i].color);
            }
            EXPECT_EQ(hovered[0].color, styleManager.GetVisualTextStyle(style.visualTextStyleIndex, ElementState::Hover)->textColorIndex);
        }

        // A cached scope's recorded atlas lookups hold, as recorded and across a compaction;
        // a scope never recorded has nothing to replay.
        TEST_F(SkribidiGlyphQuadTest, ARecordedScopeReplaysItsAtlasLookups)
        {
            const ID scope{ .value{ .integer = 42 } };
            textSystem->BeginAtlasRecord(scope);
            Render("Recorded text", IntRect{ 0, 0, 300, 40 });
            textSystem->EndAtlasRecord();
            EXPECT_TRUE(textSystem->ReplayAtlasTouches(scope));
            EXPECT_TRUE(textSystem->ReplayAtlasTouches(scope)) << "again, in the same epoch";
            textSystem->Update();   // rasterizes and compacts
            EXPECT_TRUE(textSystem->ReplayAtlasTouches(scope)) << "looked up again after the compaction";
            EXPECT_FALSE(textSystem->ReplayAtlasTouches(ID{ .value{ .integer = 43 } }));
        }

        TEST_F(SkribidiGlyphQuadTest, ATextDrawsTheSameAfterTheAtlasCompactsAndTheFontsReload)
        {
            const IntRect rect{ 0, 0, 300, 40 };
            const std::vector<GlyphDraw> first = Render("Same quads", rect);
            textSystem->Update();   // rasterizes the new glyphs and compacts the atlas
            const std::vector<GlyphDraw> second = Render("Same quads", rect);
            ASSERT_EQ(first.size(), second.size());
            for (size_t i = 0; i < first.size(); ++i)
                ExpectSame(first[i], second[i]);

            // Registering a family remakes the font collection.
            const FontFaceSource faces[] = { { .file = "fonts/Inter/Inter-Regular.ttf" } };
            ASSERT_NE(textSystem->RegisterFontFamily("Reloaded", faces), FontID::Invalid);
            textSystem->Update();
            const std::vector<GlyphDraw> third = Render("Same quads", rect, 6);
            ASSERT_EQ(first.size(), third.size());
            for (size_t i = 0; i < first.size(); ++i)
            {
                EXPECT_EQ(first[i].pos.x, third[i].pos.x);
                EXPECT_EQ(first[i].size.x, third[i].size.x);
            }
        }
    }
}
