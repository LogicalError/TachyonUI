#include "application.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

//
// Work under way, shown where it happens: the Build panel's steps, the status bar's task and
// its background tasks, and the viewport's shaders compiling. The Files panel's import and
// the hierarchy loading are drawn by their panels, from the same clock.
//

namespace tachyon
{
    namespace
    {
        // A build's steps, and how long each takes in seconds.
        struct BuildStep { const char* name; float seconds; };
        constexpr BuildStep kBuildSteps[] = {
            { "Compile scripts", 2.0f }, { "Cook assets", 3.0f }, { "Bake lighting", 6.0f }, { "Package", 2.0f }, { "Upload", 3.0f },
        };
        constexpr float kBuildSecs = [] { float s = 0.0f; for (const BuildStep& b : kBuildSteps) s += b.seconds; return s; }();
        // Captures: baking, 42% of the way.
        constexpr float kBuildHeld = 2.0f + 3.0f + 6.0f * 0.42f;

        // Running from startup: the indexing, shown in the status bar, and the thumbnails.
        constexpr float kIndexingSecs   = 20.0f;
        constexpr float kThumbnailsSecs = 30.0f;
        constexpr float kIndexingHeld   = kIndexingSecs * 0.33f;
        constexpr float kThumbnailsHeld = kThumbnailsSecs * 0.78f;
    }

    float Application::ProgressElapsed(float since, float held)
    {
        return progress.held ? held : guiStateManager.Interaction().GetFrameTime() - since;
    }

    bool Application::ShadersCompiling()
    {
        return ProgressElapsed(progress.startedAt, 1.0f) < ProgressDemos::kShadersSecs;
    }

    float Application::BuildElapsed()
    {
        if (progress.held)
            return kBuildHeld;
        if (progress.buildStart < 0.0f)
            return -1.0f;
        return progress.buildCancelledAt >= 0.0f ? progress.buildCancelledAt : ProgressElapsed(progress.buildStart, kBuildHeld);
    }

    bool Application::BuildRunning()
    {
        const float at = BuildElapsed();
        return at >= 0.0f && at < kBuildSecs && progress.buildCancelledAt < 0.0f;
    }

    float Application::ImportElapsed()
    {
        if (progress.held)
            return ProgressDemos::kImportSecs * 0.36f;
        if (progress.importStart < 0.0f)
            return -1.0f;
        if (progress.importCancelledAt >= 0.0f)
            return progress.importCancelledAt;
        const float at = ProgressElapsed(progress.importStart, 0.0f);
        if (at >= ProgressDemos::kImportSecs + ProgressDemos::kImportDoneSecs)
        {
            progress.importStart = -1.0f;   // over: the toolbar has its Import button again
            return -1.0f;
        }
        return at;
    }

    uint64_t Application::ProgressLook()
    {
        // The import's whole percent and state, and what of the hierarchy still loads.
        const float imported = ImportElapsed();
        const uint64_t import = imported < 0.0f ? 0u
                              : progress.importCancelledAt >= 0.0f ? 1u
                              : 2u + uint64_t(std::min(imported / ProgressDemos::kImportSecs, 1.0f) * 100.0f);
        const float sinceStart = ProgressElapsed(progress.startedAt, 1.0f);
        const uint64_t loading = (sinceStart < ProgressDemos::kPropsLoadSecs ? 1u : 0u)
                               | (sinceStart < ProgressDemos::kTerrainStreamSecs ? 2u : 0u);
        return import << 8 | loading;
    }

    void Application::RenderBuildPanel()
    {
        const float at      = BuildElapsed();
        const bool  running = BuildRunning();

        // Each step done, under way, or still to come; cancelled, the one under way says so.
        ProgressTask steps[std::size(kBuildSteps)];
        std::string  notes[std::size(kBuildSteps)], details[std::size(kBuildSteps)];
        float stepStart = 0.0f;
        for (size_t i = 0; i < std::size(kBuildSteps); ++i)
        {
            const BuildStep& step = kBuildSteps[i];
            const float stepEnd = stepStart + step.seconds;
            ProgressTask& task = steps[i];
            task.name  = step.name;
            task.state = TaskState::Pending;
            if (at >= stepEnd)
            {
                task.state = TaskState::Done;
                notes[i]   = std::format("{:.1f} s", step.seconds);
            }
            else if (at >= stepStart)
            {
                if (progress.buildCancelledAt >= 0.0f)
                    notes[i] = "cancelled";
                else
                {
                    task.state = TaskState::Running;
                    task.value = (at - stepStart) / step.seconds;
                    const int left = int(std::ceil(stepEnd - at));
                    if (i == 2)      details[i] = std::format("Lightmap atlas, about {} s left", left);
                    else if (i == 4) details[i] = std::format("build-0.8.3-win64.zip, about {} s left", left);
                }
            }
            task.note   = notes[i];
            task.detail = details[i];
            stepStart = stepEnd;
        }

        BeginVertical(idManager.AllocateDynamicID("build"), buildPanelStyleRef, LayoutOverride{}.FlexGrow(1));
        BeginHorizontal(idManager.AllocateDynamicID("buildHeader"), buildHeaderStyleRef);
        Text(idManager.AllocateDynamicID("buildTarget"), "Windows player", buildTitleStyleRef);
        Text(idManager.AllocateDynamicID("buildConfig"), "Development", buildSubtitleStyleRef);
        EndHorizontal();

        ProgressSteps(idManager.AllocateDynamicID("buildSteps"), steps);

        // Cancel while it runs; Build, busy while it runs, keeping its width.
        BeginHorizontal(idManager.AllocateDynamicID("buildFooter"), buildFooterStyleRef);
        if (!running) BeginDisabled();
        Button(idManager.AllocateDynamicID("buildCancel"), "Cancel",
               [this] { progress.buildCancelledAt = BuildElapsed(); }, theme.dialogButtonStyleRef);
        if (!running) EndDisabled();
        BusyButton(idManager.AllocateDynamicID("buildRun"), { showcase.panelBuildIcon, "Build" }, running, "Building",
                   [this] {
                       progress.buildStart       = guiStateManager.Interaction().GetFrameTime();
                       progress.buildCancelledAt = -1.0f;
                   },
                   theme.dialogPrimaryButtonStyleRef);
        SetTooltip("Builds the Windows player");
        EndHorizontal();
        EndVertical();
    }

    std::vector<Application::RunningTask> Application::RunningWork()
    {
        std::vector<RunningTask> running;
        const auto percent = [](float value) { return std::format("{}%", int(std::clamp(value, 0.0f, 1.0f) * 100.0f)); };
        if (BuildRunning())
            running.push_back({ "Build Windows player", percent(BuildElapsed() / kBuildSecs) });
        const float indexed = ProgressElapsed(progress.startedAt, kIndexingHeld) / kIndexingSecs;
        const float thumbs  = ProgressElapsed(progress.startedAt, kThumbnailsHeld) / kThumbnailsSecs;
        if (!progress.backgroundCancelled[0] && indexed < 1.0f) running.push_back({ "Indexing assets", percent(indexed) });
        if (!progress.backgroundCancelled[1] && thumbs < 1.0f)  running.push_back({ "Generate thumbnails", percent(thumbs) });
        const bool  uploading = progress.uploadResumedAt >= 0.0f && !progress.held;
        const float uploaded  = uploading ? progress.uploadDone + ProgressElapsed(progress.uploadResumedAt, 0.0f) / ProgressDemos::kUploadSecs
                                          : progress.uploadDone;
        if (!progress.backgroundCancelled[2] && uploaded < 1.0f)
            running.push_back({ "Upload build", uploading ? percent(uploaded) : std::string("paused") });
        return running;
    }

    void Application::RenderStatusProgress()
    {
        const float indexed = ProgressElapsed(progress.startedAt, kIndexingHeld) / kIndexingSecs;
        const float thumbs  = ProgressElapsed(progress.startedAt, kThumbnailsHeld) / kThumbnailsSecs;
        const std::string count = std::format("{} / 3,800", int(std::clamp(indexed, 0.0f, 1.0f) * 3800.0f));

        // The background tasks: those running, and the paused upload.
        std::vector<ProgressTask> tasks;
        if (BuildRunning())
            tasks.push_back({ .name = "Build Windows player", .value = BuildElapsed() / kBuildSecs,
                              .onCancel = [this] { progress.buildCancelledAt = BuildElapsed(); } });
        // The upload: paused until resumed, then running on from where it was.
        const bool  uploading = progress.uploadResumedAt >= 0.0f && !progress.held;
        const float uploaded  = uploading ? progress.uploadDone + ProgressElapsed(progress.uploadResumedAt, 0.0f) / ProgressDemos::kUploadSecs
                                          : progress.uploadDone;
        const ProgressTask background[] = {
            { .name = "Indexing assets",     .value = indexed },
            { .name = "Generate thumbnails", .value = thumbs },
            { .name = "Upload build",        .value = uploaded, .state = uploading ? TaskState::Running : TaskState::Paused,
              .onPause = [this, uploading, uploaded] {
                  progress.uploadDone      = uploaded;
                  progress.uploadResumedAt = uploading ? -1.0f : guiStateManager.Interaction().GetFrameTime();
              } },
        };
        const ProgressTask* shown = nullptr;   // the status bar's: the first background one running
        for (int i = 0; i < 3; ++i)
        {
            if (progress.backgroundCancelled[i] || background[i].value >= 1.0f)
                continue;
            tasks.push_back(background[i]);
            tasks.back().onCancel = [this, i] { progress.backgroundCancelled[i] = true; };
            if (shown == nullptr && background[i].state == TaskState::Running)
                shown = &background[i];
        }

        if (shown != nullptr)
            StatusProgress(idManager.AllocateDynamicID("statusTask"),
                           ProgressTask{ .name = shown->name, .value = shown->value, .note = shown == &background[0] ? std::string_view(count) : std::string_view{} });
        BackgroundTasks(idManager.AllocateDynamicID("statusTasks"), tasks);
    }

    void Application::RenderViewportBusy(ID viewportID)
    {
        if (!ShadersCompiling() || viewportBusyWidth <= 0)
            return;
        // Along the view's top edge, and centered under it, the pill naming the work.
        IndeterminateProgressBar(idManager.AllocateDynamicID("viewportBusyBar"), viewportBusyBarStyleRef,
                                 LayoutOverride{}.Width(Pixels{ viewportBusyWidth }).Anchored(viewportID, AnchorPlacement::InsideTopLeft));
        BeginHorizontal(idManager.AllocateDynamicID("viewportBusy"), viewportBusyPillStyleRef,
                        LayoutOverride{}.Anchored(viewportID, AnchorPlacement::InsideTop | AnchorPlacement::CenterHorz,
                                                  { 0, int16_t(styleManager.Px(12)) }));
        Spinner(idManager.AllocateDynamicID("viewportBusySpinner"), SpinnerSize::Small);
        Text(idManager.AllocateDynamicID("viewportBusyText"), "Compiling shaders", viewportBusyTextStyleRef);
        EndHorizontal();
    }
} // namespace tachyon
