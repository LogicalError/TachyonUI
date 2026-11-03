#include "application.h"
#include <gui_controls/gui_file_icons.h>
#include <gui_controls/gui_file_system.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <unordered_set>

//
// This file is a file browser panel demo, meant to test and demonstrate the grid view and
// drag & drop features - including dragging files and folders between the grid and the
// folder tree. It browses an IFileSystem: a copy of a folder on the disk (the home folder,
// or the one given with --files), read as it's browsed. Moves and renames change only the
// copy; the disk is never written.
//

namespace tachyon
{
    static std::unique_ptr<MemoryFileSystem> files;
    static std::unique_ptr<FileIcons>        fileIcons;       // the OS's icons, where there are any
    static uint64_t                          filesGeneration = 0;

    static tachyon::ItemDragDropConfig gridDnD;
    static tachyon::ItemDragDropConfig folderDnD;
    static tachyon::GridViewVirtualState gridScrollState;
    constexpr uint32_t kGridPayload = 0x47524944; // 'GRID'

    // Drag payload: an entry's index in gridEntries, or a folder's flat folderRows index
    // tagged with the top bit (folders can be dragged from the grid and from the folder
    // tree alike).
    constexpr uintptr_t kPayloadFolderBit = uintptr_t(1) << (sizeof(uintptr_t) * 8 - 1);
    constexpr uintptr_t kNoPayload        = ~uintptr_t(0);
    static uintptr_t FolderPayload(int flat)        { return kPayloadFolderBit | static_cast<uintptr_t>(flat); }
    static bool      IsFolderPayload(uintptr_t v)   { return (v & kPayloadFolderBit) != 0; }
    static int       FolderPayloadIndex(uintptr_t v){ return static_cast<int>(v & ~kPayloadFolderBit); }

    // What the grid shows for the folder on view: its folders, then its files, each by name,
    // reduced to the ones that match the search.
    struct GridEntry {
        FileEntry   file;
        std::string renameBuffer;   // a rename edits this; it goes to the file system once it's done
    };
    static std::vector<GridEntry> gridEntries;
    // Grid of icons, or a list (the toolbar's grid/list toggles).
    static bool filesListMode = false;

    // Folder tree left of the grid (flat VirtualTreeView rows): the root, the folders of the
    // open folders, and one level further, so a closed folder shows whether it holds any.
    // folderPaths is parallel to folderRows.
    static std::vector<tachyon::TreeViewRow> folderRows;
    static std::vector<std::string>          folderPaths;
    static std::vector<int>                  folderVisibleIdx;
    static tachyon::TableState               folderTreeState;
    static int                               folderTreeWidth = 180; // CSS pixels
    static std::unordered_set<std::string>   openFolders;           // paths
    static std::string                       currentFolder;         // the path of the folder on view
    static std::string                       rootName;
    static bool                              treeDirty = true;

    // Search state, rebuilt only when the query, the files or the folder on view change.
    //   filteredFolder* - the folder tree reduced to folders holding a match (plus their
    //                     ancestors), shown while a query is active.
    static std::string                       filesSearch;
    static std::string                       filesQueryLower;
    static bool                              gridFilterDirty = true;
    static std::vector<tachyon::TreeViewRow> filteredFolderRows;
    static std::vector<int>                  filteredFolderVisibleIdx;
    static tachyon::TableState               filteredFolderState;
    static std::vector<int>                  filteredToMaster; // filtered flat idx -> master flat idx

    // Visible position encoded in a grid item's ranged sub-ID (-1 if not one).
    static int GridItemIndex(tachyon::ID id)
    {
        if (id.GetMode() != tachyon::IDMode::SubRange) return -1;
        return static_cast<int>(id.value.subRangeParts.index);
    }

    static const GridEntry* GridEntryAt(int visiblePos)
    {
        return (visiblePos >= 0 && visiblePos < static_cast<int>(gridEntries.size())) ? &gridEntries[visiblePos] : nullptr;
    }

    // Flat row index a tree's single-selection points at (-1 if none).
    static int SelectedFlat(const tachyon::TableState& state,
                            const std::vector<int>& visibleIdx)
    {
        if (state.selectedRowID.GetMode() != tachyon::IDMode::SubRange) return -1;
        const int visIdx = static_cast<int>(state.selectedRowID.value.subRangeParts.index);
        if (visIdx < 0 || visIdx >= int(visibleIdx.size())) return -1;
        return visibleIdx[visIdx];
    }

    // The flat row index of a tree row's ID (-1 if not one of the rows).
    static int RowFlat(tachyon::ID rowID)
    {
        if (rowID.GetMode() != tachyon::IDMode::SubRange) return -1;
        const int visIdx = static_cast<int>(rowID.value.subRangeParts.index);
        return (visIdx >= 0 && visIdx < static_cast<int>(folderVisibleIdx.size())) ? folderVisibleIdx[visIdx] : -1;
    }

    // Folders first, then by name (case-insensitive): the view is sorted, not ordered by hand.
    static bool EntryBefore(const FileEntry& a, const FileEntry& b)
    {
        if (a.isFolder != b.isFolder)
            return a.isFolder;
        return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end(), [](char x, char y) {
            return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
        });
    }

    static std::vector<FileEntry> SortedList(std::string_view folder)
    {
        std::vector<FileEntry> list = files->List(folder);
        std::sort(list.begin(), list.end(), EntryBefore);
        return list;
    }

    // `path`, or a path in it, with `from` replaced by `to`; untouched otherwise.
    static bool Retarget(std::string& path, const std::string& from, const std::string& to)
    {
        if (path == from) { path = to; return true; }
        if (path.size() > from.size() && path.compare(0, from.size(), from) == 0 && path[from.size()] == '/' && !from.empty())
        {
            path = to + path.substr(from.size());
            return true;
        }
        return false;
    }

    // After a folder moved or was renamed, the folder on view and the open folders follow it.
    static void FolderMoved(const std::string& from, const std::string& to)
    {
        Retarget(currentFolder, from, to);
        std::unordered_set<std::string> open;
        for (std::string path : openFolders)
        {
            Retarget(path, from, to);
            open.insert(std::move(path));
        }
        openFolders = std::move(open);
    }

    // The rows of the folders in `path`, at `depth`. `shown`: the folder's own rows can be
    // seen, so their folders are listed too (each row knows whether it has children).
    static void AddFolderRows(const std::string& path, int depth, bool shown)
    {
        for (const FileEntry& entry : SortedList(path))
        {
            if (!entry.isFolder)
                continue;
            const std::string child = JoinPath(path, entry.name);
            tachyon::TreeViewRow row{};
            row.name   = entry.name;
            row.depth  = depth;
            row.isOpen = openFolders.contains(child);
            const bool open = row.isOpen;
            folderRows.push_back(std::move(row));
            folderPaths.push_back(child);
            if (shown)
                AddFolderRows(child, depth + 1, open);
        }
    }

    static void RebuildFolderTree()
    {
        folderRows.clear();
        folderPaths.clear();
        tachyon::TreeViewRow root{};
        root.name   = rootName;
        root.isOpen = openFolders.contains("");
        const bool open = root.isOpen;
        folderRows.push_back(std::move(root));
        folderPaths.push_back("");
        AddFolderRows("", 1, open);
        GUIApplication::RebuildTreeViewIndex(folderRows);
        // Visible indices moved - the render pass re-selects currentFolder.
        folderTreeState.selectedRowID = tachyon::ID::Invalid();
    }

    // The grid's entries for the folder on view, reduced to the ones that match the search.
    static void RebuildGridVisible()
    {
        gridEntries.clear();
        for (FileEntry& file : SortedList(currentFolder))
            if (SearchMatches(file.name, filesQueryLower))
            {
                std::string name = file.name;
                gridEntries.push_back({ .file = std::move(file), .renameBuffer = std::move(name) });
            }
    }

    // The reduced folder tree shown while searching: folders with at least one matching
    // entry, plus their ancestors (forced open). Depths are preserved, so the copy is a
    // valid forest for RebuildTreeViewIndex.
    static void RebuildFolderFilter()
    {
        filteredFolderRows.clear();
        filteredToMaster.clear();
        if (filesQueryLower.empty()) return;

        std::vector<char> keep(folderRows.size(), 0);
        std::vector<int>  chain; // chain[d] = index of the current ancestor at depth d
        for (int i = 0; i < int(folderRows.size()); ++i)
        {
            chain.resize(folderRows[i].depth);
            chain.push_back(i);
            bool hasHit = false;
            for (const FileEntry& entry : files->List(folderPaths[i]))
                if (SearchMatches(entry.name, filesQueryLower)) { hasHit = true; break; }
            if (hasHit)
                for (int a : chain) keep[a] = 1;
        }
        for (int i = 0; i < int(folderRows.size()); ++i)
        {
            if (!keep[i]) continue;
            tachyon::TreeViewRow r = folderRows[i];
            r.isOpen       = true;
            r.renameBuffer = nullptr; // copies don't write back - no rename
            filteredFolderRows.push_back(std::move(r));
            filteredToMaster.push_back(i);
        }
        GUIApplication::RebuildTreeViewIndex(filteredFolderRows);
    }

    // The path a drag payload stands for; false for none (the root doesn't move).
    static bool PayloadPath(uintptr_t payload, std::string& path)
    {
        if (payload == kNoPayload)
            return false;
        if (IsFolderPayload(payload))
        {
            const int flat = FolderPayloadIndex(payload);
            if (flat <= 0 || flat >= static_cast<int>(folderPaths.size())) return false;
            path = folderPaths[flat];
            return true;
        }
        const GridEntry* entry = GridEntryAt(static_cast<int>(payload));
        if (!entry) return false;
        path = JoinPath(currentFolder, entry->file.name);
        return true;
    }

    // Whether what `payload` stands for may go into the folder `folder`: not where it is
    // already, and a folder not into itself or its own subtree.
    static bool CanDropInto(uintptr_t payload, const std::string& folder)
    {
        std::string path;
        if (!PayloadPath(payload, path) || ParentPath(path) == folder) return false;
        std::string inside = folder;
        return !Retarget(inside, path, "");
    }

    void Application::MoveFileInto(uintptr_t payload, const std::string& folder)
    {
        std::string path;
        if (!CanDropInto(payload, folder) || !PayloadPath(payload, path)) return;
        std::string error;
        if (!files->Move(path, folder, error))
        {
            ShowToast(ToastDesc{ .severity = Severity::Error, .title = "Can't move \"" + std::string(FileName(path)) + "\"", .body = error });
            return;
        }
        FolderMoved(path, JoinPath(folder, FileName(path)));
    }

    // Renames whose editors closed: a name that's changed goes to the file system; one it
    // refuses goes back, with a toast saying why.
    void Application::ApplyFileRenames()
    {
        std::vector<std::pair<std::string, std::string>> renames; // path, new name
        for (const GridEntry& entry : gridEntries)
            if (entry.renameBuffer != entry.file.name)
                renames.emplace_back(JoinPath(currentFolder, entry.file.name), entry.renameBuffer);
        for (size_t i = 1; i < folderRows.size(); ++i)
            if (folderRows[i].name != FileName(folderPaths[i]))
                renames.emplace_back(folderPaths[i], folderRows[i].name);
        for (const auto& [path, name] : renames)
        {
            std::string error;
            if (files->Rename(path, name, error))
                FolderMoved(path, JoinPath(ParentPath(path), name));
            else
                ShowToast(ToastDesc{ .severity = Severity::Error, .title = "Can't rename \"" + std::string(FileName(path)) + "\"", .body = error });
        }
        // Back to the file system's names, the refused ones too.
        treeDirty = true;
    }

    void Application::EnableFilesPanel()
    {
        // The folder to show: --files, else the home folder, else the working folder.
        std::filesystem::path root = m_filesRoot;
        if (root.empty())
        {
#ifdef _WIN32
            wchar_t* home = nullptr;
            size_t length = 0;
            if (_wdupenv_s(&home, &length, L"USERPROFILE") == 0 && home)
                root = home;
            std::free(home);
#else
            if (const char* home = std::getenv("HOME"))
                root = home;
#endif
        }
        std::error_code ec;
        if (root.empty() || !std::filesystem::is_directory(root, ec))
            root = std::filesystem::current_path(ec);
        root = std::filesystem::absolute(root, ec);
        const std::u8string name = (root.has_filename() ? root.filename() : root).u8string();
        rootName = std::string(name.begin(), name.end());

        files     = std::make_unique<MemoryFileSystem>(root);
        fileIcons = std::make_unique<FileIcons>(*textSystem, 48);
        openFolders   = { "" };
        currentFolder.clear();
        filesGeneration = files->Generation();
        treeDirty       = true;
        gridFilterDirty = true;

        // Ghost icon that follows the mouse cursor during the drag. The
        // payload identifies the icon for either source view (grid or tree).
        auto ghost = [this](const tachyon::DragVisualContext& visual) {
            std::string path;
            if (!PayloadPath(visual.payloadValue, path)) return;
            const bool isFolder = IsFolderPayload(visual.payloadValue) ||
                                  GridEntryAt(static_cast<int>(visual.payloadValue))->file.isFolder;
            const IconID icon = FileIcon(FileName(path), isFolder);
            if (icon == IconID::Invalid) return;
            if (BeginCursorGhostWindow(showcase.dragGhostWindowID, visual.screenPos))
            {
                AddGhostIcon(icon);
            }
            EndGhostWindow();
        };

        gridDnD.payloadType = kGridPayload;
        // The item ID's sub-ID index is a position in gridEntries; folders and files drag
        // alike, and both go by their path once dropped.
        gridDnD.getPayload = [](tachyon::ID id) -> uintptr_t {
            const int i = GridItemIndex(id);
            return GridEntryAt(i) ? static_cast<uintptr_t>(i) : kNoPayload;
        };
        // A folder takes what's dropped on it; files and the background route it into the
        // folder on view (where a file from this grid already is).
        auto dropFolder = [](tachyon::ID targetID) {
            const GridEntry* target = GridEntryAt(GridItemIndex(targetID));
            return (target && target->file.isFolder) ? JoinPath(currentFolder, target->file.name) : currentFolder;
        };
        gridDnD.canDrop = [dropFolder](uintptr_t payloadValue, tachyon::ID targetID) {
            return CanDropInto(payloadValue, dropFolder(targetID));
        };
        gridDnD.visualizer = ghost;
        gridDnD.onDrop = [this, dropFolder](uintptr_t payloadValue, tachyon::ID targetID) {
            MoveFileInto(payloadValue, dropFolder(targetID));
        };

        // The folder tree shares the grid's payload space: rows drag their
        // tagged flat index and accept anything the grid drags. Every drop
        // zone on a row means "into this folder".
        folderDnD.payloadType    = kGridPayload;
        folderDnD.dropInsideOnly = true;
        folderDnD.getPayload = [](tachyon::ID rowID) -> uintptr_t {
            const int flat = RowFlat(rowID);
            return flat > 0 ? FolderPayload(flat) : kNoPayload;   // the root stays where it is
        };
        folderDnD.canDrop = [](uintptr_t payloadValue, tachyon::ID targetID) {
            const int dst = RowFlat(targetID);
            return dst >= 0 && CanDropInto(payloadValue, folderPaths[dst]);
        };
        folderDnD.visualizer = ghost;
        folderDnD.onReorder = [this](uintptr_t srcPayload, tachyon::ID targetID, TreeDropPosition) {
            const int dst = RowFlat(targetID);
            if (dst >= 0)
                MoveFileInto(srcPayload, folderPaths[dst]);
        };
        folderTreeState.rowDragDrop = &folderDnD;
    }

    void Application::DisableFilesPanel()
    {
        folderRows.clear();
        folderPaths.clear();
        folderVisibleIdx.clear();
        gridEntries.clear();
        filteredFolderRows.clear();
        filteredFolderVisibleIdx.clear();
        filteredToMaster.clear();
        openFolders.clear();
        currentFolder.clear();
        folderTreeState.rowDragDrop = nullptr;
        fileIcons.reset();
        files.reset();
    }

    // Files dropped from another application go into the folder on view, in the copy only.
    // A folder isn't copied, nor a name already there.
    void Application::AddDroppedFiles(const std::vector<std::string>& paths)
    {
        if (!files)
            return;
        std::vector<std::string> skipped;
        for (const std::string& path : paths)
        {
            const std::filesystem::path disk(std::u8string(path.begin(), path.end()));
            const std::u8string name8 = disk.filename().u8string();
            const std::string name(name8.begin(), name8.end());
            std::error_code ec;
            const bool isFile = std::filesystem::is_regular_file(disk, ec);
            const uint64_t size = isFile ? std::filesystem::file_size(disk, ec) : 0;
            if (!isFile)
                skipped.push_back((name.empty() ? path : name) + " (not a file)");
            else if (!files->AddFile(JoinPath(currentFolder, name), ec ? 0 : size))
                skipped.push_back(name + " (the name is taken)");
        }
        if (!skipped.empty())
        {
            std::string body;
            for (const std::string& line : skipped)
                body += (body.empty() ? "" : "\n") + line;
            ShowToast(ToastDesc{ .severity = Severity::Warning, .title = "Not added", .body = body });
        }
    }

    // Folders in the showcase's own icons; a file in the OS's where there is one, else the
    // showcase's own, by the extension.
    IconID Application::FileIcon(std::string_view name, bool isFolder, bool open)
    {
        if (isFolder)
            return open ? showcase.folderOpenIcon : showcase.folderClosedIcon;
        if (const IconID os = fileIcons ? fileIcons->Get(name, false) : IconID::Invalid; os != IconID::Invalid)
            return os;
        const std::string ext = FileIcons::Extension(name);
        if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb")                     return showcase.fileModelIcon;
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif" || ext == ".bmp")    return showcase.fileImageIcon;
        if (ext == ".mp3" || ext == ".wav" || ext == ".ogg" || ext == ".flac")                     return showcase.fileAudioIcon;
        if (ext == ".mp4" || ext == ".mov" || ext == ".mkv" || ext == ".webm")                     return showcase.fileVideoIcon;
        if (ext == ".zip" || ext == ".7z" || ext == ".rar" || ext == ".gz" || ext == ".tar")       return showcase.fileArchiveIcon;
        return showcase.fileTextIcon;
    }

    void Application::RenderFilesPanel()
    {
        if (!files)
            return;

        // A rename moves the item to its new place in the sorted view once it's done.
        static bool wasRenaming = false;
        if (wasRenaming && !IsRenaming())
            ApplyFileRenames();
        wasRenaming = IsRenaming();

        // A folder opened or closed in the tree: its folders' rows are read in (or left).
        for (size_t i = 0; i < folderRows.size(); ++i)
            if (folderRows[i].isOpen != openFolders.contains(folderPaths[i]))
            {
                if (folderRows[i].isOpen) openFolders.insert(folderPaths[i]);
                else                      openFolders.erase(folderPaths[i]);
                treeDirty = true;
            }

        // Rebuilt on demand: only when the files, the tree or the search changed.
        if (files->Generation() != filesGeneration || treeDirty)
        {
            if (!IsRenaming())   // not under an open editor, whose buffer is a row's name
            {
                filesGeneration = files->Generation();
                RebuildFolderTree();
                treeDirty       = false;
                gridFilterDirty = true;
            }
        }
        if (gridFilterDirty && !IsRenaming())
        {
            filesQueryLower = SearchToLower(filesSearch);
            RebuildFolderFilter();
            RebuildGridVisible();
            gridFilterDirty = false;
        }

        const bool searching = !filesQueryLower.empty();

        // Horizontal split: folder tree (left) | draggable sizer | file grid (right).
        // flexFillRow (flex-basis:0) bounds the split to the panel height so the
        // grid's virtual content scrolls inside it instead of stretching the row.
        BeginHorizontal(idManager.AllocateDynamicID("filesSplit"),
                        theme.flexFillRowStyleRef,
                        tachyon::LayoutOverride::Auto().FlexGrow(1));

        if (searching)
        {
            // Reduced tree: only folders with search hits. Own state; clicks
            // map back to the master folder through filteredToMaster.
            for (size_t i = 0; i < filteredFolderRows.size(); ++i)
                filteredFolderRows[i].icon = FileIcon({}, true, filteredFolderRows[i].HasChildren());
            VirtualTreeView(idManager.AllocateDynamicID("filesFolderTree"),
                            filteredFolderRows, filteredFolderVisibleIdx, filteredFolderState,
                            theme.treeViewStyleRef,
                            tachyon::LayoutOverride::Auto().FixedWidth(CssPx(folderTreeWidth)).NoGrow());

            const int selFiltered = SelectedFlat(filteredFolderState, filteredFolderVisibleIdx);
            const int selMaster   = (selFiltered >= 0 && selFiltered < int(filteredToMaster.size()))
                                  ? filteredToMaster[selFiltered] : -1;
            if (selMaster >= 0 && folderPaths[selMaster] != currentFolder)
            {
                currentFolder = folderPaths[selMaster];
                RebuildGridVisible();
            }
        }
        else
        {
            // Open-folder icon while expanded or hovered as a drop target. The
            // rename pointer is refreshed every frame because rebuilds move rows;
            // the root's name is the disk folder's, which stays.
            const int dropFlat = guiStateManager.Drag().IsDragging() ? RowFlat(TreeDropTargetRow()) : -1;
            for (size_t i = 0; i < folderRows.size(); ++i)
            {
                auto& row = folderRows[i];
                row.icon = FileIcon(row.name, true, (row.HasChildren() && row.isOpen) || int(i) == dropFlat);
                row.renameBuffer = i == 0 ? nullptr : &row.name;
            }

            VirtualTreeView(idManager.AllocateDynamicID("filesFolderTree"),
                            folderRows, folderVisibleIdx, folderTreeState,
                            theme.treeViewStyleRef,
                            tachyon::LayoutOverride::Auto().FixedWidth(CssPx(folderTreeWidth)).NoGrow());

            const int selFlat = SelectedFlat(folderTreeState, folderVisibleIdx);
            if (selFlat >= 0 && folderPaths[selFlat] != currentFolder)
            {
                currentFolder = folderPaths[selFlat];
                gridFilterDirty = true;
            }
            // First frame, and after a rebuild: highlight the folder on view (the sub-ID
            // needs the range the table just allocated).
            else if (folderTreeState.selectedRowID == tachyon::ID::Invalid())
            {
                for (size_t i = 0; i < folderVisibleIdx.size(); ++i)
                    if (folderPaths[folderVisibleIdx[i]] == currentFolder)
                    {
                        folderTreeState.selectedRowID = folderTreeState.RowIDForIndex(static_cast<int>(i));
                        break;
                    }
            }
        }

        // 4px draggable sizer reusing the dock-sizer style (same cursor and hit
        // region as the dock splitter).
        {
            tachyon::ID sizerID = idManager.AllocateDynamicID("filesSizer");
            tachyon::Element sizer{};
            sizer.id       = sizerID;
            sizer.styleRef = theme.dockSizerHorzStyleRef;
            sizer.state    = guiStateManager.GetCurrentState(sizerID);
            sizer.eventHandler = tachyon::MakeHorizontalSizerHandler(
                guiStateManager, elementStorage, folderTreeWidth, 120, 480, nullptr, nullptr, false, styleManager.GetUIScale());
            auto sRes = elementStorage.Add(std::move(sizer), tachyon::LayoutOverride::FixedWidth(4));
            VERIFY(sRes.status);
            VERIFY(windowRegistry.Add(sRes.ref));
        }

        // Right pane: a toolbar (path, search, grid/list) over the virtualized file grid.
        // flex-fill-col (flex-basis:0) bounds it to its share of the split width,
        // otherwise the stretched cells floor its min width and it can't shrink.
        // MinHeight(0) bounds it to the split height so the grid scrolls.
        // It takes files dragged from other applications, into the folder on view.
        const tachyon::ID gridPaneID = idManager.AllocateDynamicID("filesGridPane");
        {
            tachyon::Element pane{ .id = gridPaneID, .styleRef = theme.flexFillColStyleRef };
            pane.state = guiStateManager.GetCurrentState(gridPaneID);
            pane.eventHandler = [this](tachyon::ElementEvent&& event) -> bool
            {
                if (event.type == tachyon::EventType::FileDragOver)
                    return true;
                if (event.type != tachyon::EventType::FileDrop || event.state.droppedFiles.empty())
                    return false;   // text isn't a file
                AddDroppedFiles(event.state.droppedFiles);
                return true;
            };
            RegisterElement(std::move(pane), tachyon::LayoutOverride::Auto().FlexGrow(1).MinHeight(0));
            VERIFY(elementStorage.BeginChildren());
        }

        BeginHorizontal(idManager.AllocateDynamicID("filesToolbar"), filesToolbarStyleRef,
                        tachyon::LayoutOverride::Auto().NoShrink());
        {
            // The open folder's path, from the root; a segment opens that folder.
            std::vector<std::string> path{ rootName };
            std::vector<std::string> pathFolders{ std::string() };
            for (size_t start = 0; !currentFolder.empty() && start <= currentFolder.size();)
            {
                const size_t slash = std::min(currentFolder.find('/', start), currentFolder.size());
                path.push_back(currentFolder.substr(start, slash - start));
                pathFolders.push_back(currentFolder.substr(0, slash));
                start = slash + 1;
            }
            Breadcrumb(idManager.AllocateDynamicID("filesPath"), path,
                       [pathFolders](int segment) {
                           currentFolder = pathFolders[size_t(segment)];
                           folderTreeState.selectedRowID = tachyon::ID::Invalid();   // re-selected from currentFolder
                           gridFilterDirty = true;
                       },
                       tachyon::LayoutOverride{}.FlexGrow(1));

            if (SearchField(idManager.AllocateDynamicID("filesSearch"), filesSearch, showcase.findIcon,
                            TextEditBehavior::ContinuousUpdate,
                            tachyon::LayoutOverride{}.Width(150).NoGrow()))
                gridFilterDirty = true;
            Toggle(idManager.AllocateDynamicID("filesGridMode"), !filesListMode, showcase.layoutGridIcon,
                   [](bool) { filesListMode = false; }, iconToggleStyleRef);
            SetTooltip("Grid");
            Toggle(idManager.AllocateDynamicID("filesListMode"), filesListMode, showcase.listIcon,
                   [](bool) { filesListMode = true; }, iconToggleStyleRef);
            SetTooltip("List");
        }
        EndHorizontal();

        if (FileDragTargetID() == gridPaneID)
            Banner(idManager.AllocateDynamicID("filesDropHere"), Severity::Info,
                   "Drop to add to " + (currentFolder.empty() ? rootName : std::string(FileName(currentFolder))),
                   {}, tachyon::LayoutOverride::Auto().NoShrink());

        const bool listMode = filesListMode;
        const int  cellW    = styleManager.Px(96);
        const int  cellH    = styleManager.Px(86);
        const int  rowH     = std::max(1, theme.gridViewListRowHeight);   // measured list row height

        // Grid items: 34 px icons, the showcase's own tinted by their kind (folders keep
        // their color, and the OS's icons are pictures in their own colors).
        auto itemStyleFor = [this](IconID icon) -> StyleRef {
            const char* color = (icon == showcase.fileModelIcon) ? "--text-link"
                              : (icon == showcase.fileImageIcon) ? "--ok"
                              :                                "--text-2";
            Style def = styleManager.ToPixels(theme.gridViewItemBaseStyle);
            def.iconSize = static_cast<uint16_t>(styleManager.Px(34));
            for (auto& st : def.states)
                st.iconColor = ColorStyle::Create(theme.GetColor(color));
            return styleManager.Register(def, StyleUnits::Pixels);   // Register dedupes, so per frame is cheap
        };

        // Only the visible rows are built, so the count can run into the thousands.
        // List mode forces one column by making the cell wider than any viewport.
        const tachyon::int2 cellSize = listMode ? tachyon::int2{ 1 << 20, rowH }
                                            : tachyon::int2{ cellW, cellH };
        VirtualGridView(idManager.AllocateDynamicID("filesGrid"), gridScrollState,
                        static_cast<int>(gridEntries.size()), cellSize,
                        [this, listMode, itemStyleFor, cellW, cellH, rowH](int i, tachyon::ID itemID)
                        {
                            if (i < 0 || i >= int(gridEntries.size())) return;
                            GridEntry& entry = gridEntries[size_t(i)];

                            // Open-folder icon while hovered by a drag this folder accepts.
                            const bool dropTarget = entry.file.isFolder
                                && guiStateManager.Drag().IsDropTargetFor(itemID, kGridPayload)
                                && CanDropInto(guiStateManager.Drag().GetPayloadValue(), JoinPath(currentFolder, entry.file.name));
                            const IconID icon = FileIcon(entry.file.name, entry.file.isFolder, dropTarget);
                            std::string_view name = entry.file.name;

                            if (listMode)
                                GridViewItem(itemID, { icon, name }, tachyon::ElementState::None,
                                             &entry.renameBuffer,
                                             theme.gridViewListItemStyleRef,
                                             LayoutOverride{}.Height(Pixels{ rowH }).FlexGrow(1));
                            else
                                // Stretched width: the grid's columns split the
                                // viewport evenly, so the cells fill each row
                                // instead of leaving a gap on the right.
                                GridViewItem(itemID, { icon, name }, tachyon::ElementState::None,
                                             &entry.renameBuffer,
                                             itemStyleFor(icon),
                                             LayoutOverride{}.Size(
                                                 Pixels{ std::max(cellW, int(gridScrollState.cellStretchedW)) },
                                                 Pixels{ cellH }));
                        },
                        nullptr,                   // selectedIndex (multi-select used instead)
                        &showcase.gridViewMultiSelect,
                        &gridDnD,
                        {}, {},                    // default scroll / row styles
                        LayoutOverride{}.FlexGrow(1));

        EndVertical();

        EndHorizontal();
    }

}
