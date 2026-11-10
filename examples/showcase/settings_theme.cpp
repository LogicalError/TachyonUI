#include "application.h"

// The Appearance page's theme: Dark or Light, and below it the theme's colors and sizes by
// category, as their @property rules describe them. A change is the theme's own: Dark keeps
// its changes and Light its, saved with the UI state. Reset takes a theme back to its own.

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <string_view>

namespace tachyon
{
    namespace
    {

        std::string Hex(color4b c)
        {
            return c.a == 255 ? std::format("#{:02X}{:02X}{:02X}", c.r, c.g, c.b)
                              : std::format("#{:02X}{:02X}{:02X}{:02X}", c.r, c.g, c.b, c.a);
        }

        // Up to three decimals, without trailing zeros: 0.4, 0.125, 2.
        std::string Number(float v)
        {
            std::string text = std::format("{:.3f}", v);
            while (text.back() == '0') text.pop_back();
            if (text.back() == '.') text.pop_back();
            return text;
        }

        // A value's one to four lengths as four, the way border-width spreads them.
        BoxEdges EdgesOf(const CustomProperty& value)
        {
            const uint16_t* d = value.dims;
            switch (value.dimCount)
            {
            case 1:  return BoxEdges(d[0]);
            case 2:  return BoxEdges(d[0], d[1]);
            case 3:  return BoxEdges(d[0], d[1], d[2], d[1]);
            default: return BoxEdges(d[0], d[1], d[2], d[3]);
            }
        }

        // As EdgesOf, the way border-radius spreads them.
        BoxCorners CornersOf(const CustomProperty& value)
        {
            const uint16_t* d = value.dims;
            switch (value.dimCount)
            {
            case 1:  return BoxCorners(d[0]);
            case 2:  return BoxCorners(d[0], d[1], d[0], d[1]);
            case 3:  return BoxCorners(d[0], d[1], d[2], d[1]);
            default: return BoxCorners(d[0], d[1], d[2], d[3]);
            }
        }

        // Four lengths as CSS: one when they're the same.
        std::string Lengths(uint16_t a, uint16_t b, uint16_t c, uint16_t d)
        {
            if (a == b && a == c && a == d)
                return std::format("{}px", a);
            return std::format("{}px {}px {}px {}px", a, b, c, d);
        }

        std::string ShadowText(const BoxShadow& s)
        {
            if (s.IsNone())
                return "none";
            return std::format("{}{}px {}px {}px {}px {}", s.inset ? "inset " : "", s.x, s.y, s.blur, s.spread, Hex(s.color));
        }
    }

    // ---------------------------------------------------------------------------
    // The picker and the editor
    // ---------------------------------------------------------------------------

    void Application::RenderThemePicker()
    {
        auto& editor = showcase.themeEditor;
        if (editor.resetDialogID == ID::Invalid())
            editor.resetDialogID = idManager.CreateStaticID();
        const int index = CurrentThemeIndex();
        const std::string_view themeName = index >= 0 ? kThemeNames[index] : std::string_view("This theme");

        Label("Theme");
        BeginHorizontal(idManager.AllocateDynamicID("settingsThemeRow"), theme.searchRowStyleRef, LayoutOverride{}.FlexGrow(1));
        int selected = index;
        if (Dropdown(idManager.AllocateDynamicID("settingsThemeMenu"), selected, kThemeNames, LayoutOverride{}.FlexGrow(1)))
            RequestReloadTheme(kThemePaths[selected], showcase.settingsFontScalePct / 100.0f);
        const bool changed = !GetThemeVariables().empty();
        if (!changed) BeginDisabled();
        Button(idManager.AllocateDynamicID("settingsThemeReset"), { showcase.undoIcon, "Reset" },
               [&editor] { editor.confirmReset = true; });
        SetTooltip(std::format("The {} theme's own colors and sizes again", themeName));
        if (!changed) EndDisabled();
        EndHorizontal();

        if (editor.confirmReset && changed)
        {
            static constexpr MessageButton kButtons[] = {
                { .label = "Reset", .kind = MessageButtonKind::Danger },
                { .label = "Cancel" },
            };
            const std::string title   = std::format("Reset {} theme?", themeName);
            const std::string message = std::format("The colors and sizes you changed go back to the {} theme's own.", themeName);
            const int choice = MessageDialog(editor.resetDialogID, MessageDialogDesc{
                .icon = MessageIcon::Question, .title = title, .message = message, .buttons = kButtons,
                .defaultButton = 1, .cancelButton = 1 });
            if (choice == 0)
                ClearThemeVariables();
            if (choice >= 0)
                editor.confirmReset = false;
        }
        else
            editor.confirmReset = false;
    }

    void Application::RenderThemeEditor()
    {
        auto& editor = showcase.themeEditor;
        const std::span<const CustomPropertyInfo> variables = theme.GetVariables();

        BeginHorizontal(idManager.AllocateDynamicID("settingsThemeSearchRow"), inspectorSearchRowStyleRef,
                        LayoutOverride::Auto().NoShrink());
        SearchField(editor.search, showcase.findIcon, TextEditBehavior::ContinuousUpdate, LayoutOverride{}.FlexGrow(1));
        EndHorizontal();

        const std::string query = SearchToLower(editor.search);
        const auto matches = [&query](const CustomPropertyInfo& info) {
            return query.empty() || SearchMatches(info.label, query) || SearchMatches(info.description, query)
                || SearchMatches(info.category, query);
        };

        // A section per category, in the order they first come.
        std::vector<std::string_view> categories;
        for (const CustomPropertyInfo& info : variables)
            if (std::find(categories.begin(), categories.end(), info.category) == categories.end())
                categories.push_back(info.category);

        bool firstSection = true;
        const ID labelGroupID = idManager.AllocateDynamicID("settingsThemeLabels");   // one for all the sections
        for (const std::string_view category : categories)
        {
            const auto inSection = [&](const CustomPropertyInfo& info) { return info.category == category && matches(info); };
            if (std::none_of(variables.begin(), variables.end(), inSection))
                continue;

            // The first section starts open; a search opens them all.
            const std::string key(category);
            const bool open = editor.open.try_emplace(key, firstSection).first->second;
            const bool shown = open || !query.empty();
            Button(idManager.AllocateDynamicID("settingsThemeSection"),
                   { shown ? showcase.chevronDownIcon : showcase.chevronRightIcon, category.empty() ? "Other" : category },
                   [&editor, key] { editor.open[key] = !editor.open[key]; },
                   firstSection ? inspectorSectionFirstStyleRef : inspectorSectionStyleRef,
                   LayoutOverride{}.NoGrow().NoShrink());
            firstSection = false;
            if (!shown)
                continue;

            BeginVertical(idManager.AllocateDynamicID("settingsThemeSectionBody"), inspectorSectionBodyStyleRef,
                          LayoutOverride::Auto().NoShrink());
            BeginLabelGroup(labelGroupID, /*defaultWidth=*/150, /*minWidth=*/60, /*maxWidth=*/320);
            for (const CustomPropertyInfo& info : variables)
                if (inSection(info))
                    ThemeVariableRow(info);
            EndLabelGroup();
            EndVertical();
        }
    }

    void Application::ThemeVariableRow(const CustomPropertyInfo& info)
    {
        const CustomProperty* value = theme.GetVariable(info.name);
        const bool changed = GetThemeVariables().contains(info.name);
        const std::string name = info.name;

        Label(info.label.empty() ? std::string_view(info.name).substr(2) : std::string_view(info.label));
        std::string tooltip = info.description;
        if (info.hasRange)
            tooltip += std::format("{}{} to {}", tooltip.empty() ? "" : "\n", Number(info.min), Number(info.max));
        if (!tooltip.empty())
            SetTooltip(tooltip);

        // A new value, for this theme only; a color shows at once.
        auto set = [this, name](const std::string& css) { SetThemeVariable(name, css); };
        NumericFieldConfig<int32_t> sizes{ .stepPerPixel = 0.25, .minVal = 0, .maxVal = 64 };
        if (info.hasRange) { sizes.minVal = int32_t(std::ceil(info.min)); sizes.maxVal = int32_t(std::floor(info.max)); }

        BeginHorizontal(idManager.AllocateDynamicID("settingsThemeRow"), theme.searchRowStyleRef, LayoutOverride{}.FlexGrow(1));
        const LayoutOverride fill = LayoutOverride{}.FlexGrow(1);
        if (!value)
        {
            Text(idManager.AllocateDynamicID("settingsThemeUnset"), "Not set", theme.labelStyleRef);
        }
        else if (info.type == CustomPropertyType::Color && value->hasColor)
        {
            color4b color = value->color;
            if (ColorField(idManager.AllocateDynamicID("settingsThemeColor"), color, LayoutOverride(fill)))
                set(Hex(color));
        }
        else if (info.type == CustomPropertyType::Number && value->hasNumber)
        {
            float number = value->number;
            NumericFieldConfig<float> config{ .stepPerPixel = 0.01 };
            if (info.hasRange) { config.minVal = info.min; config.maxVal = info.max; }
            if (Field(idManager.AllocateDynamicID("settingsThemeNumber"), number, TextEditBehavior::ContinuousUpdate, config,
                      LayoutOverride(fill)))
                set(Number(number));
        }
        else if (info.type == CustomPropertyType::Length && value->hasNumber)
        {
            int32_t length = int32_t(std::lround(value->number));
            if (Field(idManager.AllocateDynamicID("settingsThemeLength"), length, TextEditBehavior::ContinuousUpdate, sizes,
                      LayoutOverride(fill)))
                set(std::format("{}px", length));
        }
        else if (info.type == CustomPropertyType::Corners && value->hasNumber)
        {
            BoxCorners corners = CornersOf(*value);
            if (CornersField(idManager.AllocateDynamicID("settingsThemeCorners"), corners, sizes, LayoutOverride(fill)))
                set(Lengths(corners.topLeft, corners.topRight, corners.bottomRight, corners.bottomLeft));
        }
        else if ((info.type == CustomPropertyType::Edges || info.type == CustomPropertyType::Lengths) && value->hasNumber)
        {
            BoxEdges edges = EdgesOf(*value);
            if (EdgesField(idManager.AllocateDynamicID("settingsThemeEdges"), edges, sizes, LayoutOverride(fill)))
                set(Lengths(edges.top, edges.right, edges.bottom, edges.left));
        }
        else if (info.type == CustomPropertyType::Shadow && value->hasShadow)
        {
            BoxShadow shadow = value->shadows[0];
            if (shadow.IsNone())
                shadow.color = color4b{ 0, 0, 0, 0 };
            if (ShadowField(idManager.AllocateDynamicID("settingsThemeShadow"), shadow, LayoutOverride(fill)))
                set(ShadowText(shadow));
        }
        else
        {
            Text(idManager.AllocateDynamicID("settingsThemeOther"), value->text, theme.labelStyleRef);
        }
        if (changed)
        {
            Button(idManager.AllocateDynamicID("settingsThemeReset"), { showcase.undoIcon, {}, "Back to the theme's own" },
                   [this, name] { ClearThemeVariable(name); }, iconToggleStyleRef);
        }
        EndHorizontal();
    }

}
