#include "application.h"

#include <gui_controls/controls_toolbar_dock.h>
#include <gui_core/graphics/vulkan/vk_graphics_context.h>

//
// View panel: thin adapter over tachyon::RenderToolbarDock. ToolbarItem lists
// live directly in toolbars.sides[] and are seeded in InitializeShowcaseGUI;
// the reorder / cross-side / tear-off callbacks mutate those side vectors.
//

namespace tachyon
{
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
        scene.showTools = false;   // the toolbar dock's Tools set scene.tool
        scene.camera.target = { 0.0f, 0.6f, 0.0f };
        scene.camera.distance = 7.0f;

        // The app draws its scene: the viewport hands it a command buffer, ready to draw into.
        scene.onRender = [this](const ViewportRenderContext& ctx) {
            // The selected shape where the gizmo has it now, this frame's drag included.
            if (sceneSelected >= 0 && sceneSelected < (int)sceneObjects.size())
                sceneObjects[size_t(sceneSelected)].transform = scene.selection;
            if (auto* vk = dynamic_cast<vulkan::VulkanGraphicsContext*>(graphicsContext))
                sceneRenderer.Render(*vk, *static_cast<const vulkan::VulkanSceneRender*>(ctx.native), ctx, sceneObjects, sceneSelected);
        };
        // A click picks the shape under it; on nothing it lets go of the selection.
        scene.onClick = [this](const Ray& ray, KeyModifier) { SelectSceneObject(PickSceneObject(sceneObjects, ray)); };
        // A gizmo's drag moves the shape, on the undo history.
        scene.onEdit = [this](const ViewportEdit& edit) {
            const int index = sceneSelected;
            auto apply = [this, index](const ViewportTransform& t) {
                if (index < 0 || index >= (int)sceneObjects.size()) return;
                sceneObjects[size_t(index)].transform = t;
                if (sceneSelected == index) scene.selection = t;
            };
            commandManager.History().Push({ .description = "Transform " + sceneObjects[size_t(index)].name,
                .undo = [apply, edit] { apply(edit.from); },
                .redo = [apply, edit] { apply(edit.to); } });
            commandManager.History().EndMerge();
        };
        SelectSceneObject(1);
    }

    void Application::SelectSceneObject(int index)
    {
        sceneSelected = index;
        scene.hasSelection = index >= 0;
        if (index >= 0)
        {
            scene.selection = sceneObjects[size_t(index)].transform;
            scene.selectionRadius = SceneObjectRadius(sceneObjects[size_t(index)]);
        }
    }

    void Application::RenderViewportPanel()
    {
        SeedScene();
        toolbars.dockState.overlay = true;   // the toolbars float over the 3D view
        RenderToolbarDock(*this, toolbars.dockState, toolbars.sides, toolbarCallbacks,
            [this]()
            {
                Viewport(idManager.AllocateDynamicID("scene"), scene, LayoutOverride{}.FlexGrow(1));
                // The gizmo moves the selection; the shape follows it, during the drag too.
                if (sceneSelected >= 0 && sceneSelected < (int)sceneObjects.size())
                    sceneObjects[size_t(sceneSelected)].transform = scene.selection;
            });
    }
} // namespace tachyon
