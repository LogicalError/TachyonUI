#include <gui_controls/gui_file_system.h>

#include <algorithm>
#include <system_error>

namespace tachyon
{
    struct MemoryFileSystem::Node
    {
        std::string                        name;
        bool                               isFolder = false;
        uint64_t                           size     = 0;
        std::filesystem::path              disk;            ///< the disk folder it copies; empty when none
        bool                               loaded   = true; ///< its entries are read from the disk
        Node*                              parent   = nullptr;
        std::vector<std::unique_ptr<Node>> children;

        Node* Child(std::string_view childName) const
        {
            for (const auto& c : children)
                if (c->name == childName)
                    return c.get();
            return nullptr;
        }
    };

    std::string JoinPath(std::string_view folder, std::string_view name)
    {
        if (folder.empty())
            return std::string(name);
        std::string path(folder);
        path += '/';
        path += name;
        return path;
    }

    std::string_view ParentPath(std::string_view path)
    {
        const size_t slash = path.rfind('/');
        return slash == std::string_view::npos ? std::string_view{} : path.substr(0, slash);
    }

    std::string_view FileName(std::string_view path)
    {
        const size_t slash = path.rfind('/');
        return slash == std::string_view::npos ? path : path.substr(slash + 1);
    }

    MemoryFileSystem::MemoryFileSystem() : m_root(std::make_unique<Node>())
    {
        m_root->isFolder = true;
    }

    MemoryFileSystem::MemoryFileSystem(std::filesystem::path diskRoot) : MemoryFileSystem()
    {
        m_root->disk   = std::move(diskRoot);
        m_root->loaded = false;
    }

    MemoryFileSystem::~MemoryFileSystem() = default;

    void MemoryFileSystem::Load(Node& folder)
    {
        if (folder.loaded)
            return;
        folder.loaded = true;
        std::error_code ec;
        std::filesystem::directory_iterator it(folder.disk, std::filesystem::directory_options::skip_permission_denied, ec);
        for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
        {
            auto node    = std::make_unique<Node>();
            const std::u8string name = it->path().filename().u8string();
            node->name   = std::string(name.begin(), name.end());
            node->parent = &folder;
            std::error_code entryError;
            node->isFolder = it->is_directory(entryError);
            if (node->isFolder)
            {
                node->disk   = it->path();
                node->loaded = false;
            }
            else
                node->size = it->file_size(entryError);
            if (entryError)
                node->size = 0;
            folder.children.push_back(std::move(node));
        }
    }

    MemoryFileSystem::Node* MemoryFileSystem::Find(std::string_view path)
    {
        Node* node = m_root.get();
        while (!path.empty())
        {
            const size_t slash = path.find('/');
            Load(*node);   // a file has no entries: past one, nothing is found
            node = node->Child(path.substr(0, slash));
            if (!node)
                return nullptr;
            path = slash == std::string_view::npos ? std::string_view{} : path.substr(slash + 1);
        }
        return node;
    }

    MemoryFileSystem::Node* MemoryFileSystem::AddPath(std::string_view path, bool isFolder)
    {
        Node* node = m_root.get();
        while (!path.empty())
        {
            const size_t slash = path.find('/');
            const std::string_view name = path.substr(0, slash);
            const bool last = slash == std::string_view::npos;
            if (!node->isFolder)
                return nullptr;   // a file in the way
            Load(*node);
            Node* child = node->Child(name);
            if (!child)
            {
                auto made = std::make_unique<Node>();
                made->name     = std::string(name);
                made->isFolder = !last || isFolder;
                made->parent   = node;
                child = made.get();
                node->children.push_back(std::move(made));
            }
            else if (last && !isFolder)
                return nullptr;   // something already there
            node = child;
            path = last ? std::string_view{} : path.substr(slash + 1);
        }
        return node->isFolder == isFolder ? node : nullptr;
    }

    bool MemoryFileSystem::AddFolder(std::string_view path)
    {
        if (!AddPath(path, true))
            return false;
        ++m_generation;
        return true;
    }

    bool MemoryFileSystem::AddFile(std::string_view path, uint64_t size)
    {
        Node* file = path.empty() ? nullptr : AddPath(path, false);
        if (!file)
            return false;
        file->size = size;
        ++m_generation;
        return true;
    }

    std::vector<FileEntry> MemoryFileSystem::List(std::string_view path)
    {
        std::vector<FileEntry> entries;
        Node* folder = Find(path);
        if (!folder || !folder->isFolder)
            return entries;
        Load(*folder);
        entries.reserve(folder->children.size());
        for (const auto& c : folder->children)
            entries.push_back(FileEntry{ c->name, c->isFolder, c->size });
        return entries;
    }

    bool MemoryFileSystem::Move(std::string_view path, std::string_view toFolder, std::string& error)
    {
        Node* node = path.empty() ? nullptr : Find(path);
        if (!node)
        {
            error = "There's no \"" + std::string(path) + "\".";
            return false;
        }
        Node* target = Find(toFolder);
        if (!target || !target->isFolder)
        {
            error = "There's no folder \"" + std::string(toFolder) + "\".";
            return false;
        }
        if (target == node->parent)
            return true;
        for (const Node* up = target; up; up = up->parent)
            if (up == node)
            {
                error = "\"" + node->name + "\" can't go into itself.";
                return false;
            }
        Load(*target);
        if (target->Child(node->name))
        {
            error = "There's already a \"" + node->name + "\" in \"" + (toFolder.empty() ? std::string("the top folder") : std::string(toFolder)) + "\".";
            return false;
        }
        auto& siblings = node->parent->children;
        const auto at = std::find_if(siblings.begin(), siblings.end(), [node](const auto& c) { return c.get() == node; });
        std::unique_ptr<Node> moved = std::move(*at);
        siblings.erase(at);
        moved->parent = target;
        target->children.push_back(std::move(moved));
        ++m_generation;
        return true;
    }

    bool MemoryFileSystem::Rename(std::string_view path, std::string_view newName, std::string& error)
    {
        Node* node = path.empty() ? nullptr : Find(path);
        if (!node)
        {
            error = "There's no \"" + std::string(path) + "\".";
            return false;
        }
        if (newName == node->name)
            return true;
        if (newName.empty() || newName == "." || newName == ".." || newName.find_first_of("/\\") != std::string_view::npos)
        {
            error = "\"" + std::string(newName) + "\" can't be a name.";
            return false;
        }
        if (node->parent->Child(newName))
        {
            error = "There's already a \"" + std::string(newName) + "\" here.";
            return false;
        }
        node->name = std::string(newName);
        ++m_generation;
        return true;
    }

    bool MemoryFileSystem::Import(const std::filesystem::path& source, std::string_view toFolder, std::string& error)
    {
        // A folder named with a separator at its end ("C:/Photos/") is still that folder.
        std::filesystem::path disk = source.lexically_normal();
        if (!disk.has_filename() && disk.has_parent_path() && disk.parent_path() != disk.root_path())
            disk = disk.parent_path();
        const std::u8string name8 = disk.filename().u8string();
        const std::string name(name8.begin(), name8.end());
        const std::u8string shown8 = source.u8string();
        const std::string shown(shown8.begin(), shown8.end());

        std::error_code ec;
        const std::filesystem::file_status status = std::filesystem::status(disk, ec);
        if (ec || !std::filesystem::exists(status))
        {
            error = "There's no \"" + shown + "\".";
            return false;
        }
        if (name.empty() || name == "." || name == "..")
        {
            error = "\"" + shown + "\" has no name to copy it under.";
            return false;
        }
        Node* target = Find(toFolder);
        if (!target || !target->isFolder)
        {
            error = "There's no folder \"" + std::string(toFolder) + "\".";
            return false;
        }
        Load(*target);
        if (target->Child(name))
        {
            error = "There's already a \"" + name + "\" in \"" + (toFolder.empty() ? std::string("the top folder") : std::string(toFolder)) + "\".";
            return false;
        }

        auto node    = std::make_unique<Node>();
        node->name   = name;
        node->parent = target;
        node->isFolder = std::filesystem::is_directory(status);
        if (node->isFolder)
        {
            node->disk   = disk;
            node->loaded = false;
        }
        else
        {
            node->size = std::filesystem::file_size(disk, ec);
            if (ec)
                node->size = 0;
        }
        target->children.push_back(std::move(node));
        ++m_generation;
        return true;
    }
}
