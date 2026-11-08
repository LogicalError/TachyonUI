#include <gui_controls/gui_application_internal.h>

#include <gui_core/graphics/gui_custom_draw_context.h>
#include <gui_core/graphics/gui_renderer.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace tachyon
{

    // ---------------------------------------------------------------------------
    // Progress bars, spinners, circular progress, skeletons and the busy button.
    // ---------------------------------------------------------------------------

    namespace
    {
        constexpr float kSlideSecs   = 1.2f;    // a sliding segment's trip along its bar
        constexpr float kSegmentPart = 0.26f;   // the segment's length, of its bar's
        constexpr float kSpinSecs    = 0.8f;    // a spinner's turn
        constexpr float kShineSecs   = 1.5f;    // a skeleton shine's trip across

        // How far through a loop of `loopSecs` an animation is at `timeSecs`: 0 to 1.
        float LoopPhase(float timeSecs, float loopSecs)
        {
            const float loops = timeSecs / loopSecs;
            return loops - std::floor(loops);
        }

        float Clamped01(float value)
        {
            return std::isnan(value) ? 0.0f : std::clamp(value, 0.0f, 1.0f);
        }

        // `rect` without `inset` pixels at either end: a bar's straight part, between its
        // round ends.
        IntRect InsetEnds(IntRect rect, int32_t inset)
        {
            inset = std::clamp(inset, 0, rect.w / 2);
            return IntRect{ rect.x + inset, rect.y, rect.w - 2 * inset, rect.h };
        }

        // A spinner's size, the radius of its ring and the ring's width, in CSS px.
        struct SpinnerMetrics
        {
            int32_t size;
            float   radius;
            float   width;
        };

        SpinnerMetrics MetricsOf(SpinnerSize size)
        {
            switch (size)
            {
                case SpinnerSize::Small: return { 12, 4.5f, 1.5f };
                case SpinnerSize::Large: return { 24, 10.0f, 2.5f };
                default:                 return { 16, 6.5f, 2.0f };
            }
        }
    }

    void GUIApplication::ProgressBar(ID id, float value, bool failed, StyleRef style, LayoutOverride&& constraints)
    {
        const ElementRef track = RegisterElement(Element{ .id = id, .styleRef = style, .flags = ElementFlags::NoInput },
                                                 std::move(constraints));
        const float done = Clamped01(value);
        const VisualBoxStyleIndex fill = (failed ? theme.progressFillFailedStyleRef : theme.progressFillStyleRef).visualBoxStyleIndex;
        VERIFY(elementStorage.AddCustomDraw(track, [done, fill](CustomDrawContext& ctx)
        {
            IntRect rect = ctx.ElementRect();
            rect.w = int32_t(float(rect.w) * done + 0.5f);
            if (rect.w > 0)
                ctx.RenderBox(rect, fill);
        }));
    }

    void GUIApplication::IndeterminateProgressBar(ID id, StyleRef style, LayoutOverride&& constraints)
    {
        const ElementRef track = RegisterElement(Element{ .id = id, .styleRef = style,
                                                          .flags = ElementFlags::NoInput | ElementFlags::Animated },
                                                 std::move(constraints));
        const float phase = LoopPhase(guiStateManager.Interaction().GetFrameTime(), kSlideSecs);
        const VisualBoxStyleIndex segment = theme.progressSegmentStyleRef.visualBoxStyleIndex;
        VERIFY(elementStorage.AddCustomDraw(track, [phase, segment](CustomDrawContext& ctx)
        {
            // From just before the bar's start to just past its end, drawn only over the
            // bar's straight part, so its round ends stay the track's.
            const IntRect bar    = ctx.ElementRect();
            const int32_t length = std::max(1, int32_t(float(bar.w) * kSegmentPart + 0.5f));
            const IntRect piece{ bar.x - length + int32_t(float(bar.w + length) * phase + 0.5f), bar.y, length, bar.h };
            ctx.RenderBox(piece, InsetEnds(bar, bar.h / 2), segment);
        }));
    }

    void GUIApplication::Spinner(ID id, SpinnerSize size, LayoutOverride&& constraints)
    {
        const SpinnerMetrics metrics = MetricsOf(size);
        const int32_t side = styleManager.Px(metrics.size);
        if (!constraints.HasFixedWidth())
            constraints.Width(Pixels{ side });
        if (!constraints.HasFixedHeight())
            constraints.Height(Pixels{ side });
        const ElementRef ref = RegisterElement(Element{ .id = id, .styleRef = theme.emptyHorzStyleRef,
                                                        .flags = ElementFlags::NoInput | ElementFlags::Animated },
                                               std::move(constraints));
        // The arc's start, in turns clockwise from the right; it starts at the top.
        const float start = LoopPhase(guiStateManager.Interaction().GetFrameTime(), kSpinSecs) - 0.25f;
        const VisualColorStyleIndex track = theme.progressTrackColor, arc = theme.progressArcColor;
        VERIFY(elementStorage.AddCustomDraw(ref, [metrics, start, track, arc](CustomDrawContext& ctx)
        {
            // Scaled to the room it has, so a spinner given another size keeps its shape.
            const IntRect inner = ctx.ElementInnerRect();
            const float   scale = float(std::min(inner.w, inner.h)) / float(metrics.size);
            if (!(scale > 0.0f))
                return;
            const float2 center{ float(inner.x) + float(inner.w) * 0.5f, float(inner.y) + float(inner.h) * 0.5f };
            const float  radius = metrics.radius * scale, width = metrics.width * scale;
            ctx.RenderCircle(center, radius, LineStyle{ .colorStyleIndex = track, .lineWidth = width });
            ctx.RenderArc(center, radius, width, start, 0.25f, arc);
        }));
    }

    void GUIApplication::CircularProgress(ID id, float value, StyleRef style, LayoutOverride&& constraints)
    {
        const float done = Clamped01(value);
        // Whole percents, rounded down: 100 only once it's done.
        const std::string percent = std::to_string(int(std::floor(done * 100.0f)));
        const ElementRef ref = RegisterElement(Element{ .id = id, .styleRef = style, .flags = ElementFlags::NoInput },
                                               percent, std::move(constraints));
        const VisualColorStyleIndex track = theme.progressTrackColor, arc = theme.progressArcColor;
        VERIFY(elementStorage.AddCustomDraw(ref, [done, track, arc](CustomDrawContext& ctx)
        {
            // A 3px ring of radius 17 in 40px, scaled to the room it has.
            const IntRect inner = ctx.ElementInnerRect();
            const float   scale = float(std::min(inner.w, inner.h)) / 40.0f;
            if (!(scale > 0.0f))
                return;
            const float2 center{ float(inner.x) + float(inner.w) * 0.5f, float(inner.y) + float(inner.h) * 0.5f };
            const float  radius = 17.0f * scale, width = 3.0f * scale;
            ctx.RenderCircle(center, radius, LineStyle{ .colorStyleIndex = track, .lineWidth = width });
            if (done > 0.0f)
                ctx.RenderArc(center, radius, width, -0.25f, done, arc);
        }));
    }

    void GUIApplication::Skeleton(ID id, StyleRef style, LayoutOverride&& constraints)
    {
        const bool shines = !IsReducedMotion();
        const ElementRef ref = RegisterElement(Element{ .id = id, .styleRef = style,
                                                        .flags = ElementFlags::NoInput | (shines ? ElementFlags::Animated : ElementFlags::None) },
                                               std::move(constraints));
        if (!shines)
            return;
        const float phase = LoopPhase(guiStateManager.Interaction().GetFrameTime(), kShineSecs);
        const VisualBoxStyleIndex shine = theme.skeletonShineStyleRef.visualBoxStyleIndex;
        VERIFY(elementStorage.AddCustomDraw(ref, [phase, shine](CustomDrawContext& ctx)
        {
            // A band half the bar long (a short bar's at least four times its height) crossing
            // from before its start to past its end, over its straight part only.
            const IntRect bar  = ctx.ElementRect();
            const int32_t band = std::max(bar.w / 2, bar.h * 4);
            const IntRect piece{ bar.x - band + int32_t(float(bar.w + band) * phase + 0.5f), bar.y, band, bar.h };
            ctx.RenderBox(piece, InsetEnds(bar, bar.h / 2), shine);
        }));
    }

    bool GUIApplication::BusyButton(ID id, const GUIContent& content, bool busy, std::string_view busyText,
                                    std::function<void()> onClick, StyleRef style, LayoutOverride&& constraints)
    {
        if (!busy)
        {
            const bool clicked = Button(id, content, std::move(onClick), style, std::move(constraints));
            // Its width, kept once it's busy.
            if (const ElementRef ref = elementStorage.FindElementRefByID(id); ref.IsValid())
                VERIFY(elementStorage.AddPostLayout(ref, [this, id](PostLayoutContext& ctx) { m_busyButtonWidths[id] = ctx.ElementRect().w; }));
            return clicked;
        }

        // Exactly as wide as it was, not grown or shrunk by the room around it.
        if (const auto width = m_busyButtonWidths.find(id); width != m_busyButtonWidths.end() && !constraints.HasFixedWidth())
            constraints.Width(Pixels{ width->second }).FlexGrow(0).FlexShrink(0);
        const IconID           icon = theme.SpinnerIcon(guiStateManager.Interaction().GetFrameTime());
        const std::string_view text = busyText.empty() ? content.text : busyText;
        ShowContentTooltip(id, content);
        auto* group = m_buttonGroup->GetActiveButton();
        if (group) style = m_buttonGroup->NextItemStyleRef(*group);
        // Hovered and focused as a button is, but a click does nothing.
        const ElementRef ref = RegisterControl(
            Element{ .id = id, .eventHandler = MakeButtonHandler({}), .styleRef = style, .flags = ElementFlags::Animated },
            [this, icon, text](Element& element) {
                return textSystem->RegisterText(element.id, icon, text, element.styleRef, element.state);
            },
            std::move(constraints));
        if (group && group->itemCount == 1)
            group->firstRef = ref;
        return false;
    }

} // namespace tachyon
