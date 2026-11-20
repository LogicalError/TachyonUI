// The default themes and fonts compiled into the library: found by the end of their
// path, kept in step with the files in workdirectory/, and enough for an app that has no
// resource files at all.

#include <gtest/gtest.h>

#include "headless_gui_app.h"
#include "scoped_temp_dir.h"

#include <gui_core/gui_embedded_files_internal.h>
#include <gui_core/gui_resources.h>
#include <gui_core/graphics/gui_render_primitives.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    using namespace tachyon;

    // A folder that doesn't exist, so nothing can come from disk.
    std::string MissingFolder()
    {
        return (std::filesystem::temp_directory_path() / "tachyon-no-such-resources").generic_string();
    }

    bool Embedded() { return !detail::EmbeddedResourceFiles().empty(); }

    TEST(BuiltInResources, FoundByTheEndOfTheirPath)
    {
        if (!Embedded()) GTEST_SKIP() << "built without TACHYON_EMBED_RESOURCES";
        EXPECT_FALSE(FindBuiltInResource("themes/styles.css").empty());
        EXPECT_FALSE(FindBuiltInResource("C:/app/resources/themes/styles.css").empty());
        EXPECT_FALSE(FindBuiltInResource("/opt/app/themes/../fonts/Inter/Inter-Regular.ttf").empty());
#ifdef _WIN32
        // Backslashes separate folders only on Windows.
        EXPECT_FALSE(FindBuiltInResource("C:\\app\\resources\\themes\\styles.css").empty());
#endif
        EXPECT_FALSE(FindBuiltInResource("fonts/JetBrainsMono/JetBrainsMono-Bold.ttf").empty());
    }

    // Each backend built in has both shaders of every primitive kind, in its own format.
    TEST(BuiltInShaders, EveryKindHasItsPairForEachBackend)
    {
        std::vector<std::string_view> extensions;
        if (TACHYON_VULKAN) extensions.push_back(".spv");
        if (TACHYON_D3D12)  extensions.push_back(".dxil");
        if (TACHYON_SDLGPU) extensions.push_back(".sdl.spv");
#if defined(_WIN32)
        if (TACHYON_SDLGPU) extensions.push_back(".sdl.dxil");
#endif
        if (extensions.empty()) GTEST_SKIP() << "built without a GPU backend";
        for (const PrimitiveSpec& spec : kPrimitiveSpecs)
            for (const std::string_view stage : { ".vert", ".frag" })
                for (const std::string_view extension : extensions)
                {
                    const std::string name = std::string(spec.shaderBaseName) + std::string(stage) + std::string(extension);
                    EXPECT_FALSE(detail::FindEmbeddedFile(detail::EmbeddedShaderFiles(), name).empty()) << name;
                }
    }

    // Direct3D 12 runs only signed DXIL: a container, "DXBC", whose 16-byte hash isn't zero. Both
    // the Direct3D 12 backend and SDL GPU (on Windows) build a DXIL pair per primitive kind.
    TEST(BuiltInShaders, TheDXILIsSigned)
    {
#if defined(_WIN32)
        const size_t backends = size_t(TACHYON_D3D12) + size_t(TACHYON_SDLGPU);
#else
        const size_t backends = size_t(TACHYON_D3D12);
#endif
        if (backends == 0) GTEST_SKIP() << "built without a backend that runs DXIL";
        size_t dxil = 0;
        for (const detail::EmbeddedFile& file : detail::EmbeddedShaderFiles())
        {
            if (!file.key.ends_with(".dxil"))
                continue;
            ++dxil;
            ASSERT_GE(file.data.size(), 20u) << file.key;
            EXPECT_EQ(std::memcmp(file.data.data(), "DXBC", 4), 0) << file.key;
            const auto hash = file.data.subspan(4, 16);
            EXPECT_TRUE(std::any_of(hash.begin(), hash.end(), [](std::byte b) { return b != std::byte{ 0 }; }))
                << file.key << " is unsigned";
        }
        EXPECT_EQ(dxil, std::size(kPrimitiveSpecs) * 2 * backends);
    }

    TEST(BuiltInResources, OnlyAtAFolderBoundary)
    {
        EXPECT_TRUE(FindBuiltInResource("styles.css").empty());
        EXPECT_TRUE(FindBuiltInResource("mythemes/styles.css").empty());
        EXPECT_TRUE(FindBuiltInResource("themes/showcase.css").empty());
    }

    // The tests run in workdirectory/, which holds the files the copies were made from.
    TEST(BuiltInResources, MatchTheFilesTheyWereMadeFrom)
    {
        if (!Embedded()) GTEST_SKIP() << "built without TACHYON_EMBED_RESOURCES";
        for (const detail::EmbeddedFile& file : detail::EmbeddedResourceFiles())
        {
            std::ifstream in(std::string(file.key), std::ios::binary);
            ASSERT_TRUE(in.good()) << file.key;
            const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            ASSERT_EQ(bytes.size(), file.data.size()) << file.key;
            EXPECT_EQ(std::memcmp(bytes.data(), file.data.data(), bytes.size()), 0) << file.key;
        }
    }

    // A file on disk wins over the built-in copy of the same name.
    TEST(BuiltInResources, ReadResourcePrefersTheFileOnDisk)
    {
        if (!Embedded()) GTEST_SKIP() << "built without TACHYON_EMBED_RESOURCES";
        const GUITest::ScopedTempDir temp("tachyon-resource-test");
        const std::filesystem::path folder = temp / "themes";
        std::filesystem::create_directories(folder);
        const std::filesystem::path file = folder / "dark.css";
        {
            std::ofstream out(file, std::ios::binary);
            out << ":root { --accent: #123456; }\n";
        }
        const Result<std::string> read = ReadResource(file.string());
        ASSERT_TRUE(read) << read.GetError().message;
        EXPECT_EQ(*read, ":root { --accent: #123456; }\n");
    }

    TEST(BuiltInResources, ReadResourceReportsAFileWithNoBuiltInCopy)
    {
        const Result<std::string> missing = ReadResource(MissingFolder() + "/themes/showcase.css");
        ASSERT_FALSE(missing);
        EXPECT_EQ(missing.GetError().code, ErrorCode::NotFound);
    }

    TEST(BuiltInResources, ReadResourceFallsBackToTheBuiltInCopy)
    {
        if (!Embedded()) GTEST_SKIP() << "built without TACHYON_EMBED_RESOURCES";
        const Result<std::string> builtIn = ReadResource(MissingFolder() + "/themes/dark.css");
        ASSERT_TRUE(builtIn) << builtIn.GetError().message;
        const Result<std::string> onDisk = ReadResource("themes/dark.css");
        ASSERT_TRUE(onDisk);
        EXPECT_EQ(*builtIn, *onDisk);
    }

    // An app with no resource files loads the default theme: palette, styles.css and the
    // @font-face fonts it names.
    TEST(BuiltInResources, AThemeLoadsWithoutFiles)
    {
        if (!Embedded()) GTEST_SKIP() << "built without TACHYON_EMBED_RESOURCES";
        GUITest::HeadlessGUIApp app(MissingFolder() + "/themes/dark.css");
        ASSERT_TRUE(app.Initialize());
        EXPECT_TRUE(app.theme.FindStyle("button")) << "styles.css is loaded";
        EXPECT_TRUE(app.theme.FindStyle("window"));
        EXPECT_NE(app.theme.GetColor("--accent", color4f{ 0, 0, 0, 0 }).a, 0.0f) << "the palette is loaded";
    }
}
