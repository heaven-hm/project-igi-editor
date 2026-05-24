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

class Renderer_Objects {
public:
    Renderer_Objects();
    ~Renderer_Objects();

    bool Init();
    void Shutdown();

    void SetLevel(int level) { current_level_ = level; }
    void ClearCaches();

    void Draw(GLuint ubo_mats, bool overlay_wireframe, const std::vector<LevelObject>& objects, int selected_object_index, int hover_object_index, int draw_parts, const glm::vec3& camera_pos);
    static bool IsSkippedModelId(const std::string& modelId);
    glm::vec3 GetMeshExtents(const std::string& modelId, bool isBuilding);
    float GetMeshZOffset(const std::string& modelId, bool isBuilding);
    Mesh GetOrLoadMesh(const std::string& modelId, bool isBuilding);
    GLuint GetShaderProgram() const { return shader_program_; }

private:
    int current_level_ = 1;
    std::map<std::string, Mesh> mesh_cache_;
    std::map<std::string, GLuint> texture_cache_;
    std::map<std::string, std::vector<std::string>> model_texture_map_cache_;
    std::map<std::string, std::vector<AttachInfo>> attachment_cache_;
    int texture_map_level_ = -1;
    GLuint shader_program_;
    GLuint ubo_binding_point_;
    GLuint selection_vao_, selection_vbo_;
    std::unordered_set<std::string> logged_draw_buildings_;
    std::set<std::string> window_model_ids_;
    bool window_ids_loaded_ = false;

    std::map<std::string, float> portal_distances_;
    bool portal_distances_loaded_ = false;

    void EnsurePortalDistancesLoaded();
    void EnsureWindowModelIdsLoaded();
    void DrawSelectionBox(const LevelObject& obj, GLuint ubo_mats, const glm::vec4& color);
    Mesh CreateCubeMesh();
    Mesh CreateTextMesh(const std::string& text);
    void AddCharacterVertices(std::vector<float>& vertices, char c, float x, float y, float scale);
    std::string FindModelFile(const std::string& modelId, bool isBuilding);
    std::string FindTextureFile(const std::string& textureId) const;
    std::string GetLevelTexturesPath() const;
    std::string GetLevelTextureDatPath() const;
    void EnsureTextureMapLoaded();
    std::vector<std::string> GetTextureIdsForModel(const std::string& modelId);
    GLuint GetOrLoadTexture(const std::string& textureId);
    void ApplyTexturesToMesh(Mesh& mesh, const std::string& modelId);
    void InitSelectionBox();
};
