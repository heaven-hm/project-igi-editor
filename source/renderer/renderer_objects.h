#pragma once
#include "../pch.h"
#include "model.h"
#include "../level/level_objects.h"
#include <map>
#include <string>
#include <vector>
#include <unordered_set>
#include <set>

struct AttachInfo {
    std::string modelId;
    float px, py, pz;           // raw game-unit position from ATTA record
    float r[9];                 // 3x3 rotation matrix (r00..r08)
};

// One pickable ATTA sub-model instance, captured during the picking pass so the
// app can "promote" it into a real, editable EditRigidObj task at the same world
// transform when the user clicks it.
struct AttaPickEntry {
    int          parentObjIndex = -1;
    std::string  modelId;
    std::string  immediateParentModelId; // MEF file that directly contains this ATTA record
    glm::vec3    worldPos = glm::vec3(0.0f);
    glm::mat3    worldRot = glm::mat3(1.0f);
    float        scale = 1.0f;
    glm::vec3    localPos = glm::vec3(0.0f);
    int          recordIndex = -1;
    glm::mat4    parentWorldMat = glm::mat4(1.0f);
};

class Renderer_Objects {
public:
    // Pick IDs at/above this base denote a picked ATTA sub-model (entry =
    // returnedValue - kAttaPickBase); below it they are normal object indices.
    static constexpr int kAttaPickBase = 0x800000; // 8388608

    bool GetAttaPickEntry(int entry, AttaPickEntry& out) const {
        if (entry < 0 || entry >= (int)atta_pick_entries_.size()) return false;
        out = atta_pick_entries_[entry];
        return true;
    }
    // Permanently suppress an ATTA (by its ORIGINAL model@worldPos key) once it has
    // been promoted to a real task. Stays hidden even after the promoted object is
    // moved away — otherwise the original ATTA reappears (looks like a clone/ghost
    // and z-fights). Cleared on level load.
    void SuppressAtta(const std::string& key) { suppressed_atta_keys_.insert(key); }
    void ClearSuppressedAttas() { suppressed_atta_keys_.clear(); promoted_atta_records_.clear(); }
    void MarkAttaPromotedByRecord(const std::string& parentModelId, int recordIndex) {
        promoted_atta_records_.insert(parentModelId + ":" + std::to_string(recordIndex));
    }
    bool SuppressAttachmentInMef(const std::string& parentModelId, const std::string& attModelId, const glm::vec3& localPos);
    bool UpdateAttaLocalPosInMef(const std::string& parentModelId, bool isBuilding, int recordIndex, const glm::vec3& newLocalPos, const glm::mat3& newLocalRot);
    // Stable key "model@roundedWorldPos" used to match an ATTA against an EditRigidObj.
    static std::string AttaOccupancyKey(const std::string& modelId, const glm::vec3& worldPos);
    Renderer_Objects();
    ~Renderer_Objects();

    bool Init();
    void Shutdown();

    void SetLevel(int level) { current_level_ = level; }
    void ClearCaches();

    // Diagnostics: live cache occupancy (for level-switch logging).
    size_t GetMeshCacheCount() const { return mesh_cache_.size(); }
    size_t GetTextureCacheCount() const { return texture_cache_.size(); }

    void Draw(GLuint ubo_mats, bool overlay_wireframe, const std::vector<LevelObject>& objects, int selected_object_index, int hover_object_index, int draw_parts, const glm::vec3& camera_pos, bool show_magic_obj_spheres = false);
    int PickObjectAtScreen(int x, int y, int w, int h,
                           GLuint ubo_mats,
                           const std::vector<LevelObject>& objects,
                           int draw_parts,
                           const glm::vec3& camera_pos);
    static bool IsSkippedModelId(const std::string& modelId);
    glm::vec3 GetMeshExtents(const std::string& modelId, bool isBuilding);
    float GetMeshZOffset(const std::string& modelId, bool isBuilding);
    Mesh GetOrLoadMesh(const std::string& modelId, bool isBuilding);
    // Render one model centered in a viewport rectangle, auto-rotated by (rotX,rotY)
    // radians, for the model-picker preview. Uploads its own preview camera into the
    // shared matrices UBO (overwritten next frame by the scene), so it must be called
    // after the main scene pass. Does not touch the fixed-function matrix stack.
    void DrawModelPreview(const std::string& modelId, GLuint ubo_mats,
                          int vpX, int vpY, int vpW, int vpH,
                          float rotX, float rotY);
    GLuint GetShaderProgram() const { return shader_program_; }
    void DrawAttachmentsForSpline(const std::string& modelId, bool isBuilding,
                                  const glm::mat4& unscaledWorldMat, GLuint ubo_mats,
                                  glm::vec3 leafScale = glm::vec3(40.96f));

private:
    int current_level_ = 1;
    std::map<std::string, Mesh> mesh_cache_;
    std::map<std::string, GLuint> texture_cache_;
    std::map<std::string, std::vector<std::string>> model_texture_map_cache_;
    mutable std::map<std::string, std::vector<std::string>> global_texture_map_;
    mutable bool global_texture_map_loaded_ = false;
    mutable std::map<std::string, int> model_level_map_;
    mutable std::map<std::string, int> texture_level_map_;
    std::map<std::string, std::vector<AttachInfo>> attachment_cache_;
    // Per-pick-pass capture of pickable ATTA sub-models (see AttaPickEntry).
    std::vector<AttaPickEntry> atta_pick_entries_;
    // Keys (model@pos) of EditRigidObj tasks; an ATTA matching one is suppressed
    // (it has been promoted to / duplicated by a real task). Rebuilt each frame.
    std::unordered_set<std::string> editrigid_occupancy_;
    // Persistent keys of ATTAs promoted this session (by their ORIGINAL position),
    // so they stay hidden even after the promoted object is moved.
    std::unordered_set<std::string> suppressed_atta_keys_;
    std::set<std::string>           promoted_atta_records_; // "parentModelId:recordIndex"
    int texture_map_level_ = -1;
    GLuint shader_program_;
    GLint  loc_glass_min_ = -1;  // u_glassMin location (glass sheen floor), set in Draw
    GLuint ubo_binding_point_;
    GLuint selection_vao_, selection_vbo_;
    std::unordered_set<std::string> logged_draw_buildings_;
    std::set<std::string> window_model_ids_;
    bool window_ids_loaded_ = false;

    std::set<std::string> ai_model_ids_;
    bool ai_ids_loaded_ = false;

    std::map<std::string, float> portal_distances_;
    bool portal_distances_loaded_ = false;

    std::unordered_set<std::string> deathzone_ids_;
    bool deathzone_ids_loaded_ = false;

    std::set<std::string> magicobj_ids_;
    bool magicobj_ids_loaded_ = false;
    GLuint sphere_vao_ = 0;
    GLuint sphere_vbo_ = 0;
    int sphere_vertex_count_ = 0;

    // GPU color picking FBO
    GLuint pick_fbo_          = 0;
    GLuint pick_color_tex_    = 0;
    GLuint pick_depth_rb_     = 0;
    GLuint pick_shader_prog_  = 0;
    int    pick_fbo_w_        = 0;
    int    pick_fbo_h_        = 0;

    void LoadAttachmentsRecursive(const std::string& modelId, bool isBuilding, std::unordered_set<std::string>& visited);
    void DrawAttachmentsRecursive(const std::string& topLevelModelId, const std::string& parentModelId, bool isBuilding, const glm::mat4& parentWorldMat,
                                   bool isTransparentPass, GLint loc_model, GLint loc_dirlight,
                                   GLint loc_ambient, GLint loc_useTex, GLint loc_tex, GLint loc_alpha,
                                   std::unordered_set<std::string>& drawn,
                                   glm::vec3 leafScale = glm::vec3(40.96f));
    // Like DrawAttachmentsRecursive but uses the picking shader. Each ATTA sub-model
    // that has no matching EditRigidObj (occupancy check) gets a UNIQUE pick ID
    // (kAttaPickBase + entry) and is recorded in atta_pick_entries_ with its world
    // transform, so the app can promote it into an editable task on click. ATTAs
    // already promoted/duplicated by an EditRigidObj are skipped.
    void DrawAttachmentsForPicking(const std::string& parentModelId, bool isBuilding,
                                   const glm::mat4& parentWorldMat, float parentScale,
                                   GLint loc_model, GLint loc_id, int parentObjIndex,
                                   std::unordered_set<std::string>& drawn);
    // True if this ATTA has been promoted/duplicated (live occupancy OR persistent).
    bool IsAttaPromoted(const std::string& modelId, const glm::vec3& worldPos) const {
        const std::string k = AttaOccupancyKey(modelId, worldPos);
        return editrigid_occupancy_.count(k) > 0 || suppressed_atta_keys_.count(k) > 0;
    }
    static bool IsVehicleType(const std::string& type);
    void EnsurePortalDistancesLoaded();
    void EnsureWindowModelIdsLoaded();
    void EnsureAiModelIdsLoaded();
    void EnsureDeathZoneIdsLoaded();
    void EnsureMagicObjIdsLoaded();
    void InitSphereMesh();
    void DrawMagicObjSpheres(const std::vector<LevelObject>& objects, GLuint ubo_mats);
    void DrawSelectionBox(const LevelObject& obj, GLuint ubo_mats, const glm::vec4& color);
    Mesh CreateCubeMesh();
    Mesh CreateTextMesh(const std::string& text);
    void AddCharacterVertices(std::vector<float>& vertices, char c, float x, float y, float scale);
    std::string FindModelFile(const std::string& modelId, bool isBuilding);
    std::string FindTextureFile(const std::string& textureId) const;
    std::string GetLevelTexturesPath() const;
    std::string GetLevelTextureDatPath() const;
    void LoadDatIntoMap(const std::string& datPath, std::map<std::string, std::vector<std::string>>& outMap);
    void EnsureTextureMapLoaded();
    void EnsureGlobalTextureMapLoaded() const;
    std::vector<std::string> GetTextureIdsForModel(const std::string& modelId);
    GLuint GetOrLoadTexture(const std::string& textureId);
    void ApplyTexturesToMesh(Mesh& mesh, const std::string& modelId, const std::string& parentModelId = "");
    void InitSelectionBox();
    void InitPickingFBO(int w, int h);
    void DrawForPicking(GLuint ubo_mats,
                        const std::vector<LevelObject>& objects,
                        int draw_parts,
                        const glm::vec3& camera_pos);
};
