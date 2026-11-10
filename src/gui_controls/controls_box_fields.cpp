#include <gui_controls/gui_application.h>

#include <algorithm>
#include <string_view>

// Fields for a box's looks: EdgesField and CornersField, four numbers (one a side or a
// corner) with a link toggle that makes them one; ShadowField, a shadow's color and sizes.

namespace tachyon
{
    namespace
    {
        // Whether the sides are linked, kept while the field is shown.
        struct BoxSidesLink
        {
            bool linked = true;
        };
    }

    bool GUIApplication::BoxSidesField(ID id, uint16_t* const (&sides)[4], const std::string_view (&captions)[4],
                                       const std::string_view (&names)[4], NumericFieldConfig<int32_t> config,
                                       LayoutOverride&& constraints)
    {
        const bool equal = *sides[0] == *sides[1] && *sides[0] == *sides[2] && *sides[0] == *sides[3];
        ElementDataStore& store = guiStateManager.GetElementDataStore();
        BoxSidesLink& link = store.GetOrCreate<BoxSidesLink>(id, BoxSidesLink{ equal });

        auto clamped = [&config](int32_t v) { return uint16_t(std::clamp(v, std::max(config.minVal, 0), std::min(config.maxVal, 65535))); };
        bool modified = false;
        if (link.linked && !equal)
        {
            // Just linked: the others take the first's.
            for (uint16_t* side : sides)
                *side = *sides[0];
            modified = true;
        }

        // Width 0 + flex-grow: the group fills what's left after the label.
        constraints.Width(0);
        RegisterElement(Element{ .id = id, .styleRef = theme.fieldElementGroupStyleRef,
                                 .flags = ElementFlags::PropagatesDescendantState }, std::move(constraints));
        VERIFY(elementStorage.BeginChildren());
        const bool pushed = idManager.PushParent(id);

        Toggle(idManager.AllocateDynamicID("box_sides_link"), link.linked,
               GUIContent{ link.linked ? theme.linkIcon : theme.unlinkIcon, {},
                           link.linked ? "Linked: one value for all four" : "Each its own" },
               [&store, id](bool on) { store.GetOrCreate<BoxSidesLink>(id).linked = on; },
               theme.boxSidesLinkStyleRef, LayoutOverride{}.NoGrow().NoShrink());

        if (link.linked)
        {
            int32_t all = *sides[0];
            if (Field(idManager.AllocateDynamicID("box_sides_all"), all, TextEditBehavior::ContinuousUpdate,
                      theme.controls->number, config, LayoutOverride::Auto().MinWidth(40).FlexGrow(1)))
            {
                for (uint16_t* side : sides)
                    *side = clamped(all);
                modified = true;
            }
        }
        else
        {
            for (int i = 0; i < 4; ++i)
            {
                DragLabel(idManager.AllocateDynamicID("box_sides_caption"), captions[i], theme.axisLabelStyleRefs[3],
                          LayoutOverride::Auto().MinWidth(Pixels{ theme.axisLabelWidth }).NoGrow().KeepWithNext());
                SetTooltip(names[i]);
                int32_t v = *sides[i];
                if (Field(idManager.AllocateDynamicID("box_sides_value"), v, TextEditBehavior::ContinuousUpdate,
                          theme.vectorComponentStyleRef, config, LayoutOverride::Auto().MinWidth(40)))
                {
                    *sides[i] = clamped(v);
                    modified = true;
                }
            }
        }

        if (pushed) (void)idManager.PopParent();
        VERIFY(elementStorage.EndChildren());
        guiStateManager.SetModified(modified);
        return modified;
    }

    bool GUIApplication::EdgesField(ID id, BoxEdges& value, NumericFieldConfig<int32_t> config, LayoutOverride&& constraints)
    {
        static constexpr std::string_view kCaptions[4] = { "T", "R", "B", "L" };
        static constexpr std::string_view kNames[4]    = { "Top", "Right", "Bottom", "Left" };
        uint16_t* const sides[4] = { &value.top, &value.right, &value.bottom, &value.left };
        return BoxSidesField(id, sides, kCaptions, kNames, config, std::move(constraints));
    }

    bool GUIApplication::CornersField(ID id, BoxCorners& value, NumericFieldConfig<int32_t> config, LayoutOverride&& constraints)
    {
        static constexpr std::string_view kCaptions[4] = { "TL", "TR", "BR", "BL" };
        static constexpr std::string_view kNames[4]    = { "Top left", "Top right", "Bottom right", "Bottom left" };
        uint16_t* const sides[4] = { &value.topLeft, &value.topRight, &value.bottomRight, &value.bottomLeft };
        return BoxSidesField(id, sides, kCaptions, kNames, config, std::move(constraints));
    }

    bool GUIApplication::ShadowField(ID id, BoxShadow& value, LayoutOverride&& constraints)
    {
        bool modified = false;
        BeginVertical(id, theme.emptyVertStyleRef, std::move(constraints));
        const bool pushed = idManager.PushParent(id);

        modified = ColorField(idManager.AllocateDynamicID("shadow_color"), value.color) || modified;

        // The offsets may be negative; the blur may not.
        struct Part { std::string_view caption, name; int32_t value; int32_t min; };
        Part parts[] = {
            { "X",      "Horizontal offset",              value.x,      -256 },
            { "Y",      "Vertical offset",                value.y,      -256 },
            { "Blur",   "Blur radius",                    value.blur,      0 },
            { "Spread", "How far it grows past the box",  value.spread, -256 },
        };
        RegisterElement(Element{ .id = idManager.AllocateDynamicID("shadow_sizes"), .styleRef = theme.fieldElementGroupStyleRef,
                                 .flags = ElementFlags::PropagatesDescendantState }, LayoutOverride::Auto());
        VERIFY(elementStorage.BeginChildren());
        for (Part& part : parts)
        {
            DragLabel(idManager.AllocateDynamicID("shadow_caption"), part.caption, theme.axisLabelStyleRefs[3],
                      LayoutOverride::Auto().MinWidth(Pixels{ theme.axisLabelWidth }).NoGrow().KeepWithNext());
            SetTooltip(part.name);
            modified = Field(idManager.AllocateDynamicID("shadow_size"), part.value, TextEditBehavior::ContinuousUpdate,
                             theme.vectorComponentStyleRef,
                             NumericFieldConfig<int32_t>{ .stepPerPixel = 0.25, .minVal = part.min, .maxVal = 256 },
                             LayoutOverride::Auto().MinWidth(40)) || modified;
        }
        VERIFY(elementStorage.EndChildren());
        value.x      = int16_t(std::clamp(parts[0].value, -256, 256));
        value.y      = int16_t(std::clamp(parts[1].value, -256, 256));
        value.blur   = uint16_t(std::clamp(parts[2].value, 0, 256));
        value.spread = int16_t(std::clamp(parts[3].value, -256, 256));

        if (pushed) (void)idManager.PopParent();
        EndVertical();
        guiStateManager.SetModified(modified);
        return modified;
    }

}
