#pragma once

// For a backend that binds only a few images per draw (SDL GPU, 16 samplers a shader stage):
// which images the primitives of a draw read, and where to cut the draw so each part reads no
// more than the backend binds. The parts are consecutive runs of the draw's primitives, so
// they draw the same thing in the same order. The images are 0-based image slots, as the
// shaders index them.

#include <gui_core/graphics/gui_render_primitives.h>
#include <gui_core/style/gui_style.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace tachyon
{
    /// @addtogroup graphics
    /// @{

    class ImageBatcher
    {
    public:
        /// The most images a batch holds.
        static constexpr uint32_t kMaxImages = 16;

        /// A run of a draw's primitives, and the images it reads.
        struct Batch
        {
            uint32_t first      = 0;   ///< its first primitive, in the kind's stream
            uint32_t count      = 0;
            uint32_t imageCount = 0;
            std::array<uint32_t, kMaxImages> images {};
        };

        /// The styles the frame's draws use, as uploaded: the color styles' gradient pages are
        /// their image slots (PrepareFrame resolves them).
        void SetStyles(std::span<const VisualColorStyle> colorStyles, std::span<const VisualBoxStyle> boxStyles);

        /// Cuts a draw of `kind`: `primitives` are its primitives' bytes, from `firstPrimitive`.
        /// Appends runs that read at most `maxImages` images each (kMaxImages at most). A
        /// primitive that alone reads more keeps the first `maxImages`.
        void Split(PrimitiveKind kind, std::span<const std::byte> primitives, uint32_t firstPrimitive, uint32_t maxImages,
                   std::vector<Batch>& out) const;

    private:
        static constexpr uint32_t kNone = UINT32_MAX;
        /// The most a primitive reads: its texture, and a box's five color styles in each of four
        /// box styles, plus its own, two images each.
        static constexpr uint32_t kMostPerPrimitive = 1 + 2 * (1 + 4 * 5);

        /// What a primitive reads, deduplicated, into `images`; how many.
        uint32_t Read(PrimitiveKind kind, const std::byte* primitive, std::array<uint32_t, kMostPerPrimitive>& images) const;
        void     AddColorStyle(int colorStyle, std::array<uint32_t, kMostPerPrimitive>& images, uint32_t& count) const;

        /// By color style (0-based): its gradient page's slot and its texture's, or kNone.
        std::vector<std::array<uint32_t, 2>> m_colorImages;
        /// By box style (0-based): the color styles it names (1-based; 0 none).
        std::vector<std::array<uint16_t, 5>> m_boxColors;
    };
    /// @}
}
