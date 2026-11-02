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
    void OnMouseWheel(int direction);
    void SetMapComputerOpen(bool is_open);

    PlayerInputCmd ConsumeGameplayInput();

    void ResetInputState();

private:
    void UpdateMovementAxes();

    WindowFocusTarget focus_target_ = WindowFocusTarget::EditorWindow;
    ProfileConfig profile_;
    PlayerInputCmd current_cmd_;
    bool forward_key_down_ = false;
    bool backward_key_down_ = false;
    bool left_strafe_key_down_ = false;
    bool right_strafe_key_down_ = false;
    bool map_computer_key_down_ = false;
    bool map_computer_open_ = false;
    bool map_drag_active_ = false;
};

} // namespace igi
