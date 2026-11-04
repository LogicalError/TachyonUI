// Saved UI state (#21): windows, dock trees, tables, toolbars and app values as JSON.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_controls/gui_dock_node.h>
#include <gui_core/gui_json.h>

#include <string>
#include <vector>

namespace
{
    using namespace tachyon;
    using GUITest::HeadlessGUIApp;

    // ---- JSON ----

    TEST(JsonTest, ReadsWhatItWrites)
    {
        JsonValue doc = JsonValue::MakeObject();
        doc.Set("name", "a \"quoted\"\nline");
        doc.Set("count", 3);
        doc.Set("ratio", 1.5);
        doc.Set("on", true);
        doc.Set("none", nullptr);
        JsonValue list = JsonValue::MakeArray();
        list.Push(1); list.Push("two");
        doc.Set("list", std::move(list));

        const Result<JsonValue> back = JsonValue::Parse(doc.Write());
        ASSERT_TRUE(back) << back.GetError().message;
        EXPECT_EQ(back->Find("name")->AsString(), "a \"quoted\"\nline");
        EXPECT_EQ(back->Find("count")->AsNumber(), 3.0);
        EXPECT_EQ(back->Find("ratio")->AsNumber(), 1.5);
        EXPECT_TRUE(back->Find("on")->AsBool());
        EXPECT_TRUE(back->Find("none")->IsNull());
        ASSERT_EQ(back->Find("list")->Items().size(), 2u);
        EXPECT_EQ(back->Find("list")->Items()[1].AsString(), "two");
        EXPECT_EQ(back->Write(), doc.Write()) << "members keep their order";
    }

    TEST(JsonTest, DecodesUnicodeEscapes)
    {
        const Result<JsonValue> v = JsonValue::Parse(R"("caf\u00e9 \ud83d\ude00")");
        ASSERT_TRUE(v);
        EXPECT_EQ(v->AsString(), "caf\xC3\xA9 \xF0\x9F\x98\x80");
    }

    TEST(JsonTest, MalformedTextIsAnError)
    {
        for (const char* text : { "", "{", "[1,]", "{\"a\" 1}", "\"open", "tru", "{} x", "\"\\ud800\"" })
        {
            const Result<JsonValue> v = JsonValue::Parse(text);
            EXPECT_FALSE(v) << text;
            if (!v) { EXPECT_EQ(v.GetError().code, ErrorCode::ParseFailed); }
        }
        EXPECT_FALSE(JsonValue::Parse(std::string(100, '[') + std::string(100, ']'))) << "too deep";
    }

    // ---- UI state ----

    class UIStateTest : public ::testing::Test
    {
    protected:
        HeadlessGUIApp app;
        void SetUp() override { ASSERT_TRUE(app.Initialize()); }
        void TearDown() override { app.Cleanup(); }
    };

    TEST_F(UIStateTest, AppValuesComeBack)
    {
        bool on = true; int count = 7; float zoom = 1.25f; std::string theme = "dark";
        app.PersistValue("on", on);
        app.PersistValue("count", count);
        app.PersistValue("zoom", zoom);
        app.PersistValue("theme", theme);
        const std::string saved = app.SaveUIState();

        on = false; count = 0; zoom = 1.0f; theme = "light";
        ASSERT_TRUE(app.RestoreUIState(saved));
        EXPECT_TRUE(on);
        EXPECT_EQ(count, 7);
        EXPECT_FLOAT_EQ(zoom, 1.25f);
        EXPECT_EQ(theme, "dark");

        // A value saved as another type is skipped.
        ASSERT_TRUE(app.RestoreUIState(R"({"values":{"count":"seven"}})"));
        EXPECT_EQ(count, 7);
    }

    TEST_F(UIStateTest, ChangedShortcutsComeBack)
    {
        auto registerCommands = [](CommandManager& m, KeyCode copyKey) {
            const CommandMode sculpt = m.RegisterMode("Sculpt");
            m.RegisterCommand(KeyCode::KeyS, KeyModifier::Control, [] {}, "Save");
            m.RegisterCommand(KeyCode::KeyZ, KeyModifier::Control, [] {}, "Undo");
            m.RegisterCommand(copyKey,       KeyModifier::Control, [] {}, "Copy");
            m.RegisterCommand(sculpt, KeyCode::KeyB, KeyModifier::None, [] {}, "Tool");
            m.RegisterCommand(sculpt, KeyCode::KeyE, KeyModifier::None, [] {}, "Tool");
            m.RegisterCommand(KeyCode::KeyV, KeyModifier::Control, [] {}, "Paste");
            m.RegisterCommand(KeyCode::Insert, KeyModifier::Shift, [] {}, "Paste");
        };
        CommandManager before;
        registerCommands(before, KeyCode::KeyC);
        before.SetShortcut(CommandManager::IDAt(1), KeyCode::KeyS, KeyModifier::Control);   // Undo takes Save's
        before.SetShortcut(CommandManager::IDAt(4), KeyCode::KeyR, KeyModifier::Shift);     // the second Tool
        before.SetShortcut(CommandManager::IDAt(5), KeyCode::KeyP, KeyModifier::Control);   // the first Paste
        app.PersistShortcuts("keys", before);
        const std::string saved = app.SaveUIState();

        // Only the ones changed are saved.
        const Result<JsonValue> doc = JsonValue::Parse(saved);
        ASSERT_TRUE(doc);
        const JsonValue* list = doc->Find("shortcuts")->Find("keys");
        ASSERT_TRUE(list && list->IsArray());
        ASSERT_EQ(list->Items().size(), 4u);
        EXPECT_EQ(list->Items()[0].Find("command")->AsString(), "Save");
        EXPECT_EQ(list->Items()[0].Find("mode")->AsString(), "Default");
        EXPECT_EQ(list->Items()[2].Find("mode")->AsString(), "Sculpt");
        EXPECT_EQ(list->Items()[0].Find("nth"), nullptr) << "the first of its name";
        ASSERT_NE(list->Items()[2].Find("nth"), nullptr);
        EXPECT_EQ(list->Items()[2].Find("nth")->AsNumber(), 1.0) << "the second Tool";
        EXPECT_EQ(list->Items()[3].Find("nth"), nullptr) << "the first Paste, with another after it";

        // A later version of the app: Copy has another key, and keeps it.
        CommandManager after;
        registerCommands(after, KeyCode::Insert);
        app.PersistShortcuts("keys", after);
        ASSERT_TRUE(app.RestoreUIState(saved));
        const auto& commands = after.GetCommands();
        EXPECT_FALSE(commands[0].HasShortcut()) << "Save";
        EXPECT_EQ(commands[1].key, KeyCode::KeyS);
        EXPECT_EQ(commands[1].modifiers, KeyModifier::Control);
        EXPECT_EQ(commands[2].key, KeyCode::Insert) << "not changed, so the app's own";
        EXPECT_EQ(commands[3].key, KeyCode::KeyB) << "the first Tool";
        EXPECT_EQ(commands[4].key, KeyCode::KeyR) << "the second Tool";
        EXPECT_EQ(commands[4].modifiers, KeyModifier::Shift);
        EXPECT_EQ(commands[4].displayString, "Shift+R");
        EXPECT_EQ(commands[5].key, KeyCode::KeyP) << "the first Paste";
        EXPECT_EQ(commands[6].key, KeyCode::Insert) << "the second Paste";
    }

    TEST_F(UIStateTest, ShortcutsItCanNotPlaceAreSkipped)
    {
        CommandManager m;
        m.RegisterCommand(KeyCode::KeyS, KeyModifier::Control, [] {}, "Save");
        app.PersistShortcuts("keys", m);
        const char* entries[] = {
            R"({"mode":"Default","command":"Gone","key":20,"modifiers":0})",
            R"({"mode":"Nowhere","command":"Save","key":20,"modifiers":0})",
            R"({"mode":"Default","command":"Save","key":-1,"modifiers":0})",
            R"({"mode":"Default","command":"Save","key":65536,"modifiers":0})",
            R"({"mode":"Default","command":"Save","key":20,"modifiers":256})",
            R"({"mode":"Default","command":"Save","key":"S","modifiers":0})",
            R"({"mode":"Default","command":"Save","modifiers":0})",
            R"({"mode":"Default","command":"Save","key":20})",
            R"({"mode":"Default","command":"Save","key":20,"modifiers":0,"nth":1})",
            R"({"mode":"Default","command":"Save","key":20,"modifiers":0,"nth":-1})",
            R"({"mode":"Default","command":"Save","key":20,"modifiers":0,"nth":"first"})",
            R"("Save")",
        };
        for (const char* entry : entries)
        {
            ASSERT_TRUE(app.RestoreUIState(std::string(R"({"shortcuts":{"keys":[)") + entry + "]}}")) << entry;
            EXPECT_FALSE(m.GetCommands()[0].IsRebound()) << entry;
        }
        ASSERT_TRUE(app.RestoreUIState(R"({"shortcuts":{"keys":{}}})"));
        EXPECT_FALSE(m.GetCommands()[0].IsRebound()) << "not a list";

        // Key 0 is none: saved for a command the user left without one.
        ASSERT_TRUE(app.RestoreUIState(R"({"shortcuts":{"keys":[{"mode":"Default","command":"Save","key":0,"modifiers":0}]}})"));
        EXPECT_FALSE(m.GetCommands()[0].HasShortcut());

        // The largest values still in range are taken.
        ASSERT_TRUE(app.RestoreUIState(R"({"shortcuts":{"keys":[{"mode":"Default","command":"Save","key":65535,"modifiers":255}]}})"));
        EXPECT_EQ(uint32_t(m.GetCommands()[0].key), 65535u);
        EXPECT_EQ(uint32_t(m.GetCommands()[0].modifiers), 255u);
    }

    TEST_F(UIStateTest, TableColumnOrderAndWidthsComeBack)
    {
        auto makeColumns = [] {
            std::vector<VirtualTableColumn> columns(3);
            columns[0].column.header = "Name";
            columns[1].column.header = "Size";
            columns[2].column.header = "Date";
            return columns;
        };
        TableState state;
        std::vector<VirtualTableColumn> columns = makeColumns();
        std::swap(columns[0], columns[2]);                    // the user moved Date first
        state.columnWidths = { 90, 60, 200 };                 // Date, Size, Name
        app.PersistTable("files", state, columns);
        const std::string saved = app.SaveUIState();

        TableState fresh;
        std::vector<VirtualTableColumn> freshColumns = makeColumns();
        freshColumns.push_back({});
        freshColumns.back().column.header = "Kind";           // added since the save
        app.PersistTable("files", fresh, freshColumns);
        ASSERT_TRUE(app.RestoreUIState(saved));
        ASSERT_EQ(freshColumns.size(), 4u);
        EXPECT_EQ(freshColumns[0].column.header, "Date");
        EXPECT_EQ(freshColumns[1].column.header, "Size");
        EXPECT_EQ(freshColumns[2].column.header, "Name");
        EXPECT_EQ(freshColumns[3].column.header, "Kind") << "a new column comes last";
        EXPECT_EQ(fresh.columnWidths, (std::vector<int32_t>{ 90, 60, 200, 120 }));
    }

    TEST_F(UIStateTest, ADockTreeComesBackByPanelKey)
    {
        auto addPanels = [](DockingLayout& layout, std::initializer_list<const char*> titles) {
            std::vector<ID> ids;
            uint32_t n = 1;
            for (const char* title : titles)
            {
                ID id = ID::Invalid();
                id.value.integer = 1000 + n++;   // stable test IDs
                layout.panels[id] = Panel{ .id = id, .title = title };
                ids.push_back(id);
            }
            return ids;
        };
        DockingLayout layout;
        const std::vector<ID> a = addPanels(layout, { "Scene", "Details", "Log" });
        layout.root = MakeSplit(DockSplitMode::LeftPinned, 250, MakeLeaf({ a[0] }), MakeLeaf({ a[1], a[2] }));
        app.PersistDocking("dock", layout);
        const std::string saved = app.SaveUIState();

        DockingLayout fresh;   // a new run: new IDs, the panels in one leaf, and a new one
        const std::vector<ID> b = addPanels(fresh, { "Log", "Details", "Scene", "Help" });
        fresh.root = MakeLeaf({ b[0], b[1], b[2], b[3] });
        app.PersistDocking("dock", fresh);
        ASSERT_TRUE(app.RestoreUIState(saved));

        ASSERT_TRUE(fresh.root);
        ASSERT_FALSE(fresh.root->isLeaf);
        EXPECT_EQ(fresh.root->splitSize, CssPx(250));
        EXPECT_EQ(fresh.root->first->panelIds, (std::vector<ID>{ b[2], b[3] })) << "Scene, then the new Help";
        EXPECT_EQ(fresh.root->second->panelIds, (std::vector<ID>{ b[1], b[0] })) << "Details, Log";
    }

    TEST_F(UIStateTest, ToolbarsComeBackOnTheirSides)
    {
        auto item = [](const char* name) { ToolbarItem i; i.debugName = name; return i; };
        std::vector<ToolbarItem> sides[kToolbarSideCount];
        sides[int(ToolbarSide::Top)]  = { item("File"), item("Edit") };
        sides[int(ToolbarSide::Left)] = { item("Tools") };
        app.PersistToolbars("tools", sides);
        const std::string saved = app.SaveUIState();

        std::vector<ToolbarItem> fresh[kToolbarSideCount];
        fresh[int(ToolbarSide::Top)] = { item("Tools"), item("Edit"), item("File"), item("View") };
        app.PersistToolbars("tools", fresh);
        ASSERT_TRUE(app.RestoreUIState(saved));
        auto names = [](const std::vector<ToolbarItem>& side) {
            std::vector<std::string> n;
            for (const ToolbarItem& i : side) n.push_back(i.debugName);
            return n;
        };
        EXPECT_EQ(names(fresh[int(ToolbarSide::Top)]), (std::vector<std::string>{ "File", "Edit", "View" }));
        EXPECT_EQ(names(fresh[int(ToolbarSide::Left)]), (std::vector<std::string>{ "Tools" }));
    }

    // A window not open yet is laid out at its saved rect (its OS window is made from that);
    // one saved off every monitor keeps only its size.
    TEST_F(UIStateTest, AWindowIsMadeAtItsSavedRect)
    {
        const ID windowID = app.idManager.CreateStaticID();
        app.PersistWindow("main", windowID);
        ASSERT_TRUE(app.RestoreUIState(R"({"windows":{"main":{"x":100,"y":50,"w":640,"h":480,"mode":"Normal"}}})"));
        app.Frame([&] {
            if (app.BeginWindow(windowID, "Main", LayoutOverride{}.Size(300, 200)))
                app.Label("inside");
            app.EndWindow();
        });
        const IntRect created = app.GetElementRect(windowID);
        EXPECT_EQ(created.w, 640);
        EXPECT_EQ(created.h, 480);
        EXPECT_EQ(created.x, 100);
        EXPECT_EQ(created.y, 50);
    }

    TEST_F(UIStateTest, AWindowSavedOffEveryMonitorKeepsOnlyItsSize)
    {
        const ID windowID = app.idManager.CreateStaticID();
        app.PersistWindow("main", windowID);
        app.Frame([&] {});   // the monitors are known from the first frame
        ASSERT_TRUE(app.RestoreUIState(R"({"windows":{"main":{"x":-90000,"y":-90000,"w":640,"h":480}}})"));
        app.Frame([&] {
            if (app.BeginWindow(windowID, "Main", LayoutOverride{}.Size(300, 200).Position(10, 20)))
                app.Label("inside");
            app.EndWindow();
        });
        const IntRect created = app.GetElementRect(windowID);
        EXPECT_EQ(created.w, 640);
        EXPECT_EQ(created.x, 10) << "the app's own position, not off-screen";
        EXPECT_EQ(created.y, 20);
    }

    // On the screen but with its top above it, where it can't be grabbed or closed: placed by
    // the app, as if off every monitor.
    TEST_F(UIStateTest, AWindowSavedWithItsTopOffTheScreenIsPlacedByTheApp)
    {
        const ID windowID = app.idManager.CreateStaticID();
        app.PersistWindow("main", windowID);
        app.Frame([&] {});
        ASSERT_TRUE(app.RestoreUIState(R"({"windows":{"main":{"x":331,"y":-77,"w":1700,"h":900}}})"));
        app.Frame([&] {
            if (app.BeginWindow(windowID, "Main", LayoutOverride{}.Size(300, 200).Position(10, 20)))
                app.Label("inside");
            app.EndWindow();
        });
        const IntRect created = app.GetElementRect(windowID);
        EXPECT_EQ(created.h, 900) << "its size is kept";
        EXPECT_EQ(created.y, 20) << "the app's own position, not with its top off the screen";
    }

    // A window saved with its monitor: on that monitor where it was; with the displays
    // rearranged, at the same place on the monitor of the same size; with that monitor gone,
    // at the same place on the primary one, inside its work area.
    class WindowMonitorTest : public UIStateTest
    {
    protected:
        ID windowID = ID::Invalid();
        void SetUp() override
        {
            UIStateTest::SetUp();
            windowID = app.idManager.CreateStaticID();
            app.PersistWindow("main", windowID);
        }
        void SetMonitors(std::vector<MonitorInfo> monitors)
        {
            ON_CALL(app.mockBackend, GetMonitors()).WillByDefault(::testing::Return(monitors));
            app.windowManager->RefreshMonitors();
        }
        static MonitorInfo Monitor(IntRect rect, bool primary)
        {
            MonitorInfo m{};
            m.rect = rect;
            m.workArea = IntRect{ rect.x, rect.y, rect.w, rect.h - 40 };   // a taskbar along the bottom
            m.isPrimary = primary;
            return m;
        }
        IntRect Restore(std::string_view json)
        {
            EXPECT_TRUE(app.RestoreUIState(json));
            app.Frame([&] {
                if (app.BeginWindow(windowID, "Main", LayoutOverride{}.Size(300, 200).Position(10, 20)))
                    app.Label("inside");
                app.EndWindow();
            });
            return app.GetElementRect(windowID);
        }
    };

    TEST_F(WindowMonitorTest, OnItsMonitorWhereItWas)
    {
        SetMonitors({ Monitor({ 0, 0, 1920, 1080 }, true), Monitor({ 1920, 0, 2560, 1440 }, false) });
        const IntRect r = Restore(R"({"windows":{"main":{"x":2100,"y":150,"w":800,"h":600,"monitor":{"x":1920,"y":0,"w":2560,"h":1440}}}})");
        EXPECT_EQ(r.x, 2100);
        EXPECT_EQ(r.y, 150);
    }

    TEST_F(WindowMonitorTest, DisplaysRearrangedGoesToTheMonitorOfTheSameSize)
    {
        // Saved with the big monitor on the left; now it's on the right.
        SetMonitors({ Monitor({ 0, 0, 1920, 1080 }, true), Monitor({ 1920, 0, 2560, 1440 }, false) });
        const IntRect r = Restore(R"({"windows":{"main":{"x":-2460,"y":150,"w":800,"h":600,"monitor":{"x":-2560,"y":0,"w":2560,"h":1440}}}})");
        EXPECT_EQ(r.x, 1920 + 100) << "the same place on it";
        EXPECT_EQ(r.y, 150);
        EXPECT_EQ(r.w, 800);
    }

    TEST_F(WindowMonitorTest, MonitorGoneGoesToThePrimaryInsideItsWorkArea)
    {
        SetMonitors({ Monitor({ 0, 0, 1920, 1080 }, true) });
        const IntRect r = Restore(R"({"windows":{"main":{"x":3500,"y":900,"w":2400,"h":600,"monitor":{"x":1920,"y":0,"w":2560,"h":1440}}}})");
        EXPECT_EQ(r.w, 1920) << "no wider than the monitor";
        EXPECT_EQ(r.x, 0);
        EXPECT_EQ(r.y, 1080 - 40 - 600) << "pulled up above the taskbar";
        EXPECT_EQ(r.h, 600);
    }

    // Saving records the monitor the window's center is on.
    TEST_F(WindowMonitorTest, SavingRecordsTheMonitor)
    {
        using ::testing::_;
        SetMonitors({ Monitor({ 0, 0, 1920, 1080 }, true), Monitor({ 1920, 0, 2560, 1440 }, false) });
        // An OS window, 800x600 at (2100, 150): on the second monitor.
        ON_CALL(app.mockBackend, CreateWindow(_, ::testing::A<IntRect>(), _, _))
            .WillByDefault([](std::string_view, IntRect, WindowFlags, OSWindowHandle) { return reinterpret_cast<OSWindowHandle>(uintptr_t(0x100)); });
        ON_CALL(app.mockBackend, IsWindow(_)).WillByDefault(::testing::Return(true));
        ON_CALL(app.mockBackend, GetWindowRect(_)).WillByDefault(::testing::Return(IntRect{ 2100, 150, 800, 600 }));
        app.Frame([&] { (void)app.windowManager->GetOrCreateWindow(windowID, "Main", IntRect{ 2100, 150, 800, 600 }, nullptr); });
        ASSERT_NE(app.windowManager->GetWindowHandle(windowID), nullptr);

        Result<JsonValue> saved = JsonValue::Parse(app.SaveUIState());
        ASSERT_TRUE(saved);
        const JsonValue* window = saved->Find("windows") ? saved->Find("windows")->Find("main") : nullptr;
        ASSERT_NE(window, nullptr);
        const JsonValue* monitor = window->Find("monitor");
        ASSERT_NE(monitor, nullptr);
        EXPECT_EQ(monitor->Find("x")->AsNumber(), 1920);
        EXPECT_EQ(monitor->Find("w")->AsNumber(), 2560);
        EXPECT_EQ(monitor->Find("h")->AsNumber(), 1440);
    }

    // ---- Documents ----

    // A document with panels titled `titles`, all in one leaf.
    Document& AddDocument(HeadlessGUIApp& app, DocumentSet& set, std::string name, std::string extension, std::string kind,
                          std::initializer_list<const char*> titles)
    {
        Document& document = app.CreateDocument(set, std::move(name), std::move(extension), std::move(kind), IconID::Invalid);
        std::vector<ID> ids;
        for (const char* title : titles)
            ids.push_back(app.CreatePanel(document.layout.panels, title, IconID::Invalid, [] {}));
        document.layout.root = MakeLeaf({});
        document.layout.root->panelIds = std::move(ids);
        return document;
    }

    std::vector<std::string> TitlesOf(const Document& document, const DockNode* leaf)
    {
        std::vector<std::string> titles;
        for (const ID id : leaf->panelIds)
            titles.push_back(document.layout.panels.at(id).title);
        return titles;
    }

    // Each main window's documents come back: which, in what order, the active one, each
    // one's panel layout; the app makes them again.
    TEST_F(UIStateTest, DocumentsComeBackPerWindow)
    {
        DocumentSet first, second;
        Document& forest = AddDocument(app, first, "Forest", ".level", "level", { "Scene", "Details", "Log" });
        forest.layout.root = MakeSplit(DockSplitMode::LeftPinned, 250, MakeLeaf({ forest.layout.root->panelIds[0] }),
                                       MakeLeaf({ forest.layout.root->panelIds[1], forest.layout.root->panelIds[2] }));
        AddDocument(app, first, "Terrain", ".mat", "material", { "Graph" });
        AddDocument(app, second, "Hud", ".layout", "layout", { "Canvas" });
        first.active = 1;
        const ID firstWindow = app.idManager.CreateStaticID(), secondWindow = app.idManager.CreateStaticID();
        app.PersistDocuments("documents", [&] {
            return std::vector<DocumentWindowRef>{ { firstWindow, &first }, { secondWindow, &second } };
        });
        const std::string saved = app.SaveUIState();

        ASSERT_TRUE(app.RestoreUIState(saved));
        std::vector<SavedDocumentWindow> windows = app.TakeSavedDocuments("documents");
        EXPECT_TRUE(app.TakeSavedDocuments("documents").empty()) << "taken once";
        ASSERT_EQ(windows.size(), 2u);
        ASSERT_EQ(windows[0].documents.size(), 2u);
        EXPECT_EQ(windows[0].documents[0].name, "Forest");
        EXPECT_EQ(windows[0].documents[0].extension, ".level");
        EXPECT_EQ(windows[0].documents[0].kind, "level");
        EXPECT_EQ(windows[0].documents[1].name, "Terrain");
        EXPECT_EQ(windows[0].active, 1);
        ASSERT_EQ(windows[1].documents.size(), 1u);
        EXPECT_EQ(windows[1].documents[0].name, "Hud");
        EXPECT_EQ(windows[1].rect, IntRect{}) << "not open: no place kept";

        // A new run: the document made again, its panels in one leaf, a new one among them.
        DocumentSet fresh;
        Document& again = AddDocument(app, fresh, "Forest", ".level", "level", { "Log", "Details", "Scene", "Help" });
        ASSERT_TRUE(app.RestoreDocumentLayout(again, windows[0].documents[0]));
        ASSERT_FALSE(again.layout.root->isLeaf);
        EXPECT_EQ(again.layout.root->splitSize, CssPx(250));
        EXPECT_EQ(TitlesOf(again, again.layout.root->first.get()), (std::vector<std::string>{ "Scene", "Help" })) << "Scene, then the new Help";
        EXPECT_EQ(TitlesOf(again, again.layout.root->second.get()), (std::vector<std::string>{ "Details", "Log" }));
    }

    TEST_F(UIStateTest, WhatCantBeReadOfSavedDocumentsIsSkipped)
    {
        const ID windowID = app.idManager.CreateStaticID();
        DocumentSet set;
        app.PersistDocuments("documents", [&] { return std::vector<DocumentWindowRef>{ { windowID, &set } }; });
        ASSERT_TRUE(app.RestoreUIState(R"({"documents":{"documents":[
            {"active":9,"documents":[{"name":"Forest","kind":"level","layout":"not a tree"},{"kind":"level"},{"name":"Desert"}]},
            {"active":0,"documents":[]},
            "not a window"
        ]}})"));
        std::vector<SavedDocumentWindow> windows = app.TakeSavedDocuments("documents");
        ASSERT_EQ(windows.size(), 1u) << "a window without documents is left out";
        ASSERT_EQ(windows[0].documents.size(), 2u) << "a document without a name is left out";
        EXPECT_EQ(windows[0].documents[1].name, "Desert");
        EXPECT_EQ(windows[0].documents[1].extension, "");
        EXPECT_EQ(windows[0].active, 1) << "the last one, as there's no tenth";

        Document& forest = AddDocument(app, set, "Forest", ".level", "level", { "Scene" });
        const DockNode* before = forest.layout.root.get();
        EXPECT_FALSE(app.RestoreDocumentLayout(forest, windows[0].documents[0]));
        EXPECT_EQ(forest.layout.root.get(), before) << "its own layout stays";
        EXPECT_FALSE(app.RestoreDocumentLayout(forest, windows[0].documents[1])) << "none saved";
        EXPECT_TRUE(app.TakeSavedDocuments("other").empty());
    }

    // A document window's place is kept, and comes back like a persisted window's.
    TEST_F(WindowMonitorTest, ADocumentWindowsPlaceComesBack)
    {
        using ::testing::_;
        SetMonitors({ Monitor({ 0, 0, 1920, 1080 }, true), Monitor({ 1920, 0, 2560, 1440 }, false) });
        ON_CALL(app.mockBackend, CreateWindow(_, ::testing::A<IntRect>(), _, _))
            .WillByDefault([](std::string_view, IntRect, WindowFlags, OSWindowHandle) { return reinterpret_cast<OSWindowHandle>(uintptr_t(0x100)); });
        ON_CALL(app.mockBackend, IsWindow(_)).WillByDefault(::testing::Return(true));
        ON_CALL(app.mockBackend, GetWindowRect(_)).WillByDefault(::testing::Return(IntRect{ 2100, 150, 800, 600 }));
        const ID documentWindow = app.idManager.CreateStaticID();
        app.Frame([&] { (void)app.windowManager->GetOrCreateWindow(documentWindow, "Docs", IntRect{ 2100, 150, 800, 600 }, nullptr); });
        DocumentSet set;
        AddDocument(app, set, "Forest", ".level", "level", { "Scene" });
        app.PersistDocuments("documents", [&] { return std::vector<DocumentWindowRef>{ { documentWindow, &set } }; });
        const std::string saved = app.SaveUIState();

        ASSERT_TRUE(app.RestoreUIState(saved));
        std::vector<SavedDocumentWindow> windows = app.TakeSavedDocuments("documents");
        ASSERT_EQ(windows.size(), 1u);
        EXPECT_EQ(windows[0].rect, (IntRect{ 2100, 150, 800, 600 }));
        EXPECT_EQ(windows[0].monitor, (IntRect{ 1920, 0, 2560, 1440 }));

        // Made again in a new run, at a new ID: where it was.
        const ID again = app.idManager.CreateStaticID();
        app.RestoreWindowPlace(again, windows[0]);
        app.Frame([&] {
            if (app.BeginWindow(again, "Docs", LayoutOverride{}.Size(300, 200).Position(10, 20)))
                app.Label("inside");
            app.EndWindow();
        });
        const IntRect r = app.GetElementRect(again);
        EXPECT_EQ(r.x, 2100);
        EXPECT_EQ(r.y, 150);
        EXPECT_EQ(r.w, 800);

        // Saved with the big monitor on the left, now on the right: the same place on it.
        SavedDocumentWindow moved = windows[0];
        moved.rect    = IntRect{ -2460, 150, 800, 600 };
        moved.monitor = IntRect{ -2560, 0, 2560, 1440 };
        const ID rearranged = app.idManager.CreateStaticID();
        app.RestoreWindowPlace(rearranged, moved);
        app.Frame([&] {
            if (app.BeginWindow(rearranged, "Docs", LayoutOverride{}.Size(300, 200).Position(10, 20)))
                app.Label("inside");
            app.EndWindow();
        });
        EXPECT_EQ(app.GetElementRect(rearranged).x, 1920 + 100);

        // No place saved: the app's own.
        const ID unplaced = app.idManager.CreateStaticID();
        app.RestoreWindowPlace(unplaced, SavedDocumentWindow{});
        app.Frame([&] {
            if (app.BeginWindow(unplaced, "Docs", LayoutOverride{}.Size(300, 200).Position(10, 20)))
                app.Label("inside");
            app.EndWindow();
        });
        EXPECT_EQ(app.GetElementRect(unplaced).x, 10);
    }

    TEST_F(UIStateTest, MalformedOrNewerStateIsAnError)
    {
        EXPECT_FALSE(app.RestoreUIState("{ not json"));
        const Result<void> newer = app.RestoreUIState(R"({"version":99})");
        ASSERT_FALSE(newer);
        EXPECT_EQ(newer.GetError().code, ErrorCode::Unsupported);
        EXPECT_TRUE(app.RestoreUIState(R"({"version":1,"unknown":{"x":1}})")) << "unknown sections are skipped";
    }
}
