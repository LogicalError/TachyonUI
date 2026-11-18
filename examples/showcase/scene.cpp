#include "scene.h"

#include <algorithm>
#include <cmath>

namespace tachyon
{
    float SceneObjectRadius(const SceneObject& object)
    {
        const float3 s = object.transform.scale;
        // Half the cube's diagonal; the sphere and cone fit inside it.
        return 0.87f * std::max({ std::fabs(s.x), std::fabs(s.y), std::fabs(s.z) });
    }

    int PickSceneObject(std::span<const SceneObject> objects, const Ray& ray)
    {
        int best = -1;
        float nearest = 0.0f;
        for (int i = 0; i < (int)objects.size(); ++i)
        {
            const float3 to = objects[size_t(i)].transform.position - ray.origin;
            const float along = Dot(to, ray.direction);
            const float r = SceneObjectRadius(objects[size_t(i)]);
            const float miss2 = Dot(to, to) - along * along;
            if (along < 0.0f || miss2 > r * r)
                continue;
            if (best < 0 || along < nearest) { best = i; nearest = along; }
        }
        return best;
    }
}
