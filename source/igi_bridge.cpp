#include "pch.h"
#include "igi_bridge.h"

IGIBridge::IGIBridge() : running_(false), enabled_(false) {
}
IGIBridge::~IGIBridge() { Stop(); }

void IGIBridge::Start() {
    running_ = true;
    worker_thread_ = std::thread(&IGIBridge::ThreadLoop, this);
}

void IGIBridge::Stop() {
    running_ = false;
    if (worker_thread_.joinable()) worker_thread_.join();
}

void IGIBridge::ThreadLoop() {
    while (running_) {
        UpdateData();
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
    }
}

void IGIBridge::UpdateData() {
    std::lock_guard<std::mutex> lock(data_mutex_);
    current_data_.status_msg = "IGI LINK: NATIVE MODE";
    current_data_.connected = false; // Disconnected from real game memory
}

void IGIBridge::SyncFromEditor(const glm::vec3& raw_pos, float yaw_deg, float pitch_deg, float roll_deg) {
    // No-op: Memory writing disabled
}

void IGIBridge::SetGameLevel(int level_no) {
    // No-op: Memory writing disabled
}

IGIBridge::PositionData IGIBridge::GetLatestData() {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return current_data_;
}
