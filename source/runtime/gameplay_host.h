// gameplay_host.h - Host controller for gameplay runtime, simulation, and twin-window lifecycle
#pragma once

#include <memory>
#include <string>
#include "runtime_world.h"
#include "window_input_router.h"
#include "simulation_scheduler.h"
#include "editor_snapshot.h"

namespace igi {

class GameplayHost {
public:
    GameplayHost();
    ~GameplayHost();

    // Lifecycle
    void Initialize(float (*get_terrain_z)(float x, float y));
    void Shutdown();

    // Mode Transitions (Editor <-> Gameplay)
    bool OpenGameplay(const EditorSnapshot& snapshot);
    bool CloseGameplay(EditorSnapshot& out_snapshot);
    void RestartGameplay();

    // Frame update & render
    void Update(int64_t now_milliseconds);
    void Render();

    // State queries
    bool IsGameplayActive() const { return is_gameplay_active_; }
    bool IsPaused() const { return is_paused_; }
    void SetPaused(bool paused);

    RuntimeWorld& GetWorld() { return world_; }
    const RuntimeWorld& GetWorld() const { return world_; }

    WindowInputRouter& GetInputRouter() { return input_router_; }
    SimulationScheduler& GetScheduler() { return scheduler_; }

private:
    RuntimeWorld world_;
    WindowInputRouter input_router_;
    SimulationScheduler scheduler_;
    EditorSnapshotManager snapshot_mgr_;

    bool is_gameplay_active_ = false;
    bool is_paused_ = false;
    float (*get_terrain_z_)(float x, float y) = nullptr;
};

} // namespace igi
