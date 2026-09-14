#pragma once
#include "../pch.h"
#include "../level/level_objects.h"
#include "renderer_objects.h"
#include "spline_geometry.h"
#include <array>
#include <functional>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class Renderer_Splines {
public:
    Renderer_Splines(Renderer_Objects& obj_renderer) : obj_renderer_(obj_renderer) {}

    void Init();
    void ClearBentCache();
    void Draw(const std::vector<LevelObject>& objects, GLuint ubo_mats, GLuint shader_program);
    void RequestTrace(std::string path) { pending_trace_path_ = std::move(path); }

    // Optional terrain height callback — when set, tile Z positions are snapped to
    // max(hermite_z, terrain_z) so flat track sits on terrain and elevated sections
    // stay above it. Signature: (world_x, world_y, out_z) → true if terrain found.
    void SetTerrainQuery(std::function<bool(double, double, float&)> fn) {
        terrain_z_fn_ = std::move(fn);
    }

private:
    struct TraceTile {
        int routeIndex = -1;
        int intervalIndex = -1;
        int tileIndex = -1;
        int steps = 0;
        int longitudinalAxis = -1;
        std::string containerModelId;
        std::string containerTaskId;
        std::string segmentModelId;
        glm::dvec3 routeBegin{0.0};
        glm::dvec3 routeEnd{0.0};
        glm::dvec3 tileBegin{0.0};
        glm::dvec3 tileEnd{0.0};
        std::array<double, 16> model{};
        std::array<double, 16> unscaledModel{};
        glm::vec3 attachmentScale{1.0f};
        int meshSubmeshCount = 0;
        int meshVertexCount = 0;
    };

    struct BentSpanKey {
        std::string modelId;
        glm::dvec3 p0{0.0};
        glm::dvec3 p1{0.0};
        glm::dvec3 rot0{0.0};
        glm::dvec3 rot1{0.0};
        bool linear = false;
        bool operator==(const BentSpanKey& other) const {
            return linear == other.linear && modelId == other.modelId &&
                   p0 == other.p0 && p1 == other.p1 && rot0 == other.rot0 && rot1 == other.rot1;
        }
    };
    struct BentSpanKeyHash {
        size_t operator()(const BentSpanKey& key) const;
    };

    struct CachedBentSpanGpu {
        std::vector<SubMesh> subMeshes;
    };

    Renderer_Objects& obj_renderer_;
    std::function<bool(double, double, float&)> terrain_z_fn_;
    std::string pending_trace_path_;
    std::unordered_map<BentSpanKey, CachedBentSpanGpu, BentSpanKeyHash> bent_gpu_cache_;
    BentSpanKey bent_lookup_key_;

    void DrawSplineSegment(
        const LevelObject& start,
        const LevelObject& end,
        const LevelObject& parent,
        GLuint ubo_mats,
        GLuint shader_program,
        GLint loc_model,
        GLint loc_dirlight,
        GLint loc_ambient,
        GLint loc_useTex,
        GLint loc_tex,
        GLint loc_useLightmap,
        GLint loc_alpha,
        GLint loc_baseColor,
        GLint loc_tint,
        GLint loc_glassMin,
        std::string_view fallbackSegmentModelId);

};
