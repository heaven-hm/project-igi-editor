/******************************************************************************
 * @file    renderer_objects_visual.cpp
 * @brief   Renderer_Objects: spheres, text/cube meshes, selection box, model preview
 *          Split from renderer_objects.cpp; shares renderer_objects_internal.h.
 *****************************************************************************/
#include "renderer_objects_internal.h"

void Renderer_Objects::InitSphereMesh() {
    const int LAT = 12;
    const int LON = 24;
    std::vector<float> verts;
    verts.reserve(LAT * LON * 6 * 8); // 2 triangles per quad, 3 verts each, 8 floats

    auto addVert = [&](float x, float y, float z) {
        verts.push_back(x); verts.push_back(y); verts.push_back(z);
        verts.push_back(x); verts.push_back(y); verts.push_back(z); // normal = position for unit sphere
        verts.push_back(0.0f); verts.push_back(0.0f);
    };

    for (int i = 0; i < LAT; ++i) {
        float lat0 = glm::pi<float>() * (-0.5f + (float)i / LAT);
        float lat1 = glm::pi<float>() * (-0.5f + (float)(i + 1) / LAT);
        float sin0 = std::sin(lat0), cos0 = std::cos(lat0);
        float sin1 = std::sin(lat1), cos1 = std::cos(lat1);

        for (int j = 0; j < LON; ++j) {
            float lon0 = 2.0f * glm::pi<float>() * (float)j / LON;
            float lon1 = 2.0f * glm::pi<float>() * (float)(j + 1) / LON;
            float clon0 = std::cos(lon0), slon0 = std::sin(lon0);
            float clon1 = std::cos(lon1), slon1 = std::sin(lon1);

            float x00 = cos0*clon0, y00 = cos0*slon0, z00 = sin0;
            float x01 = cos0*clon1, y01 = cos0*slon1, z01 = sin0;
            float x10 = cos1*clon0, y10 = cos1*slon0, z10 = sin1;
            float x11 = cos1*clon1, y11 = cos1*slon1, z11 = sin1;

            addVert(x00, y00, z00); addVert(x10, y10, z10); addVert(x11, y11, z11);
            addVert(x00, y00, z00); addVert(x11, y11, z11); addVert(x01, y01, z01);
        }
    }

    sphere_vertex_count_ = (int)(verts.size() / 8);
    glGenVertexArrays(1, &sphere_vao_);
    glGenBuffers(1, &sphere_vbo_);
    glBindVertexArray(sphere_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, sphere_vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}

// ─── DrawMagicObjSpheres ──────────────────────────────────────────────────────
// Draws a solid red sphere at each MagicObject ATTA attachment position.
void Renderer_Objects::DrawMagicObjSpheres(const std::vector<LevelObject>& objects, GLuint ubo_mats) {
    EnsureMagicObjIdsLoaded();
    if (magicobj_ids_.empty()) return;

    if (sphere_vao_ == 0) InitSphereMesh();
    if (sphere_vao_ == 0) return;

    glUseProgram(shader_program_);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    GLint loc_model     = glGetUniformLocation(shader_program_, "u_model");
    GLint loc_dirlight  = glGetUniformLocation(shader_program_, "u_dirlight");
    GLint loc_ambient   = glGetUniformLocation(shader_program_, "u_ambient");
    GLint loc_useTex    = glGetUniformLocation(shader_program_, "u_useTexture");
    GLint loc_alpha     = glGetUniformLocation(shader_program_, "u_alpha");
    GLint loc_baseColor = glGetUniformLocation(shader_program_, "u_baseColor");

    glUniform1i(loc_useTex, 0);
    glUniform1f(loc_alpha, 1.0f);
    glUniform3f(loc_dirlight, 0.5f, 0.5f, 0.5f);
    glUniform3f(loc_ambient,  0.5f, 0.5f, 0.5f);
    glUniform4f(loc_baseColor, 1.0f, 0.0f, 0.0f, 1.0f); // red

    // Sphere radius = 0.3 meters in world-unit space
    const float SPHERE_SCALE = WORLD_UNITS_PER_METER * 0.3f;

    auto drawSphere = [&](const glm::vec3& worldPos) {
        glm::mat4 sphereMat = glm::translate(glm::mat4(1.0f), worldPos);
        sphereMat = glm::scale(sphereMat, glm::vec3(SPHERE_SCALE));
        glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(sphereMat));
        glBindVertexArray(sphere_vao_);
        glDrawArrays(GL_TRIANGLES, 0, sphere_vertex_count_);
    };

    for (const auto& obj : objects) {
        if (obj.deleted || obj.modelId.empty()) continue;

        glm::mat4 parentRot(1.0f);
        parentRot = glm::rotate(parentRot, (float)obj.rot.z, glm::vec3(0, 0, 1));
        parentRot = glm::rotate(parentRot, (float)obj.rot.x, glm::vec3(1, 0, 0));
        parentRot = glm::rotate(parentRot, (float)obj.rot.y, glm::vec3(0, 1, 0));

        glm::mat4 rootWorldMat(1.0f);
        rootWorldMat = glm::translate(rootWorldMat, glm::vec3((float)obj.pos.x, (float)obj.pos.y, (float)obj.pos.z));
        rootWorldMat = rootWorldMat * parentRot;

        // ── ATTA magic objects ───────────────────────────────────────────────
        std::string prefix = obj.isBuilding ? "building:" : "object:";
        std::string attKey = std::to_string(current_level_) + ":" + prefix + obj.modelId;
        auto ait = attachment_cache_.find(attKey);
        if (ait != attachment_cache_.end()) {
            for (const auto& att : ait->second) {
                if (!magicobj_ids_.count(att.modelId)) continue;
                glm::vec3 localOff(att.px, att.py, att.pz);
                glm::vec3 worldPos = glm::vec3(rootWorldMat * glm::vec4(localOff, 1.0f));
                drawSphere(worldPos);
            }
        }

        // ── XTVM magic vertices ──────────────────────────────────────────────
        // magicVertices are stored in mesh-local space (divided by 40.96).
        // Reconstruct world position using the full scaled model matrix.
        std::string meshKey = std::to_string(current_level_) + ":" + prefix + obj.modelId;
        auto mit = mesh_cache_.find(meshKey);
        if (mit != mesh_cache_.end()) {
            const Mesh& mesh = mit->second;
            if (!mesh.magicVertices.empty()) {
                glm::mat4 modelMat = rootWorldMat * glm::scale(glm::mat4(1.0f), glm::vec3(40.96f));
                for (const auto& mv : mesh.magicVertices) {
                    // skip null entries (all-zero position)
                    if (mv.x == 0.0f && mv.y == 0.0f && mv.z == 0.0f) continue;
                    glm::vec3 worldPos = glm::vec3(modelMat * glm::vec4(mv, 1.0f));
                    drawSphere(worldPos);
                }
            }
        }
    }

    glBindVertexArray(0);
    glUseProgram(0);
}


// METAL_DOOR_SLIDE_UP (model 506_xx) appears in levels 12/13/14 as an EditRigidObj
// carrying a genuine multi-axis Euler tuple. It bypasses the engine's special door
// transform and so needs a different Euler application order than other rigid objects.
void Renderer_Objects::DrawModelPreview(const std::string& modelId, GLuint ubo_mats,
                                        int vpX, int vpY, int vpW, int vpH,
                                        float rotX, float rotY) {
    if (!shader_program_ || modelId.empty() || vpW <= 0 || vpH <= 0) return;

    // Picker model IDs are almost all props; fall back to the building variant.
    Mesh mesh = GetOrLoadMesh(modelId, false);
    if (mesh.subMeshes.empty() && mesh.vertexCount == 0)
        mesh = GetOrLoadMesh(modelId, true);
    if (mesh.subMeshes.empty() && (mesh.VAO == 0 || mesh.vertexCount == 0)) return;

    // Fit the model into a unit-ish sphere so any model frames the same.
    float maxExt = std::max(std::max(mesh.halfExtents.x, mesh.halfExtents.y),
                            std::max(mesh.halfExtents.z, 1.0f));
    float fit = 1.0f / maxExt;

    // Preview camera: IGI models are Z-up. View from -Y, slightly raised, at origin.
    glm::mat4 proj = glm::perspective(glm::radians(40.0f),
                                      (float)vpW / (float)vpH, 0.05f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, -3.2f, 1.2f),
                                 glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 mvp = proj * view;

    glm::mat4 model(1.0f);
    model = glm::rotate(model, rotY, glm::vec3(0.0f, 0.0f, 1.0f)); // horizontal spin (Z up)
    model = glm::rotate(model, rotX, glm::vec3(1.0f, 0.0f, 0.0f)); // vertical tumble
    model = glm::scale(model, glm::vec3(fit));
    model = glm::translate(model, -mesh.center);

    // Overwrite u_mvp (3rd mat4 in the shared UBO). The scene re-uploads the whole
    // UBO next frame, and the scene pass for this frame already finished, so this is safe.
    glBindBuffer(GL_UNIFORM_BUFFER, ubo_mats);
    glBufferSubData(GL_UNIFORM_BUFFER, 2 * sizeof(glm::mat4), sizeof(glm::mat4),
                    glm::value_ptr(mvp));

    // Isolated viewport with its own cleared depth so the preview never z-fights HUD.
    glViewport(vpX, vpY, vpW, vpH);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vpX, vpY, vpW, vpH);
    glClear(GL_DEPTH_BUFFER_BIT);

    glUseProgram(shader_program_);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    GLint loc_model     = glGetUniformLocation(shader_program_, "u_model");
    GLint loc_dirlight  = glGetUniformLocation(shader_program_, "u_dirlight");
    GLint loc_ambient   = glGetUniformLocation(shader_program_, "u_ambient");
    GLint loc_useTex    = glGetUniformLocation(shader_program_, "u_useTexture");
    GLint loc_tex       = glGetUniformLocation(shader_program_, "u_texture");
    GLint loc_alpha     = glGetUniformLocation(shader_program_, "u_alpha");
    GLint loc_glass     = glGetUniformLocation(shader_program_, "u_glassMin");
    GLint loc_baseColor = glGetUniformLocation(shader_program_, "u_baseColor");
    glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(model));
    glUniform1f(loc_alpha, 1.0f);
    if (loc_glass >= 0)     glUniform1f(loc_glass, 0.0f);
    if (loc_baseColor >= 0) glUniform4f(loc_baseColor, 1.0f, 1.0f, 1.0f, 1.0f);

    // Hash-based fallback color for untextured submeshes (matches the main draw).
    float r = 0.6f, g = 0.6f, b = 0.6f;
    {
        size_t h = std::hash<std::string>{}(modelId);
        r = 0.4f + (float)(h & 0xFF) / 255.0f * 0.4f;
        g = 0.4f + (float)((h >> 8) & 0xFF) / 255.0f * 0.4f;
        b = 0.4f + (float)((h >> 16) & 0xFF) / 255.0f * 0.4f;
    }

    auto drawSub = [&](GLuint vao, int vcount, GLuint texId, const glm::vec4& baseColor) {
        if (vao == 0 || vcount == 0) return;
        if (texId > 0) {
            glUniform3f(loc_dirlight, 0.7f, 0.7f, 0.7f);
            glUniform3f(loc_ambient,  0.45f, 0.45f, 0.45f);
            glUniform1i(loc_useTex, 1);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texId);
            glUniform1i(loc_tex, 0);
        } else {
            glm::vec3 c(baseColor.r, baseColor.g, baseColor.b);
            if (c.r >= 0.99f && c.g >= 0.99f && c.b >= 0.99f) c = glm::vec3(r, g, b);
            glUniform3f(loc_dirlight, c.r * 0.7f, c.g * 0.7f, c.b * 0.7f);
            glUniform3f(loc_ambient,  c.r * 0.45f, c.g * 0.45f, c.b * 0.45f);
            glUniform1i(loc_useTex, 0);
        }
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, vcount);
    };

    if (!mesh.subMeshes.empty()) {
        for (const auto& sub : mesh.subMeshes)
            drawSub(sub.VAO, sub.vertexCount, sub.textureID, sub.baseColorFactor);
    } else {
        drawSub(mesh.VAO, mesh.vertexCount, mesh.textureID, glm::vec4(1.0f));
    }

    glBindVertexArray(0);
    glDisable(GL_SCISSOR_TEST);
    // Caller restores full viewport and 2D HUD state.
}

// ─── LoadAttachmentsRecursive ────────────────────────────────────────────────
// Recursively scans the ATTA section of modelId's MEF file, caches each
// sub-model mesh AND its own attachment records, then recurses into children.
// The visited set prevents infinite loops on circular ATTA references.
Mesh Renderer_Objects::CreateTextMesh(const std::string& text) {
    Mesh m;
    std::vector<float> vertices;
    
    // Simple stroke font - each character defined as line segments
    // Scale factor for text - reduced to be less intrusive
    const float scale = 20.0f;
    const float charSpacing = 25.0f;
    const float startX = -((text.size() * charSpacing) / 2.0f);
    
    float cursorX = startX;
    
    for (char c : text) {
        AddCharacterVertices(vertices, c, cursorX, 0.0f, scale);
        cursorX += charSpacing;
    }
    
    if (vertices.empty()) {
        // Fallback to a simple cross if text generation fails
        vertices = {
            -50.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             50.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             0.0f, -50.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
             0.0f,  50.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f
        };
    }
    
    glGenVertexArrays(1, &m.VAO);
    glGenBuffers(1, &m.VBO);
    glBindVertexArray(m.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    
    // Position (xyz)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Normal (xyz) - use as color multiplier
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)12);
    glEnableVertexAttribArray(1);
    // TexCoord (uv) - unused
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)24);
    glEnableVertexAttribArray(2);
    
    glBindVertexArray(0);
    m.vertexCount = vertices.size() / 8;
    m.textureID = 0;
    m.vertexData = nullptr;
    return m;
}

void Renderer_Objects::AddCharacterVertices(std::vector<float>& vertices, char c, float x, float y, float scale) {
    // Simple stroke definitions for common characters - using triangle strips for each line
    // Each line segment becomes a thin quad (2 triangles)
    auto addLineQuad = [&](float x1, float y1, float x2, float y2) {
        float thickness = 0.1f;
        float nx = 1.0f, ny = 0.0f, nz = 0.0f; // Red color
        
        // Calculate perpendicular offset for thickness
        float dx = x2 - x1;
        float dy = y2 - y1;
        float len = sqrt(dx*dx + dy*dy);
        if (len < 0.0001f) return;
        float ox = -dy / len * thickness;
        float oy = dx / len * thickness;
        
        // Quad vertices (2 triangles)
        // Triangle 1
        vertices.push_back(x + x1 * scale + ox); vertices.push_back(y + y1 * scale + oy); vertices.push_back(0.0f);
        vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
        vertices.push_back(0.0f); vertices.push_back(0.0f);
        
        vertices.push_back(x + x2 * scale + ox); vertices.push_back(y + y2 * scale + oy); vertices.push_back(0.0f);
        vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
        vertices.push_back(0.0f); vertices.push_back(0.0f);
        
        vertices.push_back(x + x1 * scale - ox); vertices.push_back(y + y1 * scale - oy); vertices.push_back(0.0f);
        vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
        vertices.push_back(0.0f); vertices.push_back(0.0f);
        
        // Triangle 2
        vertices.push_back(x + x1 * scale - ox); vertices.push_back(y + y1 * scale - oy); vertices.push_back(0.0f);
        vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
        vertices.push_back(0.0f); vertices.push_back(0.0f);
        
        vertices.push_back(x + x2 * scale + ox); vertices.push_back(y + y2 * scale + oy); vertices.push_back(0.0f);
        vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
        vertices.push_back(0.0f); vertices.push_back(0.0f);
        
        vertices.push_back(x + x2 * scale - ox); vertices.push_back(y + y2 * scale - oy); vertices.push_back(0.0f);
        vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
        vertices.push_back(0.0f); vertices.push_back(0.0f);
    };
    
    c = toupper(c);
    
    switch (c) {
        case 'M':
            addLineQuad(0, 1, 0, -1);
            addLineQuad(0, -1, 0.5, 0);
            addLineQuad(0.5, 0, 1, -1);
            addLineQuad(1, -1, 1, 1);
            break;
        case 'I':
            addLineQuad(0, 1, 0, -1);
            addLineQuad(-0.3, 1, 0.3, 1);
            addLineQuad(-0.3, -1, 0.3, -1);
            break;
        case 'S':
            addLineQuad(0.3, 1, 0, 1);
            addLineQuad(0, 1, 0, 0);
            addLineQuad(0, 0, 1, 0);
            addLineQuad(1, 0, 1, -1);
            addLineQuad(1, -1, 0.7, -1);
            break;
        case 'N':
            addLineQuad(0, -1, 0, 1);
            addLineQuad(0, 1, 1, -1);
            addLineQuad(1, -1, 1, 1);
            break;
        case 'G':
            addLineQuad(0.7, 1, 0, 1);
            addLineQuad(0, 1, 0, -1);
            addLineQuad(0, -1, 0.7, -1);
            addLineQuad(0.7, -1, 1, -1);
            addLineQuad(1, -1, 1, 0);
            addLineQuad(1, 0, 0.7, 0);
            break;
        case ':':
            addLineQuad(0, 0.3, 0, 0.3);
            addLineQuad(0, -0.3, 0, -0.3);
            break;
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            // Simple box for numbers with slash
            addLineQuad(0, 1, 0, -1);
            addLineQuad(0, -1, 1, -1);
            addLineQuad(1, -1, 1, 1);
            addLineQuad(1, 1, 0, 1);
            addLineQuad(0.2, -0.8, 0.8, -0.2);
            break;
        case '_':
            addLineQuad(0, -1, 1, -1);
            break;
        default:
            // Default to a box for unknown characters
            addLineQuad(0, 1, 0, -1);
            addLineQuad(0, -1, 1, -1);
            addLineQuad(1, -1, 1, 1);
            addLineQuad(1, 1, 0, 1);
            break;
    }
}

Mesh Renderer_Objects::CreateCubeMesh() {
    Mesh m;
    std::vector<float> v = {
        -500.0f,-500.0f,-500.0f, 0,0,-1, 0,0,  500.0f, 500.0f,-500.0f, 0,0,-1, 1,1,  500.0f,-500.0f,-500.0f, 0,0,-1, 1,0,
        -500.0f,-500.0f,-500.0f, 0,0,-1, 0,0, -500.0f, 500.0f,-500.0f, 0,0,-1, 0,1,  500.0f, 500.0f,-500.0f, 0,0,-1, 1,1,
        -500.0f,-500.0f, 500.0f, 0,0, 1, 0,0,  500.0f,-500.0f, 500.0f, 0,0, 1, 1,0,  500.0f, 500.0f, 500.0f, 0,0, 1, 1,1,
        -500.0f,-500.0f, 500.0f, 0,0, 1, 0,0,  500.0f, 500.0f, 500.0f, 0,0, 1, 1,1, -500.0f, 500.0f, 500.0f, 0,0, 1, 0,1,
        -500.0f, 500.0f, 500.0f,-1,0, 0, 1,0, -500.0f, 500.0f,-500.0f,-1,0, 0, 1,1, -500.0f,-500.0f,-500.0f,-1,0, 0, 0,1,
        -500.0f, 500.0f, 500.0f,-1,0, 0, 1,0, -500.0f,-500.0f,-500.0f,-1,0, 0, 0,1, -500.0f,-500.0f, 500.0f,-1,0, 0, 0,0,
         500.0f, 500.0f, 500.0f, 1,0, 0, 1,0,  500.0f,-500.0f,-500.0f, 1,0, 0, 0,1,  500.0f, 500.0f,-500.0f, 1,0, 0, 1,1,
         500.0f, 500.0f, 500.0f, 1,0, 0, 1,0,  500.0f,-500.0f, 500.0f, 1,0, 0, 0,0,  500.0f,-500.0f,-500.0f, 1,0, 0, 0,1,
        -500.0f,-500.0f,-500.0f, 0,-1,0, 0,1,  500.0f,-500.0f,-500.0f, 0,-1,0, 1,1,  500.0f,-500.0f, 500.0f, 0,-1,0, 1,0,
        -500.0f,-500.0f,-500.0f, 0,-1,0, 0,1,  500.0f,-500.0f, 500.0f, 0,-1,0, 1,0, -500.0f,-500.0f, 500.0f, 0,-1,0, 0,0,
        -500.0f, 500.0f,-500.0f, 0, 1,0, 0,1,  500.0f, 500.0f, 500.0f, 0, 1,0, 1,0,  500.0f, 500.0f,-500.0f, 0, 1,0, 1,1,
        -500.0f, 500.0f,-500.0f, 0, 1,0, 0,1, -500.0f, 500.0f, 500.0f, 0, 1,0, 0,0,  500.0f, 500.0f, 500.0f, 0, 1,0, 1,0
    };
    glGenVertexArrays(1, &m.VAO); glGenBuffers(1, &m.VBO);
    glBindVertexArray(m.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
    glBufferData(GL_ARRAY_BUFFER, v.size()*4, v.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)24); glEnableVertexAttribArray(2);
    m.vertexCount = 36; m.textureID = 0; m.vertexData = nullptr;
    glBindVertexArray(0); return m;
}

void Renderer_Objects::InitSelectionBox() {
    // Create a simple cube wireframe
    float boxSize = WORLD_UNITS_PER_METER * 2.0f; // 2m box
    float halfSize = boxSize * 0.5f;
    
    float vertices[] = {
        // Front face
        -halfSize, -halfSize,  halfSize,
         halfSize, -halfSize,  halfSize,
         halfSize,  halfSize,  halfSize,
        -halfSize,  halfSize,  halfSize,
        // Back face
        -halfSize, -halfSize, -halfSize,
         halfSize, -halfSize, -halfSize,
         halfSize,  halfSize, -halfSize,
        -halfSize,  halfSize, -halfSize
    };
    
    glGenVertexArrays(1, &selection_vao_);
    glGenBuffers(1, &selection_vbo_);
    
    glBindVertexArray(selection_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, selection_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    
    glBindVertexArray(0);
}

// ─── DrawSelectionBox ─────────────────────────────────────────────────────────
void Renderer_Objects::DrawSelectionBox(const LevelObject& obj, GLuint ubo_mats, const glm::vec4& color) {
    if (selection_vao_ == 0) {
        InitSelectionBox();
    }
    
    // Simple shader for solid color
    static const char* simple_vert = R"(
#version 330 core
layout(std140) uniform Matrices {
    mat4 u_unused1;
    mat4 u_unused2;
    mat4 u_mvp;
};
uniform mat4 u_model;
layout(location = 0) in vec3 a_pos;
void main() {
    gl_Position = u_mvp * u_model * vec4(a_pos, 1.0);
}
)";

    static const char* simple_frag = R"(
#version 330 core
uniform vec4 u_color;
out vec4 fragColor;
void main() {
    fragColor = u_color;
}
)";
    
    static GLuint simple_shader = 0;
    if (simple_shader == 0) {
        GLuint vert = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vert, 1, &simple_vert, nullptr);
        glCompileShader(vert);
        
        GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(frag, 1, &simple_frag, nullptr);
        glCompileShader(frag);
        
        simple_shader = glCreateProgram();
        glAttachShader(simple_shader, vert);
        glAttachShader(simple_shader, frag);
        glLinkProgram(simple_shader);
        
        glDeleteShader(vert);
        glDeleteShader(frag);
    }
    
    glUseProgram(simple_shader);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);
    
    // Build model matrix for selection box (slightly larger than object)
    // Cast dvec3 to vec3 for rendering
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(obj.pos));
    model = glm::rotate(model, static_cast<float>(obj.rot.z), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::rotate(model, static_cast<float>(obj.rot.x), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, static_cast<float>(obj.rot.y), glm::vec3(0.0f, 1.0f, 0.0f));

    // Weapon/ammo pickups are authored with barrel along model +Y (standing upright).
    // Rotate 90° around world Z so barrel maps to world +X — weapon lies flat on ground.
    bool isWeapon = IsWeaponModel(obj.modelId) || obj.type == "GunPickup" || obj.type == "AmmoPickup" || obj.type == "GenericPickup";
    if (isWeapon) {
        model = glm::rotate(model, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    }

    model = glm::scale(model, glm::vec3(obj.scale * 1.2f)); // 20% larger
    
    GLint loc_model = glGetUniformLocation(simple_shader, "u_model");
    glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(model));
    
    GLint loc_color = glGetUniformLocation(simple_shader, "u_color");
    glUniform4fv(loc_color, 1, glm::value_ptr(color));
    
    // Draw wireframe
    glBindVertexArray(selection_vao_);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glDisable(GL_CULL_FACE);
    
    // Draw edges
    GLuint indices[] = {
        0,1, 1,2, 2,3, 3,0, // Front face
        4,5, 5,6, 6,7, 7,4, // Back face
        0,4, 1,5, 2,6, 3,7  // Connecting edges
    };
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); // Unbind any existing EBO
    glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, indices);
    
    // Reset state
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
    glUseProgram(0);
}

// ─── DrawAttachmentsForSpline ─────────────────────────────────────────────────
// Public entry point called by Renderer_Splines once per SEGMENT INTERVAL.
// unscaledWorldMat = translate + rotate at the segment midpoint (no scale).
// leafScale.x = intervalLen (stretch ATTA to cover the full segment in X).
// leafScale.y/z = 40.96 (normal world scale for width/height).

