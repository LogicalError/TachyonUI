#include <gui_controls/gui_file_icons.h>

#include <gui_core/text/gui_text_system.h>

#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commoncontrols.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace tachyon
{
#ifdef _WIN32
    bool detail::IconPixels(void* icon, uint32_t& width, uint32_t& height, std::vector<uint8_t>& rgba)
    {
        ICONINFO info {};
        if (!GetIconInfo(static_cast<HICON>(icon), &info))
            return false;
        bool ok = false;
        BITMAP bm {};
        if (info.hbmColor && GetObjectW(info.hbmColor, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0)
        {
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
            std::vector<uint8_t> mask(size_t(width) * height * 4);
            HDC dc = GetDC(nullptr);
            ok = GetDIBits(dc, info.hbmColor, 0, height, bgra.data(), &bi, DIB_RGB_COLORS) == int(height);
            const bool hasMask = ok && info.hbmMask &&
                                 GetDIBits(dc, info.hbmMask, 0, height, mask.data(), &bi, DIB_RGB_COLORS) == int(height);
            ReleaseDC(nullptr, dc);
            if (ok)
            {
                bool anyAlpha = false;
                for (size_t i = 3; i < bgra.size(); i += 4)
                    anyAlpha |= bgra[i] != 0;
                rgba.resize(bgra.size());
                for (size_t i = 0; i < bgra.size(); i += 4)
                {
                    rgba[i]     = bgra[i + 2];
                    rgba[i + 1] = bgra[i + 1];
                    rgba[i + 2] = bgra[i];
                    // The mask is white where the icon is transparent.
                    rgba[i + 3] = anyAlpha ? bgra[i + 3] : (hasMask && mask[i] != 0 ? 0 : 255);
                }
            }
        }
        if (info.hbmColor)
            DeleteObject(info.hbmColor);
        if (info.hbmMask)
            DeleteObject(info.hbmMask);
        return ok;
    }
#endif

    namespace
    {
#ifdef _WIN32
        std::wstring Wide(std::string_view utf8)
        {
            if (utf8.empty())
                return {};
            const int count = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
            std::wstring wide(size_t(count > 0 ? count : 0), L'\0');
            if (count > 0)
                MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), wide.data(), count);
            return wide;
        }

        IconID ShellIcon(ITextSystem& textSystem, const std::string& key, bool isFolder, bool open, uint32_t pixels)
        {
            // The shell wants COM on this thread; it may be on already, in either model.
            const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            IconID result = IconID::Invalid;

            // Only the attributes and the extension count: the file needn't be there.
            const std::wstring path = isFolder ? L"folder" : L"file" + Wide(key);
            SHFILEINFOW file {};
            if (SHGetFileInfoW(path.c_str(), isFolder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL, &file, sizeof(file),
                               SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES | (open ? SHGFI_OPENICON : 0)))
            {
                const int list = pixels <= 16 ? SHIL_SMALL : pixels <= 32 ? SHIL_LARGE : pixels <= 48 ? SHIL_EXTRALARGE : SHIL_JUMBO;
                IImageList* images = nullptr;
                if (SUCCEEDED(SHGetImageList(list, __uuidof(IImageList), reinterpret_cast<void**>(&images))) && images)
                {
                    HICON icon = nullptr;
                    if (SUCCEEDED(images->GetIcon(file.iIcon, ILD_TRANSPARENT, &icon)) && icon)
                    {
                        uint32_t width = 0, height = 0;
                        std::vector<uint8_t> rgba;
                        if (detail::IconPixels(icon, width, height, rgba))
                        {
                            const std::string name = "file-icon:" + std::to_string(pixels) + ":" + key;
                            result = textSystem.RegisterIconFromPixels(name.c_str(), uint2{ width, height }, rgba);
                        }
                        DestroyIcon(icon);
                    }
                    images->Release();
                }
            }
            if (SUCCEEDED(com))
                CoUninitialize();
            return result;
        }
#endif
    }

    FileIcons::FileIcons(ITextSystem& textSystem, uint32_t pixels) : m_textSystem(textSystem), m_pixels(pixels)
    {
    }

    FileIcons::~FileIcons()
    {
        for (const auto& [key, icon] : m_icons)
            if (icon != IconID::Invalid)
                m_textSystem.UnregisterIcon(icon);
    }

    std::string FileIcons::Extension(std::string_view name)
    {
        const size_t dot = name.rfind('.');
        if (dot == std::string_view::npos || dot == 0)
            return {};
        std::string extension(name.substr(dot));
        for (char& c : extension)
            if (c >= 'A' && c <= 'Z')
                c = char(c - 'A' + 'a');
        return extension;
    }

    IconID FileIcons::Get(std::string_view name, bool isFolder)
    {
        return isFolder ? Read("/", true, false) : Read(Extension(name), false, false);
    }

    IconID FileIcons::OpenFolder()
    {
        return Read("/open", true, true);
    }

    IconID FileIcons::Read(const std::string& key, bool isFolder, bool open)
    {
        if (const auto it = m_icons.find(key); it != m_icons.end())
            return it->second;
#ifdef _WIN32
        const IconID icon = ShellIcon(m_textSystem, key, isFolder, open, m_pixels);
#else
        (void)isFolder;
        (void)open;
        const IconID icon = IconID::Invalid;
#endif
        m_icons.emplace(key, icon);
        return icon;
    }
}
