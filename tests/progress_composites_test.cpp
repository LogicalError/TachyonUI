// Progress built from the bars and spinners: a task in full (TaskProgress), the steps of a
// job (ProgressSteps), a task in a status bar (StatusProgress) and the pill and popover of
// the tasks running in the background (BackgroundTasks).

#include <gtest/gtest.h>

#include "headless_gui_app.h"
#include "skribidi_test_fixture.h"

#include <gui_core/graphics/gui_custom_draw_context.h>
#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/elements/gui_element_flags.h>

#include <string>
#include <utility>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;
    using GUITest::StubGUIRenderer;

    namespace
    {
        class ProgressCompositesTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;

            void SetUp()    override { ASSERT_TRUE(app.Initialize()); }
            void TearDown() override { app.Cleanup(); }

            void Frame(const std::function<void()>& build)
            {
                app.Frame([&] {
                    app.BeginRootWindow();
                    build();
                    app.EndRootWindow();
                });
            }

            // Every element in `style`, top to bottom.
            std::vector<ID> WithStyle(StyleRef style)
            {
                std::vector<ID> found;
                const auto& storage = app.elementStorage;
                for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
                    for (const Element& e : it->GetChildren(storage))
                        if (e.styleRef == style)
                            found.push_back(e.id);
                return found;
            }

            const Element& ElementOf(ID id) { return app.elementStorage.GetElementUnsafe(app.elementStorage.FindElementRefByID(id)); }
            IconID IconOf(ID id) { return app.ownedTextSystem.IconOf(ElementOf(id).textID); }
            bool   Animated(ID id) { return (ElementOf(id).flags & ElementFlags::Animated) != ElementFlags::None; }

            // The fill a bar draws: its width and box style.
            struct Fill { int32_t w = -1; uint32_t style = 0; };
            Fill FillOf(ID bar)
            {
                StubGUIRenderer renderer;
                const ElementRef ref = app.elementStorage.FindElementRefByID(bar);
                const ElementCustomDraw* draw = app.elementStorage.GetCustomDraw(ref);
                if (draw == nullptr || !draw->callback)
                    return {};
                const ElementRect* rects = app.elementStorage.GetElementRectPtr(ref);
                CustomDrawContext ctx{ renderer, app.elementStorage, ref, rects->rect, rects->innerRect,
                                       IntRect{ -4000, -4000, 8000, 8000 }, float3x2{}, int2{ 0, 0 } };
                draw->callback(ctx);
                if (renderer.boxes.empty())
                    return { 0, 0 };
                return { renderer.boxes[0].w, renderer.boxStyles[0] };
            }

            uint32_t SlotOf(StyleRef style) { return GUIRenderer::StyleIndex(style.visualBoxStyleIndex, ElementState::None); }

            void Click(ID id)
            {
                app.ClickAt(app.CenterOf(id));
            }
        };

        // ---------------------------------------------------------------------
        // TaskProgress
        // ---------------------------------------------------------------------

        // The name and percent over the bar, a cancel button after them, the detail under it.
        TEST_F(ProgressCompositesTest, ARunningTaskShowsItsNamePercentBarAndDetail)
        {
            const ID task = app.idManager.CreateStaticID();
            int cancels = 0;
            auto frame = [&] {
                Frame([&] {
                    app.TaskProgress(task, ProgressTask{ .name = "Importing character.fbx", .value = 0.648f,
                                                         .detail = "12.4 MB of 19.3 MB", .onCancel = [&] { ++cancels; } },
                                     LayoutOverride{}.Width(Pixels{ 300 }));
                });
            };
            frame();
            const ID name = app.FindByText("Importing character.fbx"), percent = app.FindByText("64%");
            const ID detail = app.FindByText("12.4 MB of 19.3 MB");
            ASSERT_TRUE(name != ID::Invalid());
            ASSERT_TRUE(percent != ID::Invalid()) << "rounded down";
            ASSERT_TRUE(detail != ID::Invalid());
            EXPECT_EQ(ElementOf(name).styleRef, app.theme.taskProgressNameStyleRef);
            EXPECT_EQ(ElementOf(percent).styleRef, app.theme.taskProgressPercentStyleRef);
            EXPECT_EQ(ElementOf(detail).styleRef, app.theme.taskProgressDetailStyleRef);
            const std::vector<ID> cancel = WithStyle(app.theme.taskProgressCancelStyleRef);
            ASSERT_EQ(cancel.size(), 1u);
            const std::vector<ID> bars = WithStyle(app.theme.progressBarStyleRef);
            ASSERT_EQ(bars.size(), 1u);

            // One row over the bar, the detail under it.
            const IntRect n = app.GetElementRect(name), p = app.GetElementRect(percent), c = app.GetElementRect(cancel[0]);
            const IntRect b = app.GetElementRect(bars[0]), d = app.GetElementRect(detail);
            EXPECT_LT(n.x, p.x);
            EXPECT_LT(p.x, c.x);
            EXPECT_EQ(c.x + c.w, b.x + b.w) << "the cancel button at the end";
            EXPECT_GE(b.y, n.y + n.h);
            EXPECT_GE(d.y, b.y + b.h);
            EXPECT_EQ(b.w, 300);
            const Fill fill = FillOf(bars[0]);
            EXPECT_EQ(fill.w, 194);
            EXPECT_EQ(fill.style, SlotOf(app.theme.progressFillStyleRef));

            Click(cancel[0]);
            frame();
            frame();
            EXPECT_EQ(cancels, 1);
        }

        // A note takes the percent's place; paused, it says so in the warning color; with
        // nothing known the bar slides and there's no percent.
        TEST_F(ProgressCompositesTest, ATaskShowsANoteOrPausedOrSlides)
        {
            ProgressTask task{ .name = "Export", .value = 0.5f, .note = "about 3 s left" };
            Frame([&] { app.TaskProgress(task); });
            ASSERT_TRUE(app.FindByText("about 3 s left") != ID::Invalid());
            EXPECT_EQ(ElementOf(app.FindByText("about 3 s left")).styleRef, app.theme.taskProgressNoteStyleRef);
            EXPECT_FALSE(app.FindByText("50%") != ID::Invalid());
            EXPECT_TRUE(WithStyle(app.theme.taskProgressCancelStyleRef).empty()) << "no cancel without onCancel";

            task.note  = {};
            task.state = TaskState::Paused;
            Frame([&] { app.TaskProgress(task); });
            EXPECT_TRUE(app.FindByText("paused") != ID::Invalid());
            EXPECT_EQ(FillOf(WithStyle(app.theme.progressBarStyleRef).at(0)).style, SlotOf(app.theme.progressFillPausedStyleRef));

            task.state = TaskState::Running;
            task.value = -1.0f;
            Frame([&] { app.TaskProgress(task); });
            const std::vector<ID> bars = WithStyle(app.theme.progressBarStyleRef);
            ASSERT_EQ(bars.size(), 1u);
            EXPECT_TRUE(Animated(bars[0])) << "sliding";
            EXPECT_FALSE(app.FindByText("0%") != ID::Invalid());

            // Only running work slides: paused with nothing known, it's an empty bar.
            task.state = TaskState::Paused;
            Frame([&] { app.TaskProgress(task); });
            const ID paused = WithStyle(app.theme.progressBarStyleRef).at(0);
            EXPECT_FALSE(Animated(paused));
            EXPECT_EQ(FillOf(paused).w, 0);
        }

        // Failed: the error icon, the bar in the error color, why in error text, and Retry in
        // place of the percent and the cancel button.
        TEST_F(ProgressCompositesTest, AFailedTaskSaysWhyAndOffersRetry)
        {
            int retries = 0, cancels = 0;
            auto frame = [&] {
                Frame([&] {
                    app.TaskProgress(app.idManager.AllocateDynamicID("bake"),
                                     ProgressTask{ .name = "Bake failed at 73%", .value = 0.73f, .state = TaskState::Failed,
                                                   .note = "1:02", .detail = "Out of GPU memory", .onCancel = [&] { ++cancels; },
                                                   .onRetry = [&] { ++retries; } },
                                     LayoutOverride{}.Width(Pixels{ 200 }));
                });
            };
            frame();
            const ID name = app.FindByText("Bake failed at 73%");
            ASSERT_TRUE(name != ID::Invalid());
            EXPECT_EQ(ElementOf(name).styleRef, app.theme.taskProgressFailedStyleRef);
            EXPECT_EQ(IconOf(name), app.theme.invalidIcon);
            EXPECT_EQ(ElementOf(app.FindByText("Out of GPU memory")).styleRef, app.theme.taskProgressErrorStyleRef);
            EXPECT_FALSE(app.FindByText("73%") != ID::Invalid());
            EXPECT_FALSE(app.FindByText("1:02") != ID::Invalid()) << "Retry in place of the note";
            EXPECT_TRUE(WithStyle(app.theme.taskProgressCancelStyleRef).empty());
            const Fill fill = FillOf(WithStyle(app.theme.progressBarStyleRef).at(0));
            EXPECT_EQ(fill.w, 146);
            EXPECT_EQ(fill.style, SlotOf(app.theme.progressFillFailedStyleRef));

            const ID retry = app.FindByText("Retry");
            ASSERT_TRUE(retry != ID::Invalid());
            EXPECT_EQ(ElementOf(retry).styleRef, app.theme.taskProgressRetryStyleRef);
            Click(retry);
            frame();
            frame();
            EXPECT_EQ(retries, 1);
            EXPECT_EQ(cancels, 0);
        }

        // Done: one line, the check, the name and how long it took; no bar.
        TEST_F(ProgressCompositesTest, ADoneTaskIsOneLineWithACheck)
        {
            Frame([&] {
                app.TaskProgress(ProgressTask{ .name = "Import complete", .value = 1.0f, .state = TaskState::Done,
                                               .note = "2.1 s", .detail = "not shown", .onCancel = [] {} });
            });
            const ID name = app.FindByText("Import complete");
            ASSERT_TRUE(name != ID::Invalid());
            EXPECT_EQ(ElementOf(name).styleRef, app.theme.taskProgressDoneStyleRef);
            EXPECT_EQ(IconOf(name), app.theme.menuCheckIcon);
            EXPECT_EQ(ElementOf(app.FindByText("2.1 s")).styleRef, app.theme.taskProgressNoteStyleRef);
            EXPECT_TRUE(WithStyle(app.theme.progressBarStyleRef).empty());
            EXPECT_FALSE(app.FindByText("not shown") != ID::Invalid());
            EXPECT_TRUE(WithStyle(app.theme.taskProgressCancelStyleRef).empty());
        }

        // ---------------------------------------------------------------------
        // ProgressSteps
        // ---------------------------------------------------------------------

        // A glyph a step, a small bar under the running one only, and each note or percent.
        TEST_F(ProgressCompositesTest, StepsShowWhereEachIs)
        {
            const ProgressTask steps[] = {
                { .name = "Compile scripts", .state = TaskState::Done, .note = "4.2 s" },
                { .name = "Bake lighting",   .value = 0.42f, .state = TaskState::Running },
                { .name = "Package assets",  .value = 0.5f, .state = TaskState::Pending },
                { .name = "Upload",          .state = TaskState::Paused },
                { .name = "Notify",          .state = TaskState::Failed },
            };
            Frame([&] { app.ProgressSteps(steps, LayoutOverride{}.Width(Pixels{ 300 })); });

            const std::vector<ID> done = WithStyle(app.theme.progressStepDoneStyleRef);
            ASSERT_EQ(done.size(), 1u);
            EXPECT_EQ(ElementOf(done[0]).iconID, app.theme.boldCheckIcon);
            const std::vector<ID> failed = WithStyle(app.theme.progressStepFailedStyleRef);
            ASSERT_EQ(failed.size(), 1u);
            EXPECT_EQ(ElementOf(failed[0]).iconID, app.theme.boldCrossIcon);
            EXPECT_EQ(WithStyle(app.theme.progressStepWaitingStyleRef).size(), 1u);
            EXPECT_EQ(WithStyle(app.theme.progressStepPausedStyleRef).size(), 1u);

            EXPECT_EQ(ElementOf(app.FindByText("Package assets")).styleRef, app.theme.progressStepPendingStyleRef);
            EXPECT_EQ(ElementOf(app.FindByText("Bake lighting")).styleRef, app.theme.progressStepNameStyleRef);
            EXPECT_EQ(ElementOf(app.FindByText("Upload")).styleRef, app.theme.progressStepNameStyleRef);
            EXPECT_TRUE(app.FindByText("4.2 s") != ID::Invalid());
            EXPECT_TRUE(app.FindByText("42%") != ID::Invalid());
            EXPECT_TRUE(app.FindByText("paused") != ID::Invalid());
            EXPECT_FALSE(app.FindByText("50%") != ID::Invalid()) << "a percent only while running";

            // The running step's spinner, left of its name, and its bar under the name.
            const ID name = app.FindByText("Bake lighting");
            const std::vector<ID> bars = WithStyle(app.theme.progressBarSmallStyleRef);
            ASSERT_EQ(bars.size(), 1u);
            EXPECT_GE(app.GetElementRect(bars[0]).y, app.GetElementRect(name).y + app.GetElementRect(name).h);
            EXPECT_EQ(app.GetElementRect(bars[0]).x, app.GetElementRect(name).x);
            EXPECT_EQ(FillOf(bars[0]).w, int32_t(float(app.GetElementRect(bars[0]).w) * 0.42f + 0.5f));
            const IntRect glyph = app.GetElementRect(done[0]);
            EXPECT_EQ(glyph.w, app.styleManager.Px(16));
            EXPECT_EQ(glyph.h, app.styleManager.Px(16));
            EXPECT_LT(glyph.x + glyph.w, app.GetElementRect(app.FindByText("Compile scripts")).x);

            // In order, top to bottom.
            int32_t y = -1;
            for (const char* step : { "Compile scripts", "Bake lighting", "Package assets", "Upload", "Notify" })
            {
                EXPECT_GT(app.GetElementRect(app.FindByText(step)).y, y) << step;
                y = app.GetElementRect(app.FindByText(step)).y;
            }
        }

        // ---------------------------------------------------------------------
        // StatusProgress
        // ---------------------------------------------------------------------

        TEST_F(ProgressCompositesTest, AStatusBarTaskHasASpinnerABarAndACount)
        {
            ProgressTask task{ .name = "Indexing assets", .value = 0.33f, .note = "1,240 / 3,800" };
            Frame([&] {
                app.BeginStatusBar();
                app.StatusProgress(task);
                app.EndStatusBar();
            });
            const ID name = app.FindByText("Indexing assets"), count = app.FindByText("1,240 / 3,800");
            ASSERT_TRUE(name != ID::Invalid());
            ASSERT_TRUE(count != ID::Invalid());
            EXPECT_EQ(ElementOf(name).styleRef, app.theme.statusTextStyleRef);
            EXPECT_EQ(ElementOf(count).styleRef, app.theme.statusTextMonoStyleRef);
            const std::vector<ID> bars = WithStyle(app.theme.progressBarSmallStyleRef);
            ASSERT_EQ(bars.size(), 1u);
            EXPECT_EQ(app.GetElementRect(bars[0]).w, app.styleManager.Px(120));
            EXPECT_EQ(app.GetElementRect(bars[0]).h, app.styleManager.Px(4));
            EXPECT_LT(app.GetElementRect(name).x, app.GetElementRect(bars[0]).x);
            EXPECT_LT(app.GetElementRect(bars[0]).x, app.GetElementRect(count).x);

            // The spinner, 12px, before the name.
            auto spinners = [&] {
                std::vector<ID> found;
                for (const ID id : WithStyle(app.theme.emptyHorzStyleRef))
                    if (Animated(id))
                        found.push_back(id);
                return found;
            };
            ASSERT_EQ(spinners().size(), 1u);
            const IntRect spinner = app.GetElementRect(spinners()[0]);
            EXPECT_EQ(spinner.w, app.styleManager.Px(12));
            EXPECT_LT(spinner.x, app.GetElementRect(name).x);

            task.state = TaskState::Paused;
            task.note  = {};
            Frame([&] {
                app.BeginStatusBar();
                app.StatusProgress(task);
                app.EndStatusBar();
            });
            EXPECT_TRUE(spinners().empty()) << "no spinner while paused";
            EXPECT_TRUE(app.FindByText("paused") != ID::Invalid());

            // Done, the bar is full, whatever the value says.
            task.state = TaskState::Done;
            Frame([&] {
                app.BeginStatusBar();
                app.StatusProgress(task);
                app.EndStatusBar();
            });
            EXPECT_EQ(FillOf(WithStyle(app.theme.progressBarSmallStyleRef).at(0)).w, app.styleManager.Px(120));
        }

        // ---------------------------------------------------------------------
        // BackgroundTasks
        // ---------------------------------------------------------------------

        // A pill with the count; a click opens the list above it; each row a name, a bar, a
        // state and a cancel button; the pill, a press outside or Escape close it.
        TEST_F(ProgressCompositesTest, ThePillOpensTheBackgroundTasks)
        {
            const ID pill = app.idManager.CreateStaticID();
            int cancelled = -1;
            std::vector<ProgressTask> tasks = {
                { .name = "Bake lighting",       .value = 0.42f, .onCancel = [&] { cancelled = 0; } },
                { .name = "Generate thumbnails", .value = 0.78f, .onCancel = [&] { cancelled = 1; } },
                { .name = "Upload build",        .value = 0.15f, .state = TaskState::Paused },
            };
            bool open = false;
            ID closeWindow = ID::Invalid();   // set: that window closed as the OS would, next frame
            auto frame = [&] {
                Frame([&] {
                    if (closeWindow != ID::Invalid())
                        app.windowManager->RequestUserClose(std::exchange(closeWindow, ID::Invalid()));
                    // The status bar along the bottom of a panel, as it is in an app.
                    app.BeginVertical(app.idManager.AllocateDynamicID("panel"), app.theme.emptyVertStyleRef,
                                      LayoutOverride{}.Width(Pixels{ 1000 }).Height(Pixels{ 700 }));
                    app.BeginVertical(app.idManager.AllocateDynamicID("above"), app.theme.emptyVertStyleRef,
                                      LayoutOverride{}.FlexGrow(1));
                    app.EndVertical();
                    app.BeginStatusBar();
                    app.StatusText("Ready", LayoutOverride::Auto().FlexGrow(1));
                    open = app.BackgroundTasks(pill, tasks);
                    app.EndStatusBar();
                    app.EndVertical();
                });
            };
            frame();
            EXPECT_EQ(app.TextOf(pill), "3 tasks");
            EXPECT_EQ(ElementOf(pill).styleRef, app.theme.tasksPillStyleRef);
            EXPECT_FALSE(open);
            EXPECT_FALSE(app.FindByText("Background tasks") != ID::Invalid());

            Click(pill);
            frame();
            frame();
            ASSERT_TRUE(open);
            ASSERT_TRUE(app.FindByText("Background tasks") != ID::Invalid());
            EXPECT_TRUE(app.FindByText("2 running") != ID::Invalid());
            for (const char* text : { "Bake lighting", "Generate thumbnails", "Upload build", "42%", "78%", "paused" })
                EXPECT_TRUE(app.FindByText(text) != ID::Invalid()) << text;
            const std::vector<ID> bars = WithStyle(app.theme.progressBarSmallStyleRef);
            ASSERT_EQ(bars.size(), 3u);
            EXPECT_EQ(FillOf(bars[2]).style, SlotOf(app.theme.progressFillPausedStyleRef));
            const std::vector<ID> cancels = WithStyle(app.theme.taskProgressCancelStyleRef);
            ASSERT_EQ(cancels.size(), 2u) << "none for the task without onCancel";

            // Above the pill, its right edge on the pill's.
            const IntRect p = app.GetElementRect(pill);
            const std::vector<ID> popover = WithStyle(app.theme.tasksPopoverStyleRef);
            ASSERT_EQ(popover.size(), 1u);
            const IntRect w = app.GetElementRect(popover[0]);
            EXPECT_LE(w.y + w.h, p.y) << "popover " << w.x << "," << w.y << " " << w.w << "x" << w.h
                                      << ", pill " << p.x << "," << p.y << " " << p.w << "x" << p.h;
            EXPECT_EQ(w.x + w.w, p.x + p.w);

            // A cancel button cancels that task, and the list stays open.
            // The popover is its own window: the click arrives there.
            app.mouseWindow = popover[0];
            Click(cancels[1]);
            frame();
            frame();
            app.mouseWindow = ID::Invalid();
            EXPECT_EQ(cancelled, 1);
            EXPECT_TRUE(open);

            // The pill closes it again.
            Click(pill);
            frame();
            frame();
            EXPECT_FALSE(open);
            EXPECT_FALSE(app.FindByText("Background tasks") != ID::Invalid());

            // A press elsewhere closes it.
            Click(pill);
            frame();
            frame();
            ASSERT_TRUE(open);
            app.ClickAt(app.CenterOf(app.FindByText("Ready")));
            frame();
            frame();
            EXPECT_FALSE(open);

            // So does Escape.
            Click(pill);
            frame();
            frame();
            ASSERT_TRUE(open);
            app.InjectKey(KeyCode::Escape);
            frame();
            frame();
            EXPECT_FALSE(open);

            // Closed by the OS (its window gone): closed here too.
            Click(pill);
            frame();
            frame();
            ASSERT_TRUE(open);
            closeWindow = WithStyle(app.theme.tasksPopoverStyleRef).at(0);
            frame();
            EXPECT_FALSE(open);

            // One task, and none.
            tasks.resize(1);
            frame();
            EXPECT_EQ(app.TextOf(pill), "1 task");
            tasks.clear();
            frame();
            EXPECT_FALSE(app.HasElement(pill));
        }
    }
}
