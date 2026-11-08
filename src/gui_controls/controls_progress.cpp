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

        // Whole percents, rounded down, so 100 shows only once it's done.
        int WholePercent(float value)
        {
            return int(std::floor(Clamped01(value) * 100.0f));
        }

        // What a task shows beside its name: its note, else its percent while it runs (and
        // the value is known), else "paused" while paused.
        std::string StateText(const ProgressTask& task)
        {
            if (!task.note.empty())
                return std::string(task.note);
            if (task.state == TaskState::Paused)
                return "paused";
            if (task.state == TaskState::Running && task.value >= 0.0f)
                return std::to_string(WholePercent(task.value)) + "%";
            return {};
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
        TaskBar(id, ProgressTask{ .value = Clamped01(value), .state = failed ? TaskState::Failed : TaskState::Running },
                style, std::move(constraints));
    }

    void GUIApplication::TaskBar(ID id, const ProgressTask& task, StyleRef trackStyle, LayoutOverride&& constraints)
    {
        // Under way with no telling how far: the sliding segment.
        if (task.state == TaskState::Running && task.value < 0.0f)
        {
            IndeterminateProgressBar(id, trackStyle, std::move(constraints));
            return;
        }
        const ElementRef track = RegisterElement(Element{ .id = id, .styleRef = trackStyle, .flags = ElementFlags::NoInput },
                                                 std::move(constraints));
        const float done = task.state == TaskState::Done ? 1.0f : Clamped01(task.value);
        const StyleRef fillStyle = task.state == TaskState::Failed ? theme.progressFillFailedStyleRef
                                 : task.state == TaskState::Paused ? theme.progressFillPausedStyleRef
                                                                   : theme.progressFillStyleRef;
        const VisualBoxStyleIndex fill = fillStyle.visualBoxStyleIndex;
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
        const std::string percent = std::to_string(WholePercent(done));
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

    // ---------------------------------------------------------------------------
    // Built from those: a task in full, the steps of a job, a task in a status bar, and
    // the pill and popover listing the tasks running in the background.
    // ---------------------------------------------------------------------------

    void GUIApplication::TaskProgress(ID id, const ProgressTask& task, LayoutOverride&& constraints)
    {
        BeginVertical(id, theme.taskProgressStyleRef, std::move(constraints));
        [[maybe_unused]] const bool pushed = idManager.PushParent(id);

        BeginHorizontal(idManager.AllocateDynamicID("header"), theme.taskProgressHeaderStyleRef);
        const ID nameID = idManager.AllocateDynamicID("name");
        const bool failed = task.state == TaskState::Failed, done = task.state == TaskState::Done;
        if (failed || done)
        {
            const StyleRef nameStyle = failed ? theme.taskProgressFailedStyleRef : theme.taskProgressDoneStyleRef;
            const IconID   icon      = failed ? theme.invalidIcon : theme.menuCheckIcon;
            RegisterElement(Element{ .id = nameID,
                                     .textID = textSystem->RegisterText(nameID, icon, task.name, nameStyle, ElementState::None),
                                     .styleRef = nameStyle, .flags = ElementFlags::NoInput },
                            LayoutOverride{});
        }
        else
            Text(nameID, task.name, theme.taskProgressNameStyleRef);
        if (failed)
        {
            if (task.onRetry)
                Button(idManager.AllocateDynamicID("retry"), "Retry", task.onRetry, theme.taskProgressRetryStyleRef);
        }
        else if (const std::string state = StateText(task); !state.empty())
            Text(idManager.AllocateDynamicID("state"), state,
                 task.note.empty() ? theme.taskProgressPercentStyleRef : theme.taskProgressNoteStyleRef);
        if (task.onCancel && !failed && !done)
        {
            Button(idManager.AllocateDynamicID("cancel"), theme.tabCloseIcon, task.onCancel, theme.taskProgressCancelStyleRef);
            SetTooltip("Cancel");
        }
        EndHorizontal();

        // Done, it's one line: the check, the name and how long it took.
        if (!done)
        {
            TaskBar(idManager.AllocateDynamicID("bar"), task, theme.progressBarStyleRef, LayoutOverride{});
            if (!task.detail.empty())
                Text(idManager.AllocateDynamicID("detail"), task.detail,
                     failed ? theme.taskProgressErrorStyleRef : theme.taskProgressDetailStyleRef);
        }

        if (pushed)
            (void)idManager.PopParent();
        EndVertical();
    }

    void GUIApplication::StepGlyph(ID id, TaskState state)
    {
        if (state == TaskState::Running)
        {
            Spinner(id, SpinnerSize::Medium);
            return;
        }
        const StyleRef style = state == TaskState::Done   ? theme.progressStepDoneStyleRef
                             : state == TaskState::Failed ? theme.progressStepFailedStyleRef
                             : state == TaskState::Paused ? theme.progressStepPausedStyleRef
                                                          : theme.progressStepWaitingStyleRef;
        const IconID icon = state == TaskState::Done   ? theme.boldCheckIcon
                          : state == TaskState::Failed ? theme.boldCrossIcon
                                                       : IconID::Invalid;
        RegisterElement(Element{ .id = id, .iconID = icon, .styleRef = style, .flags = ElementFlags::NoInput }, LayoutOverride{});
    }

    void GUIApplication::ProgressSteps(ID id, std::span<const ProgressTask> steps, LayoutOverride&& constraints)
    {
        BeginVertical(id, theme.progressStepsStyleRef, std::move(constraints));
        [[maybe_unused]] const bool pushed = idManager.PushParent(id);
        const ID rowsID = steps.empty() ? ID::Invalid() : idManager.AllocateRangedID(uint32_t(steps.size()), "steps");
        for (size_t i = 0; i < steps.size(); ++i)
        {
            const ProgressTask& step = steps[i];
            const ID rowID = idManager.GetSubID(rowsID, uint32_t(i));
            BeginHorizontal(rowID, theme.progressStepStyleRef);
            [[maybe_unused]] const bool inRow = idManager.PushParent(rowID);

            StepGlyph(idManager.AllocateDynamicID("glyph"), step.state);
            BeginVertical(idManager.AllocateDynamicID("body"), theme.progressStepBodyStyleRef);
            Text(idManager.AllocateDynamicID("name"), step.name,
                 step.state == TaskState::Pending ? theme.progressStepPendingStyleRef : theme.progressStepNameStyleRef);
            // A small bar under the step under way.
            if (step.state == TaskState::Running)
                TaskBar(idManager.AllocateDynamicID("bar"), step, theme.progressBarSmallStyleRef, LayoutOverride{});
            EndVertical();
            if (const std::string state = StateText(step); !state.empty())
                Text(idManager.AllocateDynamicID("state"), state, theme.taskProgressNoteStyleRef);

            if (inRow)
                (void)idManager.PopParent();
            EndHorizontal();
        }
        if (pushed)
            (void)idManager.PopParent();
        EndVertical();
    }

    void GUIApplication::StatusProgress(ID id, const ProgressTask& task, LayoutOverride&& constraints)
    {
        BeginHorizontal(id, theme.statusProgressStyleRef, std::move(constraints));
        [[maybe_unused]] const bool pushed = idManager.PushParent(id);
        if (task.state == TaskState::Running)
            Spinner(idManager.AllocateDynamicID("spinner"), SpinnerSize::Small);
        StatusText(idManager.AllocateDynamicID("name"), task.name, theme.statusTextStyleRef);
        TaskBar(idManager.AllocateDynamicID("bar"), task, theme.progressBarSmallStyleRef, LayoutOverride{}.Width(Pixels{ styleManager.Px(120) }));
        if (const std::string state = StateText(task); !state.empty())
            StatusText(idManager.AllocateDynamicID("state"), state, theme.statusTextMonoStyleRef);
        if (pushed)
            (void)idManager.PopParent();
        EndHorizontal();
    }

    bool GUIApplication::BackgroundTasks(ID id, std::span<const ProgressTask> tasks, LayoutOverride&& constraints)
    {
        TasksPopover& popover = m_tasksPopovers[id];
        if (tasks.empty())
        {
            popover.open = false;
            return false;
        }
        if (popover.windowID == ID::Invalid())
            popover.windowID = idManager.CreateStaticID();

        // Escape, with the keyboard on the pill, closes it.
        if (popover.open && TakeEscapeFor(popover.windowID, id))
            popover.open = false;

        const std::string label = tasks.size() == 1 ? "1 task" : std::to_string(tasks.size()) + " tasks";
        Button(id, label, [this, id] { m_tasksPopovers[id].open = !m_tasksPopovers[id].open; }, theme.tasksPillStyleRef,
               std::move(constraints));
        if (!popover.open)
            return false;
        // A press anywhere but on it or the pill closes it; the pill toggles it itself.
        CloseOnPressOutside(popover.windowID, id, [this, id] { m_tasksPopovers[id].open = false; });

        // Above the pill, its right edge on the pill's: a status bar is at the bottom. It
        // takes no focus, so the keyboard stays where it was.
        auto place = LayoutOverride{}.Anchored(id, AnchorPlacement::OutsideTop | AnchorPlacement::InsideRight,
                                               { 0, int16_t(-styleManager.Px(4)) }, AutoFlipMode::FlipAvoidTarget);
        const ElementFlags flags = ElementFlags::WindowNoCaption | ElementFlags::WindowIsPopup
                                 | ElementFlags::Shadows | ElementFlags::WindowNotFocusable;
        if (BeginWindow(popover.windowID, theme.tasksPopoverStyleRef, std::move(place), flags))
        {
            [[maybe_unused]] const bool pushed = idManager.PushParent(popover.windowID);
            const auto running = std::count_if(tasks.begin(), tasks.end(),
                                               [](const ProgressTask& t) { return t.state == TaskState::Running; });
            const ID headerID = idManager.AllocateDynamicID("header");
            BeginHorizontal(headerID, theme.tasksPopoverHeaderStyleRef);
            Text(idManager.AllocateDynamicID("title"), "Background tasks", theme.tasksPopoverTitleStyleRef);
            Text(idManager.AllocateDynamicID("running"), std::to_string(running) + " running", theme.tasksPopoverCountStyleRef);
            EndHorizontal();

            const ID rowsID = idManager.AllocateRangedID(uint32_t(tasks.size()), "tasks");
            for (size_t i = 0; i < tasks.size(); ++i)
            {
                const ProgressTask& task = tasks[i];
                const ID rowID = idManager.GetSubID(rowsID, uint32_t(i));
                BeginHorizontal(rowID, theme.tasksRowStyleRef);
                [[maybe_unused]] const bool inRow = idManager.PushParent(rowID);
                BeginVertical(idManager.AllocateDynamicID("body"), theme.tasksRowBodyStyleRef);
                Text(idManager.AllocateDynamicID("name"), task.name, theme.taskProgressNameStyleRef);
                TaskBar(idManager.AllocateDynamicID("bar"), task, theme.progressBarSmallStyleRef, LayoutOverride{});
                EndVertical();
                std::string state = StateText(task);
                if (state.empty())
                    state = task.state == TaskState::Failed ? "failed" : task.state == TaskState::Done ? "done" : "";
                Text(idManager.AllocateDynamicID("state"), state, theme.tasksRowStateStyleRef);
                if (task.onCancel && (task.state == TaskState::Running || task.state == TaskState::Paused || task.state == TaskState::Pending))
                {
                    Button(idManager.AllocateDynamicID("cancel"), theme.tabCloseIcon, task.onCancel, theme.taskProgressCancelStyleRef);
                    SetTooltip("Cancel " + std::string(task.name));
                }
                if (inRow)
                    (void)idManager.PopParent();
                EndHorizontal();
            }
            if (pushed)
                (void)idManager.PopParent();
        }
        else
            popover.open = false;
        EndWindow();
        return popover.open;
    }

} // namespace tachyon
