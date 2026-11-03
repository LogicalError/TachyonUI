#pragma once

// Pictures of what's in files, as the OS shows them in its file views: a photo's, a frame of
// a video, a document's first page. Read off the UI thread.

#include <gui_core/gui_base_types.h>
#include <gui_core/style/gui_style_text.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace tachyon
{
    class ITextSystem;

    /// @addtogroup controls_data
    /// @{

    /// A file's thumbnail, registered with a text system as a picture icon
    /// (ITextSystem::RegisterIconFromPixels). Thumbnails are read on a thread of their own,
    /// so Get gives IconID::Invalid until one is ready, and for a file without one: a view
    /// shows the file's type icon (FileIcons) meanwhile. Read from the Windows shell (what
    /// Explorer shows); elsewhere there are none.
    ///
    /// Kept by path, modification time and size, so a file that changed is read again. At most
    /// `capacity` are kept: past that, the one asked for longest ago is let go, and its icon
    /// unregistered (ITextSystem::UnregisterIcon). One asked for within `inUse` (on screen
    /// still) isn't let go: a file past those gets no thumbnail until one is, rather than all
    /// of them being read over and over. Destroy it before the text system.
    class FileThumbnails
    {
    public:
        /// RGBA pixels, rows from the top, straight alpha.
        struct Picture
        {
            uint2                size {};
            std::vector<uint8_t> rgba;
        };
        /// Reads `file`'s thumbnail, at most `pixels` square, keeping its shape. False when
        /// it has none. Called on the reading thread.
        using Reader = std::function<bool(const std::filesystem::path& file, uint32_t pixels, Picture& picture)>;

        explicit FileThumbnails(ITextSystem& textSystem, size_t capacity = 512, Reader reader = ShellReader(),
                                std::chrono::milliseconds inUse = std::chrono::milliseconds(500));
        ~FileThumbnails();
        FileThumbnails(const FileThumbnails&)            = delete;
        FileThumbnails& operator=(const FileThumbnails&) = delete;

        /// The thumbnail of the file `file` on the disk, last written at `modified`, at most
        /// `pixels` square (the icon's size on screen, in pixels). Starts reading it the first
        /// time; IconID::Invalid until it's ready, for a file without one, and for an empty
        /// path (a file that's only in memory).
        IconID Get(const std::filesystem::path& file, std::filesystem::file_time_type modified, uint32_t pixels);

        /// How many are kept, read or still being read.
        size_t Count() const { return m_entries.size(); }
        /// How many of those are still being read.
        size_t Reading();

        /// What a thumbnail is kept by: the same for the same file, time and size.
        static std::string Key(const std::filesystem::path& file, std::filesystem::file_time_type modified, uint32_t pixels);
        /// The Windows shell's thumbnails (IShellItemImageFactory, thumbnails only, never the
        /// type's icon); an empty Reader elsewhere.
        static Reader ShellReader();

    private:
        struct Entry
        {
            IconID                                icon    = IconID::Invalid;
            bool                                  pending = true;
            std::list<std::string>::iterator      used;   ///< its place in m_used
            std::chrono::steady_clock::time_point asked;
        };
        struct Job
        {
            std::string           key;
            std::filesystem::path file;
            uint32_t              pixels = 0;
        };
        struct Done
        {
            std::string key;
            bool        ok = false;
            Picture     picture;
        };

        void Collect();
        void Forget(const std::string& key);
        void Work();

        ITextSystem&                           m_textSystem;
        size_t                                 m_capacity;
        Reader                                 m_reader;
        std::chrono::milliseconds              m_inUse;
        std::unordered_map<std::string, Entry> m_entries;
        std::list<std::string>                 m_used;   ///< keys, the most recently asked for first

        // Shared with the reading thread.
        std::mutex              m_mutex;
        std::condition_variable m_wake;
        std::vector<Job>        m_jobs;   ///< the newest at the back, read first: what's on screen now
        std::vector<Done>       m_done;
        bool                    m_stop = false;
        std::thread             m_thread;   ///< started by the first file to read
    };

#ifdef _WIN32
    namespace detail
    {
        /// The pixels of a Windows HBITMAP as RGBA, rows from the top, straight alpha. A
        /// bitmap with no alpha at all is opaque. False when it can't be read.
        bool BitmapPixels(void* bitmap, uint32_t& width, uint32_t& height, std::vector<uint8_t>& rgba);
    }
#endif

    /// @}
}
