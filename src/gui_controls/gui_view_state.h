#pragma once

#include <gui_controls/gui_application.h>
#include <gui_controls/gui_curve_types.h>
#include <gui_controls/gui_node_view_types.h>
#include <gui_controls/gui_timeline_types.h>
#include <gui_controls/gui_viewport_types.h>

#include <string>
#include <string_view>

namespace tachyon
{
    /// @addtogroup controls_data
    /// @{

    /// A view's own state as text, for a panel to keep with its layout (Panel::saveState and
    /// Panel::restoreState), and back. Restore returns false, and changes nothing, for text it
    /// can't read; a value it doesn't know stays as it is.
    ///
    ///     panel.saveState    = [this] { return SaveViewState(graph); };
    ///     panel.restoreState = [this](std::string_view state) { RestoreViewState(graph, state); };

    /// A node view's pan and zoom (NodeView::transform).
    std::string SaveViewState(const NodeView& view);
    bool        RestoreViewState(NodeView& view, std::string_view state);

    /// The part of a timeline in view and its zoom slider.
    std::string SaveViewState(const TimelineView& view);
    bool        RestoreViewState(TimelineView& view, std::string_view state);

    /// The part of a curve plot in view in its editor. Empty until the editor has shown it.
    std::string SaveViewState(const CurveView& view);
    bool        RestoreViewState(CurveView& view, std::string_view state);

    /// A viewport's camera: where it looks, from how far and which way, its field of view and
    /// whether it's orthographic.
    std::string SaveViewState(const ViewportCamera& camera);
    bool        RestoreViewState(ViewportCamera& camera, std::string_view state);

    /// The settings page open, by its name. False when no page has that name: the one open
    /// stays.
    std::string SaveViewState(const GUIApplication::SettingsPanelState& settings);
    bool        RestoreViewState(GUIApplication::SettingsPanelState& settings, std::string_view state);

    /// @}
}
