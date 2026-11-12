#include "application.h"

#include <gui_core/diagnostics/gui_logging.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

//
// --capture <folder>: draws each showcase panel, the main window, and the menus, tooltips,
// pickers, toasts and dialogs opened in them, in a window of its own that renders to an image
// (no OS window, nothing on screen), in the dark theme and then the light one, and saves each
// as <folder>/<theme>/<Shot>.png. Then the app quits. For checking the UI against its design.
//

namespace tachyon
{
    namespace
    {
        constexpr Diagnostics::LogCategory kLogCapture = Diagnostics::MakeLogCategory("capture");

        // What a shot shows, and what's done in it before it's read.
        enum class CaptureScene : uint8_t
        {
            Panel,      // a showcase panel
            Main,       // the main window, as the showcase has it
            Document,   // a second document window, with documents of its own
            States,     // the controls in each of their states, side by side
        };
        enum class CaptureAct : uint8_t
        {
            None,
            Hover,        // the pointer over the target
            Click,        // the target clicked
            RightClick,   // the target's context menu opened, as a right-click opens it
            Toasts,       // the demo toasts shown
            ExitDialog,   // the question asked on exit with unsaved documents
            Settle,       // nothing done; waits for the state transitions to end
            Float,        // a panel torn off into a window of its own, docked again after
        };
        // What's acted on: the element with this text, or with one of these looks.
        enum class CaptureTarget : uint8_t { Text, ColorSwatch, GradientSwatch, TasksPill, DockButton };
        struct CaptureShot
        {
            const char*   name;
            CaptureScene  scene  = CaptureScene::Panel;
            const char*   panel  = nullptr;
            CaptureAct    act    = CaptureAct::None;
            CaptureTarget target = CaptureTarget::Text;
            const char*   text   = nullptr;
        };

        // What's captured, in order. A panel's first frames lay it out, measure its text and
        // load its images before it's read.
        constexpr CaptureShot kCaptureShots[] = {
            { "Viewport",     CaptureScene::Panel, "Viewport" },
            { "NodeView",     CaptureScene::Panel, "NodeView" },
            { "StateMachine", CaptureScene::Panel, "StateMachine" },
            { "Inspector",    CaptureScene::Panel, "Inspector" },
            { "Hierarchy",    CaptureScene::Panel, "Hierarchy" },
            { "Files",        CaptureScene::Panel, "Files" },
            { "Log",          CaptureScene::Panel, "Log" },
            { "Profiler",     CaptureScene::Panel, "Profiler" },
            { "Settings",     CaptureScene::Panel, "Settings" },
            { "Timeline",     CaptureScene::Panel, "Timeline" },
            { "Curves",       CaptureScene::Panel, "Curves" },
            { "Table",        CaptureScene::Panel, "Table" },
            { "TreeTable",    CaptureScene::Panel, "TreeTable" },
            { "FrozenTable",  CaptureScene::Panel, "FrozenTable" },
            { "Reorderable",  CaptureScene::Panel, "Reorderable" },
            { "Misc",         CaptureScene::Panel, "Misc" },
            { "BoxTest",      CaptureScene::Panel, "BoxTest" },
            { "Sketch",       CaptureScene::Panel, "Sketch" },
            { "Tooltip",        CaptureScene::Panel, "Inspector", CaptureAct::Hover,      CaptureTarget::Text, "Click me" },
            { "ContextMenu",    CaptureScene::Panel, "Inspector", CaptureAct::RightClick, CaptureTarget::Text, "Click me" },
            { "ColorPicker",    CaptureScene::Panel, "Inspector", CaptureAct::Click,      CaptureTarget::ColorSwatch },
            { "GradientEditor", CaptureScene::Panel, "Inspector", CaptureAct::Click,      CaptureTarget::GradientSwatch },
            { "TasksPopover",   CaptureScene::Panel, "Misc",      CaptureAct::Click,      CaptureTarget::TasksPill },
            { "Main",       CaptureScene::Main },
            { "MenuFile",   CaptureScene::Main, nullptr, CaptureAct::Click, CaptureTarget::Text, "File" },
            { "MenuEdit",   CaptureScene::Main, nullptr, CaptureAct::Click, CaptureTarget::Text, "Edit" },
            { "MessageBox", CaptureScene::Main, nullptr, CaptureAct::ExitDialog },
            { "Toasts",     CaptureScene::Main, nullptr, CaptureAct::Toasts },
            { "States",         CaptureScene::States,   nullptr, CaptureAct::Settle },
            { "DocumentWindow", CaptureScene::Document },
            { "FloatingPanel",  CaptureScene::Document, nullptr, CaptureAct::Float },
        };
        constexpr struct { const char* name; const char* path; } kCaptureThemes[] = {
            { "dark", "themes/dark.css" }, { "light", "themes/light.css" },
        };
        constexpr int  kFramesPerPanel = 12;
        constexpr int  kFramesPerPage  = 4;     // a page further down: only scrolled
        constexpr int  kMaxPages       = 4;
        constexpr int  kFramesPerTheme = 20;
        // After a shot that opened something: found, pointed at, pressed, released, left.
        constexpr int  kFramesClosing  = 5;
        constexpr int  kFramesActing   = 3;     // the pointer moved over the target, pressed, released
        // A theme switch cross-fades from the old look for a while (real time, not frames):
        // nothing is read until it's over.
        constexpr std::chrono::milliseconds kThemeFade { 800 };
        // Tooltips wait before they show, and popups and toasts slide in.
        constexpr std::chrono::milliseconds kActWait { 1500 };
        constexpr int2 kCaptureSize     { 1280, 800 };
        constexpr int2 kCaptureMainSize { 1600, 900 };
        constexpr int2 kCaptureDocumentSize { 1200, 760 };   // as the showcase opens one

        // The States board: its rows and columns, as the design's.
        constexpr const char* kStateRows[] = {
            "Button", "Primary button", "Icon button", "Text field", "Number field", "Dropdown", "Toggle", "Checkbox",
        };
        constexpr const char* kStateColumns[] = { "Rest", "Hover", "Pressed", "Focus", "Selected / on", "Disabled", "Invalid" };

        bool Points(CaptureAct act) { return act == CaptureAct::Hover || act == CaptureAct::Click || act == CaptureAct::RightClick; }

        // A PNG of `bgra` pixels (top row first), uncompressed: the zlib stream holds stored
        // blocks, so it needs no compressor.
        bool WritePng(const std::filesystem::path& file, int2 size, std::span<const uint8_t> bgra)
        {
            static const std::array<uint32_t, 256> crcTable = [] {
                std::array<uint32_t, 256> t {};
                for (uint32_t n = 0; n < 256; ++n)
                {
                    uint32_t c = n;
                    for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                    t[n] = c;
                }
                return t;
            }();
            auto crc = [&](const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFFu) {
                for (size_t i = 0; i < n; ++i) c = crcTable[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
                return c;
            };
            auto be32 = [](std::vector<uint8_t>& out, uint32_t v) {
                out.push_back(uint8_t(v >> 24)); out.push_back(uint8_t(v >> 16)); out.push_back(uint8_t(v >> 8)); out.push_back(uint8_t(v));
            };
            std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
            auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
                be32(png, uint32_t(data.size()));
                const size_t start = png.size();
                png.insert(png.end(), type, type + 4);
                png.insert(png.end(), data.begin(), data.end());
                be32(png, ~crc(png.data() + start, png.size() - start));
            };

            std::vector<uint8_t> header;
            be32(header, uint32_t(size.x));
            be32(header, uint32_t(size.y));
            header.insert(header.end(), { 8, 6, 0, 0, 0 });   // 8 bits, RGBA, deflate, no filter method, no interlace
            chunk("IHDR", header);

            // The rows, each after its filter byte (none), RGBA.
            std::vector<uint8_t> raw;
            raw.reserve(size_t(size.y) * (size_t(size.x) * 4 + 1));
            for (int y = 0; y < size.y; ++y)
            {
                raw.push_back(0);
                const uint8_t* row = bgra.data() + size_t(y) * size_t(size.x) * 4;
                for (int x = 0; x < size.x; ++x)
                {
                    const uint8_t* p = row + size_t(x) * 4;
                    raw.insert(raw.end(), { p[2], p[1], p[0], p[3] });
                }
            }
            std::vector<uint8_t> zlib = { 0x78, 0x01 };
            uint32_t a = 1, b = 0;
            for (const uint8_t v : raw) { a = (a + v) % 65521u; b = (b + a) % 65521u; }
            for (size_t at = 0; at < raw.size() || at == 0; )
            {
                const size_t n = std::min<size_t>(65535, raw.size() - at);
                const bool last = at + n >= raw.size();
                zlib.push_back(last ? 1 : 0);
                zlib.push_back(uint8_t(n)); zlib.push_back(uint8_t(n >> 8));
                zlib.push_back(uint8_t(~n)); zlib.push_back(uint8_t(~n >> 8));
                zlib.insert(zlib.end(), raw.begin() + ptrdiff_t(at), raw.begin() + ptrdiff_t(at + n));
                at += n;
                if (last) break;
            }
            be32(zlib, (b << 16) | a);
            chunk("IDAT", zlib);
            chunk("IEND", {});

            std::ofstream out(file, std::ios::binary);
            out.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
            return bool(out);
        }
    }

    ID Application::CaptureScrollingArea(ID window) const
    {
        // Of the areas in the window that scroll and hold more than they show, the one hiding
        // the most: a panel's own list or form, where it has one.
        const ElementRef windowRef = windowRegistry.GetWindowRef(window);
        ID best = ID::Invalid();
        int32_t most = 0;
        for (uint32_t d = 0; d < elementStorage.NumberOfDepthLevels(); ++d)
            for (uint32_t i = 0; i < elementStorage.DepthSizeOf(d); ++i)
            {
                const ElementRef ref{ d, i };
                if (!elementStorage.IsValidRef(ref)) continue;
                const ElementRect& rect = elementStorage.GetElementRectUnsafe(ref);
                if (rect.layoutStyle == nullptr || rect.layoutStyle->overflowY != Overflow::Auto) continue;
                const int32_t hidden = rect.contentBounds.h - rect.innerRect.h;
                if (hidden <= most) continue;
                ElementRef up = ref;
                while (up.IsValid() && !(up == windowRef))
                    up = elementStorage.GetParentRef(up);
                if (!up.IsValid()) continue;
                most = hidden;
                best = elementStorage.GetElementUnsafe(ref).id;
            }
        return best;
    }

    void Application::CaptureFindTarget()
    {
        // The first element the shot acts on, in the capture window or a window opened in it
        // (a floating panel's Dock button), and the window it's in.
        CaptureState& c = m_capture;
        const CaptureShot& shot = kCaptureShots[c.shot];
        const CaptureTarget target = c.closing > 0 && shot.act == CaptureAct::Float ? CaptureTarget::DockButton : shot.target;
        const uint32_t self = windowRegistry.GetWindowIndex(c.windowID);
        auto inCapture = [&](ID window) {
            for (uint32_t up = windowRegistry.GetWindowIndex(window); up != WindowRegistry::kInvalidIndex;
                 up = windowRegistry.Windows()[up].parentWindowEntry)
                if (up == self)
                    return true;
            return false;
        };
        c.targetFound = false;
        for (uint32_t d = 0; d < elementStorage.NumberOfDepthLevels() && !c.targetFound; ++d)
            for (uint32_t i = 0; i < elementStorage.DepthSizeOf(d) && !c.targetFound; ++i)
            {
                const ElementRef ref{ d, i };
                if (!elementStorage.IsValidRef(ref)) continue;
                const Element& element = elementStorage.GetElementUnsafe(ref);
                bool match = false;
                switch (target)
                {
                    case CaptureTarget::ColorSwatch:    match = element.styleRef == theme.colorSwatchStyleRef; break;
                    case CaptureTarget::GradientSwatch: match = element.styleRef == theme.gradientSwatchStyleRef; break;
                    case CaptureTarget::TasksPill:      match = element.styleRef == theme.tasksPillStyleRef; break;
                    case CaptureTarget::DockButton:
                    case CaptureTarget::Text:
                    {
                        const char* text = target == CaptureTarget::DockButton ? "Dock" : shot.text;
                        match = text != nullptr && element.textID != TextID::Invalid && textSystem->GetText(element.textID) == text;
                        break;
                    }
                }
                if (!match) continue;
                // The window it's in.
                ElementRef up = ref;
                while (up.IsValid() && (elementStorage.GetElementUnsafe(up).flags & ElementFlags::IsWindow) == ElementFlags::None)
                    up = elementStorage.GetParentRef(up);
                if (!up.IsValid() || !inCapture(elementStorage.GetElementUnsafe(up).id)) continue;
                // In the window's pixels, as input is injected: element rects are on the screen.
                c.target = elementStorage.GetElementRectUnsafe(ref).rect;
                c.target.position = c.target.position - elementStorage.GetElementRectUnsafe(up).rect.position;
                c.targetID = element.id;
                c.targetWindowID = elementStorage.GetElementUnsafe(up).id;
                c.targetFound = true;
            }
    }

    void Application::CaptureStatesBoard()
    {
        // A row per control, a column per state: each cell the control as it looks in that
        // state, shown as the design's States board does.
        CaptureState& c = m_capture;
        const size_t rows = std::size(kStateRows), columns = std::size(kStateColumns);
        c.stateTexts.resize(rows * columns);
        c.stateChoices.resize(rows * columns);
        guiStateManager.Interaction().SetFocusVisible(true);   // as after Tab: the focus ring shows

        const int32_t labelWidth = styleManager.Px(150), cellWidth = styleManager.Px(150), rowGap = styleManager.Px(20);
        const ID boardID = idManager.AllocateDynamicID("states_board");
        BeginVertical(boardID, theme.emptyVertStyleRef, LayoutOverride{}.FlexGrow(1));
        [[maybe_unused]] const bool pushed = idManager.PushParent(boardID);

        BeginHorizontal(idManager.AllocateDynamicID("head"), theme.emptyHorzStyleRef, LayoutOverride{});
        Text(idManager.AllocateDynamicID("corner"), "", LayoutOverride{}.Width(Pixels{ labelWidth }));
        const ID headRange = idManager.AllocateRangedID(uint32_t(columns), "head_cell");
        for (size_t col = 0; col < columns; ++col)
            Text(idManager.GetSubID(headRange, uint32_t(col)), kStateColumns[col], LayoutOverride{}.Width(Pixels{ cellWidth }));
        EndHorizontal();

        const ID cellRange = idManager.AllocateRangedID(uint32_t(rows * columns), "state_cell");
        const ID rowRange  = idManager.AllocateRangedID(uint32_t(rows), "state_row");
        for (size_t row = 0; row < rows; ++row)
        {
            const std::string_view kind = kStateRows[row];
            // Rows apart, each as high as its tallest control: a control keeps its own height.
            Text(idManager.AllocateDynamicID(std::format("gap{}", row)), "", LayoutOverride{}.Height(Pixels{ rowGap }));
            BeginHorizontal(idManager.GetSubID(rowRange, uint32_t(row)), theme.emptyHorzStyleRef, LayoutOverride{});
            Text(idManager.AllocateDynamicID(std::format("label{}", row)), kind, LayoutOverride{}.Width(Pixels{ labelWidth }));
            for (size_t col = 0; col < columns; ++col)
            {
                const size_t cell = row * columns + col;
                const std::string_view state = kStateColumns[col];
                const bool on = state == "Selected / on" || state == "Disabled";
                const ID cellID = idManager.GetSubID(cellRange, uint32_t(cell));
                BeginHorizontal(cellID, theme.emptyHorzStyleRef, LayoutOverride{}.Width(Pixels{ cellWidth }));
                [[maybe_unused]] const bool inCell = idManager.PushParent(cellID);
                // Selected, disabled and invalid as the app has them; hover, pressed and
                // focus shown.
                if (state == "Hover")         BeginShownState(ElementState::Hover);
                if (state == "Pressed")       BeginShownState(ElementState::Hover | ElementState::Active);
                if (state == "Focus")         BeginShownState(ElementState::Focused);
                if (state == "Selected / on") BeginSelected();
                if (state == "Disabled")      BeginDisabled();
                if (state == "Invalid")       BeginInvalid();

                const ID id = idManager.AllocateDynamicID("control");
                std::string& text = c.stateTexts[cell];
                if (kind == "Button")
                    Button(id, "Apply", [] {}, theme.controls->button, LayoutOverride{});
                else if (kind == "Primary button")
                    Button(id, "Save", [] {}, theme.dialogPrimaryButtonStyleRef, LayoutOverride{});
                else if (kind == "Icon button")
                    Button(id, GUIContent{ theme.deleteIcon }, [] {}, theme.iconButtonStyleRef, LayoutOverride{});
                else if (kind == "Text field")
                {
                    if (text.empty()) text = state == "Invalid" ? "1.8" : "0.75";
                    Field(id, text, TextEditBehavior::None, LayoutOverride{}.Width(Pixels{ cellWidth - styleManager.Px(16) }));
                }
                else if (kind == "Number field")
                {
                    if (text.empty()) text = state == "Invalid" ? "1.8" : "0.75";
                    Field(id, text, TextEditBehavior::None, theme.controls->number, LayoutOverride{}.Width(Pixels{ cellWidth - styleManager.Px(16) }));
                }
                else if (kind == "Dropdown")
                    Dropdown(id, c.stateChoices[cell], { "Linear", "Smooth", "Constant" }, LayoutOverride{}.Width(Pixels{ cellWidth - styleManager.Px(16) }));
                else if (kind == "Toggle")
                    Switch(id, on, "", [](bool) {}, LayoutOverride{});
                else if (kind == "Checkbox")
                    Checkbox(id, on, "Visible", [](bool) {}, LayoutOverride{});
                if (state == "Invalid")       EndInvalid();
                if (state == "Disabled")      EndDisabled();
                if (state == "Selected / on") EndSelected();
                if (state == "Hover" || state == "Pressed" || state == "Focus") EndShownState();
                if (inCell) (void)idManager.PopParent();
                EndHorizontal();

            }
            EndHorizontal();
        }
        if (pushed) (void)idManager.PopParent();
        EndVertical();
    }

    void Application::CaptureDocumentWindow()
    {
        // A second main window, as a document dragged out of the first makes: its own
        // documents, menus without the command search, its status bar, its floating panels.
        CaptureState& c = m_capture;
        if (c.documentsID == ID::Invalid())
        {
            c.documentsID = idManager.CreateStaticID();
            SetUpDocuments(c.documents);
            CreateShowcaseDocument(c.documents, "Rock", ".prefab", "prefab");
            CreateShowcaseDocument(c.documents, "Walk", ".anim", "animation");
            c.documents.active = 0;
        }
        RenderMainMenu(c.windowID, c.documentsID, c.documents, /*withSearch*/ false);
        DocumentPanels(c.documents);
        RenderStatusBar(c.documents);
        EmitFloatingDockWindows(c.documents);
    }

    bool Application::CaptureReadPixels(IntRect& rect, std::vector<uint8_t>& bgra)
    {
        const ID windowID = m_capture.windowID;
        rect = windowManager->GetWindowRect(windowID);
        if (rect.w <= 0 || rect.h <= 0)
            return false;
        bgra.assign(size_t(rect.w) * size_t(rect.h) * 4, 0);
        if (!ReadWindowPixels(windowID, bgra))
            return false;

        // The windows opened in it (WindowImagePopups), in the order they were declared, over
        // it where they are on its screen. Their pixels are premultiplied.
        const auto& windows = windowRegistry.Windows();
        const uint32_t self = windowRegistry.GetWindowIndex(windowID);
        std::vector<uint8_t> popup;
        for (uint32_t w = 0; w < uint32_t(windows.size()); ++w)
        {
            bool inside = false;
            for (uint32_t up = windows[w].parentWindowEntry; up != WindowRegistry::kInvalidIndex && !inside; up = windows[up].parentWindowEntry)
                inside = up == self;
            if (!inside) continue;
            const IntRect r = windowManager->GetWindowRect(windows[w].id);
            if (r.w <= 0 || r.h <= 0) continue;
            popup.assign(size_t(r.w) * size_t(r.h) * 4, 0);
            if (!ReadWindowPixels(windows[w].id, popup)) continue;
            for (int y = 0; y < r.h; ++y)
            {
                const int dy = r.y - rect.y + y;
                if (dy < 0 || dy >= rect.h) continue;
                for (int x = 0; x < r.w; ++x)
                {
                    const int dx = r.x - rect.x + x;
                    if (dx < 0 || dx >= rect.w) continue;
                    const uint8_t* src = popup.data() + (size_t(y) * size_t(r.w) + size_t(x)) * 4;
                    uint8_t* dst = bgra.data() + (size_t(dy) * size_t(rect.w) + size_t(dx)) * 4;
                    const uint32_t keep = 255u - src[3];
                    for (int k = 0; k < 4; ++k)
                        dst[k] = uint8_t(std::min(255u, uint32_t(src[k]) + (uint32_t(dst[k]) * keep + 127u) / 255u));
                }
            }
        }
        return true;
    }

    void Application::RenderCapture()
    {
        CaptureState& c = m_capture;
        if (c.windowID == ID::Invalid())
        {
            c.windowID = idManager.CreateStaticID();
            c.areaID   = idManager.CreateStaticID();
            SetRedrawMode(RedrawMode::Always);   // frames keep coming while nothing moves
        }
        const size_t shotCount = std::size(kCaptureShots), themeCount = std::size(kCaptureThemes);
        if (c.theme >= themeCount)
        {
            applicationQuit = true;
            return;
        }
        const CaptureShot& shot = kCaptureShots[c.shot];
        const auto now = std::chrono::steady_clock::now();

        // A new theme: loaded, and given time to take.
        if (c.loadedTheme != c.theme)
        {
            RequestReloadTheme(kCaptureThemes[c.theme].path, showcase.settingsFontScalePct / 100.0f);
            c.loadedTheme = c.theme;
            c.frames = -kFramesPerTheme;
            c.themeAt = now;
        }
        // A new panel: made as the docking makes it, and enabled. The main window's
        // documents, as the showcase makes them.
        if (shot.scene == CaptureScene::Panel && c.panelID == ID::Invalid())
        {
            c.panelID = CreateShowcasePanel(c.layout, shot.panel);
            if (Panel& panel = c.layout.panels[c.panelID]; panel.onEnable)
                panel.onEnable();
        }
        if (shot.scene == CaptureScene::Main && !showcase.dockInitialized)
        {
            MakeShowcaseDocuments();
            showcase.dockInitialized = true;
        }
        if (c.frames == 0 && c.closing == 0)
        {
            c.actAt = now;
            if (shot.act == CaptureAct::Toasts)
                ShowDemoToasts();
            if (shot.act == CaptureAct::ExitDialog)
            {
                showcase.exitRequested = true;
                showcase.exitWindow    = c.windowID;
            }
            if (shot.act == CaptureAct::Float)
                if (Document* document = c.documents.Active())
                    for (const auto& [panelID, panel] : document->layout.panels)
                        if (panel.title != "Viewport")
                        {
                            const int2 at = windowManager->GetWindowRect(c.windowID).position + int2{ styleManager.Px(700), styleManager.Px(160) };
                            if (!FloatPanel(document->layout, panelID, at))
                                TACHYON_LOG_ERROR(kLogCapture, "{}: couldn't float {}", shot.name, panel.title);
                            break;
                        }
        }

        // Read: what the shot opened is closed, a level at a time, before what opened it is
        // built and takes the key.
        if (c.closing > 1 && shot.act == CaptureAct::Float)
        {
            if (c.targetFound)
            {
                const int2 at { c.target.x + c.target.w / 2, c.target.y + c.target.h / 2 };
                if (c.closing == kFramesClosing - 1)
                    InjectMouseMove(c.targetWindowID, at);
                else if (c.closing < kFramesClosing - 1)
                    InjectMouseButton(c.targetWindowID, at, MouseButtons::Left, c.closing == kFramesClosing - 2);
            }
            else if (c.closing == kFramesClosing - 1)
                TACHYON_LOG_ERROR(kLogCapture, "{}: no Dock button to dock it again", shot.name);
        }
        else if (c.closing > 1)
        {
            InjectKey(c.windowID, KeyCode::Escape, KeyAction::KeyDown);
            InjectKey(c.windowID, KeyCode::Escape, KeyAction::KeyUp);
        }

        // The main window's style: a column that stretches its panel, as a dock does. The
        // windows opened in it render to images too, read with it.
        const int2 size = shot.scene == CaptureScene::Main     ? kCaptureMainSize
                        : shot.scene == CaptureScene::Document ? kCaptureDocumentSize : kCaptureSize;
        ElementFlags flags = ElementFlags::WindowRenderToImage | ElementFlags::WindowImagePopups;
        if (shot.scene == CaptureScene::Main || shot.scene == CaptureScene::Document)
            flags |= ElementFlags::WindowCustomTitleBar;
        if (BeginWindow(c.windowID, "Capture", theme.mainWindowStyleRef, LayoutOverride{}.Size(size.x, size.y), flags))
        {
            // Once it's laid out: the area to page through, for the first page, and what the
            // shot acts on.
            VERIFY(elementStorage.AddPostLayout(elementStorage.GetLastAddedRef(), [this](PostLayoutContext&) {
                if (m_capture.page == 0)
                    m_capture.scrollID = CaptureScrollingArea(m_capture.windowID);
                const CaptureAct act = kCaptureShots[m_capture.shot].act;
                if (Points(act) || (act == CaptureAct::Float && m_capture.closing > 0))
                    CaptureFindTarget();
            }));
            if (shot.scene == CaptureScene::Main)
            {
                // As the showcase's main window has it.
                RenderMainMenu(c.windowID, showcase.documentsID, showcase.documents);
                AskBeforeExit(c.windowID);
                DocumentPanels(showcase.documents);
                RenderStatusBar(showcase.documents);
                if (shot.act == CaptureAct::Toasts)
                {
                    UpdateDemoToasts();
                    Toasts();
                }
            }
            else if (shot.scene == CaptureScene::Document)
                CaptureDocumentWindow();
            else if (BeginArea(c.areaID, theme.dockContentStyleRef, LayoutOverride{}.FlexGrow(1)))
            {
                if (shot.scene == CaptureScene::States)
                    CaptureStatesBoard();
                else if (const Panel& panel = c.layout.panels[c.panelID]; panel.onPanelGUI)
                    panel.onPanelGUI();
                EndArea();
            }
        }
        EndWindow();

        // The next shot: what this one made goes.
        auto nextShot = [&] {
            // A panel docked again is the tab shown where it lands: the first tabs, as before.
            if (shot.act == CaptureAct::Float)
                if (Document* document = c.documents.Active())
                {
                    auto first = [](auto& self, DockNode* node) -> void {
                        if (node == nullptr) return;
                        node->selectedTab = 0;
                        self(self, node->first.get());
                        self(self, node->second.get());
                    };
                    first(first, document->layout.root.get());
                }
            c.page = 0;
            c.pageScroll = 0;
            c.targetFound = false;
            showcase.exitRequested = false;
            if (c.panelID != ID::Invalid())
            {
                if (Panel& panel = c.layout.panels[c.panelID]; panel.onDisable)
                    panel.onDisable();
                c.layout.panels.erase(c.panelID);
                c.panelID = ID::Invalid();
            }
            c.frames = 0;
            if (++c.shot >= shotCount)
            {
                c.shot = 0;
                ++c.theme;
            }
        };

        // Closed: on to the next shot.
        if (c.closing > 0)
        {
            if (--c.closing == 0)
            {
                InjectMouseLeave(c.targetFound && c.targetWindowID != ID::Invalid() ? c.targetWindowID : c.windowID);
                InjectMouseLeave(c.windowID);
                nextShot();
            }
            return;
        }

        // The shot's action, on its target once it's laid out and in view.
        const bool points = Points(shot.act);
        if (points && c.frames >= kFramesPerPanel && c.frames < kFramesPerPanel + kFramesActing)
        {
            if (!c.targetFound)
            {
                TACHYON_LOG_ERROR(kLogCapture, "{}: nothing to act on", shot.name);
                c.frames = kFramesPerPanel + kFramesActing;
            }
            else if (c.frames == kFramesPerPanel && c.target.y + c.target.h > size.y - styleManager.Px(320) && c.scrollID != ID::Invalid())
            {
                // Out of view, or with no room under it for what it opens: scrolled up to
                // near the top, and found again.
                auto& scrolling = guiStateManager.GetScrollManagement();
                const auto handle = scrolling.GetOrCreateScrollOffsetHandle(c.scrollID);
                const int2 offset = scrolling.GetScrollOffset(handle);
                scrolling.SetScrollOffset(handle, { offset.x, offset.y - (c.target.y - styleManager.Px(120)) });
                c.frames = kFramesPerPanel - kFramesPerPage;
            }
            else
            {
                const int2 at { c.target.x + c.target.w / 2, c.target.y + c.target.h / 2 };
                const int step = c.frames - kFramesPerPanel;
                if (step == 0)
                {
                    InjectMouseMove(c.targetWindowID, at);
                    c.actAt = now;
                }
                else if (shot.act == CaptureAct::Click)
                    InjectMouseButton(c.targetWindowID, at, MouseButtons::Left, step == 1);
                else if (shot.act == CaptureAct::RightClick && step == 1)
                    OpenContextMenu(c.targetID, windowManager->GetWindowRect(c.windowID).position + at);
            }
        }

        if (++c.frames < kFramesPerPanel + (points ? kFramesActing : 0) || now - c.themeAt < kThemeFade
            || (shot.act != CaptureAct::None && now - c.actAt < kActWait))
            return;

        // Read back and saved; on to the next page, shot, and theme.
        const std::filesystem::path folder = std::filesystem::path(m_captureDir) / kCaptureThemes[c.theme].name;
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        auto& scrolling = guiStateManager.GetScrollManagement();
        const auto handle = scrolling.GetOrCreateScrollOffsetHandle(c.scrollID != ID::Invalid() ? c.scrollID : c.areaID);
        const int scrolled = scrolling.GetScrollOffset(handle).y;
        // Only a panel left as it is is paged through. Scrolled no further than the last
        // page: the panel ends there.
        const bool pages = shot.scene == CaptureScene::Panel && shot.act == CaptureAct::None;
        const bool ended = c.page > 0 && (!pages || scrolled == c.pageScroll);
        const std::filesystem::path file = folder / (c.page == 0 ? std::format("{}.png", shot.name)
                                                                 : std::format("{}-{}.png", shot.name, c.page + 1));
        if (!ended && c.page < kMaxPages)
        {
            IntRect rect {};
            std::vector<uint8_t> pixels;
            if (CaptureReadPixels(rect, pixels) && WritePng(file, { rect.w, rect.h }, pixels))
                TACHYON_LOG_INFO(kLogCapture, "Saved {}", file.string());
            else
                TACHYON_LOG_ERROR(kLogCapture, "Couldn't capture {}", file.string());
            c.pageScroll = scrolled;
            ++c.page;
            if (pages)
            {
                // The next page: a window's height down, less a little to see where it joins.
                scrolling.SetScrollOffset(handle, { 0, scrolled - (rect.h - styleManager.Px(80)) });
                c.frames = kFramesPerPanel - kFramesPerPage;
                return;
            }
        }
        scrolling.SetScrollOffset(handle, { 0, 0 });
        if (shot.act != CaptureAct::None)
            c.closing = kFramesClosing;
        else
            nextShot();
    }
}
