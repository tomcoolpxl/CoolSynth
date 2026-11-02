#pragma once

#include <vector>

#include <juce_core/juce_core.h>

namespace coolsynth::plugin
{
    // Snapshot-based undo for the parameter surface. Each entry is the complete set of
    // normalised parameter values plus the patch identity that was current before an
    // edit, so undo restores the exact pre-edit state regardless of how many
    // parameters the edit touched. Message thread only.
    class ParameterUndoHistory final
    {
    public:
        struct Snapshot
        {
            juce::String label;
            std::vector<float> normalisedValues;
            juce::String patchName;
            bool patchDirty = false;
        };

        static constexpr int maximumDepth = 200;

        // Opens an edit: the snapshot is parked until commitEdit() confirms a change.
        void beginEdit(Snapshot before)
        {
            pendingEdit = std::move(before);
            editPending = true;
        }

        bool isEditPending() const noexcept { return editPending; }

        // Closes the edit opened by beginEdit(). The snapshot is kept only when the
        // values actually changed, so a click without a drag leaves no undo step.
        void commitEdit(const std::vector<float>& valuesAfterEdit)
        {
            if (! editPending)
                return;

            editPending = false;

            if (pendingEdit.normalisedValues == valuesAfterEdit)
                return;

            undoStack.push_back(std::move(pendingEdit));
            redoStack.clear();

            if (static_cast<int>(undoStack.size()) > maximumDepth)
                undoStack.erase(undoStack.begin());
        }

        bool canUndo() const noexcept { return ! undoStack.empty(); }
        bool canRedo() const noexcept { return ! redoStack.empty(); }

        juce::String getUndoLabel() const { return canUndo() ? undoStack.back().label : juce::String {}; }
        juce::String getRedoLabel() const { return canRedo() ? redoStack.back().label : juce::String {}; }

        // Pops the newest undo entry; `current` is pushed to the redo stack under the
        // same label so redo restores what undo just replaced.
        Snapshot popUndo(Snapshot current)
        {
            jassert(canUndo());
            auto restored = std::move(undoStack.back());
            undoStack.pop_back();
            current.label = restored.label;
            redoStack.push_back(std::move(current));
            return restored;
        }

        Snapshot popRedo(Snapshot current)
        {
            jassert(canRedo());
            auto restored = std::move(redoStack.back());
            redoStack.pop_back();
            current.label = restored.label;
            undoStack.push_back(std::move(current));
            return restored;
        }

        void clear()
        {
            undoStack.clear();
            redoStack.clear();
            editPending = false;
        }

    private:
        std::vector<Snapshot> undoStack;
        std::vector<Snapshot> redoStack;
        Snapshot pendingEdit;
        bool editPending = false;
    };
}
