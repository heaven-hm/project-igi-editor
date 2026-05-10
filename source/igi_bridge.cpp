#include "pch.h"
#include "igi_bridge.h"

IGIBridge::IGIBridge() : running_(false), enabled_(false) {
    gt_.EnableLogs(true);
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
    if (!enabled_) {
        std::lock_guard<std::mutex> lock(data_mutex_);
        current_data_.status_msg = "IGI BRIDGE: STANDBY";
        current_data_.connected = false;
        return;
    }
    std::lock_guard<std::mutex> lock(data_mutex_);
    try {
        if (!gt_.GetGameHandle()) {
            if (gt_.FindGameProcess("igi")) {
                game_base_addr_ = gt_.GetGameBaseAddress();
            } else {
                current_data_.connected = false;
                current_data_.status_msg = "SEARCHING FOR IGI.EXE...";
                return;
            }
        }

        // Pointer chain from CT
        DWORD human_static = 0x0016E210;
        std::vector<DWORD> offsets = {0x8, 0x7CC, 0x14};
        
        gt_.EnableLogs(false);
        DWORD base_ptr = gt_.ReadPointerOffset<DWORD>(game_base_addr_, human_static);
        DWORD human_addr = gt_.ReadPointerOffsets<DWORD>(base_ptr, offsets);

        if (human_addr) {
            current_data_.meters_pos.x = gt_.ReadAddress<float>(human_addr + 0x24);
            current_data_.meters_pos.y = gt_.ReadAddress<float>(human_addr + 0x2C);
            current_data_.meters_pos.z = gt_.ReadAddress<float>(human_addr + 0x34);
            
            current_data_.raw_pos = current_data_.meters_pos * 4096.0f;
            current_data_.human_addr = human_addr;
            current_data_.connected = true;
            current_data_.status_msg = "IGI LINK: CONNECTED";

            // Read additional data
            current_data_.game_level = gt_.ReadAddress<int>(game_base_addr_ + 0x139560);
            current_data_.view_h = gt_.ReadAddress<float>(human_addr + 0x50C);
            current_data_.view_v = gt_.ReadAddress<float>(human_addr + 0xF3C);
            current_data_.cam_pitch = gt_.ReadAddress<float>(0x005CA164);
            current_data_.cam_yaw = gt_.ReadAddress<float>(0x005CA168);
            current_data_.cam_roll = gt_.ReadAddress<float>(0x005CA170);
            current_data_.cam_fov = gt_.ReadAddress<float>(0x005CA174);
            gt_.EnableLogs(true);
        } else {
            gt_.EnableLogs(true);
            current_data_.connected = false;
            current_data_.status_msg = "LINK ERROR: INVALID POINTER";
        }
    } catch (...) {
        current_data_.connected = false;
        current_data_.status_msg = "LINK ERROR: EXCEPTION";
    }
}

void IGIBridge::SyncFromEditor(const glm::vec3& raw_pos, float yaw_deg, float pitch_deg, float roll_deg) {
    if (!enabled_ || !current_data_.connected || current_data_.human_addr == 0) return;

    glm::vec3 meters_pos = raw_pos / 4096.0f;
    float yaw_rad = glm::radians(yaw_deg);
    float pitch_rad = glm::radians(pitch_deg);
    float roll_rad = glm::radians(roll_deg);

    gt_.EnableLogs(false);
    // Write position
    gt_.WriteAddress<float>(current_data_.human_addr + 0x24, meters_pos.x);
    gt_.WriteAddress<float>(current_data_.human_addr + 0x2C, meters_pos.y);
    gt_.WriteAddress<float>(current_data_.human_addr + 0x34, meters_pos.z);

    // Write orientation (Human)
    gt_.WriteAddress<float>(current_data_.human_addr + 0x50C, yaw_rad);
    gt_.WriteAddress<float>(current_data_.human_addr + 0xF3C, pitch_rad);

    // Write orientation (Camera Static)
    gt_.WriteAddress<float>(0x005CA164, pitch_rad);
    gt_.WriteAddress<float>(0x005CA168, yaw_rad);
    gt_.WriteAddress<float>(0x005CA170, roll_rad);
    gt_.EnableLogs(true);
}

void IGIBridge::SetGameLevel(int level_no) {
    if (!enabled_ || game_base_addr_ == 0) return;
    gt_.WriteAddress<int>(game_base_addr_ + 0x139560, level_no);
}

IGIBridge::PositionData IGIBridge::GetLatestData() {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return current_data_;
}
