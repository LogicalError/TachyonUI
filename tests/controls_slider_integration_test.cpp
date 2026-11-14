// Integration tests for GUIApplication::SliderField through the harness.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/input/gui_input_enums.h>

#include <cstdint>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class ControlsSliderIntegrationTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            ID sliderID{};
            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                sliderID = app.idManager.CreateStaticID();
            }
            void TearDown() override { app.Cleanup(); }

            // The slider's outer element IS the interactive track - `sliderID`
            // owns the event handler, custom-draw, and SliderState.
            ID FindTrackID() { return sliderID; }

            void EmitSlider(float& value, float minV, float maxV)
            {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.SliderField(sliderID, value, minV, maxV, "",
                                    LayoutOverride{}.Size(300, 30));
                    app.EndRootWindow();
                });
            }
        };

        TEST_F(ControlsSliderIntegrationTest, SliderRenders)
        {
            float v = 0.5f;
            EmitSlider(v, 0.0f, 1.0f);
            EXPECT_TRUE(app.HasElement(sliderID));
        }

        TEST_F(ControlsSliderIntegrationTest, CleanClickEntersEditModeWithoutChangingValue)
        {
            // Blender semantics: a click without movement opens in-place text edit.
            // The value is unchanged, regardless of where on the bar the click landed.
            float v = 0.5f;
            EmitSlider(v, 0.0f, 1.0f);

            ID trackID = FindTrackID();
            ASSERT_NE(trackID, ID::Invalid());
            const auto trackRect = app.GetElementRect(trackID);
            ASSERT_GT(trackRect.w, 0);

            app.ClickAt({ trackRect.x + 1, trackRect.y + trackRect.h / 2 });
            EmitSlider(v, 0.0f, 1.0f);

            EXPECT_FLOAT_EQ(v, 0.5f) << "Clean click must not jump the value";
            EXPECT_EQ(app.guiStateManager.Interaction().GetFocusedID(), trackID)
                << "Clean click should focus the slider for in-place text edit";
        }

        TEST_F(ControlsSliderIntegrationTest, DraggingFromLeftToRightSweepsValue)
        {
            float v = 0.0f;
            EmitSlider(v, 0.0f, 1.0f);

            ID trackID = FindTrackID();
            const auto trackRect = app.GetElementRect(trackID);

            // Drag from the leftmost pixel to the rightmost.
            app.DragFrom(
                { trackRect.x + 1, trackRect.y + trackRect.h / 2 },
                { trackRect.x + trackRect.w - 1, trackRect.y + trackRect.h / 2 });
            EmitSlider(v, 0.0f, 1.0f);

            EXPECT_GT(v, 0.9f) << "Drag to far right should land near max";
        }

        TEST_F(ControlsSliderIntegrationTest, ClampsToMinWhenInitialValueBelowRange)
        {
            float v = -10.0f;
            EmitSlider(v, 0.0f, 1.0f);
            EXPECT_FLOAT_EQ(v, 0.0f);
        }

        TEST_F(ControlsSliderIntegrationTest, ClampsToMaxWhenInitialValueAboveRange)
        {
            float v = 999.0f;
            EmitSlider(v, 0.0f, 1.0f);
            EXPECT_FLOAT_EQ(v, 1.0f);
        }

        TEST_F(ControlsSliderIntegrationTest, SwapsMinAndMaxIfReversed)
        {
            // SliderField swaps reversed min/max, so (1.0, 0.0) gives a [0,1] range.
            float v = 0.5f;
            app.Frame([&] {
                app.BeginRootWindow();
                app.SliderField(sliderID, v, 1.0f, 0.0f, "",
                                LayoutOverride{}.Size(300, 30));
                app.EndRootWindow();
            });
            EXPECT_GE(v, 0.0f);
            EXPECT_LE(v, 1.0f);
        }

        TEST_F(ControlsSliderIntegrationTest, NonZeroRangeMapsDragProportionally)
        {
            float v = 50.0f;
            EmitSlider(v, 0.0f, 100.0f);

            ID trackID = FindTrackID();
            const auto trackRect = app.GetElementRect(trackID);

            // Drag from the middle to ~25% of the track.
            const int dst = trackRect.x + trackRect.w / 4;
            app.DragFrom(
                { trackRect.x + trackRect.w / 2, trackRect.y + trackRect.h / 2 },
                { dst, trackRect.y + trackRect.h / 2 });
            EmitSlider(v, 0.0f, 100.0f);

            EXPECT_LT(v, 50.0f) << "Drag into the left quarter should reduce value below midpoint";
            EXPECT_GT(v, 0.0f);
        }

        // A drag that changes the value makes the call return true on some frame, with
        // IsModified() saying the same; frames without a change return false.
        TEST_F(ControlsSliderIntegrationTest, EachSliderReportsTheFrameItsValueChanged)
        {
            enum class Kind { FloatField, IntField, IntSlider };
            for (const Kind kind : { Kind::FloatField, Kind::IntField, Kind::IntSlider })
            {
                HeadlessGUIApp drag;
                ASSERT_TRUE(drag.Initialize());
                const ID id = drag.idManager.CreateStaticID();
                float f = 0.0f;
                int   i = 0;
                bool returned = false, modified = false, mismatch = false;
                auto draw = [&] {
                    drag.BeginRootWindow();
                    bool r = false;
                    switch (kind)
                    {
                        case Kind::FloatField: r = drag.SliderField(id, f, 0.0f, 1.0f, "", LayoutOverride{}.Size(300, 30)); break;
                        case Kind::IntField:   r = drag.SliderField(id, i, 0, 10, "", LayoutOverride{}.Size(300, 30));     break;
                        case Kind::IntSlider:  r = drag.Slider(id, i, 0, 10, LayoutOverride{}.Size(300, 30));               break;
                    }
                    const bool m = drag.guiStateManager.IsModified();
                    returned = returned || r;
                    modified = modified || m;
                    mismatch = mismatch || (r != m);
                    drag.EndRootWindow();
                };
                drag.Frame(draw);
                drag.Frame(draw);
                EXPECT_FALSE(returned) << "no change yet, kind " << int(kind);
                const IntRect r = drag.GetElementRect(id);
                drag.DragFrom({ r.x + 1, r.y + r.h / 2 }, { r.x + r.w - 1, r.y + r.h / 2 });
                for (int frame = 0; frame < 3; ++frame)
                    drag.Frame(draw);
                EXPECT_TRUE(f > 0.5f || i > 5) << "the drag moved it, kind " << int(kind);
                EXPECT_TRUE(returned) << "kind " << int(kind);
                EXPECT_TRUE(modified) << "kind " << int(kind);
                EXPECT_FALSE(mismatch) << "kind " << int(kind);
                drag.Cleanup();
            }
        }

        // A float slider moves smoothly: a quarter of the way along is near 0.25, not a whole number.
        TEST_F(ControlsSliderIntegrationTest, AFloatSliderIsntRoundedToWholeNumbers)
        {
            float v = 0.0f;
            auto draw = [&] {
                app.BeginRootWindow();
                app.Slider(sliderID, v, 0.0f, 1.0f, LayoutOverride{}.Size(300, 30));
                app.EndRootWindow();
            };
            app.Frame(draw);
            app.Frame(draw);
            const IntRect r = app.GetElementRect(sliderID);
            app.DragFrom({ r.x + 1, r.y + r.h / 2 }, { r.x + r.w / 4, r.y + r.h / 2 });
            app.Frame(draw);
            EXPECT_NEAR(v, 0.25f, 0.05f);
        }

        // Any integer type works: a uint8_t slider in [50, 100] reaches both ends.
        TEST_F(ControlsSliderIntegrationTest, ASmallIntegerSliderReachesBothEnds)
        {
            uint8_t v = 75;
            auto draw = [&] {
                app.BeginRootWindow();
                app.Slider(sliderID, v, uint8_t(50), uint8_t(100), LayoutOverride{}.Size(300, 30));
                app.EndRootWindow();
            };
            app.Frame(draw);
            app.Frame(draw);
            const IntRect r = app.GetElementRect(sliderID);
            app.DragFrom({ r.x + r.w / 2, r.y + r.h / 2 }, { r.x + r.w - 1, r.y + r.h / 2 });
            app.Frame(draw);
            EXPECT_EQ(v, 100);
            app.DragFrom({ r.x + r.w / 2, r.y + r.h / 2 }, { r.x + 1, r.y + r.h / 2 });
            app.Frame(draw);
            EXPECT_EQ(v, 50);
        }

        // A ranged number: a slider that takes the room, and a 56px value box after it.
        // Dragging the slider changes the value the box shows.
        TEST_F(ControlsSliderIntegrationTest, ARangeFieldIsASliderAndAValueBox)
        {
            float v = 0.5f;
            auto draw = [&] {
                app.BeginRootWindow();
                app.RangeField(sliderID, v, 0.25f, 1.0f, LayoutOverride{}.Width(Pixels{ 400 }).NoGrow());
                app.EndRootWindow();
            };
            app.Frame(draw);
            app.Frame(draw);

            IntRect slider{}, box{};
            int sliders = 0, boxes = 0;
            const ElementRef root = app.elementStorage.FindElementRefByID(sliderID);
            ASSERT_TRUE(app.elementStorage.IsValidRef(root));
            const auto children = app.elementStorage.GetChildRange(root);
            for (uint32_t idx = children.begin; idx < children.end; ++idx)
            {
                const ElementRef ref{ root.depth + 1, idx };
                const Element& e = *app.elementStorage.GetElementPtr(ref);
                const IntRect& rect = app.elementStorage.GetElementRectPtr(ref)->rect;
                if (e.styleRef == app.theme.sliderStyleRef)    { slider = rect; ++sliders; }
                if (e.styleRef == app.theme.controls->number)  { box = rect;    ++boxes; }
            }
            ASSERT_EQ(sliders, 1);
            ASSERT_EQ(boxes, 1);
            EXPECT_EQ(box.w, app.styleManager.Px(56));
            EXPECT_GT(slider.w, box.w) << "the slider takes the rest of the row";
            EXPECT_LT(slider.x + slider.w, box.x) << "the box comes after the slider";

            app.DragFrom({ slider.x + slider.w / 2, slider.y + slider.h / 2 },
                         { slider.x + slider.w - 1, slider.y + slider.h / 2 });
            app.Frame(draw);
            EXPECT_NEAR(v, 1.0f, 0.01f);
        }

    } // namespace
} // namespace tachyon
