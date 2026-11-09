#pragma once

#include <gui_core/style/gui_style_ref.h>
#include <gui_core/style/gui_style.h>

#include <gui_core/gui_result.h>
#include <gui_core/text/gui_text_system.h>

#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace tachyon
{
    /// @addtogroup styling
    /// @{

    class StyleManager;
    class ITextSystem;

    /// An accent color, one for dark palettes and one for light ones: the theme takes the one
    /// that suits the palette it loads. The colors derived from the accent follow it: the soft
    /// fill of selected rows, the menu highlight, the text selection, the caret, the focus
    /// and selection rings, and a hovered divider.
    struct AccentColors
    {
        color4b dark;    ///< for a dark palette
        color4b light;   ///< for a light palette

        bool operator==(const AccentColors&) const = default;
    };

    /// A named accent, for a settings page.
    struct AccentPreset
    {
        std::string_view name;
        AccentColors     colors;
    };

    /// The accents of the Graphite design: the palettes' own blue, and teal, violet and orange.
    inline constexpr AccentPreset kAccentPresets[] = {
        { "Blue",   { { 0x6B, 0x9B, 0xFF, 0xFF }, { 0x2F, 0x6F, 0xEB, 0xFF } } },
        { "Teal",   { { 0x34, 0xD1, 0xBF, 0xFF }, { 0x0E, 0x82, 0x77, 0xFF } } },
        { "Violet", { { 0xA9, 0x8B, 0xFF, 0xFF }, { 0x6D, 0x4F, 0xD6, 0xFF } } },
        { "Orange", { { 0xF5, 0x9E, 0x4B, 0xFF }, { 0xB8, 0x5C, 0x10, 0xFF } } },
    };

    /// Default control styles. GUITheme::controls points at the active bundle;
    /// BeginToolbar/EndToolbar swap in the toolbar variants.
    struct ControlStyles
    {
        StyleRef button;        ///< .button / .toolbar-button
        StyleRef toggle;        ///< .toggle / .toolbar-toggle
        StyleRef textbox;       ///< .textbox / .toolbar-textbox
        StyleRef number;        ///< numeric fields: the textbox in the monospace font
        StyleRef menuTrigger;   ///< .menu-trigger / .toolbar-menu-trigger
    };

    /// The library's styles, icons and theme metrics, the defaults the controls draw with.
    /// LoadTheme registers every class of the style files; each xxxStyleRef below is the
    /// class of the same name in kebab-case (tableRowAltStyleRef -> .table-row-alt) unless
    /// its comment names another. They are reassigned on every theme (re)load.
    ///
    /// An app's own classes (from SetStyleFiles) are found by name; look them up in OnGUI,
    /// since a reload registers them again:
    ///
    ///     Text("My themed app", theme.GetStyle("banner"));
    ///     Button("Careful!", onClick, theme.GetStyle("warning-button"));
    class GUITheme
    {
    public:
        ControlStyles regularControls {};                   ///< .button, .toggle, .textbox, .number-field, .menu-trigger
        ControlStyles toolbarControls {};                   ///< the .toolbar-* variants of those
        const ControlStyles* controls = &regularControls;   ///< the bundle in use

        StyleRef emptyHorzStyleRef;     ///< horizontal flex, no wrap; groups elements without drawing anything
        StyleRef emptyHorzWrapStyleRef; ///< horizontal flex, wrap
        StyleRef emptyVertStyleRef;     ///< vertical flex, no wrap
        StyleRef emptyVertWrapStyleRef; ///< vertical flex, wrap
        /// Built in code: an undecorated, growing, wrapping flex row (a multi-value drag field).
        StyleRef fieldElementGroupStyleRef;

        // View panel toolbars: side bins (one reorderable group per side), the per-toolbar
        // item container and the central content placeholder.
        StyleRef toolbarSideHorzStyleRef     {}; ///< toolbar side bin, top / bottom: row + flex-wrap
        StyleRef toolbarSideVertStyleRef     {}; ///< toolbar side bin, left / right: column + flex-wrap

        StyleRef toolbarItemHorzStyleRef     {}; ///< toolbar on a horizontal side: row, thumb left of body
        StyleRef toolbarItemVertStyleRef     {}; ///< toolbar on a vertical side: column, thumb above body
        StyleRef toolbarThumbHorzStyleRef    {}; ///< a toolbar's drag thumb on a horizontal side
        StyleRef toolbarThumbVertStyleRef    {}; ///< a toolbar's drag thumb on a vertical side

        /// Zero-thickness top / bottom side bin for an empty side, used only while no drag
        /// is in flight (during a drag the full bin is used, so the side can be hit).
        StyleRef toolbarSideHorzEmptyStyleRef {};
        StyleRef toolbarSideVertEmptyStyleRef {}; ///< the same, for left / right
        StyleRef toolbarFloatingWindowStyleRef {}; ///< floating tear-off window content host (no padding)

        StyleRef viewContentStyleRef          {}; ///< the central content placeholder

        StyleRef panelDropVisualizationStyleRef{}; ///< overlay showing where a dragged panel or toolbar docks
        StyleRef ghostWindowStyleRef {};   ///< BeginGhostWindow's default: the window following a drag
        StyleRef ghostIconStyleRef {};     ///< the icon in a drag ghost
        StyleRef textStyleRef {};          ///< .text-style: rich text
        StyleRef staticTextStyleRef {};    ///< plain non-interactive text
        StyleRef imageStyleRef {};            ///< default box for the Image() element
        StyleRef labelStyleRef {};       ///< Label()'s default
        StyleRef iconLabelStyleRef {};   ///< inline icon + text, centered (e.g. command cells)
        StyleRef iconLabelNoIconStyleRef {}; ///< icon-label with no icon, left-padded to align under icon'd siblings
        StyleRef dragLabelStyleRef {};   ///< DragLabel()'s default

        /// @name Button and toggle groups
        /// Tight-packed button/toggle group: the container and its items' style. Used by
        /// BeginButtonGroup / BeginToggleGroup; the items' :first-child and :last-child
        /// rules shape the group's ends.
        ///@{
        StyleRef buttonGroupStyleRef {};
        StyleRef groupButtonStyleRef {};   ///< .group-button
        StyleRef toggleGroupStyleRef {};
        StyleRef groupToggleStyleRef {};   ///< .group-toggle
        ///@}

        StyleRef dashedStyleRef{};          ///< a box with a dashed border
        StyleRef firstIconStyleRef {};      ///< .first-icon
        StyleRef iconLeftStyleRef {};   ///< icon left, text right
        StyleRef iconRightStyleRef {};  ///< text left, icon right
        StyleRef iconTopStyleRef {};    ///< icon above, text below
        StyleRef iconBottomStyleRef {}; ///< text above, icon below

        StyleRef knobStyleRef {};           ///< .knob
        StyleRef sliderFieldTrackStyleRef {}; ///< SliderField: the track holding the parts below
        StyleRef sliderFieldBgStyleRef {};   ///< unfilled half painted by the slider's custom-draw
        StyleRef sliderFieldFillStyleRef {}; ///< filled half painted by the slider's custom-draw
        StyleRef sliderFieldTextStyleRef {}; ///< label text overlaid on the track
        StyleRef sliderFieldValueStyleRef {}; ///< value text overlaid on the track
        StyleRef sliderFieldEdgeStyleRef {}; ///< the line ending the fill
        StyleRef sliderStyleRef {};          ///< Slider: a groove and knob, no value text
        StyleRef sliderGrooveStyleRef {};    ///< Slider: groove line painted by the custom-draw
        StyleRef sliderFillStyleRef {};      ///< Slider: the groove's filled part, up to the knob
        StyleRef sliderKnobStyleRef {};      ///< Slider: knob painted by the custom-draw
        StyleRef switchStyleRef {};          ///< Switch: the control (track + label)
        StyleRef switchTrackStyleRef {};     ///< Switch: the track; its knob is painted by the custom-draw
        StyleRef switchKnobStyleRef {};      ///< Switch: the knob
        StyleRef switchLabelStyleRef {};     ///< Switch: the label after the track
        /// @name Scrollbars
        /// Per axis: the track element, and the thumb custom-drawn on top of it.
        /// Tracks: .scrollbar-vertical / .scrollbar-horizontal; thumbs: the same + -thumb.
        ///@{
        StyleRef scrollbarVerticalTrackStyleRef   {};
        StyleRef scrollbarVerticalThumbStyleRef   {};
        StyleRef scrollbarHorizontalTrackStyleRef {};
        StyleRef scrollbarHorizontalThumbStyleRef {};
        ///@}
        int      scrollbarThickness = 12;         ///< .scrollbar-vertical width (read from CSS, at the UI scale)
        StyleRef colorSwatchStyleRef {};      ///< ColorField: the swatch
        StyleRef colorFieldStyleRef {};       ///< ColorField: swatch, hex and alpha in a row
        StyleRef colorHexStyleRef {};         ///< ColorField: the hex field
        StyleRef colorAlphaStyleRef {};       ///< ColorField: the alpha box (value and %)
        StyleRef colorAlphaInputStyleRef {};  ///< ColorField: the alpha value in its box
        StyleRef colorAlphaUnitStyleRef {};   ///< ColorField: the % after it
        StyleRef colorAlphaCaptionStyleRef {}; ///< ColorField: the "A" before it
        StyleRef colorPickerStyleRef {};      ///< color picker: the window
        StyleRef dialogHeaderStyleRef {};               ///< dialog: the title row
        StyleRef dialogTitleStyleRef {};      ///< dialog: its title
        StyleRef dialogContextStyleRef {};    ///< dialog: what's being edited, after the title
        StyleRef colorPickerHdrValueStyleRef {}; ///< color picker: the intensity value
        StyleRef dialogCloseStyleRef {};                ///< dialog: the close button
        StyleRef dialogFooterStyleRef {};               ///< dialog: status and buttons
        StyleRef dialogStatusStyleRef {};               ///< dialog: the footer's status text
        StyleRef dialogStatusDotStyleRef {};            ///< dialog: the dot before it
        StyleRef dialogButtonStyleRef {};               ///< dialog: a secondary button (Cancel)
        StyleRef dialogPrimaryButtonStyleRef {};        ///< dialog: the primary button (OK); .dialog-button-primary
        StyleRef dialogDangerButtonStyleRef {};         ///< dialog: a destructive button (Quit, Delete); .dialog-button-danger
        StyleRef messageBoxStyleRef {};                 ///< MessageDialog: the window
        StyleRef messageBoxCloseStyleRef {};            ///< MessageDialog: the close button, top right
        StyleRef messageBoxBodyStyleRef {};             ///< MessageDialog: the icon and text row
        StyleRef messageBoxWarningStyleRef {};          ///< MessageDialog: severity circle, warning / quit
        StyleRef messageBoxQuestionStyleRef {};         ///< MessageDialog: severity circle, question / info
        StyleRef messageBoxErrorStyleRef {};            ///< MessageDialog: severity circle, error
        StyleRef messageBoxTextStyleRef {};             ///< MessageDialog: title, message and extras
        StyleRef messageBoxTitleStyleRef {};            ///< MessageDialog: the title
        StyleRef messageBoxMessageStyleRef {};          ///< MessageDialog: the message
        StyleRef messageBoxWellStyleRef {};             ///< MessageDialog: the affected items
        StyleRef messageBoxItemStyleRef {};             ///< MessageDialog: an affected item's row
        StyleRef messageBoxItemDotStyleRef {};          ///< MessageDialog: an affected item's dot
        StyleRef messageBoxItemNameStyleRef {};         ///< MessageDialog: an affected item's name
        StyleRef messageBoxItemMetaStyleRef {};         ///< MessageDialog: an affected item's extra info
        StyleRef messageBoxDetailsToggleStyleRef {};    ///< MessageDialog: "Details" with its chevron
        StyleRef messageBoxDetailsStyleRef {};          ///< MessageDialog: the details, monospace in a well
        StyleRef checkboxStyleRef {};                   ///< Checkbox: the control
        StyleRef checkboxBoxStyleRef {};                ///< Checkbox: the box
        StyleRef checkboxLabelStyleRef {};              ///< Checkbox: its label
        StyleRef colorPickerBodyStyleRef {};            ///< color picker: planes and controls
        StyleRef colorPickerPlanesStyleRef {};          ///< color picker: square and strips
        StyleRef colorPickerSquareStyleRef {};          ///< color picker: the saturation/value square
        StyleRef colorPickerStripStyleRef {};           ///< color picker: the hue and alpha strips
        StyleRef colorPickerControlsStyleRef {};        ///< color picker: the right column
        StyleRef colorPickerPreviewGroupStyleRef {};    ///< color picker: preview and captions
        StyleRef colorPickerCaptionsStyleRef {};        ///< color picker: New / Original
        StyleRef colorPickerCaptionStyleRef {};         ///< color picker: a small caption
        StyleRef colorPickerModeRowStyleRef {};         ///< color picker: space, Linear, range
        StyleRef colorPickerRangeStyleRef {};           ///< color picker: the 0-255 / 0-1 toggle
        StyleRef colorPickerChannelsStyleRef {};        ///< color picker: the channel rows
        StyleRef colorPickerChannelStyleRef {};         ///< color picker: one channel row
        StyleRef colorPickerChannelLabelStyleRef {};    ///< color picker: R, G, B, A ...
        StyleRef colorPickerTrackStyleRef {};           ///< color picker: a channel's track
        StyleRef colorPickerChannelValueStyleRef {};    ///< color picker: a channel's value
        StyleRef colorPickerFieldRowStyleRef {};        ///< color picker: Hex / HDR rows
        StyleRef colorPickerRowLabelStyleRef {};        ///< color picker: Hex, HDR
        StyleRef colorPickerHdrStyleRef {};             ///< color picker: the intensity box
        StyleRef colorPickerSwatchesStyleRef {};        ///< color picker: swatches and recent
        StyleRef colorPickerSwatchRowStyleRef {};       ///< color picker: one swatch row
        StyleRef colorPickerSectionLabelStyleRef {};    ///< color picker: Swatches, Recent
        StyleRef colorPickerSwatchStyleRef {};          ///< color picker: a swatch
        StyleRef colorPickerRecentStyleRef {};          ///< color picker: a recent color
        StyleRef colorPickerPreviewStyleRef {};         ///< color picker: the color preview
        StyleRef colorPickerNewStyleRef{};              ///< color picker: the add-swatch button
        StyleRef gradientSwatchStyleRef {};             ///< GradientField: the swatch
        StyleRef gradientEditorStyleRef {};             ///< gradient editor: the window
        StyleRef gradientEditorBarStyleRef {};          ///< gradient editor: the gradient bar with its stops
        StyleRef dialogSmallButtonStyleRef {};              ///< dialog: a short button (Save preset, Reverse)
        StyleRef dialogIconButtonStyleRef {};               ///< dialog: a small borderless icon button (previous / next)
        StyleRef dialogSquareButtonStyleRef {};             ///< dialog: a small bordered icon button (duplicate)
        StyleRef dialogDeleteButtonStyleRef {};             ///< dialog: the same, in the error color
        StyleRef gradientEditorPresetsStyleRef {};          ///< gradient editor: the presets row
        StyleRef gradientEditorSectionLabelStyleRef {};     ///< gradient editor: Presets
        StyleRef gradientEditorPresetStyleRef {};           ///< gradient editor: a preset chip
        StyleRef gradientEditorBadgeStyleRef {};            ///< gradient editor: the selected stop's location
        StyleRef gradientEditorColumnsStyleRef {};          ///< gradient editor: the two columns
        StyleRef gradientEditorColumnStyleRef {};           ///< gradient editor: the left column
        StyleRef gradientEditorRowStyleRef {};              ///< gradient editor: a labeled row
        StyleRef gradientEditorHeadingStyleRef {};          ///< gradient editor: Color stop
        StyleRef gradientEditorCountStyleRef {};            ///< gradient editor: 2 / 3
        StyleRef gradientEditorLabelStyleRef {};            ///< gradient editor: a row's label
        StyleRef gradientEditorLocationStyleRef {};         ///< gradient editor: the location box
        StyleRef gradientEditorLocationInputStyleRef {};    ///< gradient editor: its value
        StyleRef gradientEditorLocationUnitStyleRef {};     ///< gradient editor: its %
        StyleRef gradientEditorDividerStyleRef {};          ///< gradient editor: a divider line
        StyleRef gradientEditorActionsStyleRef {};          ///< gradient editor: Reverse, Distribute
        StyleRef gradientEditorTableStyleRef {};            ///< gradient editor: the stops table
        StyleRef gradientEditorTableHeadStyleRef {};        ///< gradient editor: its header row
        StyleRef gradientEditorTableRowStyleRef {};         ///< gradient editor: a stop's row
        StyleRef gradientEditorTypeCellStyleRef {};         ///< gradient editor: Type
        StyleRef gradientEditorSwatchCellStyleRef {};       ///< gradient editor: the stop's color
        StyleRef gradientEditorHeadCellStyleRef {};         ///< gradient editor: a header caption
        StyleRef gradientEditorHeadLocationStyleRef {};     ///< gradient editor: Location
        StyleRef gradientEditorValueCellStyleRef {};        ///< gradient editor: the stop's hex
        StyleRef gradientEditorLocationCellStyleRef {};     ///< gradient editor: the stop's location
        StyleRef tableStyleRef {};           ///< Table: the table
        /// Table bounded to its flex container (flex-basis:0): it doesn't grow, its rows scroll.
        StyleRef tableFillStyleRef {};
        /// Header grid, a sibling of the body so it doesn't scroll with the rows.
        StyleRef tableHeaderSectionStyleRef {};
        StyleRef tableBodiesRowStyleRef {};      ///< row holding the table's body sections, bounded to the table
        StyleRef tableScrollableBodyStyleRef {}; ///< a scrolling section body
        /// SectionSizing::FitContent section body.
        StyleRef tableFitContentBodyStyleRef {};
        StyleRef tableRowStyleRef {};     ///< row backdrop (odd rows)
        StyleRef tableRowAltStyleRef {};  ///< row backdrop (even rows)
        StyleRef tableRowBarStyleRef {};  ///< accent bar on a selected row's left
        StyleRef listRowStyleRef {};      ///< rounded row without stripes (tree views, TableState::rowStyle)
        /// Row-mode tree cell: transparent hover/selected, so the row backdrop shows them.
        StyleRef treeTableCellRowModeStyleRef {};
        /// The arrow's NoInput glyph, inside a full-row-height hit wrapper
        /// (treeTableArrowStyleRef) for forgiving clicks.
        StyleRef treeTableArrowGlyphStyleRef {};
        /// Inline-rename editor. Same box as treeTableTextStyleRef (both have a 1px border),
        /// so the text doesn't move when a rename starts.
        StyleRef treeTableTextEditStyleRef {};
        StyleRef tableHeaderStyleRef {};    ///< a column header
        /// Locked column headers: default cursor, since they can't be dragged.
        StyleRef tableHeaderLockedStyleRef {};
        StyleRef tableSizerStyleRef {};     ///< a column's resize handle
        StyleRef tableSeparatorStyleRef {}; ///< the divider between table sections
        StyleRef tableCellStyleRef {};      ///< a cell
        StyleRef tableCellNumberStyleRef {};  ///< right-aligned number cell (TableColumn::cellStyle)
        StyleRef tableCellCompactStyleRef {}; ///< tighter-padded cell for dense tables (opt-in via VirtualTable cellStyle)
        /// @name Tree table cells
        /// Tree column: [indent][arrow|spacer][icon?][text], guide lines custom-drawn.
        ///@{
        StyleRef treeTableCellStyleRef {};
        StyleRef treeTableIndentStyleRef {};
        StyleRef treeTableArrowStyleRef {};
        StyleRef treeTableIconStyleRef {};
        StyleRef treeTableTextStyleRef {};
        ///@}
        StyleRef nodeViewStyleRef {};       ///< NodeView: the canvas
        StyleRef viewBackgroundStyleRef {}; ///< solid backdrop (--bg-app) for the View panel's toolbar dock
        StyleRef nodeStyleRef {};       ///< single node container (vertical column with border + bg)
        StyleRef nodeRerouteStyleRef {};   ///< a reroute node's square (the dot is drawn on it)
        StyleRef nodeFrameStyleRef {};       ///< a frame's box (drawn behind the wires)
        StyleRef nodeFrameBandStyleRef {};   ///< its title bar's band, over the top of the box
        StyleRef nodeFrameTitleStyleRef {};  ///< the title on it
        StyleRef nodeFrameRenameStyleRef {}; ///< the field over the title bar while renaming
        StyleRef nodeFramePortsStyleRef {};  ///< a collapsed frame's row, a wire in and one out
        StyleRef nodeFramePortStyleRef {};   ///< the name of the input on it
        StyleRef nodeFramePortOutStyleRef {};///< the name of the output on it
        StyleRef nodeHeaderStyleRef {}; ///< node title bar (distinct background, sits at top of node)
        StyleRef nodeCategoryStyleRef {}; ///< the category square in a node header
        StyleRef nodeTitleStyleRef {};    ///< a node header's title
        StyleRef nodeCaptionStyleRef {};  ///< a node header's category name, right-aligned
        StyleRef nodeViewOverlayStyleRef {};       ///< panel floating over the node view (breadcrumb, zoom bar)
        StyleRef nodeViewOverlayButtonStyleRef {}; ///< flat button in such a panel
        StyleRef nodeViewMinimapStyleRef {};       ///< the node view's minimap
        StyleRef breadcrumbStyleRef {};            ///< the breadcrumb path bar
        StyleRef breadcrumbSegmentStyleRef {};     ///< a clickable path segment
        StyleRef breadcrumbCurrentStyleRef {};     ///< the last segment
        StyleRef breadcrumbSeparatorStyleRef {};   ///< between two segments
        StyleRef axisLabelStyleRefs[4] {}; ///< vector fields: the label of each component (X, Y, Z, W)
        /// Width shared by the component labels: the widest name (X..W, R..A) with its label's
        /// padding and border, in pixels. Measured in ComputeRowMetrics.
        int32_t  axisLabelWidth = 0;
        StyleRef vectorComponentStyleRef {}; ///< a vector component's field
        StyleRef statusBarStyleRef {};       ///< BeginStatusBar's default
        StyleRef statusTextStyleRef {};      ///< text in the status bar
        /// @name Command search
        /// Command search: the box, its hint and keycap, the list of matches and its highlighted row.
        ///@{
        StyleRef commandSearchStyleRef {};
        StyleRef commandSearchHintStyleRef {};
        StyleRef commandSearchKeyStyleRef {};
        StyleRef commandSearchListStyleRef {};
        StyleRef commandSearchCurrentRowStyleRef {};
        ///@}
        StyleRef nodeRowStyleRef {};    ///< node property row (socket | body | socket)
        StyleRef nodeBodyCenterStyleRef {}; ///< centered body for full-body content (image previews)
        /// A socket's hit target; overflow:visible so the dot and its hover halo aren't clipped.
        StyleRef nodeSocketStyleRef {};
        /// .node-socket box size in pixels (read from CSS); the custom-drawn dot and the
        /// on-border insets derive from it.
        int      socketSize = 12;
        StyleRef nodeTextLeftStyleRef {};   ///< text-only property body, left-aligned (left dot only)
        StyleRef nodeTextCenterStyleRef {}; ///< text-only property body, centered (both dots)
        StyleRef nodeTextRightStyleRef {};  ///< text-only property body, right-aligned (right dot only)
        StyleRef itemListStyleRef {};       ///< a bordered scrolling list box
        StyleRef itemStyleRef {};           ///< an item in it
        StyleRef mainWindowStyleRef{};  ///< a window without padding, its content in a column
        StyleRef windowStyleRef {};     ///< BeginWindow's default
        StyleRef dockWindowStyleRef {}; ///< floating dock window - no internal padding
        StyleRef dockWindowLeafStyleRef {};     ///< a torn-off window's root leaf: the title bar over the panel
        StyleRef dockWindowPanelStyleRef {};    ///< the panel inset under a torn-off window's title bar
        StyleRef dockWindowBodyStyleRef {};     ///< a torn-off window's split layout, inset the same
        StyleRef titleBarTabBarStyleRef {};     ///< the tab strip in a torn-off window's title bar
        StyleRef titleBarTabStyleRef {};        ///< a tab in it
        StyleRef titleBarGripStyleRef {};       ///< the grip before it
        StyleRef titleBarTitleStyleRef {};      ///< the window's name when the tabs aren't in the bar
        StyleRef titleBarTextButtonStyleRef {}; ///< a text button in a title bar (Dock)
        StyleRef listAreaStyleRef {};       ///< undecorated scrolling column (BeginArea)
        StyleRef collectionAreaStyleRef {}; ///< card-like well behind the list & dictionary Field() helpers
        StyleRef typedFieldStyleRef {};                 ///< TypedField: the box
        StyleRef typedFieldInputStyleRef {};            ///< TypedField: the value
        StyleRef typedFieldTypeStyleRef {};             ///< TypedField: the type tag
        StyleRef collectionListStyleRef {};             ///< list field: the rows
        StyleRef collectionRowStyleRef {};              ///< list field: a row
        StyleRef collectionIndexStyleRef {};            ///< list field: a row's index
        StyleRef collectionRemoveStyleRef {};           ///< list / dictionary: remove
        StyleRef collectionAddStyleRef {};              ///< list field: Add item
        StyleRef collectionAddEntryStyleRef {};         ///< dictionary: Add entry
        StyleRef collectionEntryStyleRef {};            ///< dictionary: a row
        StyleRef collectionValueStyleRef {};            ///< dictionary: a value
        StyleRef collectionHeaderStyleRef {};           ///< dictionary: the header row
        StyleRef collectionHeaderCellStyleRef {};       ///< dictionary: Key / Value
        StyleRef collectionHeaderCaptionStyleRef {};    ///< dictionary: the caption
        StyleRef collectionHeaderTypeStyleRef {};       ///< dictionary: the type
        StyleRef groupLabelTopStyleRef {};              ///< Label with a caption: the container
        StyleRef groupLabelTitleStyleRef {};            ///< Label with a caption: the title
        StyleRef groupLabelCaptionStyleRef {};          ///< Label with a caption: the caption
        StyleRef iconButtonStyleRef {}; ///< compact flat icon button (a list / dictionary row's remove)
        StyleRef iconButtonEndStyleRef {}; ///< the same, end-aligned (the trailing add button)
        StyleRef menuStyleRef {};               ///< a menu's window
        StyleRef menuItemStyleRef{};            ///< a menu item
        StyleRef menuScrollButtonStyleRef{};    ///< the scroll up / down arrows of a menu taller than the screen
        StyleRef menuBarStyleRef {};        ///< horizontal strip at the top of the main window
        StyleRef menuBarTriggerStyleRef {}; ///< flat top-level trigger inside the menu bar
        StyleRef menuBarDropdownStyleRef {}; ///< a dropdown in the menu bar or title bar (BeginDropdownMenu)
        StyleRef titleBarStyleRef {};            ///< a window's own title bar (BeginTitleBar)
        StyleRef titleBarButtonStyleRef {};      ///< its minimize and maximize buttons
        StyleRef titleBarCloseButtonStyleRef {}; ///< its close button
        StyleRef menuSeparatorStyleRef {};       ///< a divider between menu items
        StyleRef menuHeaderStyleRef {};     ///< a section's name in a menu
        StyleRef reorderDropSlotStyleRef {};   ///< a list row's drop slot while it's dragged
        StyleRef reorderLiftedStyleRef {};     ///< the dragged row itself, under its parts
        StyleRef reorderLiftedRingStyleRef {}; ///< the dragged row's ring, over its parts
        StyleRef menuItemDestructiveLabelStyleRef {};   ///< a destructive item's label (.menu-item-label-destructive)
        StyleRef menuItemDestructiveIconStyleRef {};    ///< a destructive item's icon (.menu-item-icon-destructive)
        StyleRef menuItemRowStyleRef {};      ///< interactive row: icon slot + label + trailing shortcut/arrow
        StyleRef menuItemIconStyleRef {};     ///< fixed icon slot inside a menu item row
        StyleRef menuItemLabelStyleRef {};    ///< grow:1 label inside a menu item row
        StyleRef menuItemShortcutStyleRef {}; ///< dim trailing keyboard-shortcut text
        StyleRef menuItemArrowStyleRef {};    ///< dim trailing submenu arrow
        StyleRef tooltipFrameStyleRef {};     ///< a tooltip's window: see-through, with room for the arrow
        StyleRef tooltipStyleRef {};          ///< the tooltip's box
        StyleRef tooltipTextStyleRef {};      ///< its text
        StyleRef tooltipShortcutStyleRef {};  ///< a shortcut chip after the text
        StyleRef tooltipRichStyleRef {};      ///< property help's box, on the popup surface
        StyleRef tooltipTitleStyleRef {};     ///< its title
        StyleRef tooltipDescriptionStyleRef {}; ///< its description
        StyleRef tooltipFooterStyleRef {};    ///< its footer, under a line
        StyleRef tooltipExpandStyleRef {};    ///< text cut short, shown whole over itself: the box
        StyleRef tooltipExpandTextStyleRef {}; ///< its text's colors (the font is the cut text's)
        StyleRef tooltipErrorStyleRef {};     ///< a value that isn't accepted: the box, error border
        StyleRef tooltipErrorTextStyleRef {}; ///< its message
        StyleRef tooltipLinesStyleRef {};     ///< a tooltip of a few lines: a disabled control's, a pin's
        StyleRef tooltipNoteStyleRef {};      ///< a muted line: why it's disabled, where a pin's value comes from
        StyleRef tooltipPinRowStyleRef {};    ///< a pin's first line: its name, type and value
        StyleRef tooltipPinTypeStyleRef {};   ///< ... its type, muted and monospace
        StyleRef tooltipPinValueStyleRef {};  ///< ... its value, monospace, at the end
        StyleRef labelHelpStyleRef {};        ///< a Label() with property help: underlined, dotted
        StyleRef groupLabelHelpStyleRef {};   ///< the same inside a label group
        StyleRef dragLabelHelpStyleRef {};    ///< a DragLabel() with property help
        StyleRef groupDragLabelHelpStyleRef {}; ///< the same inside a label group
        StyleRef groupLabelStyleRef {};       ///< Label() inside a label group
        StyleRef groupDragLabelStyleRef {};   ///< DragLabel() inside a label group
        StyleRef collectionStyleRef {};       ///< BeginArea / BeginCached default
        /// @name Reorderable groups
        /// BeginReorderableItem's default and the reorderable group per direction, with and
        /// without wrapping. CSS classes in camelCase: .reorderableItem, .horzReorderableGroup ...
        ///@{
        StyleRef reorderableItemStyleRef{};
        StyleRef horzReorderableGroupStyleRef{};
        StyleRef vertReorderableGroupStyleRef{};
        StyleRef horzReorderableGroupWrapStyleRef{};
        StyleRef vertReorderableGroupWrapStyleRef{};
        ///@}
        StyleRef listViewStyleRef {};         ///< BeginListView's default
        StyleRef listItemStyleRef {};         ///< ListItem's default
        StyleRef listItemRowStyleRef {};      ///< reorderable list item outer container
        StyleRef listItemDragKnobHorzStyleRef {}; ///< reorderable list item drag handle (horizontal list - drag along x)
        StyleRef listItemDragKnobVertStyleRef {}; ///< reorderable list item drag handle (vertical list - drag along y)
        StyleRef listItemBodyStyleRef {};     ///< reorderable list item text/icon body
        StyleRef treeViewStyleRef {};         ///< a tree view
        StyleRef gridViewStyleRef {};         ///< BeginGridView's default
        StyleRef gridViewItemStyleRef {};     ///< a grid view cell, by default
        StyleRef gridViewScrollStyleRef {};   ///< virtualized grid scroll viewport (column)
        StyleRef gridViewRowStyleRef {};      ///< one row strip of cells in the virtual grid
        StyleRef gridViewListItemStyleRef {}; ///< list-mode cell: icon left, name right, one line
        StyleRef gridViewRenameItemStyleRef {};     ///< rename-mode cell: icon above inline editor
        StyleRef gridViewRenameListItemStyleRef {}; ///< rename-mode list row: icon left of editor
        StyleRef gridViewRenameIconStyleRef {};     ///< the icon child of a renaming cell
        StyleRef gridViewRenameEditStyleRef {};     ///< grid-mode inline editor (full cell width)
        StyleRef flexFillRowStyleRef {};      ///< horizontal flex bounded to its parent (flex-basis:0)
        StyleRef flexFillColStyleRef {};      ///< vertical flex bounded to its parent (flex-basis:0)
        StyleRef flatButtonStyleRef {};       ///< button that reads as plain left-aligned text (no chrome)
        StyleRef searchRowStyleRef {};         ///< full-width row hosting a top-of-panel SearchField (bottom spacing)
        /// @name Search field
        /// SearchField: the textbox chrome container, its icon, and the borderless inner field.
        ///@{
        StyleRef searchTextboxStyleRef {};
        StyleRef searchTextboxIconStyleRef {};
        StyleRef searchTextboxFieldStyleRef {};
        ///@}
        /// Parsed .grid-view-item, for cloning with another iconSize (registering is deduped).
        Style    gridViewItemBaseStyle {};
        /// Line height of a grid view cell's name in pixels, measured in ComputeRowMetrics so
        /// names never clip.
        int32_t  gridViewItemLineHeight = 0;
        int32_t  gridViewListRowHeight  = 0; ///< a list-mode grid view row, in pixels (ComputeRowMetrics)
        /// @name Tabs
        /// A tab bar, a tab, a tab's icon and text, and its close button.
        ///@{
        StyleRef tabBarStyleRef {};
        StyleRef tabStyleRef {};
        StyleRef tabOverflowStyleRef {};   ///< the "+N" button after the tabs that fit
        StyleRef tabContentStyleRef {};
        StyleRef tabCloseStyleRef {};
        ///@}

        /// @name Progress and loading
        ///@{
        StyleRef progressBarStyleRef {};         ///< ProgressBar's track, 6px high
        StyleRef progressBarSmallStyleRef {};    ///< the 4px track, for lists and the status bar
        StyleRef progressFillStyleRef {};        ///< the part done
        StyleRef progressFillFailedStyleRef {};  ///< the part done of a failed one, in the error color
        StyleRef progressSegmentStyleRef {};     ///< the segment sliding along a bar of unknown length
        StyleRef circularProgressStyleRef {};    ///< CircularProgress: its size and its percentage's text
        StyleRef skeletonStyleRef {};            ///< Skeleton: a placeholder bar
        StyleRef skeletonShineStyleRef {};       ///< the shine sweeping across it
        tachyon::VisualColorStyleIndex progressTrackColor {};   ///< --line-subtle: a spinner's ring
        tachyon::VisualColorStyleIndex progressArcColor {};     ///< --accent: a spinner's arc
        StyleRef progressFillPausedStyleRef {};  ///< the part done of a paused one, in the warning color
        StyleRef taskProgressStyleRef {};        ///< TaskProgress: name row, bar, detail
        StyleRef taskProgressHeaderStyleRef {};  ///< its name row
        StyleRef taskProgressNameStyleRef {};    ///< the name
        StyleRef taskProgressFailedStyleRef {};  ///< the name with the error icon
        StyleRef taskProgressDoneStyleRef {};    ///< the name with the check
        StyleRef taskProgressPercentStyleRef {}; ///< the percent, monospace
        StyleRef taskProgressNoteStyleRef {};    ///< a note such as the time taken, monospace
        StyleRef taskProgressCancelStyleRef {};  ///< the cancel button
        StyleRef taskProgressRetryStyleRef {};   ///< the Retry button
        StyleRef taskProgressDetailStyleRef {};  ///< the line under the bar
        StyleRef taskProgressErrorStyleRef {};   ///< the line under the bar once failed: why
        StyleRef progressStepsStyleRef {};       ///< ProgressSteps: the rows
        StyleRef progressStepStyleRef {};        ///< a step's row
        StyleRef progressStepDoneStyleRef {};    ///< a done step's glyph: a check on an --ok disc
        StyleRef progressStepFailedStyleRef {};  ///< a failed step's: a cross on an --error disc
        StyleRef progressStepWaitingStyleRef {}; ///< a pending step's: a dashed ring
        StyleRef progressStepPausedStyleRef {};  ///< a paused step's: a dashed ring in --warn
        StyleRef progressStepBodyStyleRef {};    ///< its name and bar
        StyleRef progressStepNameStyleRef {};    ///< the name of a step started or done
        StyleRef progressStepPendingStyleRef {}; ///< the name of a step to come
        StyleRef statusProgressStyleRef {};      ///< StatusProgress: spinner, name, bar, count
        StyleRef statusTextMonoStyleRef {};      ///< a status bar's count, monospace
        StyleRef tasksPillStyleRef {};           ///< BackgroundTasks' "3 tasks" pill
        StyleRef tasksPopoverStyleRef {};        ///< its popover
        StyleRef tasksPopoverHeaderStyleRef {};  ///< "Background tasks" and the count running
        StyleRef tasksPopoverTitleStyleRef {};   ///< "Background tasks"
        StyleRef tasksPopoverCountStyleRef {};   ///< the count running
        StyleRef tasksRowStyleRef {};            ///< a task's row
        StyleRef tasksRowBodyStyleRef {};        ///< its name and bar
        StyleRef tasksRowStateStyleRef {};       ///< its percent or state, monospace
        StyleRef timelineStyleRef {};            ///< Timeline: the row
        StyleRef timelineTransportStyleRef {};   ///< its buttons
        StyleRef timelineButtonStyleRef {};      ///< a transport button: start, step, end
        StyleRef timelinePlayStyleRef {};        ///< play / pause, in the accent
        StyleRef timelineLoopStyleRef {};        ///< the loop toggle; selected while looping
        StyleRef timelineTimeStyleRef {};        ///< the time's well
        StyleRef timelineTimeCurrentStyleRef {}; ///< the time now, monospace
        StyleRef timelineTimeTotalStyleRef {};   ///< "/ " and the length
        StyleRef timelineFrameStyleRef {};       ///< the frame chip
        StyleRef timelineScrubberStyleRef {};    ///< the scrubber
        StyleRef timelineReadyStyleRef {};       ///< drawn: the progress fill
        StyleRef timelineTickStyleRef {};        ///< drawn: a tick every quarter second (or the like)
        StyleRef timelineTickMajorStyleRef {};   ///< drawn: a tick every second (or the like)
        StyleRef timelineMarkerStyleRef {};      ///< drawn: a marker's line
        StyleRef timelinePlayheadStyleRef {};    ///< drawn: the playhead's line and its head
        StyleRef timelineEditorStyleRef {};         ///< TimelineEditor: the whole
        StyleRef timelineToolbarStyleRef {};        ///< its toolbar: transport, time, fps, snap, zoom
        StyleRef timelineDividerStyleRef {};        ///< a line between the toolbar's groups
        StyleRef timelineFpsStyleRef {};            ///< the fps dropdown
        StyleRef timelineSnapStyleRef {};           ///< the Snap toggle; selected while snapping
        StyleRef timelineZoomStyleRef {};           ///< the zoom slider and its icons
        StyleRef timelineHeaderStyleRef {};         ///< the row over the track list and the ruler
        StyleRef timelineListHeaderStyleRef {};     ///< "Tracks", the filter and the add button
        StyleRef timelineListTitleStyleRef {};      ///< "Tracks"
        StyleRef timelineFilterStyleRef {};         ///< the filter field
        StyleRef timelineAddTrackStyleRef {};       ///< the add-track button
        StyleRef timelineBodyStyleRef {};           ///< the track list and lanes, scrolled together
        StyleRef timelineListStyleRef {};           ///< the track list
        StyleRef timelineRowStyleRef {};            ///< a track's row; selected while it's the selected track
        StyleRef timelineChevronStyleRef {};        ///< a group's expand / collapse
        StyleRef timelineChipStyleRef {};           ///< a group's color chip
        StyleRef timelineGroupNameStyleRef {};      ///< a group's name
        StyleRef timelineTrackNameStyleRef {};      ///< another track's name, with its icon
        StyleRef timelineRowButtonStyleRef {};      ///< eye, lock and add-key buttons
        StyleRef timelineMuteStyleRef {};           ///< an audio track's M; selected while muted
        StyleRef timelineSoloStyleRef {};           ///< an audio track's S; selected while solo
        StyleRef timelineRulerStyleRef {};          ///< the ruler over the lanes
        StyleRef timelineRulerLabelStyleRef {};     ///< drawn: a second's label on the ruler
        StyleRef timelinePlayheadLabelStyleRef {};  ///< drawn: the playhead's time on the ruler
        StyleRef timelineLanesStyleRef {};          ///< the lanes
        StyleRef timelineLaneSelectedStyleRef {};   ///< drawn: the selected track's lane
        StyleRef timelineRowLineStyleRef {};        ///< drawn: the line under a row
        StyleRef timelineOutsideStyleRef {};        ///< drawn: the lanes outside the loop range
        StyleRef timelineLoopRangeStyleRef {};      ///< drawn: the loop range on the ruler
        StyleRef timelineLoopEdgeStyleRef {};       ///< drawn: its ends
        StyleRef timelineClipStyleRef {};           ///< drawn: an audio clip
        StyleRef timelineEventStyleRef {};          ///< drawn: an event's line
        StyleRef timelineEventChipStyleRef {};      ///< drawn: the chip under an event's name
        StyleRef timelineEventLabelStyleRef {};     ///< drawn: an event's name
        StyleRef timelineBoxSelectStyleRef {};      ///< drawn: the box being dragged to select keys
        StyleRef timelineScrollStyleRef {};         ///< the scroll bar under the lanes
        StyleRef timelineScrollThumbStyleRef {};    ///< drawn: its thumb
        static constexpr int kSpinnerFrames = 24;
        /// A 90-degree arc, one frame per 15 degrees round: a spinner that goes where an icon
        /// goes, in the color of the icon it stands in for (BusyButton).
        IconID spinnerIcons[kSpinnerFrames] {};
        /// The spinner frame at `timeSecs`: a turn every 0.8 s.
        IconID SpinnerIcon(float timeSecs) const;
        ///@}

        /// @name Toasts and banners
        /// The circle, banner and banner message come per severity, indexed by Severity.
        ///@{
        StyleRef toastStackStyleRef {};       ///< the toasts, a column in the window's corner
        StyleRef toastStyleRef {};            ///< one toast, on the popup surface
        StyleRef toastCircleStyleRefs[4] {};  ///< .toast-circle-info, -success, -warning, -error
        StyleRef toastContentStyleRef {};     ///< title row, body, progress and actions
        StyleRef toastHeaderStyleRef {};      ///< the title and its time
        StyleRef toastTitleStyleRef {};
        StyleRef toastTimeStyleRef {};        ///< "now", "4 min"
        StyleRef toastBodyStyleRef {};
        StyleRef toastBodyMonoStyleRef {};    ///< a body of code or file locations
        StyleRef toastProgressStyleRef {};    ///< the progress bar's track
        StyleRef toastProgressFillStyleRef {};
        StyleRef toastActionsStyleRef {};     ///< the row of actions
        StyleRef toastActionStyleRef {};      ///< the first action, in the link color
        StyleRef toastActionSecondaryStyleRef {};
        StyleRef toastDismissStyleRef {};
        StyleRef toastEarlierStyleRef {};     ///< "+N earlier"
        StyleRef bannerStyleRefs[4] {};       ///< .banner-info, -success, -warning, -error
        StyleRef bannerMessageStyleRefs[4] {}; ///< .banner-message-info, ...: the icon in the severity's color
        StyleRef bannerPrimaryStyleRef {};    ///< BannerActionKind::Primary
        StyleRef bannerSecondaryStyleRef {};
        StyleRef bannerLinkStyleRef {};
        ///@}

        /// @name Documents
        /// The document bar and the workspace under it. A document's tab, name and line take
        /// their colors from its type's classes (.document-tab-level, ...); these are the plain
        /// ones, for a type the theme has none for.
        ///@{
        StyleRef documentsStyleRef {};          ///< the bar and the active document's panels, a column
        StyleRef documentBarStyleRef {};        ///< the bar: its row over the line
        StyleRef documentRowStyleRef {};        ///< the tabs, "+" and the list
        StyleRef documentTabBarStyleRef {};     ///< the tabs and "+"
        StyleRef documentTabStyleRef {};
        StyleRef documentNameStyleRef {};       ///< a tab's icon and name
        StyleRef documentExtensionStyleRef {};  ///< ".level", after the name
        StyleRef documentMarkStyleRef {};       ///< the slot for the close button or the unsaved dot
        StyleRef documentCloseStyleRef {};
        StyleRef documentDirtyStyleRef {};      ///< the unsaved-changes dot
        StyleRef documentLineStyleRef {};       ///< under the bar, in the active document's color
        StyleRef documentNewStyleRef {};        ///< "+"
        StyleRef documentListStyleRef {};       ///< "N open", listing every document
        StyleRef titleBarDocumentsStyleRef {};  ///< a document bar in a title bar (DocumentBar)
        ///@}

        StyleRef dockContainerStyleRef {};       ///< a docking container
        StyleRef dockContainerMainStyleRef {};   ///< a main window's docking area, with a side gutter
        StyleRef dockSplitHorzStyleRef {};   ///< horizontal flex row (split into left / right)
        StyleRef dockSplitVertStyleRef {};   ///< vertical flex column (split into top / bottom)
        StyleRef dockSizerHorzStyleRef {};   ///< vertical bar between left / right panes
        StyleRef dockSizerVertStyleRef {};   ///< horizontal bar between top / bottom panes
        StyleRef dockLeafStyleRef {};        ///< vertical flex: [tab bar][content]
        StyleRef dockContentStyleRef {};     ///< content area holding the selected panel's draw()

        StyleRef settingsPageSelectionRef {};   ///< the settings page list (.settings-page-selection)
        StyleRef settingsPageStyleRef     {};   ///< settings page body - dock-content layout + left padding off the sizer

        /// @name Built-in icons
        /// Registered by InitializeDefaultStyles.
        ///@{

        IconID treeArrowRight    = IconID::Invalid; ///< tree expander: collapsed
        IconID treeArrowDown     = IconID::Invalid; ///< tree expander: expanded

        IconID sortArrowUpIcon = IconID::Invalid;   ///< table header: sorted descending
        IconID sortArrowDownIcon = IconID::Invalid; ///< table header: sorted ascending

        IconID tabCloseIcon = IconID::Invalid; ///< a tab's close button
        IconID menuCheckIcon = IconID::Invalid; ///< marks the current/checked item in a menu
        IconID boldCheckIcon = IconID::Invalid; ///< a heavier check, small on a filled disc: a done step
        IconID boldCrossIcon = IconID::Invalid; ///< a heavier cross, small on a filled disc: a failed step
        IconID dropdownArrowIcon = IconID::Invalid; ///< after a dropdown's text (menu triggers outside a menu bar)
        IconID dropdownArrowUpIcon = IconID::Invalid; ///< the same while its menu is open
        IconID chevronLeftIcon   = IconID::Invalid; ///< previous
        IconID duplicateIcon     = IconID::Invalid; ///< duplicate
        IconID deleteIcon        = IconID::Invalid; ///< delete (trash can)
        IconID reverseIcon       = IconID::Invalid; ///< gradient editor: Reverse
        IconID distributeIcon    = IconID::Invalid; ///< gradient editor: Distribute evenly

        IconID addIcon    = IconID::Invalid; ///< list / dictionary Field(): add a row
        IconID removeIcon = IconID::Invalid; ///< list / dictionary Field(): remove a row

        IconID dragHandleIcon = IconID::Invalid;  ///< three-bar grip of a reorderable list / tree row
        IconID dockIcon = IconID::Invalid;        ///< returns a torn-off panel to its dock
        IconID eyedropperIcon = IconID::Invalid;  ///< picks a color from the screen
        IconID alertIcon      = IconID::Invalid;  ///< message severity: warning
        IconID powerIcon      = IconID::Invalid;  ///< message severity: quitting
        IconID questionIcon   = IconID::Invalid;  ///< message severity: question
        IconID infoIcon       = IconID::Invalid;  ///< message severity: information
        IconID errorIcon      = IconID::Invalid;  ///< message severity: error
        IconID invalidIcon    = IconID::Invalid;  ///< in a field whose value isn't accepted
        IconID chevronRightIcon = IconID::Invalid; ///< next; a collapsed section
        IconID searchIcon     = IconID::Invalid;  ///< SearchField
        IconID windowMinimizeIcon = IconID::Invalid; ///< title bar: minimize
        IconID windowMaximizeIcon = IconID::Invalid; ///< title bar: maximize
        IconID windowRestoreIcon  = IconID::Invalid; ///< title bar: restore
        IconID timelineStartIcon    = IconID::Invalid; ///< Timeline: go to the start
        IconID timelineStepBackIcon = IconID::Invalid; ///< Timeline: a frame back
        IconID timelinePlayIcon     = IconID::Invalid; ///< Timeline: play (filled)
        IconID timelinePauseIcon    = IconID::Invalid; ///< Timeline: pause (filled)
        IconID timelineStepIcon     = IconID::Invalid; ///< Timeline: a frame on
        IconID timelineEndIcon      = IconID::Invalid; ///< Timeline: go to the end
        IconID timelineLoopIcon     = IconID::Invalid; ///< Timeline: loop
        IconID timelinePrevKeyIcon  = IconID::Invalid; ///< TimelineEditor: the previous key
        IconID timelineNextKeyIcon  = IconID::Invalid; ///< TimelineEditor: the next key
        IconID timelineSnapIcon     = IconID::Invalid; ///< TimelineEditor: snap (a magnet)
        IconID timelineZoomInIcon   = IconID::Invalid; ///< TimelineEditor: zoom in
        IconID timelineZoomOutIcon  = IconID::Invalid; ///< TimelineEditor: zoom out
        IconID timelineEyeIcon      = IconID::Invalid; ///< TimelineEditor: a group shown
        IconID timelineEyeOffIcon   = IconID::Invalid; ///< TimelineEditor: a group hidden
        IconID timelineLockIcon     = IconID::Invalid; ///< TimelineEditor: a group locked
        IconID timelineUnlockIcon   = IconID::Invalid; ///< TimelineEditor: a group unlocked
        IconID timelineKeyIcon      = IconID::Invalid; ///< TimelineEditor: add a key (a diamond)
        IconID timelineAudioIcon    = IconID::Invalid; ///< TimelineEditor: an audio track
        IconID timelineEventsIcon   = IconID::Invalid; ///< TimelineEditor: an events track (a flag)
        ///@}

        // Line colors, from the palette's custom properties.
        tachyon::VisualColorStyleIndex lineGrayStyle;          ///< --text-3
        tachyon::VisualColorStyleIndex lineYellowStyle;        ///< --warn
        tachyon::VisualColorStyleIndex lineWhiteStyle;         ///< --text-1
        tachyon::VisualColorStyleIndex lineDarkStyle;          ///< --bg-app
        tachyon::VisualColorStyleIndex treeGuideLineStyle;     ///< --line-node: tree guide lines
        tachyon::VisualColorStyleIndex dividerHoverLineStyle; ///< a hovered divider drawn as a line (label sizer)
        tachyon::VisualColorStyleIndex nanTextColor;          ///< a number field's NaN
        /// TimelineEditor's keys: a key, its edge, a selected key and its edge, a group's
        /// summary key, and an audio clip's waveform.
        tachyon::VisualColorStyleIndex timelineKeyColor, timelineKeyEdgeColor, timelineKeySelectedColor,
                                       timelineKeySelectedEdgeColor, timelineSummaryKeyColor, timelineWaveColor;
        tachyon::color4b               scrim {};              ///< dims a window under its modal dialog (fading in and out)
        /// --disabled-opacity: a disabled control, with all its parts, is drawn this opaque,
        /// unless its style sets an opacity for :disabled.
        float                          disabledOpacity = 0.4f;
        /// An invalid control's border color (--error) and the halo around it (--invalid-halo).
        tachyon::color4b               invalidColor {};
        tachyon::VisualShadow          invalidHalo {};
        /// The .link class: its color, :hover color and cursor, for a ContentRun's linkStyle.
        /// The LinkStyle defaults when the theme has no such class.
        tachyon::LinkStyle             linkStyle {};
        tachyon::VisualColorStyleIndex checkerAColor, checkerBColor;     ///< transparency checkerboard
        tachyon::VisualColorStyleIndex alphaTrackColor, alphaFillColor;  ///< a color's alpha bar
        tachyon::VisualColorStyleIndex accentColor, panelColor;          ///< a selected swatch's ring and gap
        tachyon::VisualColorStyleIndex treeDropIndicatorStyle; ///< drop indicator lines + closed-branch outline

        tachyon::VisualColorStyleIndex marqueeFillColorStyle; ///< selection-marquee fill (fixed translucent blue)
        tachyon::VisualColorStyleIndex imageFillColorStyle;   ///< a texture stretched over a quad (RenderQuad with an image)
        tachyon::VisualColorStyleIndex clearColorStyle;       ///< fully transparent
        tachyon::VisualColorStyleIndex tooltipArrowFill;      ///< --tooltip-bg: a tooltip's arrow
        tachyon::VisualColorStyleIndex tooltipArrowLine;      ///< --tooltip-border: its edges
        tachyon::VisualColorStyleIndex popupArrowFill;        ///< --bg-popup: property help's arrow
        tachyon::VisualColorStyleIndex popupArrowLine;        ///< --line-strong: its edges
        tachyon::VisualColorStyleIndex invalidLine;           ///< --error: an error tooltip's arrow edges, a field's error icon

        /// Table row height in pixels, measured from real font metrics in ComputeRowMetrics.
        /// Must match the rendered rows exactly, or the max scroll oscillates.
        int32_t tableRowHeight = 0;

        /// @name Tree table metrics
        /// Pixels, read from the stylesheet in ComputeRowMetrics; a theme that omits one gets
        /// the measured text line height.
        ///@{
        int32_t treeTableIndent        = 0; ///< per indent level
        int32_t treeTableExpanderWidth = 0; ///< the expander column (arrow or leaf spacer)
        int32_t treeTableIconSize      = 0; ///< a node's icon
        int32_t treeTableArrowGlyphSize = 0; ///< the expander arrow
        ///@}

        /// Node-view grid pitch: half a property row, measured in ComputeRowMetrics.
        int32_t nodeGridSize = 0;

    public:
        /// Registers the styles built in code (fieldElementGroupStyleRef, the empty*StyleRefs)
        /// and the built-in icons; textSystem must be valid. Call before LoadTheme, and again
        /// after StyleManager::ClearAll.
        void InitializeDefaultStyles(StyleManager& styleManager, ITextSystem* textSystem);
        /// Parses the palette file `themePath` followed by `styleFiles` (in order; relative
        /// paths are next to the palette file) and registers every class, the palette colors
        /// and, when `textSystem` is given, the `@font-face` families. `fontScale` scales every
        /// font size. `accent`, when given, replaces the palette's accent and the colors derived
        /// from it (its dark or light color, as the palette is). Parse errors are logged and
        /// skipped; false when a file can't be opened.
        bool LoadTheme(StyleManager& styleManager, ITextSystem* textSystem, const std::string& themePath,
                       std::span<const std::string> styleFiles, float fontScale,
                       std::optional<AccentColors> accent = std::nullopt);

        /// Whether the loaded palette is a light one: its --bg-panel is closer to white than
        /// to black.
        bool IsLight() const { return m_isLight; }

        /// The style of a CSS class from the loaded style files (library or app); an invalid
        /// style, with a warning once per class, when no file defines it.
        StyleRef         GetStyle(std::string_view cssClass) const;
        /// As GetStyle, but a class no file defines is a NotFound error.
        Result<StyleRef> FindStyle(std::string_view cssClass) const;

        /// A palette color by custom-property name ("--node-input"), or `fallback`
        /// when the loaded theme doesn't define it.
        color4f GetColor(std::string_view varName, color4f fallback = {}) const;

        /// Measures the font-dependent metrics above. Call after every LoadTheme.
        void ComputeRowMetrics(StyleManager& styleManager, ITextSystem& textSystem);
    private:
        std::unordered_map<std::string, color4f> m_paletteColors;
        bool m_isLight = false;
        std::map<std::string, StyleRef, std::less<>> m_classStyles;  ///< every class in the style files
        mutable std::set<std::string, std::less<>>   m_missingWarned;
    };

    /// @}
} // namespace tachyon
