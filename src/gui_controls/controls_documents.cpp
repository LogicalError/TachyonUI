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

        // Where a document dropped at `x` goes in `set`: before the first tab whose middle is
        // past it (last frame's tabs; one left out of the bar is skipped).
        int InsertionIndex(const DocumentSet& set, int32_t x)
        {
            const auto& rects = set.internal.tabRects;
            for (size_t i = 0; i < rects.size() && i < set.documents.size(); ++i)
                if (rects[i].w > 0 && x < rects[i].x + rects[i].w / 2)
                    return int(i);
            return int(set.documents.size());
        }

        // A dragged document's tab, as it looks active: its kind's tab and name.
        ElementRef DocumentTabPreview(GUIApplication& app, const Document& document, ID id, LayoutOverride&& constraints)
        {
            const KindStyles styles = StylesOf(app.theme, document.kind);
            const ElementRef ref = app.RegisterElement(tachyon::Element{ .id = id, .styleRef = styles.tab, .flags = ElementFlags::NoInput,
                                                                         .state = ElementState::Selected }, std::move(constraints));
            VERIFY(app.elementStorage.BeginChildren());
            [[maybe_unused]] const bool pushed = app.idManager.PushParent(id);
            const ID nameID = app.idManager.AllocateDynamicID("name");
            app.RegisterElement(tachyon::Element{
                .id       = nameID,
                .textID   = app.textSystem->RegisterText(nameID, document.icon, document.name, styles.name, ElementState::Selected),
                .styleRef = styles.name,
                .flags    = ElementFlags::NoInput,
                .state    = ElementState::Selected }, LayoutOverride::Auto());
            const ID extensionID = app.idManager.AllocateDynamicID("extension");
            app.RegisterElement(tachyon::Element{
                .id       = extensionID,
                .textID   = app.textSystem->RegisterText(extensionID, IconID::Invalid, document.extension,
                                                         app.theme.documentExtensionStyleRef, ElementState::Selected),
                .styleRef = app.theme.documentExtensionStyleRef,
                .flags    = ElementFlags::NoInput,
                .state    = ElementState::Selected }, LayoutOverride::Auto());
            if (pushed)
                (void)app.idManager.PopParent();
            VERIFY(app.elementStorage.EndChildren());
            return ref;
        }

        // At the cursor, the window a drop would make: its document bar with the tab, over an
        // empty workspace.
        void DocumentGhost(GUIApplication& app, const Document& document, int2 cursor)
        {
            const int2 size{ app.styleManager.Px(260), app.styleManager.Px(150) };
            if (app.BeginCursorGhostWindow(GUIApplicationInternals::TabDragGhostWindowID(app), cursor, size,
                                           app.theme.mainWindowStyleRef))
            {
                app.RegisterElement(tachyon::Element{ .id = app.idManager.AllocateDynamicID("document_ghost_bar"),
                                                      .styleRef = app.theme.documentBarStyleRef }, LayoutOverride::Auto());
                VERIFY(app.elementStorage.BeginChildren());
                app.RegisterElement(tachyon::Element{ .id = app.idManager.AllocateDynamicID("document_ghost_row"),
                                                      .styleRef = app.theme.documentRowStyleRef }, LayoutOverride::Auto());
                VERIFY(app.elementStorage.BeginChildren());
                DocumentTabPreview(app, document, app.idManager.AllocateDynamicID("document_ghost_tab"), LayoutOverride::Auto());
                VERIFY(app.elementStorage.EndChildren());
                app.RegisterElement(tachyon::Element{ .id = app.idManager.AllocateDynamicID("document_ghost_line"),
                                                      .styleRef = StylesOf(app.theme, document.kind).line }, LayoutOverride::Auto());
                VERIFY(app.elementStorage.EndChildren());
            }
            app.EndGhostWindow();
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
        set.internal.hostWindowID = activeWindowID;

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
        set.internal.barID = idManager.AllocateDynamicID("document_bar");
        RegisterElement(tachyon::Element{ .id = set.internal.barID, .styleRef = theme.documentBarStyleRef },
                        LayoutOverride::Auto());
        VERIFY(elementStorage.BeginChildren());
        RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID("document_row"), .styleRef = theme.documentRowStyleRef },
                        LayoutOverride::Auto());
        VERIFY(elementStorage.BeginChildren());

        const ID tabBarID = idManager.AllocateDynamicID("document_tabs");
        set.internal.tabBarID = tabBarID;
        TabBarLayout oneRow = TabBarLayout::OneRow;
        DocumentSet* const setPtr = &set;

        // A tab dropped off the bar: onto another bar it moves there, elsewhere it's torn off
        // into a window of its own; either at the next frame's start. Only document bars take
        // documents: a drop on panels is a tear-off too.
        auto onDragOut = [this, setPtr](int index, int2 screenPos)
        {
            windowManager->RequestCloseAllowReopen(tabDragGhostWindowID);
            if (index < 0 || index >= int(setPtr->documents.size()))
                return;
            Document* const document = setPtr->documents[size_t(index)].get();
            // Its own bar, off the tabs ("+", the empty bar, "N open"): it moves along it, last
            // when it's past them.
            DocumentSet* const target = FindDocumentBarAt(screenPos);
            if (target)
                m_documentMove = DocumentMove{ document, setPtr, target, InsertionIndex(*target, screenPos.x), screenPos };
            else if (setPtr->onTearOff)
                m_documentMove = DocumentMove{ document, setPtr, nullptr, 0, screenPos };
        };
        // Off the bar: over another one its phantom tab shows where it goes, elsewhere the
        // window a drop would make follows the cursor.
        auto dragVisualizer = [this, setPtr](int index, int2 cursor)
        {
            if (index < 0 || index >= int(setPtr->documents.size()) || !setPtr->onTearOff)
                return;
            for (const IntRect& bar : m_documentBarRects)
                if (bar.Contains(cursor))
                    return;
            DocumentGhost(*this, *setPtr->documents[size_t(index)], cursor);
        };
        BeginTabBar(tabBarID, &set.active,
                    [setPtr](int from, int to) { MoveDocument(*setPtr, from, to); },
                    [setPtr](int index) {
                        if (index >= 0 && index < int(setPtr->documents.size()))
                            setPtr->documents[size_t(index)]->internal.closeAsked = true;
                    },
                    std::move(onDragOut), std::move(dragVisualizer), theme.documentTabBarStyleRef, {}, &oneRow,
                    TabBarOptions{ .closeLabel = "Close Document", .listHidden = false, .layoutMenu = false,
                                   .dropArea = set.internal.barID });   // the whole bar, its line and "N open" too

        // This bar's tab being dragged: the other bars learn which document it is.
        const ReorderableDragState& drag = m_reorderable->Drag();
        if (drag.IsDragging() && drag.groupID == tabBarID)
        {
            // The drag counts the tabs in the bar; the ones left out are skipped.
            int index = -1;
            for (int i = 0, slot = 0; i < int(set.documents.size()); ++i)
            {
                const auto& shown = m_tabBarState.shown;
                if (!shown.empty() && (size_t(i) >= shown.size() || !shown[size_t(i)]))
                    continue;
                if (slot++ == drag.draggedIndex)
                {
                    index = i;
                    break;
                }
            }
            const bool measured = index >= 0 && drag.draggedIndex < int(drag.originalRects.size());
            if (index >= 0)
                m_documentDrag = { set.documents[size_t(index)].get(), &set,
                                   measured ? drag.originalRects[size_t(drag.draggedIndex)].w : 0,
                                   measured ? drag.press.pressScreen.x - drag.originalRects[size_t(drag.draggedIndex)].x : 0 };
        }
        // Another bar's tab over this one: a phantom where it would go, the tabs moving apart.
        int phantomAt = -1;
        if (m_documentDrag.document && m_documentDrag.source != &set && drag.IsPressed() && drag.groupID != tabBarID
            && set.internal.barRect.Contains(drag.cursorScreen))
            phantomAt = InsertionIndex(set, drag.cursorScreen.x);
        // It follows the cursor, held where it was grabbed, over the other tabs, as a tab
        // dragged within its own bar does: offset from where the bar would put it (after the
        // tab before it, else where the tab after it starts).
        auto phantom = [&]
        {
            const ElementRef ref = DocumentTabPreview(*this, *m_documentDrag.document, idManager.AllocateDynamicID("document_phantom"),
                                                      m_documentDrag.tabWidth > 0 ? LayoutOverride::Auto().Width(Pixels{ m_documentDrag.tabWidth })
                                                                                  : LayoutOverride::Auto());
            if (!elementStorage.IsValidRef(ref))
                return;
            const auto& rects = set.internal.tabRects;
            const LayoutStyle* barLayout = styleManager.GetLayoutStyle(theme.documentTabBarStyleRef.layoutStyleIndex);
            const int32_t gap = barLayout ? int32_t(barLayout->layoutData.flex.gap) : 0;
            int32_t placed = set.internal.barRect.x;
            bool found = false;
            for (int i = std::min(phantomAt, int(rects.size())) - 1; i >= 0 && !found; --i)
                if (rects[size_t(i)].w > 0)
                {
                    placed = rects[size_t(i)].x + rects[size_t(i)].w + gap;
                    found = true;
                }
            for (size_t i = size_t(phantomAt); i < rects.size() && !found; ++i)
                if (rects[i].w > 0)
                {
                    placed = rects[i].x;
                    found = true;
                }
            const int32_t offset = drag.cursorScreen.x - m_documentDrag.grabOffset - placed;
            auto& rect = elementStorage.GetElementRectUnsafe(ref);
            const LayoutOverride relative = LayoutOverride{}.Relative(static_cast<int16_t>(offset), 0);
            rect.constraints.x = relative.x;
            rect.constraints.y = relative.y;
            windowRegistry.MoveSubtreeToEnd(ref);
        };

        for (size_t i = 0; i < set.documents.size(); ++i)
        {
            if (int(i) == phantomAt)
                phantom();
            Document& document = *set.documents[i];
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
        if (phantomAt >= int(set.documents.size()))
            phantom();

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

    void GUIApplication::ApplyDocumentMove()
    {
        if (!m_documentMove)
            return;
        const DocumentMove move = *m_documentMove;
        m_documentMove.reset();
        // Both sets were built last frame, so they're still there.
        auto built = [this](const DocumentSet* set)
            { return std::find(m_documentSetsThisFrame.begin(), m_documentSetsThisFrame.end(), set) != m_documentSetsThisFrame.end(); };
        if (!built(move.source) || (move.target && !built(move.target)) || (!move.target && !move.source->onTearOff))
            return;
        auto& from = move.source->documents;
        const auto it = std::find_if(from.begin(), from.end(), [&](const auto& d) { return d.get() == move.document; });
        if (it == from.end())
            return;   // closed meanwhile
        const int index = int(it - from.begin());
        std::unique_ptr<Document> taken = std::move(*it);
        from.erase(it);
        if (move.source->active > index)
            --move.source->active;
        move.source->active = std::clamp(move.source->active, 0, std::max(0, int(from.size()) - 1));

        if (!move.target)
        {
            move.source->onTearOff(std::move(taken), move.screenPos);
            return;
        }
        auto& to = move.target->documents;
        // Within its own set, the slots after it moved up one when it was taken out.
        const int wanted = move.target == move.source && move.index > index ? move.index - 1 : move.index;
        const int at = std::clamp(wanted, 0, int(to.size()));
        to.insert(to.begin() + at, std::move(taken));
        move.target->active = at;
        if (move.target->internal.hostWindowID != ID::Invalid())
            windowManager->BringToFront(move.target->internal.hostWindowID);
    }

    void GUIApplication::SyncDocumentRects()
    {
        auto rectOf = [this](ID id)
        {
            const auto* rect = elementStorage.GetElementRectPtr(elementStorage.FindElementRefByID(id));
            return rect ? rect->rect : IntRect{};
        };
        m_documentBarRects.clear();
        for (DocumentSet* set : m_documentSetsThisFrame)
        {
            set->internal.barRect = rectOf(set->internal.barID);
            set->internal.tabRects.clear();
            for (const auto& document : set->documents)
                set->internal.tabRects.push_back(rectOf(document->id));
            m_documentBarRects.push_back(set->internal.barRect);
        }
    }

    DocumentSet* GUIApplication::FindDocumentBarAt(int2 screenPos)
    {
        for (DocumentSet* set : m_documentSetsThisFrame)
            if (set->internal.barRect.Contains(screenPos))
                return set;
        return nullptr;
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
