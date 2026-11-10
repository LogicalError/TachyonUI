#pragma once

#include <gui_controls/gui_application.h>
#include <gui_controls/controls_toolbar_dock.h>
#include <gui_core/input/gui_pointer_drag.h>

#include "scene_renderer.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace tachyon
{
    enum class CommandMode : uint32_t;
    struct FileEntry;
    // Each panel's own data (panel_*.cpp): two panels of a kind don't share it.
    struct FilesPanel;
    struct HierarchyPanel;
    struct InspectorPanel;
    struct LogPanel;

    // Case-insensitive substring search for the panels' search boxes. Lowercase
    // the query once when it changes, then match each item against it.
    inline std::string SearchToLower(std::string_view s)
    {
        std::string r; r.reserve(s.size());
        for (char c : s) r += (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
        return r;
    }
    inline bool SearchMatches(std::string_view haystack, std::string_view needleLower)
    {
        if (needleLower.empty()) return true;
        if (needleLower.size() > haystack.size()) return false;
        for (size_t i = 0; i + needleLower.size() <= haystack.size(); ++i)
        {
            bool ok = true;
            for (size_t j = 0; j < needleLower.size(); ++j)
            {
                char a = haystack[i + j];
                if (a >= 'A' && a <= 'Z') a = char(a + 32);
                if (a != needleLower[j]) { ok = false; break; }
            }
            if (ok) return true;
        }
        return false;
    }

    enum class ViewMode : uint32_t
    {
        Wireframe   = 0, 
        Solid       = 1, 
        Textured    = 2, 
		Lit         = 4, 
		SolidLit    = Solid | Lit,
        TexturedLit = Textured | Lit,
        Default     = TexturedLit
    };

    // The palettes the View menu and Settings > Appearance offer, by name and file.
    inline constexpr std::string_view kThemeNames[] = { "Dark", "Light" };
    inline constexpr const char*      kThemePaths[] = { "themes/dark.css", "themes/light.css" };

    // The showcase's saved UI state, under the resource root (workdirectory/ in the source tree).
    inline constexpr const char* kUIStateFile = "ui_state.json";

    // A main window that documents were dragged out to, with the first window's chrome.
    struct DocumentWindow
    {
        ID          windowID    = ID::Invalid();
        ID          documentsID = ID::Invalid();
        DocumentSet documents;
        int2        spawnPosition {};      // screen pixels, used on the first frame only
        bool        positioned = false;    // once true, the OS manages the position
    };

    class Application : public GUIApplication
    {
    public:
        // Panels to bring to the front of their tab groups at startup, by title.
        void AddStartPanel(std::string title) { m_startPanels.push_back(std::move(title)); }
        void AddFloatPanel(std::string title) { m_floatPanels.push_back(std::move(title)); }
        // The disk folder the Files panel shows a copy of; the workdirectory when empty.
        void SetFilesRoot(std::filesystem::path path) { m_filesRoot = std::move(path); }
        // The palette to start with, over the one saved at the last exit.
        void SetStartTheme(std::string path) { m_startTheme = std::move(path); }

        // ---- Toolbar model (used by the View panel) -----------------------------
        struct ToolbarLayoutModel
        {
            std::vector<ToolbarItem>         sides[kToolbarSideCount];
            ToolbarDockState                 dockState{};
            // Torn-off toolbars, each in its own OS window; dock-back moves the
            // item back into a side vector.
            std::vector<FloatingToolbarItem> floatingItems;
        };
        ToolbarLayoutModel toolbars;
        ToolbarDockCallbacks toolbarCallbacks;
        FloatingToolbarOptions toolbarOpts;

		bool            showGrid         = true;
        bool            enableAudio      = true;
		ViewMode        viewMode         = ViewMode::Default;   // View mode toolbar buttons toggle these flags
        CommandMode     nodeViewMode = CommandMode::Default;   // the node view's shortcuts; copied into each NodeView's commandMode
        // The transform tool the left toolbar picks (the showcase's own, not a library mode).

        std::vector<StyleRef> boxStyleRefs;
        StyleRef borderTestStyleRef;

        // Classes from themes/showcase.css, looked up after each theme load.
        StyleRef hierarchyVizCellStyleRef, hierarchyVizBgStyleRef, hierarchyVisibilityStyleRef;
        StyleRef logLevelIconStyleRef, logLevelWellStyleRef;
        StyleRef logLevelPillStyleRefs[3];   // indexed by LogLevel
        StyleRef iconToggleStyleRef, logMessageStyleRef, logCellStyleRef, logCellTimeStyleRef, logCountBadgeStyleRef;
        StyleRef logTableStyleRef, filesToolbarStyleRef, appIconStyleRef;
        StyleRef inspectorPairHalfStyleRef, inspectorCaptionStyleRef, inspectorHeaderStyleRef, inspectorObjectIconStyleRef, inspectorObjectNamesStyleRef, inspectorObjectTitleStyleRef, inspectorObjectPathStyleRef, inspectorSearchRowStyleRef, inspectorSectionStyleRef, inspectorSectionFirstStyleRef, inspectorSectionBodyStyleRef;
        StyleRef profilerHeaderStyleRef, profilerRecordStyleRef, profilerPauseStyleRef, profilerMsStyleRef, profilerFpsStyleRef;
        StyleRef profilerPctCellStyleRef, profilerPctTrackStyleRef, profilerPctFillStyleRef, profilerPctTextStyleRef;
        StyleRef logDetailPaneStyleRef, logDetailTextStyleRef;
        StyleRef shortcutCategoryStyleRef, shortcutTableStyleRef, shortcutCellStyleRef, settingsTitleStyleRef, settingsScrollStyleRef, sketchPadStyleRef;
        StyleRef dataTableCellStyleRef, dataTableNumberStyleRef;
        StyleRef textShadowSampleStyleRef, textGlowSampleStyleRef;
        StyleRef iconVAlignDemoStyleRefs[4][3];   // [IconSide L/R/T/B][top/mid/bot]

        tachyon::VisualColorStyleIndex triangleFillStyle;
        tachyon::VisualColorStyleIndex triangleFillStyle2;
        tachyon::VisualColorStyleIndex quadFillStyle;
        tachyon::VisualColorStyleIndex animatedHueStyle = tachyon::VisualColorStyleIndex::Invalid; // misc panel; recreated after a theme reload

        tachyon::ColorStyle triangleFillColorStyle;
        tachyon::ColorStyle quadFillColorStyle;

        ImageIndex checkerboardIndex { ImageIndex::Invalid };
        ImageIndex demoImageIndex { ImageIndex::Invalid };   // sample image for the Image() element demo
        std::vector<uint8_t> demoImagePixels;                 // its RGBA, for "Copy image"
        // "Paste image": the clipboard's image, shown in the Misc panel, while there is one.
        tachyon::Image*  pastedImage = nullptr;
        ImageIndex       pastedImageIndex { ImageIndex::Invalid };
        uint2            pastedImageSize {};
        // "Save" in the progress demos: busy until this time (seconds on the frame clock).
        float            saveBusyUntil = -1.0f;
        // The import demo: when it started, and whether it was cancelled.
        float            importStart = 0.0f;
        bool             importCancelled = false;
        // The failed bake demo: running again until this time after Retry.
        float            bakeRetryUntil = -1.0f;
        // The background tasks demo: which were cancelled.
        bool             backgroundCancelled[3] = {};
        // The timeline editor demo: its tracks, set up the first time it's drawn.
        TimelineView     animation;
        bool             animationSeeded = false;
        // The Viewport panel's scene: its shapes, the view onto them, and what draws them.
        std::vector<SceneObject> sceneObjects;
        int              sceneSelected = -1;
        ViewportView     scene;
        SceneRenderer    sceneRenderer;
        // The sketch pad: strokes drawn with the pen (or the mouse), wider where it pressed harder.
        struct SketchPoint  { float2 pos; float width; };
        struct SketchStroke { std::vector<SketchPoint> points; };
        std::vector<SketchStroke> sketchStrokes;
        PointerDrag      sketchDrag;
        PenState         sketchPen;          // as of the latest pointer event over the pad
        float2           sketchCursor {};    // where the pointer is on the pad
        bool             sketchHovered = false;

        // ---- Showcase state ----
        // State used by the showcase panels for testing and prototyping GUI features.
        // This will be removed or moved to an examples/ directory once the framework matures.
        struct ShowcaseState
        {
            ID windowLayoutID1 { };
            ID windowLayoutID2 { };
            ID dragGhostWindowID {};

            // The theme editor on the Appearance page: its search, which categories are open,
            // and the Reset dialog.
            struct ThemeEditor
            {
                std::string                 search;
                std::map<std::string, bool> open;
                bool                        confirmReset = false;
                ID                          resetDialogID {};
            } themeEditor;

            // Reorderable demo state
            ID reorderTabsID { ID::Invalid() };
            ID reorderListID { ID::Invalid() };
            ID reorderWrappedHorzID { ID::Invalid() };
            ID reorderWrappedVertID { ID::Invalid() };
            ID reorderStringListID { ID::Invalid() };
            ID reorderFloatListID { ID::Invalid() };

            int testBoxStyleCount = 0;

            float sliderValue = 0.5f;
            float sliderFieldValue = 50.0f; // SliderField demo (range [0, 100])
            tachyon::color4f demoColor { 0.2f, 0.5f, 0.8f, 0.5f }; // ColorField demo, half transparent to show its alpha
            tachyon::Gradient demoGradient = tachyon::Gradient::Linear(
                tachyon::color4f{1.0f, 0.0f, 0.0f, 1.0f},
                tachyon::color4f{0.0f, 0.0f, 1.0f, 1.0f}); // GradientField demo
            // GradientField on a GradientF: an HDR heat ramp with a step and a smooth stretch.
            tachyon::GradientF demoGradientF = []
            {
                tachyon::GradientF g;
                g.colorSpace = tachyon::ColorSpace::Oklab;
                g.AddStop(tachyon::color4f{ 0.05f, 0.0f, 0.1f, 1.0f }, 0.0f, tachyon::StopInterpolation::Smooth);
                g.AddStop(tachyon::color4f{ 0.9f, 0.2f, 0.05f, 1.0f }, 0.45f, tachyon::StopInterpolation::Linear);
                g.AddStop(tachyon::color4f{ 1.0f, 0.8f, 0.3f, 1.0f }, 0.75f, tachyon::StopInterpolation::Constant);
                g.AddStop(tachyon::color4f{ 3.0f, 2.6f, 2.0f, 1.0f }, 0.9f);   // brighter than white
                return g;
            }();
            // CurveField demos: a falloff, and a color's red, green, blue and alpha curves.
            tachyon::CurveView demoCurve;
            tachyon::CurveView demoColorCurves;
            bool               curvesSeeded = false;
            // Plot demo: the demo image's levels, its R, G and B counted into 64 bins.
            tachyon::PlotView  demoLevels;
            int                demoLevelsKind = 1;      // a PlotSeriesKind: bars, area or line
            bool               demoLevelsLog = false;
            int listSelected = 0;

            // ListView
            int listViewSelected = 0;
            std::vector<std::string> listViewItems = {
                "Documents", "Downloads", "Pictures", "Videos",
                "Music", "Desktop", "Projects", "Backup"
            };

            int buttonClickCount = 0;
            bool toggleValue = false;

            // ButtonGroup demo: segmented alignment picker (one selected at a time).
            int alignment = 1; // 0 = left, 1 = center, 2 = right
            // ToggleGroup demo: text style flags (independent toggles).
            bool bold      = false;
            bool italic    = true;
            bool underline = false;

            std::string textBox1;
            std::string textBox2;
            std::string fruit;    // the inspector's autocomplete field

            // Inline-edit
            std::string inlineEditText = "Click me to edit";
            std::string inlineEditBuffer;  // separate edit buffer for validation
            bool        inlineEditActive = false;
            bool        inlineEditFirstFrame = false;

            float floatValue = 0.5f;
            double doubleValue = 0.6;
            int64_t int64Value = -43;
            int32_t int32Value = -42;
            int16_t int16Value = -45;
            int8_t  int8Value = -44;
            uint64_t uint64Value = 43;
            uint32_t uint32Value = 42;
            uint16_t uint16Value = 45;
            uint8_t  uint8Value = 44;

            float clampedFloat = 0.5f;
            uint8_t clampedUInt8 = 50;

            float nanFloat = std::numeric_limits<float>::quiet_NaN();
            float posInfFloat = std::numeric_limits<float>::infinity();
            float negInfFloat = -std::numeric_limits<float>::infinity();

            int2  int2Value = { 1,2 };
            int3  int3Value = { 1,2,3 };
            int4  int4Value = { 1,2,3,4 };

            // Collection
            std::vector<std::string> stringList = { "Apple", "Banana", "Cherry" };
            std::vector<float> floatList = { 1.0f, 2.5f, 3.14f };
            std::vector<std::pair<std::string, float>> dictionary = { {"speed", 1.0f}, {"health", 100.0f}, {"damage", 25.0f} };

            ID textBoxId1 { ID::Invalid() };
            ID textBoxId2 { ID::Invalid() };

            // Each NodeView is an independent graph with its own pan/zoom and drag state.
            NodeView nodeView;
            bool     nodeFramesSeeded = false;   // the "Inputs" frame, put round its nodes once they're laid out
            // The values of the nodes added from the canvas menu, by their stableID.
            struct GraphNodeValues
            {
                float value = 0.5f;   // Value
                int   op    = 0;      // Math: Add, Multiply, Min, Max
                float scale = 4.0f;   // Noise
                int   seed  = 1;      // Noise
            };
            std::unordered_map<tachyon::ID, GraphNodeValues> graphNodeValues;
            // The node view's edits, for Undo / Redo (the showcase's only undoable changes).
            bool exitRequested = false;   // Exit with unsaved documents: asking
            ID   exitWindow    = ID::Invalid();   // the window asking
            ID   exitDialogID  = ID::Invalid();

            // Values bound by the node properties' renderBody lambdas.
            float       nodeStrength = 0.5f;
            float       nodeGain     = 1.0f;
            int32_t     nodeSeed     = 7;
            std::string nodeName     = "Source";
            bool        nodeEnabled  = true;

            // Table demos (Table panel + Shortcuts settings page)
            TableState tablePanelTableState;         // resizable Name/Type/Value table in panel_table.cpp
            TableState tablePanelSimpleState;        // 3-column A/B/C demo table in panel_table.cpp
            TableState tablePanelSizingState;        // column-sizing demo (fixed / resizable / stretch) in panel_table.cpp
            TableState tablePanelVirtualTableState;  // virtual-scrolled 50k-row demo in panel_table.cpp
            TableState frozenTableState;             // virtual table with frozenLeftColumns demo in panel_split_scroll.cpp
            TableState shortcutsTableState;          // Category/Command/Shortcut in the Shortcuts settings page

            // ---- TreeTable demo (hierarchical rows in a table) ----
            // Flat row list; a closed node is skipped by advancing skipCount + 1.
            struct TreeTableRow {
                std::string  name;
                std::string  size;
                std::string  type;
                int          depth       = 0;
                bool         hasChildren = false;
                bool         isOpen      = true;
                IconID       icon        = IconID::Invalid;
                tachyon::ID      id          = tachyon::ID::Invalid();
                // Descendant count. Recomputed by RebuildTreeTableIndex() after edits.
                int          skipCount   = 0;
                // Guide-line metadata (also recomputed by RebuildTreeTableIndex):
                //   ancestorLineMask - bit d set iff the ancestor at depth d+1 has
                //       a next sibling, so its connector line passes this row at column d.
                //   isLastSibling    - last child of its parent; the own-depth guide
                //       line ends at the row center as an L-tick.
                uint32_t     ancestorLineMask = 0;
                bool         isLastSibling    = true;
            };
            std::vector<TreeTableRow> treeTableRows;
            TableState                treeTableState;
            MultiSelectState          treeTableMultiSelect;
            // Row drag-and-drop (TableState.rowDragDrop); payload is the dataset index.
            ItemDragDropConfig        treeTableDragDrop;
            // Visible row -> flat row index, rebuilt each frame.
            std::vector<int>          treeTableVisibleIdx;
            // Moved row's new flat index, applied to the selection after the next
            // VirtualTable call (which reallocates the row IDs).
            int                       treeTablePendingSelectFlatIdx = -1;

            // Icon IDs
            IconID circleIcon = IconID::Invalid;
            IconID heartIcon = IconID::Invalid;
            IconID starIcon = IconID::Invalid;

            // Console log-level icons (+ monochrome variants for the filter toggles)
            IconID infoIcon      = IconID::Invalid;
            IconID warningIcon   = IconID::Invalid;
            IconID errorIcon     = IconID::Invalid;
            IconID dotIcon       = IconID::Invalid;   // a filled dot in the text color
            IconID pauseIcon     = IconID::Invalid;
            IconID listIcon      = IconID::Invalid;
            IconID layoutGridIcon = IconID::Invalid;
            IconID boxIcon          = IconID::Invalid;
            IconID moreIcon         = IconID::Invalid;
            IconID chevronDownIcon  = IconID::Invalid;
            IconID chevronRightIcon = IconID::Invalid;

            IconID folderClosedIcon = IconID::Invalid;
            IconID folderOpenIcon   = IconID::Invalid;
                                    
                                    
            IconID audioOnIcon      = IconID::Invalid;
            IconID audioOffIcon     = IconID::Invalid;
                                    
            IconID gridOnIcon       = IconID::Invalid;
            IconID gridOffIcon      = IconID::Invalid;
                                    
            IconID lightOnIcon      = IconID::Invalid;
            IconID lightOffIcon     = IconID::Invalid;
                                    
            IconID toolSelectIcon   = IconID::Invalid;
            IconID toolMoveIcon     = IconID::Invalid;
            IconID toolScaleIcon    = IconID::Invalid;
            IconID toolRotateIcon   = IconID::Invalid;
                                    
            IconID viewWireframe    = IconID::Invalid;
            IconID viewSolid        = IconID::Invalid;
            IconID viewTextured     = IconID::Invalid;

			IconID panelFilesIcon      = IconID::Invalid;
			IconID panelGameIcon       = IconID::Invalid;
            IconID panelHierarchyIcon  = IconID::Invalid;
			IconID panelInspectorIcon  = IconID::Invalid;
			IconID panelLogIcon        = IconID::Invalid;
			IconID panelNodeViewIcon   = IconID::Invalid;
			IconID panelProfilerIcon   = IconID::Invalid;
			IconID panelSettingsIcon   = IconID::Invalid;
			IconID panelViewportIcon   = IconID::Invalid;
            IconID panelMiscIcon       = IconID::Invalid;

            IconID documentLevelIcon     = IconID::Invalid;
            IconID documentMaterialIcon  = IconID::Invalid;
            IconID documentLayoutIcon    = IconID::Invalid;
            IconID documentPrefabIcon    = IconID::Invalid;
            IconID documentAnimationIcon = IconID::Invalid;
                                       
            IconID objectIcon          = IconID::Invalid;
            IconID object2Icon         = IconID::Invalid;
                                       
            IconID fileImageIcon       = IconID::Invalid;
            IconID fileAudioIcon       = IconID::Invalid;
            IconID fileVideoIcon       = IconID::Invalid;
            IconID fileModelIcon       = IconID::Invalid;
            IconID fileTextIcon        = IconID::Invalid;
            IconID fileArchiveIcon     = IconID::Invalid;
                                       
            IconID openFileIcon        = IconID::Invalid;
            IconID saveIcon            = IconID::Invalid;
			IconID saveAsIcon          = IconID::Invalid;
			IconID closeIcon           = IconID::Invalid;
			IconID addIcon             = IconID::Invalid;
			IconID removeIcon          = IconID::Invalid;
			IconID undoIcon            = IconID::Invalid;
			IconID redoIcon            = IconID::Invalid;
			IconID cutIcon             = IconID::Invalid;
			IconID copyIcon            = IconID::Invalid;
			IconID pasteIcon           = IconID::Invalid;
			IconID blockIcon           = IconID::Invalid;
			IconID syncIcon            = IconID::Invalid;
			IconID downloadIcon        = IconID::Invalid;
            IconID uploadIcon          = IconID::Invalid;
			IconID publishIcon         = IconID::Invalid;   
			IconID openInWindowIcon    = IconID::Invalid;
                                       
            IconID duplicateIcon       = IconID::Invalid;
            IconID newIcon             = IconID::Invalid;
            IconID selectAllIcon       = IconID::Invalid;
            IconID deleteIcon          = IconID::Invalid;
            IconID zoomInIcon          = IconID::Invalid;
            IconID zoomOutIcon         = IconID::Invalid;
            IconID findIcon            = IconID::Invalid;
            IconID playIcon            = IconID::Invalid;
            IconID stopIcon            = IconID::Invalid;
            IconID recenterIcon        = IconID::Invalid;
            IconID frameSelectedIcon   = IconID::Invalid;
            IconID brushIcon           = IconID::Invalid;
            IconID eraserIcon          = IconID::Invalid;
            IconID draggableIcon       = IconID::Invalid;

            IconID darkModeIcon        = IconID::Invalid;
            IconID lightModeIcon       = IconID::Invalid;
            IconID lightModeAutoIcon   = IconID::Invalid;
            IconID fullscreenIcon      = IconID::Invalid;
            IconID fullscreenExitIcon  = IconID::Invalid;
            IconID viewSelectedIcon    = IconID::Invalid;
            IconID dropperIcon         = IconID::Invalid;
            IconID editIcon            = IconID::Invalid;
            IconID tagIcon             = IconID::Invalid;
            IconID targetIcon          = IconID::Invalid;
            IconID visibilityLockIcon  = IconID::Invalid;
            IconID visibilityMixedIcon = IconID::Invalid;
            IconID visibilityOffIcon   = IconID::Invalid;
            IconID visibilityOnIcon    = IconID::Invalid;
            
            IconID dropdownArrowIcon   = IconID::Invalid;
            IconID iconCollapseAllIcon = IconID::Invalid;

            IconID keyCommandIcon = IconID::Invalid;


            // Command handles shared by the main menu and toolbar.
            // cmdDocumentation / cmdAbout have no shortcut key.
            CommandID cmdNew = {}, cmdOpen = {}, cmdSave = {}, cmdSaveAs = {}, cmdExit = {};
            CommandID cmdUndo = {}, cmdRedo = {}, cmdCut = {}, cmdCopy = {}, cmdPaste = {};
            CommandID cmdDocumentation = {}, cmdAbout = {};
            // F11 toggles the focused window's fullscreen and back to the mode it had before.
            CommandID  cmdFullscreen = {};
            std::unordered_map<ID, WindowMode> modeBeforeFullscreen;   // by window, while it's fullscreen
            CommandID cmdSearchCommands = {};
            CommandSearchState commandSearch;
            CommandID cmdDuplicate = {}, cmdSelectAll = {};
            CommandID cmdDelete = {}, cmdZoomIn = {}, cmdZoomOut = {};
            CommandID cmdFind = {}, cmdPlay = {}, cmdStop = {};
            CommandID cmdRecenter = {}, cmdFrameSelected = {};
            CommandID cmdBrush = {}, cmdEraser = {};

            ID smallMenuID{ ID::Invalid() };
            ID cameraMenuID{ ID::Invalid() };
            int cameraView = 0;   // the viewport toolbar's camera menu: perspective, orthographic, ...
            ID bigMenuID{ ID::Invalid() };
            int quality = 1;      // the inspector's dropdowns
            int fontIndex = 0;

            // GridView demo
            int gridViewSelected = -1;

            // TabBar demo
            int tabBarSelected = 0;
            std::vector<std::string> tabNames = { "File", "Edit", "View", "Help" };
            ID tabBarID { ID::Invalid() };

            // Documents, each with its own panels; the first holds the full DockingContainer demo
            ID            documentsID { ID::Invalid() };
            DocumentSet   documents;
            Document*     closingDocument = nullptr; // asked about its unsaved changes
            DocumentSet*  closingSet      = nullptr; // the set it's in
            std::vector<std::unique_ptr<DocumentWindow>> documentWindows;   // documents dragged out
            bool          mainWindowOpen = true;
            ToastID       uploadToast = 0;          // the demo upload, its progress moving
            float         uploadStart = 0.0f;       // when it started, frame time in seconds
            bool          inspectorUnsaved = true;  // the Inspector's banners
            bool          inspectorReadOnly = true;
            ID            closeDocumentDialogID { ID::Invalid() };
            int           untitledCount = 0;
            std::string   dockNotes;
            bool          dockToggle = false;
            bool          dockInitialized = false;
            std::string   savedUIState;   // kUIStateFile's text, restored at start

            // The title bar's layout picker: the built-in layouts, the saved ones, and the file
            // types' ones (kept in the saved UI state).
            LayoutPresets layoutPresets;
            ID            pendingLayoutDocument { ID::Invalid() };   // given pendingLayoutName next frame
            std::string   pendingLayoutName;
            ID            namingLayoutDocument { ID::Invalid() };    // "Save layout as...": asking for a name
            std::string   layoutNameInput;
            ID            layoutNameDialogID { ID::Invalid() };

            // Settings panel chrome (page list, selection, tree width); page content
            // state lives in the fields below.
            SettingsPanelState settings;

            // Appearance page bound values, kept in the saved UI state
            std::string settingsThemePath;            // the palette loaded
            int     settingsMotionIndex = 0; // 0 = as the OS, 1 = reduced, 2 = full
            int     settingsAccentIndex = 0; // into kAccentPresets; 0 = the palette's own blue
            int     settingsWheelIndex = int(DefaultWheelMode());   // a WheelMode: 0 zooms, 1 scrolls
            float   settingsFontScalePct = 100.0f; // percent; 100 = unscaled

            // Misc dummy page values
            std::string settingsUserName = "Player One";
            bool        settingsAutosave = true;
            int         settingsMaxUndo = 64;

            // Shortcuts page state. Lists shortcuts whose mode is in the category
            // selection; an empty selection re-selects "All".
            std::string      settingsShortcutsSearch;
            std::string      settingsShortcutsSearchLower; // cached lowercased copy of the search
            MultiSelectState settingsShortcutsCatSelection;
            int              settingsShortcutsCategoryWidth = 160; // CSS pixels
            // Category-list VirtualTreeView. Rows / VisibleIdx are rebuilt each
            // frame; InternalMulti is the table's sub-range selection, mapped
            // to/from settingsShortcutsCatSelection's static IDs.
            std::vector<TreeViewRow> settingsShortcutsCatRows;
            std::vector<int>         settingsShortcutsCatVisibleIdx;
            TableState               settingsShortcutsCatTableState;
            MultiSelectState         settingsShortcutsCatInternalMulti;

            // Must persist across frames: the column-sizer double-click handler
            // holds &column.internal.maxContentWidth and fires after OnGUI returns.
            struct ShortcutVisRow { uint32_t mode; uint32_t entryIdx; };
            std::vector<VirtualTableColumn> settingsShortcutsColumns;        // persistent column template (built once)
            std::vector<VirtualTableColumn> settingsShortcutsActiveColumns;  // per-frame active subset (Context dropped in single-category view)
            std::vector<ShortcutVisRow>     settingsShortcutsVisRows;
            // Filter fingerprint: visRows are rebuilt only when it changes. A mask
            // of 0 never occurs, so it forces a rebuild on the first frame.
            uint64_t                        settingsShortcutsFilterMask = 0;
            std::string                     settingsShortcutsFilterQuery;
        };
        ShowcaseState showcase;


    public:
        Result<void> Initialize(const InitializeOptions& options = {}) override;

        // Re-registers showcase-owned styles after a theme reload.
        void OnStylesReloaded() override;
        void ApplyNodeCategories();

        void Cleanup() override;

        bool InitializeShowcaseGUI();
        bool InitializeShowcaseStyles();
        bool InitializeShowcaseResources();

        // One of the showcase's own files (icons, the saved UI state) under the resource root.
        std::string ResourceFile(std::string_view relative) const
        {
            return (std::filesystem::path(GetResourceRoot()) / relative).string();
        }

        void OnGUI() override;

    private:
        // Settings pages (panel_settings.cpp). InitSettingsPages() runs once after
        // icon registration. Appearance mutates global state (theme, font scale).
        void InitSettingsPages();
        void RenderSettingsAppearance();
        void RenderSettingsGeneral();
        /// The font scale, as a percentage: 100 to 300 in steps of 10, the theme reloaded at it.
        void SetFontScale(float percent);
        void RenderSettingsShortcuts();
        /// Reduced motion as settingsMotionIndex says.
        void ApplyMotionSetting();
        /// The wheel mode settingsWheelIndex says.
        void ApplyWheelSetting();
        /// The accent settingsAccentIndex picks: none for the palette's own.
        void ApplyAccentSetting();
        /// The Appearance settings read from the saved UI state, applied (--theme wins).
        void ApplySavedAppearance();

        // Dock-panel body renderers (one per panel_*.cpp), called each frame
        // while the panel is the active tab.
        void RenderReorderablePanel();
        void RenderTimelinePanel();
        void RenderSketchPanel();
        void EraseSketchAt(float2 at, float radius);
        /// The Appearance page's theme editor: the theme's colors and sizes by category, each
        /// change kept for that theme (settings_theme.cpp).
        void RenderThemeEditor();
        /// A theme variable's row: its label, and a field for its type.
        void ThemeVariableRow(const CustomPropertyInfo& info);
        /// The theme picker's row: Dark or Light, and Reset for the changes made to it.
        void RenderThemePicker();
        /// The timeline editor demo's tracks, keys, sound and events, and its callbacks.
        void SeedAnimation();
        /// The Inspector's curves: a falloff, and a color's channels; their edits on the undo history.
        void SeedCurves();
        /// The Inspector's Levels plot: the demo image's histogram and black, gray and white handles.
        void SeedLevels();
        void RenderHierarchyPanel(HierarchyPanel& panel);
        void RenderNodeViewPanel();
        /// A reroute (NodeModel::reroute) in `color`, unwired, its dot on the grid point
        /// nearest `at` (node.pos space).
        NodeModel RerouteNode(float2 at, color4f color);
        /// Removes the node view's selected nodes, their wires with them, and its selected
        /// frames, as one edit.
        void DeleteSelectedNodes();
        /// Puts a frame titled `title` round the node view's nodes at `indices`, as one edit.
        /// A frame round the node view's selected nodes and frames, then selected itself.
        void GroupSelection();
        /// The kinds of node the canvas menu adds.
        enum class GraphNodeKind { Value, Math, Noise, Switch };
        /// Adds a node of `kind` with its top-left at `at` (node.pos space), as one edit.
        void AddGraphNode(GraphNodeKind kind, float2 at);

        void RenderBoxTestPanel();
        void RenderInspectorPanel(InspectorPanel& panel);
        void RenderMiscPanel();
        /// Puts the sample image on the clipboard.
        void CopyDemoImage();
        /// Shows the clipboard's image in the Misc panel, in place of the one pasted before.
        void PasteImage();
        void RenderTablePanel();
        void RenderLogPanel(LogPanel& panel);
        void RenderTreeTablePanel();
        void RenderSplitScrollPanel();
        void RenderViewportPanel();
        /// The Viewport panel's shapes and the view onto them, the first time it's shown.
        void SeedScene();
        /// Picks scene object `index` (-1: none) for the viewport's gizmo.
        void SelectSceneObject(int index);
        
        void EnableProfilerTreePanel();
        void RenderProfilerTreePanel();
        /// The profiler panel's static IDs go back (at Cleanup).
        void ReleaseProfilerTreeIDs();

        // Main menu bar across the top of the main window (File / Edit / View / Help).
        /// Which of kThemePaths the theme is (however it was picked: the menu, the settings,
        /// --theme); -1 for another.
        int CurrentThemeIndex() const;
        /// The title bar of `window`, with the tabs of `documents` (their bar is `documentsID`),
        /// and under it the menus for the active document; `withSearch` adds the command search
        /// (one window has it).
        void RenderMainMenu(ID window, ID documentsID, DocumentSet& documents, bool withSearch = true);
        /// The title bar's layout picker for the active document of `documents`.
        void RenderLayoutMenu(DocumentSet& documents);
        /// While "Save layout as..." waits, in the window asking: the name to save it as.
        void AskLayoutName(DocumentSet& documents);
        /// The layouts a document can be given; the file types' ones are set here, and may be
        /// changed by the saved UI state.
        void AddBuiltInLayouts();
        /// Gives a document the layout of that name, making the panels it lacks. False when
        /// there's none of that name or it can't be applied.
        bool ApplyLayoutPreset(Document& document, std::string_view name);
        /// The layout picked last frame, given to its document before the documents are built.
        void ApplyPendingLayout();
        /// The document with that ID, in any window; null when it's gone.
        Document* FindShowcaseDocument(ID id);
        /// The documents of the main window `windowID` is; the first open one's for another.
        DocumentSet& DocumentsOfWindow(ID windowID);
        /// A file opened in `set` as a document (its type's layout, if it has one), or brought to
        /// the front when it's open already; `icon` for a type the showcase has none for.
        void OpenFileAsDocument(DocumentSet& set, std::string_view fileName, IconID icon);
        /// State on the left, the active document in `documents`, frame time on the right.
        void RenderStatusBar(const DocumentSet& documents);
        /// Quits, or asks first (in `from`, else the window with the focus) while a document in
        /// any window has unsaved changes.
        void RequestExit(ID from = ID::Invalid());
        /// While an exit waits, in the window asking: lists the unsaved documents and offers
        /// Save All, Discard or Cancel.
        void AskBeforeExit(ID window);
        /// The documents with unsaved changes, in every window.
        std::vector<Document*> UnsavedDocuments();
        /// Saves a document (the showcase only marks it saved).
        void SaveShowcaseDocument(Document& document);

        /// A panel the showcase knows by its title ("Viewport", "Inspector", ...), added to `layout`.
        ID   CreateShowcasePanel(DockingLayout& layout, std::string_view title);
        /// Help > Show Notifications: a toast of each kind.
        void ShowDemoToasts();
        /// Moves the demo upload's progress bar along.
        void UpdateDemoToasts();
        /// A document of `kind` with its panels (the samples by name), added to `set`.
        Document& CreateShowcaseDocument(DocumentSet& set, std::string name, std::string extension, std::string kind);
        /// On the first frame: the documents as they were saved, else the samples.
        void MakeShowcaseDocuments();
        /// A document's panels: `main`, and `side` docked to its right.
        void FillDocument(Document& document, std::string_view main, std::string_view side);
        /// While an unsaved document of `set` waits to close: asks whether to save or discard its changes.
        void AskBeforeClosingDocument(DocumentSet& set);
        /// A set's "+", its unsaved-changes question and its tear-off into a window of its own.
        void SetUpDocuments(DocumentSet& documents);
        /// The windows documents were dragged out to, each with the main window's chrome.
        void RenderDocumentWindows();
        /// `window` into `fullscreen` (Fullscreen or ExclusiveFullscreen), or out of fullscreen when in it.
        void ToggleFullscreen(ID window, WindowMode fullscreen);
        /// The window F11 is for: the focused one with the showcase's menus, else the main window.
        ID FullscreenTarget() const;

        /// The panels with data of their own, each made with its own (and added to `panels`).
        ID CreateFilesPanel(std::unordered_map<ID, Panel>& panels, std::string title);
        ID CreateHierarchyPanel(std::unordered_map<ID, Panel>& panels, std::string title);
        ID CreateInspectorPanel(std::unordered_map<ID, Panel>& panels, std::string title);
        ID CreateLogPanel(std::unordered_map<ID, Panel>& panels, std::string title);

        void EnableFilesPanel(FilesPanel& panel);
        void DisableFilesPanel(FilesPanel& panel);
        void RenderFilesPanel(FilesPanel& panel);
        void MoveFileInto(FilesPanel& panel, uintptr_t payload, const std::string& folder);
        void ApplyFileRenames(FilesPanel& panel);
        void AddDroppedFiles(FilesPanel& panel, const std::vector<std::string>& paths, const std::string& folder);
        IconID FileIcon(const FilesPanel& panel, std::string_view name, bool isFolder, bool open = false);
        IconID FileIcon(const FilesPanel& panel, const FileEntry& file, bool open = false);

        // Build/tear down panel data; wired into CreatePanel's onEnable/onDisable.
        void EnableHierarchyPanel(HierarchyPanel& panel);
        void EnableInspectorPanel(InspectorPanel& panel);
        void DisableInspectorPanel(InspectorPanel& panel);
        void EnableLogPanel(LogPanel& panel);
    private:
        // The Files panels made, to let go of their icons before the text system goes.
        std::vector<std::weak_ptr<FilesPanel>> m_filesPanels;
        std::vector<std::string> m_startPanels;
        std::vector<std::string> m_floatPanels;   // --float: start in their own windows
        std::filesystem::path    m_filesRoot;     // --files
        std::string              m_startTheme;    // --theme
    };

} // namespace tachyon
