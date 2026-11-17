#include <gui_core/graphics/gui_gpu_choice.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>

namespace tachyon
{
    namespace
    {
        std::string Lower(std::string_view text)
        {
            std::string out(text);
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return out;
        }

        std::string_view Trimmed(std::string_view text)
        {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))  text.remove_suffix(1);
            return text;
        }
    }

    std::string_view GpuKindName(GpuKind kind)
    {
        switch (kind)
        {
            case GpuKind::Integrated: return "integrated";
            case GpuKind::Discrete:   return "discrete";
            case GpuKind::Virtual:    return "virtual";
            case GpuKind::Cpu:        return "cpu";
            default:                  return "other";
        }
    }

    std::optional<size_t> ChooseGpu(std::span<const GpuInfo> gpus, std::string_view preference)
    {
        const std::string wanted = Lower(Trimmed(preference));
        if (wanted.empty())
            return std::nullopt;

        for (const GpuKind kind : { GpuKind::Integrated, GpuKind::Discrete, GpuKind::Virtual, GpuKind::Cpu })
            if (wanted == GpuKindName(kind))
            {
                for (size_t i = 0; i < gpus.size(); ++i)
                    if (gpus[i].kind == kind)
                        return i;
                return std::nullopt;
            }

        size_t index = 0;
        const auto [end, error] = std::from_chars(wanted.data(), wanted.data() + wanted.size(), index);
        if (error == std::errc{} && end == wanted.data() + wanted.size())
            return index < gpus.size() ? std::optional<size_t>(index) : std::nullopt;

        for (size_t i = 0; i < gpus.size(); ++i)
            if (Lower(gpus[i].name).find(wanted) != std::string::npos)
                return i;
        return std::nullopt;
    }

    std::string GpuPreference(std::string_view appPreference)
    {
#if defined(_WIN32)
        char*  value  = nullptr;
        size_t length = 0;
        if (_dupenv_s(&value, &length, "TACHYON_GPU") == 0 && value != nullptr)
        {
            std::string fromEnvironment(value);
            std::free(value);
            if (!fromEnvironment.empty())
                return fromEnvironment;
        }
#else
        if (const char* fromEnvironment = std::getenv("TACHYON_GPU"); fromEnvironment && *fromEnvironment)
            return fromEnvironment;
#endif
        return std::string(appPreference);
    }
}
