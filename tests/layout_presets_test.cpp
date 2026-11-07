// Layout presets: named dock trees a document can be given (the app's built-in ones and the
// ones the user saved), the layout each file type opens with, and applying a tree to a layout.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_controls/gui_dock_node.h>
#include <gui_controls/gui_layout_presets.h>

#include <string>
#include <vector>

namespace
{
    using namespace tachyon;
    using GUITest::HeadlessGUIApp;

    std::vector<std::string> NamesOf(const LayoutPresets& presets)
    {
        std::vector<std::string> names;
        for (const LayoutPreset& preset : presets.All())
            names.push_back(preset.name);
        return names;
    }

    // ---- The list ----

    TEST(LayoutPresetsTest, BuiltInOnesComeFirstAndStay)
    {
        LayoutPresets presets;
        presets.AddBuiltIn("Scene", "a");
        EXPECT_TRUE(presets.Save("Mine", "m"));
        presets.AddBuiltIn("Graph", "b");
        EXPECT_EQ(NamesOf(presets), (std::vector<std::string>{ "Scene", "Graph", "Mine" }));
        EXPECT_TRUE(presets.Find("Scene")->builtIn);
        EXPECT_FALSE(presets.Find("Mine")->builtIn);

        EXPECT_FALSE(presets.Save("Scene", "x")) << "a built-in one isn't replaced";
        EXPECT_EQ(presets.Find("Scene")->tree, "a");
        EXPECT_FALSE(presets.Remove("Scene")) << "nor deleted";
        EXPECT_NE(presets.Find("Scene"), nullptr);

        presets.AddBuiltIn("Scene", "a2");   // the app's own, changed
        EXPECT_EQ(presets.Find("Scene")->tree, "a2");
        EXPECT_EQ(NamesOf(presets), (std::vector<std::string>{ "Scene", "Graph", "Mine" }));
    }

    TEST(LayoutPresetsTest, SavingANameAgainReplacesIt)
    {
        LayoutPresets presets;
        EXPECT_FALSE(presets.Save("", "t")) << "it needs a name";
        EXPECT_TRUE(presets.All().empty());
        EXPECT_TRUE(presets.Save("Mine", "one"));
        EXPECT_TRUE(presets.Save("Other", "two"));
        EXPECT_TRUE(presets.Save("Mine", "three"));
        EXPECT_EQ(NamesOf(presets), (std::vector<std::string>{ "Mine", "Other" })) << "in its place";
        EXPECT_EQ(presets.Find("Mine")->tree, "three");

        EXPECT_TRUE(presets.Remove("Mine"));
        EXPECT_EQ(presets.Find("Mine"), nullptr);
        EXPECT_FALSE(presets.Remove("Mine")) << "gone";

        presets.AddBuiltIn("Other", "built-in");   // the app's own name wins over a saved one
        ASSERT_EQ(presets.All().size(), 1u);
        EXPECT_TRUE(presets.Find("Other")->builtIn);
        EXPECT_EQ(presets.Find("Other")->tree, "built-in");
    }

    TEST(LayoutPresetsTest, AFileTypeOpensWithItsLayout)
    {
        LayoutPresets presets;
        presets.AddBuiltIn("Graph", "g");
        EXPECT_TRUE(presets.Save("Mine", "m"));
        presets.SetForFileType(".MAT", "Graph");
        presets.SetForFileType(".level", "Mine");
        EXPECT_EQ(presets.NameForFileType(".mat"), "Graph") << "any case";
        ASSERT_NE(presets.ForFileType(".Mat"), nullptr);
        EXPECT_EQ(presets.ForFileType(".mat")->tree, "g");
        EXPECT_EQ(presets.ForFileType(".anim"), nullptr);
        EXPECT_EQ(presets.NameForFileType(".anim"), "");

        presets.SetForFileType(".mat", "Mine");   // another layout
        EXPECT_EQ(presets.NameForFileType(".mat"), "Mine");
        ASSERT_EQ(presets.FileTypes().size(), 2u) << "one per type";

        presets.SetForFileType(".level", "Gone");
        EXPECT_EQ(presets.ForFileType(".level"), nullptr) << "no layout of that name";

        EXPECT_TRUE(presets.Remove("Mine"));
        EXPECT_EQ(presets.NameForFileType(".mat"), "") << "its file types go with it";
        EXPECT_EQ(presets.NameForFileType(".level"), "Gone");
        presets.SetForFileType(".level", "");
        EXPECT_TRUE(presets.FileTypes().empty());
    }

    TEST(LayoutPresetsTest, JsonKeepsTheSavedOnesAndTheFileTypes)
    {
        LayoutPresets presets;
        presets.AddBuiltIn("Graph", "g");
        EXPECT_TRUE(presets.Save("Mine", "{\"tabs\":[\"A\"],\"selected\":0}"));
        presets.SetForFileType(".mat", "Mine");
        const std::string json = presets.ToJson();
        EXPECT_EQ(json.find("Graph"), std::string::npos) << "built-in ones aren't in it";

        LayoutPresets again;
        again.AddBuiltIn("Graph", "g2");
        EXPECT_TRUE(again.Save("Stale", "s"));
        again.SetForFileType(".level", "Stale");
        ASSERT_TRUE(again.FromJson(json));
        EXPECT_EQ(NamesOf(again), (std::vector<std::string>{ "Graph", "Mine" })) << "in place of the saved ones there were";
        EXPECT_EQ(again.Find("Graph")->tree, "g2");
        EXPECT_EQ(again.Find("Mine")->tree, presets.Find("Mine")->tree);
        EXPECT_EQ(again.NameForFileType(".mat"), "Mine");
        EXPECT_EQ(again.NameForFileType(".level"), "");

        // A saved one of a built-in one's name is skipped; what can't be read changes nothing.
        ASSERT_TRUE(again.FromJson(R"({"presets":[{"name":"Graph","layout":"x"},{"layout":"no name"},7],"fileTypes":[{"layout":"Graph"},"x"]})"));
        EXPECT_EQ(NamesOf(again), (std::vector<std::string>{ "Graph" }));
        EXPECT_EQ(again.Find("Graph")->tree, "g2");
        EXPECT_TRUE(again.FileTypes().empty());
        EXPECT_TRUE(again.Save("Kept", "k"));
        EXPECT_FALSE(again.FromJson("not json"));
        EXPECT_FALSE(again.FromJson("[]"));
        EXPECT_NE(again.Find("Kept"), nullptr);
    }

    // ---- Applying a tree ----

    class ApplyDockLayoutTest : public ::testing::Test
    {
    protected:
        HeadlessGUIApp app;
        void SetUp() override { ASSERT_TRUE(app.Initialize()); }
        void TearDown() override { app.Cleanup(); }

        ID AddPanel(DockingLayout& layout, const char* title)
        {
            return app.CreatePanel(layout.panels, title, IconID::Invalid, [] {});
        }
        // A panel of that title in the layout; Invalid when there's none.
        static ID PanelNamed(const DockingLayout& layout, std::string_view title)
        {
            for (const auto& [id, panel] : layout.panels)
                if (panel.title == title) return id;
            return ID::Invalid();
        }
        static std::vector<std::string> Titles(const DockingLayout& layout, const DockNode* leaf)
        {
            std::vector<std::string> titles;
            for (const ID id : leaf->panelIds)
                titles.push_back(layout.panels.at(id).title);
            return titles;
        }
    };

    // The tree another layout was saved with: the panels it names that this one has keep
    // their place in it, the ones it hasn't are made, and the rest are closed.
    TEST_F(ApplyDockLayoutTest, ThePanelsAreLaidOutAsTheTreeSays)
    {
        DockingLayout saved;
        const ID scene = AddPanel(saved, "Scene"), details = AddPanel(saved, "Details"), log = AddPanel(saved, "Log");
        saved.root = MakeSplit(DockSplitMode::LeftPinned, 250, MakeLeaf({ scene }), MakeLeaf({ details, log }));
        saved.root->second->selectedTab = 1;
        const std::string tree = app.SaveDockLayout(saved);
        ASSERT_FALSE(tree.empty());

        DockingLayout layout;
        const ID ownLog = AddPanel(layout, "Log"), help = AddPanel(layout, "Help");
        layout.root = MakeLeaf({ ownLog, help });
        std::vector<std::string> made;
        ASSERT_TRUE(app.ApplyDockLayout(layout, tree, [&](std::string_view key) -> std::optional<ID> {
            made.emplace_back(key);
            return AddPanel(layout, std::string(key).c_str());
        }));

        EXPECT_EQ(made, (std::vector<std::string>{ "Scene", "Details" })) << "only the ones it lacked";
        ASSERT_FALSE(layout.root->isLeaf);
        EXPECT_EQ(layout.root->splitSize, CssPx(250));
        EXPECT_EQ(Titles(layout, layout.root->first.get()), (std::vector<std::string>{ "Scene" }));
        EXPECT_EQ(Titles(layout, layout.root->second.get()), (std::vector<std::string>{ "Details", "Log" }));
        EXPECT_EQ(layout.root->second->selectedTab, 1);
        EXPECT_EQ(PanelNamed(layout, "Log"), ownLog) << "its own Log, not a new one";
        EXPECT_EQ(PanelNamed(layout, "Help"), ID::Invalid()) << "closed";
        EXPECT_EQ(layout.panels.size(), 3u);
        EXPECT_EQ(app.SaveDockLayout(layout), tree) << "laid out as saved";
    }

    // Without a way to make them, the panels it lacks are left out, and the tree closes up.
    TEST_F(ApplyDockLayoutTest, PanelsItCantMakeAreLeftOut)
    {
        DockingLayout layout;
        const ID details = AddPanel(layout, "Details");
        layout.root = MakeLeaf({ details });
        const std::string tree = R"({"split":"LeftPinned","size":250,"minFirst":80,"minSecond":80,)"
                                 R"("first":{"tabs":["Scene"],"selected":0},"second":{"tabs":["Details"],"selected":0}})";
        ASSERT_TRUE(app.ApplyDockLayout(layout, tree));
        ASSERT_TRUE(layout.root->isLeaf);
        EXPECT_EQ(layout.root->panelIds, (std::vector<ID>{ details }));

        // nullopt from makePanel, or an ID it made no panel for: left out too.
        ASSERT_TRUE(app.ApplyDockLayout(layout, tree, [](std::string_view) -> std::optional<ID> { return std::nullopt; }));
        EXPECT_EQ(layout.root->panelIds, (std::vector<ID>{ details }));
        ASSERT_TRUE(app.ApplyDockLayout(layout, tree, [this](std::string_view) -> std::optional<ID> { return app.idManager.CreateStaticID(); }));
        ASSERT_TRUE(layout.root->isLeaf);
        EXPECT_EQ(layout.root->panelIds, (std::vector<ID>{ details }));
        EXPECT_EQ(layout.panels.size(), 1u);
    }

    // A tree that can't be read, or leaves no panel, changes nothing; panels made for it go.
    TEST_F(ApplyDockLayoutTest, ATreeThatCantBeUsedLeavesTheLayout)
    {
        DockingLayout layout;
        const ID details = AddPanel(layout, "Details");
        layout.root = MakeLeaf({ details });
        const DockNode* before = layout.root.get();
        int made = 0;
        auto makePanel = [&](std::string_view key) -> std::optional<ID> { ++made; return AddPanel(layout, std::string(key).c_str()); };

        EXPECT_FALSE(app.ApplyDockLayout(layout, "not a tree", makePanel));
        EXPECT_FALSE(app.ApplyDockLayout(layout, "", makePanel));
        // Read as far as its first tab group, which made a panel, then cut off.
        EXPECT_FALSE(app.ApplyDockLayout(layout, R"({"split":"LeftPinned","size":100,"minFirst":80,"minSecond":80,"first":{"tabs":["New"],"selected":0},"second":)", makePanel));
        EXPECT_EQ(made, 1) << "test setup: a panel was made before it failed";
        EXPECT_FALSE(app.ApplyDockLayout(layout, R"({"tabs":["Gone"],"selected":0})", [](std::string_view) -> std::optional<ID> { return std::nullopt; }))
            << "no panel left";

        EXPECT_EQ(layout.root.get(), before);
        EXPECT_EQ(layout.panels.size(), 1u) << "the panel made for it went";
        EXPECT_NE(PanelNamed(layout, "Details"), ID::Invalid());
    }

    // Saved UI state: the saved layouts and the file types' layouts.
    TEST_F(ApplyDockLayoutTest, SavedPresetsAndFileTypesComeBack)
    {
        LayoutPresets presets;
        presets.AddBuiltIn("Graph", "g");
        ASSERT_TRUE(presets.Save("Mine", R"({"tabs":["Scene"],"selected":0})"));
        presets.SetForFileType(".level", "Mine");
        app.PersistLayoutPresets("layouts", presets);
        const std::string saved = app.SaveUIState();

        LayoutPresets fresh;   // a new run: the app's own again
        fresh.AddBuiltIn("Graph", "g");
        app.PersistLayoutPresets("layouts", fresh);
        ASSERT_TRUE(app.RestoreUIState(saved));
        EXPECT_EQ(NamesOf(fresh), (std::vector<std::string>{ "Graph", "Mine" }));
        EXPECT_EQ(fresh.Find("Mine")->tree, presets.Find("Mine")->tree);
        EXPECT_EQ(fresh.NameForFileType(".level"), "Mine");
    }
}
