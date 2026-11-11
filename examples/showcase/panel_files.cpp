#include "application.h"
#include <gui_controls/gui_file_icons.h>
#include <gui_controls/gui_file_system.h>
#include <gui_controls/gui_file_thumbnails.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <optional>
#include <unordered_set>

//
// This file is a file browser panel demo, meant to test and demonstrate the grid view and
// drag & drop features - including dragging files and folders between the grid and the
// folder tree, and dropping them in from other applications. It browses an IFileSystem: a
// copy of a folder on the disk (the showcase's workdirectory, or the one given with
// --files), read as it's browsed. Moves, renames and dropped files change only the copy;
// the disk is never written. Files on the disk show their thumbnails, as Explorer does.
//

namespace tachyon
{
    constexpr uint32_t kGridPayload = 0x47524944; // 'GRID'
    constexpr int      kFileIconSize = 34;        // the grid's icons, CSS pixels

    // Drag payload: an entry's index in gridEntries, or a folder's flat folderRows index
    // tagged with the top bit (folders can be dragged from the grid and from the folder
    // tree alike).
    constexpr uintptr_t kPayloadFolderBit = uintptr_t(1) << (sizeof(uintptr_t) * 8 - 1);
    constexpr uintptr_t kNoPayload        = ~uintptr_t(0);
    static uintptr_t FolderPayload(int flat)        { return kPayloadFolderBit | static_cast<uintptr_t>(flat); }
    static bool      IsFolderPayload(uintptr_t v)   { return (v & kPayloadFolderBit) != 0; }
    static int       FolderPayloadIndex(uintptr_t v){ return static_cast<int>(v & ~kPayloadFolderBit); }

    // Visible position encoded in a grid item's ranged sub-ID (-1 if not one).
    static int GridItemIndex(tachyon::ID id)
    {
        if (id.GetMode() != tachyon::IDMode::SubRange) return -1;
        return static_cast<int>(id.value.subRangeParts.index);
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

    // Folders first, then by name (case-insensitive): the view is sorted, not ordered by hand.
    static bool EntryBefore(const FileEntry& a, const FileEntry& b)
    {
        if (a.isFolder != b.isFolder)
            return a.isFolder;
        return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end(), [](char x, char y) {
            return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
        });
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

    // A Files panel's data: made the first time it shows, kept while it's hidden but for the
    // icons it made in the text system (DisableFilesPanel).
    struct FilesPanel
    {
        std::unique_ptr<MemoryFileSystem> files;
        std::unique_ptr<FileIcons>        fileIcons;       // the OS's icons, where there are any
        std::unique_ptr<FileThumbnails>   fileThumbnails;  // and its pictures of what's in the files on the disk
        uint64_t                          filesGeneration = 0;

        tachyon::ItemDragDropConfig   gridDnD;
        tachyon::ItemDragDropConfig   folderDnD;
        tachyon::GridViewVirtualState gridScrollState;
        tachyon::MultiSelectState     gridMultiSelect;

        // What the grid shows for the folder on view: its folders, then its files, each by name,
        // reduced to the ones that match the search.
        struct GridEntry {
            FileEntry   file;
            std::string renameBuffer;   // a rename edits this; it goes to the file system once it's done
        };
        std::vector<GridEntry> gridEntries;
        // The grid's items built this frame: their IDs, and their entries' indices.
        std::vector<std::pair<tachyon::ID, int>> gridItemIDs;
        // The folder a drag from another application would drop into now; none while there's none.
        std::optional<std::string> fileDropFolder;
        // The window it was last built in, whose documents a file it opens goes to.
        tachyon::ID windowID = tachyon::ID::Invalid();
        // Grid of icons, or a list (the toolbar's grid/list toggles).
        bool filesListMode = false;
        bool wasRenaming = false;

        // Folder tree left of the grid (flat VirtualTreeView rows): the root, the folders of the
        // open folders, and one level further, so a closed folder shows whether it holds any.
        // folderPaths is parallel to folderRows.
        std::vector<tachyon::TreeViewRow> folderRows;
        std::vector<std::string>          folderPaths;
        std::vector<int>                  folderVisibleIdx;
        tachyon::TableState               folderTreeState;
        int                               folderTreeWidth = 180; // CSS pixels
        std::unordered_set<std::string>   openFolders;           // paths
        std::string                       currentFolder;         // the path of the folder on view
        std::string                       rootName;
        bool                              treeDirty = true;

        // Search state, rebuilt only when the query, the files or the folder on view change.
        //   filteredFolder* - the folder tree reduced to folders holding a match (plus their
        //                     ancestors), shown while a query is active.
        std::string                       filesSearch;
        std::string                       filesQueryLower;
        bool                              gridFilterDirty = true;
        std::vector<tachyon::TreeViewRow> filteredFolderRows;
        std::vector<int>                  filteredFolderVisibleIdx;
        tachyon::TableState               filteredFolderState;
        std::vector<int>                  filteredToMaster; // filtered flat idx -> master flat idx

        const GridEntry* GridEntryAt(int visiblePos) const
        {
            return (visiblePos >= 0 && visiblePos < static_cast<int>(gridEntries.size())) ? &gridEntries[visiblePos] : nullptr;
        }

        // The flat row index of a tree row's ID (-1 if not one of the rows).
        int RowFlat(tachyon::ID rowID) const
        {
            if (rowID.GetMode() != tachyon::IDMode::SubRange) return -1;
            const int visIdx = static_cast<int>(rowID.value.subRangeParts.index);
            return (visIdx >= 0 && visIdx < static_cast<int>(folderVisibleIdx.size())) ? folderVisibleIdx[visIdx] : -1;
        }

        std::vector<FileEntry> SortedList(std::string_view folder) const
        {
            std::vector<FileEntry> list = files->List(folder);
            std::sort(list.begin(), list.end(), EntryBefore);
            return list;
        }

        // After a folder moved or was renamed, the folder on view and the open folders follow it.
        void FolderMoved(const std::string& from, const std::string& to)
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
        void AddFolderRows(const std::string& path, int depth, bool shown)
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

        void RebuildFolderTree()
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
        void RebuildGridVisible()
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
        void RebuildFolderFilter()
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
        bool PayloadPath(uintptr_t payload, std::string& path) const
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

        // The folder at `screenPos`: a folder in the grid or a row of the tree, else the folder
        // on view (a file in the grid, the grid's background).
        std::string FolderAt(const ElementStorage& storage, tachyon::int2 screenPos) const
        {
            auto under = [&](tachyon::ID id) {
                const tachyon::ElementRef ref = storage.FindElementRefByID(id);
                return ref.IsValid() && storage.ElementRectOnScreen(ref).Contains(screenPos);
            };
            for (const auto& [id, index] : gridItemIDs)
                if (index < int(gridEntries.size()) && under(id))
                    return gridEntries[size_t(index)].file.isFolder ? JoinPath(currentFolder, gridEntries[size_t(index)].file.name)
                                                                    : currentFolder;
            const bool searching = !filesQueryLower.empty();
            const tachyon::TableState& state      = searching ? filteredFolderState : folderTreeState;
            const std::vector<int>&    visibleIdx = searching ? filteredFolderVisibleIdx : folderVisibleIdx;
            for (size_t vi = 0; vi < visibleIdx.size(); ++vi)
                if (under(state.RowIDForIndex(static_cast<int>(vi))))
                {
                    const int flat = searching ? filteredToMaster[size_t(visibleIdx[vi])] : visibleIdx[vi];
                    return folderPaths[size_t(flat)];
                }
            return currentFolder;
        }

        // Whether what `payload` stands for may go into the folder `folder`: not where it is
        // already, and a folder not into itself or its own subtree.
        bool CanDropInto(uintptr_t payload, const std::string& folder) const
        {
            std::string path;
            if (!PayloadPath(payload, path) || ParentPath(path) == folder) return false;
            std::string inside = folder;
            return !Retarget(inside, path, "");
        }
    };

    ID Application::CreateFilesPanel(std::unordered_map<ID, Panel>& panels, std::string title)
    {
        auto panel = std::make_shared<FilesPanel>();
        m_filesPanels.push_back(panel);
        return CreatePanel(panels, std::move(title), showcase.panelFilesIcon, [this, panel] { RenderFilesPanel(*panel); }, {},
                           [this, panel] { EnableFilesPanel(*panel); }, [this, panel] { DisableFilesPanel(*panel); });
    }

    void Application::MoveFileInto(FilesPanel& p, uintptr_t payload, const std::string& folder)
    {
        std::string path;
        if (!p.CanDropInto(payload, folder) || !p.PayloadPath(payload, path)) return;
        std::string error;
        if (!p.files->Move(path, folder, error))
        {
            ShowToast(ToastDesc{ .severity = Severity::Error, .title = "Can't move \"" + std::string(FileName(path)) + "\"", .body = error });
            return;
        }
        p.FolderMoved(path, JoinPath(folder, FileName(path)));
    }

    // Renames whose editors closed: a name that's changed goes to the file system; one it
    // refuses goes back, with a toast saying why.
    void Application::ApplyFileRenames(FilesPanel& p)
    {
        std::vector<std::pair<std::string, std::string>> renames; // path, new name
        for (const FilesPanel::GridEntry& entry : p.gridEntries)
            if (entry.renameBuffer != entry.file.name)
                renames.emplace_back(JoinPath(p.currentFolder, entry.file.name), entry.renameBuffer);
        for (size_t i = 1; i < p.folderRows.size(); ++i)
            if (p.folderRows[i].name != FileName(p.folderPaths[i]))
                renames.emplace_back(p.folderPaths[i], p.folderRows[i].name);
        for (const auto& [path, name] : renames)
        {
            std::string error;
            if (p.files->Rename(path, name, error))
                p.FolderMoved(path, JoinPath(ParentPath(path), name));
            else
                ShowToast(ToastDesc{ .severity = Severity::Error, .title = "Can't rename \"" + std::string(FileName(path)) + "\"", .body = error });
        }
        // Back to the file system's names, the refused ones too.
        p.treeDirty = true;
    }

    void Application::EnableFilesPanel(FilesPanel& p)
    {
        p.fileIcons      = std::make_unique<FileIcons>(*textSystem, 48);
        p.fileThumbnails = std::make_unique<FileThumbnails>(*textSystem);
        // A thumbnail read while the app sleeps: draw it.
        p.fileThumbnails->SetOnRead([this] { RequestRedraw(); });
        if (p.files)
            return;   // made already: its files, folders and search stay

        // The folder to show: --files, else the showcase's workdirectory, else the working folder.
        std::filesystem::path root = m_filesRoot;
        if (root.empty())
        {
            const std::string& resources = GetResourceRoot();
            root = std::filesystem::path(std::u8string(resources.begin(), resources.end()));
        }
        std::error_code ec;
        if (root.empty() || !std::filesystem::is_directory(root, ec))
            root = std::filesystem::current_path(ec);
        root = std::filesystem::absolute(root, ec).lexically_normal();
        if (!root.has_filename() && root.has_relative_path())
            root = root.parent_path();   // named after its last folder, not the separator after it
        const std::u8string name = (root.has_filename() ? root.filename() : root).u8string();
        p.rootName = std::string(name.begin(), name.end());

        p.files         = std::make_unique<MemoryFileSystem>(root);
        // Some documents to open with a double-click, in the copy only.
        if (m_filesRoot.empty())
            for (const char* file : { "Forest.level", "Desert.level", "Canyon.level", "Terrain.mat", "Rock.prefab", "Walk.anim", "Hud.layout" })
                (void)p.files->AddFile(std::string("documents/") + file);
        p.openFolders   = { "" };
        p.currentFolder.clear();
        p.filesGeneration = p.files->Generation();
        p.treeDirty       = true;
        p.gridFilterDirty = true;

        // Ghost icon that follows the mouse cursor during the drag. The
        // payload identifies the icon for either source view (grid or tree).
        auto ghost = [this, &p](const tachyon::DragVisualContext& visual) {
            std::string path;
            if (!p.PayloadPath(visual.payloadValue, path)) return;
            const FilesPanel::GridEntry* entry = IsFolderPayload(visual.payloadValue) ? nullptr : p.GridEntryAt(static_cast<int>(visual.payloadValue));
            const IconID icon = entry ? FileIcon(p, entry->file) : FileIcon(p, FileName(path), true);
            if (icon == IconID::Invalid) return;
            if (BeginCursorGhostWindow(showcase.dragGhostWindowID, visual.screenPos))
            {
                AddGhostIcon(icon);
            }
            EndGhostWindow();
        };

        // A double-click (or Enter) opens a folder, or a file as a document in this window.
        p.gridScrollState.onOpen = [this, &p](tachyon::ID id) {
            const FilesPanel::GridEntry* entry = p.GridEntryAt(GridItemIndex(id));
            if (!entry)
                return;
            if (entry->file.isFolder)
            {
                p.currentFolder = JoinPath(p.currentFolder, entry->file.name);
                p.openFolders.insert(std::string(ParentPath(p.currentFolder)));
                p.folderTreeState.selectedRowID = tachyon::ID::Invalid();   // re-selected from currentFolder
                p.treeDirty       = true;
                p.gridFilterDirty = true;
                return;
            }
            OpenFileAsDocument(DocumentsOfWindow(p.windowID), entry->file.name, FileIcon(p, entry->file));
        };

        p.gridDnD.payloadType = kGridPayload;
        // The item ID's sub-ID index is a position in gridEntries; folders and files drag
        // alike, and both go by their path once dropped.
        p.gridDnD.getPayload = [&p](tachyon::ID id) -> uintptr_t {
            const int i = GridItemIndex(id);
            return p.GridEntryAt(i) ? static_cast<uintptr_t>(i) : kNoPayload;
        };
        // A folder takes what's dropped on it; files and the background route it into the
        // folder on view (where a file from this grid already is).
        auto dropFolder = [&p](tachyon::ID targetID) {
            const FilesPanel::GridEntry* target = p.GridEntryAt(GridItemIndex(targetID));
            return (target && target->file.isFolder) ? JoinPath(p.currentFolder, target->file.name) : p.currentFolder;
        };
        p.gridDnD.canDrop = [&p, dropFolder](uintptr_t payloadValue, tachyon::ID targetID) {
            return p.CanDropInto(payloadValue, dropFolder(targetID));
        };
        p.gridDnD.visualizer = ghost;
        p.gridDnD.onDrop = [this, &p, dropFolder](uintptr_t payloadValue, tachyon::ID targetID) {
            MoveFileInto(p, payloadValue, dropFolder(targetID));
        };

        // The folder tree shares the grid's payload space: rows drag their
        // tagged flat index and accept anything the grid drags. Every drop
        // zone on a row means "into this folder".
        p.folderDnD.payloadType    = kGridPayload;
        p.folderDnD.dropInsideOnly = true;
        p.folderDnD.getPayload = [&p](tachyon::ID rowID) -> uintptr_t {
            const int flat = p.RowFlat(rowID);
            return flat > 0 ? FolderPayload(flat) : kNoPayload;   // the root stays where it is
        };
        p.folderDnD.canDrop = [&p](uintptr_t payloadValue, tachyon::ID targetID) {
            const int dst = p.RowFlat(targetID);
            return dst >= 0 && p.CanDropInto(payloadValue, p.folderPaths[dst]);
        };
        p.folderDnD.visualizer = ghost;
        p.folderDnD.onReorder = [this, &p](uintptr_t srcPayload, tachyon::ID targetID, TreeDropPosition) {
            const int dst = p.RowFlat(targetID);
            if (dst >= 0)
                MoveFileInto(p, srcPayload, p.folderPaths[dst]);
        };
        p.folderTreeState.rowDragDrop = &p.folderDnD;
    }

    // Hidden: the icons it made in the text system go (EnableFilesPanel makes them again).
    void Application::DisableFilesPanel(FilesPanel& p)
    {
        p.gridItemIDs.clear();
        p.fileDropFolder.reset();
        p.fileThumbnails.reset();
        p.fileIcons.reset();
    }

    // Files and folders dropped from another application are copied into `folder` through
    // the file system; here that's the copy in memory, which reads a dropped folder from the
    // disk as it's browsed. What can't be added is said, with why.
    void Application::AddDroppedFiles(FilesPanel& p, const std::vector<std::string>& paths, const std::string& folder)
    {
        if (!p.files)
            return;
        std::string refused;
        for (const std::string& path : paths)
        {
            std::string error;
            if (!p.files->Import(std::filesystem::path(std::u8string(path.begin(), path.end())), folder, error))
                refused += (refused.empty() ? "" : "\n") + error;
        }
        if (!refused.empty())
            ShowToast(ToastDesc{ .severity = Severity::Warning,
                                 .title = "Not added to " + (folder.empty() ? p.rootName : std::string(FileName(folder))),
                                 .body = refused });
    }

    // Folders in the showcase's own icons; a file in the OS's where there is one, else the
    // showcase's own, by the extension.
    IconID Application::FileIcon(const FilesPanel& p, std::string_view name, bool isFolder, bool open)
    {
        if (isFolder)
            return open ? showcase.folderOpenIcon : showcase.folderClosedIcon;
        if (const IconID os = p.fileIcons ? p.fileIcons->Get(name, false) : IconID::Invalid; os != IconID::Invalid)
            return os;
        const std::string ext = FileIcons::Extension(name);
        if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb")                     return showcase.fileModelIcon;
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif" || ext == ".bmp")    return showcase.fileImageIcon;
        if (ext == ".mp3" || ext == ".wav" || ext == ".ogg" || ext == ".flac")                     return showcase.fileAudioIcon;
        if (ext == ".mp4" || ext == ".mov" || ext == ".mkv" || ext == ".webm")                     return showcase.fileVideoIcon;
        if (ext == ".zip" || ext == ".7z" || ext == ".rar" || ext == ".gz" || ext == ".tar")       return showcase.fileArchiveIcon;
        return showcase.fileTextIcon;
    }

    // A file on the disk in its thumbnail, at the grid's icon size, once it's read; until then,
    // and for a file without one, its type's icon.
    IconID Application::FileIcon(const FilesPanel& p, const FileEntry& file, bool open)
    {
        if (!file.isFolder && p.fileThumbnails)
            if (const IconID thumbnail = p.fileThumbnails->Get(file.disk, file.modified, uint32_t(styleManager.Px(kFileIconSize)));
                thumbnail != IconID::Invalid)
                return thumbnail;
        return FileIcon(p, file.name, file.isFolder, open);
    }

    void Application::RenderFilesPanel(FilesPanel& p)
    {
        if (!p.files)
            return;
        p.windowID = windowRegistry.GetCurrentWindowID();

        // A rename moves the item to its new place in the sorted view once it's done.
        if (p.wasRenaming && !IsRenaming())
            ApplyFileRenames(p);
        p.wasRenaming = IsRenaming();

        // A folder opened or closed in the tree: its folders' rows are read in (or left).
        for (size_t i = 0; i < p.folderRows.size(); ++i)
            if (p.folderRows[i].isOpen != p.openFolders.contains(p.folderPaths[i]))
            {
                if (p.folderRows[i].isOpen) p.openFolders.insert(p.folderPaths[i]);
                else                      p.openFolders.erase(p.folderPaths[i]);
                p.treeDirty = true;
            }

        // Rebuilt on demand: only when the files, the tree or the search changed.
        if (p.files->Generation() != p.filesGeneration || p.treeDirty)
        {
            if (!IsRenaming())   // not under an open editor, whose buffer is a row's name
            {
                p.filesGeneration = p.files->Generation();
                p.RebuildFolderTree();
                p.treeDirty       = false;
                p.gridFilterDirty = true;
            }
        }
        if (p.gridFilterDirty && !IsRenaming())
        {
            p.filesQueryLower = SearchToLower(p.filesSearch);
            p.RebuildFolderFilter();
            p.RebuildGridVisible();
            p.gridFilterDirty = false;
        }

        const bool searching = !p.filesQueryLower.empty();

        // Horizontal split: folder tree (left) | draggable sizer | file grid (right).
        // flexFillRow (flex-basis:0) bounds the split to the panel height so the
        // grid's virtual content scrolls inside it instead of stretching the row.
        // It takes files and folders dragged from other applications, into the folder under
        // the pointer (FolderAt).
        const tachyon::ID splitID = idManager.AllocateDynamicID("filesSplit");
        if (FileDragTargetID() != splitID)
            p.fileDropFolder.reset();
        {
            tachyon::Element split{ .id = splitID, .styleRef = theme.flexFillRowStyleRef };
            split.state = guiStateManager.GetCurrentState(splitID);
            split.eventHandler = [this, &p](tachyon::ElementEvent&& event) -> bool
            {
                if (event.type == tachyon::EventType::FileDragOver)
                {
                    p.fileDropFolder = p.FolderAt(elementStorage, event.state.screenPos);
                    return true;
                }
                if (event.type != tachyon::EventType::FileDrop || event.state.droppedFiles.empty())
                    return false;   // text isn't a file
                AddDroppedFiles(p, event.state.droppedFiles, p.FolderAt(elementStorage, event.state.screenPos));
                return true;
            };
            RegisterElement(std::move(split), tachyon::LayoutOverride::Auto().FlexGrow(1));
            VERIFY(elementStorage.BeginChildren());
        }

        if (searching)
        {
            // Reduced tree: only folders with search hits. Own state; clicks
            // map back to the master folder through filteredToMaster.
            for (size_t i = 0; i < p.filteredFolderRows.size(); ++i)
                p.filteredFolderRows[i].icon = FileIcon(p, {}, true, p.filteredFolderRows[i].HasChildren() ||
                                                                (p.fileDropFolder && p.folderPaths[size_t(p.filteredToMaster[i])] == *p.fileDropFolder));
            VirtualTreeView(idManager.AllocateDynamicID("filesFolderTree"),
                            p.filteredFolderRows, p.filteredFolderVisibleIdx, p.filteredFolderState,
                            theme.treeViewStyleRef,
                            tachyon::LayoutOverride::Auto().FixedWidth(CssPx(p.folderTreeWidth)).NoGrow());

            const int selFiltered = SelectedFlat(p.filteredFolderState, p.filteredFolderVisibleIdx);
            const int selMaster   = (selFiltered >= 0 && selFiltered < int(p.filteredToMaster.size()))
                                  ? p.filteredToMaster[selFiltered] : -1;
            if (selMaster >= 0 && p.folderPaths[selMaster] != p.currentFolder)
            {
                p.currentFolder = p.folderPaths[selMaster];
                p.RebuildGridVisible();
            }
        }
        else
        {
            // Open-folder icon while expanded or hovered as a drop target. The
            // rename pointer is refreshed every frame because rebuilds move rows;
            // the root's name is the disk folder's, which stays.
            const int dropFlat = guiStateManager.Drag().IsDragging() ? p.RowFlat(TreeDropTargetRow()) : -1;
            for (size_t i = 0; i < p.folderRows.size(); ++i)
            {
                auto& row = p.folderRows[i];
                row.icon = FileIcon(p, row.name, true, (row.HasChildren() && row.isOpen) || int(i) == dropFlat ||
                                                    (p.fileDropFolder && p.folderPaths[i] == *p.fileDropFolder));
                row.renameBuffer = i == 0 ? nullptr : &row.name;
            }

            VirtualTreeView(idManager.AllocateDynamicID("filesFolderTree"),
                            p.folderRows, p.folderVisibleIdx, p.folderTreeState,
                            theme.treeViewStyleRef,
                            tachyon::LayoutOverride::Auto().FixedWidth(CssPx(p.folderTreeWidth)).NoGrow());

            const int selFlat = SelectedFlat(p.folderTreeState, p.folderVisibleIdx);
            if (selFlat >= 0 && p.folderPaths[selFlat] != p.currentFolder)
            {
                p.currentFolder = p.folderPaths[selFlat];
                p.gridFilterDirty = true;
            }
            // First frame, and after a rebuild: highlight the folder on view (the sub-ID
            // needs the range the table just allocated).
            else if (p.folderTreeState.selectedRowID == tachyon::ID::Invalid())
            {
                for (size_t i = 0; i < p.folderVisibleIdx.size(); ++i)
                    if (p.folderPaths[p.folderVisibleIdx[i]] == p.currentFolder)
                    {
                        p.folderTreeState.selectedRowID = p.folderTreeState.RowIDForIndex(static_cast<int>(i));
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
                guiStateManager, elementStorage, p.folderTreeWidth, 120, 480, nullptr, nullptr, false, styleManager.GetUIScale());
            auto sRes = elementStorage.Add(std::move(sizer), tachyon::LayoutOverride::FixedWidth(4));
            VERIFY(sRes.status);
            VERIFY(windowRegistry.Add(sRes.ref));
        }

        // Right pane: a toolbar (path, search, grid/list) over the virtualized file grid.
        // flex-fill-col (flex-basis:0) bounds it to its share of the split width,
        // otherwise the stretched cells floor its min width and it can't shrink.
        // MinHeight(0) bounds it to the split height so the grid scrolls.
        BeginVertical(idManager.AllocateDynamicID("filesGridPane"),
                      theme.flexFillColStyleRef,
                      tachyon::LayoutOverride::Auto().FlexGrow(1).MinHeight(0));

        BeginHorizontal(idManager.AllocateDynamicID("filesToolbar"), filesToolbarStyleRef,
                        tachyon::LayoutOverride::Auto().NoShrink());
        {
            // The open folder's path, from the root; a segment opens that folder.
            std::vector<std::string> path{ p.rootName };
            std::vector<std::string> pathFolders{ std::string() };
            for (size_t start = 0; !p.currentFolder.empty() && start <= p.currentFolder.size();)
            {
                const size_t slash = std::min(p.currentFolder.find('/', start), p.currentFolder.size());
                path.push_back(p.currentFolder.substr(start, slash - start));
                pathFolders.push_back(p.currentFolder.substr(0, slash));
                start = slash + 1;
            }
            Breadcrumb(idManager.AllocateDynamicID("filesPath"), path,
                       [&p, pathFolders](int segment) {
                           p.currentFolder = pathFolders[size_t(segment)];
                           p.folderTreeState.selectedRowID = tachyon::ID::Invalid();   // re-selected from currentFolder
                           p.gridFilterDirty = true;
                       },
                       tachyon::LayoutOverride{}.FlexGrow(1));

            if (SearchField(idManager.AllocateDynamicID("filesSearch"), p.filesSearch, showcase.findIcon,
                            TextEditBehavior::ContinuousUpdate,
                            tachyon::LayoutOverride{}.Width(150).NoGrow()))
                p.gridFilterDirty = true;
            Toggle(idManager.AllocateDynamicID("filesGridMode"), !p.filesListMode, showcase.layoutGridIcon,
                   [&p](bool) { p.filesListMode = false; }, iconToggleStyleRef);
            SetTooltip("Grid");
            Toggle(idManager.AllocateDynamicID("filesListMode"), p.filesListMode, showcase.listIcon,
                   [&p](bool) { p.filesListMode = true; }, iconToggleStyleRef);
            SetTooltip("List");
        }
        EndHorizontal();

        const bool listMode = p.filesListMode;
        const int  cellW    = styleManager.Px(96);
        const int  cellH    = styleManager.Px(86);
        const int  rowH     = std::max(1, theme.gridViewListRowHeight);   // measured list row height

        // Grid items: 34 px icons, the showcase's own tinted by their kind (folders keep
        // their color, and the OS's icons and thumbnails are pictures in their own colors).
        auto itemStyleFor = [this](IconID icon) -> StyleRef {
            const char* color = (icon == showcase.fileModelIcon) ? "--text-link"
                              : (icon == showcase.fileImageIcon) ? "--ok"
                              :                                "--text-2";
            Style def = styleManager.ToPixels(theme.gridViewItemBaseStyle);
            def.iconSize = static_cast<uint16_t>(styleManager.Px(kFileIconSize));
            for (auto& st : def.states)
                st.iconColor = ColorStyle::Create(theme.GetColor(color));
            return styleManager.Register(def, StyleUnits::Pixels);   // Register dedupes, so per frame is cheap
        };

        // Only the visible rows are built, so the count can run into the thousands.
        // List mode forces one column by making the cell wider than any viewport.
        const tachyon::int2 cellSize = listMode ? tachyon::int2{ 1 << 20, rowH }
                                            : tachyon::int2{ cellW, cellH };
        p.gridItemIDs.clear();
        VirtualGridView(idManager.AllocateDynamicID("filesGrid"), p.gridScrollState,
                        static_cast<int>(p.gridEntries.size()), cellSize,
                        [this, &p, listMode, itemStyleFor, cellW, cellH, rowH](int i, tachyon::ID itemID)
                        {
                            if (i < 0 || i >= int(p.gridEntries.size())) return;
                            FilesPanel::GridEntry& entry = p.gridEntries[size_t(i)];
                            p.gridItemIDs.emplace_back(itemID, i);

                            // Open-folder icon while hovered by a drag this folder accepts, or one
                            // from another application would drop into it.
                            const std::string path = JoinPath(p.currentFolder, entry.file.name);
                            const bool dropTarget = entry.file.isFolder &&
                                ((guiStateManager.Drag().IsDropTargetFor(itemID, kGridPayload) &&
                                  p.CanDropInto(guiStateManager.Drag().GetPayloadValue(), path)) ||
                                 (p.fileDropFolder && *p.fileDropFolder == path));
                            const IconID icon = FileIcon(p, entry.file, dropTarget);
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
                                                 Pixels{ std::max(cellW, int(p.gridScrollState.cellStretchedW)) },
                                                 Pixels{ cellH }));
                        },
                        nullptr,                   // selectedIndex (multi-select used instead)
                        &p.gridMultiSelect,
                        &p.gridDnD,
                        {}, {},                    // default scroll / row styles
                        LayoutOverride{}.FlexGrow(1));

        EndVertical();

        EndHorizontal();
    }

}
