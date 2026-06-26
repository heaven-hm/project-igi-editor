#include "pch.h"
#include "debug_command_manager.h"
#include "app.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/tinygltf/stb_image_write.h"
#include "renderer/renderer.h"
#include "level/level.h"
#include "level/level_objects.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <direct.h>

DebugCommandManager::DebugCommandManager(App* app) 
    : app_(app), running_(false), commands_file_path_("editor/tools/debug-command.txt") {
}

DebugCommandManager::~DebugCommandManager() {
    Stop();
}

void DebugCommandManager::Start() {
    if (running_) return;
    running_ = true;
    watcher_thread_ = std::thread(&DebugCommandManager::WatcherThread, this);
}

void DebugCommandManager::Stop() {
    running_ = false;
    if (watcher_thread_.joinable()) {
        watcher_thread_.join();
    }
}

void DebugCommandManager::WatcherThread() {
    while (running_) {
        std::ifstream file(commands_file_path_);
        if (file.is_open()) {
            std::string line;
            std::vector<std::string> lines;
            bool has_commands = false;
            while (std::getline(file, line)) {
                lines.push_back(line);
                if (line.empty()) continue;
                
                std::istringstream iss(line);
                std::string token;
                iss >> token;
                
                if (token == "goto" || token == "capture-model") {
                    DebugCommand cmd;
                    cmd.type = token;
                    while (iss >> token) {
                        if (token.find("level=") == 0) {
                            cmd.level = std::stoi(token.substr(6));
                        } else if (token.find("model=") == 0) {
                            cmd.modelId = token.substr(6);
                        } else if (token.find("x=") == 0) {
                            cmd.x = std::stod(token.substr(2));
                            cmd.has_pos = true;
                        } else if (token.find("y=") == 0) {
                            cmd.y = std::stod(token.substr(2));
                            cmd.has_pos = true;
                        } else if (token.find("z=") == 0) {
                            cmd.z = std::stod(token.substr(2));
                            cmd.has_pos = true;
                        }
                    }
                    std::lock_guard<std::mutex> lock(queue_mutex_);
                    command_queue_.push(cmd);
                    has_commands = true;
                }
            }
            file.close();

            if (has_commands) {
                // Clear file
                std::ofstream out_file(commands_file_path_, std::ios::trunc);
                out_file.close();
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void DebugCommandManager::Update() {
    std::queue<DebugCommand> local_queue;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        std::swap(local_queue, command_queue_);
    }

    while (!local_queue.empty()) {
        ProcessCommand(local_queue.front());
        local_queue.pop();
    }
}

void DebugCommandManager::ProcessCommand(const DebugCommand& cmd) {
    if (cmd.level != -1 && cmd.level != app_->GetCurLevelNo()) {
        app_->LoadLevel(cmd.level);
    }
    
    if (cmd.type == "goto") {
        GotoModel(cmd);
    } else if (cmd.type == "capture-model") {
        CaptureModel(cmd);
    }
}

void DebugCommandManager::GotoModel(const DebugCommand& cmd) {
    auto& objects = app_->level_.GetLevelObjects().GetObjects();
    
    double cx = cmd.x, cy = cmd.y, cz = cmd.z;
    if (std::abs(cx) < 1000000.0 && std::abs(cy) < 1000000.0 && std::abs(cz) < 1000000.0) {
        cx *= 256.0; cy *= 256.0; cz *= 256.0; // Assume meters, convert to engine units
    }

    int best_idx = -1;
    double min_dist = 1e30;

    for (size_t i = 0; i < objects.size(); ++i) {
        if (!objects[i].deleted && objects[i].modelId == cmd.modelId) {
            if (!cmd.has_pos) {
                best_idx = (int)i;
                break; // If no pos specified, pick first
            }
            double dx = objects[i].pos.x - cx;
            double dy = objects[i].pos.y - cy;
            double dz = objects[i].pos.z - cz;
            double dist_sq = dx*dx + dy*dy + dz*dz;
            if (dist_sq < min_dist) {
                min_dist = dist_sq;
                best_idx = (int)i;
            }
        }
    }

    if (best_idx != -1) {
        app_->viewer_.pos_ = glm::vec3(objects[best_idx].pos);
        app_->viewer_.yaw_ = -objects[best_idx].rot.z;
        app_->viewer_.pitch_ = 0;
        app_->viewer_.roll_ = 0;
        
        app_->UpdateViewerVectors();
        app_->selected_object_index_ = best_idx;
    }
}

void DebugCommandManager::CaptureModel(const DebugCommand& cmd) {
    auto& objects = app_->level_.GetLevelObjects().GetObjects();
    
    double cx = cmd.x, cy = cmd.y, cz = cmd.z;
    if (std::abs(cx) < 1000000.0 && std::abs(cy) < 1000000.0 && std::abs(cz) < 1000000.0) {
        cx *= 256.0; cy *= 256.0; cz *= 256.0; // Assume meters, convert to engine units
    }

    int target_idx = -1;
    double min_dist = 1e30;

    for (size_t i = 0; i < objects.size(); ++i) {
        if (!objects[i].deleted && objects[i].modelId == cmd.modelId) {
            if (!cmd.has_pos) {
                target_idx = (int)i;
                break; // If no pos specified, pick first
            }
            double dx = objects[i].pos.x - cx;
            double dy = objects[i].pos.y - cy;
            double dz = objects[i].pos.z - cz;
            double dist_sq = dx*dx + dy*dy + dz*dz;
            if (dist_sq < min_dist) {
                min_dist = dist_sq;
                target_idx = (int)i;
            }
        }
    }
    
    if (target_idx == -1) return;
    
    auto& obj = objects[target_idx];
    app_->selected_object_index_ = target_idx;
    
    // Retrieve mesh bounds to properly frame the model
    glm::vec3 extents = app_->renderer_.GetMeshExtents(obj.modelId, obj.isBuilding);
    glm::vec3 center = app_->renderer_.GetMeshCenter(obj.modelId, obj.isBuilding);
    
    float distance = glm::length(extents) * 40.96f * obj.scale * 2.5f;
    if (distance < 5.0f) distance = 25.0f;
    
    // The visual center of the object in world space:
    glm::mat4 objMat = glm::translate(glm::mat4(1.0f), glm::vec3(obj.pos));
    objMat = glm::rotate(objMat, (float)obj.rot.z, glm::vec3(0.0f, 0.0f, 1.0f));
    objMat = glm::rotate(objMat, (float)obj.rot.y, glm::vec3(0.0f, 1.0f, 0.0f));
    objMat = glm::rotate(objMat, (float)obj.rot.x, glm::vec3(1.0f, 0.0f, 0.0f));
    objMat = glm::scale(objMat, glm::vec3(40.96f * (float)obj.scale));
    glm::vec3 worldCenter = glm::vec3(objMat * glm::vec4(center, 1.0f));
    
    int width = app_->window_state_.viewport_width_;
    int height = app_->window_state_.viewport_height_;
    std::vector<unsigned char> pixels(width * height * 3);
    std::vector<unsigned char> flipped(width * height * 3);

    auto capture_angle = [&](const char* suffix, float angle_offset, float pitch_deg, float z_lift) {
        // We set the viewer's yaw and pitch
        app_->viewer_.yaw_ = -obj.rot.z + angle_offset;
        app_->viewer_.pitch_ = pitch_deg;
        app_->UpdateViewerVectors();
        
        glm::vec3 camTarget = worldCenter;
        camTarget.z += z_lift;
        
        // Position camera back from the object's visual center
        app_->viewer_.pos_ = camTarget - app_->viewer_.forward_ * distance;
        
        // IMPORTANT: Must sync view_define_ for the renderer to actually use this camera pos
        app_->UpdateViewDefine();
        
        // Render frame twice to ensure the double-buffer has the 3D scene in the back buffer
        app_->OnDisplay();
        app_->OnDisplay();
        
        // Read from back buffer
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        
        // Flip image vertically
        for (int y = 0; y < height; ++y) {
            memcpy(&flipped[((height - 1 - y) * width * 3)], &pixels[(y * width * 3)], width * 3);
        }
        
        char filename[256];
        snprintf(filename, sizeof(filename), "screenshots/Level%02d_Model%s_%s.png", cmd.level, cmd.modelId.c_str(), suffix);
        stbi_write_png(filename, width, height, 3, flipped.data(), width * 3);
    };

    _mkdir("screenshots");
    
    // 6 Exterior shots (every 60 degrees)
    for (int angle_deg = 0; angle_deg < 360; angle_deg += 60) {
        char suffix[32];
        snprintf(suffix, sizeof(suffix), "Ext_%03d", angle_deg);
        capture_angle(suffix, (float)angle_deg, -15.0f, 256.0f * 10.0f); // Pitch down, lift camera 10 meters
    }
    
    // 4 Interior shots (distance = 0, every 90 degrees)
    distance = 0.0f;
    for (int angle_deg = 0; angle_deg < 360; angle_deg += 90) {
        char suffix[32];
        snprintf(suffix, sizeof(suffix), "Int_%03d", angle_deg);
        capture_angle(suffix, (float)angle_deg, 0.0f, 256.0f * 1.5f); // Level pitch, eye height 1.5 meters
    }
}
