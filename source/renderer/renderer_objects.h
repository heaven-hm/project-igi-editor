#pragma once
#include "../pch.h"
#include "model.h"
#include "../level/level_objects.h"
#include <map>
#include <string>

class Renderer_Objects {
public:
    Renderer_Objects();
    ~Renderer_Objects();

    bool Init();
    void Shutdown();

    void Draw(GLuint ubo_mats, bool overlay_wireframe, const std::vector<LevelObject>& objects, int selected_object_index = -1);

private:
    std::map<std::string, Mesh> mesh_cache_;
    GLuint shader_program_;
    GLuint ubo_binding_point_;
    GLuint selection_vao_, selection_vbo_;
    
    Mesh GetOrLoadMesh(const std::string& modelId);
    std::string FindModelFile(const std::string& modelId);
    void DrawSelectionBox(const LevelObject& obj, GLuint ubo_mats);
    void InitSelectionBox();
};

