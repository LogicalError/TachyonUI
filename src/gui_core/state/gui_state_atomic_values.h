#pragma once

#include <assert.h>
#include <stdint.h>

#include <gui_core/gui_id.h>
#include <gui_core/diagnostics/gui_logging.h>

// Small state values owned by GUIStateManager.

namespace tachyon
{
    /// @addtogroup elements
    /// @{

    enum class MouseCursor : uint8_t;   // gui_core/style/gui_style.h
    enum class ElementState : uint8_t;  // gui_core/style/gui_style.h

    /// The mouse cursor requested this frame.
    class CursorState
    {
        /// 0 = MouseCursor::Default.
        MouseCursor m_requested{};

    public:
        void        Request(MouseCursor c) { m_requested = c; }
        void        Clear()                { m_requested = {}; }
        MouseCursor Get() const            { return m_requested; }
    };

    /// The focused text editor, plus the one as of the last Commit, to detect changes.
    class TextEditorFocusState
    {
        ID m_focusedID     = ID::Invalid();
        ID m_prevFocusedID = ID::Invalid();

    public:
        ID GetFocusedID()     const { return m_focusedID;     }
        /// The focused editor as of the last Commit, when the text system switched its editor
        /// to it: the field the editor holds.
        ID GetPrevFocusedID() const { return m_prevFocusedID; }

        void Set(ID id) { m_focusedID = id; }
        void Clear()    { m_focusedID = ID::Invalid(); }
        void Commit()   { m_prevFocusedID = m_focusedID; }
    };

    /// Construction-time Push/Pop nesting read by GUIStateManager::GetCurrentState.
    /// Reset at BeginFrame, balance-checked at EndFrame.
    class ConstructionScopes
    {
        uint32_t m_disabledLevel      = 0;
        uint32_t m_selectedLevel      = 0;
        uint32_t m_indeterminateLevel = 0;
        uint32_t m_invalidLevel       = 0;
        // PushShownState's states, innermost last.
        static constexpr uint32_t kMaxShownStates = 8;
        uint8_t      m_shownStates[kMaxShownStates] {};
        uint32_t     m_shownLevel = 0;

    public:
        void PushDisabled() { m_disabledLevel++; }
        void PopDisabled()  { if (m_disabledLevel > 0) m_disabledLevel--; else ReportUnmatchedPop("ConstructionScopes: PopDisabled without PushDisabled (ignored)"); }

        void PushSelected() { m_selectedLevel++; }
        void PopSelected()  { if (m_selectedLevel > 0) m_selectedLevel--; else ReportUnmatchedPop("ConstructionScopes: PopSelected without PushSelected (ignored)"); }

        void PushInvalid() { m_invalidLevel++; }
        void PopInvalid()  { if (m_invalidLevel > 0) m_invalidLevel--; else ReportUnmatchedPop("ConstructionScopes: PopInvalid without PushInvalid (ignored)"); }

        void PushIndeterminate() { m_indeterminateLevel++; }
        void PopIndeterminate()  { if (m_indeterminateLevel > 0) m_indeterminateLevel--; else ReportUnmatchedPop("ConstructionScopes: PopIndeterminate without PushIndeterminate (ignored)"); }

        /// Everything built until PopShownState looks as in `states` too (Hover, Active,
        /// Focused): a picture of a control's states side by side, for a preview or a
        /// screenshot. Only the look: the pointer and the keyboard act as before.
        void PushShownState(ElementState states)
        {
            if (m_shownLevel < kMaxShownStates) m_shownStates[m_shownLevel] = static_cast<uint8_t>(states);
            m_shownLevel++;
        }
        void PopShownState() { if (m_shownLevel > 0) m_shownLevel--; else ReportUnmatchedPop("ConstructionScopes: PopShownState without PushShownState (ignored)"); }
        /// The states the PushShownState scopes add.
        ElementState ShownStates() const
        {
            uint8_t s = 0;
            for (uint32_t i = 0; i < m_shownLevel && i < kMaxShownStates; ++i) s = uint8_t(s | m_shownStates[i]);
            return static_cast<ElementState>(s);
        }

        bool IsDisabled()      const { return m_disabledLevel      > 0; }
        bool IsSelected()      const { return m_selectedLevel      > 0; }
        bool IsIndeterminate() const { return m_indeterminateLevel > 0; }
        bool IsInvalid()       const { return m_invalidLevel       > 0; }

        void Reset()            { m_disabledLevel = 0; m_selectedLevel = 0; m_indeterminateLevel = 0; m_invalidLevel = 0; m_shownLevel = 0; }
        /// Clears the scopes for something built inside them that isn't part of them (a
        /// tooltip on a disabled control isn't disabled); Restore puts them back.
        ConstructionScopes SetAside() { const ConstructionScopes kept = *this; Reset(); return kept; }
        void Restore(const ConstructionScopes& kept) { *this = kept; }
        bool IsBalanced() const { return m_disabledLevel == 0 && m_selectedLevel == 0 && m_indeterminateLevel == 0 && m_invalidLevel == 0 && m_shownLevel == 0; }

    private:
        /// A Pop without its Push is reported and ignored.
        static void ReportUnmatchedPop(const char* message)
        {
            Diagnostics::ReportMisuse(Diagnostics::MakeLogCategory("state"), message);
        }
    };
    /// @}
}
