#include <gui_controls/gui_file_thumbnails.h>

#include <gui_core/text/gui_text_system.h>

#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace tachyon
{
#ifdef _WIN32
    bool detail::BitmapPixels(void* bitmap, uint32_t& width, uint32_t& height, std::vector<uint8_t>& rgba)
    {
        BITMAP bm {};
        if (!GetObjectW(static_cast<HBITMAP>(bitmap), sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0)
            return false;
        width  = uint32_t(bm.bmWidth);
        height = uint32_t(bm.bmHeight);
        BITMAPINFO bi {};
        bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth       = bm.bmWidth;
        bi.bmiHeader.biHeight      = -bm.bmHeight;   // rows from the top
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        std::vector<uint8_t> bgra(size_t(width) * height * 4);
        HDC dc = GetDC(nullptr);
        const bool ok = GetDIBits(dc, static_cast<HBITMAP>(bitmap), 0, height, bgra.data(), &bi, DIB_RGB_COLORS) == int(height);
        ReleaseDC(nullptr, dc);
        if (!ok)
            return false;
        bool anyAlpha = false;
        for (size_t i = 3; i < bgra.size(); i += 4)
            anyAlpha |= bgra[i] != 0;
        rgba.resize(bgra.size());
        for (size_t i = 0; i < bgra.size(); i += 4)
        {
            // The shell's bitmaps are premultiplied.
            const uint32_t a = anyAlpha ? bgra[i + 3] : 255;
            auto straight = [a](uint8_t c) { return a == 0 || a == 255 ? c : uint8_t(std::min<uint32_t>(255, (c * 255u + a / 2) / a)); };
            rgba[i]     = straight(bgra[i + 2]);
            rgba[i + 1] = straight(bgra[i + 1]);
            rgba[i + 2] = straight(bgra[i]);
            rgba[i + 3] = uint8_t(a);
        }
        return true;
    }

    namespace
    {
        bool ShellThumbnail(const std::filesystem::path& file, uint32_t pixels, FileThumbnails::Picture& picture)
        {
            // The reading thread has COM on already; this only counts it up.
            const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            bool ok = false;
            IShellItemImageFactory* factory = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(file.c_str(), nullptr, IID_PPV_ARGS(&factory))) && factory)
            {
                HBITMAP bitmap = nullptr;
                const SIZE size{ LONG(pixels), LONG(pixels) };
                if (SUCCEEDED(factory->GetImage(size, SIIGBF_THUMBNAILONLY, &bitmap)) && bitmap)
                {
                    ok = detail::BitmapPixels(bitmap, picture.size.x, picture.size.y, picture.rgba);
                    DeleteObject(bitmap);
                }
                factory->Release();
            }
            if (SUCCEEDED(com))
                CoUninitialize();
            return ok;
        }
    }
#endif

    FileThumbnails::FileThumbnails(ITextSystem& textSystem, size_t capacity, Reader reader, std::chrono::milliseconds inUse)
        : m_textSystem(textSystem), m_capacity(std::max<size_t>(capacity, 1)), m_reader(std::move(reader)), m_inUse(inUse)
    {
    }

    FileThumbnails::~FileThumbnails()
    {
        {
            std::lock_guard lock(m_mutex);
            m_stop = true;
        }
        m_wake.notify_all();
        if (m_thread.joinable())
            m_thread.join();
        for (const auto& [key, entry] : m_entries)
            if (entry.icon != IconID::Invalid)
                m_textSystem.UnregisterIcon(entry.icon);
    }

    FileThumbnails::Reader FileThumbnails::ShellReader()
    {
#ifdef _WIN32
        return ShellThumbnail;
#else
        return {};
#endif
    }

    std::string FileThumbnails::Key(const std::filesystem::path& file, std::filesystem::file_time_type modified, uint32_t pixels)
    {
        const std::u8string path = file.generic_u8string();
        // libc++ counts the time in a 128-bit integer, which std::to_string doesn't take.
        const long long ticks = static_cast<long long>(modified.time_since_epoch().count());
        return std::string(path.begin(), path.end()) + '|' + std::to_string(ticks) + '|' + std::to_string(pixels);
    }

    IconID FileThumbnails::Get(const std::filesystem::path& file, std::filesystem::file_time_type modified, uint32_t pixels)
    {
        if (file.empty() || pixels == 0 || !m_reader)
            return IconID::Invalid;
        Collect();
        const auto now = std::chrono::steady_clock::now();
        std::string key = Key(file, modified, pixels);
        if (const auto it = m_entries.find(key); it != m_entries.end())
        {
            m_used.splice(m_used.begin(), m_used, it->second.used);
            it->second.asked = now;
            return it->second.icon;
        }
        // All of them on screen: no room for another.
        if (m_entries.size() >= m_capacity && now - m_entries.at(m_used.back()).asked < m_inUse)
            return IconID::Invalid;

        m_used.push_front(key);
        m_entries.emplace(key, Entry{ .used = m_used.begin(), .asked = now });
        {
            std::lock_guard lock(m_mutex);
            m_jobs.push_back(Job{ .key = std::move(key), .file = file, .pixels = pixels });
            if (!m_thread.joinable())
                m_thread = std::thread([this] { Work(); });
        }
        m_wake.notify_one();
        while (m_entries.size() > m_capacity)
            Forget(m_used.back());
        return IconID::Invalid;
    }

    size_t FileThumbnails::Reading()
    {
        Collect();
        return size_t(std::count_if(m_entries.begin(), m_entries.end(), [](const auto& entry) { return entry.second.pending; }));
    }

    // The thumbnails read since the last time become icons.
    void FileThumbnails::Collect()
    {
        std::vector<Done> done;
        {
            std::lock_guard lock(m_mutex);
            done.swap(m_done);
        }
        for (Done& d : done)
        {
            const auto it = m_entries.find(d.key);
            if (it == m_entries.end() || !it->second.pending)
                continue;   // let go while it was read
            it->second.pending = false;
            if (d.ok)
                it->second.icon = m_textSystem.RegisterIconFromPixels(("file-thumbnail:" + d.key).c_str(), d.picture.size, d.picture.rgba);
        }
    }

    void FileThumbnails::Forget(const std::string& key)
    {
        const auto it = m_entries.find(key);
        if (it == m_entries.end())
            return;
        if (it->second.pending)
        {
            std::lock_guard lock(m_mutex);
            std::erase_if(m_jobs, [&key](const Job& job) { return job.key == key; });
        }
        if (it->second.icon != IconID::Invalid)
            m_textSystem.UnregisterIcon(it->second.icon);
        m_used.erase(it->second.used);
        m_entries.erase(it);
    }

    void FileThumbnails::Work()
    {
#ifdef _WIN32
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif
        std::unique_lock lock(m_mutex);
        for (;;)
        {
            m_wake.wait(lock, [this] { return m_stop || !m_jobs.empty(); });
            if (m_stop)
                break;
            Job job = std::move(m_jobs.back());
            m_jobs.pop_back();
            lock.unlock();
            Done done{ .key = std::move(job.key) };
            done.ok = m_reader(job.file, job.pixels, done.picture) && done.picture.size.x > 0 && done.picture.size.y > 0 &&
                      done.picture.rgba.size() >= size_t(done.picture.size.x) * done.picture.size.y * 4;
            lock.lock();
            m_done.push_back(std::move(done));
        }
        lock.unlock();
#ifdef _WIN32
        if (SUCCEEDED(com))
            CoUninitialize();
#endif
    }
}
