#pragma once
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <string>
#include <glm/glm.hpp>
class IGIBridge {
public:
    struct PositionData {
        glm::vec3 raw_pos;
        glm::vec3 meters_pos;
        bool connected = false;
        std::string status_msg = "IGI LINK: NATIVE MODE";
        uint32_t human_addr = 0;
        int game_level = 0;
        float view_h = 0.0f; // Horizontal angle
        float view_v = 0.0f; // Vertical angle
        float cam_pitch = 0.0f;
        float cam_yaw = 0.0f;
        float cam_roll = 0.0f;
        float cam_fov = 0.0f;
    };

    void SyncFromEditor(const glm::vec3& raw_pos, float yaw_deg, float pitch_deg, float roll_deg);
    void SetGameLevel(int level_no);

    IGIBridge();
    ~IGIBridge();

    void Start();
    void Stop();
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    PositionData GetLatestData();

private:
    void ThreadLoop();
    void UpdateData();

    std::thread worker_thread_;
    std::atomic<bool> running_;
    std::atomic<bool> enabled_;
    std::mutex data_mutex_;
    PositionData current_data_;
};
