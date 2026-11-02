// runtime_session.h - Explicit lifecycle owner for one isolated gameplay run
#pragma once

#include <cstdint>

#include "editor_snapshot.h"
#include "runtime_world.h"
#include "simulation_scheduler.h"
#include "window_input_router.h"

namespace igi {

// The session state is deliberately independent from the window state. A
// gameplay window may be hidden or unfocused while the simulation remains
// alive, and a failed presentation must not make the editor's state ambiguous.
enum class RuntimeSessionState {
    Stopped,
    Created,
    Running,
    Paused,
    Failed,
};

class RuntimeSession {
public:
    RuntimeSession();
    ~RuntimeSession();

    void Initialize(
        float (*get_terrain_z)(float x, float y),
        bool (*check_collision)(float x, float y, float z) = nullptr);
    void Shutdown();

    bool Open(const EditorSnapshot& editor_snapshot);
    bool Close(EditorSnapshot& restored_snapshot);
    // Rebuild the mutable runtime from a new editor snapshot without leaving
    // gameplay. The source/editor representation remains owned by the caller.
    bool ApplyEditorSnapshot(const EditorSnapshot& editor_snapshot);
    void Restart();
    void SetPaused(bool paused);
    void Update(int64_t now_milliseconds);

    RuntimeSessionState GetState() const { return state_; }
    bool IsInitialized() const { return is_initialized_; }
    bool IsActive() const;
    bool IsPaused() const { return state_ == RuntimeSessionState::Paused; }

    RuntimeWorld& GetWorld() { return world_; }
    const RuntimeWorld& GetWorld() const { return world_; }

    WindowInputRouter& GetInputRouter() { return input_router_; }
    const WindowInputRouter& GetInputRouter() const { return input_router_; }

    SimulationScheduler& GetScheduler() { return scheduler_; }
    const SimulationScheduler& GetScheduler() const { return scheduler_; }

private:
    void ResetRuntimeState();

    RuntimeWorld world_;
    WindowInputRouter input_router_;
    SimulationScheduler scheduler_;
    EditorSnapshotManager snapshot_manager_;

    RuntimeSessionState state_ = RuntimeSessionState::Stopped;
    bool is_initialized_ = false;
};

} // namespace igi
