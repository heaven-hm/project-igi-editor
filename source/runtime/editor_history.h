#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace igi {

template <typename State>
void ClearEditorHistory(std::vector<State>& undo,
                        std::vector<State>& redo) noexcept {
    undo.clear();
    redo.clear();
}

// The editor keeps the current state outside both stacks. A new edit pushes
// the previous state and invalidates redo; undo/redo move the current state to
// the opposite stack before returning the state to restore.
template <typename State>
void PushEditorUndo(std::vector<State>& undo,
                    std::vector<State>& redo,
                    State state,
                    std::size_t maximum_entries = 20) {
    undo.push_back(std::move(state));
    redo.clear();
    if (maximum_entries == 0) {
        undo.clear();
    } else if (undo.size() > maximum_entries) {
        undo.erase(undo.begin(), undo.begin() +
                   static_cast<std::ptrdiff_t>(undo.size() - maximum_entries));
    }
}

template <typename State>
bool MoveEditorUndoToRedo(std::vector<State>& undo,
                          std::vector<State>& redo,
                          State current,
                          State& restored) {
    if (undo.empty()) return false;
    redo.push_back(std::move(current));
    restored = std::move(undo.back());
    undo.pop_back();
    return true;
}

template <typename State>
bool MoveEditorRedoToUndo(std::vector<State>& undo,
                          std::vector<State>& redo,
                          State current,
                          State& restored,
                          std::size_t maximum_entries = 20) {
    if (redo.empty()) return false;
    undo.push_back(std::move(current));
    if (maximum_entries == 0) {
        undo.clear();
    } else if (undo.size() > maximum_entries) {
        undo.erase(undo.begin(), undo.begin() +
                   static_cast<std::ptrdiff_t>(undo.size() - maximum_entries));
    }
    restored = std::move(redo.back());
    redo.pop_back();
    return true;
}

} // namespace igi
