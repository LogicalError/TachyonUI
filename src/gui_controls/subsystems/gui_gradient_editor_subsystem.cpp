#include <gui_controls/subsystems/gui_gradient_editor_subsystem.h>

#include <gui_core/frame/gui_frame_pipeline.h>
#include <gui_core/style/gui_style_manager.h>

namespace tachyon
{
    GradientEditorSubsystem::GradientEditorSubsystem(FramePipeline& frame)
    {
        // The swatches not claimed last frame are gone.
        frame.OnPreFrame([this](FrameContext& ctx) { m_swatchStyles.NextFrame(ctx.styleManager); });
    }

    void GradientEditorSubsystem::KeepSwatchAlive(tachyon::ID swatchID)
    {
        m_swatchStyles.KeepAlive(swatchID);
    }

    void GradientEditorSubsystem::ForgetStyles()
    {
        // ClearAll freed every style: drop the indices (without destroying them) so
        // they're created anew.
        m_swatchStyles.Forget();
        GradientEditorState& s = m_state;
        for (auto* idx : { &s.previewGradientStyle, &s.stopEdgeColor, &s.stopOutlineColor, &s.stopHaloColor })
            *idx = VisualColorStyleIndex::Invalid;
    }
}
