#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define STB_IMAGE_IMPLEMENTATION
#include "tiny_gltf.h"
#include "glb_loader.h"
#include "../common.h"
#include <iostream>
#include <glm/gtc/type_ptr.hpp>

static void compute_node_transform(const tinygltf::Model& model, int node_idx, glm::mat4& out) {
    const tinygltf::Node& node = model.nodes[node_idx];
    if (node.matrix.size() == 16) {
        out = glm::make_mat4(node.matrix.data());
    } else {
        glm::vec3 translation(0.0f);
        glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 scale(1.0f);
        if (node.translation.size() == 3)
            translation = glm::vec3((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]);
        if (node.rotation.size() == 4)
            rotation = glm::quat((float)node.rotation[3], (float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2]);
        if (node.scale.size() == 3)
            scale = glm::vec3((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]);
        glm::mat4 T = glm::translate(glm::mat4(1.0f), translation);
        glm::mat4 R = glm::mat4_cast(rotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
        out = T * R * S;
    }
}

static void traverse_nodes(const tinygltf::Model& model, int node_idx, const glm::mat4& parent_transform, glb_model_s& out_model) {
    const tinygltf::Node& node = model.nodes[node_idx];
    glm::mat4 local;
    compute_node_transform(model, node_idx, local);
    glm::mat4 world = parent_transform * local;

    if (node.mesh >= 0 && node.mesh < (int)model.meshes.size()) {
        const tinygltf::Mesh& mesh = model.meshes[node.mesh];
        for (size_t pi = 0; pi < mesh.primitives.size(); pi++) {
            const tinygltf::Primitive& prim = mesh.primitives[pi];

            glb_primitive_s gp;
            gp.local_transform = world;

            // --- Extract positions ---
            std::vector<float> positions;
            if (prim.attributes.count("POSITION")) {
                int acc_idx = prim.attributes.at("POSITION");
                const tinygltf::Accessor& acc = model.accessors[acc_idx];
                const tinygltf::BufferView& bv = model.bufferViews[acc.bufferView];
                const unsigned char* buf = model.buffers[bv.buffer].data.data() + bv.byteOffset + acc.byteOffset;
                size_t stride = bv.byteStride ? bv.byteStride : tinygltf::GetComponentSizeInBytes(acc.componentType) * tinygltf::GetNumComponentsInType(acc.type);
                positions.resize(acc.count * 3);
                for (size_t i = 0; i < acc.count; i++) {
                    const float* src = reinterpret_cast<const float*>(buf + i * stride);
                    positions[i * 3 + 0] = src[0];
                    positions[i * 3 + 1] = src[1];
                    positions[i * 3 + 2] = src[2];
                }
            }

            // --- Extract normals ---
            std::vector<float> normals;
            if (prim.attributes.count("NORMAL")) {
                int acc_idx = prim.attributes.at("NORMAL");
                const tinygltf::Accessor& acc = model.accessors[acc_idx];
                const tinygltf::BufferView& bv = model.bufferViews[acc.bufferView];
                const unsigned char* buf = model.buffers[bv.buffer].data.data() + bv.byteOffset + acc.byteOffset;
                size_t stride = bv.byteStride ? bv.byteStride : tinygltf::GetComponentSizeInBytes(acc.componentType) * tinygltf::GetNumComponentsInType(acc.type);
                normals.resize(acc.count * 3);
                for (size_t i = 0; i < acc.count; i++) {
                    const float* src = reinterpret_cast<const float*>(buf + i * stride);
                    normals[i * 3 + 0] = src[0];
                    normals[i * 3 + 1] = src[1];
                    normals[i * 3 + 2] = src[2];
                }
            }

            // --- Extract UVs ---
            std::vector<float> uvs;
            if (prim.attributes.count("TEXCOORD_0")) {
                int acc_idx = prim.attributes.at("TEXCOORD_0");
                const tinygltf::Accessor& acc = model.accessors[acc_idx];
                const tinygltf::BufferView& bv = model.bufferViews[acc.bufferView];
                const unsigned char* buf = model.buffers[bv.buffer].data.data() + bv.byteOffset + acc.byteOffset;
                size_t stride = bv.byteStride ? bv.byteStride : tinygltf::GetComponentSizeInBytes(acc.componentType) * tinygltf::GetNumComponentsInType(acc.type);
                uvs.resize(acc.count * 2);
                for (size_t i = 0; i < acc.count; i++) {
                    const float* src = reinterpret_cast<const float*>(buf + i * stride);
                    uvs[i * 2 + 0] = src[0];
                    uvs[i * 2 + 1] = src[1];
                }
            }

            // --- Extract indices ---
            std::vector<unsigned int> indices;
            if (prim.indices >= 0) {
                const tinygltf::Accessor& acc = model.accessors[prim.indices];
                const tinygltf::BufferView& bv = model.bufferViews[acc.bufferView];
                const unsigned char* buf = model.buffers[bv.buffer].data.data() + bv.byteOffset + acc.byteOffset;
                size_t stride = bv.byteStride ? bv.byteStride : tinygltf::GetComponentSizeInBytes(acc.componentType);
                indices.resize(acc.count);
                for (size_t i = 0; i < acc.count; i++) {
                    const unsigned char* src = buf + i * stride;
                    switch (acc.componentType) {
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  indices[i] = (unsigned int)src[0]; break;
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: indices[i] = (unsigned int)(*reinterpret_cast<const uint16_t*>(src)); break;
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:   indices[i] = *reinterpret_cast<const unsigned int*>(src); break;
                        default: break;
                    }
                }
            }

            // --- Load texture ---
            gp.texture_id = 0;
            if (prim.material >= 0 && prim.material < (int)model.materials.size()) {
                const tinygltf::Material& mat = model.materials[prim.material];
                if (mat.pbrMetallicRoughness.baseColorTexture.index >= 0) {
                    int tex_idx = mat.pbrMetallicRoughness.baseColorTexture.index;
                    if (tex_idx < (int)model.textures.size()) {
                        int img_idx = model.textures[tex_idx].source;
                        if (img_idx >= 0 && img_idx < (int)model.images.size()) {
                            const tinygltf::Image& img = model.images[img_idx];
                            if (!img.image.empty()) {
                                glGenTextures(1, &gp.texture_id);
                                glBindTexture(GL_TEXTURE_2D, gp.texture_id);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                                GLenum internal_fmt = (img.component == 4) ? GL_RGBA8 : GL_RGB8;
                                GLenum pixel_fmt    = (img.component == 4) ? GL_RGBA  : GL_RGB;
                                glTexImage2D(GL_TEXTURE_2D, 0, internal_fmt, img.width, img.height, 0,
                                    pixel_fmt, GL_UNSIGNED_BYTE, img.image.data());
                                glGenerateMipmap(GL_TEXTURE_2D);
                            }
                        }
                    }
                }
            }

            // --- Fill defaults for missing attributes ---
            int vertex_count = (int)positions.size() / 3;
            if (normals.empty()) {
                normals.resize(vertex_count * 3, 0.0f);
                for (int i = 0; i < vertex_count; i++) normals[i * 3 + 1] = 1.0f;
            }
            if (uvs.empty()) {
                uvs.resize(vertex_count * 2, 0.0f);
            }
            if (indices.empty()) {
                indices.resize(vertex_count);
                for (int i = 0; i < vertex_count; i++) indices[i] = (unsigned int)i;
            }

            gp.index_count = (int)indices.size();

            // --- Create GPU buffers ---
            glGenVertexArrays(1, &gp.VAO);
            glGenBuffers(1, &gp.VBO);
            glGenBuffers(1, &gp.EBO);

            glBindVertexArray(gp.VAO);

            // Positions VBO (attrib 0)
            GLuint pos_vbo;
            glGenBuffers(1, &pos_vbo);
            glBindBuffer(GL_ARRAY_BUFFER, pos_vbo);
            glBufferData(GL_ARRAY_BUFFER, positions.size() * sizeof(float), positions.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
            glEnableVertexAttribArray(0);

            // Normals VBO (attrib 1)
            GLuint norm_vbo;
            glGenBuffers(1, &norm_vbo);
            glBindBuffer(GL_ARRAY_BUFFER, norm_vbo);
            glBufferData(GL_ARRAY_BUFFER, normals.size() * sizeof(float), normals.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
            glEnableVertexAttribArray(1);

            // UVs VBO (attrib 2)
            GLuint uv_vbo;
            glGenBuffers(1, &uv_vbo);
            glBindBuffer(GL_ARRAY_BUFFER, uv_vbo);
            glBufferData(GL_ARRAY_BUFFER, uvs.size() * sizeof(float), uvs.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
            glEnableVertexAttribArray(2);

            // EBO
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gp.EBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

            glBindVertexArray(0);

            gp.VBO = pos_vbo;
            gp.extra_vbos.push_back(norm_vbo);
            gp.extra_vbos.push_back(uv_vbo);

            out_model.primitives.push_back(gp);
        }
    }

    for (int child : node.children) {
        traverse_nodes(model, child, world, out_model);
    }
}

glb_model_s GLB_Load(const char* path) {
    glb_model_s model;

    tinygltf::TinyGLTF loader;
    tinygltf::Model gltf_model;
    std::string err, warn;

    bool ok = loader.LoadBinaryFromFile(&gltf_model, &err, &warn, path);

    if (!warn.empty()) {
        std::cerr << "[GLB WARN] " << warn << "\n";
    }
    if (!err.empty()) {
        std::cerr << "[GLB ERR]  " << err << "\n";
    }
    if (!ok) {
        throw std::runtime_error("Failed to load GLB: " + std::string(path));
    }

    // Find root nodes (nodes not referenced as children by any other node)
    std::vector<bool> is_child(gltf_model.nodes.size(), false);
    for (const auto& node : gltf_model.nodes) {
        for (int child : node.children) {
            if (child >= 0 && child < (int)is_child.size())
                is_child[child] = true;
        }
    }

    glm::mat4 identity(1.0f);
    for (size_t i = 0; i < gltf_model.nodes.size(); i++) {
        if (!is_child[i]) {
            traverse_nodes(gltf_model, (int)i, identity, model);
        }
    }

    // If no root nodes found (single node scene), traverse all
    if (model.primitives.empty()) {
        for (size_t i = 0; i < gltf_model.nodes.size(); i++) {
            traverse_nodes(gltf_model, (int)i, identity, model);
        }
    }

    std::cout << "[GLB] Loaded: " << path << " | Primitives: " << model.primitives.size() << "\n";
    return model;
}

void GLB_Free(glb_model_s& model) {
    for (auto& prim : model.primitives) {
        if (prim.texture_id) {
            glDeleteTextures(1, &prim.texture_id);
            prim.texture_id = 0;
        }
        if (prim.VAO) {
            glDeleteVertexArrays(1, &prim.VAO);
            prim.VAO = 0;
        }
        if (prim.VBO) {
            glDeleteBuffers(1, &prim.VBO);
            prim.VBO = 0;
        }
        if (prim.EBO) {
            glDeleteBuffers(1, &prim.EBO);
            prim.EBO = 0;
        }
        for (auto& vbo : prim.extra_vbos) {
            if (vbo) {
                glDeleteBuffers(1, &vbo);
            }
        }
        prim.extra_vbos.clear();
    }
    model.primitives.clear();
}
