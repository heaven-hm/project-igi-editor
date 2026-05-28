#pragma once
#include <string>
#include <vector>
#include "../pch.h"

struct SubMesh {
    GLuint VAO = 0;
    GLuint VBO = 0;
    int    vertexCount = 0;
    GLuint textureID   = 0;
    int    alphaMode   = 0; // 0=OPAQUE, 1=MASK, 2=BLEND
    glm::vec4 baseColorFactor = glm::vec4(1.0f);
    int    materialSlot = -1;
};

struct Mesh {
    GLuint VAO, VBO, IBO;
    int vertexCount, indexCount;
    GLuint textureID;
    glm::vec3 halfExtents;
    glm::vec3 center;
    float zOffset;
    float mainZOffset; // zOffset from textured submeshes only (for terrain snap)
    float* vertexData;
    bool fromRenderMesh = false; // true = XTRV/DNER (proper UVs); false = XTVC/ECFC fallback (fabricated UVs)
    std::vector<SubMesh> subMeshes;
    std::vector<SubMesh> portalMeshes;
    std::vector<glm::vec3> magicVertices;

    Mesh() : VAO(0), VBO(0), IBO(0), vertexCount(0), indexCount(0), textureID(0), center(0.0f), zOffset(0.0f), mainZOffset(0.0f), vertexData(nullptr) {}
};

Mesh  loadObjModel(const std::string& filepath, const std::string& texturePath = "");
void  renderModel(const Mesh& mesh);
void  destroyModel(Mesh& mesh);
