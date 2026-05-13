#pragma once
#include "../pch.h"
#include <vector>

struct glb_primitive_s {
    GLuint VAO, VBO, EBO;
    GLuint texture_id;
    int index_count;
    glm::mat4 local_transform;
    std::vector<GLuint> extra_vbos;

    glb_primitive_s() : VAO(0), VBO(0), EBO(0), texture_id(0), index_count(0), local_transform(1.0f) {}
};

struct glb_model_s {
    std::vector<glb_primitive_s> primitives;
};

glb_model_s  GLB_Load(const char* path);
void         GLB_Free(glb_model_s& model);
