// What an edit gives back as it leaves the undo history: the static IDs that only the side
// of it not in effect holds. Done, the items its undo would bring back; undone, the items
// its redo would add. IDs both sides hold stay.

#include <gtest/gtest.h>

#include <gui_controls/gui_curve_types.h>
#include <gui_controls/gui_node_view_helpers.h>
#include <gui_controls/gui_timeline_types.h>
#include <gui_core/gui_id_manager.h>

#include <vector>

namespace tachyon
{
    namespace
    {
        TimelineTrack Track(ID id, std::vector<ID> keys, std::vector<ID> events = {})
        {
            TimelineTrack track;
            track.stableID = id;
            for (ID key : keys)    track.keys.push_back({ .stableID = key });
            for (ID event : events) track.events.push_back({ .stableID = event });
            return track;
        }

        // Deletes key b from a track, removes a whole track, and moves a third one: removed
        // from one place and added at another, so both sides hold it.
        struct TimelineDeletion
        {
            IDManager ids;
            ID kept = ids.CreateStaticID(), a = ids.CreateStaticID(), b = ids.CreateStaticID();
            ID gone = ids.CreateStaticID(), goneKey = ids.CreateStaticID(), goneEvent = ids.CreateStaticID();
            ID moved = ids.CreateStaticID(), movedKey = ids.CreateStaticID();
            TimelineEdit edit;

            TimelineDeletion()
            {
                edit.changes.push_back({ .track = 0, .from = Track(kept, { a, b }), .to = Track(kept, { a }) });
                edit.tracks.push_back({ .index = 1, .added = false, .track = Track(gone, { goneKey }, { goneEvent }) });
                edit.tracks.push_back({ .index = 2, .added = false, .track = Track(moved, { movedKey }) });
                edit.tracks.push_back({ .index = 0, .added = true,  .track = Track(moved, { movedKey }) });
            }
        };

        TEST(EditIDRelease, ADoneTimelineEditGivesBackWhatItsUndoWouldBringBack)
        {
            TimelineDeletion t;
            ReleaseTimelineEditIDs(t.ids, t.edit, /*undone*/ false);
            EXPECT_FALSE(t.ids.IsStaticIDValid(t.b));
            EXPECT_FALSE(t.ids.IsStaticIDValid(t.gone));
            EXPECT_FALSE(t.ids.IsStaticIDValid(t.goneKey));
            EXPECT_FALSE(t.ids.IsStaticIDValid(t.goneEvent));
            for (ID id : { t.kept, t.a, t.moved, t.movedKey })
                EXPECT_TRUE(t.ids.IsStaticIDValid(id)) << "both sides hold it";
            EXPECT_EQ(t.ids.GetStaticIDCount(), 4u);
        }

        TEST(EditIDRelease, AnUndoneTimelineEditGivesBackWhatItsRedoWouldAdd)
        {
            TimelineDeletion t;
            ReleaseTimelineEditIDs(t.ids, t.edit, /*undone*/ true);
            EXPECT_EQ(t.ids.GetStaticIDCount(), 8u) << "a deletion's redo adds nothing";

            IDManager ids;
            const ID key = ids.CreateStaticID(), added = ids.CreateStaticID(), track = ids.CreateStaticID();
            TimelineEdit add;
            add.changes.push_back({ .track = 0, .from = Track(track, { key }), .to = Track(track, { key, added }) });
            ReleaseTimelineEditIDs(ids, add, /*undone*/ true);
            EXPECT_FALSE(ids.IsStaticIDValid(added));
            EXPECT_TRUE(ids.IsStaticIDValid(key));
            EXPECT_TRUE(ids.IsStaticIDValid(track));
        }

        Curve KeysCurve(std::vector<ID> keys)
        {
            Curve curve;
            for (ID key : keys) curve.keys.push_back({ .stableID = key });
            return curve;
        }

        // A preset swaps a curve's keys for new ones.
        TEST(EditIDRelease, ACurveEditGivesBackTheKeysOnlyOneSideHas)
        {
            IDManager ids;
            const ID before = ids.CreateStaticID(), after = ids.CreateStaticID(), both = ids.CreateStaticID();
            CurveEdit edit;
            edit.changes.push_back({ .channel = 0, .from = KeysCurve({ before, both }), .to = KeysCurve({ after, both }) });

            IDManager undoneIDs = ids;
            ReleaseCurveEditIDs(ids, edit, /*undone*/ false);
            EXPECT_FALSE(ids.IsStaticIDValid(before));
            EXPECT_TRUE(ids.IsStaticIDValid(after));
            EXPECT_TRUE(ids.IsStaticIDValid(both));

            ReleaseCurveEditIDs(undoneIDs, edit, /*undone*/ true);
            EXPECT_TRUE(undoneIDs.IsStaticIDValid(before));
            EXPECT_FALSE(undoneIDs.IsStaticIDValid(after));
            EXPECT_TRUE(undoneIDs.IsStaticIDValid(both));
        }

        TEST(EditIDRelease, ANodeViewEditGivesBackItsNodesFramesAndTransitions)
        {
            IDManager ids;
            const ID node = ids.CreateStaticID(), frame = ids.CreateStaticID(), transition = ids.CreateStaticID();
            const ID addedNode = ids.CreateStaticID();
            NodeViewEdit edit;
            NodeModel removed;  removed.stableID = node;
            NodeModel added;    added.stableID   = addedNode;
            edit.nodes.push_back({ .index = 0, .added = false, .node = removed });
            edit.nodes.push_back({ .index = 3, .added = true,  .node = added });
            edit.frames.push_back({ .index = 0, .added = false, .frame = NodeFrame{ .stableID = frame } });
            edit.transitions.push_back({ .index = 0, .added = false, .transition = StateTransition{ .stableID = transition } });
            edit.initial = NodeViewEdit::Initial{ .from = node, .to = ID::Invalid() };   // names a state, holds nothing

            IDManager undoneIDs = ids;
            ReleaseNodeViewEditIDs(ids, edit, /*undone*/ false);
            EXPECT_FALSE(ids.IsStaticIDValid(node));
            EXPECT_FALSE(ids.IsStaticIDValid(frame));
            EXPECT_FALSE(ids.IsStaticIDValid(transition));
            EXPECT_TRUE(ids.IsStaticIDValid(addedNode));

            ReleaseNodeViewEditIDs(undoneIDs, edit, /*undone*/ true);
            EXPECT_EQ(undoneIDs.GetStaticIDCount(), 3u);
            EXPECT_FALSE(undoneIDs.IsStaticIDValid(addedNode));
        }

        // An edit that neither adds nor removes anything gives nothing back.
        TEST(EditIDRelease, AMoveGivesNothingBack)
        {
            IDManager ids;
            const ID track = ids.CreateStaticID(), key = ids.CreateStaticID();
            TimelineEdit edit;
            TimelineTrack later = Track(track, { key });
            later.keys[0].time = 1.0f;
            edit.changes.push_back({ .track = 0, .from = Track(track, { key }), .to = later });
            ReleaseTimelineEditIDs(ids, edit, false);
            ReleaseTimelineEditIDs(ids, edit, true);
            EXPECT_EQ(ids.GetStaticIDCount(), 2u);
        }
    }
}
