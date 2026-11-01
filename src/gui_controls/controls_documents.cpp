#include <gui_controls/gui_application_internal.h>

#include <gui_controls/gui_dock_node.h>

#include <algorithm>
#include <string>

namespace tachyon
{

    // ---------------------------------------------------------------------------
    // Documents: the level above panels. A bar of tabs, one per document, each in its
    // type's colors, over the active document's docking container.
    // ---------------------------------------------------------------------------

    namespace
    {
        // A document type's classes, or the plain ones when the theme has none for it.
        struct KindStyles
        {
            StyleRef tab;
            StyleRef name;
            StyleRef line;
        };

        KindStyles StylesOf(const GUITheme& theme, std::string_view kind)
        {
            auto find = [&](std::string_view part, StyleRef plain)
            {
                if (kind.empty())
                    return plain;
                std::string cssClass = "document-";
                cssClass.append(part).append("-").append(kind);
                const Result<StyleRef> found = theme.FindStyle(cssClass);
                return found ? *found : plain;
            };
            return { find("tab", theme.documentTabStyleRef),
                     find("name", theme.documentNameStyleRef),
                     find("line", theme.documentLineStyleRef) };
        }

        // Moves a document within the set; the active one stays active.
        void MoveDocument(DocumentSet& set, int from, int to)
        {
            const int count = int(set.documents.size());
            if (from == to || from < 0 || from >= count || to < 0 || to >= count)
                return;
            std::unique_ptr<Document> moved = std::move(set.documents[size_t(from)]);
            set.documents.erase(set.documents.begin() + from);
            set.documents.insert(set.documents.begin() + to, std::move(moved));
            if (set.active == from)
                set.active = to;
            else if (from < to && set.active > from && set.active <= to)
                --set.active;
            else if (from > to && set.active >= to && set.active < from)
                ++set.active;
        }
    }

    Document& GUIApplication::CreateDocument(DocumentSet& set, std::string name, std::string extension,
                                             std::string kind, IconID icon)
    {
        auto document = std::make_unique<Document>();
        document->id        = idManager.CreateStaticID();
        document->name      = std::move(name);
        document->extension = std::move(extension);
        document->kind      = std::move(kind);
        document->icon      = icon;
        document->internal.dockID = idManager.CreateStaticID();
        set.documents.push_back(std::move(document));
        if (set.documents.size() == 1)
            set.active = 0;
        return *set.documents.back();
    }

    void GUIApplication::CloseDocument(DocumentSet& set, int index)
    {
        if (index < 0 || index >= int(set.documents.size()))
            return;
        Document& document = *set.documents[size_t(index)];

        // Its torn-off panels go with it; they would dock back into a layout that's gone.
        for (size_t i = m_floatingDockWindows.size(); i-- > 0; )
            if (m_floatingDockWindows[i]->home == &document.layout)
                CloseFloatingDockWindow(i);
        std::erase(m_dockingLayoutsThisFrame, &document.layout);
        std::erase_if(m_persistDocks, [&](const PersistedDock& dock) { return dock.target == &document.layout; });

        DisableAllPanels(document.layout);
        ReleaseDockTreeIDs(document.layout.root.get(), &idManager);
        idManager.ReleaseStaticID(document.internal.dockID);
        if (document.id.GetMode() == IDMode::Static)
            idManager.ReleaseStaticID(document.id);
        set.documents.erase(set.documents.begin() + index);

        // The active one stays; when it was this one, the one after it (else before it).
        if (set.active > index)
            --set.active;
        set.active = std::clamp(set.active, 0, std::max(0, int(set.documents.size()) - 1));
    }

    void GUIApplication::Documents(ID id, DocumentSet& set, LayoutOverride&& constraints)
    {
        // The documents whose tabs asked to close, now that no handler holds on to them.
        for (size_t i = set.documents.size(); i-- > 0; )
        {
            Document& document = *set.documents[i];
            if (!document.internal.closeAsked)
                continue;
            document.internal.closeAsked = false;
            if (!set.onClose || set.onClose(document))
                CloseDocument(set, int(i));
        }
        set.active = std::clamp(set.active, 0, std::max(0, int(set.documents.size()) - 1));
        m_documentSetsThisFrame.push_back(&set);

        // Only the active document's panels are shown.
        Document* const active = set.Active();
        for (const auto& document : set.documents)
            if (document.get() != active)
                DisableAllPanels(document->layout);

        RegisterElement(tachyon::Element{ .id = id, .styleRef = theme.documentsStyleRef, .state = guiStateManager.GetCurrentState(id) },
                        std::move(constraints));
        VERIFY(elementStorage.BeginChildren());
        [[maybe_unused]] const bool pushed = idManager.PushParent(id);

        // ---- The bar: the tabs, "+", and the list of every document ----
        RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID("document_bar"), .styleRef = theme.documentBarStyleRef },
                        LayoutOverride::Auto());
        VERIFY(elementStorage.BeginChildren());
        RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID("document_row"), .styleRef = theme.documentRowStyleRef },
                        LayoutOverride::Auto());
        VERIFY(elementStorage.BeginChildren());

        const ID tabBarID = idManager.AllocateDynamicID("document_tabs");
        TabBarLayout oneRow = TabBarLayout::OneRow;
        DocumentSet* const setPtr = &set;
        BeginTabBar(tabBarID, &set.active,
                    [setPtr](int from, int to) { MoveDocument(*setPtr, from, to); },
                    [setPtr](int index) {
                        if (index >= 0 && index < int(setPtr->documents.size()))
                            setPtr->documents[size_t(index)]->internal.closeAsked = true;
                    },
                    nullptr, nullptr, theme.documentTabBarStyleRef, {}, &oneRow,
                    TabBarOptions{ .closeLabel = "Close Document", .listHidden = false, .layoutMenu = false });
        for (const auto& owned : set.documents)
        {
            Document& document = *owned;
            const KindStyles styles = StylesOf(theme, document.kind);
            if (BeginTab(document.id, { document.icon, document.name }, styles.tab, styles.name))
            {
                const ElementState state = guiStateManager.GetCurrentState(m_tabState.bodyID);
                [[maybe_unused]] const bool inTab = idManager.PushParent(document.id);

                // Name + extension: the extension right after the name, muted.
                const ID extensionID = idManager.AllocateDynamicID("extension");
                RegisterElement(tachyon::Element{
                    .id       = extensionID,
                    .textID   = textSystem->RegisterText(extensionID, IconID::Invalid, document.extension, theme.documentExtensionStyleRef, state),
                    .styleRef = theme.documentExtensionStyleRef,
                    .flags    = ElementFlags::NoInput,
                    .state    = state }, LayoutOverride::Auto());
                RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID("gap"), .styleRef = theme.emptyHorzStyleRef,
                                                  .flags = ElementFlags::NoInput }, LayoutOverride{}.FlexGrow(1));

                // Its close button on the active or hovered tab, else a dot for unsaved changes.
                // The slot keeps its size, so the tab doesn't change width on hover.
                RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID("mark"), .styleRef = theme.documentMarkStyleRef,
                                                  .state = state }, LayoutOverride::Auto());
                VERIFY(elementStorage.BeginChildren());
                const bool showClose = (state & (ElementState::Selected | ElementState::Hover)) != ElementState::None;
                if (showClose)
                {
                    Document* const target = &document;
                    Button(idManager.AllocateDynamicID("close"), theme.tabCloseIcon,
                           [target] { target->internal.closeAsked = true; }, theme.documentCloseStyleRef);
                }
                else if (document.dirty)
                    RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID("dirty"), .styleRef = theme.documentDirtyStyleRef,
                                                      .flags = ElementFlags::NoInput, .state = state }, LayoutOverride::Auto());
                VERIFY(elementStorage.EndChildren());

                if (inTab)
                    (void)idManager.PopParent();
            }
            EndTab();
        }

        // "+" after the last tab; the bar leaves room for it when tabs don't all fit.
        auto& fit = guiStateManager.GetElementDataStore().GetOrCreate<TabBarMemory>(tabBarID);
        if (set.onNew)
        {
            const ID newID = idManager.AllocateDynamicID("document_new");
            Button(newID, theme.addIcon, [setPtr] { if (setPtr->onNew) setPtr->onNew(); }, theme.documentNewStyleRef,
                   LayoutOverride{}.NoGrow().NoShrink());
            if (const ElementRef ref = elementStorage.FindElementRefByID(newID); ref.IsValid())
                [[maybe_unused]] auto added = elementStorage.AddPostLayout(ref, [this, tabBarID](PostLayoutContext& ctx)
                    { guiStateManager.GetElementDataStore().GetOrCreate<TabBarMemory>(tabBarID).trailingWidth = ctx.ElementRect().w; });
        }
        else
            fit.trailingWidth = 0;
        EndTabBar();

        // "N open": every document, the hidden ones too; picking one shows it.
        if (!set.documents.empty())
        {
            const ID listID = idManager.AllocateDynamicID("document_list");
            if (BeginMenu(listID, std::to_string(set.documents.size()) + " open", theme.documentListStyleRef,
                          theme.menuStyleRef, theme.menuItemRowStyleRef, LayoutOverride{}.NoGrow().NoShrink()))
            {
                for (size_t i = 0; i < set.documents.size(); ++i)
                {
                    const Document& document = *set.documents[i];
                    MenuItem({ document.icon, document.name + document.extension },
                             [setPtr, index = int(i)] { setPtr->active = index; });
                }
            }
            EndMenu();
        }

        VERIFY(elementStorage.EndChildren());   // row

        // The line under the bar, in the active document's color: the workspace below is its.
        RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID("document_line"),
                                          .styleRef = active ? StylesOf(theme, active->kind).line : theme.documentLineStyleRef },
                        LayoutOverride::Auto());
        VERIFY(elementStorage.EndChildren());   // bar

        if (pushed)
            (void)idManager.PopParent();

        // ---- The active document's panels ----
        if (active)
        {
            BeginDockingContainer(active->internal.dockID, &active->layout, theme.dockContainerMainStyleRef, LayoutOverride::Auto());
            EndDockingContainer();
        }
        VERIFY(elementStorage.EndChildren());
    }

    bool GUIApplication::ActivateDocumentOf(const DockingLayout* layout)
    {
        for (DocumentSet* set : m_documentSetsThisFrame)
            for (size_t i = 0; i < set->documents.size(); ++i)
                if (&set->documents[i]->layout == layout)
                {
                    set->active = int(i);
                    return true;
                }
        return false;
    }

}
