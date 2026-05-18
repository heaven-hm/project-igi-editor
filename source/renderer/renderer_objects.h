#pragma once
#include "../pch.h"
#include "model.h"
#include "../level/level_objects.h"
#include <map>
#include <string>
#include <vector>

class Renderer_Objects {
public:
    Renderer_Objects();
    ~Renderer_Objects();

    bool Init();
    void Shutdown();

    void SetLevel(int level) { current_level_ = level; }

    void Draw(GLuint ubo_mats, bool overlay_wireframe, const std::vector<LevelObject>& objects, int selected_object_index, int hover_object_index, int draw_parts);
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
    int texture_map_level_ = -1;
    GLuint shader_program_;
    GLuint ubo_binding_point_;
    GLuint selection_vao_, selection_vbo_;

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
