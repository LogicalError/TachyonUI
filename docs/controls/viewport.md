# Viewport

A 3D view of your scene, as an editor has one: a camera to move round it, and gizmos to move,
rotate and scale the selected object.

- TachyonUI doesn't draw 3D scenes. Your app does, into an image the viewport keeps for it.
- The viewport handles the rest:
  - the image, at the view's size, made again when it changes
  - the camera and the mouse and keys that move it
  - the gizmo over the selection, and its edits for undo
  - the tools down the left and the axes at the top right

## Viewport

```cpp
bool Viewport(ID id, ViewportView& view, LayoutOverride&& constraints = {});
```

- Give it room: it fills what it's given, at least 120 pixels each way.
- It returns true on the frame after a gizmo's drag changed the selection.
- Disabled, it takes no input.

```cpp
ViewportView scene;   // a member

scene.onRender = [this](const ViewportRenderContext& ctx) { DrawScene(ctx); };
scene.onClick  = [this](const Ray& ray, KeyModifier) { SelectObjectUnder(ray); };

Viewport(idManager.AllocateDynamicID("scene"), scene, LayoutOverride{}.FlexGrow(1));
```

## Drawing your scene

Each frame the viewport calls `onRender`, with:

| Field | What it is |
| --- | --- |
| `size` | The image's size, in pixels |
| `view`, `projection` | The camera's matrices |
| `native` | The backend's own recording state |

- On Vulkan, `native` is a `VulkanSceneRender` (`vk_graphics_context.h`):
  - `commandBuffer`, with dynamic rendering already begun
  - `colorFormat` (`R8G8B8A8_UNORM`) and `depthFormat` (`D32_SFLOAT`), for your pipelines
  - `extent`; the viewport and scissor are already set to all of it
- The color is cleared to `view.background` and the depth to 1. Bind your pipeline and draw;
  leave the rendering open.
- The layouts, the synchronization and showing the image are the library's.

```cpp
void DrawScene(const ViewportRenderContext& ctx)
{
    const auto& vk = *static_cast<const tachyon::vulkan::VulkanSceneRender*>(ctx.native);
    vkCmdBindPipeline(vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    const float4x4 viewProjection = ctx.projection * ctx.view;
    for (const Mesh& mesh : m_meshes)
        DrawMesh(vk.commandBuffer, viewProjection * mesh.transform, mesh);
}
```

- `onRender` runs while the frame is recorded, after that frame's input. The scene and the
  gizmo over it use the same camera.
- The viewport is built anew every frame, as its scene is drawn every frame, also in a
  cached panel.
- Without a GPU backend (the headless test app), `onRender` isn't called. The view shows
  `background`.
- The showcase's Viewport panel draws a few shapes on a grid this way:
  `examples/showcase/scene_renderer.cpp`.

### The matrices

`gui_math3d.h` has the 3D math the viewport uses, for your app too: `Dot`, `Cross`,
`Normalize`, `quat`, `TRS`, `LookAt`, `Perspective`, `Inverse`, `Ray`.

- Matrices act on column vectors (`p' = M * p`); `r0` to `r3` are their rows.
  - In GLSL, declare them `layout(row_major)`, or transpose them.
- Right-handed, y up.
- Clip space is Vulkan's: y down, depth from 0 (near) to 1 (far).

## The camera

`view.camera` is a `ViewportCamera`: it orbits `target`, `distance` away.

| Field | What it's for |
| --- | --- |
| `target` | What it looks at, and turns round |
| `distance` | How far away it is |
| `yaw`, `pitch` | Its direction, in radians; yaw 0 looks down -z |
| `fovY` | The view's height as an angle (50 degrees) |
| `orthographic` | No perspective; as high as the perspective view is at the target |
| `nearZ`, `farZ` | The depth range |

The mouse moves it:

| Input | Does |
| --- | --- |
| Middle drag | Pans |
| Alt + left drag | Orbits round the target |
| Alt + right drag | Dollies in and out |
| Right drag | Looks round, where it stands |
| Alt + Ctrl + left drag | Pans, without a middle button |
| Wheel | Dollies, or pans, as `SetWheelMode` says |
| Pinch | Dollies |

- Escape during a drag puts the camera back.
- A click on an axis end at the top right looks along that axis, from that end.

### Flying

While the right button looks round, keys move the camera:

| Key | Moves |
| --- | --- |
| W, S | Forward, back along the view |
| A, D | Left, right |
| Q, E | Down, up |
| Shift | Three times as fast |
| Wheel | Changes the speed |

- `flySpeed` is the speed, in world units a second (4 by default).
- Letting go of the right button stops it.

```cpp
scene.flySpeed = 20.0f;   // a big level
```

- The helpers move a camera from your own code too: `OrbitCamera`, `LookCamera`,
  `PanCamera`, `DollyCamera`, `AlignCamera`, `FrameSelection`.
- `ViewportRay(camera, pixel, size)` is the ray through a pixel, and `ViewportProject`
  where a point shows.

## Selection and gizmos

The viewport doesn't know your scene's objects. It moves one transform: the selection.

- `hasSelection`: there is one, with a gizmo.
- `selection`: its `ViewportTransform`: `position`, `rotation` (a `quat`), `scale`.
- `selectionRadius`: how big it is, for F to frame it.
- `onClick` gets the ray through a click that didn't drag, off the gizmo. Pick with it.

```cpp
scene.onClick = [this](const Ray& ray, KeyModifier) {
    const int hit = PickObject(ray);   // your scene's
    scene.hasSelection = hit >= 0;
    if (hit >= 0)
        scene.selection = m_objects[size_t(hit)].transform;
};
```

`tool` says what the gizmo does:

| Tool | Key | The gizmo |
| --- | --- | --- |
| `Select` | Q | None: a click picks |
| `Move` | W | An arrow along each axis; a square for each plane; the middle moves in the view's plane |
| `Rotate` | E | A ring round each axis |
| `Scale` | R | A handle along each of the object's own axes; the middle scales all three |

- The parts light up under the pointer.
- Escape during a drag puts the selection back.
- Move and rotate go along the world's axes.
- F frames the selection.

### Edits and undo

Every drag comes to `onEdit` as a `ViewportEdit`, already done: the selection as it was
(`from`) and is (`to`).

- `ApplyViewportEdit(view, edit, undo)` does it again, or undoes it.
- `CommitViewportEdit(view, edit)` applies an edit and hands it to `onEdit`.
- Copy `selection` back to your object as the gizmo moves it: each frame, or in `onRender`
  to draw it where the gizmo is now.

```cpp
scene.onEdit = [this](const ViewportEdit& edit) {
    history.Push({ .description = "Move",
                   .undo = [this, edit] { ApplyViewportEdit(scene, edit, /*undo*/ true); },
                   .redo = [this, edit] { ApplyViewportEdit(scene, edit); } });
};
```

## The tools

The tools' buttons sit at the top left of the view.

- `showTools = false` hides them, for an app with a toolbar of its own that sets `tool`.
  - Q, W, E and R still pick a tool.
- With a toolbar dock round the view, set its `overlay`, so the toolbars float over the
  scene instead of taking room beside it. See RenderToolbarDock in
  [Tabs, docking and toolbars](tabs-docking-toolbars.md).

## Classes

- the view: `viewport`; its background color shows only before the scene's first image
- the tools' strip: `viewport-tools`; a tool's button: `viewport-tool`, selected for the one
  in use
- drawn: `viewport-axis-label`, an axis's letter at the top right

The palette sets:

- the gizmo and the axes: `--axis-x`, `--axis-y`, `--axis-z`; a lit part: `--warn`
- the middle of the gizmo: `--text-1`
- the selected tool: `--accent`, its icon `--on-accent`

---

[Previous: Plots](plots.md) | [Controls](../controls.md) | [Next: Menus](menus.md)
