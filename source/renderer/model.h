#pragma once
#include <string>
#include "../pch.h"

struct Mesh {
    unsigned int VAO, VBO;
    unsigned int textureID; // Added texture support
    int vertexCount;
    float* vertexData; // For client-side array rendering
};

Mesh  loadObjModel(const std::string& filepath);
void  renderModel(const Mesh& mesh);
void  destroyModel(Mesh& mesh);
