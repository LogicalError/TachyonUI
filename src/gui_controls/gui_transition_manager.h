#pragma once

#include <gui_core/gui_easing.h>
#include <gui_core/gui_id.h>
#include <gui_core/graphics/gui_renderer.h>

#include <array>
#include <optional>
#include <stdint.h>
#include <unordered_map>
#include <vector>

namespace tachyon
{
    /// @addtogroup app
    /// @{

    enum class ElementState : uint8_t;
    enum class VisualBoxStyleIndex : uint16_t;

    /// The parts of an element's state that change over time, each on its own: hover, press,
    /// focus and selected pick its style; invalid grows its error halo in.
    enum class TransitionChannel : uint8_t { Hover, Active, Focused, Selected, Invalid };
    inline constexpr int kTransitionChannelCount = 5;

    /// A box's look for its state now: the state styles mixed while channels run (`styles`),
    /// and the style of the state it's in, where it ends up (`current`).
    struct TransitionState
    {
        StyleBlend styles;
        uint32_t   current = 0;
    };

    /// How long a change of state takes, and how it eases. A duration of 0 is instant.
    struct TransitionTiming
    {
        float       duration = 0.0f;              ///< seconds
        CubicBezier easing   = Easing::EaseOut;

        bool operator==(const TransitionTiming&) const = default;
    };

    /// Flat GPU style index for a base pool index and state. Must match vk_renderer.cpp and
    /// StyleManager::GetVisualBoxStyle (VisualMask, Disabled collapsing).
    uint32_t ComputeGPUStyleIndex(VisualBoxStyleIndex base, ElementState state);

    /// Tracks the running transitions of the elements' state channels. Each channel goes from
    /// off (0) to on (1) or back on its own timing; one that turns around mid-way carries on
    /// from where it is. Only elements with a channel running take a slot, in a compact array,
    /// and only they are looked at each frame. Times are seconds on the application's
    /// elapsed-time clock.
    class TransitionManager
    {
    public:
        // Each kind of change has its own timing; the ones not listed (press, selection,
        // disabled) are instant.
        static constexpr TransitionTiming kHoverIn  { 0.070f, Easing::EaseOut };
        static constexpr TransitionTiming kHoverOut { 0.140f, Easing::EaseOut };
        static constexpr TransitionTiming kRelease  { 0.120f, Easing::EaseOut };
        static constexpr TransitionTiming kFocus    { 0.100f, Easing::EaseOut };
        static constexpr TransitionTiming kToggle   { 0.120f, Easing::EaseInOut }; ///< a switch, check box or radio turning on or off
        static constexpr TransitionTiming kInvalid  { 0.150f, Easing::EaseOut };   ///< the invalid halo growing in

        /// The timing of `channel` turning on, or off. Selected is instant here; a toggle's
        /// part (OnStateSeen) turns in kToggle.
        static TransitionTiming TimingFor(TransitionChannel channel, bool on);

        /// The element's state went from `fromState` to `toState`: each channel that changed
        /// starts toward it, or turns around, in its timing. Disabled changes at once.
        void OnStateChange(ID id, ElementState fromState, ElementState toState, float elapsedSecs);

        /// For an element whose `watched` bits change between frames rather than with input
        /// (Selected for ElementFlags::AnimatesSelected, Invalid), each frame it's built: when
        /// one differs from the frame before, its channel runs, Selected in kToggle. An
        /// element that wasn't built the frame before starts without one.
        void OnStateSeen(ID id, ElementState state, ElementState watched, float elapsedSecs);
        /// True when OnStateSeen saw the element the frame before or this one.
        bool IsWatched(ID id) const { return !m_seen.empty() && m_seen.contains(id); }

        /// How far on (0 to 1) the element's `channel` is while it runs; nothing when it doesn't.
        std::optional<float> Value(ID id, TransitionChannel channel, float elapsedSecs) const;
        /// True while one of the element's channels runs.
        bool IsRunning(ID id) const { return m_index.contains(id); }
        /// True while any element's channel runs.
        bool HasRunning() const { return !m_slots.empty(); }

        /// The element's box `base` in `currentState`: the styles of the states its running
        /// channels are between, each by how far they are (at most StyleBlend::kMaxStyles,
        /// the heaviest), or its current style alone.
        TransitionState Resolve(ID id, VisualBoxStyleIndex base,
                                ElementState currentState, float elapsedSecs) const;

        /// Once a frame, after the build: lets go of the channels that have ended, and of the
        /// elements without one; forgets what OnStateSeen saw of elements not built this frame.
        void Purge(float elapsedSecs);

        /// Removes all active transitions, for instance after a reorder invalidates ID mappings.
        void Clear() { m_slots.clear(); m_index.clear(); m_seen.clear(); }

        /// Test/debug seam: when disabled, states snap instantly (deterministic rendering).
        /// Disabling drops the active transitions.
        void SetEnabled(bool enabled) { m_enabled = enabled; if (!enabled) Clear(); }
        /// True unless SetEnabled(false) was called.
        bool IsEnabled() const { return m_enabled; }

        /// Reduced motion: every change is instant. Turning it on drops the active transitions.
        void SetReducedMotion(bool reduced) { m_reducedMotion = reduced; if (reduced) { m_slots.clear(); m_index.clear(); } }
        bool IsReducedMotion() const { return m_reducedMotion; }

    private:
        /// The timings a channel can run in, by number (TimingFor's and kToggle).
        enum class Timing : uint8_t { HoverIn, HoverOut, Release, Focus, Toggle, Invalid };
        static const TransitionTiming& TimingOf(Timing timing);

        /// An element with a channel running: for each running channel, where it heads and when
        /// it would have started from the other end to be where it is now.
        struct Slot
        {
            ID                                          id;
            uint8_t                                     running = 0;   ///< a bit per TransitionChannel
            uint8_t                                     on      = 0;   ///< ... where it heads: on or off
            std::array<Timing, kTransitionChannelCount> timing {};
            std::array<float, kTransitionChannelCount>  start  {};
        };
        struct Seen
        {
            ElementState state;     ///< the watched bits
            uint32_t     frame;     ///< the frame it was last built in
        };

        /// How far on `channel` of `slot` is at `elapsedSecs` (its eased value).
        static float ValueOf(const Slot& slot, int channel, float elapsedSecs);
        /// Starts `channel` toward `on` in `timing`, from where it is (`was` when not running).
        void Run(ID id, int channel, bool on, Timing timing, bool was, float elapsedSecs);
        /// Removes the slot at `index`, moving the last one into its place.
        void Remove(size_t index);

        std::vector<Slot>                  m_slots;
        std::unordered_map<ID, uint32_t>   m_index;   ///< element -> its slot
        std::unordered_map<ID, Seen>       m_seen;
        uint32_t m_frame         = 0;
        bool     m_enabled       = true;
        bool     m_reducedMotion = false;
    };

    /// @}
} // namespace tachyon
