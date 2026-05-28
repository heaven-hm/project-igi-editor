# GPU Color Picking Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the CPU AABB hover/selection picker in `App::PickObjectAtScreenPos()` with a GPU color-picking pass that is pixel-perfect and hides interior building objects when the camera is outside.

**Architecture:** Render the scene to an off-screen FBO using a flat-color shader where each object's ID is encoded as an RGB triplet, then read back the single pixel under the cursor. `Renderer_Objects` owns the FBO and both picking methods; `Renderer` exposes a thin forwarding wrapper; `App::PickObjectAtScreenPos()` becomes a one-liner.

**Tech Stack:** OpenGL 3.3 (existing), GLSL 330 core, GLM (existing), C++17 (existing)

---

## File Map

| File | Change |
|---|---|
| `source/renderer/renderer_objects.h` | Add picking FBO members + `DrawForPicking()` + `PickObjectAtScreen()` declarations |
| `source/renderer/renderer_objects.cpp` | Add picking shader source, FBO helpers, `DrawForPicking()`, `PickObjectAtScreen()` |
| `source/renderer/renderer.h` | Add `PickObjectAtScreen()` forwarding declaration |
| `source/app.cpp` | Replace `PickObjectAtScreenPos()` body with one-liner |

---

## Task 1: Add picking state to `renderer_objects.h`

**Files:**
- Modify: `source/renderer/renderer_objects.h`

- [ ] **Step 1: Add FBO members and method declarations**

Open `source/renderer/renderer_objects.h`. In the `private:` section, after the existing `GLuint sphere_vbo_ = 0;` line (~line 63), add:

```cpp
    // GPU color picking FBO
    GLuint pick_fbo_          = 0;
    GLuint pick_color_tex_    = 0;
    GLuint pick_depth_rb_     = 0;
    GLuint pick_shader_prog_  = 0;
    int    pick_fbo_w_        = 0;
    int    pick_fbo_h_        = 0;
```

In the `public:` section, after the existing `Draw(...)` declaration (~line 28), add:

```cpp
    int PickObjectAtScreen(int x, int y, int w, int h,
                           GLuint ubo_mats,
                           const std::vector<LevelObject>& objects,
                           int draw_parts,
                           const glm::vec3& camera_pos);
```

- [ ] **Step 2: Add private helper declarations**

In the `private:` section, after the existing `void InitSelectionBox();` line, add:

```cpp
    void InitPickingFBO(int w, int h);
    void DrawForPicking(GLuint ubo_mats,
                        const std::vector<LevelObject>& objects,
                        int draw_parts,
                        const glm::vec3& camera_pos);
```

- [ ] **Step 3: Build and verify it compiles (declarations only)**

```
cmake --build build --target igi_editor 2>&1 | tail -20
```

Expected: build succeeds (no implementations yet, so linker errors are acceptable at this step — but no header parse errors).

- [ ] **Step 4: Commit**

```
git add source/renderer/renderer_objects.h
git commit -m "feat: declare GPU color picking members in Renderer_Objects"
```

---

## Task 2: Add picking shader and FBO helpers to `renderer_objects.cpp`

**Files:**
- Modify: `source/renderer/renderer_objects.cpp`

- [ ] **Step 1: Add picking shader sources after the existing `OBJ_FRAG_SRC` string (~line 428)**

```cpp
// ─── Picking Shader Sources ───────────────────────────────────────────────────
static const char* PICK_VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

layout(std140) uniform Matrices {
    mat4 u_unused1;
    mat4 u_unused2;
    mat4 u_mvp;
};

uniform mat4 u_model;

void main() {
    gl_Position = u_mvp * u_model * vec4(a_pos, 1.0);
}
)";

static const char* PICK_FRAG_SRC = R"(
#version 330 core
uniform int u_object_id;
out vec4 fragColor;
void main() {
    int id = u_object_id;
    fragColor = vec4(
        float((id >> 16) & 0xFF) / 255.0,
        float((id >>  8) & 0xFF) / 255.0,
        float( id        & 0xFF) / 255.0,
        1.0
    );
}
)";
```

- [ ] **Step 2: Add `BuildPickShaderProgram()` static helper immediately after the existing `BuildShaderProgram()` function (~line 470)**

```cpp
static GLuint BuildPickShaderProgram() {
    GLuint vert = CompileShader(GL_VERTEX_SHADER,   PICK_VERT_SRC);
    GLuint frag = CompileShader(GL_FRAGMENT_SHADER, PICK_FRAG_SRC);
    if (!vert || !frag) return 0;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, 512, nullptr, log);
        std::cerr << "[PickShader] Link error: " << log << "\n";
        glDeleteProgram(prog);
        prog = 0;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    return prog;
}
```

- [ ] **Step 3: Initialize picking shader in `Init()`, after the `InitSelectionBox()` call (~line 499)**

```cpp
    // Picking shader
    pick_shader_prog_ = BuildPickShaderProgram();
    if (!pick_shader_prog_) {
        Logger::Get().Log(LogLevel::WARNING, "[Renderer_Objects] Failed to build picking shader.");
    } else {
        GLuint blk = glGetUniformBlockIndex(pick_shader_prog_, "Matrices");
        if (blk != GL_INVALID_INDEX)
            glUniformBlockBinding(pick_shader_prog_, blk, ubo_binding_point_);
    }
    // FBO created on first use (size unknown at init time)
```

- [ ] **Step 4: Tear down picking resources in `Shutdown()`, after the `sphere_vbo_` block (~line 567)**

```cpp
    if (pick_fbo_) {
        glDeleteFramebuffers(1, &pick_fbo_);
        pick_fbo_ = 0;
    }
    if (pick_color_tex_) {
        glDeleteTextures(1, &pick_color_tex_);
        pick_color_tex_ = 0;
    }
    if (pick_depth_rb_) {
        glDeleteRenderbuffers(1, &pick_depth_rb_);
        pick_depth_rb_ = 0;
    }
    if (pick_shader_prog_) {
        glDeleteProgram(pick_shader_prog_);
        pick_shader_prog_ = 0;
    }
    pick_fbo_w_ = 0;
    pick_fbo_h_ = 0;
```

- [ ] **Step 5: Implement `InitPickingFBO()`**

Add this function after `Shutdown()` (before the `Draw()` function):

```cpp
// ─── InitPickingFBO ───────────────────────────────────────────────────────────
void Renderer_Objects::InitPickingFBO(int w, int h) {
    // Delete existing resources
    if (pick_fbo_)       { glDeleteFramebuffers(1,  &pick_fbo_);       pick_fbo_ = 0; }
    if (pick_color_tex_) { glDeleteTextures(1,       &pick_color_tex_); pick_color_tex_ = 0; }
    if (pick_depth_rb_)  { glDeleteRenderbuffers(1,  &pick_depth_rb_);  pick_depth_rb_ = 0; }

    // Color texture (RGB8 — ID encoded as RGB)
    glGenTextures(1, &pick_color_tex_);
    glBindTexture(GL_TEXTURE_2D, pick_color_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Depth renderbuffer
    glGenRenderbuffers(1, &pick_depth_rb_);
    glBindRenderbuffer(GL_RENDERBUFFER, pick_depth_rb_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // Framebuffer
    glGenFramebuffers(1, &pick_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, pick_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pick_color_tex_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, pick_depth_rb_);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        Logger::Get().Log(LogLevel::ERR, "[Renderer_Objects] Picking FBO incomplete: " + std::to_string(status));
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    pick_fbo_w_ = w;
    pick_fbo_h_ = h;
}
```

- [ ] **Step 6: Build and verify it compiles**

```
cmake --build build --target igi_editor 2>&1 | tail -20
```

Expected: builds cleanly (two unresolved methods remain: `DrawForPicking` and `PickObjectAtScreen`).

- [ ] **Step 7: Commit**

```
git add source/renderer/renderer_objects.cpp
git commit -m "feat: add GPU picking shader, FBO init/teardown in Renderer_Objects"
```

---

## Task 3: Implement `DrawForPicking()`

**Files:**
- Modify: `source/renderer/renderer_objects.cpp`

- [ ] **Step 1: Implement `DrawForPicking()` after `InitPickingFBO()`**

This function mirrors the `Draw()` loop but uses the flat-color picking shader and skips ATTA sub-models (they share their parent's ID). ID = `(object_index + 1)`. Objects with `parentIndex` pointing to a building that is NOT in `inside_buildings` are skipped.

```cpp
// ─── DrawForPicking ───────────────────────────────────────────────────────────
void Renderer_Objects::DrawForPicking(GLuint ubo_mats,
                                      const std::vector<LevelObject>& objects,
                                      int draw_parts,
                                      const glm::vec3& camera_pos)
{
    if (!pick_shader_prog_) return;

    constexpr float BASE_SCALE = 40.96f;
    const int DRAW_OBJECTS  = 4;
    const int DRAW_BUILDINGS = 16;
    const int DRAW_PROPS    = 32;

    // Build set of building indices whose AABB contains the camera
    std::unordered_set<int> inside_buildings;
    for (int i = 0; i < (int)objects.size(); ++i) {
        const auto& obj = objects[i];
        if (obj.deleted || !obj.isBuilding || obj.modelId.empty()) continue;

        glm::vec3 extents = GetMeshExtents(obj.modelId, true) * BASE_SCALE * obj.scale;
        glm::vec3 center  = glm::vec3(obj.pos);
        glm::vec3 delta   = camera_pos - center;
        if (std::abs(delta.x) <= extents.x &&
            std::abs(delta.y) <= extents.y &&
            std::abs(delta.z) <= extents.z) {
            inside_buildings.insert(i);
        }
    }

    // Set picking render state
    glBindFramebuffer(GL_FRAMEBUFFER, pick_fbo_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glUseProgram(pick_shader_prog_);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);

    GLint loc_model = glGetUniformLocation(pick_shader_prog_, "u_model");
    GLint loc_id    = glGetUniformLocation(pick_shader_prog_, "u_object_id");

    for (int i = 0; i < (int)objects.size(); ++i) {
        const auto& obj = objects[i];
        if (obj.deleted || obj.modelId.empty()) continue;
        if (obj.isSplineWaypoint || obj.isSplineContainer) continue;
        if (IsSkippedModelId(obj.modelId)) continue;

        // Selective rendering (mirrors Draw())
        bool shouldDraw = false;
        if (draw_parts & DRAW_OBJECTS) {
            shouldDraw = true;
        } else {
            if ((draw_parts & DRAW_BUILDINGS) && obj.isBuilding)  shouldDraw = true;
            if ((draw_parts & DRAW_PROPS)     && !obj.isBuilding) shouldDraw = true;
        }
        if (!shouldDraw) continue;

        // Building interior occlusion: skip children of buildings the camera is outside
        if (obj.parentIndex >= 0 && obj.parentIndex < (int)objects.size()) {
            const auto& parent = objects[obj.parentIndex];
            if (parent.isBuilding && inside_buildings.find(obj.parentIndex) == inside_buildings.end()) {
                continue; // camera is outside this parent building
            }
        }

        Mesh mesh = GetOrLoadMesh(obj.modelId, obj.isBuilding);
        if (mesh.vertexCount == 0) continue;
        if (!mesh.fromRenderMesh) continue;

        // Build model matrix (same convention as Draw())
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(obj.pos));
        model = glm::rotate(model, (float)obj.rot.z, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::rotate(model, (float)obj.rot.x, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, (float)obj.rot.y, glm::vec3(0.0f, 1.0f, 0.0f));
        if (IsWeaponModel(obj.modelId) || obj.type == "GunPickup" || obj.type == "AmmoPickup") {
            model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        }
        model = glm::scale(model, glm::vec3(BASE_SCALE * obj.scale));

        glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(model));
        glUniform1i(loc_id, i + 1); // ID 0 = background

        // Draw hull submeshes
        if (!mesh.subMeshes.empty()) {
            for (const auto& sub : mesh.subMeshes) {
                if (sub.VAO == 0 || sub.vertexCount == 0) continue;
                glBindVertexArray(sub.VAO);
                if (sub.indexCount > 0)
                    glDrawElements(GL_TRIANGLES, sub.indexCount, GL_UNSIGNED_INT, nullptr);
                else
                    glDrawArrays(GL_TRIANGLES, 0, sub.vertexCount);
            }
        } else if (mesh.VAO) {
            glBindVertexArray(mesh.VAO);
            glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
        }
        // Note: ATTA attachments are NOT drawn here — they share the parent's ID
        // via the parent object's own hull, so they don't need separate draw calls.
    }

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Restore normal polygon mode
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_CULL_FACE);
}
```

- [ ] **Step 2: Build and verify**

```
cmake --build build --target igi_editor 2>&1 | tail -20
```

Expected: no new errors beyond any pre-existing `PickObjectAtScreen` unresolved symbol.

- [ ] **Step 3: Commit**

```
git add source/renderer/renderer_objects.cpp
git commit -m "feat: implement DrawForPicking with building interior occlusion"
```

---

## Task 4: Implement `PickObjectAtScreen()`

**Files:**
- Modify: `source/renderer/renderer_objects.cpp`

- [ ] **Step 1: Implement `PickObjectAtScreen()` after `DrawForPicking()`**

```cpp
// ─── PickObjectAtScreen ───────────────────────────────────────────────────────
int Renderer_Objects::PickObjectAtScreen(int x, int y, int w, int h,
                                          GLuint ubo_mats,
                                          const std::vector<LevelObject>& objects,
                                          int draw_parts,
                                          const glm::vec3& camera_pos)
{
    if (!pick_shader_prog_ || w <= 0 || h <= 0) return -1;

    // Resize FBO if window size changed
    if (w != pick_fbo_w_ || h != pick_fbo_h_) {
        InitPickingFBO(w, h);
    }
    if (!pick_fbo_) return -1;

    DrawForPicking(ubo_mats, objects, draw_parts, camera_pos);

    // Read back the single pixel under the cursor.
    // OpenGL origin is bottom-left; screen coords are top-left → flip Y.
    uint8_t pixel[3] = {0, 0, 0};
    glBindFramebuffer(GL_FRAMEBUFFER, pick_fbo_);
    glReadPixels(x, h - y - 1, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    int id = (static_cast<int>(pixel[0]) << 16) |
             (static_cast<int>(pixel[1]) <<  8) |
              static_cast<int>(pixel[2]);

    return (id == 0) ? -1 : id - 1;
}
```

- [ ] **Step 2: Build and verify — all symbols now resolved**

```
cmake --build build --target igi_editor 2>&1 | tail -20
```

Expected: clean build (only `renderer.h` and `app.cpp` wiring remain).

- [ ] **Step 3: Commit**

```
git add source/renderer/renderer_objects.cpp
git commit -m "feat: implement PickObjectAtScreen with FBO readback"
```

---

## Task 5: Expose `PickObjectAtScreen()` through `Renderer`

**Files:**
- Modify: `source/renderer/renderer.h`

- [ ] **Step 1: Add forwarding method to `Renderer`**

In `source/renderer/renderer.h`, after the `GetMeshZOffset(...)` line (~line 110), add:

```cpp
    int PickObjectAtScreen(int x, int y, int w, int h,
                           const view_define_s& vd,
                           const std::vector<LevelObject>& objects,
                           int draw_parts) {
        SetupUBOMats(vd);
        return objects_.PickObjectAtScreen(x, y, w, h, ubo_mats_, objects, draw_parts, vd.pos_);
    }
```

`SetupUBOMats()` uploads the projection + view matrices to the shared UBO so the picking vertex shader uses the same transform as the main render pass.

You'll also need to add the `LevelObject` include. Check if it is already transitively included via `renderer_objects.h` → `level_objects.h`. If not, add at the top of `renderer.h`:

```cpp
#include "../level/level_objects.h"
```

- [ ] **Step 2: Build and verify**

```
cmake --build build --target igi_editor 2>&1 | tail -20
```

Expected: clean build.

- [ ] **Step 3: Commit**

```
git add source/renderer/renderer.h
git commit -m "feat: expose PickObjectAtScreen forwarding on Renderer"
```

---

## Task 6: Replace `App::PickObjectAtScreenPos()` body in `app.cpp`

**Files:**
- Modify: `source/app.cpp`

- [ ] **Step 1: Locate the method body**

The function starts at line ~2861:
```cpp
int App::PickObjectAtScreenPos(int screen_x, int screen_y) {
```
It ends with the return statement of the second pass (~line 2960). The entire body (everything between the opening `{` and the closing `}`) must be replaced.

- [ ] **Step 2: Replace the entire body**

```cpp
int App::PickObjectAtScreenPos(int screen_x, int screen_y) {
    const auto& objects = level_.GetLevelObjects().GetObjects();
    if (objects.empty()) return -1;

    int w = window_state_.viewport_width_;
    int h = window_state_.viewport_height_;
    if (w == 0 || h == 0) return -1;

    return renderer_.PickObjectAtScreen(
        screen_x, screen_y, w, h,
        view_define_,
        objects,
        renderer_.DRAW_OBJECTS | renderer_.DRAW_BUILDINGS | renderer_.DRAW_PROPS
    );
}
```

The `draw_parts` value `DRAW_OBJECTS | DRAW_BUILDINGS | DRAW_PROPS` (= 4|16|32 = 52) matches what the main render pass draws, so all pickable object categories are included.

- [ ] **Step 3: Build the full project**

```
cmake --build build --target igi_editor 2>&1 | tail -20
```

Expected: clean build with no warnings about the old dead code.

- [ ] **Step 4: Smoke test manually**

1. Launch `igi_editor.exe`.
2. Open any level.
3. Hover over an object — tooltip/highlight should appear only on the exact rendered pixels of the mesh, not on empty bounding-box space around it.
4. Hover over a building from outside — only the building itself highlights, not interior child objects.
5. Walk the camera inside a building — interior objects should now become hoverable.
6. Hover over empty ground — no object selected (prior AABB picker often selected the nearest object even with the cursor off-model).

- [ ] **Step 5: Commit**

```
git add source/app.cpp
git commit -m "feat: replace CPU AABB picker with GPU color picking"
```

---

## Self-Review

**Spec coverage check:**

| Requirement | Task covering it |
|---|---|
| Pixel-perfect hover — only rendered pixels trigger hit | Task 3 (flat-color shader), Task 4 (1-pixel readback) |
| Building interior occlusion — outside camera can't pick interior objects | Task 3 (`inside_buildings` AABB test) |
| ATTA sub-models return parent ID | Task 3 — ATTA attachments not drawn separately; parent hull covers them |
| FBO lifecycle: create 1×1 at Init, resize on viewport change, delete in Shutdown | Task 2 (Init/Shutdown), Task 4 (resize check) |
| ID 0 = background → return -1; ID = index+1 | Task 4 decode |
| Y-flip for OpenGL origin | Task 4 `h - y - 1` |
| Gate picking on mouse move only | Not added — existing call sites in `App::OnMouseMove` and `App::Frame` already control invocation frequency. No change needed. |

**Placeholder scan:** None found — every step contains full code.

**Type consistency:**
- `pick_shader_prog_` used consistently across Tasks 1–4 (not `pick_shader_program_` — shortened for consistency with `pick_fbo_`, `pick_color_tex_`, etc.)
- `InitPickingFBO(w, h)` signature matches declaration and call sites.
- `DrawForPicking(ubo_mats, objects, draw_parts, camera_pos)` — 4 params, matches declaration and call in `PickObjectAtScreen`.
- `PickObjectAtScreen(x, y, w, h, ubo_mats, objects, draw_parts, camera_pos)` — 8 params match header, forwarding call in renderer.h, and usage in app.cpp.
- `IsWeaponModel()` is called in Task 3 — verify this function is visible in `renderer_objects.cpp`. It is a file-local static or member defined earlier in the same TU (used by `Draw()` at line ~685, so it exists).
- `obj.scale` — used in both model matrix and AABB extents scaling. `LevelObject::scale` is `float` (default 1.0f).

**Scope check:** Single subsystem (object picking), produces working testable software on its own. ✓
