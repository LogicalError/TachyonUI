// IDMap: an ID-keyed hash map for per-frame indexes, filled and cleared every frame without
// allocating once it has grown to the frame's size.

#include <gtest/gtest.h>

#include <gui_core/gui_id_map.h>

#include <cstdint>

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
    }
}
