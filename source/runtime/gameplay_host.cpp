// gameplay_host.cpp - Host controller for gameplay runtime, simulation, and twin-window lifecycle implementation
#include "gameplay_host.h"

namespace igi {

GameplayHost::GameplayHost()
    : scheduler_(world_, input_router_) {}

GameplayHost::~GameplayHost() {
    Shutdown();
}

void GameplayHost::Initialize(float (*get_terrain_z)(float x, float y)) {
    get_terrain_z_ = get_terrain_z;
    world_.Initialize(get_terrain_z);
}

void GameplayHost::Shutdown() {
    if (is_gameplay_active_) {
        EditorSnapshot dummy;
        CloseGameplay(dummy);
    }
}

bool GameplayHost::OpenGameplay(const EditorSnapshot& snapshot) {
    if (is_gameplay_active_) return false;

    // 1. Capture snapshot of editor state
    snapshot_mgr_.Capture(snapshot);

    // 2. Initialize fresh runtime world
    world_.Reset();
    scheduler_.Reset();

    // 3. Focus gameplay input
    input_router_.SetFocus(WindowFocusTarget::GameplayWindow);
    is_gameplay_active_ = true;
    is_paused_ = false;

    return true;
}

bool GameplayHost::CloseGameplay(EditorSnapshot& out_snapshot) {
    if (!is_gameplay_active_) return false;

    // 1. Restore editor snapshot
    bool restored = snapshot_mgr_.Restore(out_snapshot);

    // 2. Reset runtime state
    input_router_.SetFocus(WindowFocusTarget::EditorWindow);
    is_gameplay_active_ = false;
    is_paused_ = false;
    snapshot_mgr_.Clear();

    return restored;
}

void GameplayHost::RestartGameplay() {
    if (!is_gameplay_active_) return;

    world_.Reset();
    scheduler_.Reset();
    is_paused_ = false;
}

void GameplayHost::SetPaused(bool paused) {
    is_paused_ = paused;
    if (paused) {
        input_router_.ResetInputState();
    }
    scheduler_.GetClock().SetPaused(paused);
}

void GameplayHost::Update(int64_t now_milliseconds) {
    if (!is_gameplay_active_ || is_paused_) return;

    scheduler_.Update(now_milliseconds);
}

void GameplayHost::Render() {
    if (!is_gameplay_active_) return;

    // Gameplay HUD & runtime camera render hook
}

} // namespace igi
