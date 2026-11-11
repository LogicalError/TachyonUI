#include <gui_controls/gui_transition_manager.h>

#include <gui_core/graphics/gui_renderer.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_ref.h>

#include <algorithm>

namespace tachyon
{
    namespace
    {
        // Each channel's state bit, in TransitionChannel's order.
        constexpr ElementState kChannelBits[kTransitionChannelCount] = {
            ElementState::Hover, ElementState::Active, ElementState::Focused, ElementState::Selected, ElementState::Invalid };
        // The channels that pick a style (Invalid draws its own halo instead).
        constexpr int kStyleChannels = 4;

        bool Has(ElementState state, ElementState bit) { return (state & bit) != ElementState::None; }
    }

    uint32_t ComputeGPUStyleIndex(VisualBoxStyleIndex base, ElementState state)
    {
        return GUIRenderer::StyleIndex(base, state);
    }

    const TransitionTiming& TransitionManager::TimingOf(Timing timing)
    {
        static constexpr TransitionTiming kTimings[] = { kHoverIn, kHoverOut, kRelease, kFocus, kToggle, kInvalid };
        return kTimings[size_t(timing)];
    }

    TransitionTiming TransitionManager::TimingFor(TransitionChannel channel, bool on)
    {
        switch (channel)
        {
            case TransitionChannel::Hover:    return on ? kHoverIn : kHoverOut;
            case TransitionChannel::Active:   return on ? TransitionTiming{} : kRelease;
            case TransitionChannel::Focused:  return kFocus;
            case TransitionChannel::Selected: return {};
            case TransitionChannel::Invalid:  return on ? kInvalid : TransitionTiming{};
        }
        return {};
    }

    float TransitionManager::ValueOf(const Slot& slot, int channel, float elapsedSecs)
    {
        const TransitionTiming& timing = TimingOf(slot.timing[size_t(channel)]);
        const float progress = std::clamp((elapsedSecs - slot.start[size_t(channel)]) / timing.duration, 0.0f, 1.0f);
        const float eased    = timing.easing(progress);
        return (slot.on & (1u << channel)) != 0 ? eased : 1.0f - eased;
    }

    void TransitionManager::Remove(size_t index)
    {
        m_index.erase(m_slots[index].id);
        if (index + 1 != m_slots.size())
        {
            m_slots[index] = m_slots.back();
            m_index[m_slots[index].id] = uint32_t(index);
        }
        m_slots.pop_back();
    }

    void TransitionManager::Run(ID id, int channel, bool on, Timing timing, bool was, float elapsedSecs)
    {
        const auto found = m_index.find(id);
        const uint8_t bit = uint8_t(1u << channel);
        const bool instant = TimingFor(TransitionChannel(channel), on).duration <= 0.0f && timing != Timing::Toggle;
        // Where it is now: carries on from there, toward the other end.
        Slot* slot = found != m_index.end() ? &m_slots[found->second] : nullptr;
        const bool running = slot && (slot->running & bit) != 0;
        if (running && ((slot->on & bit) != 0) == on && !instant && !m_reducedMotion)
            return;   // already heading there
        const float value = running ? ValueOf(*slot, channel, elapsedSecs) : (was ? 1.0f : 0.0f);
        const TransitionTiming& t = TimingOf(timing);
        const float progress = t.easing.TimeAt(on ? value : 1.0f - value);
        if (instant || m_reducedMotion || progress >= 1.0f)
        {
            // Shows its new state from now on.
            if (slot)
            {
                slot->running &= uint8_t(~bit);
                if (slot->running == 0)
                    Remove(found->second);
            }
            return;
        }
        if (!slot)
        {
            m_index[id] = uint32_t(m_slots.size());
            m_slots.push_back(Slot{ .id = id });
            slot = &m_slots.back();
        }
        slot->running |= bit;
        slot->on = on ? uint8_t(slot->on | bit) : uint8_t(slot->on & ~bit);
        slot->timing[size_t(channel)] = timing;
        slot->start[size_t(channel)]  = elapsedSecs - progress * t.duration;
    }

    void TransitionManager::OnStateChange(ID id, ElementState fromState, ElementState toState, float elapsedSecs)
    {
        if (!m_enabled)
            return;
        const ElementState changed = fromState ^ toState;
        if (Has(changed, ElementState::Disabled))
        {
            if (const auto found = m_index.find(id); found != m_index.end())
                Remove(found->second);
            return;
        }
        for (int c = 0; c < kTransitionChannelCount; ++c)
        {
            const ElementState bit = kChannelBits[c];
            if (!Has(changed, bit))
                continue;
            const bool on = Has(toState, bit);
            Timing timing = Timing::HoverIn;
            switch (TransitionChannel(c))
            {
                case TransitionChannel::Hover:    timing = on ? Timing::HoverIn : Timing::HoverOut; break;
                case TransitionChannel::Active:   timing = Timing::Release; break;
                case TransitionChannel::Focused:  timing = Timing::Focus; break;
                case TransitionChannel::Selected: timing = Timing::Focus; break;   // instant (TimingFor)
                case TransitionChannel::Invalid:  timing = Timing::Invalid; break;
            }
            Run(id, c, on, timing, Has(fromState, bit), elapsedSecs);
        }
    }

    void TransitionManager::OnStateSeen(ID id, ElementState state, ElementState watched, float elapsedSecs)
    {
        const ElementState now = state & watched;
        const auto [it, added] = m_seen.try_emplace(id, Seen{ now, m_frame });
        if (added)
            return;
        const ElementState was = it->second.state;
        it->second = Seen{ now, m_frame };
        const ElementState flipped = was ^ now;
        if (flipped == ElementState::None || !m_enabled)
            return;
        if (Has(flipped, ElementState::Selected))
            Run(id, int(TransitionChannel::Selected), Has(now, ElementState::Selected), Timing::Toggle,
                Has(was, ElementState::Selected), elapsedSecs);
        if (Has(flipped, ElementState::Invalid))
            Run(id, int(TransitionChannel::Invalid), Has(now, ElementState::Invalid), Timing::Invalid,
                Has(was, ElementState::Invalid), elapsedSecs);
    }

    std::optional<float> TransitionManager::Value(ID id, TransitionChannel channel, float elapsedSecs) const
    {
        const auto found = m_index.find(id);
        if (found == m_index.end())
            return std::nullopt;
        const Slot& slot = m_slots[found->second];
        if ((slot.running & (1u << int(channel))) == 0)
            return std::nullopt;
        return ValueOf(slot, int(channel), elapsedSecs);
    }

    TransitionState TransitionManager::Resolve(ID id, VisualBoxStyleIndex base,
                                               ElementState currentState, float elapsedSecs) const
    {
        TransitionState r;
        r.current = ComputeGPUStyleIndex(base, currentState);
        r.styles  = StyleBlend::Single(r.current);

        const auto found = m_index.find(id);
        if (found == m_index.end())
            return r;
        const Slot& slot = m_slots[found->second];

        // The running channels that pick a style, and how far on each is.
        int   channels[kStyleChannels];
        float values[kStyleChannels];
        int   k = 0;
        ElementState runningBits = ElementState::None;
        for (int c = 0; c < kStyleChannels; ++c)
            if ((slot.running & (1u << c)) != 0)
            {
                channels[k] = c;
                values[k]   = ValueOf(slot, c, elapsedSecs);
                runningBits |= kChannelBits[c];
                ++k;
            }
        if (k == 0)
            return r;

        // Each mix of those channels on or off is a state; its share is how far each is.
        struct Part { uint32_t style; float weight; };
        Part parts[1 << kStyleChannels];
        int count = 0;
        for (int mask = 0; mask < (1 << k); ++mask)
        {
            ElementState state = currentState & ~runningBits;
            float weight = 1.0f;
            for (int j = 0; j < k; ++j)
            {
                const bool on = (mask & (1 << j)) != 0;
                weight *= on ? values[j] : 1.0f - values[j];
                if (on)
                    state |= kChannelBits[channels[j]];
            }
            if (weight <= 0.0f)
                continue;
            const uint32_t style = ComputeGPUStyleIndex(base, state);
            Part* same = std::find_if(parts, parts + count, [style](const Part& p) { return p.style == style; });
            if (same != parts + count)
                same->weight += weight;
            else
                parts[count++] = Part{ style, weight };
        }

        // The heaviest few, scaled to add up to all of it.
        std::sort(parts, parts + count, [](const Part& a, const Part& b) { return a.weight > b.weight; });
        count = std::min(count, StyleBlend::kMaxStyles);
        float total = 0.0f;
        for (int i = 0; i < count; ++i)
            total += parts[i].weight;
        if (count == 0 || total <= 0.0f)
            return r;
        r.styles.count = uint8_t(count);
        for (int i = 0; i < StyleBlend::kMaxStyles; ++i)
        {
            r.styles.styles[size_t(i)]  = i < count ? parts[i].style : 0;
            r.styles.weights[size_t(i)] = i < count ? parts[i].weight / total : 0.0f;
        }
        return r;
    }

    void TransitionManager::Purge(float elapsedSecs)
    {
        for (size_t i = 0; i < m_slots.size(); )
        {
            Slot& slot = m_slots[i];
            for (int c = 0; c < kTransitionChannelCount; ++c)
                if ((slot.running & (1u << c)) != 0
                    && elapsedSecs - slot.start[size_t(c)] >= TimingOf(slot.timing[size_t(c)]).duration)
                    slot.running &= uint8_t(~(1u << c));
            if (slot.running == 0)
                Remove(i);   // the last one moves here: looked at next
            else
                ++i;
        }
        std::erase_if(m_seen, [this](const auto& seen) { return seen.second.frame != m_frame; });
        ++m_frame;
    }

} // namespace tachyon
