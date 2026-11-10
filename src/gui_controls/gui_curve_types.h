#pragma once

#include <gui_core/gui_base_types.h>
#include <gui_core/gui_id.h>
#include <gui_core/input/gui_pointer_drag.h>
#include <gui_controls/gui_widget_state_types.h>   // DoubleClickTracker

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace tachyon
{
    /// @addtogroup app
    /// @{

    // =========================================================================
    // Curves
    // =========================================================================

    /// How a key's tangents are set: worked out from its neighbors, or by the user.
    enum class CurveTangentMode : uint8_t
    {
        ClampedAuto,  ///< smooth through the key, without overshooting its neighbors (the default)
        Auto,         ///< smooth through the key; may overshoot
        FreeSmooth,   ///< one slope the user sets, the same on both sides
        Flat,         ///< level on both sides
        Broken,       ///< each side its own, as inMode and outMode say
    };

    /// How one side of a Broken key runs to its neighbor.
    enum class CurveSideMode : uint8_t
    {
        Free,      ///< the slope the user sets
        Linear,    ///< straight to the neighbor
        Constant,  ///< holds this side's value up to the next key (a step)
    };

    /// What a curve does before its first key or after its last.
    enum class CurveWrapMode : uint8_t
    {
        Clamp,     ///< holds the end key's value
        Loop,      ///< repeats the curve
        PingPong,  ///< repeats it, every other time backwards
    };

    /// A key: a time where the curve has a value, and its slopes on either side.
    struct CurveKey
    {
        ID               stableID = ID::Invalid();   ///< assigned once on creation, for its selection
        float            time     = 0.0f;
        float            value    = 0.0f;
        float            inSlope  = 0.0f;   ///< the slope arriving, value per time
        float            outSlope = 0.0f;   ///< the slope leaving
        CurveTangentMode mode     = CurveTangentMode::ClampedAuto;
        CurveSideMode    inMode   = CurveSideMode::Free;    ///< a Broken key's arriving side
        CurveSideMode    outMode  = CurveSideMode::Free;    ///< and its leaving side

        bool operator==(const CurveKey&) const = default;
    };

    /// A curve: keys in time order, joined by cubic (Hermite) segments that take their
    /// slopes, as an animation curve does.
    struct Curve
    {
        std::vector<CurveKey> keys;
        CurveWrapMode         preWrap  = CurveWrapMode::Clamp;   ///< before the first key
        CurveWrapMode         postWrap = CurveWrapMode::Clamp;   ///< after the last

        bool operator==(const Curve&) const = default;
    };

    /// The curve's value at `time`; 0 without keys. NaN or infinite times count as 0.
    ///     const float falloff = EvaluateCurve(curve, distance / radius);
    float EvaluateCurve(const Curve& curve, float time);

    /// Works out the slopes of the keys whose mode sets them (ClampedAuto, Auto, Flat, and
    /// the Linear sides of Broken ones) from their neighbors. The edits call it; call it
    /// after changing keys yourself.
    void UpdateCurveTangents(Curve& curve);

    /// A curve, with its keys' tangents worked out: one key at each `points` (time, value).
    Curve MakeCurve(std::span<const float2> points, CurveTangentMode mode = CurveTangentMode::ClampedAuto);

    /// The color a channel is drawn in.
    enum class CurveChannelColor : uint8_t
    {
        Value,   ///< one value: the warning color, as a scalar channel in the design
        Red,     ///< the X axis color
        Green,   ///< the Y axis color
        Blue,    ///< the Z axis color
        Alpha,   ///< the primary text color
    };

    /// One of a CurveView's curves, such as the red one of a color's.
    struct CurveChannel
    {
        std::string       name;   ///< shown in the editor when there are several: "R", "Alpha"
        CurveChannelColor color = CurveChannelColor::Value;
        bool              visible = true;   ///< drawn; a hidden channel's keys can't be picked
        Curve             curve;

        bool operator==(const CurveChannel&) const = default;
    };

    /// The span a CurveField shows, from timeMin to timeMax and valueMin to valueMax.
    struct CurveRange
    {
        float timeMin  = 0.0f;
        float timeMax  = 1.0f;
        float valueMin = 0.0f;
        float valueMax = 1.0f;

        bool operator==(const CurveRange&) const = default;
    };

    /// A named curve the editor offers to start from, as a thumbnail under the plot.
    struct CurvePreset
    {
        std::string name;    ///< its tooltip
        Curve       curve;   ///< over 0 to 1, both ways; stretched to the range when used
    };

    /// The built-in presets: constant, linear, ease in, ease out, ease in and out, and their
    /// mirror images.
    std::span<const CurvePreset> DefaultCurvePresets();

    /// A change to a CurveView, for undo: what each changed channel's curve was before and
    /// after. Every change the editor makes is one; ApplyCurveEdit does or undoes it.
    struct CurveEdit
    {
        struct Change { int channel = -1; Curve from, to; };
        std::vector<Change> changes;

        bool empty() const { return changes.empty(); }
    };

    /// A CurveField's curves and how they're shown, kept by the app from frame to frame.
    /// Everything the user changes comes to the app as a CurveEdit (onEdit), already applied,
    /// for its undo history.
    struct CurveView
    {
        std::vector<CurveChannel> channels;   ///< one for a plain curve; four for a color's R, G, B and A
        /// What the field shows, and where the editor opens. Keys stay in its time span; in
        /// its value span too with clampValues.
        CurveRange                range;
        bool                      clampValues = false;
        int                       activeChannel = 0;   ///< the one whose keys can be picked and added to
        std::unordered_set<ID>    selectedKeys;        ///< by stableID
        /// The thumbnails under the plot; empty: DefaultCurvePresets().
        std::vector<CurvePreset>  presets;

        /// A change the user made, already applied: put it in the undo history.
        std::function<void(const CurveEdit&)> onEdit;

        /// @name Internal: kept between frames while the user works
        ///@{
        CurveRange        shown;                ///< the part of the plot in view
        bool              shownSet = false;     ///< false until the editor first opens: then `range`
        bool              popupOpen = false;    ///< the field's editor window is open
        ID                popupID = ID::Invalid();
        PointerDrag       keyDrag;              ///< moving the selected keys
        float2            keyDragFrom {};       ///< the time and value under the press
        std::vector<Curve> before;              ///< the curves as they were, for Escape and the edit
        bool              keyMoved = false;
        PointerDrag       tangentDrag;          ///< a handle
        ID                tangentKey = ID::Invalid();
        bool              tangentOut = false;   ///< the leaving side's handle
        PointerDrag       boxSelect;
        float2            boxFrom {}, boxTo {};  ///< in time and value
        std::unordered_set<ID> boxBase;         ///< the selection before a Shift / Ctrl box
        PointerDrag       pan;                  ///< the right button held over the plot
        CurveRange        panFrom;
        ID                menuKey = ID::Invalid();   ///< the key a right click opened the menu for
        float2            menuAt {};             ///< where the right click was, in time and value
        bool              menuOpen = false;      ///< the plot's menu was open last frame
        IntRect           plotRect {};           ///< the plot's grid last frame, in screen pixels
        DoubleClickTracker plotClicks;          ///< a double click on the plot adds a key
        float2            lastClick {};          ///< where the last click on the plot was, in pixels
        uint32_t          revision = 0;          ///< counts the edits committed, for CurveField's return
        uint32_t          revisionSeen = 0;      ///< the count CurveField last saw
        ///@}
    };

    /// Does (or with `undo`, undoes) `edit` on `view`.
    void ApplyCurveEdit(CurveView& view, const CurveEdit& edit, bool undo = false);
    /// Applies `edit` and hands it to `view.onEdit`; does nothing for an empty one.
    void CommitCurveEdit(CurveView& view, const CurveEdit& edit);

    /// A key on `channel` at `time` with `value`, kept in the view's range; none on a hidden
    /// channel, or where the channel has a key at that time already.
    CurveEdit AddCurveKeyEdit(const CurveView& view, int channel, float time, float value, ID keyID);
    /// The selected keys gone, from every channel.
    CurveEdit DeleteSelectedCurveKeysEdit(const CurveView& view);
    /// The selected keys' tangent mode set; for Broken, each side's too.
    CurveEdit SetCurveTangentModeEdit(const CurveView& view, CurveTangentMode mode);
    /// The selected keys broken, and their arriving side (`in`), leaving side (`out`) or both
    /// set to `side`.
    CurveEdit SetCurveSideModeEdit(const CurveView& view, CurveSideMode side, bool in, bool out);
    /// `channel`'s curve made `preset` stretched over the view's range, with new key IDs from
    /// `makeID`.
    CurveEdit ApplyCurvePresetEdit(const CurveView& view, int channel, const CurvePreset& preset,
                                   const std::function<ID()>& makeID);
    /// A channel's wrap modes set.
    CurveEdit SetCurveWrapEdit(const CurveView& view, int channel, CurveWrapMode pre, CurveWrapMode post);

    /// @}
}
