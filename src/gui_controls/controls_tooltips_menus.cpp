#include <gui_controls/gui_application.h>

#include <gui_core/graphics/gui_custom_draw_context.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace tachyon
{


    // ---------------------------------------------------------------------------
    // Tooltips
    // ---------------------------------------------------------------------------

    namespace
    {
        constexpr int kTooltipGap = 9;   // CSS px from the target to the box
    }

    void GUIApplication::SetTooltip(ID targetID, std::string_view text)
    {
        SetTooltip(targetID, IconID::Invalid, text, {});
    }

    void GUIApplication::SetTooltip(ID targetID, IconID icon, std::string_view text)
    {
        SetTooltip(targetID, icon, text, {});
    }

    void GUIApplication::SetTooltip(ID targetID, std::string_view text, std::string_view shortcut)
    {
        SetTooltip(targetID, IconID::Invalid, text, shortcut);
    }

    void GUIApplication::SetTooltip(ID targetID, IconID icon, std::string_view text, std::string_view shortcut)
    {
        if (!OpenTooltip(targetID))
            return;
        const ID textID = idManager.AllocateDynamicID();
        RegisterElement(tachyon::Element{ .id = textID, .iconID = icon, .styleRef = theme.tooltipTextStyleRef }, text, LayoutOverride{});
        // The shortcut that does the same, in a chip after the text.
        if (!shortcut.empty())
            RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID(), .styleRef = theme.tooltipShortcutStyleRef,
                                              .flags = ElementFlags::NoInput },
                            shortcut, LayoutOverride{});
        CloseTooltip();
    }

    void GUIApplication::SetTooltip(ID targetID, const PropertyHelp& help)
    {
        // A label with help is underlined, dotted, whether or not its help shows. Its text was
        // registered in the plain style: registered again, it takes the new one.
        if (const ElementRef ref = elementStorage.FindElementRefByID(targetID); ref.IsValid())
        {
            Element& label = elementStorage.GetElementUnsafe(ref);
            const StyleRef underlined = label.styleRef == theme.labelStyleRef          ? theme.labelHelpStyleRef
                                : label.styleRef == theme.groupLabelStyleRef     ? theme.groupLabelHelpStyleRef
                                : label.styleRef == theme.dragLabelStyleRef      ? theme.dragLabelHelpStyleRef
                                : label.styleRef == theme.groupDragLabelStyleRef ? theme.groupDragLabelHelpStyleRef
                                : StyleRef{};
            if (underlined != StyleRef{})
            {
                label.styleRef = underlined;
                if (label.textID != TextID::Invalid)
                    label.textID = textSystem->RegisterText(label.id, label.iconID, textSystem->GetText(label.textID), underlined, label.state);
            }
        }
        if (!OpenTooltip(targetID, true))
            return;
        auto part = [this](std::string_view text, StyleRef style) {
            if (!text.empty())
                RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID(), .styleRef = style,
                                                  .flags = ElementFlags::NoInput }, text, LayoutOverride{});
        };
        part(help.title, theme.tooltipTitleStyleRef);
        part(help.description, theme.tooltipDescriptionStyleRef);
        part(help.footer, theme.tooltipFooterStyleRef);
        CloseTooltip();
    }

    bool GUIApplication::BeginTooltip(ID targetID)
    {
        const bool open = OpenTooltip(targetID);
        m_tooltipOpen.push_back(open);
        return open;
    }

    bool GUIApplication::OpenTooltip(ID targetID, bool rich)
    {
        if (!tooltipManager.ShouldShow(targetID))
            return false;

        const StyleRef boxStyle = rich ? theme.tooltipRichStyleRef : theme.tooltipStyleRef;
        // Below the target (above when there's no room), its arrow pointing at it. The window
        // is see-through, the box's margin room for its arrow and shadow: the window is placed
        // so the box lines up with the target, the gap away, and that room may reach past the
        // screen's edges.
        const LayoutStyle* boxLayout = styleManager.GetLayoutStyle(boxStyle.layoutStyleIndex);
        const int marginLeft = boxLayout ? boxLayout->margin.left : 0;
        const int marginTop  = boxLayout ? boxLayout->margin.top : 0;
        auto pos = LayoutOverride{}.Anchored(targetID,
            AnchorPlacement::OutsideBottom | AnchorPlacement::InsideLeft,
            { int16_t(-marginLeft), int16_t(styleManager.Px(kTooltipGap) - marginTop) }, AutoFlipMode::FlipAvoidTarget)
            .Overhang({ marginLeft, marginTop });

        ElementFlags flags = ElementFlags::WindowNoCaption | ElementFlags::WindowIsPopup | ElementFlags::WindowIsTransparent
                           | ElementFlags::WindowNotFocusable;

        if (!BeginWindow(tooltipWindowID, theme.tooltipFrameStyleRef, std::move(pos), flags))
        {
            EndWindow();   // the window's skipped scope
            return false;
        }
        if (const ElementRef winRef = elementStorage.FindElementRefByID(tooltipWindowID); winRef.IsValid())
            elementStorage.GetElementUnsafe(winRef).flags |= ElementFlags::NoInput;

        const ID boxID = idManager.AllocateDynamicID("tooltip_box");
        const ElementRef box = RegisterElement(tachyon::Element{ .id = boxID, .styleRef = boxStyle,
                                                                 .flags = ElementFlags::NoInput }, LayoutOverride{});
        const VisualColorStyleIndex fill = rich ? theme.popupArrowFill : theme.tooltipArrowFill;
        const VisualColorStyleIndex line = rich ? theme.popupArrowLine : theme.tooltipArrowLine;
        const float px = styleManager.GetUIScale();
        // The tip from the side the box lines up on, and the box's corner radius.
        const float fromEdgeCss = rich ? 45.5f : 17.5f, cornerCss = rich ? 8.0f : 6.0f;
        // Drawn before the box's children, not after them: an after-draw clips to the box itself.
        VERIFY(elementStorage.AddCustomDraw(box, [this, targetID, fill, line, px, fromEdgeCss, cornerCss](CustomDrawContext& ctx)
        {
            // A bordered square turned on its corner, the half out of the box pointing at the
            // target: a triangle in the border's color, and the fill's inside it one border
            // in, which also covers the box's own border under it.
            const IntRect r = ctx.ElementRect();
            const ElementRect* target = elementStorage.GetElementRectPtr(elementStorage.FindElementRefByID(targetID));
            const ElementRect* self = elementStorage.GetElementRectPtr(ctx.Ref());
            if (!target || !self)
                return;
            // The target is in another window: compare the two in layout space, where the
            // tooltip was placed against it, then draw in this window's.
            const IntRect laid = self->rect;
            const float height = std::round(6.0f * px);      // the tip, out from the box's edge
            const float inner  = height - 1.41421356f * px;  // the fill's: one border in, at 45 degrees
            const float fromEdge = fromEdgeCss * px;
            const float inset = cornerCss * px + height + px;   // clear of the rounded corner

            // Near the side the box is lined up with the target on (its left, or its right
            // when it was kept on the screen), but not past the target's middle.
            const float targetLeft  = float(target->rect.x - laid.x + r.x);
            const float targetMid   = targetLeft + float(target->rect.w) * 0.5f;
            const bool  onLeft = std::abs(laid.x - target->rect.x) <= std::abs(laid.Max(0) - target->rect.Max(0));
            float x = onLeft ? std::min(float(r.x) + fromEdge, targetMid)
                             : std::max(float(r.x + r.w) - fromEdge, targetMid);
            x = std::clamp(x, float(r.x) + inset, float(r.x + r.w) - inset);

            const bool  below = target->rect.y + target->rect.h / 2 < laid.y;   // the target is above it
            const float out   = below ? -1.0f : 1.0f;
            const float edge  = below ? float(r.y) : float(r.y + r.h);
            // The point on a pixel's middle, a whole number of pixels out: one pixel wide.
            x = std::floor(x) + 0.5f;
            ctx.RenderTriangle({ x - height, edge }, { x + height, edge }, { x, edge + out * height }, line);
            const float base = edge - out * px;   // over the box's border
            ctx.RenderTriangle({ x - inner - px, base }, { x + inner + px, base }, { x, edge + out * inner }, fill);
        }));
        VERIFY(elementStorage.BeginChildren());
        return true;
    }

    void GUIApplication::CloseTooltip()
    {
        VERIFY(elementStorage.EndChildren());   // the box
        EndWindow();
    }

    // Called for every BeginTooltip, shown or not.
    void GUIApplication::EndTooltip()
    {
        if (m_tooltipOpen.empty())
        {
            ReportMisuse("GUIApplication: EndTooltip without BeginTooltip (ignored)");
            return;
        }
        const bool open = m_tooltipOpen.back();
        m_tooltipOpen.pop_back();
        if (open)
            CloseTooltip();
    }


    // ---------------------------------------------------------------------------
    // Context Menus
    // ---------------------------------------------------------------------------

    bool GUIApplication::BeginContextMenu(ID targetID)
    {
        const bool open = OpenContextMenuWindow(targetID);
        m_menuOpen.push_back(open);   // EndContextMenu is EndMenu
        return open;
    }

    bool GUIApplication::OpenContextMenuWindow(ID targetID)
    {
        // A right-click on the target (last frame's hovered element) or on a part of it that
        // it takes the states of, or Shift+F10 / the Menu key with the focus there, opens
        // it. The first target that claims the request this frame wins: a control's menu,
        // declared right after it, over the one of a row or node it is in.
        if (targetID != ID::Invalid() && inputManager && !m_contextMenuClaimed)
        {
            bool byMouse = false, byKeyboard = false;
            int2 pressedAt{};
            for (const auto& e : inputManager->GetRecordedEvents())
            {
                if (e.type == tachyon::EventType::MouseDown && e.state.RightPressed())
                {
                    byMouse   = true;
                    pressedAt = e.state.screenPos;
                }
                if (e.type == tachyon::EventType::KeyDown &&
                    (e.state.lastKey == tachyon::KeyCode::Application || e.state.lastKey == tachyon::KeyCode::Menu ||
                     (e.state.lastKey == tachyon::KeyCode::F10 && (e.state.modifiers & KeyModifier::Shift) != KeyModifier::None)))
                    byKeyboard = true;
            }
            auto isIn = [targetID](ID leaf, const std::vector<ID>& around)
            {
                return leaf != ID::Invalid() &&
                       (leaf == targetID || std::find(around.begin(), around.end(), targetID) != around.end());
            };
            const auto& interaction = guiStateManager.Interaction();
            const bool mouseHit = byMouse && isIn(interaction.GetHoveredID(), interaction.GetPropagatedHoverIDs());
            const bool keyHit   = !mouseHit && byKeyboard && isIn(interaction.GetFocusedID(), interaction.GetPropagatedFocusedIDs());
            if (mouseHit || keyHit)
            {
                m_contextMenuClaimed = true;
                contextMenuTargetID = targetID;
                // At the pointer when right-clicked; below the target from the keyboard.
                if (mouseHit)
                    contextMenuScreenPos = pressedAt;
                else
                    contextMenuScreenPos.reset();
                if (menuManager.IsOpen())
                    menuManager.CloseAll(guiStateManager);
                menuManager.OpenMenu(guiStateManager, contextMenuWindowID);
                if (keyHit)
                    menuManager.mouseHiddenForMenu = true;   // the keyboard leads in the menu
            }
        }

        // If context menu is open for this target, declare the popup window
        if (contextMenuTargetID != targetID)
            return false;
        if (!menuManager.IsOpen() || menuManager.levels[0].triggerID != contextMenuWindowID)
        {
            contextMenuTargetID = ID::Invalid();
            return false;
        }

        auto& level = menuManager.GetOrCreateNextLevel(idManager);

        // Where it was right-clicked or OpenContextMenu said, else below the target element.
        LayoutOverride pos = contextMenuScreenPos
            ? LayoutOverride{}.Position(contextMenuScreenPos->x, contextMenuScreenPos->y)
            : LayoutOverride{}.Anchored(targetID,
                  AnchorPlacement::OutsideBottom | AnchorPlacement::InsideLeft,
                  {0, 0}, AutoFlipMode::FlipAvoidTarget);

        menuManager.PrepareLevelWindow(level, guiStateManager.GetScrollManagement());
        if (BeginWindow(level.windowID, theme.menuStyleRef, std::move(pos),
                        ElementFlags::IsMenuWindow))
        {
            menuManager.currentDepth = level.depth;
            menuManager.maxOpenDepth = std::max(menuManager.maxOpenDepth, menuManager.currentDepth);
            level.reserveIconColumn = menuManager.menuItemsHaveIcons[level.triggerID];
            level.itemsHaveIcons    = false;
            level.scrollUpID = idManager.AllocateDynamicID();
            level.scrollDownID = idManager.AllocateDynamicID();
            return true;
        }
        else
        {
            EndWindow();   // the popup window's skipped scope
            menuManager.CloseAll(guiStateManager);
            contextMenuTargetID = ID::Invalid();
            return false;
        }
    }

    void GUIApplication::OpenContextMenu(ID targetID, int2 screenPos)
    {
        contextMenuTargetID  = targetID;
        contextMenuScreenPos = screenPos;
        if (menuManager.IsOpen())
            menuManager.CloseAll(guiStateManager);
        menuManager.OpenMenu(guiStateManager, contextMenuWindowID);
    }

    void GUIApplication::EndContextMenu()
    {
        EndMenu();
    }
}