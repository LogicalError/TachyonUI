#pragma once

// A folder of a test's own under the system's temp folder, removed with everything in it when
// the test ends, failed ASSERTs included: tests leave nothing behind and can't trip over
// another run's files.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace GUITest
{
    class ScopedTempDir
    {
    public:
        explicit ScopedTempDir(std::string_view name)
        {
            static std::atomic<uint32_t> s_count{ 0 };
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            m_path = std::filesystem::temp_directory_path()
                   / (std::string(name) + "-" + std::to_string(stamp) + "-" + std::to_string(s_count++));
            std::filesystem::create_directories(m_path);
        }
        ~ScopedTempDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(m_path, ec);
        }
        ScopedTempDir(const ScopedTempDir&) = delete;
        ScopedTempDir& operator=(const ScopedTempDir&) = delete;

        const std::filesystem::path& Path() const { return m_path; }
        std::filesystem::path operator/(std::string_view child) const { return m_path / child; }

    private:
        std::filesystem::path m_path;
    };
}
