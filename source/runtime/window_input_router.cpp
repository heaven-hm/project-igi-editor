// window_input_router.cpp - Deterministic input routing between Editor and Gameplay windows implementation
#include "window_input_router.h"
#include <cctype>

namespace igi {

WindowInputRouter::WindowInputRouter() {
    profile_.SetDefaultBindings();
}

void WindowInputRouter::SetProfile(const ProfileConfig& profile) {
    profile_ = profile;
}

void WindowInputRouter::SetFocus(WindowFocusTarget target) {
    focus_target_ = target;
    ResetInputState();
}

void WindowInputRouter::ResetInputState() {
    current_cmd_ = PlayerInputCmd();
    forward_key_down_ = false;
    backward_key_down_ = false;
    left_strafe_key_down_ = false;
    right_strafe_key_down_ = false;
    map_computer_key_down_ = false;
    map_drag_active_ = false;
}

void WindowInputRouter::UpdateMovementAxes() {
    current_cmd_.forward = (forward_key_down_ ? 1.0f : 0.0f) +
        (backward_key_down_ ? -1.0f : 0.0f);
    current_cmd_.strafe = (right_strafe_key_down_ ? 1.0f : 0.0f) +
        (left_strafe_key_down_ ? -1.0f : 0.0f);
}

void WindowInputRouter::OnKeyboardKey(int key, bool is_down) {
    if (focus_target_ != WindowFocusTarget::GameplayWindow) return;

    const unsigned char up_key = static_cast<unsigned char>(
        std::toupper(static_cast<unsigned char>(key)));

    const unsigned char forward_key = static_cast<unsigned char>(std::toupper(
        profile_.GetKeyForAction("MoveUp", 'W')));
    const unsigned char backward_key = static_cast<unsigned char>(std::toupper(
        profile_.GetKeyForAction("MoveDown", 'S')));
    const unsigned char left_strafe_key = static_cast<unsigned char>(std::toupper(
        profile_.GetKeyForAction("MoveLeft", 'A')));
    const unsigned char right_strafe_key = static_cast<unsigned char>(std::toupper(
        profile_.GetKeyForAction("MoveRight", 'D')));
    const unsigned char jump_key = static_cast<unsigned char>(std::toupper(
        profile_.GetKeyForAction("Jump", ' ')));
    const unsigned char crouch_key = static_cast<unsigned char>(std::toupper(
        profile_.GetKeyForAction("Crouch", 17)));
    const unsigned char reload_key = static_cast<unsigned char>(std::toupper(
        profile_.GetKeyForAction("Reload", 'R')));
    const unsigned char activate_key = static_cast<unsigned char>(std::toupper(
        profile_.GetKeyForAction("Activate", 'E')));
    const unsigned char map_computer_key = static_cast<unsigned char>(std::toupper(
        profile_.GetKeyForAction("MapComputer", 'C')));

    if (up_key == forward_key) {
        forward_key_down_ = is_down;
        UpdateMovementAxes();
    } else if (up_key == backward_key) {
        backward_key_down_ = is_down;
        UpdateMovementAxes();
    } else if (up_key == right_strafe_key) {
        right_strafe_key_down_ = is_down;
        UpdateMovementAxes();
    } else if (up_key == left_strafe_key) {
        left_strafe_key_down_ = is_down;
        UpdateMovementAxes();
    } else if (up_key == jump_key || key == ' ') {
        current_cmd_.jump = is_down;
    } else if (up_key == crouch_key) {
        current_cmd_.crouch = is_down;
    } else if (up_key == activate_key || up_key == 'E') {
        current_cmd_.interact = is_down;
    } else if (up_key == reload_key || up_key == 'R') {
        current_cmd_.reload = is_down;
    } else if (up_key == map_computer_key) {
        current_cmd_.map_computer = is_down && !map_computer_key_down_;
        map_computer_key_down_ = is_down;
    } else if (key >= '1' && key <= '9') {
        if (is_down) current_cmd_.switch_weapon = key - '1';
    }
}

void WindowInputRouter::OnMouseMove(float delta_x, float delta_y) {
    if (focus_target_ != WindowFocusTarget::GameplayWindow) return;

    if (map_computer_open_ && map_drag_active_) {
        constexpr float map_pointer_normalization = 0.001f;
        current_cmd_.map_pan_delta_x += delta_x * map_pointer_normalization;
        current_cmd_.map_pan_delta_y += delta_y * map_pointer_normalization;
        return;
    }

    float sensitivity = profile_.mouse_sensitivity * 0.35f;
    if (sensitivity <= 0.001f) sensitivity = 0.15f;

    // Standard FPS mouse orientation (moving mouse right turns right, moving mouse up looks up)
    current_cmd_.yaw_delta += -delta_x * sensitivity;
    float y_dir = profile_.invert_mouse ? -1.0f : 1.0f;
    current_cmd_.pitch_delta += -delta_y * sensitivity * y_dir;
}

void WindowInputRouter::OnMouseButton(int button, bool is_down) {
    if (focus_target_ != WindowFocusTarget::GameplayWindow) return;

    if (button == 1 && map_computer_open_) {
        map_drag_active_ = is_down;
        return;
    }

    int fire_btn = profile_.GetMouseButtonForAction("Fire", 0);
    if (button == fire_btn || button == 0) { // 0 = Left click
        current_cmd_.fire = is_down;
    } else if (button == 2) { // 2 = Right click (Zoom / Aim down sights)
        current_cmd_.zoom = is_down;
    }
}

void WindowInputRouter::OnMouseWheel(int direction) {
    if (focus_target_ != WindowFocusTarget::GameplayWindow ||
        !map_computer_open_ || direction == 0) {
        return;
    }

    current_cmd_.map_zoom_delta += direction > 0 ? 1 : -1;
}

void WindowInputRouter::SetMapComputerOpen(bool is_open) {
    map_computer_open_ = is_open;
    if (!is_open) {
        map_drag_active_ = false;
    }
}

PlayerInputCmd WindowInputRouter::ConsumeGameplayInput() {
    PlayerInputCmd cmd = current_cmd_;
    // Clear momentary deltas and single-trigger events
    current_cmd_.yaw_delta = 0.0f;
    current_cmd_.pitch_delta = 0.0f;
    current_cmd_.jump = false;
    current_cmd_.reload = false;
    current_cmd_.interact = false;
    current_cmd_.map_computer = false;
    current_cmd_.map_pan_delta_x = 0.0f;
    current_cmd_.map_pan_delta_y = 0.0f;
    current_cmd_.map_zoom_delta = 0;
    current_cmd_.switch_weapon = -1;
    return cmd;
}

} // namespace igi
