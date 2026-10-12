#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "../pch.h"

namespace igi {

enum class LightmapRenderMode {
    Baked = 0,
    Hybrid = 1,
    Dynamic = 2
};

struct LightmapPage {
    GLuint texture_id = 0;
    uint16_t width = 0;
    uint16_t height = 0;
};

class ObjectLightmapManager {
public:
    static ObjectLightmapManager& Get();

    void LoadLevelLightmaps(int level_no);
    void Clear();

    // Look up lightmap texture for a placement model (e.g. "001_01_1_00000.olm")
    GLuint GetLightmapTexture(const std::string& object_name, int draw_record_index);

    LightmapRenderMode GetRenderMode() const { return render_mode_; }
    void SetRenderMode(LightmapRenderMode mode) { render_mode_ = mode; }
    void CycleRenderMode();
    const char* GetRenderModeName() const;

private:
    ObjectLightmapManager() = default;
    ~ObjectLightmapManager();

    std::unordered_map<std::string, std::vector<LightmapPage>> lightmaps_;
    LightmapRenderMode render_mode_ = LightmapRenderMode::Hybrid;
    int current_level_no_ = 0;
};

} // namespace igi
