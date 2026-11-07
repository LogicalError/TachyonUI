// Integration tests for Button, Field(bool&), Field(string&) and Label,
// run through the headless harness with the real handlers.

#include <gtest/gtest.h>

#include "headless_gui_app.h"
#include "skribidi_test_fixture.h"

#include <gui_core/graphics/gui_custom_draw_context.h>

#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/elements/gui_element_flags.h>
#include <gui_core/input/gui_input_enums.h>

#include <cmath>
#include <limits>
#include <string>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {

        class ControlsBasicTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;

            void SetUp()    override { ASSERT_TRUE(app.Initialize()); }
            void TearDown() override { app.Cleanup(); }
        };

        // ---------------------------------------------------------------------
        // Button
        // ---------------------------------------------------------------------

        // Numbers show in the monospace font (#79): numeric fields default to the number
        // style, a textbox in the monospace font at the textbox's height.
        TEST_F(ControlsBasicTest, NumericFieldsUseTheMonospaceNumberStyle)
        {
            const ID numberID = app.idManager.CreateStaticID();
            const ID textID   = app.idManager.CreateStaticID();
            float value = 1.5f;
            std::string text = "1.5";
            app.Frame([&] {
                app.BeginRootWindow();
                app.Field(numberID, value);
                app.Field(textID, text);
                app.EndRootWindow();
            });

            const ElementRef numberRef = app.elementStorage.FindElementRefByID(numberID);
            ASSERT_TRUE(numberRef.IsValid());
            EXPECT_EQ(app.elementStorage.GetElementUnsafe(numberRef).styleRef, app.theme.regularControls.number);
            const VisualTextStyle* number = app.styleManager.GetVisualTextStyle(app.theme.regularControls.number.visualTextStyleIndex, ElementState::None);
            ASSERT_NE(number, nullptr);
            EXPECT_EQ(number->textStyle.family, FontFamily::Monospace);
            EXPECT_EQ(app.GetElementRect(numberID).h, app.GetElementRect(textID).h) << "rows stay aligned";
        }

        // ---------------------------------------------------------------------
        // Image
        // ---------------------------------------------------------------------

        // An image part (a UV rect) draws over a clear box: the box itself has no image, so
        // the box's texture fill doesn't show the whole image under the part.
        TEST_F(ControlsBasicTest, AnImagePartDrawsOverAClearBox)
        {
            const ID whole = app.idManager.CreateStaticID();
            const ID part  = app.idManager.CreateStaticID();
            app.Frame([&] {
                app.BeginRootWindow();
                app.Image(whole, ImageIndex(3), LayoutOverride::FixedSize(Pixels{ 40 }, Pixels{ 40 }));
                app.Image(part, ImageIndex(3), UVRect{ { 0.5f, 0.0f }, { 1.0f, 1.0f } }, LayoutOverride::FixedSize(Pixels{ 40 }, Pixels{ 40 }));
                app.EndRootWindow();
            });
            const Element& wholeElement = app.elementStorage.GetElementUnsafe(app.elementStorage.FindElementRefByID(whole));
            EXPECT_EQ(wholeElement.backgroundImage, ImageIndex(3));
            const ElementRef partRef = app.elementStorage.FindElementRefByID(part);
            ASSERT_TRUE(partRef.IsValid());
            const Element& partElement = app.elementStorage.GetElementUnsafe(partRef);
            EXPECT_EQ(partElement.backgroundImage, ImageIndex::Invalid);
            EXPECT_EQ(partElement.colorStyleIndex, app.theme.clearColorStyle);
            EXPECT_EQ(partElement.styleRef, app.theme.imageStyleRef);
            EXPECT_EQ(app.GetElementRect(part).size, app.GetElementRect(whole).size);

            // Its draw: the part of the image, stretched over the padding box.
            const ElementCustomDraw* draw = app.elementStorage.GetCustomDraw(partRef);
            ASSERT_TRUE(draw != nullptr && draw->callback);
            const ElementRect* rects = app.elementStorage.GetElementRectPtr(partRef);
            GUITest::StubGUIRenderer renderer;
            CustomDrawContext ctx{ renderer, app.elementStorage, partRef, rects->rect, rects->innerRect,
                                   IntRect{ 0, 0, 4000, 4000 }, float3x2{}, int2{ 0, 0 } };
            draw->callback(ctx);
            ASSERT_EQ(renderer.quads.size(), 1u);
            EXPECT_EQ(renderer.quads[0].rect, (IntRect{ 0, 0, rects->innerRect.w, rects->innerRect.h }));
            EXPECT_EQ(renderer.quads[0].color, app.theme.imageFillColorStyle);
            EXPECT_EQ(renderer.quads[0].image, ImageIndex(3));
            EXPECT_EQ(renderer.quads[0].uv, (UVRect{ { 0.5f, 0.0f }, { 1.0f, 1.0f } }));
        }

        // A custom draw's quad takes the UV rect it's given, with a sub-clip or without.
        TEST_F(ControlsBasicTest, ACustomDrawQuadKeepsItsUVRect)
        {
            const ID id = app.idManager.CreateStaticID();
            app.Frame([&] {
                app.BeginRootWindow();
                app.Image(id, ImageIndex(3), LayoutOverride::FixedSize(Pixels{ 40 }, Pixels{ 40 }));
                app.EndRootWindow();
            });
            const ElementRef ref = app.elementStorage.FindElementRefByID(id);
            ASSERT_TRUE(ref.IsValid());
            GUITest::StubGUIRenderer renderer;
            CustomDrawContext ctx{ renderer, app.elementStorage, ref, IntRect{ 0, 0, 40, 40 }, IntRect{ 0, 0, 40, 40 },
                                   IntRect{ 0, 0, 4000, 4000 }, float3x2{}, int2{ 0, 0 } };
            const UVRect a{ { 0.25f, 0.5f }, { 0.75f, 1.0f } }, b{ { 1.0f, 0.0f }, { 0.0f, 1.0f } };
            ctx.RenderQuad(IntRect{ 0, 0, 10, 10 }, app.theme.imageFillColorStyle, 0.0f, ImageIndex(3), a);
            ctx.RenderQuad(IntRect{ 0, 0, 10, 10 }, IntRect{ 0, 0, 5, 5 }, app.theme.imageFillColorStyle, 0.0f, ImageIndex(3), b);
            ctx.RenderQuad(IntRect{ 0, 0, 10, 10 }, app.theme.imageFillColorStyle);
            ASSERT_EQ(renderer.quads.size(), 3u);
            EXPECT_EQ(renderer.quads[0].uv, a);
            EXPECT_EQ(renderer.quads[1].uv, b) << "with a sub-clip";
            EXPECT_EQ(renderer.quads[2].uv, UVRect{}) << "none given: the whole image";
        }

        // A rect in pixels over the image's size: an atlas cell's UV rect.
        TEST(UVRectTest, FromPixelsIsTheRectOverTheImageSize)
        {
            EXPECT_EQ(UVRect::FromPixels(IntRect{ 256, 128, 128, 64 }, uint2{ 1024, 512 }),
                      (UVRect{ { 0.25f, 0.25f }, { 0.375f, 0.375f } }));
            EXPECT_EQ(UVRect::FromPixels(IntRect{ 0, 0, 1024, 512 }, uint2{ 1024, 512 }), UVRect{}) << "the whole image";
            EXPECT_EQ(UVRect::FromPixels(IntRect{ 2, 3, 4, 5 }, uint2{ 0, 0 }),
                      (UVRect{ { 2.0f, 3.0f }, { 6.0f, 8.0f } })) << "no size: the pixels as they are";
        }

        // Sized from the source, an image part takes the aspect of the pixels it shows.
        TEST_F(ControlsBasicTest, AnImagePartIsSizedToThePartsAspect)
        {
            const ID part  = app.idManager.CreateStaticID();
            const ID same  = app.idManager.CreateStaticID();
            const ID boxed = app.idManager.CreateStaticID();
            app.Frame([&] {
                app.BeginRootWindow();
                // A quarter of a 200x100 image's width and half its height is 50x50.
                app.Image(part, ImageIndex(3), uint2{ 200, 100 }, UVRect{ { 0.25f, 0.5f }, { 0.5f, 1.0f } });
                app.Image(same, ImageIndex(3), uint2{ 50, 50 });
                // In a 40 by 40 box: 20 by 40.
                app.Image(boxed, ImageIndex(3), uint2{ 200, 100 }, UVRect{ { 0.5f, 0.0f }, { 0.25f, 1.0f } },
                          LayoutOverride::FixedSize(Pixels{ 40 }, Pixels{ 40 }));
                app.EndRootWindow();
            });
            EXPECT_EQ(app.GetElementRect(part).size, app.GetElementRect(same).size);
            EXPECT_EQ(app.GetElementRect(boxed).size, (int2{ 20, 40 })) << "flipped, the same aspect";
        }

        TEST_F(ControlsBasicTest, ButtonRendersAndIsHittable)
        {
            ID buttonID = app.idManager.CreateStaticID();
            int callCount = 0;

            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "Click", [&] { callCount++; });
                app.EndRootWindow();
            });

            ASSERT_TRUE(app.HasElement(buttonID));
            const auto rect = app.GetElementRect(buttonID);
            EXPECT_GT(rect.w, 0);
            EXPECT_GT(rect.h, 0);
            EXPECT_EQ(callCount, 0);  // No interaction yet.
        }

        TEST_F(ControlsBasicTest, ClickingButtonInvokesOnClickExactlyOnce)
        {
            ID buttonID = app.idManager.CreateStaticID();
            int callCount = 0;

            // Frame 1: lay out the button.
            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "Click", [&] { callCount++; });
                app.EndRootWindow();
            });

            // Click in the centre and run the dispatch frame.
            const int2 center = app.CenterOf(buttonID);
            app.InjectMouseMove(center);
            app.InjectMouseDown(center);
            app.InjectMouseUp(center);

            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "Click", [&] { callCount++; });
                app.EndRootWindow();
            });

            EXPECT_EQ(callCount, 1);
        }

        // Press and release can land in the same frame (touchpad taps, hitches);
        // the press's capture must still be released so the next click reaches
        // whatever is under the cursor.
        TEST_F(ControlsBasicTest, SameFrameClickReleasesCapture)
        {
            ID a = app.idManager.CreateStaticID();
            ID b = app.idManager.CreateStaticID();
            int aCount = 0, bCount = 0;
            auto emit = [&] {
                app.BeginRootWindow();
                app.Button(a, "A", [&] { aCount++; });
                app.Button(b, "B", [&] { bCount++; });
                app.EndRootWindow();
            };

            app.Frame(emit);
            app.ClickAt(app.CenterOf(a));
            app.Frame(emit);
            app.Frame(emit);
            EXPECT_EQ(app.guiStateManager.Interaction().GetCaptureID(), ID::Invalid());

            app.ClickAt(app.CenterOf(b));
            app.Frame(emit);
            EXPECT_EQ(aCount, 1);
            EXPECT_EQ(bCount, 1);
        }

        TEST_F(ControlsBasicTest, ClickAtHelperFiresButton)
        {
            ID buttonID = app.idManager.CreateStaticID();
            int callCount = 0;

            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "Click", [&] { callCount++; });
                app.EndRootWindow();
            });

            app.ClickAt(app.CenterOf(buttonID));
            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "Click", [&] { callCount++; });
                app.EndRootWindow();
            });

            EXPECT_EQ(callCount, 1);
        }

        TEST_F(ControlsBasicTest, ButtonReturnsTrueOnFrameAfterDispatch)
        {
            // m_clickID is set during end-of-frame dispatch, so Button() returns true
            // on the frame after the click.
            ID buttonID = app.idManager.CreateStaticID();

            auto draw = [&](bool& out) {
                app.BeginRootWindow();
                out = app.Button(buttonID, "B", [] {});
                app.EndRootWindow();
            };
            bool ret = false;
            app.Frame([&] { draw(ret); });
            EXPECT_FALSE(ret);

            app.ClickAt(app.CenterOf(buttonID));
            app.Frame([&] { draw(ret); });
            // The click dispatches at the end of this frame; construction sees none yet.
            EXPECT_FALSE(ret);

            app.Frame([&] { draw(ret); });
            EXPECT_TRUE(ret);

            app.Frame([&] { draw(ret); });
            EXPECT_FALSE(ret);  // Cleared after one frame.
        }

        TEST_F(ControlsBasicTest, ClickOutsideButtonDoesNotInvokeCallback)
        {
            ID buttonID = app.idManager.CreateStaticID();
            int callCount = 0;

            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [&] { callCount++; });
                app.EndRootWindow();
            });

            // Click well clear of the button (top-left corner of the window).
            app.ClickAt({ 4, 4 });
            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [&] { callCount++; });
                app.EndRootWindow();
            });
            EXPECT_EQ(callCount, 0);
        }

        TEST_F(ControlsBasicTest, ButtonDoesNotFireOnHoverAlone)
        {
            ID buttonID = app.idManager.CreateStaticID();
            int callCount = 0;

            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [&] { callCount++; });
                app.EndRootWindow();
            });

            // Move the cursor over the button without pressing.
            app.InjectMouseMove(app.CenterOf(buttonID));
            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [&] { callCount++; });
                app.EndRootWindow();
            });
            EXPECT_EQ(callCount, 0);
        }

        TEST_F(ControlsBasicTest, RightClickOnButtonDoesNotInvokeCallback)
        {
            ID buttonID = app.idManager.CreateStaticID();
            int callCount = 0;

            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [&] { callCount++; });
                app.EndRootWindow();
            });

            // MakeButtonHandler only reacts to LeftPressed - Right should be ignored.
            app.ClickAt(app.CenterOf(buttonID), MouseButtons::Right);
            app.Frame([&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [&] { callCount++; });
                app.EndRootWindow();
            });
            EXPECT_EQ(callCount, 0);
        }

        TEST_F(ControlsBasicTest, MultipleClicksFireMultipleCallbacks)
        {
            ID buttonID = app.idManager.CreateStaticID();
            int callCount = 0;

            auto draw = [&] {
                app.BeginRootWindow();
                app.Button(buttonID, "B", [&] { callCount++; });
                app.EndRootWindow();
            };
            app.Frame(draw);

            const int2 center = app.CenterOf(buttonID);
            for (int i = 0; i < 3; ++i)
            {
                app.ClickAt(center);
                app.Frame(draw);
            }
            EXPECT_EQ(callCount, 3);
        }

        TEST_F(ControlsBasicTest, IndeterminateButtonStillRendersButContextChanges)
        {
            // Buttons stay interactive inside BeginIndeterminate.
            ID buttonID = app.idManager.CreateStaticID();
            int callCount = 0;

            app.Frame([&] {
                app.BeginRootWindow();
                app.BeginIndeterminate();
                app.Button(buttonID, "B", [&] { callCount++; });
                app.EndIndeterminate();
                app.EndRootWindow();
            });

            ASSERT_TRUE(app.HasElement(buttonID));

            app.ClickAt(app.CenterOf(buttonID));
            app.Frame([&] {
                app.BeginRootWindow();
                app.BeginIndeterminate();
                app.Button(buttonID, "B", [&] { callCount++; });
                app.EndIndeterminate();
                app.EndRootWindow();
            });
            EXPECT_EQ(callCount, 1);
        }

        // ---------------------------------------------------------------------
        // Field(bool&) - Toggle
        // ---------------------------------------------------------------------

        TEST_F(ControlsBasicTest, ToggleFieldFlipsValueOnClick)
        {
            ID toggleID = app.idManager.CreateStaticID();
            bool value = false;

            auto draw = [&] {
                app.BeginRootWindow();
                app.Field(toggleID, value, "Enabled");
                app.EndRootWindow();
            };
            app.Frame(draw);
            ASSERT_FALSE(value);

            app.ClickAt(app.CenterOf(toggleID));
            app.Frame(draw);
            EXPECT_TRUE(value);
        }

        TEST_F(ControlsBasicTest, TogglingTwiceReturnsToOriginalValue)
        {
            ID toggleID = app.idManager.CreateStaticID();
            bool value = false;

            auto draw = [&] {
                app.BeginRootWindow();
                app.Field(toggleID, value, "Enabled");
                app.EndRootWindow();
            };
            app.Frame(draw);

            const int2 center = app.CenterOf(toggleID);
            app.ClickAt(center); app.Frame(draw);
            ASSERT_TRUE(value);
            app.ClickAt(center); app.Frame(draw);
            EXPECT_FALSE(value);
        }

        TEST_F(ControlsBasicTest, ToggleFieldReturnsModifiedOnePostDispatchFrame)
        {
            // Field(bool&) returns true on the frame after the click is dispatched.
            ID toggleID = app.idManager.CreateStaticID();
            bool value = false;
            bool modified = false;

            auto draw = [&] {
                app.BeginRootWindow();
                modified = app.Field(toggleID, value, "T");
                app.EndRootWindow();
            };
            app.Frame(draw);
            EXPECT_FALSE(modified);

            app.ClickAt(app.CenterOf(toggleID));
            app.Frame(draw);     // dispatch happens here; modified read pre-dispatch
            EXPECT_FALSE(modified);

            app.Frame(draw);     // first frame after dispatch sees the click
            EXPECT_TRUE(modified);

            app.Frame(draw);     // cleared again
            EXPECT_FALSE(modified);
        }

        TEST_F(ControlsBasicTest, ClickOutsideToggleLeavesValueUnchanged)
        {
            ID toggleID = app.idManager.CreateStaticID();
            bool value = true;

            auto draw = [&] {
                app.BeginRootWindow();
                app.Field(toggleID, value, "T");
                app.EndRootWindow();
            };
            app.Frame(draw);

            app.ClickAt({ 4, 4 });  // far from the toggle
            app.Frame(draw);
            EXPECT_TRUE(value);
        }

        // ---------------------------------------------------------------------
        // Field(string&) - Text edit. StubTextSystem routes typed characters into
        // the bound string via RegisterEditableText.
        // ---------------------------------------------------------------------

        TEST_F(ControlsBasicTest, TextFieldRegistersAsEditableElement)
        {
            ID fieldID = app.idManager.CreateStaticID();
            std::string text = "hello";

            app.Frame([&] {
                app.BeginRootWindow();
                app.Field(fieldID, text);
                app.EndRootWindow();
            });

            ASSERT_TRUE(app.HasElement(fieldID));
            EXPECT_TRUE(app.ownedTextSystem.IsTextEditor(fieldID));
        }

        TEST_F(ControlsBasicTest, ClickingTextFieldFocusesIt)
        {
            ID fieldID = app.idManager.CreateStaticID();
            std::string text = "hi";

            auto draw = [&] {
                app.BeginRootWindow();
                app.Field(fieldID, text);
                app.EndRootWindow();
            };
            app.Frame(draw);

            app.ClickAt(app.CenterOf(fieldID));
            app.Frame(draw);

            EXPECT_EQ(app.guiStateManager.Interaction().GetFocusedID(), fieldID);
        }

        TEST_F(ControlsBasicTest, TypingAfterFocusAppendsToBoundString)
        {
            ID fieldID = app.idManager.CreateStaticID();
            std::string text = "";

            auto draw = [&] {
                app.BeginRootWindow();
                app.Field(fieldID, text);
                app.EndRootWindow();
            };
            app.Frame(draw);

            // Click to focus, then frame to settle SwitchTextEditor.
            app.ClickAt(app.CenterOf(fieldID));
            app.Frame(draw);

            app.InjectText("abc");
            app.Frame(draw);

            EXPECT_EQ(text, "abc");
        }

        // A text field keeps the text focus only while it has the keyboard focus: clicking a
        // button takes both, so the field's editor is let go (and its text committed).
        TEST_F(ControlsBasicTest, ClickingAButtonTakesTheTextFocus)
        {
            const ID fieldID = app.idManager.CreateStaticID();
            const ID buttonID = app.idManager.CreateStaticID();
            std::string text = "abc";
            auto draw = [&] {
                app.BeginRootWindow();
                app.Field(fieldID, text);
                app.Button(buttonID, "OK", [] {});
                app.EndRootWindow();
            };
            app.Frame(draw);
            app.ClickAt(app.CenterOf(fieldID));
            app.Frame(draw);
            ASSERT_EQ(app.guiStateManager.TextEditorFocus().GetFocusedID(), fieldID);

            app.ClickAt(app.CenterOf(buttonID));
            app.Frame(draw);
            EXPECT_EQ(app.guiStateManager.TextEditorFocus().GetFocusedID(), ID::Invalid());
            EXPECT_EQ(app.guiStateManager.TextEditorFocus().GetPrevFocusedID(), ID::Invalid()) << "the editor let go of the field";
        }

        // A focused field takes Ctrl+Z for itself (its own typing, then the app's history):
        // the app's Undo command must not run on top of it.
        TEST_F(ControlsBasicTest, AFocusedFieldTakesCtrlZBeforeTheUndoCommand)
        {
            int undoFired = 0;
            app.commandManager.RegisterCommand(KeyCode::KeyZ, KeyModifier::Control, [&] { undoFired++; }, "Undo");
            app.inputManager->PushGlobalHandler(app.commandManager.MakeHandler());
            const ID fieldID = app.idManager.CreateStaticID();
            std::string text = "abc";
            auto draw = [&] {
                app.BeginRootWindow();
                app.Field(fieldID, text);
                app.EndRootWindow();
            };
            app.Frame(draw);
            app.InjectKey(KeyCode::KeyZ, KeyAction::KeyDown, KeyModifier::Control);
            app.Frame(draw);
            ASSERT_EQ(undoFired, 1) << "with no field focused, Ctrl+Z is the app's Undo";

            app.ClickAt(app.CenterOf(fieldID));
            app.Frame(draw);
            ASSERT_EQ(app.guiStateManager.TextEditorFocus().GetFocusedID(), fieldID);
            app.InjectKey(KeyCode::KeyZ, KeyAction::KeyDown, KeyModifier::Control);
            app.Frame(draw);
            EXPECT_EQ(undoFired, 1) << "the focused field's Ctrl+Z also ran the Undo command";
        }

        // Typed text is UTF-8: 2, 3 and 4 byte characters go in whole, and a backspace takes
        // the last character, not its last byte.
        TEST_F(ControlsBasicTest, TypingNonAsciiText)
        {
            const ID fieldID = app.idManager.CreateStaticID();
            std::string text = "";
            auto draw = [&] {
                app.BeginRootWindow();
                app.Field(fieldID, text);
                app.EndRootWindow();
            };
            app.Frame(draw);
            app.ClickAt(app.CenterOf(fieldID));
            app.Frame(draw);
            app.InjectText("h\xC3\xA9 \xE2\x82\xAC\xF0\x9D\x84\x9E");   // "he-acute euro G-clef"
            app.Frame(draw);
            EXPECT_EQ(text, "h\xC3\xA9 \xE2\x82\xAC\xF0\x9D\x84\x9E");

            app.InjectKey(KeyCode::Backspace, KeyAction::KeyDown);
            app.Frame(draw);
            EXPECT_EQ(text, "h\xC3\xA9 \xE2\x82\xAC") << "the 4-byte character went as one";
        }

        TEST_F(ControlsBasicTest, BackspaceDeletesLastCharacter)
        {
            ID fieldID = app.idManager.CreateStaticID();
            std::string text = "hello";

            auto draw = [&] {
                app.BeginRootWindow();
                app.Field(fieldID, text);
                app.EndRootWindow();
            };
            app.Frame(draw);

            app.ClickAt(app.CenterOf(fieldID));
            app.Frame(draw);
            app.InjectKey(KeyCode::Backspace, KeyAction::KeyDown);
            app.Frame(draw);

            EXPECT_EQ(text, "hell");
        }

        TEST_F(ControlsBasicTest, TypingWithoutFocusDoesNotMutateString)
        {
            ID fieldID = app.idManager.CreateStaticID();
            std::string text = "untouched";

            auto draw = [&] {
                app.BeginRootWindow();
                app.Field(fieldID, text);
                app.EndRootWindow();
            };
            app.Frame(draw);

            // No click -> no focus -> no active text editor.
            app.InjectText("xyz");
            app.Frame(draw);

            EXPECT_EQ(text, "untouched");
        }

        TEST_F(ControlsBasicTest, IndeterminateTextFieldIsReadOnlyPlaceholder)
        {
            // Under BeginIndeterminate, Field(string&) only renders the placeholder.
            ID fieldID = app.idManager.CreateStaticID();
            std::string text = "real";

            app.Frame([&] {
                app.BeginRootWindow();
                app.BeginIndeterminate();
                app.Field(fieldID, text);
                app.EndIndeterminate();
                app.EndRootWindow();
            });

            EXPECT_FALSE(app.ownedTextSystem.IsTextEditor(fieldID));
        }

        // Focusing (e.g. tabbing into) a numeric field that holds NaN/Inf must
        // not mutate the bound value - only a real edit may change it.
        TEST_F(ControlsBasicTest, FocusingNonFiniteNumericFieldDoesNotChangeValue)
        {
            ID id = app.idManager.CreateStaticID();
            ITextSystem& ts = app.ownedTextSystem;
            for (float v0 : { std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity() })
            {
                float value = v0;
                bool modified = false;
                ts.RegisterEditableNumber<float>(
                    id, value, TextEditBehavior::ContinuousUpdate,
                    app.theme.controls->textbox, ElementState::Focused, modified);
                EXPECT_FALSE(modified);
                EXPECT_FALSE(std::isfinite(value)); // value left untouched
            }
        }

        // ---------------------------------------------------------------------
        // Label
        // ---------------------------------------------------------------------

        TEST_F(ControlsBasicTest, LabelRegistersWithIsLabelFlag)
        {
            ID labelID = app.idManager.CreateStaticID();

            app.Frame([&] {
                app.BeginRootWindow();
                app.Label(labelID, "Name");
                app.EndRootWindow();
            });

            auto ref = app.elementStorage.FindElementRefByID(labelID);
            ASSERT_TRUE(ref.IsValid());
            const auto& el = app.elementStorage.GetElementUnsafe(ref);
            EXPECT_NE(el.flags & ElementFlags::IsLabel, ElementFlags::None);
        }

        TEST_F(ControlsBasicTest, LabelRectIsNonEmptyAfterLayout)
        {
            ID labelID = app.idManager.CreateStaticID();

            app.Frame([&] {
                app.BeginRootWindow();
                app.Label(labelID, "Some label text");
                app.EndRootWindow();
            });

            const auto rect = app.GetElementRect(labelID);
            EXPECT_GT(rect.w, 0);
            EXPECT_GT(rect.h, 0);
        }

        TEST_F(ControlsBasicTest, HoveringLabelMarksItHovered)
        {
            ID labelID = app.idManager.CreateStaticID();

            auto draw = [&] {
                app.BeginRootWindow();
                app.Label(labelID, "Hover me");
                app.EndRootWindow();
            };
            app.Frame(draw);

            app.InjectMouseMove(app.CenterOf(labelID));
            app.Frame(draw);

            const auto state = app.guiStateManager.GetCurrentState(labelID);
            EXPECT_NE(state & ElementState::Hover, ElementState::None);
        }

    } // namespace
} // namespace tachyon
