// gameplay_host.cpp - Host controller for gameplay runtime, simulation, and twin-window lifecycle implementation
#include "gameplay_host.h"

#include "audio_system.h"

#include <utility>

namespace igi {

GameplayHost::GameplayHost()
    = default;

GameplayHost::~GameplayHost() {
    Shutdown();
}

void GameplayHost::Initialize(float (*get_terrain_z)(float x, float y), bool (*check_collision)(float x, float y, float z)) {
    DispatchPendingAudioEvents();
    session_.Initialize(get_terrain_z, check_collision);
    DispatchPendingAudioEvents();
}

void GameplayHost::Shutdown() {
    DispatchPendingAudioEvents();
    session_.Shutdown();
    DispatchPendingAudioEvents();
    runtime_renderer_.Clear();
}

bool GameplayHost::OpenGameplay(const EditorSnapshot& snapshot) {
    const bool opened = session_.Open(snapshot);
    DispatchPendingAudioEvents();
    return opened;
}

bool GameplayHost::CloseGameplay(EditorSnapshot& out_snapshot) {
    const bool closed = session_.Close(out_snapshot);
    DispatchPendingAudioEvents();
    return closed;
}

bool GameplayHost::ApplyAndRestartGameplay(const EditorSnapshot& snapshot) {
    DispatchPendingAudioEvents();
    const bool applied = session_.ApplyEditorSnapshot(snapshot);
    DispatchPendingAudioEvents();
    return applied;
}

void GameplayHost::RestartGameplay() {
    DispatchPendingAudioEvents();
    session_.Restart();
    DispatchPendingAudioEvents();
}

void GameplayHost::SetGameplayInputModifier(
    SimulationScheduler::GameplayInputModifier input_modifier) {
    session_.GetScheduler().SetGameplayInputModifier(std::move(input_modifier));
}

void GameplayHost::SetPaused(bool paused) {
    session_.SetPaused(paused);
}

void GameplayHost::Update(int64_t now_milliseconds) {
    session_.Update(now_milliseconds);
    DispatchPendingAudioEvents();
}

void GameplayHost::Render(const RuntimeRenderCamera& camera) {
    if (!session_.IsActive()) {
        runtime_renderer_.Clear();
        return;
    }

    runtime_renderer_.Capture(session_.GetWorld(), camera);
}

bool GameplayHost::InitializeGameplayWindow(
    int editor_window_id,
    int width,
    int height,
    const GameplayWindowCallbacks& callbacks) {
    return gameplay_window_.Create(editor_window_id, width, height, callbacks);
}

void GameplayHost::ShutdownGameplayWindow() {
    gameplay_window_.Destroy();
}

void GameplayHost::NotifyGameplayWindowClosed() {
    gameplay_window_.NotifyClosed();
}

void GameplayHost::ShowGameplayWindow() {
    gameplay_window_.Show();
}

void GameplayHost::HideGameplayWindow() {
    gameplay_window_.Hide();
}

void GameplayHost::FocusGameplayWindow() {
    gameplay_window_.Focus();
    session_.GetInputRouter().SetFocus(WindowFocusTarget::GameplayWindow);
}

void GameplayHost::FocusEditorWindow() {
    gameplay_window_.FocusEditor();
    session_.GetInputRouter().SetFocus(WindowFocusTarget::EditorWindow);
}

void GameplayHost::MakeGameplayWindowCurrent() const {
    gameplay_window_.MakeCurrent();
}

void GameplayHost::DispatchPendingAudioEvents() {
    for (const RuntimeAudioEvent& audio_event :
         session_.GetWorld().ConsumePendingAudioEvents()) {
        switch (audio_event.type) {
            case RuntimeAudioEventType::OneShot:
                if (audio_event.authored_sound.empty()) {
                    AudioSystem::Play(audio_event.fallback_effect);
                } else {
                    AudioSystem::PlayWeaponFire(
                        audio_event.authored_sound,
                        audio_event.fallback_effect);
                }
                break;
            case RuntimeAudioEventType::StartLoop:
                AudioSystem::PlayConditionalSound(
                    audio_event.channel_id,
                    audio_event.authored_sound,
                    audio_event.fallback_effect);
                break;
            case RuntimeAudioEventType::StopLoop:
                AudioSystem::StopConditionalSound(audio_event.channel_id);
                break;
        }
    }
}

} // namespace igi
