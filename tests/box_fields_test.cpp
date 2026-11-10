// EdgesField, CornersField and ShadowField: their parts, linking the four sides, typing into
// them, and the ranges they keep to.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class BoxFieldsTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            ID fieldID {};

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                fieldID = app.idManager.CreateStaticID();
            }
            void TearDown() override { app.Cleanup(); }

            // The field's children, in order.
            std::vector<const Element*> Children(ID parent)
            {
                std::vector<const Element*> children;
                const ElementRef ref = app.elementStorage.FindElementRefByID(parent);
                if (!ref.IsValid())
                    return children;
                const auto range = app.elementStorage.GetChildRange(ref);
                for (uint32_t i = range.begin; i < range.end; ++i)
                    if (const Element* element = app.elementStorage.GetElementPtr(ElementRef{ ref.depth + 1, i }))
                        children.push_back(element);
                return children;
            }

            // The children with `style`, in order.
            std::vector<ID> ChildrenWithStyle(ID parent, StyleRef style)
            {
                std::vector<ID> ids;
                for (const Element* element : Children(parent))
                    if (element->styleRef == style)
                        ids.push_back(element->id);
                return ids;
            }

            template<typename F>
            bool Emit(F&& field)
            {
                bool changed = false;
                app.Frame([&] {
                    app.BeginRootWindow({ 800, 600 });
                    changed = field();
                    app.EndRootWindow();
                });
                return changed;
            }

            // Types `text` after the field's own, so a field showing 0 reads it as typed.
            template<typename F>
            void TypeInto(ID id, std::string_view text, F&& field)
            {
                app.ClickAt(app.CenterOf(id));
                Emit(field);
                app.InjectKey(KeyCode::End);
                Emit(field);
                app.InjectText(text);
                Emit(field);
                Emit(field);
            }
        };

        // ---------------------------------------------------------------------
        // EdgesField and CornersField
        // ---------------------------------------------------------------------

        // Four equal sides start linked: the toggle and one field. Four others: the toggle, and
        // a caption and a field each.
        TEST_F(BoxFieldsTest, EqualSidesStartLinked)
        {
            BoxEdges equal(2);
            Emit([&] { return app.EdgesField(fieldID, equal); });
            auto children = Children(fieldID);
            ASSERT_EQ(children.size(), 2u);
            EXPECT_EQ(children[0]->styleRef, app.theme.boxSidesLinkStyleRef);
            EXPECT_EQ(children[1]->styleRef, app.theme.controls->number);
            EXPECT_TRUE(app.HasElement(children[0]->id));

            const ID otherID = app.idManager.CreateStaticID();
            BoxEdges mixed(1, 2, 3, 4);
            Emit([&] { return app.EdgesField(otherID, mixed); });
            children = Children(otherID);
            ASSERT_EQ(children.size(), 9u);
            EXPECT_EQ(ChildrenWithStyle(otherID, app.theme.axisLabelStyleRefs[3]).size(), 4u);
            EXPECT_EQ(ChildrenWithStyle(otherID, app.theme.vectorComponentStyleRef).size(), 4u);
            EXPECT_EQ(app.TextOf(children[1]->id), "T");
            EXPECT_EQ(app.TextOf(children[7]->id), "L");
        }

        // Linked, one number sets all four.
        TEST_F(BoxFieldsTest, LinkedOneNumberSetsAllFour)
        {
            BoxEdges value(0);
            auto field = [&] { return app.EdgesField(fieldID, value); };
            Emit(field);
            const ID all = Children(fieldID)[1]->id;
            TypeInto(all, "7", field);
            EXPECT_EQ(value.top, 7);
            EXPECT_EQ(value.right, 7);
            EXPECT_EQ(value.bottom, 7);
            EXPECT_EQ(value.left, 7);
        }

        // Unlinked, each its own; linking again makes the others the first's.
        TEST_F(BoxFieldsTest, UnlinkAndLinkAgain)
        {
            BoxEdges value(0);
            auto field = [&] { return app.EdgesField(fieldID, value); };
            Emit(field);
            app.ClickAt(app.CenterOf(Children(fieldID)[0]->id));
            Emit(field);
            Emit(field);
            std::vector<ID> numbers = ChildrenWithStyle(fieldID, app.theme.vectorComponentStyleRef);
            ASSERT_EQ(numbers.size(), 4u) << "each side its own field";

            TypeInto(numbers[1], "5", field);
            EXPECT_EQ(value.top, 0);
            EXPECT_EQ(value.right, 5);
            EXPECT_EQ(value.bottom, 0);

            app.ClickAt(app.CenterOf(Children(fieldID)[0]->id));
            Emit(field);
            bool changed = Emit(field);
            EXPECT_TRUE(changed) << "linking changed the value";
            EXPECT_EQ(value.right, 0) << "the others take the top's";
            EXPECT_EQ(Children(fieldID).size(), 2u);
        }

        // The corners go top left, top right, bottom right, bottom left.
        TEST_F(BoxFieldsTest, CornersInTheirOrder)
        {
            BoxCorners value(1, 2, 0, 4);
            auto field = [&] { return app.CornersField(fieldID, value); };
            Emit(field);
            const auto children = Children(fieldID);
            ASSERT_EQ(children.size(), 9u);
            EXPECT_EQ(app.TextOf(children[1]->id), "TL");
            EXPECT_EQ(app.TextOf(children[3]->id), "TR");
            EXPECT_EQ(app.TextOf(children[5]->id), "BR");
            EXPECT_EQ(app.TextOf(children[7]->id), "BL");

            TypeInto(children[6]->id, "9", field);
            EXPECT_EQ(value.bottomRight, 9);
            EXPECT_EQ(value.topLeft, 1);
            EXPECT_EQ(value.bottomLeft, 4);
        }

        // A number past the range is held to it.
        TEST_F(BoxFieldsTest, HeldToTheRange)
        {
            BoxEdges value(0);
            auto field = [&] {
                return app.EdgesField(fieldID, value, NumericFieldConfig<int32_t>{ .stepPerPixel = 1, .minVal = 0, .maxVal = 8 });
            };
            Emit(field);
            TypeInto(Children(fieldID)[1]->id, "40", field);
            EXPECT_EQ(value.top, 8);
            EXPECT_EQ(value.left, 8);
        }

        // ---------------------------------------------------------------------
        // ShadowField
        // ---------------------------------------------------------------------

        // A color, then the offsets, blur and spread.
        TEST_F(BoxFieldsTest, AShadowsParts)
        {
            BoxShadow value{ .x = 0, .y = 4, .blur = 0, .spread = 0, .color = { 0, 0, 0, 90 } };
            auto field = [&] { return app.ShadowField(fieldID, value); };
            Emit(field);
            const auto children = Children(fieldID);
            ASSERT_EQ(children.size(), 2u) << "the color, and the sizes";
            const ID sizes = children[1]->id;
            const std::vector<ID> numbers = ChildrenWithStyle(sizes, app.theme.vectorComponentStyleRef);
            ASSERT_EQ(numbers.size(), 4u);
            const auto captions = ChildrenWithStyle(sizes, app.theme.axisLabelStyleRefs[3]);
            ASSERT_EQ(captions.size(), 4u);
            EXPECT_EQ(app.TextOf(captions[0]), "X");
            EXPECT_EQ(app.TextOf(captions[2]), "Blur");

            TypeInto(numbers[0], "6", field);
            EXPECT_EQ(value.x, 6);
            TypeInto(numbers[2], "20", field);
            EXPECT_EQ(value.blur, 20);
            TypeInto(numbers[3], "3", field);
            EXPECT_EQ(value.spread, 3);
            EXPECT_EQ(value.y, 4) << "the others stay";
            EXPECT_EQ(value.color.a, 90);
        }

        // The sizes are held to 256 either way; the offsets and spread may be negative, the blur not.
        TEST_F(BoxFieldsTest, AShadowsSizesAreHeld)
        {
            BoxShadow value{ .x = -300, .y = 300, .blur = 400, .spread = -5, .color = { 0, 0, 0, 90 } };
            auto field = [&] { return app.ShadowField(fieldID, value); };
            Emit(field);
            EXPECT_EQ(value.x, -256);
            EXPECT_EQ(value.y, 256);
            EXPECT_EQ(value.blur, 256);
            EXPECT_EQ(value.spread, -5);

            value.y = 0;
            Emit(field);
            const std::vector<ID> numbers = ChildrenWithStyle(Children(fieldID)[1]->id, app.theme.vectorComponentStyleRef);
            TypeInto(numbers[1], "999", field);
            EXPECT_EQ(value.y, 256) << "typed past it";
        }
    }
}
