// Resource paths name files in the case they have on disk. Windows and macOS open a file
// whatever the case, Linux does not, so a wrong case has to fail on every platform: the
// path case check refuses it at run time, and the source scan below finds it in the code.

#include <gtest/gtest.h>

#include "skribidi_test_fixture.h"

#include <gui_core/gui_embedded_files_internal.h>
#include <gui_core/gui_resources.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace
{
    using namespace tachyon;
    namespace fs = std::filesystem;

    std::string Lower(std::string_view text)
    {
        std::string out(text);
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return out;
    }

    // `path` with the case of its file name's first letter flipped: "a/b" -> "a/B".
    std::string FlipFirstLetter(std::string path)
    {
        const size_t slash = path.find_last_of('/');
        const size_t i = slash == std::string::npos ? 0 : slash + 1;
        const unsigned char c = static_cast<unsigned char>(path[i]);
        path[i] = char(std::isupper(c) ? std::tolower(c) : std::toupper(c));
        return path;
    }

    class ScopedPathCaseCheck
    {
    public:
        explicit ScopedPathCaseCheck(PathCaseCheck mode) : m_previous(GetPathCaseCheck()) { SetPathCaseCheck(mode); }
        ~ScopedPathCaseCheck() { SetPathCaseCheck(m_previous); }
    private:
        PathCaseCheck m_previous;
    };

    // A temporary folder holding Folder/Sample.css, one per test: ctest runs the tests as
    // processes side by side, and one's TearDown mustn't remove another's files.
    class PathCaseTest : public ::testing::Test
    {
    protected:
        fs::path root;
        std::string exact;
        std::string wrong;

        void SetUp() override
        {
            root = fs::temp_directory_path() /
                   (std::string("tachyon-path-case-") + ::testing::UnitTest::GetInstance()->current_test_info()->name());
            fs::create_directories(root / "Folder");
            std::ofstream(root / "Folder" / "Sample.css", std::ios::binary) << ":root { --accent: #123456; }\n";
            exact = (root / "Folder" / "Sample.css").generic_string();
            wrong = (root / "folder" / "sample.css").generic_string();
        }

        void TearDown() override
        {
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        // Windows and macOS (by default) find a file whatever the case of its name.
        bool FileSystemIgnoresCase() const
        {
            std::error_code ec;
            return fs::exists(wrong, ec);
        }
    };

    TEST_F(PathCaseTest, AnExactPathMatches)
    {
        EXPECT_EQ(FindPathCaseMismatch(exact), "");
    }

    TEST_F(PathCaseTest, AMissingPathHasNothingToCompare)
    {
        EXPECT_EQ(FindPathCaseMismatch((root / "Folder" / "Missing.css").generic_string()), "");
    }

    TEST_F(PathCaseTest, AWrongCaseGivesTheSpellingOnDisk)
    {
        if (!FileSystemIgnoresCase()) GTEST_SKIP() << "the file system tells the names apart, so the wrong one is missing";
        EXPECT_EQ(fs::path(FindPathCaseMismatch(wrong)).generic_string(), exact);
    }

#if defined(_WIN32)
    // Windows can name a folder by its 8.3 short form ("RUNNER~1", as a temp path may be
    // given), which no folder listing holds: it's kept as given, and the parts after it are
    // still checked.
    TEST_F(PathCaseTest, AShortFolderNameKeepsTheCheckGoing)
    {
        const fs::path longFolder = root / "A Long Folder Name";
        fs::create_directories(longFolder);
        std::ofstream(longFolder / "Sample.css", std::ios::binary) << ":root {}\n";
        wchar_t shortName[MAX_PATH] = {};
        const DWORD length = GetShortPathNameW(longFolder.c_str(), shortName, MAX_PATH);
        const fs::path shortFolder(std::wstring(shortName, length));
        if (length == 0 || length >= MAX_PATH || shortFolder.filename() == longFolder.filename())
            GTEST_SKIP() << "this volume makes no 8.3 short names";

        EXPECT_EQ(FindPathCaseMismatch((shortFolder / "Sample.css").generic_string()), "");
        EXPECT_EQ(fs::path(FindPathCaseMismatch((shortFolder / "sample.css").generic_string())).generic_string(),
                  (shortFolder / "Sample.css").generic_string());
    }
#endif

    TEST_F(PathCaseTest, ARelativePathIsComparedFromTheWorkingDirectory)
    {
        const std::string relative = "themes/dark.css";
        EXPECT_EQ(FindPathCaseMismatch(relative), "");
        EXPECT_EQ(FindPathCaseMismatch("./themes/../" + relative), "");
        const std::string wrongCase = FlipFirstLetter(relative);
        if (!fs::exists(wrongCase)) GTEST_SKIP() << "the file system tells the names apart";
        EXPECT_EQ(fs::path(FindPathCaseMismatch(wrongCase)).generic_string(), relative);
    }

    // Read with the wrong case, a resource fails on every platform: here because the check
    // refuses it, on Linux because the file is missing.
    TEST_F(PathCaseTest, ReadResourceRefusesAWrongCaseWhenChecking)
    {
        const ScopedPathCaseCheck check(PathCaseCheck::Fail);
        EXPECT_TRUE(ReadResource(exact));
        EXPECT_FALSE(ReadResource(wrong));
    }

    TEST_F(PathCaseTest, ReadResourceLetsAWrongCaseThroughWithTheCheckOff)
    {
        if (!FileSystemIgnoresCase()) GTEST_SKIP() << "the file system tells the names apart";
        const ScopedPathCaseCheck check(PathCaseCheck::Off);
        EXPECT_TRUE(ReadResource(wrong));
    }

    TEST_F(PathCaseTest, AWarningStillReads)
    {
        if (!FileSystemIgnoresCase()) GTEST_SKIP() << "the file system tells the names apart";
        const ScopedPathCaseCheck check(PathCaseCheck::Warn);
        EXPECT_TRUE(CheckPathCase(wrong));
        EXPECT_TRUE(ReadResource(wrong));
    }

    // The built-in copies are found by their exact name, as files on Linux are.
    TEST(PathCase, BuiltInCopiesNeedTheExactCase)
    {
        if (detail::EmbeddedResourceFiles().empty()) GTEST_SKIP() << "built without TACHYON_EMBED_RESOURCES";
        for (const detail::EmbeddedFile& file : detail::EmbeddedResourceFiles())
        {
            EXPECT_FALSE(FindBuiltInResource(file.key).empty()) << file.key;
            EXPECT_TRUE(FindBuiltInResource(FlipFirstLetter(std::string(file.key))).empty()) << file.key;
        }
    }

    class PathCaseTextTest : public GUITest::SkribidiHeadlessTest {};

    TEST_F(PathCaseTextTest, IconsAndFontsNeedTheExactCase)
    {
        const ScopedPathCaseCheck check(PathCaseCheck::Fail);
        EXPECT_NE(textSystem->RegisterIconFromSVGFile("icons/lucide/x.svg"), IconID::Invalid);
        EXPECT_EQ(textSystem->RegisterIconFromSVGFile(FlipFirstLetter("icons/lucide/x.svg").c_str()), IconID::Invalid);

        const std::string wrongPath = FlipFirstLetter("fonts/Inter/Inter-Bold.ttf");
        const FontFaceSource wrongCase{ .file = wrongPath };
        EXPECT_EQ(textSystem->RegisterFontFamily("Wrong Case", std::span(&wrongCase, 1)), FontID::Invalid);
        const FontFaceSource exactCase{ .file = "fonts/Inter/Inter-Bold.ttf" };
        EXPECT_NE(textSystem->RegisterFontFamily("Exact Case", std::span(&exactCase, 1)), FontID::Invalid);
    }

    // ------------------------------------------------------------------------
    // The source tree
    // ------------------------------------------------------------------------

    fs::path SourceDir() { return fs::path(TACHYON_SOURCE_DIR); }

    bool IsResourceName(std::string_view name)
    {
        static constexpr std::array kExtensions{ ".css", ".ttf", ".otf", ".svg", ".spv", ".slang",
                                                 ".json", ".png", ".jpg" };
        const size_t dot = name.find_last_of('.');
        if (dot == std::string_view::npos)
            return false;
        const std::string extension = Lower(name.substr(dot));
        return std::find(kExtensions.begin(), kExtensions.end(), extension) != kExtensions.end();
    }

    // Every resource file, by path relative to workdirectory/.
    std::vector<std::string> ResourceFiles()
    {
        std::vector<std::string> files;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(SourceDir() / "workdirectory", ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec))
                files.push_back(it->path().lexically_relative(SourceDir() / "workdirectory").generic_string());
        return files;
    }

    // Each run of path characters ending in a resource extension: a quoted string, a CSS
    // url(), a CMake list entry, a path in a comment or a doc. "../fonts/a.ttf" gives
    // "fonts/a.ttf".
    std::vector<std::string> ResourceNames(std::string_view text)
    {
        auto pathChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.' || c == '/'; };
        std::vector<std::string> names;
        for (size_t i = 0; i < text.size();)
        {
            if (!pathChar(text[i])) { ++i; continue; }
            size_t end = i;
            while (end < text.size() && pathChar(text[end]))
                ++end;
            std::string_view name = text.substr(i, end - i);
            i = end;
            while (name.starts_with("./") || name.starts_with("../"))
                name.remove_prefix(name.find('/') + 1);
            while (name.starts_with('/'))
                name.remove_prefix(1);
            while (name.ends_with('.'))
                name.remove_suffix(1);
            if (IsResourceName(name))
                names.emplace_back(name);
        }
        return names;
    }

    // A path in the code, the docs or the build that names a resource file names it in its
    // case on disk: an "inter" folder for "Inter" finds the font on Windows but not on Linux.
    TEST(PathCase, EveryResourcePathInTheSourceMatchesTheFileOnDisk)
    {
        if (!fs::is_directory(SourceDir() / "workdirectory")) GTEST_SKIP() << "no source tree at " << SourceDir();

        // Lower-case path and file name -> the files they name.
        const std::vector<std::string> files = ResourceFiles();
        std::unordered_map<std::string, std::vector<const std::string*>> byName;
        for (const std::string& file : files)
            byName[Lower(fs::path(file).filename().generic_string())].push_back(&file);

        static constexpr std::array kTextExtensions{ ".cpp", ".h", ".inl", ".css", ".md", ".txt", ".cmake", ".json", ".in" };
        int scanned = 0;
        for (const char* folder : { "src", "examples", "tests", "docs", "cmake", "workdirectory/themes" })
        {
            std::error_code ec;
            for (fs::recursive_directory_iterator it(SourceDir() / folder, ec), end; !ec && it != end; it.increment(ec))
            {
                const std::string extension = Lower(it->path().extension().string());
                if (!it->is_regular_file(ec) || std::find(kTextExtensions.begin(), kTextExtensions.end(), extension) == kTextExtensions.end())
                    continue;
                // The guide to this rule shows wrong names on purpose.
                if (it->path().lexically_relative(SourceDir()).generic_string() == "docs/contributing/resource-paths.md")
                    continue;
                std::ifstream in(it->path(), std::ios::binary);
                const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                ++scanned;
                for (const std::string& name : ResourceNames(text))
                {
                    const auto found = byName.find(Lower(fs::path(name).filename().generic_string()));
                    if (found == byName.end())
                        continue;
                    // The files whose path ends in `name`, ignoring case; one of them must match it exactly.
                    const std::string lowerName = Lower(name);
                    const std::string* closest = nullptr;
                    bool exactMatch = false;
                    for (const std::string* file : found->second)
                    {
                        const bool tail = file->size() == name.size() || (file->size() > name.size() && (*file)[file->size() - name.size() - 1] == '/');
                        if (!tail || Lower(std::string_view(*file).substr(file->size() - name.size())) != lowerName)
                            continue;
                        closest = file;
                        if (std::string_view(*file).ends_with(name))
                            exactMatch = true;
                    }
                    if (closest && !exactMatch)
                        ADD_FAILURE() << it->path().lexically_relative(SourceDir()).generic_string() << " names \"" << name
                                      << "\", which is workdirectory/" << *closest << " on disk";
                }
            }
        }
        EXPECT_GT(scanned, 0);
    }

    // Two names that differ only in case can't both live on Windows or macOS: a checkout
    // there loses one of them.
    TEST(PathCase, NoTwoFilesDifferOnlyInCase)
    {
        if (!fs::is_directory(SourceDir() / "workdirectory")) GTEST_SKIP() << "no source tree at " << SourceDir();
        for (const char* folder : { "", "src", "examples", "tests", "docs", "cmake", "workdirectory" })
        {
            std::unordered_map<std::string, std::string> seen;
            std::error_code ec;
            const fs::path top = SourceDir() / folder;
            auto check = [&](const fs::path& path)
            {
                const std::string key = Lower(path.generic_string());
                const auto [it, added] = seen.emplace(key, path.generic_string());
                if (!added && it->second != path.generic_string())
                    ADD_FAILURE() << it->second << " and " << path.generic_string() << " differ only in case";
            };
            if (*folder == '\0')
            {
                for (fs::directory_iterator it(top, ec), end; !ec && it != end; it.increment(ec))
                    check(it->path().lexically_relative(SourceDir()));
                continue;
            }
            for (fs::recursive_directory_iterator it(top, ec), end; !ec && it != end; it.increment(ec))
                check(it->path().lexically_relative(SourceDir()));
        }
    }
}
