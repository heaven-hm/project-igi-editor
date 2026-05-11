#define TINYOBJLOADER_IMPLEMENTATION
#define TINYOBJLOADER_DISABLE_FAST_FLOAT
#include "../../third_party/tiny_obj_loader.h"
#include "model.h"
#include "../pch.h"
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <stdexcept>
#include <iostream>

Mesh loadObjModel(const std::string& filepath) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t>    shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filepath.c_str());

    if (!warn.empty()) {
		std::cerr << "[OBJ WARN] " << warn << "\n";
	}

    if (!err.empty()) {
		std::cerr << "[OBJ ERR]  " << err  << "\n";
	}
    
	if (!ok) {
		throw std::runtime_error("Failed to load OBJ: " + filepath);
	}

    // Layout: position(3) + normal(3) + uv(2) = 8 floats per vertex
    std::vector<float> vertices;
    vertices.reserve(shapes[0].mesh.indices.size() * 8);

    // Compute Centroid
    glm::vec3 centroid(0.0f);
    int total_v = 0;
    for (const auto& shape : shapes) {
        for (const auto& idx : shape.mesh.indices) {
            centroid.x += attrib.vertices[3 * idx.vertex_index + 0];
            centroid.y += attrib.vertices[3 * idx.vertex_index + 1];
            centroid.z += attrib.vertices[3 * idx.vertex_index + 2];
            total_v++;
        }
    }
    if (total_v > 0) centroid /= (float)total_v;

    glm::vec3 min_p(1e10f), max_p(-1e10f);
    for (const auto& shape : shapes) {
        for (const auto& idx : shape.mesh.indices) {

            // --- Position ---
            float vx = attrib.vertices[3 * idx.vertex_index + 0] - centroid.x;
            float vy = attrib.vertices[3 * idx.vertex_index + 1] - centroid.y;
            float vz = attrib.vertices[3 * idx.vertex_index + 2] - centroid.z;
            
            vertices.push_back(vx);
            vertices.push_back(vy);
            vertices.push_back(vz);

            min_p.x = std::min(min_p.x, vx);
            min_p.y = std::min(min_p.y, vy);
            min_p.z = std::min(min_p.z, vz);
            max_p.x = std::max(max_p.x, vx);
            max_p.y = std::max(max_p.y, vy);
            max_p.z = std::max(max_p.z, vz);


            // --- Normal (fallback to UP if missing) ---
            if (idx.normal_index >= 0) {
                vertices.push_back(attrib.normals[3 * idx.normal_index + 0]);
                vertices.push_back(attrib.normals[3 * idx.normal_index + 1]);
                vertices.push_back(attrib.normals[3 * idx.normal_index + 2]);
            } else {
                vertices.push_back(0.0f);
                vertices.push_back(1.0f);
                vertices.push_back(0.0f);
            }

            // --- UV (fallback to 0,0 if missing) ---
            if (idx.texcoord_index >= 0) {
                vertices.push_back(attrib.texcoords[2 * idx.texcoord_index + 0]);
                vertices.push_back(attrib.texcoords[2 * idx.texcoord_index + 1]);
            } else {
                vertices.push_back(0.0f);
                vertices.push_back(0.0f);
            }
        }
    }

    Mesh mesh;
    mesh.textureID = 0;
    mesh.vertexCount = static_cast<int>(vertices.size()) / 8;
    mesh.halfExtents = (max_p - min_p) * 0.5f;

    // Store vertex data in the mesh for client-side rendering
    mesh.vertexData = new float[vertices.size()];
    memcpy(mesh.vertexData, vertices.data(), vertices.size() * sizeof(float));

    glGenVertexArrays(1, &mesh.VAO);
    glGenBuffers(1,     &mesh.VBO);

    glBindVertexArray(mesh.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    // attrib 0 = position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // attrib 1 = normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // attrib 2 = uv
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    std::cout << "[OBJ] Loaded: " << filepath << " | Vertices: " << mesh.vertexCount << "\n";
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
