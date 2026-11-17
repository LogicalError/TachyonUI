#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace tachyon
{
    /// @addtogroup graphics
    /// @{

    /// What kind of GPU a device is.
    enum class GpuKind : uint8_t { Other, Integrated, Discrete, Virtual, Cpu };

    /// A GPU as the graphics API reports it, for ChooseGpu.
    struct GpuInfo
    {
        std::string name;
        GpuKind     kind = GpuKind::Other;
    };

    /// The kind's name, as a preference names it: "integrated", "discrete", "virtual", "cpu",
    /// "other".
    std::string_view GpuKindName(GpuKind kind);

    /// Which of `gpus` `preference` asks for:
    ///   - a kind ("discrete", "integrated", "virtual", "cpu"): the first of that kind;
    ///   - a number: the GPU at that place in the list, from 0;
    ///   - other text: the first whose name holds it, ignoring case ("radeon").
    /// Empty, or nothing that matches: none, and the graphics API's own choice stands.
    std::optional<size_t> ChooseGpu(std::span<const GpuInfo> gpus, std::string_view preference);

    /// The GPU preference in effect: the TACHYON_GPU environment variable when it's set, for
    /// a run without changing code; else `appPreference`.
    std::string GpuPreference(std::string_view appPreference);

    /// @}
}
