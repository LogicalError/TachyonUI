// Toasts and banners (controls_notifications.cpp): where toasts go, how long they stay, the
// "+N earlier" pill, their actions, sliding in and out; a banner's look and actions.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/elements/gui_element_storage.h>

#include <string>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class NotificationsTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            const int2 windowSize{ 1200, 800 };
            std::function<void()> content;   // built before the toasts

            void SetUp() override { ASSERT_TRUE(app.Initialize()); }
            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow(windowSize, ElementFlags::None, app.theme.mainWindowStyleRef);
                    if (content)
                        content();
                    app.Toasts();
                    app.EndRootWindow();
                });
            }
            // Time passes, a frame at a time.
            void Wait(uint32_t ms, uint32_t step = 50)
            {
                for (uint32_t t = 0; t < ms; t += step)
                {
                    app.AdvanceTime(step);
                    Frame();
                }
            }

            std::vector<ID> FindAllByStyle(StyleRef style) const
            {
                std::vector<ID> found;
                const auto& storage = app.elementStorage;
                for (auto it = storage.TopDownDepthScanBegin(); it != storage.TopDownDepthScanEnd(); ++it)
                    for (const Element& e : it->GetChildren(storage))
                        if (e.styleRef == style)
                            found.push_back(e.id);
                return found;
            }
            // The toasts on screen, top to bottom.
            std::vector<IntRect> Toasts() const
            {
                std::vector<IntRect> rects;
                for (const ID id : FindAllByStyle(app.theme.toastStyleRef))
                    rects.push_back(app.GetElementRect(id));
                return rects;
            }
            bool Shown(std::string_view title) const { return app.FindByText(title) != ID::Invalid(); }

            ToastID Show(std::string title, Severity severity = Severity::Info)
            {
                return app.ShowToast(ToastDesc{ .severity = severity, .title = std::move(title) });
            }
        };

        TEST_F(NotificationsTest, AToastSitsInTheWindowsCorner)
        {
            Show("Build succeeded");
            Wait(300);
            const std::vector<IntRect> toasts = Toasts();
            ASSERT_EQ(toasts.size(), 1u);
            EXPECT_EQ(toasts[0].w, app.styleManager.Px(380));
            EXPECT_EQ(toasts[0].x + toasts[0].w, windowSize.x - app.styleManager.Px(16));
            EXPECT_EQ(toasts[0].y + toasts[0].h, windowSize.y - app.styleManager.Px(16));
            EXPECT_TRUE(Shown("now"));
        }

        TEST_F(NotificationsTest, TheNewestIsNearestTheCorner)
        {
            Show("First");
            Show("Second");
            Wait(300);
            const IntRect first = app.GetElementRect(app.FindByText("First"));
            const IntRect second = app.GetElementRect(app.FindByText("Second"));
            EXPECT_LT(first.y, second.y);
            const std::vector<IntRect> toasts = Toasts();
            ASSERT_EQ(toasts.size(), 2u);
            EXPECT_EQ(toasts[1].y - (toasts[0].y + toasts[0].h), app.styleManager.Px(8)) << "8 px apart";
        }

        TEST_F(NotificationsTest, AToastIsOverWhatTheWindowBuildsBeforeIt)
        {
            // A button filling the window under it: the click goes to the toast.
            int under = 0;
            content = [&] { app.Button("Under", [&] { ++under; }, LayoutOverride{}.FlexGrow(1)); };
            Show("On top", Severity::Error);
            Wait(300);
            const std::vector<ID> dismiss = FindAllByStyle(app.theme.toastDismissStyleRef);
            ASSERT_EQ(dismiss.size(), 1u);
            app.ClickAt(app.CenterOf(dismiss[0]));
            Wait(300);
            EXPECT_FALSE(Shown("On top"));
            EXPECT_EQ(under, 0);

            // Built before the window's content, the toasts are still over it.
            Show("Before", Severity::Error);
            auto frameToastsFirst = [&] {
                app.Frame([&] {
                    app.BeginRootWindow(windowSize, ElementFlags::None, app.theme.mainWindowStyleRef);
                    app.Toasts();
                    content();
                    app.EndRootWindow();
                });
            };
            for (int i = 0; i < 6; ++i) { app.AdvanceTime(50); frameToastsFirst(); }
            const std::vector<ID> again = FindAllByStyle(app.theme.toastDismissStyleRef);
            ASSERT_EQ(again.size(), 1u);
            app.ClickAt(app.CenterOf(again[0]));
            for (int i = 0; i < 6; ++i) { app.AdvanceTime(50); frameToastsFirst(); }
            EXPECT_FALSE(Shown("Before"));
            EXPECT_EQ(under, 0);
        }

        TEST_F(NotificationsTest, OnlyErrorsStay)
        {
            Show("Info", Severity::Info);
            Show("Done", Severity::Success);
            Show("Careful", Severity::Warning);
            Show("Failed", Severity::Error);
            Wait(5800);
            EXPECT_TRUE(Shown("Info")) << "still there before 6 s";
            Wait(600);
            EXPECT_FALSE(Shown("Info"));
            EXPECT_FALSE(Shown("Done"));
            EXPECT_FALSE(Shown("Careful"));
            EXPECT_TRUE(Shown("Failed")) << "an error stays until dismissed";
            Wait(60000, 1000);
            EXPECT_TRUE(Shown("Failed"));
            EXPECT_TRUE(Shown("1 min"));
        }

        // A toast about something earlier says when it happened, and still stays its 6 s.
        TEST_F(NotificationsTest, AnEarlierToastSaysWhenItHappened)
        {
            app.ShowToast(ToastDesc{ .title = "Built", .age = 4.0f * 60.0f + 10.0f });
            app.ShowToast(ToastDesc{ .title = "Old", .age = 2.0f * 3600.0f });
            Wait(300);
            EXPECT_TRUE(Shown("4 min"));
            EXPECT_TRUE(Shown("2 h"));
            Wait(5000);
            EXPECT_TRUE(Shown("Built"));
            Wait(1200);
            EXPECT_FALSE(Shown("Built"));
        }

        TEST_F(NotificationsTest, ThePointerOverTheToastsKeepsThem)
        {
            Show("Info");
            Wait(300);
            app.InjectMouseMove(app.CenterOf(app.FindByText("Info")));
            Wait(8000, 200);
            EXPECT_TRUE(Shown("Info")) << "not while it's pointed at";
            app.InjectMouseMove({ 10, 10 });
            Wait(5000, 200);
            EXPECT_TRUE(Shown("Info")) << "the time it was pointed at didn't count";
            Wait(1500, 100);
            EXPECT_FALSE(Shown("Info"));
        }

        TEST_F(NotificationsTest, AProgressBarKeepsItUntilDone)
        {
            const ToastID upload = app.ShowToast(ToastDesc{ .title = "Uploading", .body = "build.zip", .progress = 0.48f });
            Wait(7000, 200);
            ASSERT_TRUE(Shown("Uploading"));
            ToastDesc* desc = app.FindToast(upload);
            ASSERT_NE(desc, nullptr);
            EXPECT_EQ(FindAllByStyle(app.theme.toastProgressStyleRef).size(), 1u);
            desc->progress = 1.0f;
            Wait(7000, 200);
            EXPECT_FALSE(Shown("Uploading"));
            EXPECT_EQ(app.FindToast(upload), nullptr) << "gone";
        }

        TEST_F(NotificationsTest, ItsCloseButtonAndDismissToastTakeItAway)
        {
            Show("Failed", Severity::Error);
            const ToastID other = Show("Also failed", Severity::Error);
            Wait(300);
            const std::vector<ID> dismiss = FindAllByStyle(app.theme.toastDismissStyleRef);
            ASSERT_EQ(dismiss.size(), 2u);
            app.ClickAt(app.CenterOf(dismiss[0]));
            Wait(300);
            EXPECT_FALSE(Shown("Failed"));
            EXPECT_TRUE(Shown("Also failed"));
            app.DismissToast(other);
            EXPECT_EQ(app.FindToast(other), nullptr) << "going: no longer to change";
            Wait(300);
            EXPECT_TRUE(Toasts().empty());
        }

        TEST_F(NotificationsTest, AnActionRunsAndTheToastGoes)
        {
            int opened = 0, ran = 0;
            app.ShowToast(ToastDesc{ .severity = Severity::Success, .title = "Build succeeded", .body = "Windows x64",
                                     .actions = { { "Open folder", [&] { ++opened; } }, { "Run", [&] { ++ran; } } } });
            Wait(300);
            EXPECT_EQ(FindAllByStyle(app.theme.toastActionStyleRef).size(), 1u) << "the first in the link color";
            EXPECT_EQ(FindAllByStyle(app.theme.toastActionSecondaryStyleRef).size(), 1u);
            app.ClickAt(app.CenterOf(app.FindByText("Run")));
            Wait(300);
            EXPECT_EQ(ran, 1);
            EXPECT_EQ(opened, 0);
            EXPECT_FALSE(Shown("Build succeeded"));
        }

        TEST_F(NotificationsTest, MoreThanFourFoldIntoAPill)
        {
            for (int i = 0; i < 6; ++i)
                Show("Toast " + std::to_string(i), Severity::Error);
            Wait(300);
            EXPECT_EQ(Toasts().size(), 4u);
            EXPECT_FALSE(Shown("Toast 0"));
            EXPECT_FALSE(Shown("Toast 1"));
            EXPECT_TRUE(Shown("Toast 5"));
            const ID pill = app.FindByText("+2 earlier");
            ASSERT_NE(pill, ID::Invalid());
            EXPECT_LT(app.GetElementRect(pill).y, Toasts().front().y) << "above the toasts";

            app.ClickAt(app.CenterOf(pill));
            Wait(100);
            EXPECT_EQ(Toasts().size(), 6u) << "the pill shows them all";
            EXPECT_EQ(app.FindByText("+2 earlier"), ID::Invalid());

            // Down to four, it folds again when more come.
            for (const ID id : FindAllByStyle(app.theme.toastDismissStyleRef))
                if (id != ID::Invalid()) { app.ClickAt(app.CenterOf(id)); Wait(300); break; }
            for (const ID id : FindAllByStyle(app.theme.toastDismissStyleRef))
                if (id != ID::Invalid()) { app.ClickAt(app.CenterOf(id)); Wait(300); break; }
            ASSERT_EQ(Toasts().size(), 4u);
            Show("Toast 6", Severity::Error);
            Wait(300);
            EXPECT_EQ(Toasts().size(), 4u);
            EXPECT_NE(app.FindByText("+1 earlier"), ID::Invalid());
        }

        TEST_F(NotificationsTest, ItSlidesInFromTheRightAndOut)
        {
            const ToastID toast = Show("Sliding", Severity::Error);
            app.AdvanceTime(20);
            Frame();
            const int32_t settledX = windowSize.x - app.styleManager.Px(16) - app.styleManager.Px(380);
            const int32_t early = Toasts().at(0).x;
            EXPECT_GT(early, settledX) << "still coming in from the right";
            Wait(300);
            EXPECT_EQ(Toasts().at(0).x, settledX);

            app.DismissToast(toast);
            app.AdvanceTime(40);
            Frame();
            ASSERT_EQ(Toasts().size(), 1u) << "going, not gone";
            EXPECT_GT(Toasts().at(0).x, settledX);
            Wait(200);
            EXPECT_TRUE(Toasts().empty());
        }

        TEST_F(NotificationsTest, WithReducedMotionItDoesntSlide)
        {
            app.SetReducedMotion(true);
            const ToastID toast = Show("Still", Severity::Error);
            app.AdvanceTime(20);
            Frame();
            EXPECT_EQ(Toasts().at(0).x, windowSize.x - app.styleManager.Px(16) - app.styleManager.Px(380));
            app.DismissToast(toast);
            Frame();
            EXPECT_TRUE(Toasts().empty());
        }

        TEST_F(NotificationsTest, AMonospaceBodyForCode)
        {
            app.ShowToast(ToastDesc{ .severity = Severity::Error, .title = "Shader compile failed",
                                     .body = "Terrain.shader:118", .monospaceBody = true });
            app.ShowToast(ToastDesc{ .severity = Severity::Warning, .title = "3 textures exceed 4K", .body = "Downscaled on import." });
            Wait(300);
            EXPECT_EQ(FindAllByStyle(app.theme.toastBodyMonoStyleRef).size(), 1u);
            EXPECT_EQ(FindAllByStyle(app.theme.toastBodyStyleRef).size(), 1u);
            EXPECT_EQ(FindAllByStyle(app.theme.toastCircleStyleRefs[int(Severity::Error)]).size(), 1u);
            EXPECT_EQ(FindAllByStyle(app.theme.toastCircleStyleRefs[int(Severity::Warning)]).size(), 1u);
        }

        // A long body wraps onto as many lines as it takes, inside the toast; a long title
        // stays on one line, all of it in its tooltip.
        TEST_F(NotificationsTest, ALongBodyWrapsAndALongTitleIsCutShort)
        {
            const std::string longBody  = "Terrain.shader:118 - undeclared identifier uv2 in the vertex stage of the terrain pass";
            const std::string longTitle = "Shader compile failed in every one of the twelve terrain material variants";
            app.ShowToast(ToastDesc{ .severity = Severity::Error, .title = longTitle, .body = longBody, .monospaceBody = true });
            app.ShowToast(ToastDesc{ .severity = Severity::Error, .title = "Short", .body = "One line." });
            Wait(300);
            const std::vector<IntRect> toasts = Toasts();
            ASSERT_EQ(toasts.size(), 2u);
            const IntRect body = app.GetElementRect(FindAllByStyle(app.theme.toastBodyMonoStyleRef).at(0));
            const IntRect shortBody = app.GetElementRect(FindAllByStyle(app.theme.toastBodyStyleRef).at(0));
            EXPECT_LE(body.x + body.w, toasts[0].x + toasts[0].w - app.styleManager.Px(12)) << "inside the toast";
            EXPECT_GE(body.h, 2 * shortBody.h) << "on more than one line";
            EXPECT_GT(toasts[0].h, toasts[1].h) << "the toast grows with it";
            EXPECT_EQ(toasts[1].y - (toasts[0].y + toasts[0].h), app.styleManager.Px(8)) << "the next one below it";

            const ID title = app.FindByText(longTitle);
            EXPECT_EQ(app.GetElementRect(title).h, app.GetElementRect(app.FindByText("Short")).h) << "one line";
            app.InjectMouseMove(app.CenterOf(title));
            Wait(1500, 100);
            EXPECT_EQ(app.CountByText(longTitle), 2) << "its tooltip has all of it";
        }

        // ---- Banners ----

        TEST_F(NotificationsTest, ABannerRunsAcrossWithItsActionsOnTheRight)
        {
            int saved = 0, discarded = 0, editable = 0;
            const ID warning = app.idManager.CreateStaticID();
            const ID info    = app.idManager.CreateStaticID();
            const BannerAction actions[] = {
                { "Discard", [&] { ++discarded; } },
                { "Save", [&] { ++saved; }, BannerActionKind::Primary },
            };
            const BannerAction link[] = { { "Make editable", [&] { ++editable; }, BannerActionKind::Link } };
            content = [&] {
                app.BeginVertical(LayoutOverride{}.Width(480));
                app.Banner(warning, Severity::Warning, "Scene has unsaved changes", actions);
                app.Banner(info, Severity::Info, "Read-only", link);
                app.EndVertical();
            };
            Frame();
            Frame();

            const IntRect banner = app.GetElementRect(warning);
            EXPECT_EQ(banner.w, 480) << "full width";
            const IntRect save = app.GetElementRect(app.FindByText("Save"));
            EXPECT_EQ(save.x + save.w, banner.x + banner.w - app.styleManager.Px(12)) << "right-aligned, inside the padding";
            EXPECT_LT(app.GetElementRect(app.FindByText("Discard")).x, save.x);
            EXPECT_EQ(FindAllByStyle(app.theme.bannerStyleRefs[int(Severity::Warning)]).size(), 1u);
            EXPECT_EQ(FindAllByStyle(app.theme.bannerMessageStyleRefs[int(Severity::Info)]).size(), 1u);
            EXPECT_EQ(FindAllByStyle(app.theme.bannerPrimaryStyleRef).size(), 1u);
            EXPECT_EQ(FindAllByStyle(app.theme.bannerSecondaryStyleRef).size(), 1u);
            EXPECT_EQ(FindAllByStyle(app.theme.bannerLinkStyleRef).size(), 1u);

            app.ClickAt(app.CenterOf(app.FindByText("Save")));
            Frame();
            app.ClickAt(app.CenterOf(app.FindByText("Make editable")));
            Frame();
            EXPECT_EQ(saved, 1);
            EXPECT_EQ(editable, 1);
            EXPECT_EQ(discarded, 0);
        }
    }
}
