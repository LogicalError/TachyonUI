#pragma once

#include <gui_core/gui_base_types.h>

#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_text.h>

#include <gui_core/text/gui_text_system.h>

#include <algorithm>

namespace tachyon
{
    /// @addtogroup text
    /// @{

    /// X offset, in pixels, of `contentWidth` content aligned inside a `boxWidth` box.
    /// Vertical values map as skribidi does: Top acts as left, Bottom as right.
    inline int32_t HorizontalAlignOffset(TextAlign horizontalAlignment, int32_t boxWidth, int32_t contentWidth)
    {
        switch (horizontalAlignment)
        {
            case TextAlign::Center: return (boxWidth - contentWidth) / 2;
            case TextAlign::End:
            case TextAlign::Right:
            case TextAlign::Bottom: return boxWidth - contentWidth;
            default:                return 0;
        }
    }

    inline int32_t VerticalAlignOffset(TextAlign verticalAlignment, int32_t boxHeight, int32_t contentHeight)
    {
        switch (verticalAlignment)
        {
            case TextAlign::Center: return (boxHeight - contentHeight) / 2;
            case TextAlign::End:
            case TextAlign::Bottom:
            case TextAlign::Right:  return boxHeight - contentHeight;
            default:                return 0;
        }
    }

    /// Placement of a text + icon pair inside `rect`:
    ///   * The icon always sits adjacent to its label, never at a stretched rect's edge.
    ///   * Left / Right: icon and text share one centre line. textRect is the rect less
    ///     the icon area (IconAwareLayoutMaxWidth): for Left it starts past the icon at
    ///     rect.x, for Right it keeps rect.x and shrinks; text-align places the text in it.
    ///   * Top / Bottom: icon is centred on the [icon, text] column. Top hugs rect's
    ///     top; Bottom sits iconTextGap below the text, not at rect's bottom.
    ///   * Left / Right icons taller than the text may extend outside rect; the
    ///     caller sizes rect via measurement when it wants them contained.
    struct IconTextPlacement
    {
        IntRect textRect;
        IntRect iconRect;
    };

    inline IconTextPlacement ComputeIconTextPlacement(
        const IntRect&         rect,
        int32_t                textWidth,
        int32_t                textHeight,
        const IconMeasurement& iconMeasurement,
        IconSide               iconSide,
        int32_t                iconTextGap,
        TextAlign              horizontalAlignment,
        TextAlign              verticalAlignment = TextAlign::Start)
    {
        IconTextPlacement out = {
            .textRect = rect,
            .iconRect = IntRect{ 0, 0, 0, 0 },
        };

        // Icon only: align it within `rect` directly. The text+icon model below
        // centres the icon on the text line, which has no height here.
        if (textWidth <= 0)
        {
            const int32_t iconX = rect.x + HorizontalAlignOffset(horizontalAlignment, rect.w, iconMeasurement.width);
            const int32_t iconY = rect.y + VerticalAlignOffset(verticalAlignment, rect.h, iconMeasurement.height);
            out.iconRect = IntRect{ iconX, iconY, iconMeasurement.width, iconMeasurement.height };
            return out;
        }

        // Bounding box of the packed [icon][text] block, for text-align and
        // -tachyon-vertical-text-align.
        int32_t combinedWidth  = textWidth;
        int32_t combinedHeight = textHeight;
        switch (iconSide)
        {
            case IconSide::None: break;
            case IconSide::Left:
            case IconSide::Right:
                combinedWidth  = textWidth + iconTextGap + iconMeasurement.width;
                combinedHeight = std::max(textHeight, iconMeasurement.height);
                break;
            case IconSide::Top:
            case IconSide::Bottom:
                combinedWidth  = std::max(textWidth, iconMeasurement.width);
                combinedHeight = textHeight + iconTextGap + iconMeasurement.height;
                break;
        }

        const int32_t textAlignmentOffsetX = HorizontalAlignOffset(horizontalAlignment, rect.w, combinedWidth);

        // Block vertical offset - shifts iconRect.y AND textRect.y by the
        // same amount so the icon/text pair tracks its alignment together.
        const int32_t blockY = rect.y + VerticalAlignOffset(verticalAlignment, rect.h, combinedHeight);

        switch (iconSide)
        {
            case IconSide::None: break;
            case IconSide::Left:
            {
                // The text is laid out (and aligned) in the rect less the icon and gap
                // (IconAwareLayoutMaxWidth), so its area is that: from past the icon at the
                // rect's start to the rect's end. The icon sits just before the aligned text.
                const int32_t combinedStartX = rect.x + textAlignmentOffsetX;
                out.iconRect = IntRect{
                    .x = combinedStartX,
                    .y = blockY + (combinedHeight - iconMeasurement.height) / 2,
                    .w = iconMeasurement.width,
                    .h = iconMeasurement.height
                };
                out.textRect.x = rect.x + iconMeasurement.width + iconTextGap;
                out.textRect.w = std::max(0, rect.w - iconMeasurement.width - iconTextGap);
                out.textRect.y = blockY + (combinedHeight - textHeight) / 2;
                break;
            }
            case IconSide::Right:
            {
                const int32_t combinedStartX = rect.x + textAlignmentOffsetX;
                out.iconRect = IntRect{
                    .x = combinedStartX + textWidth + iconTextGap,
                    .y = blockY + (combinedHeight - iconMeasurement.height) / 2,
                    .w = iconMeasurement.width,
                    .h = iconMeasurement.height
                };
                out.textRect.x = rect.x;
                out.textRect.w = rect.w - iconTextGap - iconMeasurement.width;
                out.textRect.y = blockY + (combinedHeight - textHeight) / 2;
                break;
            }
            case IconSide::Top:
            {
                const int32_t combinedStartX = rect.x + textAlignmentOffsetX;
                const int32_t iconCenterX    = combinedStartX + combinedWidth / 2;
                out.iconRect = IntRect{
                    .x = iconCenterX - iconMeasurement.width / 2,
                    .y = blockY,
                    .w = iconMeasurement.width,
                    .h = iconMeasurement.height
                };
                out.textRect.x = rect.x;
                out.textRect.w = rect.w;
                out.textRect.y = blockY + iconMeasurement.height + iconTextGap;
                out.textRect.h = rect.h - iconMeasurement.height - iconTextGap;
                break;
            }
            case IconSide::Bottom:
            {
                const int32_t combinedStartX = rect.x + textAlignmentOffsetX;
                const int32_t iconCenterX    = combinedStartX + combinedWidth / 2;
                out.textRect.x = rect.x;
                out.textRect.w = rect.w;
                out.textRect.y = blockY;
                out.textRect.h = rect.h - iconMeasurement.height - iconTextGap;   // as Top: the rect less the icon's room
                out.iconRect = IntRect{
                    .x = iconCenterX - iconMeasurement.width / 2,
                    .y = blockY + textHeight + iconTextGap,
                    .w = iconMeasurement.width,
                    .h = iconMeasurement.height
                };
                break;
            }
        }

        (void)combinedHeight;
        return out;
    }

    /// The max width to lay text out in: `rectWidth` minus the icon and gap for a
    /// Left / Right icon, so text-align can't reach into the icon area; `rectWidth` otherwise.
    inline int32_t IconAwareLayoutMaxWidth(
        int32_t                rectWidth,
        const IconMeasurement& iconMeasurement,
        IconSide               iconSide,
        int32_t                iconTextGap,
        bool                   hasIcon)
    {
        if (!hasIcon) return rectWidth;
        if (iconSide == IconSide::Left || iconSide == IconSide::Right)
            return std::max(0, rectWidth - iconTextGap - iconMeasurement.width);
        return rectWidth;
    }
    /// @}
}
