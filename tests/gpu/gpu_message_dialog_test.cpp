// A MessageDialog on a real device, in its own OS window: a message of two lines and a
// list of items all show, the window as big as what's in it.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    using namespace tachyon;

    constexpr const char* kMessage = "These documents have unsaved changes. Quitting without saving discards them.";

    class DialogApp : public GUIApplicationInternals
    {
    public:
        ID windowID = ID::Invalid(), dialogID = ID::Invalid();
        IntRect dialogLaid{}, dialogOS{};
        struct Laid { std::string text; IntRect rect; };
        std::vector<Laid> texts;   // each text in the dialog, where it was laid out
        int frame = 0;

        void OnGUI() override
        {
            static constexpr MessageButton kButtons[] = {
                { .label = "Discard", .kind = MessageButtonKind::Danger, .alignLeft = true },
                { .label = "Cancel" },
                { .label = "Save All", .kind = MessageButtonKind::Primary },
            };
            const std::string names[] = { "Desert.level", "Forest.level" };
            const MessageDialogItem items[] = { { .name = names[0] }, { .name = names[1] } };
            if (BeginWindow(windowID, "Owner", theme.windowStyleRef, LayoutOverride{}.Size(900, 600)))
            {
                (void)MessageDialog(dialogID, MessageDialogDesc{
                    .icon = MessageIcon::Quit, .title = "Quit Test?", .message = kMessage,
                    .buttons = kButtons, .defaultButton = 2, .cancelButton = 1, .items = items });
            }
            EndWindow();
            if (++frame >= 12)
            {
                dialogOS = windowManager->GetWindowRect(dialogID);
                applicationQuit = true;
            }
        }

        void Collect(ElementRef ref)
        {
            for (const Element& child : elementStorage.GetChildrenSpan(ref))
            {
                const ElementRef childRef = elementStorage.FindElementRefByID(child.id);
                if (!childRef.IsValid())
                    continue;
                if (child.textID != TextID::Invalid)
                    texts.push_back({ std::string(textSystem->GetText(child.textID)), elementStorage.GetElementRectPtr(childRef)->rect });
                Collect(childRef);
            }
        }

        bool Start()
        {
            if (!Initialize())
                return false;
            windowID = idManager.CreateStaticID();
            dialogID = idManager.CreateStaticID();
            m_frame.OnPostLayout([this](FrameContext&)
            {
                const ElementRef dialog = elementStorage.FindElementRefByID(dialogID);
                if (!dialog.IsValid())
                    return;
                dialogLaid = elementStorage.GetElementRectPtr(dialog)->rect;
                texts.clear();
                Collect(dialog);
            });
            return true;
        }
    };

    TEST(GpuMessageDialog, ItsMessageAndItemsShowWhole)
    {
        DialogApp app;
        if (!app.Start())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        ASSERT_TRUE(app.Run());
        ASSERT_GT(app.dialogLaid.h, 0);
        EXPECT_EQ(app.dialogOS.size, app.dialogLaid.size) << "its window as big as laid out";
        for (const char* text : { kMessage, "Desert.level", "Forest.level" })
        {
            const auto it = std::find_if(app.texts.begin(), app.texts.end(), [&](const auto& t) { return t.text == text; });
            ASSERT_NE(it, app.texts.end()) << text;
            EXPECT_GT(it->rect.h, 0) << text;
            EXPECT_GE(it->rect.y, app.dialogLaid.y) << text;
            EXPECT_LE(it->rect.y + it->rect.h, app.dialogLaid.y + app.dialogLaid.h) << text << " is cut off";
        }
        const auto message = std::find_if(app.texts.begin(), app.texts.end(), [](const auto& t) { return t.text == kMessage; });
        const auto title   = std::find_if(app.texts.begin(), app.texts.end(), [](const auto& t) { return t.text == "Quit Test?"; });
        ASSERT_NE(title, app.texts.end());
        EXPECT_GT(message->rect.h, title->rect.h) << "the message wraps to a second line";
        EXPECT_LE(message->rect.x + message->rect.w, app.dialogLaid.x + app.dialogLaid.w) << "and stays inside the dialog";
        const auto item = std::find_if(app.texts.begin(), app.texts.end(), [](const auto& t) { return t.text == "Desert.level"; });
        EXPECT_GE(item->rect.h, title->rect.h * 3 / 4) << "an item's name as tall as text";
        app.Cleanup();
    }
}
