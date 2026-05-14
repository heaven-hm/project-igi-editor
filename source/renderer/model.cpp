#include "model.h"
#include "glb_loader.h"
#include "../pch.h"
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <stdexcept>
#include <iostream>

Mesh loadObjModel(const std::string& filepath, const std::string& texturePath) {
    glb_model_s glb = GLB_Load(filepath.c_str());

    Mesh mesh;
    glm::vec3 min_p(1e10f), max_p(-1e10f);
    int total_verts = 0;

    for (auto& prim : glb.primitives) {
        if (prim.VAO == 0 || prim.index_count == 0) continue;

        glBindVertexArray(prim.VAO);

        GLint bound_vbo = 0;
        GLint pos_size = 0, norm_size = 0, uv_size = 0;
        GLint pos_vbo = 0, norm_vbo = 0, uv_vbo = 0;

        glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &bound_vbo);
        if (bound_vbo) { glBindBuffer(GL_ARRAY_BUFFER, bound_vbo); glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &pos_size); pos_vbo = bound_vbo; }
        glGetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &bound_vbo);
        if (bound_vbo) { glBindBuffer(GL_ARRAY_BUFFER, bound_vbo); glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &norm_size); norm_vbo = bound_vbo; }
        glGetVertexAttribiv(2, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &bound_vbo);
        if (bound_vbo) { glBindBuffer(GL_ARRAY_BUFFER, bound_vbo); glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &uv_size); uv_vbo = bound_vbo; }

        int vertex_count = pos_vbo ? pos_size / (3 * (int)sizeof(float)) : 0;
        if (vertex_count == 0) { glBindVertexArray(0); continue; }

        std::vector<float> pos_data(vertex_count * 3);
        glBindBuffer(GL_ARRAY_BUFFER, pos_vbo);
        glGetBufferSubData(GL_ARRAY_BUFFER, 0, pos_size, pos_data.data());

        std::vector<float> norm_data(vertex_count * 3, 0.0f);
        if (norm_vbo && norm_size > 0) { glBindBuffer(GL_ARRAY_BUFFER, norm_vbo); glGetBufferSubData(GL_ARRAY_BUFFER, 0, norm_size, norm_data.data()); }

        std::vector<float> uv_data(vertex_count * 2, 0.0f);
        if (uv_vbo && uv_size > 0) { glBindBuffer(GL_ARRAY_BUFFER, uv_vbo); glGetBufferSubData(GL_ARRAY_BUFFER, 0, uv_size, uv_data.data()); }

        std::vector<unsigned int> idx_data(prim.index_count);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, prim.EBO);
        glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, prim.index_count * sizeof(unsigned int), idx_data.data());
        glBindVertexArray(0);

        // Build interleaved vertex array for this primitive
        std::vector<float> verts;
        verts.reserve(prim.index_count * 8);
        for (int i = 0; i < prim.index_count; i++) {
            unsigned int idx = idx_data[i];
            if (idx >= (unsigned int)vertex_count) continue;
            float vx = pos_data[idx*3+0], vy = pos_data[idx*3+1], vz = pos_data[idx*3+2];
            verts.push_back(vx); verts.push_back(vy); verts.push_back(vz);
            min_p.x = std::min(min_p.x, vx); min_p.y = std::min(min_p.y, vy); min_p.z = std::min(min_p.z, vz);
            max_p.x = std::max(max_p.x, vx); max_p.y = std::max(max_p.y, vy); max_p.z = std::max(max_p.z, vz);
            verts.push_back(norm_data[idx*3+0]); verts.push_back(norm_data[idx*3+1]); verts.push_back(norm_data[idx*3+2]);
            verts.push_back(uv_data[idx*2+0]);   verts.push_back(uv_data[idx*2+1]);
        }
        if (verts.empty()) continue;

        SubMesh sub;
        sub.textureID   = prim.texture_id;
        sub.alphaMode   = prim.alpha_mode;
        sub.vertexCount = (int)verts.size() / 8;
        prim.texture_id = 0; // Mesh now owns the texture

        glGenVertexArrays(1, &sub.VAO);
        glGenBuffers(1, &sub.VBO);
        glBindVertexArray(sub.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, sub.VBO);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)0);                  glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)(3*sizeof(float)));  glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)(6*sizeof(float)));  glEnableVertexAttribArray(2);
        glBindVertexArray(0);

        total_verts += sub.vertexCount;
        mesh.subMeshes.push_back(sub);
    }

    GLB_Free(glb);

    if (mesh.subMeshes.empty()) {
        throw std::runtime_error("GLB file contains no geometry: " + filepath);
    }

    // Keep legacy fields for compatibility
    mesh.VAO         = mesh.subMeshes[0].VAO;
    mesh.VBO         = mesh.subMeshes[0].VBO;
    mesh.textureID   = mesh.subMeshes[0].textureID;
    mesh.vertexCount = total_verts;
    mesh.halfExtents = (max_p - min_p) * 0.5f;
    mesh.zOffset     = -min_p.y;
    mesh.vertexData  = nullptr;

    std::cout << "[GLB] Loaded: " << filepath << " | SubMeshes: " << mesh.subMeshes.size() << " | Vertices: " << total_verts << "\n";
    return mesh;
}

void renderModel(const Mesh& mesh) {
    if (mesh.subMeshes.empty() && mesh.VAO == 0) return;

    if (!mesh.subMeshes.empty()) {
        for (const auto& sub : mesh.subMeshes) {
            if (sub.VAO == 0 || sub.vertexCount == 0) continue;
            glBindVertexArray(sub.VAO);
            glDrawArrays(GL_TRIANGLES, 0, sub.vertexCount);
        }
        glBindVertexArray(0);
    } else {
        glBindVertexArray(mesh.VAO);
        glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
        glBindVertexArray(0);
    }
}

void destroyModel(Mesh& mesh) {
    for (auto& sub : mesh.subMeshes) {
        if (sub.VBO) glDeleteBuffers(1, &sub.VBO);
        if (sub.VAO) glDeleteVertexArrays(1, &sub.VAO);
        if (sub.textureID) glDeleteTextures(1, &sub.textureID);
        sub.VAO = sub.VBO = sub.textureID = 0;
    }
    mesh.subMeshes.clear();

    if (mesh.vertexData) {
        delete[] mesh.vertexData;
        mesh.vertexData = nullptr;
    }
    mesh.VAO = mesh.VBO = 0;
    mesh.vertexCount = 0;
}
