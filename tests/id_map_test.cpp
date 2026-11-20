// IDMap: an ID-keyed hash map for per-frame indexes, filled and cleared every frame without
// allocating once it has grown to the frame's size.

#include <gtest/gtest.h>

#include <gui_core/gui_id_map.h>
#include <gui_core/gui_recent_id_map.h>
#include <gui_core/state/gui_scroll_management.h>

#include <cstdint>
#include <unordered_set>

namespace tachyon
{
    namespace
    {
        ID MakeID(uint64_t bits) { return ID{ { bits } }; }

        TEST(IDMap, FindsWhatWasAddedAndNothingElse)
        {
            IDMap<int> map;
            for (uint64_t i = 1; i <= 1000; ++i)
                map.TryEmplace(MakeID(i * 4 + 1), int(i));
            EXPECT_EQ(map.Size(), 1000u);
            for (uint64_t i = 1; i <= 1000; ++i)
            {
                const int* value = map.Find(MakeID(i * 4 + 1));
                ASSERT_NE(value, nullptr);
                EXPECT_EQ(*value, int(i));
            }
            EXPECT_EQ(map.Find(MakeID(3)), nullptr);
            EXPECT_EQ(map.Find(ID::Invalid()), nullptr);
        }

        TEST(IDMap, AddingAKeyAgainKeepsTheFirstValueAndSaysSo)
        {
            IDMap<int> map;
            auto [first, added] = map.TryEmplace(MakeID(7), 1);
            EXPECT_TRUE(added);
            auto [again, addedAgain] = map.TryEmplace(MakeID(7), 2);
            EXPECT_FALSE(addedAgain);
            EXPECT_EQ(again, first);
            EXPECT_EQ(*again, 1);
            EXPECT_EQ(map.Size(), 1u);
        }

        TEST(IDMap, EraseIfDropsWhatItSaysAndKeepsTheRest)
        {
            IDMap<int> map;
            for (uint64_t i = 1; i <= 300; ++i)
                map.TryEmplace(MakeID(i * 8 + 1), int(i));
            map.EraseIf([](ID, int value) { return value % 3 == 0; });
            EXPECT_EQ(map.Size(), 200u);
            for (uint64_t i = 1; i <= 300; ++i)
            {
                const int* value = map.Find(MakeID(i * 8 + 1));
                if (i % 3 == 0)
                    EXPECT_EQ(value, nullptr);
                else
                {
                    ASSERT_NE(value, nullptr);
                    EXPECT_EQ(*value, int(i));
                }
            }
            int visited = 0;
            map.ForEach([&](ID, int value) { visited += value % 3 == 0 ? 1000 : 1; });
            EXPECT_EQ(visited, 200);
        }

        TEST(IDMap, ClearEmptiesItAndKeepsItsRoom)
        {
            IDMap<int> map;
            for (uint64_t i = 1; i <= 100; ++i)
                map.TryEmplace(MakeID(i << 40), int(i));   // keys that differ only in high bits
            const size_t capacity = map.Capacity();
            map.Clear();
            EXPECT_EQ(map.Size(), 0u);
            EXPECT_EQ(map.Find(MakeID(1ull << 40)), nullptr);
            EXPECT_EQ(map.Capacity(), capacity);
            for (uint64_t i = 1; i <= 100; ++i)
                map.TryEmplace(MakeID(i << 40), int(i) + 1);
            EXPECT_EQ(map.Capacity(), capacity);
            EXPECT_EQ(*map.Find(MakeID(5ull << 40)), 6);
        }

        // One huge fill, then small ones: the room it took goes, and clearing stays cheap.
        TEST(IDMap, ClearGivesBackRoomFarBeyondTheFill)
        {
            IDMap<int> map;
            for (uint64_t i = 1; i <= 20000; ++i)
                map.TryEmplace(MakeID(i * 4 + 1), int(i));
            const size_t big = map.Capacity();
            map.Clear();
            for (uint64_t i = 1; i <= 10; ++i)
                map.TryEmplace(MakeID(i * 4 + 1), int(i));
            map.Clear();
            EXPECT_LT(map.Capacity(), big / 16);
            for (uint64_t i = 1; i <= 10; ++i)
                map.TryEmplace(MakeID(i * 4 + 1), int(i));
            EXPECT_EQ(*map.Find(MakeID(5 * 4 + 1)), 5);
            EXPECT_EQ(map.Find(MakeID(11 * 4 + 1)), nullptr);
        }

        // Kept entries go back in after a sweep at the highest load, and new ones still fit.
        TEST(IDMap, EraseIfAtFullLoadThenAddingMore)
        {
            IDMap<int> map;
            for (uint64_t i = 1; i <= 32; ++i)   // 32 of 64 slots: the most before it grows
                map.TryEmplace(MakeID(i * 4 + 1), int(i));
            ASSERT_EQ(map.Capacity(), 64u);
            map.EraseIf([](ID, int value) { return value % 2 == 0; });
            for (uint64_t i = 100; i < 116; ++i)
                map.TryEmplace(MakeID(i * 4 + 1), int(i));
            EXPECT_EQ(map.Size(), 32u);
            for (uint64_t i = 1; i <= 32; ++i)
                EXPECT_EQ(map.Find(MakeID(i * 4 + 1)) != nullptr, i % 2 == 1) << i;
            for (uint64_t i = 100; i < 116; ++i)
                EXPECT_NE(map.Find(MakeID(i * 4 + 1)), nullptr) << i;
        }

        // The standard containers bucket on a hash's low bits: IDs that differ only in their
        // index or only in their high bits must still spread.
        TEST(IDHash, SpreadsOverTheLowBits)
        {
            auto buckets = [](auto makeBits) {
                std::unordered_set<size_t> used;
                for (uint64_t i = 0; i < 1024; ++i)
                    used.insert(std::hash<ID>{}(MakeID(makeBits(i))) & 1023);
                return used.size();
            };
            EXPECT_GT(buckets([](uint64_t i) { return (i << 2) | 1; }), 550u) << "indexes above the mode bits";
            EXPECT_GT(buckets([](uint64_t i) { return (i << 27) | (5 << 2) | 2; }), 550u) << "only the high bits differ";
        }

        // State of IDs no longer written goes after a while; what is still written, or only
        // touched, stays.
        TEST(RecentIDMap, ForgetsWhatIsNoLongerWritten)
        {
            RecentIDMap<int> map;
            int dropped = 0;
            for (uint32_t frame = 0; frame < RecentIDMap<int>::kKeepFrames + 2 * RecentIDMap<int>::kSweepPeriod; ++frame)
            {
                map[MakeID(5)] = int(frame);          // written every frame
                map.Touch(MakeID(9));                 // only touched
                if (frame == 0)
                {
                    map[MakeID(7)] = 1;               // written once
                    map[MakeID(9)] = 2;
                }
                map.EndFrame([&](ID, int&) { ++dropped; return true; });
            }
            EXPECT_NE(map.Find(MakeID(5)), nullptr);
            EXPECT_NE(map.Find(MakeID(9)), nullptr);
            EXPECT_EQ(map.Find(MakeID(7)), nullptr);
            EXPECT_EQ(dropped, 1);
            EXPECT_EQ(map.Size(), 2u);
        }

        // No ID, no scroll state: nothing is made that could never be found and freed.
        TEST(ScrollManagement, NoHandleForAnInvalidID)
        {
            ScrollManagement scroll;
            EXPECT_EQ(scroll.GetOrCreateScrollOffsetHandle(ID::Invalid()), ScrollOffsetHandle::Invalid);
            EXPECT_EQ(scroll.GetScrollOffsetHandle(ID::Invalid()), ScrollOffsetHandle::Invalid);
        }
    }
}
