// UndoHistory: steps undo and redo in order, a push drops the undone steps, and steps
// with the same key merge until the merge is ended.

#include <gtest/gtest.h>

#include <gui_core/input/gui_undo_history.h>

#include <string>

namespace tachyon
{
    namespace
    {
        // A step that sets `value` to `before` on undo and `after` on redo.
        UndoHistory::Step SetStep(int& value, int before, int after, uint64_t mergeKey = 0)
        {
            return { .description = "Set",
                     .undo = [&value, before] { value = before; },
                     .redo = [&value, after]  { value = after; },
                     .mergeKey = mergeKey };
        }

        TEST(UndoHistoryTest, UndoAndRedoWalkTheSteps)
        {
            UndoHistory history;
            int value = 0;
            EXPECT_FALSE(history.CanUndo());
            EXPECT_FALSE(history.CanRedo());

            value = 1; history.Push(SetStep(value, 0, 1)); history.EndMerge();
            value = 2; history.Push(SetStep(value, 1, 2)); history.EndMerge();
            EXPECT_EQ(history.UndoCount(), 2u);

            history.Undo();
            EXPECT_EQ(value, 1);
            history.Undo();
            EXPECT_EQ(value, 0);
            EXPECT_FALSE(history.CanUndo());
            history.Undo();   // nothing left: no-op
            EXPECT_EQ(value, 0);

            history.Redo();
            EXPECT_EQ(value, 1);
            history.Redo();
            EXPECT_EQ(value, 2);
            EXPECT_FALSE(history.CanRedo());
        }

        TEST(UndoHistoryTest, PushDropsTheUndoneSteps)
        {
            UndoHistory history;
            int value = 0;
            value = 1; history.Push(SetStep(value, 0, 1)); history.EndMerge();
            value = 2; history.Push(SetStep(value, 1, 2)); history.EndMerge();
            history.Undo();
            EXPECT_EQ(history.RedoCount(), 1u);

            value = 5; history.Push(SetStep(value, 1, 5));
            EXPECT_FALSE(history.CanRedo());
            EXPECT_EQ(history.UndoCount(), 2u);
            history.Undo();
            EXPECT_EQ(value, 1);
        }

        TEST(UndoHistoryTest, StepsWithTheSameKeyMergeUntilEndMerge)
        {
            UndoHistory history;
            int value = 0;
            value = 1; history.Push(SetStep(value, 0, 1, 7));
            value = 2; history.Push(SetStep(value, 1, 2, 7));
            EXPECT_EQ(history.UndoCount(), 1u) << "same key while merging: one step";

            history.EndMerge();
            value = 3; history.Push(SetStep(value, 2, 3, 7));
            EXPECT_EQ(history.UndoCount(), 2u) << "EndMerge starts a step of its own";

            history.Undo();
            EXPECT_EQ(value, 2);
            history.Undo();
            EXPECT_EQ(value, 0) << "the merged step undoes to the first one's state";
            history.Redo();
            EXPECT_EQ(value, 2) << "and redoes to the latest one's";
        }

        TEST(UndoHistoryTest, DifferentKeysAndUnkeyedStepsDoNotMerge)
        {
            UndoHistory history;
            int value = 0;
            history.Push(SetStep(value, 0, 1, 7));
            history.Push(SetStep(value, 1, 2, 8));
            history.Push(SetStep(value, 2, 3));
            history.Push(SetStep(value, 3, 4));
            EXPECT_EQ(history.UndoCount(), 4u);
        }

        TEST(UndoHistoryTest, UndoEndsTheMerge)
        {
            UndoHistory history;
            int value = 0;
            history.Push(SetStep(value, 0, 1, 7));
            history.Push(SetStep(value, 1, 2, 7));
            history.Undo();
            history.Redo();
            history.Push(SetStep(value, 2, 3, 7));
            EXPECT_EQ(history.UndoCount(), 2u) << "a step after an undo / redo doesn't merge into it";
        }

        TEST(UndoHistoryTest, DescriptionsAndClear)
        {
            UndoHistory history;
            int value = 0;
            history.Push({ .description = "Move text", .undo = [&] { value = 0; }, .redo = [&] { value = 1; } });
            EXPECT_EQ(history.UndoDescription(), "Move text");
            EXPECT_EQ(history.RedoDescription(), "");
            history.Undo();
            EXPECT_EQ(history.RedoDescription(), "Move text");

            history.Clear();
            EXPECT_FALSE(history.CanUndo());
            EXPECT_FALSE(history.CanRedo());
        }

        // A step's callback can change the history while it runs: it carries on with what it
        // captured, and the history is as the change left it. The text is longer than a short
        // string, so it lives on the heap and a freed callback would read freed memory.
        TEST(UndoHistoryTest, AnUndoThatPushesAStepKeepsRunning)
        {
            UndoHistory history;
            const std::string kText = "the text the undo writes, longer than a short string";
            std::string written;
            int value = 0;
            history.Push({ .description = "Write",
                           .undo = [&history, &value, &written, kText] {
                               history.Push(SetStep(value, 0, 7));   // drops this step's redo
                               written = kText;
                           } });
            history.Undo();
            EXPECT_EQ(written, kText);
            EXPECT_EQ(history.UndoCount(), 1u) << "the pushed step";
            EXPECT_EQ(history.RedoCount(), 0u) << "the push dropped the undone step";
        }

        // A step that notes in `dropped` how it left the history: "A undone", "A done".
        UndoHistory::Step DropStep(std::string& dropped, std::string name, uint64_t mergeKey = 0)
        {
            return { .description = name,
                     .undo = [] {}, .redo = [] {},
                     .mergeKey = mergeKey,
                     .drop = [&dropped, name](bool undone) { dropped += name + (undone ? " undone;" : " done;"); } };
        }

        TEST(UndoHistoryTest, APushDropsTheUndoneStepsAsUndone)
        {
            UndoHistory history;
            std::string dropped;
            history.Push(DropStep(dropped, "A")); history.EndMerge();
            history.Push(DropStep(dropped, "B")); history.EndMerge();
            history.Push(DropStep(dropped, "C")); history.EndMerge();
            history.Undo();
            history.Undo();
            EXPECT_EQ(dropped, "") << "undone steps stay until something replaces them";

            history.Push(DropStep(dropped, "D"));
            EXPECT_EQ(dropped, "B undone;C undone;");
        }

        TEST(UndoHistoryTest, ClearDropsEachStepAsItIs)
        {
            UndoHistory history;
            std::string dropped;
            history.Push(DropStep(dropped, "A")); history.EndMerge();
            history.Push(DropStep(dropped, "B")); history.EndMerge();
            history.Undo();
            history.Clear();
            EXPECT_EQ(dropped, "A done;B undone;");
            history.Clear();
            EXPECT_EQ(dropped, "A done;B undone;") << "each step drops once";
        }

        // Merging throws away the later step's undo, and with it what that undo would give
        // back, so a step that has a drop keeps to itself.
        TEST(UndoHistoryTest, StepsWithADropDoNotMerge)
        {
            UndoHistory history;
            std::string dropped;
            history.Push(DropStep(dropped, "A", 7));
            history.Push(DropStep(dropped, "B", 7));
            EXPECT_EQ(history.UndoCount(), 2u);
            EXPECT_EQ(dropped, "");
        }

        // A drop may push, clear or undo: the history is settled before it runs.
        TEST(UndoHistoryTest, ADropThatClearsTheHistoryKeepsRunning)
        {
            UndoHistory history;
            std::string dropped;
            int value = 0;
            history.Push({ .description = "Clears", .undo = [] {}, .redo = [] {},
                           .drop = [&history, &dropped](bool) { history.Clear(); dropped += "cleared;"; } });
            history.EndMerge();
            history.Undo();
            history.Push(SetStep(value, 0, 1));
            EXPECT_EQ(dropped, "cleared;");
            EXPECT_FALSE(history.CanUndo()) << "the drop cleared what the push left";
        }

        TEST(UndoHistoryTest, ARedoThatClearsTheHistoryKeepsRunning)
        {
            UndoHistory history;
            const std::string kText = "the text the redo writes, longer than a short string";
            std::string written;
            history.Push({ .description = "Write",
                           .undo = [] {},
                           .redo = [&history, &written, kText] {
                               history.Clear();
                               written = kText;
                           } });
            history.Undo();
            history.Redo();
            EXPECT_EQ(written, kText);
            EXPECT_FALSE(history.CanUndo());
            EXPECT_FALSE(history.CanRedo());
        }
    }
}
