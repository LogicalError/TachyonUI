// Saved UI state (#21): the windows, dock trees, tables, toolbars and app values an app
// names, as JSON the host stores.

#include <gui_controls/gui_application.h>
#include <gui_controls/controls_toolbar_dock.h>
#include <gui_core/gui_json.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace tachyon
{
    namespace
    {
        constexpr int kStateVersion = 1;
        constexpr const char* kSideNames[kToolbarSideCount] = { "Top", "Right", "Bottom", "Left" };

        const char* ModeName(WindowMode mode)
        {
            switch (mode)
            {
                case WindowMode::Maximized:           return "Maximized";
                case WindowMode::Fullscreen:          return "Fullscreen";
                case WindowMode::ExclusiveFullscreen: return "ExclusiveFullscreen";
                default:                              return "Normal";   // a minimized window comes back normal
            }
        }

        WindowMode ModeFromName(std::string_view name)
        {
            if (name == "Maximized")           return WindowMode::Maximized;
            if (name == "Fullscreen")          return WindowMode::Fullscreen;
            if (name == "ExclusiveFullscreen") return WindowMode::ExclusiveFullscreen;
            return WindowMode::Normal;
        }

        // A monitor's whole area, or its work area where the backend gives no other.
        IntRect MonitorBounds(const MonitorInfo& monitor)
        {
            return monitor.rect.w > 0 && monitor.rect.h > 0 ? monitor.rect : monitor.workArea;
        }

        // A panel's key: its type when it has one, else its title.
        std::string PanelKey(const Panel& panel) { return panel.panelType.empty() ? panel.title : panel.panelType; }

        // The layout's panel for a saved tab of `key`: the first of that key not handed out
        // yet, so a tree with two of a kind gets two panels; else the one `makePanel` makes
        // (it adds it to the layout), recorded in `made`. Nullopt when there's neither.
        std::optional<ID> PanelForKey(DockingLayout& layout, std::string_view key, std::vector<ID>& handed,
                                      const std::function<std::optional<ID>(std::string_view)>& makePanel, std::vector<ID>& made)
        {
            for (const auto& [id, panel] : layout.panels)
                if (PanelKey(panel) == key && std::find(handed.begin(), handed.end(), id) == handed.end())
                {
                    handed.push_back(id);
                    return id;
                }
            if (!makePanel)
                return std::nullopt;
            const std::optional<ID> created = makePanel(key);
            if (!created || layout.panels.find(*created) == layout.panels.end())
                return std::nullopt;   // it made none
            made.push_back(*created);
            handed.push_back(*created);
            return created;
        }

        DockNode* FirstLeaf(DockNode* node)
        {
            while (node != nullptr && !node->isLeaf)
                node = node->first ? node->first.get() : node->second.get();
            return node;
        }

        template<class Entry, class Target>
        void Upsert(std::vector<Entry>& entries, std::string key, Target target)
        {
            for (Entry& entry : entries)
                if (entry.key == key) { entry.target = target; return; }
            entries.push_back(Entry{ std::move(key), target });
        }

        // Each side's items by debugName: { "Top": ["File", "View"], "Right": [], ... }.
        JsonValue ToolbarSidesJson(std::span<const std::vector<ToolbarItem>, kToolbarSideCount> sides)
        {
            JsonValue bySide = JsonValue::MakeObject();
            for (int s = 0; s < kToolbarSideCount; ++s)
            {
                JsonValue names = JsonValue::MakeArray();
                for (const ToolbarItem& item : sides[size_t(s)])
                    names.Push(item.debugName);
                bySide.Set(kSideNames[s], std::move(names));
            }
            return bySide;
        }

        // Each item `saved` names on its side and place; the rest stay on their own sides, after.
        void PlaceToolbarItems(std::span<std::vector<ToolbarItem>, kToolbarSideCount> sides, const JsonValue& saved)
        {
            std::vector<ToolbarItem> pool;
            std::vector<int>         poolSide;
            for (int s = 0; s < kToolbarSideCount; ++s)
                for (ToolbarItem& item : sides[size_t(s)]) { pool.push_back(std::move(item)); poolSide.push_back(s); }
            std::vector<bool> placed(pool.size(), false);
            std::vector<ToolbarItem> rebuilt[kToolbarSideCount];
            for (int s = 0; s < kToolbarSideCount; ++s)
                if (const JsonValue* names = saved.Find(kSideNames[s]))
                    for (const JsonValue& name : names->Items())
                        for (size_t i = 0; i < pool.size(); ++i)
                            if (!placed[i] && pool[i].debugName == name.AsString())
                            {
                                placed[i] = true;
                                rebuilt[s].push_back(std::move(pool[i]));
                                break;
                            }
            for (size_t i = 0; i < pool.size(); ++i)
                if (!placed[i]) rebuilt[poolSide[i]].push_back(std::move(pool[i]));
            for (int s = 0; s < kToolbarSideCount; ++s)
                sides[size_t(s)] = std::move(rebuilt[s]);
        }

        // A layout's dock tree as JSON, panels by key; null when it has none.
        std::optional<JsonValue> DockTreeJson(const DockingLayout& layout)
        {
            if (!layout.root)
                return std::nullopt;
            const std::string tree = SerializeDockTree(*layout.root, [&layout](ID id) {
                const auto it = layout.panels.find(id);
                return it != layout.panels.end() ? PanelKey(it->second) : std::string();
            });
            Result<JsonValue> parsed = JsonValue::Parse(tree);
            return parsed ? std::optional<JsonValue>(std::move(*parsed)) : std::nullopt;
        }

        // A saved dock tree applied to a layout: panels by key, a panel `makePanel` makes for
        // each it lacks, the ones it hasn't to its first leaf. False when it can't be read; the
        // layout stays. The old tree's node and tab-bar IDs go back to `idManager`.
        bool ApplyDockTree(DockingLayout& layout, std::string_view tree, IDManager& idManager,
                           const std::function<std::optional<ID>(std::string_view)>& makePanel = {})
        {
            std::vector<ID> handed, made;
            std::unique_ptr<DockNode> root = ParseDockTree(tree, [&](std::string_view panelKey) {
                return PanelForKey(layout, panelKey, handed, makePanel, made);
            });
            if (!root)
            {
                // Never shown: only their IDs, when CreatePanel made them, to give back.
                for (const ID id : made)
                    if (const auto it = layout.panels.find(id); it != layout.panels.end())
                    {
                        if (it->second.internal.ownsID)
                            idManager.ReleaseStaticID(id);
                        layout.panels.erase(it);
                    }
                return false;
            }
            std::vector<ID> all;
            for (const auto& [id, panel] : layout.panels) all.push_back(id);
            if (DockNode* leaf = FirstLeaf(root.get()))
                for (const ID id : UnplacedPanels(*root, all))
                    leaf->panelIds.push_back(id);
            ReleaseDockTreeIDs(layout.root.get(), &idManager);
            layout.root = std::move(root);
            return true;
        }

        std::string StringOf(const JsonValue& object, const char* name)
        {
            const JsonValue* v = object.Find(name);
            return v && v->IsString() ? v->AsString() : std::string();
        }

        // A layout's panels' own view states, by panel key: { "NodeView": "..." }. Several
        // panels of one key go in an array, in the order they're in the tree, "" for one that
        // keeps nothing: { "Timeline": ["...", "..."] }. Null when none keeps one.
        std::optional<JsonValue> PanelStatesJson(const DockingLayout& layout)
        {
            if (!layout.root)
                return std::nullopt;
            std::vector<std::pair<std::string, std::vector<std::string>>> byKey;
            bool any = false;
            for (const ID id : PanelsInTree(*layout.root))
            {
                const auto it = layout.panels.find(id);
                if (it == layout.panels.end())
                    continue;
                const Panel& panel = it->second;
                std::string state = panel.saveState ? panel.saveState() : std::string();
                any |= !state.empty();
                const std::string key = PanelKey(panel);
                auto entry = std::find_if(byKey.begin(), byKey.end(), [&key](const auto& e) { return e.first == key; });
                if (entry == byKey.end())
                    entry = byKey.insert(byKey.end(), { key, {} });
                entry->second.push_back(std::move(state));
            }
            if (!any)
                return std::nullopt;
            JsonValue states = JsonValue::MakeObject();
            for (auto& [key, list] : byKey)
            {
                if (std::all_of(list.begin(), list.end(), [](const std::string& s) { return s.empty(); }))
                    continue;
                if (list.size() == 1)
                {
                    states.Set(key, std::move(list[0]));
                    continue;
                }
                JsonValue array = JsonValue::MakeArray();
                for (std::string& state : list)
                    array.Push(std::move(state));
                states.Set(key, std::move(array));
            }
            return states;
        }

        // Saved view states handed to the layout's panels of the same keys: the n-th saved
        // for a key to the n-th panel of that key in the tree.
        void ApplyPanelStates(DockingLayout& layout, std::span<const std::pair<std::string, std::string>> states)
        {
            if (!layout.root)
                return;
            std::vector<std::pair<std::string, size_t>> taken;   // per key, how many are handed out
            for (const ID id : PanelsInTree(*layout.root))
            {
                const auto it = layout.panels.find(id);
                if (it == layout.panels.end())
                    continue;
                Panel& panel = it->second;
                const std::string key = PanelKey(panel);
                auto count = std::find_if(taken.begin(), taken.end(), [&key](const auto& e) { return e.first == key; });
                if (count == taken.end())
                    count = taken.insert(taken.end(), { key, 0 });
                const size_t n = count->second++;
                size_t seen = 0;
                for (const auto& [savedKey, state] : states)
                    if (savedKey == key && seen++ == n)
                    {
                        if (panel.restoreState && !state.empty())
                            panel.restoreState(state);
                        break;
                    }
            }
        }

        // The saved states as pairs, a key's array as one pair each, in order.
        std::vector<std::pair<std::string, std::string>> PanelStatesOf(const JsonValue* states)
        {
            std::vector<std::pair<std::string, std::string>> out;
            if (states && states->IsObject())
                for (const auto& [key, state] : states->Members())
                {
                    if (state.IsString())
                        out.emplace_back(key, state.AsString());
                    else if (state.IsArray())
                        for (const JsonValue& one : state.Items())
                            out.emplace_back(key, one.IsString() ? one.AsString() : std::string());
                }
            return out;
        }
    }

    std::string SaveToolbarLayout(std::span<const std::vector<ToolbarItem>, kToolbarSideCount> sides)
    {
        return ToolbarSidesJson(sides).Write();
    }

    bool ApplyToolbarLayout(std::span<std::vector<ToolbarItem>, kToolbarSideCount> sides, std::string_view text)
    {
        const Result<JsonValue> saved = JsonValue::Parse(text);
        if (!saved || !saved->IsObject())
            return false;
        PlaceToolbarItems(sides, *saved);
        return true;
    }

    void GUIApplication::PersistWindow(std::string key, ID windowID)                    { Upsert(m_persistWindows, std::move(key), windowID); }
    void GUIApplication::PersistDocking(std::string key, DockingLayout& layout)          { Upsert(m_persistDocks, std::move(key), &layout); }
    void GUIApplication::PersistToolbars(std::string key, std::span<std::vector<ToolbarItem>, kToolbarSideCount> sides)
                                                                                         { Upsert(m_persistToolbars, std::move(key), sides); }
    void GUIApplication::PersistValue(std::string key, bool& value)                      { Upsert(m_persistValues, std::move(key), PersistedValue::Target(&value)); }
    void GUIApplication::PersistValue(std::string key, int& value)                       { Upsert(m_persistValues, std::move(key), PersistedValue::Target(&value)); }
    void GUIApplication::PersistValue(std::string key, float& value)                     { Upsert(m_persistValues, std::move(key), PersistedValue::Target(&value)); }
    void GUIApplication::PersistValue(std::string key, std::string& value)               { Upsert(m_persistValues, std::move(key), PersistedValue::Target(&value)); }
    void GUIApplication::PersistTable(std::string key, TableState& state, std::span<VirtualTableColumn> columns)
    {
        Upsert(m_persistTables, std::move(key), PersistedTable::Target{ &state, columns });
    }
    void GUIApplication::PersistDocuments(std::string key, std::function<std::vector<DocumentWindowRef>()> windows)
    {
        Upsert(m_persistDocuments, std::move(key), std::move(windows));
    }
    void GUIApplication::PersistShortcuts(std::string key, CommandManager& commands)     { Upsert(m_persistShortcuts, std::move(key), &commands); }
    void GUIApplication::PersistLayoutPresets(std::string key, LayoutPresets& presets)   { Upsert(m_persistPresets, std::move(key), &presets); }
    void GUIApplication::PersistThemeVariables(std::string key)
    {
        if (std::find(m_persistThemeVariables.begin(), m_persistThemeVariables.end(), key) == m_persistThemeVariables.end())
            m_persistThemeVariables.push_back(std::move(key));
    }

    std::string GUIApplication::SaveDockLayout(const DockingLayout& layout) const
    {
        const std::optional<JsonValue> tree = DockTreeJson(layout);
        return tree ? tree->Write() : std::string();
    }

    bool GUIApplication::ApplyDockLayout(DockingLayout& layout, std::string_view tree,
                                         const std::function<std::optional<ID>(std::string_view key)>& makePanel)
    {
        // Each key to a panel: the layout's own, else a new one.
        std::vector<ID> handed, made;
        std::unique_ptr<DockNode> root = ParseDockTree(tree, [&](std::string_view panelKey) {
            return PanelForKey(layout, panelKey, handed, makePanel, made);
        });
        if (!root)
        {
            for (const ID id : made)
                ClosePanel(layout, id);
            return false;
        }

        DisableAllPanels(layout);
        std::vector<ID> all;
        for (const auto& [id, panel] : layout.panels) all.push_back(id);
        for (const ID id : UnplacedPanels(*root, all))
            ClosePanel(layout, id);
        ReleaseDockTreeIDs(layout.root.get(), &idManager);
        layout.root = std::move(root);
        return true;
    }

    std::vector<SavedDocumentWindow> GUIApplication::TakeSavedDocuments(std::string_view key)
    {
        const auto it = m_savedDocuments.find(std::string(key));
        if (it == m_savedDocuments.end())
            return {};
        std::vector<SavedDocumentWindow> windows = std::move(it->second);
        m_savedDocuments.erase(it);
        return windows;
    }

    bool GUIApplication::RestoreDocumentLayout(Document& document, const SavedDocument& saved,
                                               const std::function<std::optional<ID>(std::string_view key)>& makePanel)
    {
        if (saved.layout.empty() || !ApplyDockTree(document.layout, saved.layout, idManager, makePanel))
            return false;
        ApplyPanelStates(document.layout, saved.panelStates);
        return true;
    }

    void GUIApplication::RestoreWindowPlace(ID windowID, const SavedDocumentWindow& saved)
    {
        if (saved.rect.w <= 0 || saved.rect.h <= 0)
            return;
        RestoredWindow restored;
        restored.rect       = saved.rect;
        restored.mode       = saved.mode;
        restored.monitor    = saved.monitor;
        restored.hasMonitor = saved.monitor.w > 0 && saved.monitor.h > 0;
        m_restoredWindows[windowID] = restored;
    }

    std::string GUIApplication::SaveUIState() const
    {
        JsonValue doc = JsonValue::MakeObject();
        doc.Set("version", kStateVersion);

        // A window's place; null when it isn't open (its last place isn't known here).
        auto windowJson = [this](ID windowID) -> std::optional<JsonValue>
        {
            if (!windowManager || windowManager->GetWindowHandle(windowID) == nullptr)
                return std::nullopt;
            const IntRect rect = windowManager->GetWindowRect(windowID);
            JsonValue w = JsonValue::MakeObject();
            w.Set("x", rect.x); w.Set("y", rect.y); w.Set("w", rect.w); w.Set("h", rect.h);
            w.Set("mode", ModeName(windowManager->GetWindowMode(windowID)));
            // The monitor it's on (its center's), so it can come back there, or to the same
            // place on another monitor when that one is gone.
            const int2 center{ rect.x + rect.w / 2, rect.y + rect.h / 2 };
            for (const MonitorInfo& monitor : windowManager->Monitors())
            {
                const IntRect bounds = MonitorBounds(monitor);
                if (bounds.Contains(center))
                {
                    JsonValue m = JsonValue::MakeObject();
                    m.Set("x", bounds.x); m.Set("y", bounds.y); m.Set("w", bounds.w); m.Set("h", bounds.h);
                    w.Set("monitor", std::move(m));
                    break;
                }
            }
            return w;
        };

        JsonValue windows = JsonValue::MakeObject();
        for (const auto& [key, windowID] : m_persistWindows)
            if (std::optional<JsonValue> w = windowJson(windowID))
                windows.Set(key, std::move(*w));
        doc.Set("windows", std::move(windows));

        JsonValue docking = JsonValue::MakeObject();
        JsonValue dockingStates = JsonValue::MakeObject();
        for (const auto& [key, layout] : m_persistDocks)
        {
            if (std::optional<JsonValue> tree = DockTreeJson(*layout))
                docking.Set(key, std::move(*tree));
            if (std::optional<JsonValue> states = PanelStatesJson(*layout))
                dockingStates.Set(key, std::move(*states));
        }
        doc.Set("docking", std::move(docking));
        doc.Set("panelStates", std::move(dockingStates));

        // Each main window's documents: which, in what order, the active one and their dock
        // trees, with the window's place.
        JsonValue documents = JsonValue::MakeObject();
        for (const auto& [key, listWindows] : m_persistDocuments)
        {
            JsonValue list = JsonValue::MakeArray();
            for (const DocumentWindowRef& window : listWindows ? listWindows() : std::vector<DocumentWindowRef>{})
            {
                if (!window.documents)
                    continue;
                JsonValue w = JsonValue::MakeObject();
                if (std::optional<JsonValue> place = windowJson(window.windowID))
                    w.Set("window", std::move(*place));
                w.Set("active", window.documents->active);
                JsonValue items = JsonValue::MakeArray();
                for (const auto& document : window.documents->documents)
                {
                    JsonValue d = JsonValue::MakeObject();
                    d.Set("name", document->name);
                    d.Set("extension", document->extension);
                    d.Set("kind", document->kind);
                    if (std::optional<JsonValue> tree = DockTreeJson(document->layout))
                        d.Set("layout", std::move(*tree));
                    if (std::optional<JsonValue> states = PanelStatesJson(document->layout))
                        d.Set("panelStates", std::move(*states));
                    items.Push(std::move(d));
                }
                w.Set("documents", std::move(items));
                list.Push(std::move(w));
            }
            documents.Set(key, std::move(list));
        }
        doc.Set("documents", std::move(documents));

        JsonValue tables = JsonValue::MakeObject();
        for (const auto& [key, table] : m_persistTables)
        {
            JsonValue columns = JsonValue::MakeArray();
            for (size_t i = 0; i < table.columns.size(); ++i)
            {
                const TableColumn& column = table.columns[i].column;
                const int32_t width = i < table.state->columnWidths.size() ? table.state->columnWidths[i] : column.initialWidth.value;
                JsonValue c = JsonValue::MakeObject();
                c.Set("column", std::string(column.header));
                c.Set("width", width);
                columns.Push(std::move(c));
            }
            tables.Set(key, std::move(columns));
        }
        doc.Set("tables", std::move(tables));

        JsonValue toolbars = JsonValue::MakeObject();
        for (const auto& [key, sides] : m_persistToolbars)
            toolbars.Set(key, ToolbarSidesJson(sides));
        doc.Set("toolbars", std::move(toolbars));

        // Only the shortcuts the user changed, so the rest follow the app's own.
        JsonValue shortcuts = JsonValue::MakeObject();
        for (const auto& [key, commands] : m_persistShortcuts)
        {
            JsonValue list = JsonValue::MakeArray();
            for (const CommandManager::Command& command : commands->GetCommands())
            {
                if (!command.IsRebound())
                    continue;
                JsonValue c = JsonValue::MakeObject();
                c.Set("mode", std::string(commands->GetModeName(command.mode)));
                c.Set("command", command.description);
                // Which of the commands of this name in its mode, when it isn't the first.
                int nth = 0;
                for (const CommandID id : commands->GetCommandsForMode(command.mode))
                {
                    const CommandManager::Command* other = commands->GetCommand(id);
                    if (other == &command)
                        break;
                    if (other->description == command.description)
                        ++nth;
                }
                if (nth > 0)
                    c.Set("nth", nth);
                c.Set("key", int(command.key));
                c.Set("modifiers", int(command.modifiers));
                list.Push(std::move(c));
            }
            shortcuts.Set(key, std::move(list));
        }
        doc.Set("shortcuts", std::move(shortcuts));

        JsonValue presets = JsonValue::MakeObject();
        for (const auto& [key, target] : m_persistPresets)
            if (Result<JsonValue> parsed = JsonValue::Parse(target->ToJson()))
                presets.Set(key, std::move(*parsed));
        doc.Set("layoutPresets", std::move(presets));

        // Each theme's variables, by its path: { "themes/dark.css": { "--bg-panel": "#202020" } }.
        JsonValue themeVariables = JsonValue::MakeObject();
        for (const std::string& key : m_persistThemeVariables)
        {
            JsonValue byTheme = JsonValue::MakeObject();
            for (const auto& [path, variables] : m_themeVariables)
            {
                if (variables.empty())
                    continue;
                JsonValue set = JsonValue::MakeObject();
                for (const auto& [name, value] : variables)
                    set.Set(name, value);
                byTheme.Set(path, std::move(set));
            }
            themeVariables.Set(key, std::move(byTheme));
        }
        doc.Set("themeVariables", std::move(themeVariables));

        JsonValue values = JsonValue::MakeObject();
        for (const auto& [key, target] : m_persistValues)
            std::visit([&](auto* v) { values.Set(key, JsonValue(*v)); }, target);
        doc.Set("values", std::move(values));

        return doc.Write();
    }

    Result<void> GUIApplication::RestoreUIState(std::string_view json)
    {
        Result<JsonValue> parsed = JsonValue::Parse(json);
        if (!parsed)
            return parsed.GetError();
        const JsonValue& doc = *parsed;
        if (!doc.IsObject())
            return Error{ ErrorCode::ParseFailed, "UI state: not a JSON object" };
        if (const JsonValue* version = doc.Find("version"); version && version->AsNumber() > kStateVersion)
            return Error{ ErrorCode::Unsupported, "UI state: saved by a newer version" };

        auto section = [&](const char* name, std::string_view key) -> const JsonValue* {
            const JsonValue* s = doc.Find(name);
            return s ? s->Find(key) : nullptr;
        };

        // A saved window place; null when it has no size.
        auto readWindow = [](const JsonValue* w) -> std::optional<RestoredWindow>
        {
            if (!w || !w->IsObject())
                return std::nullopt;
            RestoredWindow restored;
            restored.rect = IntRect{ int32_t(w->Find("x") ? w->Find("x")->AsNumber() : 0), int32_t(w->Find("y") ? w->Find("y")->AsNumber() : 0),
                                     int32_t(w->Find("w") ? w->Find("w")->AsNumber() : 0), int32_t(w->Find("h") ? w->Find("h")->AsNumber() : 0) };
            if (restored.rect.w <= 0 || restored.rect.h <= 0)
                return std::nullopt;
            restored.mode = ModeFromName(w->Find("mode") ? w->Find("mode")->AsString() : std::string_view{});
            if (const JsonValue* m = w->Find("monitor"); m && m->IsObject())
            {
                auto number = [m](const char* name) { const JsonValue* v = m->Find(name); return v ? int32_t(v->AsNumber()) : 0; };
                restored.monitor    = IntRect{ number("x"), number("y"), number("w"), number("h") };
                restored.hasMonitor = restored.monitor.w > 0 && restored.monitor.h > 0;
            }
            return restored;
        };

        // Windows: applied when each is next declared (OpenWindow).
        for (const auto& [key, windowID] : m_persistWindows)
            if (std::optional<RestoredWindow> restored = readWindow(section("windows", key)))
                m_restoredWindows[windowID] = *restored;

        // Dock trees: panels by key; panels the saved tree doesn't have go to its first leaf.
        // Nothing usable: the app's own layout stays.
        for (const auto& [key, layout] : m_persistDocks)
        {
            if (const JsonValue* tree = section("docking", key))
                (void)ApplyDockTree(*layout, tree->Write(), idManager);
            ApplyPanelStates(*layout, PanelStatesOf(section("panelStates", key)));
        }

        // Documents: kept for the app, which makes them again (TakeSavedDocuments).
        for (const auto& [key, listWindows] : m_persistDocuments)
        {
            const JsonValue* saved = section("documents", key);
            if (!saved || !saved->IsArray())
                continue;
            std::vector<SavedDocumentWindow> windows;
            for (const JsonValue& w : saved->Items())
            {
                if (!w.IsObject())
                    continue;
                SavedDocumentWindow window;
                if (const JsonValue* items = w.Find("documents"); items && items->IsArray())
                    for (const JsonValue& d : items->Items())
                    {
                        if (!d.IsObject() || StringOf(d, "name").empty())
                            continue;
                        const JsonValue* tree = d.Find("layout");
                        window.documents.push_back(SavedDocument{ StringOf(d, "name"), StringOf(d, "extension"), StringOf(d, "kind"),
                                                                  tree ? tree->Write() : std::string(), PanelStatesOf(d.Find("panelStates")) });
                    }
                if (window.documents.empty())
                    continue;
                const JsonValue* active = w.Find("active");
                window.active = std::clamp(active && active->IsNumber() ? int(active->AsNumber()) : 0, 0, int(window.documents.size()) - 1);
                if (std::optional<RestoredWindow> place = readWindow(w.Find("window")))
                {
                    window.rect    = place->rect;
                    window.mode    = place->mode;
                    window.monitor = place->hasMonitor ? place->monitor : IntRect{};
                }
                windows.push_back(std::move(window));
            }
            m_savedDocuments[key] = std::move(windows);
        }

        // Tables: columns in their saved order (new ones after), with their saved widths.
        for (const auto& [key, table] : m_persistTables)
        {
            const JsonValue* saved = section("tables", key);
            if (!saved || !saved->IsArray())
                continue;
            const size_t count = table.columns.size();
            std::vector<size_t> order;
            std::vector<int32_t> widths;
            std::vector<bool> taken(count, false);
            for (const JsonValue& c : saved->Items())
            {
                const std::string& header = c.Find("column") ? c.Find("column")->AsString() : std::string();
                for (size_t i = 0; i < count; ++i)
                    if (!taken[i] && table.columns[i].column.header == header)
                    {
                        taken[i] = true;
                        order.push_back(i);
                        const TableColumn& col = table.columns[i].column;
                        const double w = c.Find("width") ? c.Find("width")->AsNumber(col.initialWidth.value) : col.initialWidth.value;
                        widths.push_back(std::clamp(int32_t(std::lround(w)), col.minWidth.value, col.maxWidth.value));
                        break;
                    }
            }
            for (size_t i = 0; i < count; ++i)
                if (!taken[i]) { order.push_back(i); widths.push_back(table.columns[i].column.initialWidth.value); }

            std::vector<VirtualTableColumn> reordered;
            reordered.reserve(count);
            for (const size_t i : order) reordered.push_back(std::move(table.columns[i]));
            std::move(reordered.begin(), reordered.end(), table.columns.begin());
            if (table.state->sortColumn >= 0 && size_t(table.state->sortColumn) < count)
                table.state->sortColumn = int(std::find(order.begin(), order.end(), size_t(table.state->sortColumn)) - order.begin());
            table.state->columnWidths = std::move(widths);
        }

        // Toolbars: each named item on its saved side and place; the rest stay where they are.
        for (const auto& [key, sides] : m_persistToolbars)
        {
            const JsonValue* saved = section("toolbars", key);
            if (saved && saved->IsObject())
                PlaceToolbarItems(sides, *saved);
        }

        // Shortcuts: each command by its mode's name and its description (and which of that
        // name, when a mode has more); ones not found are skipped.
        for (const auto& [key, commands] : m_persistShortcuts)
        {
            const JsonValue* saved = section("shortcuts", key);
            if (!saved)
                continue;
            for (const JsonValue& c : saved->Items())
            {
                auto number = [&c](const char* name, double max) -> std::optional<uint32_t> {
                    const JsonValue* v = c.Find(name);
                    if (!v || !v->IsNumber() || !(v->AsNumber() >= 0 && v->AsNumber() <= max))
                        return std::nullopt;
                    return uint32_t(v->AsNumber());
                };
                const std::optional<uint32_t> keyCode   = number("key", 0xFFFF);
                const std::optional<uint32_t> modifiers = number("modifiers", 0xFF);
                if (!keyCode || !modifiers)
                    continue;
                const std::optional<uint32_t> nth = c.Find("nth") ? number("nth", 0xFFFF) : std::optional<uint32_t>(0);
                if (!nth)
                    continue;
                const std::string mode = StringOf(c, "mode");
                const std::string name = StringOf(c, "command");
                for (uint32_t m = 0; m < commands->GetModeCount(); ++m)
                {
                    if (commands->GetModeName(CommandMode(m)) != mode)
                        continue;
                    uint32_t seen = 0;
                    for (const CommandID id : commands->GetCommandsForMode(CommandMode(m)))
                        if (commands->GetCommand(id)->description == name && seen++ == *nth)
                        {
                            commands->SetShortcut(id, KeyCode(*keyCode), KeyModifier(*modifiers));
                            break;
                        }
                    break;
                }
            }
        }

        // Layout presets: the saved ones and the file types'; the app's built-in ones stay.
        for (const auto& [key, target] : m_persistPresets)
            if (const JsonValue* saved = section("layoutPresets", key))
                (void)target->FromJson(saved->Write());

        // Theme variables, in place of the ones set before; the theme loads again with them, when
        // they differ from the ones it has (a start that changed nothing loads it once).
        for (const std::string& key : m_persistThemeVariables)
        {
            const JsonValue* saved = section("themeVariables", key);
            if (!saved || !saved->IsObject())
                continue;
            decltype(m_themeVariables) restored;
            for (const auto& [path, set] : saved->Members())
            {
                if (!set.IsObject())
                    continue;
                ThemeVariableOverrides& variables = restored[ThemePathKey(path)];
                for (const auto& [name, value] : set.Members())
                    if (value.IsString())
                        variables[name] = value.AsString();
            }
            // A theme without changes is the same whether it has an empty set or none.
            auto withoutEmpty = [](decltype(m_themeVariables) sets) {
                std::erase_if(sets, [](const auto& entry) { return entry.second.empty(); });
                return sets;
            };
            if (withoutEmpty(restored) != withoutEmpty(m_themeVariables))
            {
                m_themeVariables = std::move(restored);
                m_themeReloadPending = true;
            }
        }

        // App values, by their own type; a value of another type is skipped.
        for (const auto& [key, target] : m_persistValues)
        {
            const JsonValue* v = section("values", key);
            if (!v)
                continue;
            std::visit([&](auto* t) {
                using T = std::remove_pointer_t<decltype(t)>;
                if constexpr (std::is_same_v<T, bool>)             { if (v->IsBool())   *t = v->AsBool(); }
                else if constexpr (std::is_same_v<T, std::string>) { if (v->IsString()) *t = v->AsString(); }
                else                                               { if (v->IsNumber()) *t = T(v->AsNumber()); }
            }, target);
        }
        return {};
    }

    void GUIApplication::PlaceOnSavedMonitor(RestoredWindow& restored) const
    {
        // Its monitor is still there: where it was. Gone (unplugged, or the displays
        // rearranged): the same place on a monitor of the same size, else on the primary one,
        // inside that monitor's work area.
        const auto& monitors = windowManager->Monitors();
        if (!restored.hasMonitor || monitors.empty())
            return;
        const MonitorInfo* target = nullptr;
        for (const MonitorInfo& monitor : monitors)
            if (MonitorBounds(monitor) == restored.monitor)
                return;
        for (const MonitorInfo& monitor : monitors)
            if (MonitorBounds(monitor).size == restored.monitor.size)
            {
                target = &monitor;
                break;
            }
        if (!target)
            target = &windowManager->GetPrimaryMonitor();

        IntRect& rect = restored.rect;
        rect.position = MonitorBounds(*target).position + (rect.position - restored.monitor.position);
        const IntRect& area = target->workArea;
        rect.w = std::min(rect.w, area.w);
        rect.h = std::min(rect.h, area.h);
        rect.x = std::clamp(rect.x, area.x, area.x + area.w - rect.w);
        rect.y = std::clamp(rect.y, area.y, area.y + area.h - rect.h);
    }

    void GUIApplication::ApplyRestoredWindow(ID windowID, LayoutOverride& constraints, ElementFlags& flags)
    {
        const auto it = m_restoredWindows.find(windowID);
        if (it == m_restoredWindows.end())
            return;
        RestoredWindow& restored = it->second;
        const bool exists = windowManager->GetWindowHandle(windowID) != nullptr;
        if (!restored.rectDone)
        {
            // Off every monitor (a display unplugged, a position saved wrong): keep the size, let
            // the app place it. Its top, where it's grabbed and closed, must be on a monitor:
            // inside a work area, with some of its width. Checked here, in a frame, when the
            // monitors are known.
            restored.hasPosition = false;
            if (windowManager->Monitors().empty())   // the first frame's are read at its end
                windowManager->RefreshMonitors();
            PlaceOnSavedMonitor(restored);
            for (const MonitorInfo& monitor : windowManager->Monitors())
            {
                const IntRect& a = monitor.workArea;
                const int32_t  top = restored.rect.y;
                const int32_t  visibleWidth = std::min(restored.rect.x + restored.rect.w, a.x + a.w)
                                            - std::max(restored.rect.x, a.x);
                if (top >= a.y && top < a.y + a.h - styleManager.Px(32)
                    && visibleWidth >= std::min(restored.rect.w, int32_t(styleManager.Px(100))))
                    restored.hasPosition = true;
            }
            if (exists)
            {
                IntRect rect = restored.rect;
                if (!restored.hasPosition)
                    rect.position = windowManager->GetWindowRect(windowID).position;
                (void)windowManager->SetWindowRect(windowID, rect);
            }
            else
            {
                // The OS window is made from this declaration's constraints.
                constraints.width.SetValue(Pixels{ restored.rect.w });
                constraints.height.SetValue(Pixels{ restored.rect.h });
                if (restored.hasPosition)
                {
                    constraints.Position(restored.rect.x, restored.rect.y);
                    flags = flags & ~ElementFlags::WindowIsCentered;
                }
            }
            restored.rectDone = true;
        }
        if (restored.mode == WindowMode::Normal)
            m_restoredWindows.erase(it);
        else if (exists)
        {
            windowManager->SetWindowMode(windowID, restored.mode);
            m_restoredWindows.erase(it);
        }
    }
}
