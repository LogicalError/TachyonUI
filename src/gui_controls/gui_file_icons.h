#pragma once

// The icons the OS shows for files and folders, as picture icons for a file view.

#include <gui_core/style/gui_style_text.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tachyon
{
    class ITextSystem;

    /// @addtogroup controls_data
    /// @{

    /// The OS's icon for a file or folder, registered with a text system as a picture icon
    /// (ITextSystem::RegisterIconFromPixels). One per extension, matched without case, and one
    /// for folders; each is read once. Read from the Windows shell, from the size of icon at
    /// least `pixels` square it has (16, 32, 48 or 256, scaled with the display); elsewhere,
    /// and when the shell has none, Get gives IconID::Invalid and a view shows its own.
    class FileIcons
    {
    public:
        explicit FileIcons(ITextSystem& textSystem, uint32_t pixels = 48);

        IconID Get(std::string_view name, bool isFolder);
        /// A folder's icon while it's open, or while something is dragged over it.
        IconID OpenFolder();

        /// The extension Get goes by: from the last '.', lowercase ASCII; "" for none, and for
        /// a name that only starts with a '.'.
        static std::string Extension(std::string_view name);

    private:
        IconID Read(const std::string& key, bool isFolder, bool open);

        ITextSystem&                            m_textSystem;
        uint32_t                                m_pixels;
        std::unordered_map<std::string, IconID> m_icons;   ///< by extension; "/" and "/open" for folders
    };

#ifdef _WIN32
    namespace detail
    {
        /// The pixels of a Windows HICON as RGBA, rows from the top, straight alpha; an icon
        /// without alpha of its own gets it from its mask. False when it can't be read.
        bool IconPixels(void* icon, uint32_t& width, uint32_t& height, std::vector<uint8_t>& rgba);
    }
#endif

    /// @}
}
