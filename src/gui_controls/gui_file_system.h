#pragma once

// File systems a file view browses and changes, behind one interface: a folder's entries,
// moving and renaming. A view relists when Generation() changes.
//
// Paths are relative to the file system's root, with '/' between names; "" is the root.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tachyon
{
    /// @addtogroup controls_data
    /// @{

    /// A file or folder in a folder.
    struct FileEntry
    {
        std::string name;
        bool        isFolder = false;
        uint64_t    size     = 0;   ///< bytes; 0 for a folder
    };

    /// What a file view reads and changes. Implement it over the disk, an archive, an asset
    /// database; MemoryFileSystem keeps it all in memory.
    class IFileSystem
    {
    public:
        virtual ~IFileSystem() = default;

        /// The entries in the folder at `path`, in no particular order; none when there's no
        /// such folder.
        virtual std::vector<FileEntry> List(std::string_view path) = 0;
        /// Moves the file or folder at `path` into the folder `toFolder`, keeping its name.
        /// False, with `error` saying why, when it can't: there's no such file or folder, a
        /// folder would go into itself, or the name is taken there. Moving it to where it is
        /// changes nothing.
        virtual bool Move(std::string_view path, std::string_view toFolder, std::string& error) = 0;
        /// Renames the file or folder at `path` in its folder. False, with `error` saying why,
        /// when it can't: there's no such file or folder, the name is empty, has a '/' or '\',
        /// is "." or "..", or is taken.
        virtual bool Rename(std::string_view path, std::string_view newName, std::string& error) = 0;
        /// Changes whenever what List returns may have: after a move or a rename.
        virtual uint64_t Generation() const = 0;
    };

    /// `folder` and `name` as one path.
    std::string JoinPath(std::string_view folder, std::string_view name);
    /// The folder `path` is in; "" for an entry in the root (and for the root).
    std::string_view ParentPath(std::string_view path);
    /// The last name in `path`.
    std::string_view FileName(std::string_view path);

    /// A file system kept in memory. Filled with AddFolder / AddFile, or a copy of a folder on
    /// the disk: each folder's entries are read from the disk the first time they're needed
    /// (listed, or looked into by a move). Moves and renames change only the copy.
    class MemoryFileSystem final : public IFileSystem
    {
    public:
        MemoryFileSystem();
        /// A copy of the disk folder `diskRoot`, read as it's browsed. Entries that can't be
        /// read (no permission) are left out.
        explicit MemoryFileSystem(std::filesystem::path diskRoot);
        ~MemoryFileSystem() override;

        /// Adds the folder at `path`, and the folders on the way to it. False when a file is
        /// in the way.
        bool AddFolder(std::string_view path);
        /// Adds a file at `path` (and the folders on the way to it), `size` bytes. False when
        /// a file is in the way or something is already there.
        bool AddFile(std::string_view path, uint64_t size = 0);

        std::vector<FileEntry> List(std::string_view path) override;
        bool Move(std::string_view path, std::string_view toFolder, std::string& error) override;
        bool Rename(std::string_view path, std::string_view newName, std::string& error) override;
        uint64_t Generation() const override { return m_generation; }

    private:
        struct Node;
        Node* Find(std::string_view path);
        Node* AddPath(std::string_view path, bool isFolder);
        void  Load(Node& folder);

        std::unique_ptr<Node> m_root;
        uint64_t              m_generation = 0;
    };

    /// @}
}
