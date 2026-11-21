#include "application.h"

//
// This file is for testing and prototyping a reorderable panel, which can be used for things like tab bars, lists, etc.
//

namespace tachyon
{

    // A Reorderable panel's own: its lists, in the order they were dragged into, and its
    // groups' static IDs.
    struct ReorderablePanel
    {
        std::vector<std::string> tabs  = { "Home", "Settings", "Profile", "Help" };
        std::vector<std::string> items = { "First", "Second", "Third", "Fourth", "Fifth" };
        std::vector<std::string> wrappedHorzItems = {
            "Alpha", "Beta", "Gamma", "Delta", "Epsilon",
            "Zeta", "Eta", "Theta", "Iota", "Kappa"
        };
        std::vector<std::string> wrappedVertItems = {
            "One", "Two", "Three", "Four", "Five",
            "Six", "Seven", "Eight", "Nine", "Ten"
        };

        IDManager* ids = nullptr;
        ID tabsID, listID, wrappedHorzID, wrappedVertID;

        explicit ReorderablePanel(IDManager& manager)
            : ids(&manager), tabsID(manager.CreateStaticID()), listID(manager.CreateStaticID()),
              wrappedHorzID(manager.CreateStaticID()), wrappedVertID(manager.CreateStaticID()) {}
        ~ReorderablePanel()
        {
            for (const ID id : { tabsID, listID, wrappedHorzID, wrappedVertID })
                ids->ReleaseStaticID(id);
        }
    };

    namespace
    {
        // Moves item `from` to `to`, as a reorderable group asks.
        void MoveItem(std::vector<std::string>& list, int from, int to)
        {
            auto item = std::move(list[size_t(from)]);
            list.erase(list.begin() + from);
            list.insert(list.begin() + to, std::move(item));
        }
    }

    ID Application::CreateReorderablePanel(std::unordered_map<ID, Panel>& panels, std::string title)
    {
        auto panel = std::make_shared<ReorderablePanel>(idManager);
        return CreatePanel(panels, std::move(title), showcase.panelMiscIcon, [this, panel] { RenderReorderablePanel(*panel); });
    }

    void Application::RenderReorderablePanel(ReorderablePanel& panel)
    {
        BeginHorizontalWrap();

        // Reorderable tabs demo (horizontal) - items stretch to fill the bar's vertical space
        {
            Label("Reorderable Tabs");
            if (BeginReorderableGroup(panel.tabsID, ReorderDirection::Horizontal,
                [&panel](int from, int to) { MoveItem(panel.tabs, from, to); }, theme.emptyHorzStyleRef, tachyon::LayoutOverride{}.Height(40)))
            {
                for (int i = 0; i < static_cast<int>(panel.tabs.size()); i++)
                {
                    BeginReorderableItem();
                    AddBox(theme.regularControls.button, panel.tabs[i]);
                    EndReorderableItem();
                }
                EndReorderableGroup();
            }
        }

        // Reorderable list demo (vertical)
        {
            Label("Reorderable List");
            if (BeginReorderableGroup(panel.listID, ReorderDirection::Vertical,
                [&panel](int from, int to) { MoveItem(panel.items, from, to); }, tachyon::LayoutOverride{}.FixedWidth(300)))
            {
                for (int i = 0; i < static_cast<int>(panel.items.size()); i++)
                {
                    BeginReorderableItem();
                    AddBox(theme.regularControls.button, panel.items[i]);
                    EndReorderableItem();
                }
                EndReorderableGroup();
            }
        }

        // Wrapped reorderable demo (horizontal, wraps into rows)
        {
            Label("Wrapped Reorderable (horizontal)");
            if (BeginReorderableGroup(panel.wrappedHorzID, ReorderDirection::Horizontal, true,
                [&panel](int from, int to) { MoveItem(panel.wrappedHorzItems, from, to); }, tachyon::LayoutOverride::FixedSize(400, 200)))
            {
                for (int i = 0; i < static_cast<int>(panel.wrappedHorzItems.size()); i++)
                {
                    BeginReorderableItem();
                    AddBox(theme.regularControls.button, panel.wrappedHorzItems[i]);
                    EndReorderableItem();
                }
                EndReorderableGroup();
            }
        }

        // Wrapped reorderable demo (vertical, wraps into columns)
        {
            Label("Wrapped Reorderable (vertical)");
            if (BeginReorderableGroup(panel.wrappedVertID, ReorderDirection::Vertical, true,
                [&panel](int from, int to) { MoveItem(panel.wrappedVertItems, from, to); }, tachyon::LayoutOverride::FixedSize(400, 200)))
            {
                for (int i = 0; i < static_cast<int>(panel.wrappedVertItems.size()); i++)
                {
                    BeginReorderableItem();
                    AddBox(theme.regularControls.button, panel.wrappedVertItems[i]);
                    EndReorderableItem();
                }
                EndReorderableGroup();
            }
        }

        EndHorizontalWrap();
    }
}