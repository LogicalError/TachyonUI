#include <gui_core/gui_resources.h>
#include <gui_core/gui_embedded_files_internal.h>
#include <gui_core/diagnostics/gui_logging.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace tachyon
{
    namespace
    {
        constexpr Diagnostics::LogCategory kLogResources = Diagnostics::MakeLogCategory("resources");

#ifdef NDEBUG
        std::atomic<PathCaseCheck> g_pathCaseCheck{ PathCaseCheck::Off };
#else
        std::atomic<PathCaseCheck> g_pathCaseCheck{ PathCaseCheck::Warn };
#endif

        // Equal when ASCII letters are folded; names outside ASCII compare exactly.
        template <class Char>
        bool SameNameIgnoringCase(std::basic_string_view<Char> a, std::basic_string_view<Char> b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
            {
                Char x = a[i], y = b[i];
                if (x >= 'A' && x <= 'Z') x = Char(x - 'A' + 'a');
                if (y >= 'A' && y <= 'Z') y = Char(y - 'A' + 'a');
                if (x != y)
                    return false;
            }
            return true;
        }
    }

    void SetPathCaseCheck(PathCaseCheck mode) { g_pathCaseCheck.store(mode, std::memory_order_relaxed); }
    PathCaseCheck GetPathCaseCheck() { return g_pathCaseCheck.load(std::memory_order_relaxed); }

    std::string FindPathCaseMismatch(std::string_view path)
    {
        namespace fs = std::filesystem;
        using NativeView = std::basic_string_view<fs::path::value_type>;

        const fs::path requested = fs::path(path).lexically_normal();
        std::error_code ec;
        if (requested.empty() || !fs::exists(requested, ec))
            return {};

        // Rebuilt part by part from the names the folders list. The root ("C:/", "/") is
        // left as given: drive letters ignore case everywhere they exist.
        fs::path onDisk = requested.root_path();
        bool differs = false;
        for (const fs::path& part : requested.relative_path())
        {
            const NativeView name = part.native();
            if (name.empty() || part == "." || part == "..")
            {
                onDisk /= part;
                continue;
            }
            fs::path folded;
            bool exact = false;
            for (fs::directory_iterator it(onDisk.empty() ? fs::path(".") : onDisk, ec), end; !ec && it != end; it.increment(ec))
            {
                const fs::path entry = it->path().filename();
                const NativeView entryName = entry.native();
                if (entryName == name)
                {
                    exact = true;
                    break;
                }
                if (folded.empty() && SameNameIgnoringCase(entryName, name))
                    folded = entry;
            }
            if (exact)
                onDisk /= part;
            else if (!folded.empty())
            {
                onDisk /= folded;
                differs = true;
            }
            else
                return {}; // an alias the folder doesn't list, such as an 8.3 short name
        }
        return differs ? onDisk.generic_string() : std::string{};
    }

    bool CheckPathCase(std::string_view path)
    {
        const PathCaseCheck mode = GetPathCaseCheck();
        if (mode == PathCaseCheck::Off)
            return true;
        const std::string onDisk = FindPathCaseMismatch(path);
        if (onDisk.empty())
            return true;
        TACHYON_LOG_ERROR(kLogResources, "{} is spelled {} on disk; a file system that tells case apart won't find it",
                          path, onDisk);
        return mode != PathCaseCheck::Fail;
    }

    namespace detail
    {
        std::span<const std::byte> FindEmbeddedFile(std::span<const EmbeddedFile> files, std::string_view path)
        {
            const std::string normal = std::filesystem::path(path).lexically_normal().generic_string();
            const std::string_view p = normal;
            for (const EmbeddedFile& file : files)
            {
                if (p.size() < file.key.size())
                    continue;
                const bool atBoundary = p.size() == file.key.size() || p[p.size() - file.key.size() - 1] == '/';
                if (atBoundary && p.ends_with(file.key))
                    return file.data;
            }
            return {};
        }
    }

    std::span<const std::byte> FindBuiltInResource(std::string_view path)
    {
        return detail::FindEmbeddedFile(detail::EmbeddedResourceFiles(), path);
    }

    Result<std::string> ReadResource(std::string_view path)
    {
        const std::filesystem::path file(path);
        std::error_code ec;
        if (std::filesystem::is_regular_file(file, ec))
        {
            if (!CheckPathCase(path))
                return Error{ ErrorCode::NotFound, std::string(path) + " is in the wrong case" };
            std::ifstream in(file, std::ios::binary);
            std::ostringstream text;
            text << in.rdbuf();
            if (in.good() || in.eof())
                return text.str();
            return Error{ ErrorCode::Failed, "could not read " + std::string(path) };
        }
        if (const std::span<const std::byte> data = FindBuiltInResource(path); !data.empty())
        {
            TACHYON_LOG_INFO(kLogResources, "{}: no such file, using the built-in copy", path);
            return std::string(reinterpret_cast<const char*>(data.data()), data.size());
        }
        return Error{ ErrorCode::NotFound, std::string(path) + " not found, and the library has no built-in copy" };
    }
}
