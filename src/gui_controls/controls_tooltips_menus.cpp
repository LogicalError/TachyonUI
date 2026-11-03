#include <gui_controls/gui_application.h>

#include <gui_core/graphics/gui_custom_draw_context.h>
#include <gui_core/text/gui_icon_text_placement.h>

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

        // Where a text's start lines up in its box: 0 its left, 1 its right, 2 its middle.
        int AlignSide(TextAlign align)
        {
            switch (align)
            {
                case TextAlign::End: case TextAlign::Right: case TextAlign::Bottom: return 1;
                case TextAlign::Center: return 2;
                default: return 0;
            }
        }

        // How wide an element's text and icon are on one line.
        int32_t NaturalWidth(const ITextSystem& textSystem, const Element& element, const LayoutStyle& layout)
        {
            const int32_t text = textSystem.GetTextMeasurement(element.textID).maxWidth;
            if (element.iconID == IconID::Invalid)
                return text;
            const int32_t icon = textSystem.GetIconMeasurement(element.iconID).width;
            switch (layout.iconSide)
            {
                case IconSide::Left: case IconSide::Right: return icon + layout.iconTextGap + text;
                case IconSide::Top: case IconSide::Bottom: return std::max(icon, text);
                default: return text;
            }
        }

        // Where an element's text is drawn, on one line: from where, how much of it shows, and
        // the side it's aligned to (see AlignSide).
        struct TextSpan
        {
            int32_t start = 0, width = 0;
            int     side  = 0;
        };

        TextSpan TextSpanOf(const ITextSystem& textSystem, const Element& element, const LayoutStyle& layout, const IntRect& borderBox)
        {
            const IntRect content = layout.GetPaddingBoxContentRect(borderBox);
            TextSpan span;
            span.side  = AlignSide(layout.textLayoutStyle.horizontalAlignment);
            span.width = std::min(NaturalWidth(textSystem, element, layout), content.w);
            span.start = span.side == 0 ? content.x : span.side == 1 ? content.Max(0) - span.width
                                                                     : content.x + (content.w - span.width) / 2;
            return span;
        }
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
        if (!OpenTooltip(targetID, TooltipBox::Help))
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

    void GUIApplication::SetErrorTooltip(ID targetID, std::string_view message)
    {
        if (!OpenTooltip(targetID, TooltipBox::Error))
            return;
        RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID(), .styleRef = theme.tooltipErrorTextStyleRef,
                                          .flags = ElementFlags::NoInput }, message, LayoutOverride{});
        CloseTooltip();
    }

    void GUIApplication::SetDisabledTooltip(ID targetID, std::string_view text, std::string_view reason)
    {
        const ElementRef ref = elementStorage.FindElementRefByID(targetID);
        const bool disabled = ref.IsValid() && (elementStorage.GetElementUnsafe(ref).state & ElementState::Disabled) != ElementState::None;
        if (!disabled || reason.empty())
        {
            SetTooltip(targetID, text);
            return;
        }
        if (!OpenTooltip(targetID, TooltipBox::Lines))
            return;
        if (!text.empty())
            RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID(), .styleRef = theme.tooltipTextStyleRef,
                                              .flags = ElementFlags::NoInput }, text, LayoutOverride{});
        RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID(), .styleRef = theme.tooltipNoteStyleRef,
                                          .flags = ElementFlags::NoInput }, reason, LayoutOverride{});
        CloseTooltip();
    }

    void GUIApplication::ShowPinTooltip(const NodeView& view, ID socketID, PropertyEndpoint pin, bool isOutput)
    {
        const NodeProperty& prop = view.nodes[size_t(pin.nodeIdx)].properties[size_t(pin.propIdx)];
        if ((prop.name.empty() && prop.type.empty() && !prop.value) || !OpenTooltip(socketID, TooltipBox::Lines))
            return;
        auto line = [this](std::string_view text, StyleRef style) {
            if (!text.empty())
                RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID(), .styleRef = style,
                                                  .flags = ElementFlags::NoInput }, text, LayoutOverride{});
        };
        // "Strength  float  0.75"
        RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID(), .styleRef = theme.tooltipPinRowStyleRef,
                                          .flags = ElementFlags::NoInput }, LayoutOverride{});
        VERIFY(elementStorage.BeginChildren());
        line(prop.name, theme.tooltipTextStyleRef);
        line(prop.type, theme.tooltipPinTypeStyleRef);
        if (prop.value)
            line(prop.value(), theme.tooltipPinValueStyleRef);
        VERIFY(elementStorage.EndChildren());
        // "from Source.Value"
        if (!isOutput && prop.input && prop.input->valid() && size_t(prop.input->nodeIdx) < view.nodes.size())
        {
            const NodeModel& source = view.nodes[size_t(prop.input->nodeIdx)];
            std::string from = "from " + source.title;
            if (size_t(prop.input->propIdx) < source.properties.size() && !source.properties[size_t(prop.input->propIdx)].name.empty())
                from += "." + source.properties[size_t(prop.input->propIdx)].name;
            line(from, theme.tooltipNoteStyleRef);
        }
        CloseTooltip();
    }

    bool GUIApplication::BeginTooltip(ID targetID)
    {
        const bool open = OpenTooltip(targetID);
        m_tooltipOpen.push_back(open);
        return open;
    }

    bool GUIApplication::OpenTooltip(ID targetID, TooltipBox look)
    {
        if (!tooltipManager.ShouldShow(targetID))
            return false;

        const bool     popup    = look == TooltipBox::Help || look == TooltipBox::Error;
        const StyleRef boxStyle = look == TooltipBox::Help  ? theme.tooltipRichStyleRef
                                : look == TooltipBox::Error ? theme.tooltipErrorStyleRef
                                : look == TooltipBox::Lines ? theme.tooltipLinesStyleRef
                                                            : theme.tooltipStyleRef;
        // Below the target (above when there's no room), its arrow pointing at it. The window
        // is see-through, the box's margin room for its arrow and shadow, which may reach past
        // the screen's edges. A target's text places it: its text starts where that starts
        // (or ends where it ends, for text aligned to its end), or it's centered on centered
        // text; without text (an icon, a pin), or under a canvas's pan and zoom (its text isn't
        // drawn where it's laid out), it's centered under the target.
        const LayoutStyle* boxLayout = styleManager.GetLayoutStyle(boxStyle.layoutStyleIndex);
        const int marginLeft = boxLayout ? boxLayout->margin.left : 0;
        const int marginTop  = boxLayout ? boxLayout->margin.top : 0;
        const int boxLeft  = boxLayout ? boxLayout->borderWidths.left + boxLayout->padding.left : 0;    // to its text
        const int boxRight = boxLayout ? boxLayout->borderWidths.right + boxLayout->padding.right : 0;
        const int marginRight = boxLayout ? boxLayout->margin.right : 0;
        AnchorPlacementAxis x = AnchorPlacementAxis::Center;
        int offsetX = (marginRight - marginLeft) / 2;
        const ElementRef targetRef = elementStorage.FindElementRefByID(targetID);
        bool panned = false;
        for (ElementRef up = targetRef.IsValid() ? elementStorage.GetParentRef(targetRef) : ElementRef::Invalid(); up.IsValid() && !panned;
             up = elementStorage.GetParentRef(up))
            panned = !elementStorage.GetElementRectUnsafe(up).contentTransform.IsIdentity();
        if (const Element* target = targetRef.IsValid() ? &elementStorage.GetElementUnsafe(targetRef) : nullptr;
            target && target->textID != TextID::Invalid && !panned)
            if (const LayoutStyle* layout = styleManager.GetLayoutStyle(target->styleRef.layoutStyleIndex))
            {
                const int textLeft  = layout->borderWidths.left + layout->padding.left;
                const int textRight = layout->borderWidths.right + layout->padding.right;
                switch (AlignSide(layout->textLayoutStyle.horizontalAlignment))
                {
                    case 0:  x = AnchorPlacementAxis::InsideMin; offsetX = textLeft - boxLeft - marginLeft; break;
                    case 1:  x = AnchorPlacementAxis::InsideMax; offsetX = boxRight + marginRight - textRight; break;
                    default: x = AnchorPlacementAxis::Center;
                             offsetX = ((textLeft - textRight) - (boxLeft + marginLeft - boxRight - marginRight)) / 2; break;
                }
            }
        auto pos = LayoutOverride{}.Anchored(targetID, x, offsetX, AnchorPlacementAxis::OutsideMax,
                                             styleManager.Px(kTooltipGap) - marginTop, AutoFlipMode::FlipAvoidTarget)
                                   .Overhang({ marginLeft, marginTop });
        pos.x.autoFlip = AutoFlipMode::Flip;   // across: moved onto the screen, not narrowed

        ElementFlags flags = ElementFlags::WindowNoCaption | ElementFlags::WindowIsPopup | ElementFlags::WindowIsTransparent
                           | ElementFlags::WindowNotFocusable;

        // Not disabled, selected or invalid with what it's on.
        m_tooltipScopes.push_back(guiStateManager.Construction().SetAside());
        if (!BeginWindow(tooltipWindowID, theme.tooltipFrameStyleRef, std::move(pos), flags))
        {
            EndWindow();   // the window's skipped scope
            guiStateManager.Construction().Restore(m_tooltipScopes.back());
            m_tooltipScopes.pop_back();
            return false;
        }
        if (const ElementRef winRef = elementStorage.FindElementRefByID(tooltipWindowID); winRef.IsValid())
            elementStorage.GetElementUnsafe(winRef).flags |= ElementFlags::NoInput;

        const ID boxID = idManager.AllocateDynamicID("tooltip_box");
        const ElementRef box = RegisterElement(tachyon::Element{ .id = boxID, .styleRef = boxStyle,
                                                                 .flags = ElementFlags::NoInput }, LayoutOverride{});
        const VisualColorStyleIndex fill = popup ? theme.popupArrowFill : theme.tooltipArrowFill;
        const VisualColorStyleIndex line = look == TooltipBox::Error ? theme.invalidLine
                                         : popup                     ? theme.popupArrowLine : theme.tooltipArrowLine;
        const float px = styleManager.GetUIScale();
        // The tip from the side the box lines up on, and the box's corner radius.
        const bool  help = look == TooltipBox::Help;
        const float fromEdgeCss = help ? 45.5f : 17.5f, cornerCss = help ? 8.0f : 6.0f;
        // Drawn before the box's children, not after them: an after-draw clips to the box itself.
        VERIFY(elementStorage.AddCustomDraw(box, [this, targetID, fill, line, px, fromEdgeCss, cornerCss, boxLeft, boxRight](CustomDrawContext& ctx)
        {
            // A bordered square turned on its corner, the half out of the box pointing at the
            // target: a triangle in the border's color, and the fill's inside it one border
            // in, which also covers the box's own border under it.
            const IntRect r = ctx.ElementRect();
            const ElementRef targetRef = elementStorage.FindElementRefByID(targetID);
            const ElementRect* target = elementStorage.GetElementRectPtr(targetRef);
            const ElementRect* self = elementStorage.GetElementRectPtr(ctx.Ref());
            if (!target || !self)
                return;
            // The target is in another window: compare the two in layout space, where the
            // tooltip was placed against it (through a canvas's pan and zoom), then draw in
            // this window's.
            const IntRect laid = self->rect;
            const IntRect onScreen = elementStorage.ElementRectOnScreen(targetRef);
            const float height = std::round(6.0f * px);      // the tip, out from the box's edge
            const float inner  = height - 1.41421356f * px;  // the fill's: one border in, at 45 degrees
            const float fromEdge = fromEdgeCss * px;
            const float inset = cornerCss * px + height + px;   // clear of the rounded corner

            float at;   // what it points at, in layout space
            const Element& element = elementStorage.GetElementUnsafe(targetRef);
            if (element.textID != TextID::Invalid && target->layoutStyle && onScreen == target->rect)
            {
                // At the target's text: as far into it as the box's own text is from the tip
                // (where its text starts or ends where the target's does), at most its middle;
                // centered text at its middle.
                const TextSpan span = TextSpanOf(*textSystem, element, *target->layoutStyle, target->rect);
                const float middle = float(span.start) + float(span.width) * 0.5f;
                at = span.side == 0 ? std::min(float(span.start) + fromEdge - float(boxLeft), middle)
                   : span.side == 1 ? std::max(float(span.start + span.width) - fromEdge + float(boxRight), middle)
                                    : middle;
            }
            else
                at = float(onScreen.x) + float(onScreen.w) * 0.5f;   // without text: its middle
            float x = at - float(laid.x) + float(r.x);
            x = std::clamp(x, float(r.x) + inset, float(r.x + r.w) - inset);

            const bool  below = onScreen.y + onScreen.h / 2 < laid.y;   // the target is above it
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
        if (!m_tooltipScopes.empty())
        {
            guiStateManager.Construction().Restore(m_tooltipScopes.back());
            m_tooltipScopes.pop_back();
        }
    }

    GUIApplication::CutShortText GUIApplication::FindCutShortText(int2 mousePos) const
    {
        const ID target = tooltipManager.GetActiveTargetID();
        const ElementRef targetRef = elementStorage.FindElementRefByID(target);
        if (!targetRef.IsValid() || !textSystem)
            return {};
        // Under a zoom the text is drawn at another size than its own.
        for (ElementRef up = elementStorage.GetParentRef(targetRef); up.IsValid(); up = elementStorage.GetParentRef(up))
            if (!elementStorage.GetElementRectUnsafe(up).contentTransform.IsIdentity())
                return {};
        // Shown for the focus after a Tab: the target's own text or its first part's.
        const bool byMouse = target == guiStateManager.Interaction().GetHoveredID();

        std::vector<ElementRef> todo{ targetRef };
        for (size_t visited = 0; !todo.empty() && visited < 256; ++visited)
        {
            const ElementRef ref = todo.back();
            todo.pop_back();
            const Element&     element = elementStorage.GetElementUnsafe(ref);
            const ElementRect& rect    = elementStorage.GetElementRectUnsafe(ref);
            const LayoutStyle* layout  = rect.layoutStyle;
            if (element.textID != TextID::Invalid && layout && layout->textLayoutStyle.textWrapping == TextWrap::NoWrap &&
                !textSystem->IsTextEditor(element.id))
            {
                // Its text drawn whole, where in its box it would start, against what shows of it.
                const IntRect content = layout->GetPaddingBoxContentRect(rect.rect);
                const int32_t natural = NaturalWidth(*textSystem, element, *layout);
                const int     side    = AlignSide(layout->textLayoutStyle.horizontalAlignment);
                const int32_t start   = side == 0 ? content.x : side == 1 ? content.x + content.w - natural
                                                                         : content.x + (content.w - natural) / 2;
                const IntRect shown   = content.Intersection(rect.clipRect);
                const bool cut = start < shown.x || start + natural > shown.x + shown.w;
                if (cut && shown.w > 0 && (!byMouse || shown.Contains(mousePos)))
                {
                    ElementRef window = ref;
                    while (window.depth > 0)
                        window = elementStorage.GetParentRef(window);
                    const ElementRect& windowRect = elementStorage.GetElementRectUnsafe(window);
                    CutShortText found{ .id = element.id, .windowID = elementStorage.GetElementUnsafe(window).id,
                                        .windowPos = windowRect.rect.position,
                                        .at = { start, rect.rect.y + layout->borderWidths.top }, .width = natural,
                                        .height = rect.rect.h - layout->borderWidths.Vertical(),
                                        .monitorIndex = windowRect.monitorIndex };
                    // An icon above or below the text (a grid's item): just the text's line, where
                    // it's drawn, without the icon.
                    const VisualTextStyle* visual = styleManager.GetVisualTextStyle(element.styleRef.visualTextStyleIndex, element.state);
                    const IconSide iconSide = visual ? visual->iconSide : layout->iconSide;
                    if (element.iconID != IconID::Invalid && (iconSide == IconSide::Top || iconSide == IconSide::Bottom))
                    {
                        IconMeasurement icon = textSystem->GetIconMeasurement(element.iconID);
                        if (visual && visual->iconSize > 0)
                            icon.width = icon.height = visual->iconSize;
                        const int32_t gap  = visual ? visual->iconTextGap : layout->iconTextGap;
                        const int32_t line = textSystem->GetTextHeightWithoutIcon(element.textID);
                        const TextAlign across = layout->textLayoutStyle.horizontalAlignment;
                        const IconTextPlacement placed = ComputeIconTextPlacement(
                            content, natural, line, icon, iconSide, gap, across, layout->textLayoutStyle.verticalAlignment);
                        found.at       = { content.x + HorizontalAlignOffset(across, content.w, natural), placed.textRect.y };
                        found.height   = line;
                        found.textOnly = true;
                    }
                    return found;
                }
            }
            const IndexRange children = elementStorage.GetChildRange(ref);
            for (uint32_t i = children.end; i-- > children.begin;)
                todo.push_back(ElementRef{ ref.depth + 1, i });
        }
        return {};
    }

    void GUIApplication::ShowCutShortText()
    {
        const ElementRef ref = elementStorage.FindElementRefByID(m_cutShort.id);
        if (!ref.IsValid())
            return;
        // Its parts, as registering elements may move it.
        const Element&     found    = elementStorage.GetElementUnsafe(ref);
        const TextID       textID   = found.textID;
        const IconID       iconID   = m_cutShort.textOnly ? IconID::Invalid : found.iconID;
        const StyleRef     cutStyle = found.styleRef;
        const ElementState cutState = found.state;
        const LayoutStyle* box = styleManager.GetLayoutStyle(theme.tooltipExpandStyleRef.layoutStyleIndex);
        if (textID == TextID::Invalid || !box || !tooltipManager.ShouldShow(tooltipManager.GetActiveTargetID()))
            return;

        // Its text where the cut text's is (as laid out last frame: this frame's isn't yet), the
        // box's margin, border and padding around it; kept on the screen, but for the margin
        // (room for the shadow).
        const int boxLeft   = box->margin.left + box->borderWidths.left + box->padding.left;
        const int boxTop    = box->margin.top + box->borderWidths.top + box->padding.top;
        const int boxRight  = box->margin.right + box->borderWidths.right + box->padding.right;
        const int boxBottom = box->margin.bottom + box->borderWidths.bottom + box->padding.bottom;
        int x = m_cutShort.at.x - boxLeft, y = m_cutShort.at.y - boxTop;
        if (const IntRect area = windowManager->GetMonitorArea(m_cutShort.monitorIndex); area.w > 0 && area.h > 0)
        {
            const int w = m_cutShort.width + boxLeft + boxRight, h = m_cutShort.height + boxTop + boxBottom;
            x = std::max(std::min(x, area.Max(0) + box->margin.right - w), area.x - box->margin.left);
            y = std::max(std::min(y, area.Max(1) + box->margin.bottom - h), area.y - box->margin.top);
        }
        // Against its window, which is laid out before this one: the cut text, in it, isn't.
        auto pos = LayoutOverride{}.Anchored(m_cutShort.windowID, AnchorPlacementAxis::InsideMin, x - m_cutShort.windowPos.x,
                                             AnchorPlacementAxis::InsideMin, y - m_cutShort.windowPos.y);

        const ElementFlags flags = ElementFlags::WindowNoCaption | ElementFlags::WindowIsPopup | ElementFlags::WindowIsTransparent
                                 | ElementFlags::WindowNotFocusable;
        // Not disabled, selected or invalid with what it's on.
        m_tooltipScopes.push_back(guiStateManager.Construction().SetAside());
        if (!BeginWindow(tooltipWindowID, theme.tooltipFrameStyleRef, std::move(pos), flags))
        {
            EndWindow();   // the window's skipped scope
            guiStateManager.Construction().Restore(m_tooltipScopes.back());
            m_tooltipScopes.pop_back();
            return;
        }
        if (const ElementRef winRef = elementStorage.FindElementRefByID(tooltipWindowID); winRef.IsValid())
            elementStorage.GetElementUnsafe(winRef).flags |= ElementFlags::NoInput;
        RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID("tooltip_box"), .styleRef = theme.tooltipExpandStyleRef,
                                          .flags = ElementFlags::NoInput }, LayoutOverride{});
        VERIFY(elementStorage.BeginChildren());
        const std::string text{ textSystem->GetText(textID) };
        RegisterElement(tachyon::Element{ .id = idManager.AllocateDynamicID(), .iconID = iconID,
                                          .styleRef = styleManager.WithTextOf(theme.tooltipExpandTextStyleRef, cutStyle, cutState,
                                                                              !m_cutShort.textOnly),
                                          .flags = ElementFlags::NoInput },
                        text, LayoutOverride{}.Height(Pixels{ m_cutShort.height }));
        CloseTooltip();
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