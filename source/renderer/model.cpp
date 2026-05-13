#include "model.h"
#include "glb_loader.h"
#include "../pch.h"
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <stdexcept>
#include <iostream>

Mesh loadObjModel(const std::string& filepath, const std::string& texturePath) {
    glb_model_s glb = GLB_Load(filepath.c_str());

    std::vector<float> vertices;
    glm::vec3 min_p(1e10f), max_p(-1e10f);
    GLuint first_texture = 0;

    for (const auto& prim : glb.primitives) {
        if (first_texture == 0 && prim.texture_id != 0)
            first_texture = prim.texture_id;
    }

    for (const auto& prim : glb.primitives) {
        if (prim.VAO == 0 || prim.index_count == 0) continue;

        glBindVertexArray(prim.VAO);

        GLint pos_vbo = 0, norm_vbo = 0, uv_vbo = 0;
        GLint pos_size = 0, norm_size = 0, uv_size = 0;

        GLint bound_vbo = 0;
        glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &bound_vbo);
        if (bound_vbo) {
            glBindBuffer(GL_ARRAY_BUFFER, bound_vbo);
            glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &pos_size);
            pos_vbo = bound_vbo;
        }

        glGetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &bound_vbo);
        if (bound_vbo) {
            glBindBuffer(GL_ARRAY_BUFFER, bound_vbo);
            glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &norm_size);
            norm_vbo = bound_vbo;
        }

        glGetVertexAttribiv(2, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &bound_vbo);
        if (bound_vbo) {
            glBindBuffer(GL_ARRAY_BUFFER, bound_vbo);
            glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &uv_size);
            uv_vbo = bound_vbo;
        }

        int vertex_count = pos_size / (3 * (int)sizeof(float));
        if (vertex_count == 0) {
            glBindVertexArray(0);
            continue;
        }

        std::vector<float> pos_data(vertex_count * 3);
        glBindBuffer(GL_ARRAY_BUFFER, pos_vbo);
        glGetBufferSubData(GL_ARRAY_BUFFER, 0, pos_size, pos_data.data());

        std::vector<float> norm_data(vertex_count * 3);
        glBindBuffer(GL_ARRAY_BUFFER, norm_vbo);
        glGetBufferSubData(GL_ARRAY_BUFFER, 0, norm_size, norm_data.data());

        std::vector<float> uv_data(vertex_count * 2);
        glBindBuffer(GL_ARRAY_BUFFER, uv_vbo);
        glGetBufferSubData(GL_ARRAY_BUFFER, 0, uv_size, uv_data.data());

        std::vector<unsigned int> idx_data(prim.index_count);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, prim.EBO);
        glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, prim.index_count * sizeof(unsigned int), idx_data.data());

        glBindVertexArray(0);

        for (int i = 0; i < prim.index_count; i++) {
            unsigned int idx = idx_data[i];
            if (idx >= (unsigned int)vertex_count) continue;

            float vx = pos_data[idx * 3 + 0];
            float vy = pos_data[idx * 3 + 1];
            float vz = pos_data[idx * 3 + 2];

            vertices.push_back(vx);
            vertices.push_back(vy);
            vertices.push_back(vz);

            min_p.x = std::min(min_p.x, vx);
            min_p.y = std::min(min_p.y, vy);
            min_p.z = std::min(min_p.z, vz);
            max_p.x = std::max(max_p.x, vx);
            max_p.y = std::max(max_p.y, vy);
            max_p.z = std::max(max_p.z, vz);

            vertices.push_back(norm_data[idx * 3 + 0]);
            vertices.push_back(norm_data[idx * 3 + 1]);
            vertices.push_back(norm_data[idx * 3 + 2]);

            vertices.push_back(uv_data[idx * 2 + 0]);
            vertices.push_back(uv_data[idx * 2 + 1]);
        }
    }

    for (auto& prim : glb.primitives) {
        prim.texture_id = 0;
    }
    GLB_Free(glb);

    if (vertices.empty()) {
        throw std::runtime_error("GLB file contains no geometry: " + filepath);
    }

    Mesh mesh;
    mesh.textureID = first_texture;
    mesh.vertexCount = static_cast<int>(vertices.size()) / 8;
    mesh.halfExtents = (max_p - min_p) * 0.5f;
    mesh.zOffset = -min_p.y;

    mesh.vertexData = new float[vertices.size()];
    memcpy(mesh.vertexData, vertices.data(), vertices.size() * sizeof(float));

    glGenVertexArrays(1, &mesh.VAO);
    glGenBuffers(1, &mesh.VBO);

    glBindVertexArray(mesh.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    std::cout << "[GLB] Loaded: " << filepath << " | Vertices: " << mesh.vertexCount << "\n";
    return mesh;
}

void renderModel(const Mesh& mesh) {
    if (mesh.VAO == 0) return;

    // Draw
    glBindVertexArray(mesh.VAO);
    glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
    glBindVertexArray(0);
}

void destroyModel(Mesh& mesh) {
    glDeleteBuffers(1,      &mesh.VBO);
    glDeleteVertexArrays(1, &mesh.VAO);

    // Free client-side vertex data
    if (mesh.vertexData) {
        delete[] mesh.vertexData;
        mesh.vertexData = nullptr;
    }

    mesh.VAO = mesh.VBO = 0;
    mesh.vertexCount = 0;
}
