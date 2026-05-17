#pragma once
#include "../pch.h"
#include <vector>

struct glb_primitive_s {
    GLuint VAO, VBO, EBO;
    GLuint texture_id;
    int index_count;
    glm::mat4 local_transform;
    std::vector<GLuint> extra_vbos;
    int alpha_mode; // 0=OPAQUE, 1=MASK, 2=BLEND
    glm::vec4 baseColorFactor;

    glb_primitive_s() : VAO(0), VBO(0), EBO(0), texture_id(0), index_count(0), local_transform(1.0f), alpha_mode(0), baseColorFactor(1.0f) {}
};

struct glb_model_s {
    std::vector<glb_primitive_s> primitives;
};

glb_model_s  GLB_Load(const char* path);
void         GLB_Free(glb_model_s& model);

// Load a texture from an external file (PNG, JPG, etc.) for use with GLB models
// that have no embedded textures. Returns 0 on failure.
GLuint       GLB_LoadExternalTexture(const char* path);
