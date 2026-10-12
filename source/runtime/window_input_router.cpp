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
}

void WindowInputRouter::OnKeyboardKey(int key, bool is_down) {
    if (focus_target_ != WindowFocusTarget::GameplayWindow) return;

    unsigned char up_key = (unsigned char)std::toupper(key);

    unsigned char k_fwd = (unsigned char)std::toupper(profile_.GetKeyForAction("MoveUp", 'W'));
    unsigned char k_bwd = (unsigned char)std::toupper(profile_.GetKeyForAction("MoveDown", 'S'));
    unsigned char k_lft = (unsigned char)std::toupper(profile_.GetKeyForAction("MoveLeft", 'A'));
    unsigned char k_rgt = (unsigned char)std::toupper(profile_.GetKeyForAction("MoveRight", 'D'));
    unsigned char k_jmp = (unsigned char)std::toupper(profile_.GetKeyForAction("Jump", ' '));
    unsigned char k_crc = (unsigned char)std::toupper(profile_.GetKeyForAction("Crouch", 'C'));
    unsigned char k_rld = (unsigned char)std::toupper(profile_.GetKeyForAction("Reload", 'R'));

    if (up_key == k_fwd) {
        current_cmd_.forward = is_down ? 1.0f : (current_cmd_.forward > 0.0f ? 0.0f : current_cmd_.forward);
    } else if (up_key == k_bwd) {
        current_cmd_.forward = is_down ? -1.0f : (current_cmd_.forward < 0.0f ? 0.0f : current_cmd_.forward);
    } else if (up_key == k_rgt) {
        current_cmd_.strafe = is_down ? 1.0f : (current_cmd_.strafe > 0.0f ? 0.0f : current_cmd_.strafe);
    } else if (up_key == k_lft) {
        current_cmd_.strafe = is_down ? -1.0f : (current_cmd_.strafe < 0.0f ? 0.0f : current_cmd_.strafe);
    } else if (up_key == k_jmp || key == ' ') {
        current_cmd_.jump = is_down;
    } else if (up_key == k_crc || up_key == 'C') {
        current_cmd_.crouch = is_down;
    } else if (up_key == k_rld || up_key == 'R') {
        current_cmd_.reload = is_down;
    }
}

void WindowInputRouter::OnMouseMove(float delta_x, float delta_y) {
    if (focus_target_ != WindowFocusTarget::GameplayWindow) return;

    float sensitivity = profile_.mouse_sensitivity * 0.4f;
    if (sensitivity <= 0.001f) sensitivity = 0.15f;

    current_cmd_.yaw_delta += delta_x * sensitivity;
    float y_dir = profile_.invert_mouse ? 1.0f : -1.0f;
    current_cmd_.pitch_delta += delta_y * sensitivity * y_dir;
}

void WindowInputRouter::OnMouseButton(int button, bool is_down) {
    if (focus_target_ != WindowFocusTarget::GameplayWindow) return;

    int fire_btn = profile_.GetMouseButtonForAction("Fire", 0);
    if (button == fire_btn) {
        current_cmd_.fire = is_down;
    }
}

PlayerInputCmd WindowInputRouter::ConsumeGameplayInput() {
    PlayerInputCmd cmd = current_cmd_;
    // Clear momentary deltas
    current_cmd_.yaw_delta = 0.0f;
    current_cmd_.pitch_delta = 0.0f;
    current_cmd_.jump = false;
    current_cmd_.reload = false;
    return cmd;
}

} // namespace igi
