#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <gui_core/input/gui_input_state.h>
#include <gui_core/input/gui_input_enums.h>
#include <gui_core/input/gui_platform_input_handler.h>
#include <gui_core/input/gui_input_handler_func.h>

namespace tachyon
{
    /// @addtogroup input
    /// @{

    class ScrollManagement;
    class ElementStorage;
    class GUIStateManager;
    class TabChainManager;
    class ITextSystem;

    /// A single OS input event recorded for dispatch this frame.
    struct RecordedInputEvent
    {
        EventType  type     = EventType::None;
        InputState state    = {};
        tachyon::ID    windowId = tachyon::ID::Invalid(); ///< OS window that received the event
        bool       consumed = false; ///< already handled; Dispatch skips it
    };

    /// Receives raw OS events, records them, and runs the frame's input pipeline (tab
    /// navigation, text editor focus, dispatch, post-dispatch).
    ///
    /// With a text system set, every event also reaches it. Keys and IME text go to it at
    /// once while the focus can't have moved; after a press, Tab, Enter or Escape this frame
    /// they wait for the dispatch, which hands each one over once the events before it moved
    /// the focus, so typing right after a Tab or a click lands in the field it moved to.
    class InputManager : public IPlatformInputHandler
    {
    public:
        InputManager() = default;
        ~InputManager() = default;

        /// The text system the events are passed on to; null for none.
        void SetTextSystem(ITextSystem* textSystem) { m_textSystem = textSystem; }

        /// IPlatformInputHandler: record OS events for this frame's dispatch.
        ///@{
        void OnKeyboard(tachyon::ID windowId, KeyCode key, int repeat, KeyModifier modifiers, KeyAction action) override;
        void OnMouseMove(tachyon::ID windowId, tachyon::int2 screenPos, tachyon::MouseButtons buttons, KeyModifier modifiers) override;
        void OnMouseButton(tachyon::ID windowId, tachyon::int2 screenPos, tachyon::MouseButtons buttons, KeyModifier modifiers, MouseAction action) override;
        void OnMouseScroll(tachyon::ID windowId, tachyon::int2 screenPos, tachyon::float2 scrollOffset, KeyModifier modifiers) override;
        void OnTextComposition(tachyon::ID windowId, std::string_view text, int32_t cursor) override;
        void OnFileDrop(tachyon::ID windowId, tachyon::int2 screenPos, FileDropAction action, std::string_view data) override;
        void OnPen(tachyon::ID windowId, const PenState& pen) override;
        /// Recorded as a MouseScroll with InputState::pinch, where the pointer is.
        void OnPinch(tachyon::ID windowId, float scale, KeyModifier modifiers) override;
        ///@}

        /// The pen as of the latest OnPen; a default PenState while the mouse has the pointer.
        /// A pressure change without a move reaches elements with the next pointer event.
        const PenState& GetPen() const { return m_pen; }

        /// The drag from another application over one of the app's windows now; null when there's none.
        const FileDrag* GetFileDrag() const { return m_fileDrag.windowID != ID::Invalid() ? &m_fileDrag : nullptr; }
        /// The drops from other applications since the last call, oldest first. A drag that left
        /// without dropping anything isn't one.
        std::vector<FileDrop> TakeFileDrops() { return std::exchange(m_fileDrops, {}); }

        /// Pushes a global handler. Global handlers are tried last-pushed first, after
        /// element-level handlers had a chance to consume the input.
        void PushGlobalHandler(EventHandler h)
        {
            if (h) m_globalHandlers.push_back(std::move(h));
        }

        /// Pops the last pushed global handler; false when there is none.
        bool PopGlobalHandler()
        {
            if (m_globalHandlers.empty()) return false;
            m_globalHandlers.pop_back();
            return true;
        }

        /// Marks the most recently recorded keyboard event as consumed, so Dispatch skips it.
        void ConsumeKey();

        /// This frame's recorded events, e.g. to read them before Process.
        const std::vector<RecordedInputEvent>& GetRecordedEvents() const { return m_recordedEvents; }
        /// Marks recorded event `index` as consumed, for something that acted on it while building.
        void ConsumeEvent(size_t index) { if (index < m_recordedEvents.size()) m_recordedEvents[index].consumed = true; }

        /// Mouse buttons held as of the last recorded button event.
        MouseButtons GetCurrentButtons() const { return m_currentButtons; }
        /// Whether a drag held with `button` was left behind: the button is up and no release
        /// is queued for this frame. Drops a drag whose release never reached it (its element
        /// went away, the window lost the mouse).
        bool IsDragAbandoned(MouseButtons button = MouseButtons::Left) const;

        /// Updates the drag position from the latest recorded mouse event; call before hover
        /// resolution and GUIStateManager::EndFrame.
        void UpdatePreHover(GUIStateManager& guiStateManager);

        /// Detects mouse button presses the OS swallowed (e.g. clicking behind a parented popup
        /// window on Windows) and injects synthetic MouseDown/MouseUp events for them. Call after
        /// the window manager's Update, before Process.
        void SynthesizeMissingMouseEvents(int2 screenPos,MouseButtons lastPhysicalButtons, MouseButtons physicalButtons);

        /// Per-frame input processing: tab navigation, text editor focus, dispatch to element
        /// handlers, and post-dispatch state updates. Call after guiStateManager.EndFrame().
        void Process(GUIStateManager& guiStateManager,
                     ElementStorage& elementStorage,
                     TabChainManager& tabChainManager,
                     ITextSystem* textSystem,
                     ID overID);

        /// Dispatches each recorded event to elements by capture / focus / hover rules, then
        /// clears the events. Process calls it.
        void Dispatch(GUIStateManager& guiStateManager, ElementStorage& storage);

    private:
        static bool IsMouseEvent(EventType type);
        static bool IsKeyboardEvent(EventType type);

        void DispatchSingleEvent(RecordedInputEvent& recorded, GUIStateManager& guiStateManager, ElementStorage& storage);

        /// Records a button event without passing it on (a press the OS missed).
        void RecordMouseButton(int2 screenPos, MouseButtons buttons, KeyModifier modifiers, MouseAction action);

        /// A key or IME text for the text system, held until the focus settles.
        struct HeldTextInput
        {
            uint32_t    beforeEvent = 0;   ///< handed over before this recorded event is dispatched
            ID          windowId    = ID::Invalid();
            KeyCode     key         = KeyCode::None;
            KeyModifier modifiers   = KeyModifier::None;
            KeyAction   action      = KeyAction::KeyDown;
            int32_t     cursor      = -1;  ///< >= 0: IME text in `composition`
            std::string composition;
        };

        /// Hands the held text input due before recorded event `upTo` to the text system, once
        /// the text focus follows the focus the events before it left.
        void HandOverHeldTextInput(size_t upTo, size_t& next, GUIStateManager& guiStateManager);

        /// The pen a pointer event carries with `buttons` held: the pen's own, or for the mouse,
        /// pressure 1 while a button is held.
        PenState PointerPen(MouseButtons buttons) const;

        /// Persists across frames for computing per-event position deltas and button states.
        int2          m_prevScreenPos  {};
        MouseButtons  m_currentButtons { MouseButtons::None };
        ID            m_prevFocusedID  = ID::Invalid();
        PenState      m_pen;

        ITextSystem*  m_textSystem     = nullptr;
        bool          m_holdTextInput  = false;   ///< the focus may move this frame: hold keys

        std::vector<RecordedInputEvent> m_recordedEvents;
        std::vector<HeldTextInput>      m_heldTextInput;
        std::vector<EventHandler>       m_globalHandlers;

        FileDrag              m_fileDrag;
        FileDrop              m_dropping;    ///< what the drag over a window dropped so far
        std::vector<FileDrop> m_fileDrops;
    };
    /// @}
}
