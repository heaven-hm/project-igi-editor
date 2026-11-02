// gameplay_host.h - Host controller for gameplay runtime, simulation, and twin-window lifecycle
#pragma once

#include "runtime_session.h"
#include "gameplay_window.h"
#include "runtime_renderer.h"

namespace igi {

class GameplayHost {
public:
    GameplayHost();
    ~GameplayHost();

    // Lifecycle
    void Initialize(float (*get_terrain_z)(float x, float y), bool (*check_collision)(float x, float y, float z) = nullptr);
    void Shutdown();

    // Mode Transitions (Editor <-> Gameplay)
    bool OpenGameplay(const EditorSnapshot& snapshot);
    bool CloseGameplay(EditorSnapshot& out_snapshot);
    bool ApplyAndRestartGameplay(const EditorSnapshot& snapshot);
    void RestartGameplay();
    void SetGameplayInputModifier(SimulationScheduler::GameplayInputModifier input_modifier);

    // Frame update & render
    void Update(int64_t now_milliseconds);
    void Render(const RuntimeRenderCamera& camera);

    bool InitializeGameplayWindow(
        int editor_window_id,
        int width,
        int height,
        const GameplayWindowCallbacks& callbacks);
    void ShutdownGameplayWindow();
    void NotifyGameplayWindowClosed();
    void ShowGameplayWindow();
    void HideGameplayWindow();
    void FocusGameplayWindow();
    void FocusEditorWindow();
    void MakeGameplayWindowCurrent() const;
    bool HasGameplayWindow() const { return gameplay_window_.IsCreated(); }
    bool IsGameplayWindowCurrent() const { return gameplay_window_.IsCurrent(); }

    // State queries
    bool IsGameplayActive() const { return session_.IsActive(); }
    bool IsPaused() const { return session_.IsPaused(); }
    void SetPaused(bool paused);

    RuntimeSessionState GetSessionState() const { return session_.GetState(); }

    RuntimeWorld& GetWorld() { return session_.GetWorld(); }
    const RuntimeWorld& GetWorld() const { return session_.GetWorld(); }

    WindowInputRouter& GetInputRouter() { return session_.GetInputRouter(); }
    const WindowInputRouter& GetInputRouter() const {
        return session_.GetInputRouter();
    }
    SimulationScheduler& GetScheduler() { return session_.GetScheduler(); }
    const RuntimeRenderSnapshot& GetRenderSnapshot() const {
        return runtime_renderer_.GetSnapshot();
    }

private:
    void DispatchPendingAudioEvents();

    RuntimeSession session_;
    GameplayWindowHost gameplay_window_;
    RuntimeRenderer runtime_renderer_;
};

} // namespace igi
