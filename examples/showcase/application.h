#pragma once

#include <gui_controls/gui_application.h>
#include <gui_controls/controls_toolbar_dock.h>
#include <gui_core/input/gui_pointer_drag.h>

#include "scene.h"
#if TACHYON_VULKAN || TACHYON_D3D12 || TACHYON_SDLGPU
#include "scene_renderer.h"
#endif

#include <chrono>
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
    struct ViewportPanel;
    struct TablePanel;
    struct FrozenTablePanel;
    struct TreeTablePanel;
    struct SketchPanel;
    struct ProfilerPanel;
    struct ReorderablePanel;
    struct NodeGraphPanel;
    struct StateMachinePanel;

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
        // --capture: draws every panel to an image in each theme, saves them in `folder`, and
        // quits, instead of the usual windows (capture.cpp).
        void SetCaptureDir(std::string folder) { m_captureDir = std::move(folder); }
        bool IsCapturing() const { return !m_captureDir.empty(); }
        // --bench: draws every frame with the profiler on, and after `frames` of them (past a
        // warm-up) prints each profiled scope's mean time per frame and the GPU's (Vulkan); then
        // again with the draw cache off, every scope rebuilt each frame; then quits.
        void SetBenchFrames(int frames) { m_bench.frames = frames; }
        bool IsBenching() const { return m_bench.frames > 0; }

        // A Settings panel's own: the page shown, the page list, and each page's search,
        // selection and tables. The settings themselves are the app's (ShowcaseState).
        struct SettingsView
        {
            IDManager*         ids = nullptr;   // its pages' static IDs and the Reset dialog's go back to it
            SettingsPanelState settings;

            // The theme editor on the Appearance page: its search, which categories are open,
            // and the Reset dialog.
            struct ThemeEditor
            {
                std::string                 search;
                std::map<std::string, bool> open;
                bool                        confirmReset = false;
                ID                          resetDialogID {};
            } themeEditor;

            // The Shortcuts page. Lists shortcuts whose mode is in the category selection; an
            // empty selection re-selects "All".
            struct Shortcuts
            {
                std::string      search;
                std::string      searchLower;          // cached lowercased copy of the search
                MultiSelectState catSelection;
                int              categoryWidth = 160;  // CSS pixels
                // Category-list VirtualTreeView. Rows / visible rows are rebuilt each frame;
                // catInternalMulti is the table's sub-range selection, mapped to/from
                // catSelection's IDs.
                std::vector<TreeViewRow> catRows;
                std::vector<int>         catVisibleIdx;
                TableState               catTableState;
                MultiSelectState         catInternalMulti;

                // Must persist across frames: the column-sizer double-click handler
                // holds &column.internal.maxContentWidth and fires after OnGUI returns.
                struct VisRow { uint32_t mode; uint32_t entryIdx; };
                std::vector<VirtualTableColumn> columns;         // persistent column template (built once)
                std::vector<VirtualTableColumn> activeColumns;   // per-frame active subset (Context dropped in single-category view)
                std::vector<VisRow>             visRows;
                // Filter fingerprint: visRows are rebuilt only when it changes. A mask
                // of 0 never occurs, so it forces a rebuild on the first frame.
                uint64_t                        filterMask = 0;
                std::string                     filterQuery;
                TableState                      tableState;      // Category/Command/Shortcut
            } shortcuts;

            ~SettingsView()
            {
                if (!ids)
                    return;
                for (const SettingsPanelState::Page& page : settings.pages)
                    ids->ReleaseStaticID(page.id);
                if (themeEditor.resetDialogID != ID::Invalid())
                    ids->ReleaseStaticID(themeEditor.resetDialogID);
            }
        };

        // A TreeTable panel's row (hierarchical rows in a table). Flat row list; a closed node
        // is skipped by advancing skipCount + 1.
        struct TreeTableRow {
            std::string  name;
            std::string  size;
            std::string  type;
            int          depth       = 0;
            bool         hasChildren = false;
            bool         isOpen      = true;
            IconID       icon        = IconID::Invalid;
            tachyon::ID  id          = tachyon::ID::Invalid();
            // Descendant count. Recomputed by TreeUtils::RebuildTreeIndex after edits.
            int          skipCount   = 0;
            // Guide-line metadata (also recomputed by TreeUtils::RebuildTreeIndex):
            //   ancestorLineMask - bit d set iff the ancestor at depth d+1 has
            //       a next sibling, so its connector line passes this row at column d.
            //   isLastSibling    - last child of its parent; the own-depth guide
            //       line ends at the row center as an L-tick.
            uint32_t     ancestorLineMask = 0;
            bool         isLastSibling    = true;
        };

        CommandMode     nodeViewMode = CommandMode::Default;   // the node view's shortcuts; copied into each NodeView's commandMode
        CommandMode     stateMachineMode = CommandMode::Default;   // the state machines' shortcuts (Delete)
        CommandMode     hierarchyMode = CommandMode::Default;      // a Hierarchy's shortcuts while its tree has the focus
        CommandID       hierarchyDelete{};
        std::weak_ptr<HierarchyPanel> focusedHierarchy;           // the Hierarchy whose tree has the focus, if any
        // The transform tool the left toolbar picks (the showcase's own, not a library mode).

        std::vector<StyleRef> boxStyleRefs;
        StyleRef borderTestStyleRef;

        // Classes from themes/showcase.css, looked up after each theme load.
        StyleRef hierarchyVizCellStyleRef, hierarchyVisibilityStyleRef, hierarchyTableStyleRef;
        StyleRef logLevelDotStyleRefs[3];    // indexed by LogLevel
        StyleRef logLevelWellStyleRef, logToolbarStyleRef, logDetailSizerStyleRef;
        StyleRef statusDotStyleRef;     ///< the status bar's "ready" dot
        StyleRef statusReadyStyleRef;   ///< the dot and "Ready", close together
        StyleRef filesSizerStyleRef;    ///< the files panel's tree | grid split
        StyleRef logLevelPillStyleRefs[3];   // indexed by LogLevel
        StyleRef iconToggleStyleRef, logMessageStyleRef, logCellStyleRef, logCellTimeStyleRef, logCountBadgeStyleRef;
        StyleRef logTableStyleRef, filesToolbarStyleRef, appIconStyleRef;
        StyleRef inspectorPairHalfStyleRef, inspectorCaptionStyleRef, inspectorHeaderStyleRef, inspectorObjectIconStyleRef, inspectorObjectNamesStyleRef, inspectorObjectTitleStyleRef, inspectorObjectPathStyleRef, inspectorSearchRowStyleRef, inspectorSectionStyleRef, inspectorSectionFirstStyleRef, inspectorSectionBodyStyleRef;
        StyleRef profilerBodyStyleRef, profilerGraphStyleRef, profilerGraphLabelStyleRef;
        StyleRef profilerHeaderStyleRef, profilerRecordStyleRef, profilerPauseStyleRef, profilerMsStyleRef, profilerFpsStyleRef;
        StyleRef profilerPctCellStyleRef, profilerPctTrackStyleRef, profilerPctFillStyleRef, profilerPctTextStyleRef;
        StyleRef logDetailPaneStyleRef, logDetailTextStyleRef;
        StyleRef shortcutCategoryStyleRef, shortcutTableStyleRef, shortcutCellStyleRef, settingsTitleStyleRef, settingsScrollStyleRef, sketchPadStyleRef;
        StyleRef sketchToolbarStyleRef, sketchChipStyleRef, sketchChipLabelStyleRef, sketchChipValueStyleRef;
        StyleRef settingsBodyStyleRef, settingsHeadStyleRef, settingsTitleRowStyleRef, settingsSplitStyleRef;
        StyleRef tableToolbarStyleRef;
        StyleRef stateMachineToolbarStyleRef;   // Step, Restart, the breadcrumb, and the status at the end
        StyleRef stateMachineButtonStyleRef;    // Step and Restart
        StyleRef stateMachineDividerStyleRef;   // between the buttons and the breadcrumb
        StyleRef stateMachineStatusStyleRef;    // "Running"
        StyleRef statusDotOkStyleRef;           // the dot before it
        // The Build panel: the panel, the target's name and configuration, and Cancel and Build.
        StyleRef buildPanelStyleRef, buildHeaderStyleRef, buildTitleStyleRef, buildSubtitleStyleRef, buildFooterStyleRef;
        // The viewport's shaders compiling: the bar along its top, and the pill with a spinner.
        StyleRef viewportBusyBarStyleRef, viewportBusyPillStyleRef, viewportBusyTextStyleRef;
        // The Files panel's import: its block in the toolbar, and the tile of the file coming in.
        StyleRef filesImportTileStyleRef, filesImportWellStyleRef, filesImportNameStyleRef;
        // The Files panel's item styles, one per icon tint: registered again only when the
        // styles are rebuilt (StyleManager::Generation), the tint or the icon size changes.
        struct FileItemStyle
        {
            uint32_t generation = 0;
            color4f  tint {};
            uint16_t iconSize   = 0;
            StyleRef style {};
            bool     made       = false;
        };
        FileItemStyle fileItemStyles[3];
        // The hierarchy: "streaming" at the end of a row, and a row still loading.
        StyleRef hierarchyStreamingStyleRef, hierarchySkeletonRowStyleRef;
        StyleRef dataTableCellStyleRef, dataTableNumberStyleRef, dataTableCodeStyleRef;
        StyleRef textShadowSampleStyleRef, textGlowSampleStyleRef;
        StyleRef iconVAlignDemoStyleRefs[4][3];   // [IconSide L/R/T/B][top/mid/bot]

        tachyon::VisualColorStyleIndex triangleFillStyle;
        tachyon::VisualColorStyleIndex triangleFillStyle2;
        tachyon::VisualColorStyleIndex quadFillStyle;

        tachyon::ColorStyle triangleFillColorStyle;
        tachyon::ColorStyle quadFillColorStyle;

        ImageIndex checkerboardIndex { ImageIndex::Invalid };
        ImageIndex demoImageIndex { ImageIndex::Invalid };   // sample image for the Image() element demo
        std::vector<uint8_t> demoImagePixels;                 // its RGBA, for "Copy image"
        // The image pasted into the Misc panel's Paste image well (from the clipboard, or a file
        // dropped on it), while there is one.
        tachyon::Image*  pastedImage = nullptr;
        ImageIndex       pastedImageIndex { ImageIndex::Invalid };
        uint2            pastedImageSize {};
        // The Misc panel: its cards' styles, the colors its drawing uses (made again after a
        // theme reload), its sample images, and what's picked in it.
        struct MiscPanel
        {
            StyleRef panel, card, caption, row, label, rowLabel, state, stateLabel, richText, canvas;
            StyleRef thumbTile, thumb, thumbSelected, thumbMissing, thumbLoading, thumbName, thumbNameSelected;
            StyleRef wellColumn, well, wellEmpty, wellDrop;
            StyleRef iconButton[4];   // 20, 24, 28 and 32 square
            StyleRef iconButtonPrimary, iconButtonDanger, iconButtonGroup;

            tachyon::VisualColorStyleIndex ink, accent, canvasFill, ring, pentagonFill, triangleFill;

            // The thumbnails' images (a grey ramp, a hue sweep, a glow fading out) and the
            // Copy image well's picture, with its RGBA for the clipboard.
            ImageIndex           grey { ImageIndex::Invalid }, hue { ImageIndex::Invalid },
                                 glow { ImageIndex::Invalid }, picture { ImageIndex::Invalid };
            std::vector<uint8_t> picturePixels;
            static constexpr uint32_t kPictureWidth = 256, kPictureHeight = 128;
        } misc;
        // A Misc panel's own: the thumbnail picked and the icon buttons' toggles.
        struct MiscView
        {
            int  selectedThumb = 1;
            bool bold = true, italic = false, link = false;   // the group's toggles
            bool liked = false;
        };
        // Work under way, shown where it happens: the build in the Build panel, an import in
        // the Files panel, the shaders compiling in the viewport, the hierarchy loading, and
        // the indexing and the background tasks in the status bar. Times are seconds on the
        // frame clock.
        struct ProgressDemos
        {
            float startedAt       = -1.0f;   // the first frame: what loads at startup runs from here
            bool  held            = false;   // captures: each held at one moment, as the design has it
            float buildStart      = -1.0f;   // -1: not built yet
            float buildCancelledAt = -1.0f;  // seconds into the build it was cancelled; -1: it wasn't
            float importStart     = -1.0f;   // -1: none
            float importCancelledAt = -1.0f; // seconds into the import it was cancelled; -1: it wasn't
            bool  backgroundCancelled[3] = {};
            float uploadDone      = 0.15f;   // the upload: how far, as of when it was last paused
            float uploadResumedAt = -1.0f;   // running since; -1: paused
            static constexpr float kUploadSecs = 12.0f;   // all of it, start to end

            static constexpr float kShadersSecs       = 5.0f;    // the viewport's shaders compile at startup
            static constexpr float kPropsLoadSecs     = 6.0f;    // the hierarchy's Props load at startup
            static constexpr float kTerrainStreamSecs = 10.0f;   // and its Terrain streams in
            static constexpr float kImportSecs        = 6.0f;    // an import in the Files panel
            static constexpr float kImportDoneSecs    = 3.0f;    // "Import complete" shown this long after
        };
        ProgressDemos    progress;
        // How long since `since`, or the moment `held` while captures hold it.
        float            ProgressElapsed(float since, float held);
        bool             ShadersCompiling();
        float            BuildElapsed();   // how far into the build, in seconds; -1 before the first
        bool             BuildRunning();
        /// The work running now, as the status bar's tasks list it: what quitting would stop.
        struct RunningTask { std::string name; std::string state; };   // state: "42%", "paused"
        std::vector<RunningTask> RunningWork();
        float            ImportElapsed();  // how far into the import, in seconds; -1 while there's none
        /// What the progress shown in a cached panel looks like now, for its cache signature.
        uint64_t         ProgressLook();
        // The timeline editor demo: its tracks, set up the first time it's drawn. Each Timeline
        // panel shows them with a view of its own.
        TimelineData     animation;
        bool             animationSeeded = false;
        /// The Curves panels' curves: the animated sphere's, their playhead the timeline's.
        CurveData        animationCurves;
        // The scene the Viewport panels show (each from its own camera): its shapes, the one
        // picked, and what draws them.
        std::vector<SceneObject> sceneObjects;
        int              sceneSelected = -1;
#if TACHYON_VULKAN || TACHYON_D3D12 || TACHYON_SDLGPU
        SceneRenderer    sceneRenderer;
#endif
        // The sketch pad: strokes drawn with the pen (or the mouse), wider where it pressed harder.
        struct SketchPoint  { float2 pos; float width; };
        struct SketchStroke { std::vector<SketchPoint> points; };
        std::vector<SketchStroke> sketchStrokes;

        // ---- Showcase state ----
        // State used by the showcase panels for testing and prototyping GUI features.
        // This will be removed or moved to an examples/ directory once the framework matures.
        struct ShowcaseState
        {
            ID windowLayoutID1 { };
            ID windowLayoutID2 { };
            ID dragGhostWindowID {};

            int testBoxStyleCount = 0;

            float sliderValue = 0.5f;
            float sliderFieldValue = 50.0f; // SliderField demo (range [0, 100])
            tachyon::color4f demoColor { 0.2f, 0.5f, 0.8f, 0.5f }; // ColorField demo, half transparent to show its alpha
            // GradientField demo: the editor's Sky preset, so the editor rings it.
            tachyon::Gradient demoGradient = []
            {
                tachyon::Gradient g;
                g.type = tachyon::GradientType::Linear;
                g.AddStop(tachyon::color4f{ 0x0B / 255.0f, 0x1D / 255.0f, 0x51 / 255.0f, 1.0f }, 0.0f);
                g.AddStop(tachyon::color4f{ 0x3B / 255.0f, 0x82 / 255.0f, 0xF6 / 255.0f, 1.0f }, 0.5f);
                g.AddStop(tachyon::color4f{ 0xA5 / 255.0f, 0xF3 / 255.0f, 0xFC / 255.0f, 1.0f }, 1.0f);
                return g;
            }();
            // GradientField on a GradientF: an HDR heat ramp from see-through, with a step and a
            // smooth stretch.
            tachyon::GradientF demoGradientF = []
            {
                tachyon::GradientF g;
                g.colorSpace = tachyon::ColorSpace::Oklab;
                g.AddStop(tachyon::color4f{ 0.05f, 0.0f, 0.1f, 0.0f }, 0.0f, tachyon::StopInterpolation::Smooth);
                g.AddStop(tachyon::color4f{ 0.9f, 0.2f, 0.05f, 1.0f }, 0.45f, tachyon::StopInterpolation::Linear);
                g.AddStop(tachyon::color4f{ 1.0f, 0.8f, 0.3f, 1.0f }, 0.75f, tachyon::StopInterpolation::Constant);
                g.AddStop(tachyon::color4f{ 3.0f, 2.6f, 2.0f, 1.0f }, 0.9f);   // brighter than white
                return g;
            }();
            // CurveField demos: a falloff, and a color's red, green and blue curves.
            // Each Inspector shows them with views of its own.
            tachyon::CurveData demoCurve;
            tachyon::CurveData demoColorCurves;
            bool               curvesSeeded = false;
            // Plot demo: the demo image's levels, its R, G and B counted into 64 bins.
            tachyon::PlotData  demoLevels;
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
            bool castShadows = true;      // the Inspector's property-help example
            float opacity = 1.8f;         // the Inspector's out-of-range example: 0 to 1

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

            // The NodeView panels' graph; each panel has a view of it of its own (NodeGraphPanel).
            NodeGraph nodeGraph;
            bool      nodeFramesSeeded = false;   // the "Inputs" frame, put round its nodes once they're laid out
            // The StateMachine panels' machines: an enemy's behaviour, and the machine its Combat
            // state holds. Each panel has its views of them, and which it shows (StateMachinePanel).
            NodeGraph enemyMachine;
            NodeGraph combatMachine;
            // What an edit made outside every view (undo, redo) goes through; the views of the
            // graph catch up by its revision.
            NodeViewState noView;
            bool     stateMachinesSeeded = false;
            uint32_t stateMachineSteps = 0;         // Step takes the transitions out of a state in turn
            // When each machine (Enemy, Combat) entered the state it's in, in seconds on the
            // showcase's clock: the time in it shows under that state.
            double   stateEnteredAt[2] = {};
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
            bool exitRequested = false;   // Exit with unsaved documents, or with work running: asking
            ID   exitWindow    = ID::Invalid();   // the window asking
            ID   exitDialogID  = ID::Invalid();
            std::unordered_map<const Document*, bool> exitSave;   // the unsaved documents: saved on Save all, checked
            bool quitDontAsk   = false;   // quitting with work running doesn't ask again
            bool exitForTasks  = false;   // captures: the question about the work running, though documents are unsaved
            // Deleting several hierarchy objects: asking, about these, in that panel.
            ID   deleteDialogID = ID::Invalid();
            std::vector<ID> deleteNodes;
            std::weak_ptr<HierarchyPanel> deletePanel;
            ID   deleteWindow = ID::Invalid();   // the window asking
            std::vector<std::string> deleteNames;
            bool deleteDontAsk = false;   // this session
            // A file the Files panel couldn't open, and the document File > Reload asks about.
            ID   openErrorDialogID = ID::Invalid();
            std::string openErrorFile;
            std::string openErrorDetails;
            ID   openErrorWindow = ID::Invalid();
            ID   reloadDialogID = ID::Invalid();
            Document* reloadDocument = nullptr;
            ID   reloadWindow = ID::Invalid();

            // Values bound by the node properties' renderBody lambdas.
            float       nodeStrength = 0.5f;
            float       nodeGain     = 1.0f;
            int32_t     nodeSeed     = 7;
            std::string nodeName     = "Source";
            bool        nodeEnabled  = true;


            // Icon IDs
            IconID circleIcon = IconID::Invalid;
            IconID heartIcon = IconID::Invalid;   // filled, pink
            IconID starIcon = IconID::Invalid;    // filled, amber
            // The Misc panel's: outlines in the text color (icon buttons, a rating's empty star),
            // Like's pink heart, the picture icon and a small picture for rich text.
            IconID heartOutlineIcon  = IconID::Invalid;
            IconID starOutlineIcon   = IconID::Invalid;
            IconID starDimIcon       = IconID::Invalid;
            IconID likeIcon          = IconID::Invalid;
            IconID boldIcon          = IconID::Invalid;
            IconID italicIcon        = IconID::Invalid;
            IconID imageIcon         = IconID::Invalid;
            IconID inlinePictureIcon = IconID::Invalid;

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
            IconID panelBuildIcon      = IconID::Invalid;

            IconID documentLevelIcon     = IconID::Invalid;
            IconID documentMaterialIcon  = IconID::Invalid;
            IconID documentLayoutIcon    = IconID::Invalid;
            IconID documentPrefabIcon    = IconID::Invalid;
            IconID documentAnimationIcon = IconID::Invalid;
                                       
            IconID objectIcon          = IconID::Invalid;
            IconID object2Icon         = IconID::Invalid;
            // The hierarchy's kinds of object, drawn in the row's icon color.
            IconID sceneIcon           = IconID::Invalid;
            IconID groupIcon           = IconID::Invalid;
            IconID cameraIcon          = IconID::Invalid;
            IconID lightIcon           = IconID::Invalid;
            IconID treeFolderIcon      = IconID::Invalid;   ///< an outlined folder, in the row's icon color
            IconID treeFolderOpenIcon  = IconID::Invalid;
            IconID filesFolderIcon     = IconID::Invalid;   ///< the files tree's folders: outlined, in the folders' amber
            IconID filesFolderOpenIcon = IconID::Invalid;
            IconID layoutsIcon         = IconID::Invalid;   ///< the Layouts dropdown's
            // The Settings pages': Shortcuts, Appearance, General, Advanced, About.
            IconID settingsShortcutsIcon = IconID::Invalid, settingsAppearanceIcon = IconID::Invalid,
                   settingsGeneralIcon   = IconID::Invalid, settingsAdvancedIcon   = IconID::Invalid,
                   settingsAboutIcon     = IconID::Invalid;
                                       
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
			IconID stepIcon            = IconID::Invalid;   // the state machine's Step
			IconID restartIcon         = IconID::Invalid;   // ... and Restart
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
            CommandID cmdNew = {}, cmdOpen = {}, cmdSave = {}, cmdSaveAs = {}, cmdSaveAll = {}, cmdExit = {};
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
            std::vector<ToastID> demoToasts;        // the demo's toasts, taken away when shown again
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

            // Appearance page bound values, kept in the saved UI state
            std::string settingsThemePath;            // the palette loaded
            int     settingsMotionIndex = 0; // 0 = as the OS, 1 = reduced, 2 = full
            int     settingsAccentIndex = 0; // into kAccentPresets; 0 = the palette's own blue
            int     settingsWheelIndex = int(DefaultWheelMode());   // a WheelMode: 0 zooms, 1 scrolls
            int     settingsRedrawIndex = int(RedrawMode::WhenNeeded); // a RedrawMode
            float   settingsFontScalePct = 100.0f; // percent; 100 = unscaled

            // Misc dummy page values
            std::string settingsUserName = "Player One";
            bool        settingsAutosave = true;
            bool        settingsLogToConsole = true;   // Diagnostics::SetLogConsoleOutput; on by default
            int         settingsMaxUndo = 64;
        };
        ShowcaseState showcase;


    public:
        Result<void> Initialize(const InitializeOptions& options = {}) override;

        // Re-registers showcase-owned styles after a theme reload.
        void OnStylesReloaded() override;
        void ApplyNodeCategories();
        /// Each state its own color from the palette, so they follow the theme.
        void ApplyStateColors();

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
        ID   CreateSettingsPanel(std::unordered_map<ID, Panel>& panels, std::string title);
        /// A Settings panel's pages, with static IDs of its own.
        void InitSettingsPages(SettingsView& view);
        void RenderSettingsAppearance(SettingsView& view);
        void RenderSettingsGeneral();
        void RenderSettingsAdvanced();
        void RenderSettingsAbout();
        /// The font scale, as a percentage: 100 to 300 in steps of 10, the theme reloaded at it.
        void SetFontScale(float percent);
        void RenderSettingsShortcuts(SettingsView::Shortcuts& page);
        /// A table's selection mode as a "Select" label and a Row | Cell segmented control.
        void SelectionModeSwitch(TableSelectionMode& mode);
        /// Reduced motion as settingsMotionIndex says.
        void ApplyMotionSetting();
        /// The wheel mode settingsWheelIndex says.
        void ApplyWheelSetting();
        /// The redraw mode settingsRedrawIndex says.
        void ApplyRedrawSetting();
        /// The accent settingsAccentIndex picks: none for the palette's own.
        void ApplyAccentSetting();
        /// The Appearance settings read from the saved UI state, applied (--theme wins).
        void ApplySavedAppearance();

        // Dock-panel body renderers (one per panel_*.cpp), called each frame
        // while the panel is the active tab.
        void RenderReorderablePanel(ReorderablePanel& panel);
        void RenderTimelinePanel(TimelineViewState& view);
        void RenderCurvesPanel(CurveViewState& view);
        void RenderSketchPanel(SketchPanel& panel);
        void EraseSketchAt(float2 at, float radius);
        /// The Appearance page's theme editor: the theme's colors and sizes by category, each
        /// change kept for that theme (settings_theme.cpp).
        void RenderThemeEditor(SettingsView::ThemeEditor& editor);
        /// A theme variable's row: its label, and a field for its type.
        void ThemeVariableRow(const CustomPropertyInfo& info);
        /// The theme picker's row: Dark or Light, and Reset for the changes made to it.
        void RenderThemePicker(SettingsView::ThemeEditor& editor);
        /// The timeline editor demo's tracks, keys, sound and events, and its edits on the undo
        /// history.
        void SeedAnimation();
        /// A Timeline panel's view of the tracks: its first selection, "+" and the rows' menu.
        void SetUpTimelineView(TimelineViewState& view);
        /// The Curves panel's channels: the sphere's position, rotation and scale.
        void SeedAnimationCurves();
        /// The Inspector's curves: a falloff, and a color's channels; their edits on the undo history.
        void SeedCurves();
        /// The Inspector's Levels plot: the demo image's histogram and black, gray and white handles.
        void SeedLevels();
        void RenderHierarchyPanel(HierarchyPanel& panel);
        /// The objects the Hierarchy can add: its search row's "+" and its context menu. Each new
        /// object goes under `parent` (a node's ID), or at the end of the scene when it's invalid,
        /// and is selected.
        void HierarchyAddMenuItems(HierarchyPanel& panel, ID parent = ID::Invalid());
        /// Deletes what's selected in the focused Hierarchy's tree: the Delete key.
        void HierarchyDeleteSelected();
        /// Deletes `nodes` from `panel`'s tree; several, it asks first (unless told not to this session).
        void RequestHierarchyDelete(HierarchyPanel& panel, std::vector<ID> nodes);
        void AskBeforeDeleting(ID window);
        ID   CreateNodeGraphPanel(std::unordered_map<ID, Panel>& panels, std::string title);
        void RenderNodeViewPanel(NodeGraphPanel& panel);
        /// A NodeView panel's view of the graph: its menus, what its selection shows, its shortcuts.
        void SetUpNodeGraphView(NodeViewState& view);
        /// The StateMachine panel: the enemy's machine, or Combat's once opened; Step and Restart.
        ID   CreateStateMachinePanel(std::unordered_map<ID, Panel>& panels, std::string title);
        void RenderStateMachinePanel(StateMachinePanel& panel);
        void SeedStateMachines();
        /// Removes the machine's selected states (in `view`), their transitions with them, and
        /// its selected transitions, as one edit.
        void DeleteSelectedStates(NodeViewRef view);
        /// A reroute (NodeModel::reroute) in `color`, unwired, its dot on the grid point
        /// nearest `at` (node.pos space).
        NodeModel RerouteNode(float2 at, color4f color);
        /// Removes the nodes selected in `view`, their wires with them, and its selected
        /// frames, as one edit.
        void DeleteSelectedNodes(NodeViewState& view);
        /// A frame round the nodes and frames selected in `view`, then selected itself.
        void GroupSelection(NodeViewState& view);
        /// The kinds of node the canvas menu adds.
        enum class GraphNodeKind { Value, Math, Noise, Switch };
        /// Adds a node of `kind` with its top-left at `at` (node.pos space), as one edit.
        void AddGraphNode(NodeViewState& view, GraphNodeKind kind, float2 at);
        /// The NodeView / StateMachine panel whose canvas has the keyboard: what their
        /// shortcuts (F, Home, Delete, Ctrl+G) act on. Null when none has.
        NodeViewState*   FocusedGraphView();
        NodeViewRef      FocusedMachineView();   ///< valid only while FocusedStateMachine() isn't null
        StateMachinePanel* FocusedStateMachine();

        void RenderBoxTestPanel();
        void RenderInspectorPanel(InspectorPanel& panel);
        void RenderMiscPanel(MiscView& view);
        void RenderBuildPanel();
        /// The status bar's progress: the task running now, and the background tasks' pill.
        void RenderStatusProgress();
        /// Over a viewport while its shaders compile: a sliding bar along its top (`width`, the
        /// viewport's last frame) and a pill.
        void RenderViewportBusy(ID viewportID, int32_t width);
        /// The Misc panel's cards, in RenderMiscPanel's order.
        void MiscVectorDrawingCard();
        void MiscIconButtonsCard(MiscView& view);
        void MiscRichTextCard();
        void MiscThumbnailsCard(MiscView& view);
        void MiscClipboardWellsCard();
        void MiscExtraCards();
        /// Puts the Copy image well's picture on the clipboard.
        void CopyDemoImage();
        /// Shows the clipboard's image in the Paste image well, in place of the one pasted before.
        void PasteImage();
        /// Shows an image file's picture (as the shell reads it) in the Paste image well.
        void PasteImageFile(const std::string& path);
        /// Shows `rgba` (straight alpha, rows from the top) in the Paste image well.
        void ShowPastedImage(const uint8_t* rgba, uint2 size);
        void RenderTablePanel(TablePanel& panel);
        void RenderLogPanel(LogPanel& panel);
        void RenderTreeTablePanel(TreeTablePanel& panel);
        void RenderSplitScrollPanel(FrozenTablePanel& panel);
        void RenderViewportPanel(ViewportPanel& panel);
        /// A Viewport panel's toolbars: its file, view, camera and tool items.
        void MakeViewportToolbars(ViewportPanel& panel);
        /// The open Viewport panels' torn-off toolbars, each in its own window.
        void EmitViewportFloatingToolbars();
        /// The scene's shapes, the first time a Viewport panel is made.
        void SeedScene();
        /// Picks scene object `index` (-1: none) for the viewport's gizmo.
        void SelectSceneObject(int index);
        
        void RenderProfilerTreePanel(ProfilerPanel& panel);

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
        void RenderStatusBar();
        /// The frame timings the status bar and the Profiler show, and whether they take new
        /// ones this frame. A readout of the frame time changes with every frame drawn, so it
        /// would ask for the next one forever: unless every frame is drawn anyway, it takes
        /// new numbers at most twice a second, and the frame after sees the same ones.
        std::array<FrameTimingSample, kFrameHistoryCount> frameTimings{};
        bool                                              frameTimingsFresh = true;
        std::chrono::steady_clock::time_point             frameTimingsAt{};
        /// Quits, or asks first (in `from`, else the window with the focus) while a document in
        /// any window has unsaved changes.
        void RequestExit(ID from = ID::Invalid());
        /// While an exit waits, in the window asking: lists the unsaved documents and offers
        /// Save All, Discard or Cancel.
        void AskBeforeExit(ID window);
        void CaptureOpenDialog(std::string_view which, ID window);
        /// File > Reload, and a file the Files panel couldn't open: their questions.
        void AskBeforeReloading(ID window);
        void TellOpenFailed(ID window);
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
        ID CreateViewportPanel(std::unordered_map<ID, Panel>& panels, std::string title);
        ID CreateTablePanel(std::unordered_map<ID, Panel>& panels, std::string title);
        ID CreateFrozenTablePanel(std::unordered_map<ID, Panel>& panels, std::string title);
        ID CreateTreeTablePanel(std::unordered_map<ID, Panel>& panels, std::string title);
        ID CreateSketchPanel(std::unordered_map<ID, Panel>& panels, std::string title);
        ID CreateProfilerPanel(std::unordered_map<ID, Panel>& panels, std::string title);
        ID CreateReorderablePanel(std::unordered_map<ID, Panel>& panels, std::string title);

        void EnableFilesPanel(FilesPanel& panel);
        void DisableFilesPanel(FilesPanel& panel);
        void RenderFilesPanel(FilesPanel& panel);
        void MoveFileInto(FilesPanel& panel, uintptr_t payload, const std::string& folder);
        void ApplyFileRenames(FilesPanel& panel);
        void AddDroppedFiles(FilesPanel& panel, const std::vector<std::string>& paths, const std::string& folder);
        IconID FileIcon(const FilesPanel& panel, std::string_view name, bool isFolder, bool open = false);
        /// The folder tree's folder icon, open or closed.
        IconID TreeFolderIcon(bool open) const;
        IconID FileIcon(const FilesPanel& panel, const FileEntry& file, bool open = false);

        // Build/tear down panel data; wired into CreatePanel's onEnable/onDisable.
        void EnableHierarchyPanel(HierarchyPanel& panel);
        void EnableInspectorPanel(InspectorPanel& panel);
        void DisableInspectorPanel(InspectorPanel& panel);
        void EnableLogPanel(LogPanel& panel);
    private:
        // The Files panels made, to let go of their icons before the text system goes.
        std::vector<std::weak_ptr<FilesPanel>> m_filesPanels;
        // The Viewport panels made, whose torn-off toolbars are emitted each frame.
        std::vector<std::weak_ptr<ViewportPanel>> m_viewportPanels;
        // The NodeView and StateMachine panels made: their shortcuts act on the one with the focus.
        std::vector<std::weak_ptr<NodeGraphPanel>>    m_nodeGraphPanels;
        std::vector<std::weak_ptr<StateMachinePanel>> m_stateMachinePanels;
        std::vector<std::string> m_startPanels;
        std::vector<std::string> m_floatPanels;   // --float: start in their own windows
        std::filesystem::path    m_filesRoot;     // --files
        std::string              m_startTheme;    // --theme
        std::string              m_captureDir;    // --capture
        // --bench: the frames to measure, the frames seen, and each scope's summed times.
        struct BenchState
        {
            struct Scope
            {
                std::string name;
                int32_t     depth = 0;
                bool        isWait = false;
                double      totalMs = 0.0, selfMs = 0.0;
                int64_t     calls = 0;
            };
            int                                  frames = 0;
            int                                  seen = 0;      // frames seen in this phase
            int                                  phase = 0;     // 0: the draw cache on, 1: off
            double                               gpuMs = 0.0;   // summed over the windows and frames
            int                                  gpuFrames = 0;
            std::vector<uint64_t>                order;    // the scopes' keys, as first seen depth-first
            std::unordered_map<uint64_t, Scope>  scopes;
        } m_bench;
        // Called each frame while --bench runs; prints the means and quits at the end.
        void RunBench();
        // Where --capture is: the theme and the shot it's on, and frames since the shot began.
        struct CaptureState
        {
            ID            windowID    = ID::Invalid();
            ID            areaID      = ID::Invalid();  // the area the panel is in
            ID            scrollID    = ID::Invalid();  // the area paged through: the panel's that hides the most
            DockingLayout layout;                       // holds the panel being captured
            ID            panelID     = ID::Invalid();
            size_t        theme       = 0;
            size_t        loadedTheme = size_t(-1);
            size_t        shot        = 0;
            int           frames      = 0;
            int           page        = 0;          // a long panel is captured a page at a time
            int           pageScroll  = 0;          // where the last page was scrolled to
            bool          targetFound = false;      // the element the shot acts on, after layout
            IntRect       target {};                // its rect, in the window
            ID            targetID    = ID::Invalid();
            ID            targetWindowID = ID::Invalid();   // the window it's in: the capture's, or one opened in it
            // A document window's documents, its own, made on first use.
            DocumentSet   documents;
            ID            documentsID = ID::Invalid();
            // The States board's text fields and dropdowns, one per cell.
            std::vector<std::string> stateTexts;
            std::vector<int>         stateChoices;
            int           closing     = 0;          // frames left closing what the last shot opened
            std::chrono::steady_clock::time_point themeAt {};   // when the theme was last loaded
            std::chrono::steady_clock::time_point actAt {};     // when the shot's action began
        };
        CaptureState             m_capture;
        void RenderCapture();
        void CaptureFindTarget();
        void CaptureStatesBoard();
        void CaptureDocumentWindow();
        ID   CaptureScrollingArea(ID window) const;
        // The capture window with the windows opened in it drawn over it, as on screen.
        bool CaptureReadPixels(IntRect& rect, std::vector<uint8_t>& bgra);
    };

} // namespace tachyon
