// Text is laid out from its font and metrics alone: a state that only changes the colors keeps
// the layout, and decoration lines are drawn in their own colors. Rendering goes to a stub
// renderer (see skribidi_test_fixture.h).

#include "skribidi_test_fixture.h"

#include <algorithm>
#include <vector>

namespace tachyon
{
    using GUITest::SkribidiHeadlessTest;
    using GlyphDraw = GUITest::StubGUIRenderer::GlyphDraw;

    namespace
    {
        constexpr color4b kRed   { 255, 0, 0, 255 };
        constexpr color4b kGreen { 0, 255, 0, 255 };
        constexpr color4b kBlue  { 0, 0, 255, 255 };
        constexpr color4b kWhite { 255, 255, 255, 255 };

        class TextPaintTest : public SkribidiHeadlessTest
        {
        protected:
            const IntRect rect{ 0, 0, 200, 40 };
            const IntRect clip{ 0, 0, 2000, 2000 };
            const ID      element{ .value{ .integer = 81 } };

            // The glyph drawn last for `text` in a box `width` wide.
            GlyphDraw LastGlyph(StyleRef style, std::string_view text, int width)
            {
                const TextID id = Register(style, ElementState::None, text);
                const IntRect box{ 0, 0, width, 40 };
                graphicsContext.Renderer().glyphDraws.clear();
                textSystem->RenderText(id, box, box, clip);
                const std::vector<GlyphDraw>& draws = graphicsContext.Renderer().glyphDraws;
                return draws.empty() ? GlyphDraw{} : draws.back();
            }

            // 14px blue text in every state.
            static Style BaseStyle()
            {
                Style s{};
                s.textLayoutStyle.textWrapping = TextWrap::NoWrap;
                for (StyleState& state : s.states)
                {
                    state.textStyle.fontSize = 14.0f;
                    state.textColor = ColorStyle::Create(kBlue);
                }
                return s;
            }

            TextID Register(StyleRef style, ElementState state, std::string_view text = "Paint")
            {
                const TextID id = textSystem->RegisterText(element, IconID::Invalid, text, style, state);
                EXPECT_NE(id, TextID::Invalid);
                return id;
            }

            std::vector<GlyphDraw> Render(TextID id)
            {
                graphicsContext.Renderer().glyphDraws.clear();
                textSystem->RenderText(id, rect, rect, clip);
                return graphicsContext.Renderer().glyphDraws;
            }

            VisualColorStyleIndex ColorIndex(color4b c) { return styleManager.RegisterColorStyle(ColorStyle::Create(c)); }

            // Whether two color styles show the same: a run's color has a style of its own, which
            // shows what a registered one of that color does (the same atlas texel).
            bool SameColor(VisualColorStyleIndex a, VisualColorStyleIndex b) const
            {
                const VisualColorStyle* sa = styleManager.GetVisualColorStyle(a);
                const VisualColorStyle* sb = styleManager.GetVisualColorStyle(b);
                return sa != nullptr && sb != nullptr && *sa == *sb;
            }

            size_t CountColor(const std::vector<GlyphDraw>& draws, VisualColorStyleIndex color) const
            {
                return size_t(std::ranges::count_if(draws, [&](const GlyphDraw& d) { return SameColor(d.color, color); }));
            }

            void NextFrame() { textSystem->BeginFrame(int2{}, MouseButtons{}, 0); }
        };

        // Text cut short ends in an ellipsis on the line, where a typed one sits, not raised
        // above the text.
        TEST_F(TextPaintTest, ACutTextsEllipsisSitsWhereATypedOneDoes)
        {
            for (const TextBaseline baseline : { TextBaseline::Alphabetic, TextBaseline::Middle, TextBaseline::Central })
            {
                SCOPED_TRACE(int(baseline));
                Style s = BaseStyle();
                s.SetOverflow(Overflow::Hidden);
                s.textLayoutStyle.textOverflow = TextOverflow::Ellipsis;
                s.textLayoutStyle.baseline     = baseline;
                const StyleRef style = styleManager.Register(s);

                const GlyphDraw typed = LastGlyph(style, "x\xE2\x80\xA6", 200);
                const GlyphDraw cut   = LastGlyph(style, "a long name that doesn't fit in the box", 40);
                ASSERT_GT(cut.size.y, 0.0f);
                EXPECT_FLOAT_EQ(cut.size.y, typed.size.y) << "the same glyph";
                EXPECT_NEAR(cut.pos.y, typed.pos.y, 0.5f) << "at the same height";
            }
        }

        TEST_F(TextPaintTest, AStateThatOnlyRecolorsKeepsTheLayout)
        {
            Style s = BaseStyle();
            s.states[ElementState::Hover].textColor = ColorStyle::Create(kRed);
            const StyleRef style = styleManager.Register(s);

            const TextID id = Register(style, ElementState::None);
            const TextMeasurement before = textSystem->GetTextMeasurement(id);
            EXPECT_EQ(CountColor(Render(id), ColorIndex(kBlue)), 5u);
            const uint64_t layouts = textSystem->LayoutsMade();
            ASSERT_GT(layouts, 0u);

            EXPECT_EQ(Register(style, ElementState::Hover), id);
            EXPECT_EQ(textSystem->GetTextMeasurement(id).maxWidth, before.maxWidth);
            EXPECT_EQ(CountColor(Render(id), ColorIndex(kRed)), 5u) << "the colors follow the state";
            EXPECT_EQ(textSystem->LayoutsMade(), layouts) << "laid out again for a color";
        }

        // Drawing at another width lays the text out at that width only; its measurement stays.
        TEST_F(TextPaintTest, AnotherWidthKeepsTheMeasurement)
        {
            const TextID id = Register(styleManager.Register(BaseStyle()), ElementState::None);
            const TextMeasurement measured = textSystem->GetTextMeasurement(id);
            Render(id);
            const uint64_t layouts = textSystem->LayoutsMade();
            EXPECT_EQ(textSystem->GetTextMeasurement(id).maxWidth, measured.maxWidth);
            EXPECT_EQ(textSystem->LayoutsMade(), layouts);

            textSystem->RenderText(id, IntRect{ 0, 0, 120, 40 }, IntRect{ 0, 0, 120, 40 }, clip);
            EXPECT_EQ(textSystem->LayoutsMade(), layouts + 1) << "one layout for the new width";
            EXPECT_EQ(textSystem->GetTextMeasurement(id).maxWidth, measured.maxWidth);
            EXPECT_EQ(textSystem->LayoutsMade(), layouts + 1);
        }

        TEST_F(TextPaintTest, AStateWithAnotherFontLaysTheTextOutAgain)
        {
            Style s = BaseStyle();
            s.states[ElementState::Hover].textStyle.fontSize = 28.0f;
            const StyleRef style = styleManager.Register(s);

            const TextID id = Register(style, ElementState::None);
            const int32_t width = textSystem->GetTextMeasurement(id).maxWidth;
            const uint64_t layouts = textSystem->LayoutsMade();

            Register(style, ElementState::Hover);
            EXPECT_GT(textSystem->GetTextMeasurement(id).maxWidth, width);
            EXPECT_GT(textSystem->LayoutsMade(), layouts);
        }

        // TextLayoutStyle comes with the layout style, so two styles with the same text but
        // another alignment still lay the text out again.
        TEST_F(TextPaintTest, AnotherAlignmentLaysTheTextOutAgain)
        {
            Style start = BaseStyle();
            Style end   = BaseStyle();
            end.textLayoutStyle.horizontalAlignment = TextAlign::End;
            const StyleRef startRef = styleManager.Register(start);
            const StyleRef endRef   = styleManager.Register(end);
            ASSERT_EQ(startRef.visualTextStyleIndex, endRef.visualTextStyleIndex);
            ASSERT_NE(startRef.layoutStyleIndex, endRef.layoutStyleIndex);

            const TextID id = Register(startRef, ElementState::None);
            const float startX = Render(id).front().pos.x;
            const uint64_t layouts = textSystem->LayoutsMade();

            Register(endRef, ElementState::None);
            EXPECT_GT(Render(id).front().pos.x, startX + 50.0f) << "drawn at the end of the box";
            EXPECT_GT(textSystem->LayoutsMade(), layouts);
        }

        // A link with an underline in its style shows it at rest, as far under the text as its
        // offset says; without one, its underline shows only while it's hovered.
        TEST_F(TextPaintTest, ALinksUnderlineShowsAtRestWhenItsStyleHasOne)
        {
            const StyleRef style = styleManager.Register(BaseStyle());
            auto draw = [&](LinkStyle link) {
                ContentRun runs[] = {
                    { .type = ContentRun::Text, .text = "Read " },
                    { .type = ContentRun::Text, .text = "docs", .contentId = 1, .isLink = true, .linkStyle = link },
                };
                const TextID id = textSystem->RegisterRichText(element, runs, style, ElementState::None);
                return Render(id);
            };
            const size_t glyphs = draw(LinkStyle{}).size();   // the text alone: no underline at rest

            LinkStyle underlined;
            underlined.underline.thickness = 1;
            underlined.underline.offset    = 1;
            const std::vector<GlyphDraw> near = draw(underlined);
            ASSERT_GT(near.size(), glyphs) << "the underline, at rest";

            underlined.underline.offset = 4;
            const std::vector<GlyphDraw> far = draw(underlined);
            ASSERT_EQ(far.size(), near.size());
            // Drawn first: under the text.
            EXPECT_NEAR(far.front().pos.y - near.front().pos.y, 3.0f, 1.0f) << "three px further down";

            underlined.underline.style = DecorationStyle::Dotted;
            EXPECT_GT(draw(underlined).size(), glyphs) << "dotted, at rest too";
        }

        // A run with a background (inline code, say) has a box drawn under it, as wide as the run
        // and in its color; the runs around it have none.
        TEST_F(TextPaintTest, ARunsBackgroundIsDrawnUnderIt)
        {
            const StyleRef style = styleManager.Register(BaseStyle());
            ContentRun runs[] = {
                { .type = ContentRun::Text, .text = "Run " },
                { .type = ContentRun::Text, .text = "code", .background = kGreen },
                { .type = ContentRun::Text, .text = " here" },
            };
            const TextID id = textSystem->RegisterRichText(element, runs, style, ElementState::None);
            graphicsContext.Renderer().quads.clear();
            const std::vector<GlyphDraw> glyphs = Render(id);
            const auto& quads = graphicsContext.Renderer().quads;
            ASSERT_EQ(quads.size(), 1u) << "one box, under the middle run";
            EXPECT_TRUE(SameColor(quads[0].color, ColorIndex(kGreen)));
            // Under the four glyphs of "code", and no others.
            const IntRect box = quads[0].rect;
            const auto under = std::ranges::count_if(glyphs, [&](const GlyphDraw& g) {
                const float middle = g.pos.x + g.size.x * 0.5f;
                return g.size.x > 0.0f && middle > float(box.x) && middle < float(box.x + box.w);
            });
            EXPECT_EQ(under, 4);
        }

        TEST_F(TextPaintTest, ARunsColorPaintsOnlyItsGlyphs)
        {
            const StyleRef style = styleManager.Register(BaseStyle());
            ContentRun runs[] = {
                { .type = ContentRun::Text, .text = "Run " },
                { .type = ContentRun::Text, .text = "bold", .color = kGreen },
                { .type = ContentRun::Text, .text = " here" },
            };
            const TextID id = textSystem->RegisterRichText(element, runs, style, ElementState::None);
            const std::vector<GlyphDraw> glyphs = Render(id);
            EXPECT_EQ(CountColor(glyphs, ColorIndex(kGreen)), 4u);
            EXPECT_EQ(CountColor(glyphs, ColorIndex(kGreen)) + CountColor(glyphs, ColorIndex(kBlue)), glyphs.size())
                << "the other runs keep the element's color";
        }

        // A run whose color changes every frame (an animated one) doesn't add a style a frame: a
        // run color's style is freed some frames after nothing draws with it.
        TEST_F(TextPaintTest, ARunColorThatChangesEveryFrameDoesNotGrowTheStyles)
        {
            const StyleRef style = styleManager.Register(BaseStyle());
            auto draw = [&](uint8_t red) {
                ContentRun runs[] = {
                    { .type = ContentRun::Text, .text = "Run " },
                    { .type = ContentRun::Text, .text = "bold", .background = color4b{ 0, red, 0, 255 }, .color = color4b{ red, 0, 0, 255 } },
                };
                Render(textSystem->RegisterRichText(element, runs, style, ElementState::None));
                NextFrame();
            };
            draw(0);
            const size_t before = styleManager.VisualColorStyleCount();
            for (int frame = 1; frame < 250; ++frame)
                draw(uint8_t(frame));
            EXPECT_LE(styleManager.VisualColorStyleCount(), before + 64) << "a style or two a frame, kept";
        }

        // A cached scope's text isn't drawn again while its capture is: replaying the capture keeps
        // the run colors it was drawn with. One whose color went is drawn again.
        TEST_F(TextPaintTest, ACachedScopeKeepsItsRunColorsUntilOneGoes)
        {
            const StyleRef style = styleManager.Register(BaseStyle());
            const ID scope{ .value{ .integer = 91 } };
            ContentRun runs[] = {
                { .type = ContentRun::Text, .text = "Run " },
                { .type = ContentRun::Text, .text = "bold", .color = kGreen },
            };
            textSystem->BeginAtlasRecord(scope);
            const std::vector<GlyphDraw> captured = Render(textSystem->RegisterRichText(element, runs, style, ElementState::None));
            textSystem->EndAtlasRecord();
            ASSERT_EQ(CountColor(captured, ColorIndex(kGreen)), 4u);
            const auto green = std::ranges::find_if(captured, [&](const GlyphDraw& d) { return SameColor(d.color, ColorIndex(kGreen)); });
            const VisualColorStyleIndex runColor = green->color;

            for (int frame = 0; frame < 200; ++frame)
            {
                NextFrame();
                ASSERT_TRUE(textSystem->ReplayAtlasTouches(scope)) << "frame " << frame;
            }
            EXPECT_TRUE(SameColor(runColor, ColorIndex(kGreen))) << "kept while the capture is shown";

            // Not shown for a while: the color is let go, and the capture can't be shown again.
            for (int frame = 0; frame < 200; ++frame)
                NextFrame();
            EXPECT_FALSE(textSystem->ReplayAtlasTouches(scope));
        }

        TEST_F(TextPaintTest, ADecorationIsDrawnInTheTextColorUnlessItHasItsOwn)
        {
            Style s = BaseStyle();
            for (StyleState& state : s.states)
                state.textStyle.underLine.thickness = 2;
            s.states[ElementState::Hover].decorationColors[size_t(DecorationLine::Under)] = ColorStyle::Create(kGreen);
            const StyleRef style = styleManager.Register(s);

            const TextID id = Register(style, ElementState::None);
            const std::vector<GlyphDraw> plain = Render(id);
            const size_t underline = plain.size() - 5;
            ASSERT_GT(underline, 0u) << "no underline drawn";
            EXPECT_EQ(CountColor(plain, ColorIndex(kBlue)), plain.size());
            const uint64_t layouts = textSystem->LayoutsMade();

            Register(style, ElementState::Hover);
            const std::vector<GlyphDraw> hovered = Render(id);
            ASSERT_EQ(hovered.size(), plain.size());
            EXPECT_EQ(CountColor(hovered, ColorIndex(kGreen)), underline);
            EXPECT_EQ(CountColor(hovered, ColorIndex(kBlue)), 5u);
            EXPECT_EQ(textSystem->LayoutsMade(), layouts) << "laid out again for a decoration color";
        }

        // Each line takes its own color, the ones under the text and the one over it.
        TEST_F(TextPaintTest, EachDecorationLineHasItsColor)
        {
            Style s = BaseStyle();
            for (StyleState& state : s.states)
            {
                state.textStyle.underLine.thickness   = 2;
                state.textStyle.throughLine.thickness = 2;
                state.textStyle.overLine.thickness    = 2;
                state.decorationColors[size_t(DecorationLine::Under)]   = ColorStyle::Create(kGreen);
                state.decorationColors[size_t(DecorationLine::Through)] = ColorStyle::Create(kRed);
                state.decorationColors[size_t(DecorationLine::Over)]    = ColorStyle::Create(kWhite);
            }
            const std::vector<GlyphDraw> draws = Render(Register(styleManager.Register(s), ElementState::None));
            ASSERT_GT(CountColor(draws, ColorIndex(kGreen)), 0u);
            ASSERT_GT(CountColor(draws, ColorIndex(kRed)), 0u);
            ASSERT_GT(CountColor(draws, ColorIndex(kWhite)), 0u);
            EXPECT_EQ(CountColor(draws, ColorIndex(kBlue)), 5u);

            float underTop = 0.0f, throughTop = 0.0f, overTop = 0.0f;
            for (const GlyphDraw& d : draws)
            {
                if (d.color == ColorIndex(kGreen)) underTop   = d.pos.y;
                if (d.color == ColorIndex(kRed))   throughTop = d.pos.y;
                if (d.color == ColorIndex(kWhite)) overTop    = d.pos.y;
            }
            EXPECT_GT(underTop, throughTop) << "the underline is under the line through";
            EXPECT_GT(throughTop, overTop) << "the overline is over the line through";
        }

        // A field being edited draws its decorations in their colors too.
        TEST_F(TextPaintTest, AFocusedFieldsDecorationHasItsColor)
        {
            Style s = BaseStyle();
            for (StyleState& state : s.states)
            {
                state.textStyle.underLine.thickness = 2;
                state.textStyle.overLine.thickness  = 2;
                state.decorationColors[size_t(DecorationLine::Under)] = ColorStyle::Create(kGreen);
                state.decorationColors[size_t(DecorationLine::Over)]  = ColorStyle::Create(kWhite);
            }
            const StyleRef style = styleManager.Register(s);
            const ID field{ .value{ .integer = 82 } };
            std::string bound = "abc";
            bool modified = false;

            auto& focus = stateManager.TextEditorFocus();
            focus.Set(field);
            textSystem->SwitchTextEditor(focus.GetPrevFocusedID(), field);
            focus.Commit();

            std::vector<GlyphDraw> draws;
            for (int frame = 0; frame < 2; ++frame)
                draws = Render(textSystem->RegisterEditableText(field, bound, TextEditBehavior::None, style, ElementState::Focused, modified));
            EXPECT_EQ(CountColor(draws, ColorIndex(kBlue)), 3u);
            EXPECT_GT(CountColor(draws, ColorIndex(kGreen)), 0u);
            EXPECT_GT(CountColor(draws, ColorIndex(kWhite)), 0u);
        }

        // With a language set for all text, which goes first among a text's attributes, its
        // decoration (the last of them) is still drawn: plain, and in a field being edited.
        TEST_F(TextPaintTest, ADecorationIsDrawnWithALanguageSet)
        {
            textSystem->SetLanguage("en");
            Style s = BaseStyle();
            for (StyleState& state : s.states)
            {
                state.textStyle.underLine.thickness = 2;
                state.decorationColors[size_t(DecorationLine::Under)] = ColorStyle::Create(kGreen);
            }
            const StyleRef style = styleManager.Register(s);
            EXPECT_GT(CountColor(Render(Register(style, ElementState::None)), ColorIndex(kGreen)), 0u) << "plain";

            const ID field{ .value{ .integer = 84 } };
            std::string bound = "abc";
            bool modified = false;
            auto& focus = stateManager.TextEditorFocus();
            focus.Set(field);
            textSystem->SwitchTextEditor(focus.GetPrevFocusedID(), field);
            focus.Commit();
            std::vector<GlyphDraw> draws;
            for (int frame = 0; frame < 2; ++frame)
                draws = Render(textSystem->RegisterEditableText(field, bound, TextEditBehavior::None, style, ElementState::Focused, modified));
            EXPECT_GT(CountColor(draws, ColorIndex(kGreen)), 0u) << "edited";
        }

        // A link run is drawn in its link color, the rest in the text color.
        TEST_F(TextPaintTest, ALinkIsDrawnInItsColor)
        {
            const StyleRef style = styleManager.Register(BaseStyle());
            std::vector<ContentRun> runs{
                { .type = ContentRun::Text, .text = "ab" },
                { .type = ContentRun::Text, .text = "cde", .contentId = 1, .isLink = true,
                  .linkStyle = { .color = kGreen, .hoverColor = kRed } },
            };
            const std::vector<GlyphDraw> draws = Render(textSystem->RegisterRichText(element, runs, style, ElementState::None));
            EXPECT_EQ(CountColor(draws, ColorIndex(kBlue)), 2u);
            EXPECT_EQ(CountColor(draws, ColorIndex(kGreen)), 3u);
        }

        // So it is in selectable text, focused or not.
        TEST_F(TextPaintTest, ASelectableLinkIsDrawnInItsColor)
        {
            const StyleRef style = styleManager.Register(BaseStyle());
            std::vector<ContentRun> runs{
                { .type = ContentRun::Text, .text = "ab" },
                { .type = ContentRun::Text, .text = "cde", .contentId = 1, .isLink = true,
                  .linkStyle = { .color = kGreen, .hoverColor = kRed } },
            };
            const ID field{ .value{ .integer = 83 } };
            bool modified = false;
            auto render = [&] {
                return Render(textSystem->RegisterEditableRichText(field, runs, TextEditBehavior::ReadOnly, style,
                                                                   ElementState::None, modified));
            };
            std::vector<GlyphDraw> draws = render();
            EXPECT_EQ(CountColor(draws, ColorIndex(kBlue)), 2u);
            EXPECT_EQ(CountColor(draws, ColorIndex(kGreen)), 3u);

            auto& focus = stateManager.TextEditorFocus();
            focus.Set(field);
            textSystem->SwitchTextEditor(focus.GetPrevFocusedID(), field);
            focus.Commit();
            for (int frame = 0; frame < 2; ++frame)
                draws = render();
            EXPECT_EQ(CountColor(draws, ColorIndex(kBlue)), 2u) << "focused";
            EXPECT_EQ(CountColor(draws, ColorIndex(kGreen)), 3u) << "focused";
        }
    }
}
