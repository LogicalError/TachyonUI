#include "application.h"

#include <algorithm>

//
// The Timeline panel: playback rows for a simulation being computed and for an animation clip.
//

namespace tachyon
{

    void Application::RenderTimelinePanel()
    {
        BeginVertical(theme.emptyVertStyleRef);

        // A 10 s simulation, computed at twice real time, with checkpoints saved along it.
        // The fill shows how far it's computed; playback loops.
        {
            static const float checkpoints[] = { 2.5f, 6.0f, 8.25f };
            const TimelineOptions options{ .duration = 10.0f, .fps = 30.0f, .progress = simulated, .markers = checkpoints };
            simulated = std::min(options.duration, simulated + 2.0f * frameTime);
            AdvanceTimeline(playback, options, frameTime);

            Text("Simulation", theme.labelStyleRef);
            Timeline(idManager.AllocateDynamicID("simulation"), playback, options);
            BeginHorizontal(LayoutOverride::Auto().NoGrow());
            Button(idManager.AllocateDynamicID("resimulate"), "Simulate again", [this] { simulated = 0.0f; });
            SetTooltip("Clears the computed frames: the fill grows again from the start");
            EndHorizontal();
        }

        // A 4 s clip at 24 frames per second, everything ready, played once.
        {
            const TimelineOptions options{ .duration = 4.0f, .fps = 24.0f };
            AdvanceTimeline(clipPlayback, options, frameTime);

            Text("Animation clip", theme.labelStyleRef);
            Timeline(idManager.AllocateDynamicID("clip"), clipPlayback, options);
        }

        EndVertical();
    }
}
