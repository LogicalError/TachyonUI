#include <gui_controls/gui_application.h>
#include <array>
#include <string>
#include <cstdio>
#include <gui_controls/controls_color_internal.h>   // SetColorStyle (shared with controls_color.cpp)

#include <algorithm>
#include <cmath>

// Gradient controls: GradientField, EmitGradientEditor.

namespace
{
    // Create/update a VisualColorStyleIndex that draws a gradient horizontally, from its
    // samples (a GradientF's colors clamped for display).
    void SetGradientPreviewStyle(tachyon::StyleManager& sm, tachyon::VisualColorStyleIndex& idx,
                                 const tachyon::GradientF& gradient)
    {
        std::array<tachyon::color4b, 256> row{};
        gradient.WriteTo(std::span<tachyon::color4b>(row));
        tachyon::ColorStyle cs;
        cs.type = tachyon::ColorType::LinearGradient;
        cs.SetTransformation(tachyon::float3(0, 1, 0), tachyon::float3(1, 0, 0)); // horizontal
        if (idx == tachyon::VisualColorStyleIndex::Invalid)
            idx = sm.CreateColorStyle(cs, row);
        else
            sm.UpdateColorStyle(idx, cs, row);
    }

    tachyon::color4f Clamp01(tachyon::color4f c)
    {
        return { std::clamp(c.r, 0.0f, 1.0f), std::clamp(c.g, 0.0f, 1.0f), std::clamp(c.b, 0.0f, 1.0f), std::clamp(c.a, 0.0f, 1.0f) };
    }
}


namespace tachyon
{
    // ---------------------------------------------------------------------------
    // GradientField - gradient swatch + editor popup
    // ---------------------------------------------------------------------------

    bool GUIApplication::GradientField(ID id, Gradient& value, StyleRef style, LayoutOverride&& constraints)
    {
        if (value.stopCount > Gradient::kMaxStops)
        {
            ReportMisuse("GUIApplication: a gradient with more stops than Gradient::kMaxStops (clamped)");
            value.stopCount = Gradient::kMaxStops;
        }
        const Gradient before = value;
        GradientF edited = GradientF::From(value);
        bool cancelled = false;
        (void)GradientFieldCore(id, edited, &value, cancelled, style, std::move(constraints));
        if (cancelled)
            value = m_gradientEditorSub->State().originalGradient8;   // exactly as it was
        else if (!(edited == GradientF::From(value)))
        {
            // Only an edit writes back: the stops and the blend space, the rest kept.
            Gradient out = edited.ToGradient();
            out.type  = value.type == GradientType::None ? GradientType::Linear : value.type;
            out.value = value.value;
            value = out;
        }
        const bool changed = !(value == before);
        guiStateManager.SetModified(changed);
        return changed;
    }

    bool GUIApplication::GradientField(ID id, GradientF& value, StyleRef style, LayoutOverride&& constraints)
    {
        if (value.stops.size() > size_t(GradientF::kMaxStops))
        {
            ReportMisuse("GUIApplication: a gradient with more stops than GradientF::kMaxStops (clamped)");
            value.stops.resize(size_t(GradientF::kMaxStops));
        }
        const GradientF before = value;
        bool cancelled = false;
        (void)GradientFieldCore(id, value, nullptr, cancelled, style, std::move(constraints));
        const bool changed = !(value == before);
        guiStateManager.SetModified(changed);
        return changed;
    }

    bool GUIApplication::GradientFieldCore(ID id, GradientF& value, const Gradient* gradient8, bool& cancelled,
                                           StyleRef style, LayoutOverride&& constraints)
    {
        cancelled = false;
        const GradientEditLimits limits = gradient8 ? GradientEditLimits{ Gradient::kMaxStops, false, false }
                                                    : GradientEditLimits{ GradientF::kMaxStops, true, true };
        const Gradient original8 = gradient8 ? *gradient8 : Gradient{};

        // ---- Gradient swatch (inline preview bar) ----
        const ID swatchID = id;
        const std::string context = PendingLabelText();
        auto& ge = m_gradientEditorSub->State();
        // Escape is Cancel, and the keyboard goes back to the swatch.
        if (ge.triggerID == swatchID && ge.closeRequest == GradientEditorState::CloseRequest::None && TakeEscapeFor(ge.windowID, swatchID))
        {
            ge.closeRequest = GradientEditorState::CloseRequest::Cancel;
            guiStateManager.Interaction().SetFocusID(swatchID);
        }
        // OK keeps the gradient; Cancel and the close button restore the original.
        if (ge.triggerID == swatchID && ge.closeRequest != GradientEditorState::CloseRequest::None)
        {
            if (ge.closeRequest == GradientEditorState::CloseRequest::Cancel)
            {
                value = ge.originalGradient;
                cancelled = true;
            }
            ge.closeRequest = GradientEditorState::CloseRequest::None;
            ge.triggerID = ID::Invalid();
        }
        const bool editorIsOpen = (ge.triggerID == swatchID); // this field's editor, not just any

        // Get or create a persistent style index for this swatch
        NoteSwatchClaim(swatchID);
        auto& swatchStyle = m_gradientEditorSub->ClaimSwatchStyle(swatchID);
        SetGradientPreviewStyle(styleManager, swatchStyle, value);

        Element swatch {};
        swatch.id              = swatchID;
        swatch.styleRef        = style;
        swatch.state           = guiStateManager.GetCurrentState(swatchID);
        swatch.colorStyleIndex = swatchStyle;
        // A copy: the editor starts from the gradient as drawn, whatever becomes of the caller's.
        swatch.eventHandler = MakeButtonHandler([this, value, original8, limits, swatchID, editorIsOpen, context]()
        {
            auto& ge = m_gradientEditorSub->State();
            if (editorIsOpen)
            {
                ge.triggerID = ID::Invalid();
            } else
            {
                ge.triggerID = swatchID;
                if (ge.windowID == ID::Invalid())
                    ge.windowID = idManager.CreateStaticID();
                ge.workingCopy = value;
                ge.originalGradient = value;
                ge.originalGradient8 = original8;
                ge.limits = limits;
                ge.context = context;
                ge.selectedStop = 0;
                ge.press.Reset();
                ge.dragOff = false;
            }
        });

        const ElementRef swatchRef = RegisterControl(std::move(swatch), std::move(constraints));

        // The stops, as diamonds on the bar's bottom edge: light with a dark outline.
        {
            std::vector<float> offsets;
            for (int i = 0; i < value.StopCount(); i++)
                offsets.push_back(std::clamp(value.stops[size_t(i)].offset, 0.0f, 1.0f));
            const VisualColorStyleIndex fill    = styleManager.RegisterColorStyle(ColorStyle::Create(theme.GetColor("--text-1", color4f{ 0.9f, 0.9f, 0.9f, 1.0f })));
            const VisualColorStyleIndex outline = styleManager.RegisterColorStyle(ColorStyle::Create(theme.GetColor("--bg-app", color4f{ 0.05f, 0.05f, 0.05f, 1.0f })));
            VERIFY(elementStorage.AddCustomDraw(swatchRef,
                [offsets = std::move(offsets), fill, outline, half = float(styleManager.Px(4)), edge = float(styleManager.Px(1))](CustomDrawContext& ctx)
                {
                    const auto outer = ctx.ElementRect();
                    if (outer.w <= 0 || outer.h <= 0) return;
                    auto diamond = [&ctx](float2 c, float r, VisualColorStyleIndex color) {
                        const float2 corners[] = { { c.x - r, c.y }, { c.x, c.y - r }, { c.x + r, c.y }, { c.x, c.y + r } };
                        ctx.RenderFilledPolygon(corners, color);
                    };
                    for (const float offset : offsets)
                    {
                        // Kept inside the bar's ends so the end stops stay whole.
                        const float x = float(outer.x) + half + offset * float(std::max(0, outer.w) - 2.0f * half);
                        const float2 center{ x, float(outer.y + outer.h) };
                        diamond(center, half + edge, outline);
                        diamond(center, half, fill);
                    }
                }));
        }

        // ---- Editor popup ----
        if (editorIsOpen && ge.windowID != ID::Invalid())
        {
            auto popupPos = LayoutOverride{}.Anchored(swatchID,
                AnchorPlacement::OutsideBottom | AnchorPlacement::InsideLeft,
                {0, int16_t(styleManager.Px(4))}, AutoFlipMode::FlipAvoidTarget);

            if (BeginWindow(ge.windowID, "Gradient Editor", theme.gradientEditorStyleRef, std::move(popupPos),
                            ElementFlags::WindowCanMove | ElementFlags::WindowNoCaption | ElementFlags::Shadows))
            {
                EmitGradientEditor(value);
            }
            else
            {
                ge.triggerID = ID::Invalid();
            }
            EndWindow();
        }
        return editorIsOpen;
    }

    // ---------------------------------------------------------------------------
    // Gradient editor dialog: presets, the bar with its stops, the selected stop,
    // blend space and actions at the left, a stops table at the right; Cancel / OK.
    // ---------------------------------------------------------------------------

    namespace
    {
        // Built-in presets, as stop colors spread evenly.
        struct GradientPreset { std::string_view name; std::initializer_list<uint32_t> colors; bool fade = false; };
        constexpr uint32_t Rgb(uint32_t hex) { return hex; }
        const GradientPreset kGradientPresets[] = {
            { "Grayscale", { Rgb(0x000000), Rgb(0xFFFFFF) } },
            { "Sky",       { Rgb(0x0B1D51), Rgb(0x3B82F6), Rgb(0xA5F3FC) } },
            { "Heat",      { Rgb(0x1A0B00), Rgb(0xB85C10), Rgb(0xFFD27A), Rgb(0xFFFFFF) } },
            { "Viridis",   { Rgb(0x440154), Rgb(0x3B528B), Rgb(0x21918C), Rgb(0x5EC962), Rgb(0xFDE725) } },
            { "Hue",       { Rgb(0xFF0000), Rgb(0xFFFF00), Rgb(0x00FF00), Rgb(0x00FFFF), Rgb(0x0000FF), Rgb(0xFF00FF), Rgb(0xFF0000) } },
            { "Fade",      { Rgb(0x2D3BFF), Rgb(0x2D3BFF) }, true },
        };

        GradientF MakePreset(const GradientPreset& preset, ColorSpace space)
        {
            GradientF g;
            g.colorSpace = space;
            const int n = int(preset.colors.size());
            int i = 0;
            for (uint32_t hex : preset.colors)
            {
                const float a = (preset.fade && i == n - 1) ? 0.0f : 1.0f;
                g.AddStop(color4f{ float((hex >> 16) & 0xFF) / 255.0f, float((hex >> 8) & 0xFF) / 255.0f, float(hex & 0xFF) / 255.0f, a },
                          n > 1 ? float(i) / float(n - 1) : 0.0f);
                ++i;
            }
            return g;
        }

        // Keeps the stops sorted by location; returns where the one that was at `index` went.
        int SortStops(GradientF& g, int index)
        {
            const int count = int(g.stops.size());
            if (index < 0 || index >= count) return index;
            const GradientStop tracked = g.stops[size_t(index)];
            std::stable_sort(g.stops.begin(), g.stops.end(),
                             [](const GradientStop& a, const GradientStop& b) { return a.offset < b.offset; });
            for (int i = 0; i < count; i++)
                if (g.stops[size_t(i)] == tracked) return i;
            return index;
        }

        void RemoveStop(GradientF& g, int index)
        {
            if (g.stops.size() <= 2 || index < 0 || index >= int(g.stops.size())) return;
            g.stops.erase(g.stops.begin() + index);
        }

        // A stop's color as hex, or its numbers when it's brighter than 1; with its alpha
        // when it isn't opaque.
        std::string StopText(const GradientStop& stop)
        {
            const color4f c = stop.color;
            const int alpha = int(std::lround(std::clamp(c.a, 0.0f, 1.0f) * 100.0f));
            char buf[64];
            if (c.r > 1.0f || c.g > 1.0f || c.b > 1.0f)
                std::snprintf(buf, sizeof(buf), "%.2f %.2f %.2f", double(c.r), double(c.g), double(c.b));
            else
            {
                const color4f k = Clamp01(c);
                std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", int(std::lround(k.r * 255.0f)), int(std::lround(k.g * 255.0f)), int(std::lround(k.b * 255.0f)));
            }
            std::string text = buf;
            if (alpha != 100)
                text += " " + std::to_string(alpha) + "%";
            return text;
        }

        std::string Percent(float t)
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.1f%%", double(t) * 100.0);
            return buf;
        }

        constexpr std::string_view kInterpolationNames[] = { "Linear", "Smooth", "Constant" };
    }

    void GUIApplication::EmitGradientEditor(GradientF& outGradient)
    {
        auto& ge = m_gradientEditorSub->State();
        auto& g  = ge.workingCopy;
        if (int(g.stops.size()) > ge.limits.maxStops)
        {
            ReportMisuse("GUIApplication: a gradient with more stops than its field takes (clamped)");
            g.stops.resize(size_t(ge.limits.maxStops));
        }
        const int stopCount = int(g.stops.size());
        if (ge.selectedStop >= stopCount)
            ge.selectedStop = stopCount > 0 ? stopCount - 1 : 0;

        SetGradientPreviewStyle(styleManager, ge.previewGradientStyle, g);
        SetColorStyle(styleManager, ge.stopEdgeColor, theme.GetColor("--text-1", color4f{ 0.91f, 0.92f, 0.94f, 1.0f }));
        SetColorStyle(styleManager, ge.stopOutlineColor, color4f{ 0.0f, 0.0f, 0.0f, 0.5f });
        SetColorStyle(styleManager, ge.stopHaloColor, theme.GetColor("--accent-soft", color4f{ 0.42f, 0.61f, 1.0f, 0.16f }));

        const ColorSwatchLook look = SwatchLook();
        const auto requestClose = [this](GradientEditorState::CloseRequest request) {
            return [this, request]() { m_gradientEditorSub->State().closeRequest = request; };
        };

        // ---- Header ----
        // The header is the dialog's title bar: it drags the window.
        BeginTitleBar(idManager.AllocateDynamicID("ge_header"), theme.dialogHeaderStyleRef, LayoutOverride{}.NoShrink());
        Text(idManager.AllocateDynamicID("ge_title"), "Gradient", theme.dialogTitleStyleRef);
        Text(idManager.AllocateDynamicID("ge_context"), DialogContext(ge.context, "Gradient"), theme.dialogContextStyleRef, LayoutOverride{}.FlexGrow(1));
        Button(idManager.AllocateDynamicID("ge_close"), theme.tabCloseIcon, requestClose(GradientEditorState::CloseRequest::Cancel),
               theme.dialogCloseStyleRef);
        SetTooltip("Close without changing the gradient");
        EndTitleBar();

        // ---- Presets ----
        BeginHorizontal(idManager.AllocateDynamicID("ge_presets"), theme.gradientEditorPresetsStyleRef, LayoutOverride{}.NoShrink());
        {
            Text(idManager.AllocateDynamicID("ge_presets_label"), "Presets", theme.gradientEditorSectionLabelStyleRef);
            // A chip draws its gradient over the checkerboard; the one matching the gradient is ringed.
            auto chip = [&](ID id, const GradientF& preset, std::string_view name)
            {
                NoteSwatchClaim(id);
                auto& style = m_gradientEditorSub->ClaimSwatchStyle(id);
                SetGradientPreviewStyle(styleManager, style, preset);
                GradientF asIs = preset;
                asIs.colorSpace = g.colorSpace;
                const bool current = (asIs == g);
                const ElementRef ref = RegisterControl(Element{ .id = id,
                    .eventHandler = MakeButtonHandler([this, preset]() {
                        auto& ge = m_gradientEditorSub->State();
                        const ColorSpace space = ge.workingCopy.colorSpace;
                        ge.workingCopy = preset;
                        ge.workingCopy.colorSpace = space;
                        if (int(ge.workingCopy.stops.size()) > ge.limits.maxStops)
                            ge.workingCopy.stops.resize(size_t(ge.limits.maxStops));
                        ge.selectedStop = 0;
                    }),
                    .styleRef = theme.gradientEditorPresetStyleRef }, LayoutOverride{});
                SetTooltip(name);
                VERIFY(elementStorage.AddCustomDraw(ref,
                    [look, style, current, accent = theme.accentColor, panel = theme.panelColor,
                     radius = float(styleManager.Px(4)), gap = styleManager.Px(2)](CustomDrawContext& ctx)
                    {
                        const IntRect r = ctx.ElementRect();
                        if (current)
                        {
                            const IntRect ring{ .position = { r.x - 2 * gap, r.y - 2 * gap }, .size = { r.w + 4 * gap, r.h + 4 * gap } };
                            const IntRect gapRect{ .position = { r.x - gap, r.y - gap }, .size = { r.w + 2 * gap, r.h + 2 * gap } };
                            ctx.RenderQuad(ring, accent, radius + float(2 * gap));
                            ctx.RenderQuad(gapRect, panel, radius + float(gap));
                        }
                        ColorSwatchLook chipLook = look;
                        chipLook.radius = radius;
                        DrawCheckerboard(ctx, r, r, chipLook);
                        ctx.RenderQuad(r, style, radius);
                    }));
            };
            const ID builtInRange = idManager.AllocateRangedID(uint32_t(std::size(kGradientPresets)), "ge_preset");
            for (uint32_t i = 0; i < uint32_t(std::size(kGradientPresets)); ++i)
                chip(idManager.GetSubID(builtInRange, i), MakePreset(kGradientPresets[i], g.colorSpace), kGradientPresets[i].name);
            if (!ge.savedPresets.empty())
            {
                const ID savedRange = idManager.AllocateRangedID(uint32_t(ge.savedPresets.size()), "ge_saved_preset");
                for (uint32_t i = 0; i < uint32_t(ge.savedPresets.size()); ++i)
                    chip(idManager.GetSubID(savedRange, i), ge.savedPresets[i], "Saved preset");
            }

            Element spacer{ .id = idManager.AllocateDynamicID("ge_presets_spacer"), .styleRef = theme.emptyHorzStyleRef, .flags = ElementFlags::NoInput };
            RegisterElement(std::move(spacer), LayoutOverride{}.FlexGrow(1));
            Button(idManager.AllocateDynamicID("ge_save_preset"), { theme.addIcon, "Save preset" }, [this]() {
                auto& ge = m_gradientEditorSub->State();
                ge.savedPresets.push_back(ge.workingCopy);
            }, theme.dialogSmallButtonStyleRef);
            SetTooltip("Keep this gradient as a preset");
        }
        EndHorizontal();

        // ---- The bar and its stops ----
        {
            const ID barID = idManager.AllocateDynamicID("ge_bar");
            const int barH    = styleManager.Px(44);
            const int stopBox = styleManager.Px(18);
            const int pointer = styleManager.Px(6);
            const int inset   = styleManager.Px(24);   // the bar's side inset, so the end stops fit
            const int removeDistance = styleManager.Px(40);

            Element bar{};
            bar.id       = barID;
            bar.styleRef = theme.gradientEditorBarStyleRef;
            bar.state    = guiStateManager.GetCurrentState(barID);
            bar.eventHandler = [this, barID, barH, stopBox, pointer, inset, removeDistance](ElementEvent&& event) -> bool
            {
                auto& ge = m_gradientEditorSub->State();
                auto& g  = ge.workingCopy;
                if (!event.rect) return false;
                const IntRect& r = event.rect->rect;
                const int barW = r.w - 2 * inset;
                if (barW <= 0) return false;
                const int2 c = event.LocalCursor(elementStorage);
                const float t = std::clamp(float(c.x - r.x - inset) / float(barW), 0.0f, 1.0f);
                const int top = r.y + (r.h - (barH + pointer + stopBox)) / 2;
                const bool belowBar = c.y >= top + barH;
                const int count = int(g.stops.size());

                auto hitStop = [&]() -> int {
                    if (!belowBar || c.y > top + barH + pointer + stopBox) return -1;
                    for (int i = 0; i < count; i++)
                    {
                        const int sx = r.x + inset + int(std::lround(g.stops[size_t(i)].offset * float(barW)));
                        if (std::abs(c.x - sx) <= stopBox / 2) return i;
                    }
                    return -1;
                };

                switch (event.type)
                {
                    case EventType::MouseDown:
                    {
                        if (!event.state.LeftPressed()) break;
                        guiStateManager.Interaction().SetFocusID(barID);
                        const int hit = hitStop();
                        if (hit >= 0)
                        {
                            // Focused above already; the text editor keeps its own focus.
                            ge.press.Press(event, elementStorage, guiStateManager, { g, ge.selectedStop },
                                           { .atOnce = true, .focus = false });
                            ge.selectedStop  = hit;
                            ge.dragStopIndex = hit;
                            ge.dragGrab      = t - g.stops[size_t(hit)].offset;
                            ge.dragOff       = false;
                            return true;
                        }
                        // A click on the bar adds a stop with the gradient's color there, blending on
                        // as the stop before it did.
                        if (!belowBar && c.y >= top && count < ge.limits.maxStops)
                        {
                            StopInterpolation blend = StopInterpolation::Linear;
                            for (const GradientStop& stop : g.stops)
                                if (stop.offset <= t) blend = stop.interpolation;
                            g.AddStop(g.Sample(t), t, blend);
                            ge.selectedStop = SortStops(g, int(g.stops.size()) - 1);
                            return true;
                        }
                        break;
                    }
                    case EventType::MouseMove:
                        if (ge.dragStopIndex >= 0 && ge.dragStopIndex < count && ge.press.Move(event, 0))
                        {
                            // The cursor in the bar's own units, without the barW clamp's dead zone.
                            const float cursorT = float(c.x - r.x - inset) / float(barW);
                            g.stops[size_t(ge.dragStopIndex)].offset = std::clamp(cursorT - ge.dragGrab, 0.0f, 1.0f);
                            ge.dragStopIndex = ge.selectedStop = SortStops(g, ge.dragStopIndex);
                            ge.dragOff = (c.y > top + barH + pointer + stopBox + removeDistance) && count > 2;
                            return true;
                        }
                        break;
                    case EventType::MouseUp:
                        if (ge.press.Release(event) != PointerDrag::Ended::No)
                        {
                            // Dragged well below the bar: the stop goes.
                            if (ge.dragOff)
                            {
                                RemoveStop(g, ge.dragStopIndex);
                                ge.selectedStop = std::min(ge.dragStopIndex, int(g.stops.size()) - 1);
                            }
                            ge.dragStopIndex = -1;
                            ge.dragOff = false;
                            guiStateManager.Interaction().ReleaseCaptureID();
                            return true;
                        }
                        break;
                    case EventType::KeyDown:
                        // Escape during a drag puts the gradient back.
                        if (ge.press.Cancel(event, guiStateManager))
                        {
                            g = ge.press.before.gradient;
                            ge.selectedStop  = ge.press.before.selectedStop;
                            ge.dragStopIndex = -1;
                            ge.dragOff       = false;
                            return true;
                        }
                        if (event.state.lastKey == KeyCode::Delete && count > 2)
                        {
                            RemoveStop(g, ge.selectedStop);
                            ge.selectedStop = std::min(ge.selectedStop, int(g.stops.size()) - 1);
                            return true;
                        }
                        break;
                    default: break;
                }
                return false;
            };
            const ElementRef barRef = RegisterControl(std::move(bar), LayoutOverride{});

            struct StopDraw { float t; VisualColorStyleIndex color; };
            std::vector<StopDraw> stops;
            for (int i = 0; i < stopCount; i++)
            {
                const ID stopStyleID = idManager.AllocateDynamicID("ge_stop_color");
                NoteSwatchClaim(stopStyleID);
                auto& stopStyle = m_gradientEditorSub->ClaimSwatchStyle(stopStyleID);
                SetColorStyle(styleManager, stopStyle, Clamp01(g.stops[size_t(i)].color));
                stops.push_back({ std::clamp(g.stops[size_t(i)].offset, 0.0f, 1.0f), stopStyle });
            }
            const int selected = ge.selectedStop;
            const bool hideSelected = ge.dragOff;
            const TextID badgeText = textSystem->RegisterText(idManager.AllocateDynamicID("ge_badge_text"),
                Percent(selected < stopCount ? stops[size_t(selected)].t : 0.0f), theme.gradientEditorBadgeStyleRef, ElementState::None);
            const auto badgeSize = textSystem->GetTextMeasurement(badgeText);
            VERIFY(elementStorage.AddCustomDraw(barRef,
                [look, preview = ge.previewGradientStyle, stops = std::move(stops), selected, hideSelected, badgeText,
                 badgeW = badgeSize.maxWidth, badgeH = badgeSize.minHeight,
                 edgeColor = ge.stopEdgeColor, outline = ge.stopOutlineColor, halo = ge.stopHaloColor,
                 accent = theme.accentColor, barH, stopBox, pointer, inset,
                 radius = float(styleManager.Px(4)), border = styleManager.Px(2), haloW = styleManager.Px(3),
                 badgePadX = styleManager.Px(6), badgePadY = styleManager.Px(1), badgeGap = styleManager.Px(2)](CustomDrawContext& ctx)
                {
                    const IntRect r = ctx.ElementRect();
                    const int top = r.y + (r.h - (barH + pointer + stopBox)) / 2;
                    const IntRect barRect{ .position = { r.x + inset, top }, .size = { r.w - 2 * inset, barH } };
                    if (barRect.w <= 0) return;
                    ColorSwatchLook barLook = look;
                    barLook.radius = radius;
                    DrawCheckerboard(ctx, barRect, barRect, barLook);
                    ctx.RenderQuad(barRect, preview, radius);

                    // A stop: a pointer up to the bar and a square of its color, edged; the
                    // selected one in accent with a soft halo and its location under it.
                    auto drawStop = [&](int i) {
                        const bool sel = (i == selected);
                        const int x = barRect.x + int(std::lround(stops[size_t(i)].t * float(barRect.w)));
                        const int y = barRect.y + barRect.h;
                        const VisualColorStyleIndex edge = sel ? accent : edgeColor;
                        ctx.RenderTriangle({ float(x), float(y) }, { float(x + pointer), float(y + pointer) }, { float(x - pointer), float(y + pointer) }, edge);
                        const IntRect box{ .position = { x - stopBox / 2, y + pointer }, .size = { stopBox, stopBox } };
                        const IntRect around{ .position = { box.x - (sel ? haloW : 1), box.y - (sel ? haloW : 1) },
                                              .size = { box.w + 2 * (sel ? haloW : 1), box.h + 2 * (sel ? haloW : 1) } };
                        ctx.RenderQuad(around, sel ? halo : outline, radius + float(sel ? haloW : 1));
                        ctx.RenderQuad(box, edge, radius);
                        const IntRect fill{ .position = { box.x + border, box.y + border }, .size = { box.w - 2 * border, box.h - 2 * border } };
                        ColorSwatchLook stopLook = look;
                        stopLook.radius = radius - float(border);
                        stopLook.checkerSize = fill.w / 2;
                        DrawCheckerboard(ctx, fill, fill, stopLook);
                        ctx.RenderQuad(fill, stops[size_t(i)].color, radius - float(border));
                        if (sel)
                        {
                            const IntRect badge{ .position = { x - badgeW / 2 - badgePadX, box.y + box.h + badgeGap },
                                                 .size = { badgeW + 2 * badgePadX, badgeH + 2 * badgePadY } };
                            ctx.RenderQuad(badge, accent, radius);
                            ctx.RenderText(badgeText, IntRect{ .position = { badge.x + badgePadX, badge.y + badgePadY }, .size = { badgeW, badgeH } });
                        }
                    };
                    const int stopCount = int(stops.size());
                    for (int i = 0; i < stopCount; i++)
                        if (i != selected) drawStop(i);
                    if (selected >= 0 && selected < stopCount && !hideSelected) drawStop(selected);
                }));
        }

        // ---- Below: the selected stop and options at the left, the stops at the right ----
        BeginHorizontal(idManager.AllocateDynamicID("ge_columns"), theme.gradientEditorColumnsStyleRef, LayoutOverride{}.NoShrink());

        BeginVertical(idManager.AllocateDynamicID("ge_left"), theme.gradientEditorColumnStyleRef, LayoutOverride{}.FlexGrow(3));   // room for the six blend spaces
        if (ge.selectedStop >= 0 && ge.selectedStop < int(g.stops.size()))
        {
            // "Color stop  2 / 3", previous, next.
            BeginHorizontal(idManager.AllocateDynamicID("ge_stop_head"), theme.gradientEditorRowStyleRef, LayoutOverride{}.NoShrink());
            Text(idManager.AllocateDynamicID("ge_stop_title"), "Color stop", theme.gradientEditorHeadingStyleRef);
            char count[32];
            std::snprintf(count, sizeof(count), "%d / %d", ge.selectedStop + 1, int(g.stops.size()));
            Text(idManager.AllocateDynamicID("ge_stop_count"), count, theme.gradientEditorCountStyleRef, LayoutOverride{}.FlexGrow(1));
            Button(idManager.AllocateDynamicID("ge_prev"), theme.chevronLeftIcon, [this]() {
                auto& ge = m_gradientEditorSub->State();
                ge.selectedStop = std::max(0, ge.selectedStop - 1);
            }, theme.dialogIconButtonStyleRef);
            SetTooltip("Previous stop");
            Button(idManager.AllocateDynamicID("ge_next"), theme.treeArrowRight, [this]() {
                auto& ge = m_gradientEditorSub->State();
                ge.selectedStop = std::min(int(ge.workingCopy.stops.size()) - 1, ge.selectedStop + 1);
            }, theme.dialogIconButtonStyleRef);
            SetTooltip("Next stop");
            EndHorizontal();

            // Color, then location with duplicate and delete.
            BeginHorizontal(idManager.AllocateDynamicID("ge_color_row"), theme.gradientEditorRowStyleRef, LayoutOverride{}.NoShrink());
            Text(idManager.AllocateDynamicID("ge_color_label"), "Color", theme.gradientEditorLabelStyleRef);
            ge.selectedStopColor = g.stops[size_t(ge.selectedStop)].color;
            if (ColorField(ge.selectedStopColor, ge.limits.hdr, LayoutOverride{}.FlexGrow(1)))
                g.stops[size_t(ge.selectedStop)].color = ge.limits.hdr ? ge.selectedStopColor : Clamp01(ge.selectedStopColor);
            EndHorizontal();

            BeginHorizontal(idManager.AllocateDynamicID("ge_location_row"), theme.gradientEditorRowStyleRef, LayoutOverride{}.NoShrink());
            Text(idManager.AllocateDynamicID("ge_location_label"), "Location", theme.gradientEditorLabelStyleRef);
            BeginFieldBox(idManager.AllocateDynamicID("ge_location_box"), theme.gradientEditorLocationStyleRef, LayoutOverride{}.FlexGrow(1));
            float percent = std::round(g.stops[size_t(ge.selectedStop)].offset * 1000.0f) / 10.0f;
            if (Field(idManager.AllocateDynamicID("ge_location_value"), percent, TextEditBehavior::None, theme.gradientEditorLocationInputStyleRef,
                      NumericFieldConfig<float>{ .stepPerPixel = 0.1, .minVal = 0.0f, .maxVal = 100.0f }, LayoutOverride{}.FlexGrow(1)))
            {
                g.stops[size_t(ge.selectedStop)].offset = std::clamp(percent / 100.0f, 0.0f, 1.0f);
                ge.selectedStop = SortStops(g, ge.selectedStop);
            }
            Text(idManager.AllocateDynamicID("ge_location_unit"), "%", theme.gradientEditorLocationUnitStyleRef);
            EndFieldBox();
            Button(idManager.AllocateDynamicID("ge_duplicate"), theme.duplicateIcon, [this]() {
                auto& ge = m_gradientEditorSub->State();
                auto& g = ge.workingCopy;
                const int count = int(g.stops.size());
                if (count >= ge.limits.maxStops || ge.selectedStop >= count) return;
                // A copy halfway to the next stop (or the previous one at the end).
                const GradientStop stop = g.stops[size_t(ge.selectedStop)];
                const float there = (ge.selectedStop + 1 < count) ? g.stops[size_t(ge.selectedStop + 1)].offset
                                  : (ge.selectedStop > 0 ? g.stops[size_t(ge.selectedStop - 1)].offset : 1.0f);
                g.AddStop(stop.color, (stop.offset + there) * 0.5f, stop.interpolation);
                ge.selectedStop = SortStops(g, int(g.stops.size()) - 1);
            }, theme.dialogSquareButtonStyleRef);
            SetTooltip("Duplicate the stop");
            Button(idManager.AllocateDynamicID("ge_delete"), theme.deleteIcon, [this]() {
                auto& ge = m_gradientEditorSub->State();
                RemoveStop(ge.workingCopy, ge.selectedStop);
                ge.selectedStop = std::min(ge.selectedStop, int(ge.workingCopy.stops.size()) - 1);
            }, theme.dialogDeleteButtonStyleRef);
            SetTooltip("Delete the stop");
            EndHorizontal();

            // How it blends to the next stop (a GradientF's; the last stop has no next).
            if (ge.limits.perStopInterpolation)
            {
                BeginHorizontal(idManager.AllocateDynamicID("ge_blend_row"), theme.gradientEditorRowStyleRef, LayoutOverride{}.NoShrink());
                Text(idManager.AllocateDynamicID("ge_blend_label"), "To next", theme.gradientEditorLabelStyleRef);
                const bool last = ge.selectedStop == int(g.stops.size()) - 1;
                int blend = int(g.stops[size_t(ge.selectedStop)].interpolation);
                if (last) BeginDisabled();
                if (Dropdown(idManager.AllocateDynamicID("ge_blend"), blend, std::span<const std::string_view>(kInterpolationNames),
                             LayoutOverride{}.FlexGrow(1)))
                    g.stops[size_t(ge.selectedStop)].interpolation = StopInterpolation(std::clamp(blend, 0, 2));
                if (last) EndDisabled();
                SetTooltip(last ? "The last stop has no next stop to blend to" : "How the color goes from this stop to the next");
                EndHorizontal();
            }
        }

        Element divider{ .id = idManager.AllocateDynamicID("ge_divider"), .styleRef = theme.gradientEditorDividerStyleRef, .flags = ElementFlags::NoInput };
        RegisterElement(std::move(divider), LayoutOverride{});

        // Blend space.
        BeginHorizontal(idManager.AllocateDynamicID("ge_space_row"), theme.gradientEditorRowStyleRef, LayoutOverride{}.NoShrink());
        Text(idManager.AllocateDynamicID("ge_space_label"), "Blend space", theme.gradientEditorLabelStyleRef);
        {
            struct SpaceTab { ColorSpace space; std::string_view label; };
            constexpr SpaceTab tabs[] = {
                { ColorSpace::sRGB, "sRGB" }, { ColorSpace::LinearRGB, "Linear" }, { ColorSpace::HSL, "HSL" },
                { ColorSpace::HSV, "HSV" }, { ColorSpace::Lab, "Lab" }, { ColorSpace::Oklab, "Oklab" },
            };
            BeginButtonGroup(LayoutOverride{}.NoGrow().NoShrink());
            for (const SpaceTab& tab : tabs)
            {
                const bool active = (g.colorSpace == tab.space);
                if (active) guiStateManager.Construction().PushSelected();
                const ColorSpace target = tab.space;
                Button(tab.label, [this, target]() { m_gradientEditorSub->State().workingCopy.colorSpace = target; });
                if (active) guiStateManager.Construction().PopSelected();
            }
            EndButtonGroup();
        }
        EndHorizontal();

        // Reverse, distribute evenly.
        BeginHorizontal(idManager.AllocateDynamicID("ge_actions"), theme.gradientEditorActionsStyleRef, LayoutOverride{}.NoShrink());
        Button(idManager.AllocateDynamicID("ge_reverse"), { theme.reverseIcon, "Reverse" }, [this]() {
            auto& ge = m_gradientEditorSub->State();
            auto& g = ge.workingCopy;
            const int count = int(g.stops.size());
            // Each stretch keeps its blend: it's now the other stop's to give.
            std::vector<StopInterpolation> blends;
            for (const GradientStop& stop : g.stops)
                blends.push_back(stop.interpolation);
            for (GradientStop& stop : g.stops)
                stop.offset = 1.0f - stop.offset;
            std::reverse(g.stops.begin(), g.stops.end());
            for (int i = 0; i < count; i++)
                g.stops[size_t(i)].interpolation = i + 1 < count ? blends[size_t(count - 2 - i)] : StopInterpolation::Linear;
            ge.selectedStop = count - 1 - ge.selectedStop;
        }, theme.dialogSmallButtonStyleRef);
        Button(idManager.AllocateDynamicID("ge_distribute"), { theme.distributeIcon, "Distribute evenly" }, [this]() {
            auto& g = m_gradientEditorSub->State().workingCopy;
            const int count = int(g.stops.size());
            for (int i = 0; i < count; i++)
                g.stops[size_t(i)].offset = count > 1 ? float(i) / float(count - 1) : 0.0f;
        }, theme.dialogSmallButtonStyleRef);
        EndHorizontal();
        EndVertical();   // left

        // The stops table: type, swatch, value, location; a click selects.
        BeginVertical(idManager.AllocateDynamicID("ge_table"), theme.gradientEditorTableStyleRef, LayoutOverride{}.FlexGrow(2));
        {
            BeginHorizontal(idManager.AllocateDynamicID("ge_table_head"), theme.gradientEditorTableHeadStyleRef, LayoutOverride{}.NoShrink());
            Text(idManager.AllocateDynamicID("ge_th_type"), "Type", theme.gradientEditorTypeCellStyleRef);
            Element blank{ .id = idManager.AllocateDynamicID("ge_th_swatch"), .styleRef = theme.gradientEditorSwatchCellStyleRef, .flags = ElementFlags::NoInput };
            RegisterElement(std::move(blank), LayoutOverride{});
            Text(idManager.AllocateDynamicID("ge_th_value"), "Value", theme.gradientEditorHeadCellStyleRef, LayoutOverride{}.FlexGrow(1));
            Text(idManager.AllocateDynamicID("ge_th_location"), "Location", theme.gradientEditorHeadLocationStyleRef);
            EndHorizontal();

            const int rows = int(g.stops.size());
            const ID rowRange = idManager.AllocateRangedID(uint32_t(std::max<int>(1, rows)), "ge_table_row");
            for (int i = 0; i < rows; i++)
            {
                const ID rowID = idManager.GetSubID(rowRange, uint32_t(i));
                const bool sel = (i == ge.selectedStop);
                if (sel) guiStateManager.Construction().PushSelected();
                RegisterElement(Element{ .id = rowID,
                    .eventHandler = MakeButtonHandler([this, i]() { m_gradientEditorSub->State().selectedStop = i; }),
                    .styleRef = theme.gradientEditorTableRowStyleRef, .state = guiStateManager.GetCurrentState(rowID) },
                    LayoutOverride{}.NoShrink());
                VERIFY(elementStorage.BeginChildren());
                // The type: a color stop, and with per-stop blends how it goes on to the next.
                const std::string_view type = ge.limits.perStopInterpolation && i + 1 < rows
                    ? kInterpolationNames[std::min<size_t>(size_t(g.stops[size_t(i)].interpolation), std::size(kInterpolationNames) - 1)]
                    : std::string_view("Color");
                Text(idManager.AllocateDynamicID("ge_td_type"), type, theme.gradientEditorTypeCellStyleRef);
                const ID swID = idManager.AllocateDynamicID("ge_td_swatch");
                NoteSwatchClaim(swID);
                auto& swStyle = m_gradientEditorSub->ClaimSwatchStyle(swID);
                SetColorStyle(styleManager, swStyle, Clamp01(g.stops[size_t(i)].color));
                Element sw{ .id = swID, .styleRef = theme.gradientEditorSwatchCellStyleRef, .flags = ElementFlags::NoInput };
                const ElementRef swRef = RegisterElement(std::move(sw), LayoutOverride{});
                VERIFY(elementStorage.AddCustomDraw(swRef, [look, swStyle, radius = float(styleManager.Px(3))](CustomDrawContext& ctx) {
                    const IntRect r = ctx.ElementInnerRect();
                    ColorSwatchLook l = look;
                    l.radius = radius;
                    l.checkerSize = r.h / 2;
                    DrawCheckerboard(ctx, r, r, l);
                    ctx.RenderQuad(r, swStyle, radius);
                }));
                Text(idManager.AllocateDynamicID("ge_td_value"), StopText(g.stops[size_t(i)]), theme.gradientEditorValueCellStyleRef, LayoutOverride{}.FlexGrow(1));
                Text(idManager.AllocateDynamicID("ge_td_location"), Percent(g.stops[size_t(i)].offset), theme.gradientEditorLocationCellStyleRef);
                VERIFY(elementStorage.EndChildren());
                if (sel) guiStateManager.Construction().PopSelected();
            }
        }
        EndVertical();
        EndHorizontal();   // columns

        // ---- Footer ----
        BeginHorizontal(idManager.AllocateDynamicID("ge_footer"), theme.dialogFooterStyleRef, LayoutOverride{}.NoShrink());
        {
            char status[96];
            std::snprintf(status, sizeof(status), "%d stops \xC2\xB7 click the bar to add, drag a stop off to remove", int(g.stops.size()));
            Text(idManager.AllocateDynamicID("ge_status"), status, theme.dialogStatusStyleRef, LayoutOverride{}.FlexGrow(1));
            Button(idManager.AllocateDynamicID("ge_cancel"), "Cancel", requestClose(GradientEditorState::CloseRequest::Cancel),
                   theme.dialogButtonStyleRef);
            Button(idManager.AllocateDynamicID("ge_ok"), "OK", requestClose(GradientEditorState::CloseRequest::Ok),
                   theme.dialogPrimaryButtonStyleRef);
        }
        EndHorizontal();

        outGradient = g;
    }
}
