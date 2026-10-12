// window_input_router.h - Deterministic input routing between Editor and Gameplay windows
#pragma once

#include <cstdint>
#include "../player_controller.h"
#include "config_qvm.h"

namespace igi {

enum class WindowFocusTarget {
    EditorWindow,
    GameplayWindow
};

class WindowInputRouter {
public:
    WindowInputRouter();

    void SetProfile(const ProfileConfig& profile);
    const ProfileConfig& GetProfile() const { return profile_; }

    void SetFocus(WindowFocusTarget target);
    WindowFocusTarget GetFocus() const { return focus_target_; }

    // Input collection for gameplay simulation
    void OnKeyboardKey(int key, bool is_down);
    void OnMouseMove(float delta_x, float delta_y);
    void OnMouseButton(int button, bool is_down);

    PlayerInputCmd ConsumeGameplayInput();

    void ResetInputState();

private:
    WindowFocusTarget focus_target_ = WindowFocusTarget::EditorWindow;
    ProfileConfig profile_;
    PlayerInputCmd current_cmd_;
};

} // namespace igi
