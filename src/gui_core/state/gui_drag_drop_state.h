#pragma once

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/input/gui_input_enums.h>

#include <any>
#include <functional>
#include <span>
#include <stdint.h>
#include <string>
#include <vector>

namespace tachyon
{
    /// @addtogroup elements
    /// @{

    struct InputState;
    enum ImageIndex : uint32_t;   ///< defined in gui_image.h

    /// Payload types the library defines, for payloads any control may take. They sit at the
    /// top of the range: an application's own types are any other non-zero value (a
    /// four-character code reads well in a debugger, 0x47524944 for 'GRID').
    namespace DragPayload
    {
        inline constexpr uint32_t Image = 0xFFFF0001u;   ///< a DraggedImage
        inline constexpr uint32_t Files = 0xFFFF0002u;   ///< a DraggedFiles
    }

    /// A picture being dragged (DragPayload::Image). It refers to the source's pixels rather
    /// than copying them: the source keeps them alive while the drag lasts and in the frame of
    /// the drop.
    struct DraggedImage
    {
        ImageIndex               image{};   ///< the registered image, for drawing it
        uint2                    size{};    ///< in pixels
        std::span<const uint8_t> rgba;      ///< size.x * size.y * 4 bytes; empty when only drawn
    };

    /// Files being dragged inside the application (DragPayload::Files): paths, UTF-8.
    struct DraggedFiles
    {
        std::vector<std::string> paths;
    };

    /// What a DragVisualizer draws from: the pointer, the source and target, and the payload.
    struct DragVisualContext
    {
        tachyon::int2 screenPos{ 0, 0 };
        ID sourceID = ID::Invalid();
        ID targetID = ID::Invalid();
        bool isDropValid = false;
        bool leftMouseDown = false;

        uint32_t payloadType = 0;
        uintptr_t payloadValue = 0;
        const std::any* payload = nullptr;
    };

    /// Draws the dragged item while a drag is in progress.
    using DragVisualizer = std::function<void(const DragVisualContext& context)>;

    /// Drag-and-drop state, owned by GUIStateManager. Begin arms a pending source on mouse-down;
    /// it becomes the active drag once the pointer passes the start distance. Per-frame events
    /// (enter / over / out / drop) are cleared at the top of the next EndFrame.
    class DragDropState
    {
        ID        m_pendingDragSourceID = ID::Invalid();
        ID        m_draggedID           = ID::Invalid();
        ID        m_dragOverID          = ID::Invalid();
        int2      m_dragStartScreenPos{ 0, 0 };
        uint32_t  m_dragPayloadType     = 0;
        uintptr_t m_dragPayloadValue    = 0;
        std::any  m_dragPayload;

        DragVisualizer m_sourceDragVisualizer;
        DragVisualizer m_frameVisualizerOverrideCurrent;
        DragVisualizer m_frameVisualizerOverrideNext;

        ID m_dragEnterTargetID     = ID::Invalid();
        ID m_dragOverTargetID      = ID::Invalid();
        ID m_dragOutTargetID       = ID::Invalid();
        ID m_dragDropTargetID      = ID::Invalid();
        ID m_dragValidatedTargetID = ID::Invalid();
        ID m_dragTakerID           = ID::Invalid();

        ID        m_dragEventSourceID       = ID::Invalid();
        uint32_t  m_dragEventPayloadType    = 0;
        uintptr_t m_dragEventPayloadValue   = 0;
        std::any  m_dragEventPayload;       ///< a dropped payload, kept until its Drop is dispatched
        int32_t   m_startDistance           = kDragStartDistance;

        ID   ResolveDropTarget(ID hoveredID) const;
        void ClearDrag();
        void SetEventPayload(ID sourceID, uint32_t payloadType, uintptr_t payloadValue);

    public:
        // Query
        bool IsDragging()  const { return m_draggedID != ID::Invalid(); }
        ID   GetDraggedID()  const { return m_draggedID;  }
        ID   GetDragOverID() const { return m_dragOverID; }
        uintptr_t GetPayloadValue() const { return m_dragPayloadValue; }

        /// True when an active drag is over `id` and its payload type is `acceptedPayloadType`
        /// (0 accepts any type).
        bool IsDropTargetFor(ID id, uint32_t acceptedPayloadType = 0) const;

        ID GetEnterTargetID()     const { return m_dragEnterTargetID;     }
        ID GetOverTargetID()      const { return m_dragOverTargetID;      }
        ID GetOutTargetID()       const { return m_dragOutTargetID;       }
        ID GetDropTargetID()      const { return m_dragDropTargetID;      }
        ID GetValidatedTargetID() const { return m_dragValidatedTargetID; }
        /// The element whose handler took the drag under the pointer: the validated target or
        /// one of its ancestors. Invalid when nothing there takes it.
        ID GetTakerID()           const { return m_dragTakerID;           }

        ID        GetEventSourceID()       const { return m_dragEventSourceID;     }
        uint32_t  GetEventPayloadType()    const { return m_dragEventPayloadType;  }
        uintptr_t GetEventPayloadValue()   const { return m_dragEventPayloadValue; }
        const std::any& GetPayload()       const { return m_dragPayload; }
        /// The payload the drag events of this frame carry: the drag's own, or the one just
        /// dropped (the drag itself is over by then).
        const std::any& GetEventPayload()  const { return m_dragEventPayload.has_value() ? m_dragEventPayload : m_dragPayload; }

        // Mutation

        /// Arms a pending drag from `sourceID`; ignored for an Invalid source or payload type 0.
        /// `payload` defaults to `payloadValue`.
        void Begin(ID sourceID, uint32_t payloadType, uintptr_t payloadValue, int2 startScreenPos,
                   std::any payload = {}, DragVisualizer sourceVisualizer = {});
        void SetSourceVisualizer(DragVisualizer sourceVisualizer);
        void UpdateFromInput(const InputState& in);
        /// Pointer travel in pixels before a pending drag starts.
        void SetStartDistance(int32_t pixels) { m_startDistance = pixels; }
        void Cancel();

        /// The element under the pointer whose chain took the drag (`validated`), and the one in
        /// that chain that took it (`taker`).
        void SetValidatedTargetID(ID validated, ID taker) { m_dragValidatedTargetID = validated; m_dragTakerID = taker; }
        void SetValidatedTargetID(ID id) { SetValidatedTargetID(id, id); }

        void SetFrameVisualizerOverride(DragVisualizer visualizer) { m_frameVisualizerOverrideNext = std::move(visualizer); }
        void ClearFrameVisualizerOverride();
        bool InvokeActiveVisualizer(const DragVisualContext& context) const;

        /// Clears per-frame events. GUIStateManager::EndFrame calls it; the input manager may too.
        void ClearEvents();

        // Frame hooks
        void BeginFrame();
        void EndFrameUpdate(ID hoveredForDrop, bool leftButtonDown);
    };
    /// @}
}
