#include "application.h"

#include <gui_controls/controls_toolbar_dock.h>
#include <gui_controls/gui_view_state.h>
#include <gui_core/gui_hash.h>
#include <gui_core/gui_json.h>

#include <bit>
#if TACHYON_VULKAN
#include <gui_core/graphics/vulkan/vk_graphics_context.h>
#endif
#if TACHYON_D3D12
#include <gui_core/graphics/d3d12/d3d12_graphics_context.h>
#endif
#if TACHYON_SDLGPU
#include <gui_core/graphics/sdlgpu/sdlgpu_graphics_context.h>
#endif

//
// Viewport panel: the scene through a camera of the panel's own, framed by a toolbar dock
// of its own (tachyon::RenderToolbarDock). The scene and which shape is picked are the
// app's; how each panel looks at it, its tool and its toolbars are the panel's.
//

namespace tachyon
{
    // A Viewport panel's own: the view onto the scene, its toolbars and their torn-off
    // windows, and the view settings its toolbars show.
    struct ViewportPanel
    {
        Application* app = nullptr;   // its IDs come from it, and go back to it
        ViewportView view;
        ViewportTransform shownSelection;   // the picked shape's transform as this view last had it

        ViewMode viewMode    = ViewMode::Default;
        bool     showGrid    = true;
        bool     enableAudio = true;
        int      cameraView  = 0;   // the camera menu's: perspective, orthographic, ...
        ID       cameraMenuID = ID::Invalid();

        ToolbarDockState                 dockState {};
        std::vector<ToolbarItem>         sides[kToolbarSideCount];
        ToolbarDockCallbacks             callbacks;
        std::vector<FloatingToolbarItem> floatingItems;   // torn off, each in its own OS window
        FloatingToolbarOptions           floatingOptions;
        int32_t                          width = 0;       // the view's last frame, for the busy bar

        ~ViewportPanel()
        {
            if (!app)
                return;
            IDManager& ids = app->idManager;
            for (const std::vector<ToolbarItem>& side : sides)
                for (const ToolbarItem& item : side)
                    ids.ReleaseStaticID(item.id);
            for (const FloatingToolbarItem& floating : floatingItems)
            {
                ids.ReleaseStaticID(floating.item.id);
                ids.ReleaseStaticID(floating.windowID);
            }
            ids.ReleaseStaticID(cameraMenuID);
            ReleaseToolbarDockState(dockState, *app);
        }
    };

    namespace
    {
        constexpr const char* kCameraViews[] = { "Perspective", "Orthographic", "Top", "Front", "Side" };
    }

    void Application::SeedScene()
    {
        if (!sceneObjects.empty())
            return;
        auto place = [](float3 at, float scale = 1.0f) {
            return ViewportTransform{ .position = at, .scale = { scale, scale, scale } };
        };
        sceneObjects = {
            { "Cube",   SceneMesh::Cube,   place({ -1.8f, 0.5f, 0.0f }),        { 0.42f, 0.61f, 1.00f, 1 } },
            { "Sphere", SceneMesh::Sphere, place({  0.0f, 0.7f, 0.0f }, 1.4f),  { 0.79f, 0.81f, 0.85f, 1 } },
            { "Cone",   SceneMesh::Cone,   place({  1.9f, 0.6f, 0.4f }, 1.2f),  { 0.90f, 0.70f, 0.33f, 1 } },
        };
        SelectSceneObject(1);
    }

    void Application::SelectSceneObject(int index)
    {
        sceneSelected = index;
    }

    namespace
    {
        // The picked shape and this view agree: the view's gizmo moved it since the view last
        // had it, so the shape takes the view's; else the view takes the shape's (moved in
        // another view, by undo, or another shape picked).
        void SyncSelection(ViewportPanel& panel, std::vector<SceneObject>& objects, int selected)
        {
            ViewportView& view = panel.view;
            view.hasSelection = selected >= 0 && selected < int(objects.size());
            if (!view.hasSelection)
                return;
            SceneObject& object = objects[size_t(selected)];
            if (view.selection != panel.shownSelection && panel.shownSelection == object.transform)
                object.transform = view.selection;
            else
                view.selection = object.transform;
            view.selectionRadius = SceneObjectRadius(object);
            panel.shownSelection = view.selection;
        }
    }

    ID Application::CreateViewportPanel(std::unordered_map<ID, Panel>& panels, std::string title)
    {
        SeedScene();
        auto panel = std::make_shared<ViewportPanel>();
        panel->app = this;
        ViewportView& view = panel->view;
        view.camera.target   = { 0.0f, 0.6f, 0.0f };
        view.camera.distance = 7.0f;
        view.showTools = false;   // the toolbar dock's Tools set view.tool
        ViewportPanel* const self = panel.get();

        // The app draws its scene: the viewport hands it a command buffer, ready to draw into.
        view.onRender = [this, self](const ViewportRenderContext& ctx) {
            // The picked shape where this view's gizmo has it now, this frame's drag included.
            SyncSelection(*self, sceneObjects, sceneSelected);
#if TACHYON_VULKAN
            if (const auto* state = ctx.scene.As<vulkan::VulkanSceneRender>())
                if (auto* vk = dynamic_cast<vulkan::VulkanGraphicsContext*>(graphicsContext))
                    sceneRenderer.Render(*vk, *state, ctx, sceneObjects, sceneSelected);
#endif
#if TACHYON_D3D12
            if (const auto* state = ctx.scene.As<d3d12::D3D12SceneRender>())
                sceneRenderer.Render(*state, ctx, sceneObjects, sceneSelected);
#endif
#if TACHYON_SDLGPU
            if (const auto* state = ctx.scene.As<sdlgpu::SDLGPUSceneRender>())
                sceneRenderer.Render(*state, ctx, sceneObjects, sceneSelected);
#endif
            (void)ctx;
        };
        // A click picks the shape under it; on nothing it lets go of the selection.
        view.onClick = [this](const Ray& ray, KeyModifier) { SelectSceneObject(PickSceneObject(sceneObjects, ray)); };
        // A gizmo's drag moves the shape, on the undo history.
        view.onEdit = [this, self](const ViewportEdit& edit) {
            SyncSelection(*self, sceneObjects, sceneSelected);
            const int index = sceneSelected;
            auto apply = [this, index](const ViewportTransform& t) {
                if (index >= 0 && index < (int)sceneObjects.size())
                    sceneObjects[size_t(index)].transform = t;
            };
            commandManager.History().Push({ .description = "Transform " + sceneObjects[size_t(index)].name,
                .undo = [apply, edit] { apply(edit.from); },
                .redo = [apply, edit] { apply(edit.to); } });
            commandManager.History().EndMerge();
        };
        MakeViewportToolbars(*panel);

        std::erase_if(m_viewportPanels, [](const auto& p) { return p.expired(); });   // closed ones
        m_viewportPanels.push_back(panel);

        const ID id = CreatePanel(panels, std::move(title), showcase.panelViewportIcon, [this, panel] { RenderViewportPanel(*panel); });
        // Built again when the scene changes, in another view too: the gizmo follows the shape.
        panels[id].cacheSignature = [this]() -> std::optional<uint64_t> {
            uint64_t signature = uint64_t(int64_t(sceneSelected));
            for (const SceneObject& object : sceneObjects)
            {
                const ViewportTransform& t = object.transform;
                for (const float f : { t.position.x, t.position.y, t.position.z, t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w,
                                       t.scale.x, t.scale.y, t.scale.z })
                    signature = Hash::Combine(signature, uint64_t(std::bit_cast<uint32_t>(f)));
            }
            return signature;
        };
        // Where its camera is, how its toolbars are laid out and what they show, kept with the
        // layout.
        panels[id].saveState = [panel] {
            JsonValue state = JsonValue::MakeObject();
            state.Set("camera", SaveViewState(panel->view.camera));
            state.Set("toolbars", SaveToolbarLayout(panel->sides));
            state.Set("cameraView", panel->cameraView);
            state.Set("viewMode", int(panel->viewMode));
            state.Set("grid", panel->showGrid);
            state.Set("audio", panel->enableAudio);
            return state.Write();
        };
        panels[id].restoreState = [panel](std::string_view text) {
            const Result<JsonValue> state = JsonValue::Parse(text);
            if (!state || !state->IsObject())
            {
                (void)RestoreViewState(panel->view.camera, text);   // a camera alone, as kept before
                return;
            }
            if (const JsonValue* camera = state->Find("camera"); camera && camera->IsString())
                (void)RestoreViewState(panel->view.camera, camera->AsString());
            if (const JsonValue* toolbars = state->Find("toolbars"); toolbars && toolbars->IsString())
                (void)ApplyToolbarLayout(panel->sides, toolbars->AsString());
            if (const JsonValue* v = state->Find("cameraView"); v && v->IsNumber())
                panel->cameraView = std::clamp(int(v->AsNumber()), 0, int(std::size(kCameraViews)) - 1);
            if (const JsonValue* v = state->Find("viewMode"); v && v->IsNumber())
                panel->viewMode = ViewMode(uint32_t(v->AsNumber()) & uint32_t(ViewMode::TexturedLit));
            if (const JsonValue* v = state->Find("grid"); v && v->IsBool())
                panel->showGrid = v->AsBool();
            if (const JsonValue* v = state->Find("audio"); v && v->IsBool())
                panel->enableAudio = v->AsBool();
        };
        return id;
    }

    void Application::MakeViewportToolbars(ViewportPanel& panel)
    {
        InitializeToolbarDockState(panel.dockState, *this);
        panel.cameraMenuID = idManager.CreateStaticID();
        ViewportPanel* const self = &panel;

        // Toolbars on several sides exercise wrap, in-side reorder and cross-side
        // drag. The dock wraps each renderBody in BeginToolbar / EndToolbar.
        panel.sides[(int)ToolbarSide::Top].push_back(ToolbarItem
            {
                .id = idManager.CreateStaticID(), .debugName = "File",
                .renderBody = [this]()
                {
                    // Icon, callback and tooltip (with shortcut) come from the command.
                    CommandButton(showcase.cmdNew);
                    CommandButton(showcase.cmdOpen);
                    CommandButton(showcase.cmdSave);
                    CommandButton(showcase.cmdSaveAs);
                    CommandButton(showcase.cmdUndo);
                    CommandButton(showcase.cmdRedo);
                }
            });
        panel.sides[(int)ToolbarSide::Top].push_back(ToolbarItem
            {
                .id = idManager.CreateStaticID(), .debugName = "View",
                .renderBody = [this, self]()
                {
                    ViewMode& viewMode = self->viewMode;
                    const int viewModeNoLit = (int)viewMode & ~(int)ViewMode::Lit;
                    const int viewModeLit   = (int)viewMode & (int)ViewMode::Lit;
                    Toggle(viewModeNoLit == (int)ViewMode::Wireframe, showcase.viewWireframe, [&viewMode](bool value) { if (value) viewMode = ViewMode::Wireframe; });
                    SetTooltip("Toggle wireframe view mode");
                    Toggle(viewModeNoLit == (int)ViewMode::Solid,     showcase.viewSolid,     [&viewMode, viewModeLit](bool value) { if (value) viewMode = (ViewMode)((int)ViewMode::Solid | viewModeLit); });
                    SetTooltip("Toggle solid view mode");
                    Toggle(viewModeNoLit == (int)ViewMode::Textured,  showcase.viewTextured,  [&viewMode, viewModeLit](bool value) { if (value) viewMode = (ViewMode)((int)ViewMode::Textured | viewModeLit); });
                    SetTooltip("Toggle textured view mode");

                    const bool isLit = viewModeLit == (int)ViewMode::Lit;
                    Button(isLit ? showcase.lightOnIcon : showcase.lightOffIcon, [&viewMode, isLit, viewModeNoLit]()
                    {
                        if (isLit)
                            viewMode = (ViewMode)viewModeNoLit;
                        else if (viewModeNoLit == (int)ViewMode::Wireframe)
                            viewMode = ViewMode::TexturedLit;
                        else
                            viewMode = (ViewMode)(viewModeNoLit | (int)ViewMode::Lit);
                    });
                    SetTooltip(isLit ? "Lighting on" : "Lighting off");

                    Button(self->enableAudio ? showcase.audioOnIcon : showcase.audioOffIcon, [self]() { self->enableAudio = !self->enableAudio; });
                    SetTooltip(self->enableAudio ? "Audio on" : "Audio off");

                    Button(self->showGrid ? showcase.gridOnIcon : showcase.gridOffIcon, [self]() { self->showGrid = !self->showGrid; });
                    SetTooltip(self->showGrid ? "Grid on" : "Grid off");
                }
            });
        panel.sides[(int)ToolbarSide::Top].push_back(ToolbarItem
            {
                .id = idManager.CreateStaticID(), .debugName = "Camera",
                .renderBody = [this, self]()
                {
                    if (BeginMenu(self->cameraMenuID, kCameraViews[self->cameraView]))
                    {
                        MenuFilter();
                        for (int i = 0; i < int(std::size(kCameraViews)); ++i)
                            MenuItem(kCameraViews[i], [self, i]() { self->cameraView = i; });
                    }
                    EndMenu();
                    SetTooltip("Camera view");
                }
            });
        panel.sides[(int)ToolbarSide::Left].push_back(ToolbarItem
            {
                .id = idManager.CreateStaticID(), .debugName = "Tools",
                .renderBody = [this, self]()
                {
                    // The view's tool; Q, W, E and R over the view set it too.
                    ViewportTool& tool = self->view.tool;
                    Toggle(tool == ViewportTool::Select, showcase.toolSelectIcon, [&tool](bool value) { if (value) tool = ViewportTool::Select; });
                    SetTooltip("Select mode (Q)");
                    Toggle(tool == ViewportTool::Move,   showcase.toolMoveIcon,   [&tool](bool value) { if (value) tool = ViewportTool::Move;   });
                    SetTooltip("Move mode (W)");
                    Toggle(tool == ViewportTool::Rotate, showcase.toolRotateIcon, [&tool](bool value) { if (value) tool = ViewportTool::Rotate; });
                    SetTooltip("Rotate mode (E)");
                    Toggle(tool == ViewportTool::Scale,  showcase.toolScaleIcon,  [&tool](bool value) { if (value) tool = ViewportTool::Scale;  });
                    SetTooltip("Scale mode (R)");
                }
            });

        panel.callbacks.onReorder = [self](ToolbarSide side, int from, int to)
        {
            auto& src = self->sides[(int)side];
            if (from < 0 || from >= (int)src.size()) return;
            to = std::clamp(to, 0, (int)src.size() - 1);
            if (from == to) return;
            auto item = src[from];
            src.erase(src.begin() + from);
            src.insert(src.begin() + to, std::move(item));
        };
        panel.callbacks.onCrossSideDrop = [self](ToolbarSide srcSide, int from, ToolbarSide targetSide)
        {
            auto& src = self->sides[(int)srcSide];
            if (from < 0 || from >= (int)src.size()) return;
            auto item = src[from];
            src.erase(src.begin() + from);
            self->sides[(int)targetSide].push_back(std::move(item));
        };
        panel.callbacks.onTearOff = [this, self](ToolbarSide srcSide, int from, tachyon::int2 mousePos)
        {
            auto& src = self->sides[(int)srcSide];
            if (from < 0 || from >= (int)src.size()) return;
            auto item = src[from];
            src.erase(src.begin() + from);
            self->floatingItems.push_back(FloatingToolbarItem
            {
                .item          = item,
                .windowID      = idManager.CreateStaticID(),
                .spawnPosition = mousePos,
                .positioned    = false,
                .title         = item.debugName,
                .homeSide      = srcSide
            });
        };
        panel.floatingOptions.dockState = &panel.dockState;
        panel.floatingOptions.onDockBack = [this, self](ID windowID, ToolbarSide targetSide)
        {
            for (auto it = self->floatingItems.begin(); it != self->floatingItems.end(); ++it)
            {
                if (it->windowID != windowID)
                    continue;
                windowManager->RequestCloseAllowReopen(windowID);
                idManager.ReleaseStaticID(windowID);
                self->sides[(int)targetSide].push_back(std::move(it->item));
                self->floatingItems.erase(it);
                break;
            }
        };
    }

    void Application::EmitViewportFloatingToolbars()
    {
        for (const std::weak_ptr<ViewportPanel>& weak : m_viewportPanels)
            if (const std::shared_ptr<ViewportPanel> panel = weak.lock(); panel && !panel->floatingItems.empty())
                EmitFloatingToolbarWindows(panel->floatingItems, &panel->floatingOptions);
    }

    void Application::RenderViewportPanel(ViewportPanel& panel)
    {
        panel.dockState.overlay = true;   // the toolbars float over the 3D view
        RenderToolbarDock(*this, panel.dockState, panel.sides, panel.callbacks,
            [this, &panel]()
            {
                SyncSelection(panel, sceneObjects, sceneSelected);
                const ID viewportID = idManager.AllocateDynamicID("scene");
                Viewport(viewportID, panel.view, LayoutOverride{}.FlexGrow(1));
                // Its width, for the busy bar along its top next frame.
                if (const ElementRef ref = elementStorage.FindElementRefByID(viewportID); ref.IsValid())
                    VERIFY(elementStorage.AddPostLayout(ref, [&panel](PostLayoutContext& ctx) { panel.width = ctx.ElementRect().w; }));
                RenderViewportBusy(viewportID, panel.width);
            });
    }
} // namespace tachyon
