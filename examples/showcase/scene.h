#pragma once

// The showcase's 3D scene: a few shapes on a ground grid, in the Viewport panel. What they are
// and which one a click picks; scene_renderer.h draws them.

#include <gui_controls/gui_viewport_types.h>
#include <gui_core/gui_math3d.h>

#include <span>
#include <string>

namespace tachyon
{
    enum class SceneMesh : uint8_t { Cube, Sphere, Cone };

    /// A shape in the scene, one unit across before its scale.
    struct SceneObject
    {
        std::string       name;
        SceneMesh         mesh = SceneMesh::Cube;
        ViewportTransform transform;
        color4f           color { 0.8f, 0.8f, 0.8f, 1.0f };
    };

    /// How far round an object reaches from its middle, for picking and framing it.
    float SceneObjectRadius(const SceneObject& object);
    /// The object `ray` hits first (by the sphere round each); -1 when it hits none.
    int PickSceneObject(std::span<const SceneObject> objects, const Ray& ray);
}
