#include <gui_controls/gui_view_state.h>
#include <gui_core/gui_json.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>

namespace tachyon
{
    namespace
    {
        // The state's JSON object; null when it isn't one.
        std::optional<JsonValue> ObjectOf(std::string_view state)
        {
            Result<JsonValue> parsed = JsonValue::Parse(state);
            if (!parsed || !parsed->IsObject())
                return std::nullopt;
            return std::move(*parsed);
        }

        // `name`'s array of `out.size()` finite numbers, into `out`; false when it isn't one.
        bool Numbers(const JsonValue& object, const char* name, std::span<float> out)
        {
            const JsonValue* v = object.Find(name);
            if (!v || !v->IsArray() || v->Items().size() != out.size())
                return false;
            for (size_t i = 0; i < out.size(); ++i)
            {
                const JsonValue& item = v->Items()[i];
                if (!item.IsNumber() || !std::isfinite(item.AsNumber()))
                    return false;
                out[i] = float(item.AsNumber());
            }
            return true;
        }

        // `name`'s finite number; null when it isn't one.
        std::optional<float> Number(const JsonValue& object, const char* name)
        {
            const JsonValue* v = object.Find(name);
            if (!v || !v->IsNumber() || !std::isfinite(v->AsNumber()))
                return std::nullopt;
            return float(v->AsNumber());
        }

        JsonValue ArrayOf(std::initializer_list<float> values)
        {
            JsonValue array = JsonValue::MakeArray();
            for (const float v : values)
                array.Push(double(v));
            return array;
        }
    }

    std::string SaveViewState(const NodeView& view)
    {
        const float3x2& t = view.transform;
        JsonValue state = JsonValue::MakeObject();
        state.Set("transform", ArrayOf({ t.r0.x, t.r0.y, t.r0.z, t.r1.x, t.r1.y, t.r1.z }));
        return state.Write();
    }

    bool RestoreViewState(NodeView& view, std::string_view state)
    {
        const std::optional<JsonValue> object = ObjectOf(state);
        float m[6];
        if (!object || !Numbers(*object, "transform", m))
            return false;
        view.transform.r0 = float3{ m[0], m[1], m[2] };
        view.transform.r1 = float3{ m[3], m[4], m[5] };
        return true;
    }

    std::string SaveViewState(const TimelineView& view)
    {
        JsonValue state = JsonValue::MakeObject();
        state.Set("start", double(view.viewStart));
        state.Set("length", double(view.viewLength));
        state.Set("zoom", view.zoom);
        return state.Write();
    }

    bool RestoreViewState(TimelineView& view, std::string_view state)
    {
        const std::optional<JsonValue> object = ObjectOf(state);
        if (!object)
            return false;
        const std::optional<float> start = Number(*object, "start"), length = Number(*object, "length"), zoom = Number(*object, "zoom");
        if (!start || !length || !zoom)
            return false;
        view.viewStart  = *start;
        view.viewLength = *length;
        view.zoom       = std::clamp(int(std::lround(*zoom)), 0, 100);
        return true;
    }

    std::string SaveViewState(const CurveView& view)
    {
        if (!view.shownSet)
            return {};
        const CurveRange& r = view.shown;
        JsonValue state = JsonValue::MakeObject();
        state.Set("shown", ArrayOf({ r.timeMin, r.timeMax, r.valueMin, r.valueMax }));
        return state.Write();
    }

    bool RestoreViewState(CurveView& view, std::string_view state)
    {
        const std::optional<JsonValue> object = ObjectOf(state);
        float r[4];
        if (!object || !Numbers(*object, "shown", r) || !(r[0] < r[1]) || !(r[2] < r[3]))
            return false;
        view.shown    = CurveRange{ r[0], r[1], r[2], r[3] };
        view.shownSet = true;
        return true;
    }

    std::string SaveViewState(const ViewportCamera& camera)
    {
        JsonValue state = JsonValue::MakeObject();
        state.Set("target", ArrayOf({ camera.target.x, camera.target.y, camera.target.z }));
        state.Set("distance", double(camera.distance));
        state.Set("yaw", double(camera.yaw));
        state.Set("pitch", double(camera.pitch));
        state.Set("fovY", double(camera.fovY));
        state.Set("orthographic", camera.orthographic);
        return state.Write();
    }

    bool RestoreViewState(ViewportCamera& camera, std::string_view state)
    {
        const std::optional<JsonValue> object = ObjectOf(state);
        if (!object)
            return false;
        float target[3];
        const std::optional<float> distance = Number(*object, "distance"), yaw = Number(*object, "yaw"),
                                   pitch = Number(*object, "pitch"), fovY = Number(*object, "fovY");
        const JsonValue* orthographic = object->Find("orthographic");
        if (!Numbers(*object, "target", target) || !distance || !yaw || !pitch || !fovY || !(*distance > 0.0f)
            || !(*fovY > 0.0f && *fovY < 3.14f) || !orthographic || !orthographic->IsBool())
            return false;
        camera.target       = float3{ target[0], target[1], target[2] };
        camera.distance     = *distance;
        camera.yaw          = *yaw;
        camera.pitch        = *pitch;
        camera.fovY         = *fovY;
        camera.orthographic = orthographic->AsBool();
        return true;
    }

    std::string SaveViewState(const GUIApplication::SettingsPanelState& settings)
    {
        for (const GUIApplication::SettingsPanelState::Page& page : settings.pages)
            if (page.id == settings.selectedID)
            {
                JsonValue state = JsonValue::MakeObject();
                state.Set("page", page.name);
                return state.Write();
            }
        return {};
    }

    bool RestoreViewState(GUIApplication::SettingsPanelState& settings, std::string_view state)
    {
        const std::optional<JsonValue> object = ObjectOf(state);
        const JsonValue* name = object ? object->Find("page") : nullptr;
        if (!name || !name->IsString())
            return false;
        for (const GUIApplication::SettingsPanelState::Page& page : settings.pages)
            if (page.name == name->AsString())
            {
                settings.selectedID = page.id;
                return true;
            }
        return false;
    }
}
