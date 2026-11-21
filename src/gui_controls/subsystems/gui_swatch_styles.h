#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/style/gui_style.h>   // VisualColorStyleIndex

#include <unordered_map>

namespace tachyon
{
    /// @addtogroup controls
    /// @{

    struct NoSwatchData {};

    /// Swatches' color styles, by swatch ID: each kept while it's claimed every frame, and
    /// destroyed the frame after one goes unclaimed. One map, each entry stamped with the
    /// frame it was last claimed in, so a frame like the last allocates nothing. `Data` is
    /// what a swatch keeps besides its style.
    template <class Data = NoSwatchData>
    class SwatchStyles
    {
    public:
        struct Entry
        {
            VisualColorStyleIndex index = VisualColorStyleIndex::Invalid;   ///< Invalid until the caller makes it
            uint64_t              claimed = 0;                              ///< the frame it was last claimed in
            Data                  data {};
        };

        /// `id`'s entry, kept this frame: a new one has no style yet.
        Entry& Claim(ID id)
        {
            Entry& entry = m_entries[id];
            entry.claimed = m_frame;
            return entry;
        }

        /// `id`'s entry as last claimed, or null.
        Entry* Find(ID id)
        {
            const auto it = m_entries.find(id);
            return it != m_entries.end() ? &it->second : nullptr;
        }

        /// Keeps `id`'s entry this frame without claiming it anew (a cached scope replays its
        /// swatches without building them): only one claimed last frame is still there.
        void KeepAlive(ID id)
        {
            if (Entry* entry = Find(id))
                entry->claimed = m_frame;
        }

        /// At the start of a frame: the styles of the swatches not claimed in the last one go.
        template <class StyleManagerT>
        void NextFrame(StyleManagerT& styleManager)
        {
            std::erase_if(m_entries, [&](auto& item) {
                if (item.second.claimed == m_frame)
                    return false;
                styleManager.DestroyColorStyle(item.second.index);
                return true;
            });
            ++m_frame;
        }

        /// Drops every entry without destroying its style (a theme reload freed them all).
        void Forget() { m_entries.clear(); }

    private:
        std::unordered_map<ID, Entry> m_entries;
        uint64_t                      m_frame = 1;
    };
    /// @}
}
