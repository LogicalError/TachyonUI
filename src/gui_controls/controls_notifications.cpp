#include <gui_controls/gui_application_internal.h>

#include <gui_core/graphics/gui_custom_draw_context.h>
#include <gui_core/gui_easing.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace tachyon
{

    // ---------------------------------------------------------------------------
    // Toasts in a window's corner, and banners across the top of a panel.
    // ---------------------------------------------------------------------------

    namespace
    {
        constexpr float kToastHideSecs = 6.0f;
        constexpr float kToastInSecs   = 0.18f;
        constexpr float kToastOutSecs  = 0.14f;
        constexpr int   kToastsShown   = 4;      // the rest fold into "+N earlier"
        constexpr int   kToastWidth    = 380;    // CSS px, as .toast sets: how far it slides
        constexpr int   kToastInset    = 16;     // from the window's edges, CSS px

        IconID SeverityIcon(const GUITheme& theme, Severity severity)
        {
            switch (severity)
            {
                case Severity::Success: return theme.menuCheckIcon;
                case Severity::Warning: return theme.alertIcon;
                case Severity::Error:   return theme.errorIcon;
                default:                return theme.infoIcon;
            }
        }

        // How long ago, as a toast says it: "now", "4 min", "2 h".
        std::string Ago(float secs)
        {
            if (secs < 60.0f)
                return "now";
            if (secs < 3600.0f)
                return std::to_string(int(secs / 60.0f)) + " min";
            return std::to_string(int(secs / 3600.0f)) + " h";
        }

        // Seconds until Ago(secs) says something else: the next minute, or hour.
        float UntilAgoChanges(float secs)
        {
            const float unit = secs < 3600.0f ? 60.0f : 3600.0f;
            return unit - std::fmod(std::max(0.0f, secs), unit);
        }

        // It goes by itself: not an error, and no progress bar short of the end.
        bool HidesItself(const ToastDesc& desc)
        {
            return desc.severity != Severity::Error && (!desc.progress || *desc.progress >= 1.0f);
        }
    }

    ToastID GUIApplication::ShowToast(ToastDesc desc)
    {
        ShownToast toast;
        toast.handle  = ++m_nextToast;
        toast.desc    = std::move(desc);
        toast.id      = idManager.CreateStaticID();
        toast.shownAt = guiStateManager.Interaction().GetFrameTime();
        m_toasts.push_back(std::move(toast));
        RequestRedraw();   // shown from a handler or outside a frame: drawn without waiting for input
        return m_toasts.back().handle;
    }

    ToastDesc* GUIApplication::FindToast(ToastID toast)
    {
        for (ShownToast& shown : m_toasts)
            if (shown.handle == toast && shown.leftAt < 0.0f)
                return &shown.desc;
        return nullptr;
    }

    void GUIApplication::DismissToast(ToastID toast)
    {
        for (ShownToast& shown : m_toasts)
            if (shown.handle == toast && shown.leftAt < 0.0f)
            {
                shown.leftAt = guiStateManager.Interaction().GetFrameTime();
                RequestRedraw();   // its slide out
            }
    }

    void GUIApplication::Toasts()
    {
        const float now = guiStateManager.Interaction().GetFrameTime();
        const float dt  = m_toastsTime < 0.0f ? 0.0f : std::max(0.0f, now - m_toastsTime);
        m_toastsTime = now;
        const bool reduced = IsReducedMotion();

        if (m_toastStackID == ID::Invalid())
            m_toastStackID = idManager.CreateStaticID();
        const bool hovered = (guiStateManager.GetCurrentState(m_toastStackID) & ElementState::Hover) != ElementState::None;

        // Gone once slid out; the ones that go by themselves count down while not hovered.
        for (size_t i = m_toasts.size(); i-- > 0; )
        {
            ShownToast& toast = m_toasts[i];
            if (toast.leftAt >= 0.0f && (reduced || now - toast.leftAt >= kToastOutSecs))
            {
                idManager.ReleaseStaticID(toast.id);
                m_toasts.erase(m_toasts.begin() + ptrdiff_t(i));
                continue;
            }
            if (toast.leftAt < 0.0f && HidesItself(toast.desc))
            {
                if (!hovered)
                    toast.shownFor += dt;
                if (toast.shownFor >= kToastHideSecs)
                    toast.leftAt = now;
                else if (!hovered)
                    ScheduleFrameAt(now + (kToastHideSecs - toast.shownFor));   // time to go
            }
            // Its age, by the minute.
            if (toast.leftAt < 0.0f)
                ScheduleFrameAt(now + UntilAgoChanges(now - toast.shownAt + std::max(0.0f, toast.desc.age)));
        }
        if (m_toasts.empty())
        {
            m_toastsExpanded = false;
            return;
        }

        // The newest nearest the corner: the stack is oldest first, from the bottom up.
        const int count = int(m_toasts.size());
        const int folded = m_toastsExpanded ? 0 : std::max(0, count - kToastsShown);
        if (folded == 0 && count <= kToastsShown)
            m_toastsExpanded = false;

        const ID windowID = windowRegistry.GetCurrentWindowID();
        const int32_t inset = styleManager.Px(kToastInset);
        const ElementRef stackRef = RegisterElement(
            tachyon::Element{ .id = m_toastStackID, .styleRef = theme.toastStackStyleRef,
                              .flags = ElementFlags::PropagatesDescendantState,
                              .state = guiStateManager.GetCurrentState(m_toastStackID) },
            LayoutOverride{}.Anchored(windowID, AnchorPlacement::InsideRight | AnchorPlacement::InsideBottom, -inset, -inset));
        VERIFY(elementStorage.BeginChildren());
        [[maybe_unused]] const bool pushed = idManager.PushParent(m_toastStackID);

        if (folded > 0)
            Button(idManager.AllocateDynamicID("earlier"), "+" + std::to_string(folded) + " earlier",
                   [this] { m_toastsExpanded = true; }, theme.toastEarlierStyleRef, LayoutOverride{}.NoGrow().NoShrink());

        for (int i = folded; i < count; ++i)
        {
            ShownToast& toast = m_toasts[size_t(i)];
            const ToastDesc& desc = toast.desc;
            const ToastID handle = toast.handle;

            // It slides in from the right, and out again when it goes.
            float shown = 1.0f;
            if (!reduced)
            {
                shown = std::clamp((now - toast.shownAt) / kToastInSecs, 0.0f, 1.0f);
                if (toast.leftAt >= 0.0f)
                    shown = std::min(shown, 1.0f - std::clamp((now - toast.leftAt) / kToastOutSecs, 0.0f, 1.0f));
            }
            const float eased = Easing::EaseOut(shown);
            const int32_t width = styleManager.Px(kToastWidth);   // .toast's
            LayoutOverride place = LayoutOverride{}.NoGrow().NoShrink();
            place.x = LayoutOverride{}.Relative(static_cast<int16_t>((1.0f - eased) * float(width + inset)), 0).x;

            RegisterElement(tachyon::Element{ .id = toast.id, .styleRef = theme.toastStyleRef,
                                              .state = guiStateManager.GetCurrentState(toast.id) }, std::move(place));
            VERIFY(elementStorage.BeginChildren());
            [[maybe_unused]] const bool inToast = idManager.PushParent(toast.id);

            const int severity = std::clamp(int(desc.severity), 0, 3);
            RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID("circle"), .iconID = SeverityIcon(theme, desc.severity),
                                              .styleRef = theme.toastCircleStyleRefs[severity], .flags = ElementFlags::NoInput },
                            LayoutOverride{});

            BeginVertical(idManager.AllocateDynamicID("content"), theme.toastContentStyleRef, LayoutOverride{}.FlexGrow(1));
            BeginHorizontal(idManager.AllocateDynamicID("header"), theme.toastHeaderStyleRef);
            const ID titleID = idManager.AllocateDynamicID("title");
            Text(titleID, desc.title, theme.toastTitleStyleRef, LayoutOverride{}.FlexGrow(1));
            Text(idManager.AllocateDynamicID("time"), Ago(now - toast.shownAt + std::max(0.0f, desc.age)), theme.toastTimeStyleRef);
            EndHorizontal();
            // The title on one line, all of it in its tooltip; the body on as many as it takes.
            SetTooltip(titleID, desc.title);
            if (!desc.body.empty())
                Text(idManager.AllocateDynamicID("body"), desc.body,
                     desc.monospaceBody ? theme.toastBodyMonoStyleRef : theme.toastBodyStyleRef);
            if (desc.progress)
            {
                const ElementRef track = RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID("progress"),
                                                                           .styleRef = theme.toastProgressStyleRef,
                                                                           .flags = ElementFlags::NoInput }, LayoutOverride{});
                const float done = std::clamp(*desc.progress, 0.0f, 1.0f);
                const VisualBoxStyleIndex fill = theme.toastProgressFillStyleRef.visualBoxStyleIndex;
                VERIFY(elementStorage.AddCustomDrawAfter(track, [done, fill](CustomDrawContext& ctx)
                {
                    IntRect rect = ctx.ElementRect();
                    rect.w = int32_t(float(rect.w) * done + 0.5f);
                    if (rect.w > 0)
                        ctx.RenderBox(rect, fill);
                }));
            }
            if (!desc.actions.empty())
            {
                BeginHorizontal(idManager.AllocateDynamicID("actions"), theme.toastActionsStyleRef);
                for (size_t a = 0; a < desc.actions.size(); ++a)
                {
                    // An action does its thing, and the toast goes.
                    auto onClick = desc.actions[a].onClick;
                    Button(idManager.AllocateDynamicID("action"), desc.actions[a].label,
                           [this, handle, onClick] { if (onClick) onClick(); DismissToast(handle); },
                           a == 0 ? theme.toastActionStyleRef : theme.toastActionSecondaryStyleRef);
                }
                EndHorizontal();
            }
            EndVertical();

            Button(idManager.AllocateDynamicID("dismiss"), theme.tabCloseIcon, [this, handle] { DismissToast(handle); },
                   theme.toastDismissStyleRef);
            SetTooltip("Dismiss");

            if (inToast)
                (void)idManager.PopParent();
            VERIFY(elementStorage.EndChildren());
        }

        if (pushed)
            (void)idManager.PopParent();
        VERIFY(elementStorage.EndChildren());
        // Over everything else in the window, once it's all built.
        m_toastsBuilt = elementStorage.IsValidRef(stackRef);
    }

    void GUIApplication::Banner(ID id, Severity severity, const GUIContent& message, std::span<const BannerAction> actions,
                                LayoutOverride&& constraints)
    {
        const int s = std::clamp(int(severity), 0, 3);
        BeginHorizontal(id, theme.bannerStyleRefs[s], std::move(constraints));
        [[maybe_unused]] const bool pushed = idManager.PushParent(id);
        const IconID icon = message.icon != IconID::Invalid ? message.icon : SeverityIcon(theme, severity);
        const ID messageID = idManager.AllocateDynamicID("message");
        const StyleRef messageStyle = theme.bannerMessageStyleRefs[s];
        RegisterElement(tachyon::Element{ .id = messageID,
                                          .textID = textSystem->RegisterText(messageID, icon, message.text, messageStyle, ElementState::None),
                                          .styleRef = messageStyle, .flags = ElementFlags::NoInput },
                        LayoutOverride{}.FlexGrow(1));
        for (const BannerAction& action : actions)
        {
            const StyleRef style = action.kind == BannerActionKind::Primary ? theme.bannerPrimaryStyleRef
                                 : action.kind == BannerActionKind::Link    ? theme.bannerLinkStyleRef
                                                                            : theme.bannerSecondaryStyleRef;
            Button(idManager.AllocateDynamicID("action"), action.label, action.onClick, style);
        }
        if (pushed)
            (void)idManager.PopParent();
        EndHorizontal();
    }

}
