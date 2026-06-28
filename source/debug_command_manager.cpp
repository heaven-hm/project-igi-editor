#include "pch.h"
#include "debug_command_manager.h"
#include "app.h"
#include "logger.h"
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
                
                if (token == "goto" || token == "capture-model" || token == "delete" || token == "wireframe" || token == "draw-parts") {
                    DebugCommand cmd;
                    cmd.type = token;
                    while (iss >> token) {
                        if (token.find("level=") == 0) {
                            cmd.level = std::stoi(token.substr(6));
                        } else if (token.find("model=") == 0) {
                            cmd.modelId = token.substr(6);
                        } else if (token.find("val=") == 0) {
                            cmd.val = std::stoi(token.substr(4));
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
    } else if (cmd.type == "delete") {
        DeleteModel(cmd);
    } else if (cmd.type == "wireframe") {
        if (cmd.val) {
            if (!app_->GetOverlayWireframe()) app_->ToggleOverlayWireframe();
        } else {
            if (app_->GetOverlayWireframe()) app_->ToggleOverlayWireframe();
        }
    } else if (cmd.type == "draw-parts") {
        app_->SetDrawParts(cmd.val);
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

void DebugCommandManager::DeleteModel(const DebugCommand& cmd) {
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
        objects[best_idx].deleted = true;
        objects[best_idx].modified = true;
        if (app_->selected_object_index_ == best_idx) {
            app_->selected_object_index_ = -1;
        }
        Logger::Get().Log(LogLevel::INFO, "[App] Deleted model via developer command: " + cmd.modelId);
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

    // Ensure all building lightmaps are loaded before capturing — they are not
    // loaded by default; the user normally triggers this via the Escape menu.
    // This call is idempotent (re-uploading the same OLM textures is harmless).
    app_->CalculateLightmapsForAllObjects();

    Logger::Get().Log(LogLevel::INFO, "[CaptureModel] Picked object " + std::to_string(target_idx) +
        ", modelId=" + obj.modelId + ", taskId=" + obj.taskId + ", hasLightmap=" +
        std::to_string(app_->renderer_.HasLightmapForTask(LightmapTaskKey(obj))));
    
    // Camera parameters matching igi_game_plugin.exe defaults exactly so editor
    // and game screenshots are taken from identical world-space positions.
    // Units are native game world units (same as obj.pos).
    static constexpr double kOrbitRadius     = 40000.0;  // exterior orbit radius
    static constexpr double kExteriorHeight  = 20000.0;  // camera Z above obj.pos.z
    static constexpr double kInteriorHeight  = 9750.0;   // eye Z above obj.pos.z inside

    // Exterior pitch: tilt down to aim at the object (same as game plugin auto-pitch)
    const float extPitchDeg = -glm::degrees(static_cast<float>(
        std::atan2(kExteriorHeight, kOrbitRadius)));  // ≈ -26.57°

    int width = app_->window_state_.viewport_width_;
    int height = app_->window_state_.viewport_height_;
    std::vector<unsigned char> pixels(width * height * 3);
    std::vector<unsigned char> flipped(width * height * 3);

    // Editor yaw convention: yaw=0 → forward = +Y (north), same as game plugin.
    // For orbit angle θ: camera is placed at (obj.x - sin(θ)*R, obj.y - cos(θ)*R),
    // so the look-direction is +sin(θ), +cos(θ) in XY.
    // Editor forward.x = -sin(yaw°) and forward.y = cos(yaw°), so:
    //   lookYaw_editor = (360 - orbitYaw_deg) % 360
    auto set_camera = [&](float camX, float camY, float camZ, float lookYawDeg, float pitchDeg) {
        app_->viewer_.pos_   = glm::vec3(camX, camY, camZ);
        app_->viewer_.yaw_   = lookYawDeg;
        app_->viewer_.pitch_ = pitchDeg;
        app_->UpdateViewerVectors();
        app_->UpdateViewDefine();
    };

    auto capture_shot = [&](const char* suffix) {
        // Render twice: first pass fills back-buffer, swap puts it in front, second
        // pass ensures the final scene is in the front buffer we read from.
        app_->OnDisplay();
        app_->OnDisplay();

        glReadBuffer(GL_FRONT);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        glReadBuffer(GL_BACK);

        for (int y = 0; y < height; ++y)
            memcpy(&flipped[((height - 1 - y) * width * 3)], &pixels[(y * width * 3)], width * 3);

        char filename[256];
        snprintf(filename, sizeof(filename), "screenshots/Level%02d_Model%s_%s.png",
                 cmd.level, cmd.modelId.c_str(), suffix);
        stbi_write_png(filename, width, height, 3, flipped.data(), width * 3);
    };

    _mkdir("screenshots");

    // 6 Exterior shots (every 60°) — mirrors game plugin's default 6-shot orbit
    for (int angle_deg = 0; angle_deg < 360; angle_deg += 60) {
        const float rad = glm::radians((float)angle_deg);
        const float camX = (float)obj.pos.x - std::sin(rad) * (float)kOrbitRadius;
        const float camY = (float)obj.pos.y - std::cos(rad) * (float)kOrbitRadius;
        const float camZ = (float)obj.pos.z + (float)kExteriorHeight;
        const float lookYaw = (float)((360 - angle_deg) % 360);

        set_camera(camX, camY, camZ, lookYaw, extPitchDeg);

        char suffix[32];
        snprintf(suffix, sizeof(suffix), "Ext_%03d", angle_deg);
        capture_shot(suffix);
    }

    // 4 Interior shots (every 90°) — camera at object position at eye height, horizontal
    for (int angle_deg = 0; angle_deg < 360; angle_deg += 90) {
        const float camX = (float)obj.pos.x;
        const float camY = (float)obj.pos.y;
        const float camZ = (float)obj.pos.z + (float)kInteriorHeight;
        const float lookYaw = (float)((360 - angle_deg) % 360);

        set_camera(camX, camY, camZ, lookYaw, 0.0f);  // horizontal pitch

        char suffix[32];
        snprintf(suffix, sizeof(suffix), "Int_%03d", angle_deg);
        capture_shot(suffix);
    }
}
