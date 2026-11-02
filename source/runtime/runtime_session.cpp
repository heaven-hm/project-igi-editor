// runtime_session.cpp - Explicit lifecycle owner for one isolated gameplay run
#include "runtime_session.h"

namespace igi {

RuntimeSession::RuntimeSession()
    : scheduler_(world_, input_router_) {}

RuntimeSession::~RuntimeSession() {
    Shutdown();
}

void RuntimeSession::Initialize(
    float (*get_terrain_z)(float x, float y),
    bool (*check_collision)(float x, float y, float z)) {
    // Reinitialization is a controlled boundary used by level changes and
    // recovery from a failed presentation. It always clears the old mutable
    // world before accepting the new query providers.
    Shutdown();
    world_.Initialize(get_terrain_z, check_collision);
    input_router_.SetFocus(WindowFocusTarget::EditorWindow);
    scheduler_.Reset();
    state_ = RuntimeSessionState::Created;
    is_initialized_ = true;
}

void RuntimeSession::Shutdown() {
    input_router_.SetFocus(WindowFocusTarget::EditorWindow);
    snapshot_manager_.Clear();
    world_.Reset();
    scheduler_.Reset();
    state_ = RuntimeSessionState::Stopped;
    is_initialized_ = false;
}

bool RuntimeSession::Open(const EditorSnapshot& editor_snapshot) {
    if (!is_initialized_ || IsActive()) {
        return false;
    }

    snapshot_manager_.Capture(editor_snapshot);
    ResetRuntimeState();
    input_router_.SetFocus(WindowFocusTarget::GameplayWindow);
    state_ = RuntimeSessionState::Running;
    return true;
}

bool RuntimeSession::Close(EditorSnapshot& restored_snapshot) {
    if (!IsActive()) {
        return false;
    }

    const bool restored = snapshot_manager_.Restore(restored_snapshot);
    input_router_.SetFocus(WindowFocusTarget::EditorWindow);
    ResetRuntimeState();
    snapshot_manager_.Clear();

    // A missing snapshot is a session contract failure. The editor focus has
    // already been restored so the caller can recover without a dead window.
    state_ = restored ? RuntimeSessionState::Stopped : RuntimeSessionState::Failed;
    return restored;
}

bool RuntimeSession::ApplyEditorSnapshot(const EditorSnapshot& editor_snapshot) {
    if (!IsActive()) {
        return false;
    }

    // Capture first, then reset only mutable runtime state. This makes the
    // apply boundary transactional from the editor's perspective: a later
    // Close restores the newly applied snapshot, while no runtime mutation
    // can leak into editor-owned objects or buffers.
    snapshot_manager_.Capture(editor_snapshot);
    ResetRuntimeState();
    input_router_.SetFocus(WindowFocusTarget::GameplayWindow);
    state_ = RuntimeSessionState::Running;
    return true;
}

void RuntimeSession::Restart() {
    if (!IsActive()) {
        return;
    }

    ResetRuntimeState();
    input_router_.SetFocus(WindowFocusTarget::GameplayWindow);
    state_ = RuntimeSessionState::Running;
}

void RuntimeSession::SetPaused(bool paused) {
    if (!IsActive()) {
        return;
    }

    scheduler_.GetClock().SetPaused(paused);
    state_ = paused ? RuntimeSessionState::Paused : RuntimeSessionState::Running;
}

void RuntimeSession::Update(int64_t now_milliseconds) {
    if (state_ != RuntimeSessionState::Running) {
        return;
    }

    scheduler_.Update(now_milliseconds);
}

bool RuntimeSession::IsActive() const {
    return state_ == RuntimeSessionState::Running ||
        state_ == RuntimeSessionState::Paused;
}

void RuntimeSession::ResetRuntimeState() {
    world_.Reset();
    scheduler_.Reset();
    input_router_.ResetInputState();
}

} // namespace igi
