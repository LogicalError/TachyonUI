#pragma once

#include <gui_core/gui_result.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace tachyon
{
    /// @addtogroup utilities
    /// @{

    /// What the library does when a resource file it opens is named in a case other than the
    /// one it has on disk. Windows and macOS open a file whatever the case of its name; Linux
    /// doesn't, so the check finds the mistake on the platform that would hide it.
    enum class PathCaseCheck : uint8_t
    {
        Off,  ///< Don't compare; opening a file costs nothing extra. The default in release builds.
        Warn, ///< Log an error naming both spellings, and open the file. The default in debug builds.
        Fail, ///< Log the error and refuse the file, as Linux would. The test suites use this.
    };

    /// Sets how resource paths are checked; see PathCaseCheck. Thread safe.
    void SetPathCaseCheck(PathCaseCheck mode);
    PathCaseCheck GetPathCaseCheck();

    /// The spelling on disk of the file or folder at `path` when it exists under a name that
    /// differs from `path` in case only: "Images/logo" gives "images/logo" on a file system
    /// that ignores case. Empty when every part matches exactly, when nothing is there
    /// (as on Linux), or when a name can't be told apart from an alias (an 8.3 short name). A
    /// relative path is compared below the working directory; an absolute one from its root.
    /// Lists one folder per part, so it costs a directory read per part.
    std::string FindPathCaseMismatch(std::string_view path);

    /// Applies the PathCaseCheck mode to a resource file about to be opened: false only in
    /// Fail mode when `path` is in the wrong case. The library checks every theme, font, icon
    /// and shader file it opens this way; an app can check its own files the same way.
    bool CheckPathCase(std::string_view path);

    /// The copy of a resource compiled into the library: the default themes (dark.css,
    /// light.css, styles.css) and fonts (Inter, JetBrains Mono), unless the library was built
    /// with TACHYON_EMBED_RESOURCES off. Found by the end of the path, so "themes/styles.css"
    /// and "C:/app/resources/themes/styles.css" both find it, in the exact case of the file
    /// it was made from on every platform. Empty when there is none.
    std::span<const std::byte> FindBuiltInResource(std::string_view path);

    /// A resource's contents: the file at `path` when it exists, else its built-in copy (which
    /// is logged, so a theme file that went missing shows up). The library reads its themes
    /// this way, so an app needs no resource files, and a file of the same name under the
    /// resource root replaces the built-in one. A file the PathCaseCheck refuses is an error.
    Result<std::string> ReadResource(std::string_view path);

    /// @}
}
