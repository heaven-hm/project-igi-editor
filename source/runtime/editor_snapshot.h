// editor_snapshot.h - Editor state capture and restoration for seamless mode switching
#pragma once

#include <glm/glm.hpp>
#include <string>
#include <cstdint>

namespace igi {

struct EditorSnapshot {
    glm::vec3 camera_pos = glm::vec3(0.0f);
    float camera_yaw = 0.0f;
    float camera_pitch = 0.0f;
    uint32_t selected_object_id = 0;
    bool was_edit_mode = true;
    bool cursor_visible = true;
    std::string current_level_path;
};

class EditorSnapshotManager {
public:
    EditorSnapshotManager() = default;

    void Capture(const EditorSnapshot& snapshot) {
        snapshot_ = snapshot;
        has_snapshot_ = true;
    }

    bool Restore(EditorSnapshot& out_snapshot) {
        if (!has_snapshot_) return false;
        out_snapshot = snapshot_;
        return true;
    }

    void Clear() {
        has_snapshot_ = false;
    }

    bool HasSnapshot() const { return has_snapshot_; }

private:
    EditorSnapshot snapshot_;
    bool has_snapshot_ = false;
};

} // namespace igi
