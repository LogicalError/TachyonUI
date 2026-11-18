#include "gtest/gtest.h"
#include <gmock/gmock.h>
#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/gui_resources.h>
#include <gui_core/graphics/gui_default_graphics_context_internal.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <system_error>

// ============================================================================
// Main
// ============================================================================
//
// The harness loads `themes/dark.css` relative to the CWD. ctest / Visual Studio
// set it to workdirectory/; when the exe is launched from elsewhere, find that
// directory by walking up from the executable. If none is found the CWD is left
// alone, so tests fail loudly (InvalidStyle) instead of using another theme.
static void EnsureWorkingDirectory(const char* argv0)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    if (fs::exists("themes/dark.css", ec) && !ec) return;

    // Walk up looking for `workdirectory/themes/dark.css` (build tree) or
    // `themes/dark.css` (installed layout).
    fs::path exePath = argv0 ? fs::absolute(argv0, ec) : fs::path{};
    if (ec || exePath.empty()) return;
    fs::path dir = exePath.parent_path();

    for (int i = 0; i < 8 && !dir.empty(); ++i)
    {
        if (fs::exists(dir / "workdirectory" / "themes" / "dark.css", ec) && !ec)
        {
            fs::current_path(dir / "workdirectory", ec);
            return;
        }
        if (fs::exists(dir / "themes" / "dark.css", ec) && !ec)
        {
            fs::current_path(dir, ec);
            return;
        }
        const fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
}

// --graphics=vulkan|d3d12|none: the apps that don't choose a backend get this one instead of
// the default, so the whole-app GPU tests run on it. Taken out of the arguments; false when
// it names an API that isn't built in.
static bool TakeGraphicsOption(int& argc, char** argv)
{
    constexpr std::string_view kOption = "--graphics=";
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (!arg.starts_with(kOption))
            continue;
        const std::string_view name = arg.substr(kOption.size());
        const tachyon::GraphicsAPI api = name == "d3d12" ? tachyon::GraphicsAPI::D3D12
                                       : name == "none"  ? tachyon::GraphicsAPI::None
                                                         : tachyon::GraphicsAPI::Vulkan;
        if ((name != "vulkan" && name != "d3d12" && name != "none") || !tachyon::SetDefaultGraphicsAPIOverride(api))
        {
            std::fprintf(stderr, "%s: not vulkan, d3d12 or none, or not built in\n", argv[i]);
            return false;
        }
        std::memmove(argv + i, argv + i + 1, sizeof(char*) * size_t(argc - i));
        --argc;
        --i;
    }
    return true;
}

int main(int argc, char** argv)
{
    EnsureWorkingDirectory(argc > 0 ? argv[0] : nullptr);
    if (!TakeGraphicsOption(argc, argv))
        return 2;
    // Every test boots a theme; only problems are worth printing.
    tachyon::Diagnostics::SetMinimumLogSeverity(tachyon::Diagnostics::LogSeverity::Warning);
    // A resource named in the wrong case fails here as it would on Linux.
    tachyon::SetPathCaseCheck(tachyon::PathCaseCheck::Fail);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
