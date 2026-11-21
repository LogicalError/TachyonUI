#include <gui_controls/subsystems/gui_color_picker_subsystem.h>

#include <gui_core/frame/gui_frame_pipeline.h>
#include <gui_core/style/gui_style_manager.h>

namespace tachyon
{
    ColorPickerSubsystem::ColorPickerSubsystem(FramePipeline& frame)
    {
        // The swatches not claimed last frame are gone.
        frame.OnPreFrame([this](FrameContext& ctx) { m_swatchStyles.NextFrame(ctx.styleManager); });
    }

    VisualColorStyleIndex& ColorPickerSubsystem::ClaimSwatchStyle(tachyon::ID swatchID)
    {
        return m_swatchStyles.Claim(swatchID).index;
    }

    void ColorPickerSubsystem::KeepSwatchAlive(tachyon::ID swatchID)
    {
        m_swatchStyles.KeepAlive(swatchID);
    }

    void ColorPickerSubsystem::ForgetStyles()
    {
        // ClearAll freed every style: drop the indices (without destroying them) so
        // they're created anew.
        m_swatchStyles.Forget();
        ColorPickerState& s = m_state;
        for (auto* idx : { &s.svQuadColor, &s.hueStrip, &s.alphaStrip, &s.hueColor, &s.whiteColor,
                           &s.outlineColor, &s.newColor, &s.originalStyle })
            *idx = VisualColorStyleIndex::Invalid;
        for (auto& idx : s.gradientStyles)
            idx = VisualColorStyleIndex::Invalid;
    }
}
