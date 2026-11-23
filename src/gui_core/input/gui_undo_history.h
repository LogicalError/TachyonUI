#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tachyon
{
    /// @addtogroup input
    /// @{

    /// The app's undo history: steps that each undo and redo one change. Text fields push
    /// their committed edits here, and so can the app's own editors; Undo / Redo walk it.
    class UndoHistory
    {
    public:
        struct Step
        {
            std::string           description;   ///< "Edit text", "Move node"
            std::function<void()> undo;
            std::function<void()> redo;
            /// Steps with the same non-zero key merge while merging is open (e.g. the typing
            /// in one field while it has the focus): the first's undo, the latest's redo.
            uint64_t              mergeKey = 0;
            /// Runs once as the step leaves the history for good: a push replaces it while
            /// it's undone, or Clear. `undone` says which side of it was in effect; give back
            /// what only the other side holds, such as the IDs of the keys its undo would
            /// bring back (done) or its redo would add (undone). A step with one never merges.
            std::function<void(bool undone)> drop;
        };

        /// Adds a step after the current one, dropping any undone steps (or merges it into the
        /// last step, see Step::mergeKey). Does not run it.
        void Push(Step step)
        {
            std::vector<Step> dropped = TakeUndone();
            if (m_mergeOpen && step.mergeKey != 0 && !step.drop && !m_steps.empty()
                && m_steps.back().mergeKey == step.mergeKey && !m_steps.back().drop)
            {
                m_steps.back().redo = std::move(step.redo);
            }
            else
            {
                m_steps.push_back(std::move(step));
                m_current   = m_steps.size();
                m_mergeOpen = true;
            }
            Drop(dropped, 0);
        }

        /// The next push starts a step of its own (e.g. when a field loses the focus).
        void EndMerge() { m_mergeOpen = false; }

        bool CanUndo() const { return m_current > 0; }
        bool CanRedo() const { return m_current < m_steps.size(); }

        /// The step's callback runs from a copy, after the history has moved: it may push,
        /// clear or undo / redo again, and carries on with what it captured.
        void Undo()
        {
            if (!CanUndo()) return;
            m_mergeOpen = false;
            if (const std::function<void()> undo = m_steps[--m_current].undo) undo();
        }

        /// As Undo, the other way.
        void Redo()
        {
            if (!CanRedo()) return;
            m_mergeOpen = false;
            if (const std::function<void()> redo = m_steps[m_current++].redo) redo();
        }

        size_t           UndoCount() const { return m_current; }
        size_t           RedoCount() const { return m_steps.size() - m_current; }
        std::string_view UndoDescription() const { return CanUndo() ? std::string_view(m_steps[m_current - 1].description) : std::string_view{}; }
        std::string_view RedoDescription() const { return CanRedo() ? std::string_view(m_steps[m_current].description) : std::string_view{}; }

        /// Drops every step (see Step::drop). Destroying the history runs no drops: Clear it
        /// first, while what they give back to is still there.
        void Clear()
        {
            std::vector<Step> dropped = std::exchange(m_steps, {});
            const size_t      done    = std::exchange(m_current, 0);
            m_mergeOpen = false;
            Drop(dropped, done);
        }

    private:
        std::vector<Step> TakeUndone()
        {
            std::vector<Step> undone;
            if (m_current < m_steps.size())
            {
                undone.assign(std::make_move_iterator(m_steps.begin() + ptrdiff_t(m_current)), std::make_move_iterator(m_steps.end()));
                m_steps.resize(m_current);
            }
            return undone;
        }

        /// Runs the drops of steps already out of the history, so they may push, clear or
        /// undo again; the first `done` were done, the rest undone.
        static void Drop(std::vector<Step>& steps, size_t done)
        {
            for (size_t i = 0; i < steps.size(); ++i)
                if (steps[i].drop) steps[i].drop(i >= done);
        }

        std::vector<Step> m_steps;
        size_t            m_current   = 0;       ///< steps before it are done, from it on undone
        bool              m_mergeOpen = false;
    };
    /// @}
}
