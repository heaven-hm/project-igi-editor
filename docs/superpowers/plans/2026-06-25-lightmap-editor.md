# Calculate Light Mapping Editor Button Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a "Calculate Light Mapping" button to the right-click property
editor for `Building`/`EditRigidObj` objects, which resolves and applies that
exact placement's baked lightmap onto the building's live 3D model.

**Architecture:** Shell out to the already-working bundled
`assets/editor/tools/igi1conv/igi1conv.exe` (`lightmap resolve` / `olm
to-png`) instead of porting any binary/script parser in-process — this
follows the editor's existing `utils_igi1conv.h` convention. Only the
rendering-side plumbing (a second UV channel, a second texture sampler, a
shader blend) is ported in-process, because that runs every frame. Lightmap
textures are looked up by each placement's exact `taskId` in a map owned by
`Renderer_Objects` — NOT baked into the shared per-modelId `Mesh` cache — so
two placements of the same building model never collide.

**Tech Stack:** C++20, OpenGL 3.3, GLM, GoogleTest (gtest), CMake, MSVC
(Visual Studio 17 2022, Win32 platform), stb_image (vendored under
`third_party/tinygltf`).

## Global Constraints

- Build with the existing `./build` directory (already configured:
  `Visual Studio 17 2022`, platform `Win32`). Do not reconfigure to a
  different generator/platform.
- Every task ends with a successful build of `igi-editor` (and `igi_tests`
  where the task touches test-covered code) before moving to the next task.
- Small, focused commits — one per task, after the build/test verification
  for that task passes.
- Follow existing code conventions exactly (this is an established,
  idiosyncratic codebase): `Logger::Get().Log(LogLevel::X, "[Tag] message")`
  for logging, `std::string err` out-parameters for fallible calls (not
  exceptions/`std::optional`), `igi1conv::` namespace for all bundled-exe
  wrappers.
- Do not modify unrelated code. Do not "improve" adjacent lines.
- Final task deploys a Release build to `D:\IGI1` (the existing game/test
  install) by copying the build output, mirroring how `igi1ed.exe` already
  lives there.

---

### Task 1: `RenderVertex::uv2` — read the lightmap UV channel for modelType 3

**Files:**
- Modify: `source/renderer/mef_native.h` (struct `RenderVertex`, ~line 12-20)
- Modify: `source/renderer/mef_native.cpp` (`ParseRenderVertices()`, ~line
  225-279)
- Test: `tests/test_mef_native_lightmap.cpp` (new)
- Modify: `CMakeLists.txt` (add the new test file + `mef_native.cpp` to
  `igi_tests`)

**Interfaces:**
- Produces: `RenderVertex::uv2` (`glm::vec2`, default `{0,0}`) — every later
  task that touches vertex data (`model.cpp`) reads this field.
- Consumes: nothing new — pure addition to an existing parser.

Context: the 40-byte XTRV vertex record for `modelType == 3` currently reads
`pos` (+0..11), `normal` (+12..23), `uv` (+24..31), and leaves bytes
`+32..+39` completely unread (confirmed by reading `ParseRenderVertices()` —
the `if (modelType == 1)` branch is the only thing that touches `+32`, and it
does not run for `modelType == 3`). Those bytes are the lightmap atlas UV
for type-3 ("MODEL_LIGHTMAP", buildings) models.

- [ ] **Step 1: Write the failing test**

Create `tests/test_mef_native_lightmap.cpp`:

```cpp
#include <gtest/gtest.h>
#include "renderer/mef_native.h"
#include <filesystem>

// Real corpus fixture: 435_01_1 (WaterTower) is a known MODEL_LIGHTMAP
// (modelType 3) building model, confirmed via:
//   igi1conv lightmap resolve --model 435_01_1 \
//     --qsc D:\IGI1\missions\location0\level1\objects.qsc --task-id 1104
// which resolves successfully against this exact level/model pair.
static const char* kWaterTowerMef = "D:\\IGI1\\editor\\models\\level1\\435_01_1.mef";

TEST(MefNativeLightmapTest, ModelType3PopulatesDistinctUv2Channel) {
    if (!std::filesystem::exists(kWaterTowerMef)) {
        GTEST_SKIP() << "Real IGI1 corpus not present at: " << kWaterTowerMef;
    }

    ParsedGeometry geo = ParseMefFile(kWaterTowerMef);
    ASSERT_EQ(geo.modelType, 3u)
        << "435_01_1 (WaterTower) is expected to be MODEL_LIGHTMAP (type 3)";
    ASSERT_FALSE(geo.vertices.empty());

    // The lightmap atlas UV is a distinct mapping from the diffuse UV for
    // every type-3 model — if uv2 were still unread (all zero / equal to uv)
    // this would fail.
    bool anyVertexHasDistinctUv2 = false;
    for (const auto& v : geo.vertices) {
        if (v.uv2 != v.uv) { anyVertexHasDistinctUv2 = true; break; }
    }
    EXPECT_TRUE(anyVertexHasDistinctUv2)
        << "Expected at least one vertex whose uv2 (lightmap atlas UV) "
           "differs from uv (diffuse UV) on a type-3 model";
}
```

- [ ] **Step 2: Wire the new test into CMake and confirm it fails to compile**

In `CMakeLists.txt`, add the test file to the `igi_tests` executable's
source list (after line 244, `tests/test_res_stream_append.cpp`):

```cmake
    tests/test_res_stream_append.cpp
    tests/test_mef_native_lightmap.cpp
)
```

Add `source/renderer/mef_native.cpp` to `igi_tests`' `target_sources` (after
line 284, `source/level/terrain_files.cpp`):

```cmake
    source/level/terrain_files.cpp
    source/renderer/mef_native.cpp
)
```

Reconfigure and build the test target:

```
cmake -S . -B build
cmake --build build --target igi_tests --config Debug
```

Expected: **build error** — `'uv2': is not a member of 'RenderVertex'`
(the field doesn't exist yet).

- [ ] **Step 3: Add `uv2` to `RenderVertex` and read it for modelType 3**

In `source/renderer/mef_native.h`, change:

```cpp
struct RenderVertex {
    glm::vec3 pos{0.f};
    glm::vec3 rawPos{0.f};      // raw XTRV position (NOT scaled, NOT baked) — for ASCII export
    glm::vec3 normal{0.f};      // from XTRV bytes +12..+23
    glm::vec2 uv{0.f};
    uint16_t boneIndex{0};
    uint16_t localVertexId{0};  // XTRV.vn @+36
    float    weight{1.0f};      // XTRV.w  @+32
};
```

to:

```cpp
struct RenderVertex {
    glm::vec3 pos{0.f};
    glm::vec3 rawPos{0.f};      // raw XTRV position (NOT scaled, NOT baked) — for ASCII export
    glm::vec3 normal{0.f};      // from XTRV bytes +12..+23
    glm::vec2 uv{0.f};
    glm::vec2 uv2{0.f};         // lightmap atlas UV, modelType==3 only, XTRV bytes +32..+39
    uint16_t boneIndex{0};
    uint16_t localVertexId{0};  // XTRV.vn @+36
    float    weight{1.0f};      // XTRV.w  @+32
};
```

In `source/renderer/mef_native.cpp`, in `ParseRenderVertices()`, change:

```cpp
        if (modelType == 1) {
            vertices[i].weight        = ReadValue<float>   (bytes, base + 32);
            vertices[i].localVertexId = ReadValue<uint16_t>(bytes, base + 36);
            vertices[i].boneIndex     = ReadValue<uint16_t>(bytes, base + 38);
        }
```

to:

```cpp
        if (modelType == 1) {
            vertices[i].weight        = ReadValue<float>   (bytes, base + 32);
            vertices[i].localVertexId = ReadValue<uint16_t>(bytes, base + 36);
            vertices[i].boneIndex     = ReadValue<uint16_t>(bytes, base + 38);
        } else if (modelType == 3) {
            vertices[i].uv2 = glm::vec2(
                ReadValue<float>(bytes, base + 32),
                ReadValue<float>(bytes, base + 36)
            );
        }
```

- [ ] **Step 4: Build and run the test, verify it passes**

```
cmake --build build --target igi_tests --config Debug
build\bin\Debug\igi_tests.exe --gtest_filter=MefNativeLightmapTest.*
```

Expected: `[ PASS ]` (or `[ SKIPPED ]` if `D:\IGI1` is not present on the
machine running this — both are acceptable outcomes for this step; a SKIP
is not a silent pass, gtest reports it distinctly).

- [ ] **Step 5: Commit**

```bash
git add source/renderer/mef_native.h source/renderer/mef_native.cpp tests/test_mef_native_lightmap.cpp CMakeLists.txt
git commit -m "feat(renderer): read lightmap atlas UV2 for MEF modelType 3"
```

---

### Task 2: Carry `uv2` into the GPU vertex buffer + VAO

**Files:**
- Modify: `source/renderer/model.cpp` (`BuildMeshFromGeometry()`, both the
  `buildSubMesh` lambda path ~line 37-120, and the `renderBlocks` path
  ~line 122-230)

**Interfaces:**
- Consumes: `RenderVertex::uv2` (from Task 1).
- Produces: GPU vertex buffers with stride `10 * sizeof(float)` (was 8),
  VAO attribute slot 3 bound to `uv2`. Task 3 (the shader) consumes
  attribute slot 3.

Context: there are two near-identical code paths in `BuildMeshFromGeometry()`
that each build a `std::vector<float> verts` (8 floats/vertex: pos3 + normal3
+ uv2-component-count-of-2) and upload it. Both must change identically.
Per the design spec, non-type-3 models get `(0,0)` padding so every submesh
keeps the same 10-float stride — no second VAO layout needed.

- [ ] **Step 1: Update the `buildSubMesh` lambda path (fallback, no
  renderBlocks)**

In `source/renderer/model.cpp`, change:

```cpp
    auto buildSubMesh = [&](size_t triangleStart, size_t triangleCount) -> std::optional<SubMesh> {
        std::vector<float> verts;
        verts.reserve(triangleCount * 3 * 8);
```

to:

```cpp
    auto buildSubMesh = [&](size_t triangleStart, size_t triangleCount) -> std::optional<SubMesh> {
        std::vector<float> verts;
        verts.reserve(triangleCount * 3 * 10);
```

Change the `addVertex` lambda inside it — find:

```cpp
                verts.push_back(src.uv.x);
                verts.push_back(1.0f - src.uv.y);
            };

            addVertex(tri[0]);
            addVertex(tri[1]);
            addVertex(tri[2]);
        }

        if (verts.empty()) {
            return std::nullopt;
        }

        SubMesh sub;
        sub.textureID = 0;
        sub.alphaMode = 0;
        sub.vertexCount = static_cast<int>(verts.size() / 8);
        sub.baseColorFactor = glm::vec4(1.0f);

        glGenVertexArrays(1, &sub.VAO);
        glGenBuffers(1, &sub.VBO);
        glBindVertexArray(sub.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, sub.VBO);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);

        return sub;
    };
```

to (the first block — uv2 push — replaces lines 87-88; the rest of the
function body is the second block, changed in place):

```cpp
                verts.push_back(src.uv.x);
                verts.push_back(1.0f - src.uv.y);
                verts.push_back(geometry.modelType == 3 ? src.uv2.x : 0.0f);
                verts.push_back(geometry.modelType == 3 ? src.uv2.y : 0.0f);
            };

            addVertex(tri[0]);
            addVertex(tri[1]);
            addVertex(tri[2]);
        }

        if (verts.empty()) {
            return std::nullopt;
        }

        SubMesh sub;
        sub.textureID = 0;
        sub.alphaMode = 0;
        sub.vertexCount = static_cast<int>(verts.size() / 10);
        sub.baseColorFactor = glm::vec4(1.0f);

        glGenVertexArrays(1, &sub.VAO);
        glGenBuffers(1, &sub.VBO);
        glBindVertexArray(sub.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, sub.VBO);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(8 * sizeof(float)));
        glEnableVertexAttribArray(3);
        glBindVertexArray(0);

        return sub;
    };
```

- [ ] **Step 2: Update the `renderBlocks` path (the common case for real
  models)**

In the same file, find the second, near-identical `addVertex` lambda (inside
the `for (int materialSlot : materialOrder)` loop) — change:

```cpp
                        verts.push_back(src.uv.x);
                        verts.push_back(1.0f - src.uv.y);
                    };

                    addVertex(tri[0]);
                    addVertex(tri[1]);
                    addVertex(tri[2]);
                }
            }

            if (verts.empty()) {
                continue;
            }

            SubMesh sub;
            sub.textureID = 0;
            sub.vertexCount = static_cast<int>(verts.size() / 8);
            sub.baseColorFactor = glm::vec4(1.0f);
            sub.alphaMode = materialHasAlpha ? 2 : 0;
            sub.materialSlot = materialSlot;

            glGenVertexArrays(1, &sub.VAO);
            glGenBuffers(1, &sub.VBO);
            glBindVertexArray(sub.VAO);
            glBindBuffer(GL_ARRAY_BUFFER, sub.VBO);
            glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
            glEnableVertexAttribArray(2);
            glBindVertexArray(0);
```

to:

```cpp
                        verts.push_back(src.uv.x);
                        verts.push_back(1.0f - src.uv.y);
                        verts.push_back(geometry.modelType == 3 ? src.uv2.x : 0.0f);
                        verts.push_back(geometry.modelType == 3 ? src.uv2.y : 0.0f);
                    };

                    addVertex(tri[0]);
                    addVertex(tri[1]);
                    addVertex(tri[2]);
                }
            }

            if (verts.empty()) {
                continue;
            }

            SubMesh sub;
            sub.textureID = 0;
            sub.vertexCount = static_cast<int>(verts.size() / 10);
            sub.baseColorFactor = glm::vec4(1.0f);
            sub.alphaMode = materialHasAlpha ? 2 : 0;
            sub.materialSlot = materialSlot;

            glGenVertexArrays(1, &sub.VAO);
            glGenBuffers(1, &sub.VBO);
            glBindVertexArray(sub.VAO);
            glBindBuffer(GL_ARRAY_BUFFER, sub.VBO);
            glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(3 * sizeof(float)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(6 * sizeof(float)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(8 * sizeof(float)));
            glEnableVertexAttribArray(3);
            glBindVertexArray(0);
```

- [ ] **Step 3: Build and smoke-test (no automated test — GPU vertex layout
  is not unit-testable per the design spec §9; verify by building and
  launching the editor)**

```
cmake --build build --target igi-editor --config Debug
build\bin\Debug\igi1ed.exe
```

Expected: editor launches, loads a level, all models (textured and
untextured, type 0/1/3) render exactly as before this change — this step
only changed vertex stride/padding, no visible difference yet (the shader
doesn't read `uv2` until Task 3).

- [ ] **Step 4: Commit**

```bash
git add source/renderer/model.cpp
git commit -m "feat(renderer): carry uv2 into the GPU vertex buffer and VAO"
```

---

### Task 3: Shader, lightmap texture map, and draw-loop binding

**Files:**
- Modify: `source/renderer/renderer_objects.h` (add member map + 3 public
  methods)
- Modify: `source/renderer/renderer_objects.cpp` (shader sources, draw loop)
- Modify: `source/renderer/renderer.h` (pass-through methods on `Renderer`)

**Interfaces:**
- Consumes: VAO attribute slot 3 (`uv2`) from Task 2.
- Produces: `Renderer_Objects::SetLightmapForTask(taskId, vector<GLuint>)`,
  `ClearLightmapForTask(taskId)`, `GetLightmapForTask(taskId) const ->
  const vector<GLuint>*`; `Renderer::SetLightmapForTask(taskId,
  vector<GLuint>)`, `Renderer::ClearLightmapForTask(taskId)`. Task 7 (App
  pipeline) calls `Renderer::SetLightmapForTask`.

- [ ] **Step 1: Add the lightmap texture map and accessor methods to
  `Renderer_Objects`**

In `source/renderer/renderer_objects.h`, add to the `public:` section (after
the `GetOrLoadSkinGeometry` declaration, line 106):

```cpp
    const ParsedGeometry* GetOrLoadSkinGeometry(const std::string& modelId, bool isBuilding);

    // Lightmap textures for the "Calculate Light Mapping" button, keyed by
    // the EXACT placement's taskId (not modelId) — mesh_cache_ is shared per
    // modelId across every placement of a building, so storing these on
    // SubMesh would make two placements of the same model show whichever
    // lightmap was calculated last. The draw loop looks this up per-object.
    void SetLightmapForTask(const std::string& taskId, std::vector<GLuint> textures);
    void ClearLightmapForTask(const std::string& taskId);
    const std::vector<GLuint>* GetLightmapForTask(const std::string& taskId) const;
```

Add to the `private:` section (after `std::map<std::string, ParsedGeometry>
skin_geometry_cache_;`, line 118):

```cpp
    std::map<std::string, ParsedGeometry> skin_geometry_cache_;
    std::map<std::string, std::vector<GLuint>> lightmap_textures_by_task_;
```

- [ ] **Step 2: Implement the three methods in `renderer_objects.cpp`**

Add near the top of `source/renderer/renderer_objects.cpp` (after the
existing `IsSkippedModelId` definition, ~line 14):

```cpp
void Renderer_Objects::SetLightmapForTask(const std::string& taskId, std::vector<GLuint> textures) {
    ClearLightmapForTask(taskId);
    lightmap_textures_by_task_[taskId] = std::move(textures);
}

void Renderer_Objects::ClearLightmapForTask(const std::string& taskId) {
    auto it = lightmap_textures_by_task_.find(taskId);
    if (it == lightmap_textures_by_task_.end()) return;
    for (GLuint tex : it->second) {
        if (tex != 0) glDeleteTextures(1, &tex);
    }
    lightmap_textures_by_task_.erase(it);
}

const std::vector<GLuint>* Renderer_Objects::GetLightmapForTask(const std::string& taskId) const {
    auto it = lightmap_textures_by_task_.find(taskId);
    return it != lightmap_textures_by_task_.end() ? &it->second : nullptr;
}
```

- [ ] **Step 3: Add `a_uv2`/`v_uv2` and the lightmap sampler to the shaders**

In `source/renderer/renderer_objects.cpp`, change `OBJ_VERT_SRC` from:

```cpp
static const char* OBJ_VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

layout(std140) uniform Matrices {
    mat4 u_unused1;
    mat4 u_unused2;
    mat4 u_mvp; // Proj * View * GlobalScale
};


uniform mat4 u_model;

out vec3 v_normal;
out vec2 v_uv;
out vec3 v_fragPos;

void main() {
    vec4 worldPos   = u_model * vec4(a_pos, 1.0);
    v_fragPos       = worldPos.xyz;
    v_normal        = mat3(transpose(inverse(u_model))) * a_normal;
    v_uv            = a_uv;
    gl_Position     = u_mvp * u_model * vec4(a_pos, 1.0);
}

)";
```

to:

```cpp
static const char* OBJ_VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec2 a_uv2;

layout(std140) uniform Matrices {
    mat4 u_unused1;
    mat4 u_unused2;
    mat4 u_mvp; // Proj * View * GlobalScale
};


uniform mat4 u_model;

out vec3 v_normal;
out vec2 v_uv;
out vec2 v_uv2;
out vec3 v_fragPos;

void main() {
    vec4 worldPos   = u_model * vec4(a_pos, 1.0);
    v_fragPos       = worldPos.xyz;
    v_normal        = mat3(transpose(inverse(u_model))) * a_normal;
    v_uv            = a_uv;
    v_uv2           = a_uv2;
    gl_Position     = u_mvp * u_model * vec4(a_pos, 1.0);
}

)";
```

Change `OBJ_FRAG_SRC` from:

```cpp
static const char* OBJ_FRAG_SRC = R"(
#version 330 core
in vec3 v_normal;
in vec2 v_uv;
in vec3 v_fragPos;

uniform vec3 u_dirlight;   // directional light RGB
uniform vec3 u_ambient;    // ambient light RGB
uniform sampler2D u_texture;
uniform int u_useTexture;
uniform float u_alpha;     // material alpha (1.0 = opaque, <1.0 = transparent)
uniform vec4 u_baseColor;  // Base color when no texture
uniform vec3 u_tint; // per-object multiplicative tint (default white); magenta = missing-in-res warning
uniform float u_glassMin;  // glass sheen floor: clean (low-alpha) glass renders at
                           // least this opaque so the pane is visible. 0 = not glass.

out vec4 fragColor;

void main() {
    vec3 N = normalize(v_normal);
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.5));
    float diff = max(dot(N, lightDir), 0.0);

    vec3 viewDir = normalize(vec3(0.0, 1.0, 1.0));
    vec3 halfVec = normalize(lightDir + viewDir);
    float spec = pow(max(dot(N, halfVec), 0.0), 32.0) * 0.25;

    vec3 light = u_ambient + u_dirlight * (diff + spec);

    vec4 texColor = (u_useTexture != 0) ? texture(u_texture, v_uv) : u_baseColor;

    float finalAlpha = (u_useTexture != 0 ? texColor.a : 1.0) * u_alpha;
```

to:

```cpp
static const char* OBJ_FRAG_SRC = R"(
#version 330 core
in vec3 v_normal;
in vec2 v_uv;
in vec2 v_uv2;
in vec3 v_fragPos;

uniform vec3 u_dirlight;   // directional light RGB
uniform vec3 u_ambient;    // ambient light RGB
uniform sampler2D u_texture;
uniform int u_useTexture;
uniform sampler2D u_lightmap;
uniform int u_useLightmap; // 0 = no calculated lightmap for this submesh
uniform float u_alpha;     // material alpha (1.0 = opaque, <1.0 = transparent)
uniform vec4 u_baseColor;  // Base color when no texture
uniform vec3 u_tint; // per-object multiplicative tint (default white); magenta = missing-in-res warning
uniform float u_glassMin;  // glass sheen floor: clean (low-alpha) glass renders at
                           // least this opaque so the pane is visible. 0 = not glass.

out vec4 fragColor;

void main() {
    vec3 N = normalize(v_normal);
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.5));
    float diff = max(dot(N, lightDir), 0.0);

    vec3 viewDir = normalize(vec3(0.0, 1.0, 1.0));
    vec3 halfVec = normalize(lightDir + viewDir);
    float spec = pow(max(dot(N, halfVec), 0.0), 32.0) * 0.25;

    vec3 light = u_ambient + u_dirlight * (diff + spec);

    vec4 texColor = (u_useTexture != 0) ? texture(u_texture, v_uv) : u_baseColor;

    if (u_useLightmap != 0) {
        texColor.rgb *= texture(u_lightmap, v_uv2).rgb;
    }

    float finalAlpha = (u_useTexture != 0 ? texColor.a : 1.0) * u_alpha;
```

(The rest of `OBJ_FRAG_SRC` after this point is unchanged.)

- [ ] **Step 4: Query the new uniform locations and bind texture unit 1 in
  the draw loop**

In `source/renderer/renderer_objects.cpp`, change:

```cpp
    GLint loc_tex      = glGetUniformLocation(shader_program_, "u_texture");
```

to:

```cpp
    GLint loc_tex      = glGetUniformLocation(shader_program_, "u_texture");
    GLint loc_lightmap    = glGetUniformLocation(shader_program_, "u_lightmap");
    GLint loc_useLightmap = glGetUniformLocation(shader_program_, "u_useLightmap");
```

Then change the per-submesh draw loop from a range-for to an indexed loop so
the submesh index is available for the lightmap lookup. Find:

```cpp
                bool mixedMesh = hasTextured && hasUntextured;
                for (const auto& sub : mesh.subMeshes) {
                    if (sub.VAO == 0 || sub.vertexCount == 0) continue;
```

change to:

```cpp
                bool mixedMesh = hasTextured && hasUntextured;
                const std::vector<GLuint>* lightmaps = GetLightmapForTask(obj.taskId);
                for (size_t si = 0; si < mesh.subMeshes.size(); ++si) {
                    const auto& sub = mesh.subMeshes[si];
                    if (sub.VAO == 0 || sub.vertexCount == 0) continue;
```

Then find (this is unchanged code, just locating the insertion point — the
texture-unit-0 bind block):

```cpp
                    if (sub.textureID > 0) {
                        // Textured submesh: neutral lighting so the texture looks natural.
                        // Windows/glass keep their transparency (alpha 0.4 above) but render
                        // with the SAME normal lighting as everything else, so glass stays
                        // clear and see-through. The earlier flat-gray override (dirlight 0 /
                        // ambient 0.45) darkened panes into murky panels — reverted to the
                        // pre-3986cd9 "before" look the user asked to restore.
                        glUniform3f(loc_dirlight, 0.6f, 0.6f, 0.6f);
                        glUniform3f(loc_ambient,  0.4f, 0.4f, 0.4f);
                        glUniform1i(loc_useTex, 1);
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, sub.textureID);
                        glUniform1i(loc_tex, 0);
                    } else {
                        // Untextured submesh: use material baseColorFactor if available,
                        // otherwise fall back to the hash-based color.
                        glm::vec3 color(sub.baseColorFactor.r, sub.baseColorFactor.g, sub.baseColorFactor.b);
                        if (color.r >= 0.99f && color.g >= 0.99f && color.b >= 0.99f) {
                            color = glm::vec3(r, g, b);
                        }
                        glUniform3f(loc_dirlight, color.r * 0.6f, color.g * 0.6f, color.b * 0.6f);
                        glUniform3f(loc_ambient,  color.r * 0.4f, color.g * 0.4f, color.b * 0.4f);
                        glUniform1i(loc_useTex, 0);
                    }

                    glBindVertexArray(sub.VAO);
                    glDrawArrays(GL_TRIANGLES, 0, sub.vertexCount);
```

and insert the lightmap bind between the `if/else` block and
`glBindVertexArray`:

```cpp
                    if (sub.textureID > 0) {
                        // Textured submesh: neutral lighting so the texture looks natural.
                        // Windows/glass keep their transparency (alpha 0.4 above) but render
                        // with the SAME normal lighting as everything else, so glass stays
                        // clear and see-through. The earlier flat-gray override (dirlight 0 /
                        // ambient 0.45) darkened panes into murky panels — reverted to the
                        // pre-3986cd9 "before" look the user asked to restore.
                        glUniform3f(loc_dirlight, 0.6f, 0.6f, 0.6f);
                        glUniform3f(loc_ambient,  0.4f, 0.4f, 0.4f);
                        glUniform1i(loc_useTex, 1);
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, sub.textureID);
                        glUniform1i(loc_tex, 0);
                    } else {
                        // Untextured submesh: use material baseColorFactor if available,
                        // otherwise fall back to the hash-based color.
                        glm::vec3 color(sub.baseColorFactor.r, sub.baseColorFactor.g, sub.baseColorFactor.b);
                        if (color.r >= 0.99f && color.g >= 0.99f && color.b >= 0.99f) {
                            color = glm::vec3(r, g, b);
                        }
                        glUniform3f(loc_dirlight, color.r * 0.6f, color.g * 0.6f, color.b * 0.6f);
                        glUniform3f(loc_ambient,  color.r * 0.4f, color.g * 0.4f, color.b * 0.4f);
                        glUniform1i(loc_useTex, 0);
                    }

                    // Lightmap: bind texture unit 1 for this exact placement, if the
                    // "Calculate Light Mapping" button has been run on it.
                    if (lightmaps && si < lightmaps->size() && (*lightmaps)[si] != 0) {
                        glActiveTexture(GL_TEXTURE1);
                        glBindTexture(GL_TEXTURE_2D, (*lightmaps)[si]);
                        glUniform1i(loc_lightmap, 1);
                        glUniform1i(loc_useLightmap, 1);
                    } else {
                        glUniform1i(loc_useLightmap, 0);
                    }

                    glBindVertexArray(sub.VAO);
                    glDrawArrays(GL_TRIANGLES, 0, sub.vertexCount);
```

- [ ] **Step 5: Add pass-through methods on `Renderer`**

In `source/renderer/renderer.h`, add near the other `objects_` pass-throughs
(after `void SuppressAtta(const std::string& key) { objects_.SuppressAtta(key); }`,
line 664):

```cpp
	void SetLightmapForTask(const std::string& taskId, std::vector<GLuint> textures) {
		objects_.SetLightmapForTask(taskId, std::move(textures));
	}
	void ClearLightmapForTask(const std::string& taskId) { objects_.ClearLightmapForTask(taskId); }
```

- [ ] **Step 6: Build and smoke-test**

```
cmake --build build --target igi-editor --config Debug
build\bin\Debug\igi1ed.exe
```

Expected: editor launches and renders exactly as before (no lightmaps have
been calculated yet, so `u_useLightmap` is always 0 — purely a no-op
verification that the new uniforms/shader compile and link without error;
check `igi1ed.log` for any GL shader compile/link error).

- [ ] **Step 7: Commit**

```bash
git add source/renderer/renderer_objects.h source/renderer/renderer_objects.cpp source/renderer/renderer.h
git commit -m "feat(renderer): add per-taskId lightmap texture binding to the object shader"
```

---

### Task 4: PNG-to-GL-texture loader

**Files:**
- Create: `source/renderer/png_loader.h`
- Create: `source/renderer/png_loader.cpp`

**Interfaces:**
- Consumes: `pic_s` (from `source/common.h`, already in `pch.h`),
  `GL_RegisterTexture` (from `source/renderer/gl_helper.h`, already in
  `pch.h`), `stbi_load`/`stbi_image_free` (vendored
  `third_party/tinygltf/stb_image.h`, already an include dir for both
  CMake targets).
- Produces: `GLuint LoadPngAsTexture(const std::string& pngPath, std::string&
  err)` — Task 7 (App pipeline) calls this for each converted `.olm` → PNG.

- [ ] **Step 1: Create the header**

`source/renderer/png_loader.h`:

```cpp
#pragma once
#include "../pch.h"
#include <string>

// Loads a PNG file from disk and uploads it as an OpenGL RGBA texture.
// Returns 0 on failure (file missing / decode error), with `err` set.
GLuint LoadPngAsTexture(const std::string& pngPath, std::string& err);
```

- [ ] **Step 2: Create the implementation**

`source/renderer/png_loader.cpp`:

```cpp
#include "png_loader.h"
#include "../pch.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

GLuint LoadPngAsTexture(const std::string& pngPath, std::string& err) {
    int w = 0, h = 0, channels = 0;
    unsigned char* data = stbi_load(pngPath.c_str(), &w, &h, &channels, 4);
    if (!data) {
        err = "failed to decode PNG: " + pngPath;
        return 0;
    }
    pic_s pic{ w, h, data };
    GLuint texture = GL_RegisterTexture(&pic, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR, false);
    stbi_image_free(data);
    return texture;
}
```

- [ ] **Step 3: Reconfigure and build (new file picked up by the
  `source/renderer/*.*` glob)**

```
cmake -S . -B build
cmake --build build --target igi-editor --config Debug
```

Expected: builds cleanly. There is no isolated unit test for this file — it
requires a live GL context (same as every other `gl_helper.cpp` consumer in
this codebase); it is exercised end-to-end in Task 7's manual verification.

- [ ] **Step 4: Commit**

```bash
git add source/renderer/png_loader.h source/renderer/png_loader.cpp
git commit -m "feat(renderer): add stb_image-based PNG-to-GL-texture loader"
```

---

### Task 5: `igi1conv::LightmapResolve` / `OlmToPng` wrappers

**Files:**
- Modify: `source/utils_igi1conv.h` (add 3 declarations)
- Modify: `source/utils_igi1conv.cpp` (add `RunCaptureStdout` internal
  helper + 3 implementations)
- Test: `tests/test_igi1conv_lightmap.cpp` (new)
- Modify: `CMakeLists.txt` (add the new test file + `utils_igi1conv.cpp` to
  `igi_tests`)

**Interfaces:**
- Produces: `igi1conv::LightmapResolve(modelId, qscPath, taskId, err) ->
  vector<string>`, `igi1conv::ParseLightmapResolveStdout(stdoutText, err) ->
  vector<string>` (pure parser, exposed for the test), `igi1conv::OlmToPng
  (olmPath, outPng, err) -> bool`. Task 7 (App pipeline) calls
  `LightmapResolve` and `OlmToPng`.
- Consumes: `igi1conv::GetExePath()`, `igi1conv::MakeTempPath()` (existing).

Context: every existing wrapper in this file uses `-o <file>` and reads the
file. `igi1conv lightmap resolve` has **no** `-o` flag (confirmed by running
`igi1conv lightmap --help` against the real bundled exe) — its output is
stdout-only. `Run()` does not capture stdout (it inherits the console
silently), so a new `RunCaptureStdout` helper is needed, used only by
`LightmapResolve`.

The exact stdout format (captured by running the real bundled exe against
the real `D:\IGI1\missions\location0\level1` corpus):

```
lightmap: resolved   task 1104 "WaterTower" -> obj00000 @ (2.46585e+07, -5.59572e+07, 1.74412e+08)
lightmap: 11 .olm file(s):
  D:\IGI1\missions\location0\level1\lightmaps\lightmaps_unpacked\obj00000_00000.olm
  D:\IGI1\missions\location0\level1\lightmaps\lightmaps_unpacked\obj00000_00001.olm
  ... (9 more, one per line, each indented with 2 spaces)
```

- [ ] **Step 1: Write the failing tests**

Create `tests/test_igi1conv_lightmap.cpp`:

```cpp
#include <gtest/gtest.h>
#include "utils_igi1conv.h"

// ============================================================
//  ParseLightmapResolveStdout — pure stdout parsing, no process spawn.
//  Sample text captured from the real bundled igi1conv.exe:
//    igi1conv lightmap resolve --model 435_01_1 \
//      --qsc D:\IGI1\missions\location0\level1\objects.qsc --task-id 1104
// ============================================================

TEST(Igi1convLightmapTest, ParsesRealResolveOutput) {
    const std::string sample =
        "lightmap: resolved   task 1104 \"WaterTower\" -> obj00000 @ (2.46585e+07, -5.59572e+07, 1.74412e+08)\r\n"
        "lightmap: 11 .olm file(s):\r\n"
        "  D:\\IGI1\\missions\\location0\\level1\\lightmaps\\lightmaps_unpacked\\obj00000_00000.olm\r\n"
        "  D:\\IGI1\\missions\\location0\\level1\\lightmaps\\lightmaps_unpacked\\obj00000_00001.olm\r\n"
        "  D:\\IGI1\\missions\\location0\\level1\\lightmaps\\lightmaps_unpacked\\obj00000_00002.olm\r\n";
    std::string err;
    std::vector<std::string> paths = igi1conv::ParseLightmapResolveStdout(sample, err);
    ASSERT_EQ(paths.size(), 3u) << "err=" << err;
    EXPECT_EQ(paths[0], "D:\\IGI1\\missions\\location0\\level1\\lightmaps\\lightmaps_unpacked\\obj00000_00000.olm");
    EXPECT_EQ(paths[2], "D:\\IGI1\\missions\\location0\\level1\\lightmaps\\lightmaps_unpacked\\obj00000_00002.olm");
}

TEST(Igi1convLightmapTest, EmptyOutputReturnsErrorNoPaths) {
    std::string err;
    std::vector<std::string> paths = igi1conv::ParseLightmapResolveStdout("", err);
    EXPECT_TRUE(paths.empty());
    EXPECT_FALSE(err.empty());
}

TEST(Igi1convLightmapTest, StopsAtNextNonIndentedLine) {
    const std::string sample =
        "lightmap: resolved   task 1 \"X\" -> obj00000 @ (0, 0, 0)\r\n"
        "lightmap: 2 .olm file(s):\r\n"
        "  C:\\a.olm\r\n"
        "  C:\\b.olm\r\n"
        "some trailing unrelated line\r\n"
        "  C:\\should_not_be_included.olm\r\n";
    std::string err;
    std::vector<std::string> paths = igi1conv::ParseLightmapResolveStdout(sample, err);
    ASSERT_EQ(paths.size(), 2u);
    EXPECT_EQ(paths[1], "C:\\b.olm");
}
```

- [ ] **Step 2: Wire the new test into CMake and confirm it fails to
  compile**

In `CMakeLists.txt`, add to `igi_tests`' source list (after
`tests/test_mef_native_lightmap.cpp` from Task 1):

```cmake
    tests/test_mef_native_lightmap.cpp
    tests/test_igi1conv_lightmap.cpp
)
```

Add `source/utils_igi1conv.cpp` to `igi_tests`' `target_sources` (after
`source/renderer/mef_native.cpp` from Task 1):

```cmake
    source/renderer/mef_native.cpp
    source/utils_igi1conv.cpp
)
```

```
cmake -S . -B build
cmake --build build --target igi_tests --config Debug
```

Expected: **build error** —
`'ParseLightmapResolveStdout': is not a member of 'igi1conv'` (doesn't exist
yet).

- [ ] **Step 3: Add the declarations**

In `source/utils_igi1conv.h`, add after the `QscValidate` declaration
(after line 101, before the `fnt` section comment):

```cpp
bool QscValidate(const std::string& qscPath, std::string& err);

// ─── lightmap / olm ──────────────────────────────────────────────────────

// `igi1conv lightmap resolve --model <id> --qsc <path> --task-id <id>`.
// Returns the resolved .olm file paths, or empty with `err` set (no
// binding, no .olm files on disk, exit-code failure, or unexpected output
// format). `taskId` disambiguates exactly — the editor always knows the
// exact placement, so the CLI's ambiguous-match exit code never triggers.
std::vector<std::string> LightmapResolve(const std::string& modelId,
                                         const std::string& qscPath,
                                         const std::string& taskId,
                                         std::string& err);

// Pure parser for `lightmap resolve`'s stdout — exposed for unit testing
// without spawning a process. Extracts the indented .olm file paths
// following the "N .olm file(s):" line. Returns empty with `err` set if the
// format doesn't match (e.g. unexpected output, or no .olm files listed).
std::vector<std::string> ParseLightmapResolveStdout(const std::string& stdoutText, std::string& err);

// `igi1conv olm to-png <input.olm> -o <out.png>`.
bool OlmToPng(const std::string& olmPath, const std::string& outPng, std::string& err);
```

- [ ] **Step 4: Implement `RunCaptureStdout`, `ParseLightmapResolveStdout`,
  `LightmapResolve`, `OlmToPng`**

In `source/utils_igi1conv.cpp`, add after the existing `Run()` function
(after line 71, before the `// ─── res ───` section comment):

```cpp
// Like Run(), but captures the child process's stdout (and stderr, merged)
// into `stdoutOut`. Needed for `lightmap resolve`/`lightmap list`, which
// have no -o flag — their only output is stdout text.
static bool RunCaptureStdout(const std::string& args, std::string& stdoutOut,
                             std::string& err, DWORD timeoutMs = 120000) {
    const std::string exe = GetExePath();
    if (!fs::exists(exe)) {
        err = "igi1conv.exe not found: " + exe;
        return false;
    }
    std::string cmdLine = "\"" + exe + "\" " + args;
    Logger::Get().Log(LogLevel::INFO, "[igi1conv] " + cmdLine);

    SECURITY_ATTRIBUTES saAttr{};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = nullptr;

    HANDLE hReadPipe = nullptr, hWritePipe = nullptr;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &saAttr, 0)) {
        err = "CreatePipe failed (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWritePipe;
    si.hStdError  = hWritePipe;
    PROCESS_INFORMATION pi = {};
    std::vector<char> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back('\0');

    BOOL ok = CreateProcessA(nullptr, buf.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWritePipe);
    if (!ok) {
        err = "CreateProcess failed (" + std::to_string(GetLastError()) + ") for " + cmdLine;
        Logger::Get().Log(LogLevel::ERR, "[igi1conv] " + err);
        CloseHandle(hReadPipe);
        return false;
    }

    std::string output;
    char chunk[4096];
    DWORD nRead = 0;
    while (ReadFile(hReadPipe, chunk, sizeof(chunk), &nRead, nullptr) && nRead > 0) {
        output.append(chunk, nRead);
    }
    CloseHandle(hReadPipe);

    WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    stdoutOut = output;
    if (code != 0) {
        err = "igi1conv exit code " + std::to_string(code) + " for: " + args +
              (output.empty() ? "" : ("\n" + output));
        Logger::Get().Log(LogLevel::ERR, "[igi1conv] " + err);
        return false;
    }
    return true;
}

// ─── lightmap / olm ───────────────────────────────────────────────────────

std::vector<std::string> ParseLightmapResolveStdout(const std::string& stdoutText, std::string& err) {
    std::vector<std::string> paths;
    std::istringstream iss(stdoutText);
    std::string line;
    bool inFileList = false;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!inFileList) {
            if (line.find(".olm file(s):") != std::string::npos) {
                inFileList = true;
            }
            continue;
        }
        if (line.empty()) continue;
        if (line[0] != ' ' && line[0] != '\t') break;
        size_t start = line.find_first_not_of(" \t");
        if (start != std::string::npos) paths.push_back(line.substr(start));
    }
    if (paths.empty()) {
        err = "no .olm file paths found in lightmap resolve output: " +
              (stdoutText.empty() ? std::string("(empty output)") : stdoutText);
    }
    return paths;
}

std::vector<std::string> LightmapResolve(const std::string& modelId, const std::string& qscPath,
                                         const std::string& taskId, std::string& err) {
    std::string args = "lightmap resolve --model \"" + modelId + "\" --qsc \"" + qscPath +
                       "\" --task-id " + taskId;
    std::string out;
    if (!RunCaptureStdout(args, out, err)) return {};
    return ParseLightmapResolveStdout(out, err);
}

bool OlmToPng(const std::string& olmPath, const std::string& outPng, std::string& err) {
    return Run("olm to-png \"" + olmPath + "\" -o \"" + outPng + "\"", err);
}
```

- [ ] **Step 5: Build and run the tests, verify they pass**

```
cmake --build build --target igi_tests --config Debug
build\bin\Debug\igi_tests.exe --gtest_filter=Igi1convLightmapTest.*
```

Expected: 3/3 `[ PASSED ]`.

- [ ] **Step 6: Commit**

```bash
git add source/utils_igi1conv.h source/utils_igi1conv.cpp tests/test_igi1conv_lightmap.cpp CMakeLists.txt
git commit -m "feat(igi1conv): add LightmapResolve/OlmToPng CLI wrappers"
```

---

### Task 6: `Level::GetQscPath()` getter

**Files:**
- Modify: `source/level/level.h`

**Interfaces:**
- Produces: `const std::string& Level::GetQscPath() const`. Task 7 (App
  pipeline) calls this to get the current level's `.qsc` path for
  `LightmapResolve`.

- [ ] **Step 1: Add the getter**

In `source/level/level.h`, change:

```cpp
	const LevelObjects&		GetLevelObjects() const { return level_objects_; }
	LevelObjects&			GetLevelObjects() { return level_objects_; }
```

to:

```cpp
	const LevelObjects&		GetLevelObjects() const { return level_objects_; }
	LevelObjects&			GetLevelObjects() { return level_objects_; }
	const std::string&		GetQscPath() const { return qsc_path_; }
```

- [ ] **Step 2: Build**

```
cmake --build build --target igi-editor --config Debug
```

Expected: builds cleanly (pure addition, no behavior change to verify
beyond compilation).

- [ ] **Step 3: Commit**

```bash
git add source/level/level.h
git commit -m "feat(level): expose the current level's objects.qsc path"
```

---

### Task 7: `App::CalculateLightmapForSelectedObject()` pipeline

**Files:**
- Modify: `source/app.h` (add method declaration)
- Modify: `source/app_internal.h` (add 2 includes)
- Modify: `source/app_editor.cpp` (add method implementation)

**Interfaces:**
- Consumes: `igi1conv::LightmapResolve`, `igi1conv::OlmToPng`,
  `igi1conv::MakeTempPath` (Task 5), `LoadPngAsTexture` (Task 4),
  `Renderer::SetLightmapForTask` (Task 3), `Level::GetQscPath` (Task 6),
  `LevelObject::{modelId, taskId, type}` (existing).
- Produces: `void App::CalculateLightmapForSelectedObject()`. Task 9 (click
  handler) calls this.

- [ ] **Step 1: Declare the method**

In `source/app.h`, add after `void ToggleAnimationForObject(int objIndex,
int animId);` (line 250):

```cpp
	void ToggleAnimationForObject(int objIndex, int animId);
	// "Calculate Light Mapping" property-panel button: resolves and applies
	// the selected Building/EditRigidObj's exact-placement baked lightmap.
	// No-op (with a logged warning) if no object is selected or the selected
	// object's task type doesn't carry lightmap bindings.
	void CalculateLightmapForSelectedObject();
```

- [ ] **Step 2: Add the includes shared headers need**

In `source/app_internal.h`, add after `#include "renderer/gl_helper.h"`
(line 29):

```cpp
#include "renderer/gl_helper.h"
#include "renderer/png_loader.h"
#include "utils_igi1conv.h"
```

- [ ] **Step 3: Implement the method**

In `source/app_editor.cpp`, add after `App::LoadAIScriptForSelected()`'s
closing brace (find the end of that function — it ends with `}` before the
next `void App::CommitPropTextEdit() {` at line 297):

```cpp
void App::CalculateLightmapForSelectedObject() {
	auto& objects = level_.GetLevelObjects().GetObjects();
	if (selected_object_index_ < 0 || selected_object_index_ >= (int)objects.size()) {
		Logger::Get().Log(LogLevel::WARNING, "[Lightmap] No object selected.");
		return;
	}
	LevelObject& obj = objects[selected_object_index_];
	if (obj.type != "Building" && obj.type != "EditRigidObj") {
		Logger::Get().Log(LogLevel::WARNING, "[Lightmap] \"" + obj.type + "\" objects don't carry lightmap bindings.");
		return;
	}

	const std::string qscPath = level_.GetQscPath();
	Logger::Get().Log(LogLevel::INFO, "[Lightmap] Resolving lightmap for model=" + obj.modelId +
		" taskId=" + obj.taskId + " qsc=" + qscPath);

	std::string err;
	std::vector<std::string> olmPaths = igi1conv::LightmapResolve(obj.modelId, qscPath, obj.taskId, err);
	if (olmPaths.empty()) {
		status_message_ = "Lightmap: " + err;
		Logger::Get().Log(LogLevel::WARNING, "[Lightmap] resolve failed: " + err);
		return;
	}
	Logger::Get().Log(LogLevel::INFO, "[Lightmap] Resolved " + std::to_string(olmPaths.size()) + " .olm file(s).");

	std::vector<GLuint> textures;
	textures.reserve(olmPaths.size());
	for (const auto& olmPath : olmPaths) {
		std::string pngPath = igi1conv::MakeTempPath(".lightmap.png");
		std::string convErr;
		if (!igi1conv::OlmToPng(olmPath, pngPath, convErr)) {
			Logger::Get().Log(LogLevel::ERR, "[Lightmap] olm to-png failed for " + olmPath + ": " + convErr);
			textures.push_back(0);
			continue;
		}
		std::string loadErr;
		GLuint tex = LoadPngAsTexture(pngPath, loadErr);
		std::error_code ec;
		std::filesystem::remove(pngPath, ec);
		if (tex == 0) {
			Logger::Get().Log(LogLevel::ERR, "[Lightmap] PNG load failed for " + olmPath + ": " + loadErr);
		}
		textures.push_back(tex);
	}

	Logger::Get().Log(LogLevel::INFO, "[Lightmap] Uploaded " + std::to_string(textures.size()) +
		" lightmap texture(s) for taskId=" + obj.taskId);
	status_message_ = "Lightmap calculated: " + std::to_string(textures.size()) + " texture(s) applied";
	renderer_.SetLightmapForTask(obj.taskId, std::move(textures));
}
```

- [ ] **Step 4: Build**

```
cmake --build build --target igi-editor --config Debug
```

Expected: builds cleanly. No automated test for this method (it's pure
glue over already-tested pieces — `ParseLightmapResolveStdout` is unit
tested in Task 5, the rendering side in Task 3; this method's correctness is
verified end-to-end in Task 9's manual test, once the button exists to
trigger it).

- [ ] **Step 5: Commit**

```bash
git add source/app.h source/app_internal.h source/app_editor.cpp
git commit -m "feat(app): add CalculateLightmapForSelectedObject pipeline"
```

---

### Task 8: `PropPanel::WidgetKind::LightmapButton`

**Files:**
- Modify: `source/renderer/renderer.h` (`PropPanel` namespace)

**Interfaces:**
- Consumes: nothing new.
- Produces: `PropPanel::WidgetKind::LightmapButton`; `BuildLayout(...,
  bool showLightmapButton = false)` gains a new trailing parameter. Task 9
  (call sites + click handler + draw) consumes both.

- [ ] **Step 1: Add the widget kind**

In `source/renderer/renderer.h`, change:

```cpp
enum class WidgetKind {
    NoteBox,       // editable note (obj.name)
    PosPad,        // 2D X/Y pad
    PosZSlider,    // vertical Z slider
    SnapGround,    // button
    SnapObject,    // button
    OriSlider,     // horizontal orientation slider (Real32x9 component)
    RgbSlider,     // horizontal RGB slider (0..1) with swatch
    NumSlider,     // horizontal numeric slider (Real/Angle/Degrees)
    NumBox,        // editable numeric input box (Int/Real X/Y/Z) — click to type, drag to scrub
    StringBox,     // editable text box (String*/VarString)
    Checkbox,      // bool8 / PushButton
    ChildHeader,   // non-interactive separator label for a child task section
    AIScriptPath,  // single-line editable: resolved .qvm file path
    AIScriptText,  // multiline editable: decompiled QSC source
    AnimIdButton,  // button: toggle play/pause for one discovered AIAction_PlayAnimation id (id stored in `comp`)
};
```

to:

```cpp
enum class WidgetKind {
    NoteBox,       // editable note (obj.name)
    PosPad,        // 2D X/Y pad
    PosZSlider,    // vertical Z slider
    SnapGround,    // button
    SnapObject,    // button
    OriSlider,     // horizontal orientation slider (Real32x9 component)
    RgbSlider,     // horizontal RGB slider (0..1) with swatch
    NumSlider,     // horizontal numeric slider (Real/Angle/Degrees)
    NumBox,        // editable numeric input box (Int/Real X/Y/Z) — click to type, drag to scrub
    StringBox,     // editable text box (String*/VarString)
    Checkbox,      // bool8 / PushButton
    ChildHeader,   // non-interactive separator label for a child task section
    AIScriptPath,  // single-line editable: resolved .qvm file path
    AIScriptText,  // multiline editable: decompiled QSC source
    AnimIdButton,  // button: toggle play/pause for one discovered AIAction_PlayAnimation id (id stored in `comp`)
    LightmapButton, // button: "Calculate Light Mapping" (Building/EditRigidObj only)
};
```

- [ ] **Step 2: Emit the widget in `BuildLayout`, gated by a new parameter**

Change the function signature from:

```cpp
inline Layout BuildLayout(const TaskSchemaNS::TaskSchema& schema, bool is_ai = false,
                          const std::vector<std::pair<int, const TaskSchemaNS::TaskSchema*>>& children = {},
                          int animBoneHierarchy = -1,
                          const std::vector<int>& animIds = {}) {
```

to:

```cpp
inline Layout BuildLayout(const TaskSchemaNS::TaskSchema& schema, bool is_ai = false,
                          const std::vector<std::pair<int, const TaskSchemaNS::TaskSchema*>>& children = {},
                          int animBoneHierarchy = -1,
                          const std::vector<int>& animIds = {},
                          bool showLightmapButton = false) {
```

Then, in the body, find the AI script section block:

```cpp
    // AI Script section — only for AI tasks (HumanSoldier, HumanAI, etc.)
    if (is_ai) {
        y += kRowH;  // "AI Script Path:" label line
        L.widgets.push_back({WidgetKind::AIScriptPath,
                             kLeft + kPad, y, kLeft + kWidth - kPad, y + kBoxH,
                             kAIScriptPathField, 0});
        y += kBoxH + 6;

        y += kRowH;  // "AI Script:" label line
        const int scriptH = kBoxH * 12;
        L.widgets.push_back({WidgetKind::AIScriptText,
                             kLeft + kPad, y, kLeft + kWidth - kPad, y + scriptH,
                             kAIScriptTextField, 0});
        y += scriptH + 6;
    }

    L.panel_h = (y + kPad) - kTop;
    return L;
}
```

and insert the lightmap button just before the final `L.panel_h` line:

```cpp
    // AI Script section — only for AI tasks (HumanSoldier, HumanAI, etc.)
    if (is_ai) {
        y += kRowH;  // "AI Script Path:" label line
        L.widgets.push_back({WidgetKind::AIScriptPath,
                             kLeft + kPad, y, kLeft + kWidth - kPad, y + kBoxH,
                             kAIScriptPathField, 0});
        y += kBoxH + 6;

        y += kRowH;  // "AI Script:" label line
        const int scriptH = kBoxH * 12;
        L.widgets.push_back({WidgetKind::AIScriptText,
                             kLeft + kPad, y, kLeft + kWidth - kPad, y + scriptH,
                             kAIScriptTextField, 0});
        y += scriptH + 6;
    }

    // "Calculate Light Mapping" button — Building/EditRigidObj only.
    if (showLightmapButton) {
        y += kRowH;  // gap line, consistent with the other button sections
        L.widgets.push_back({WidgetKind::LightmapButton, kLeft + kPad, y,
                             kLeft + kWidth - kPad, y + kBoxH, -1, 0});
        y += kBoxH + 4;
    }

    L.panel_h = (y + kPad) - kTop;
    return L;
}
```

- [ ] **Step 3: Build**

```
cmake --build build --target igi-editor --config Debug
```

Expected: build fails at the two existing `BuildLayout(...)` call sites in
`app_input_mouse.cpp:151` and `renderer_draw.cpp:1797` — they pass only 4
positional args, which is fine since `showLightmapButton` defaults to
`false`. **This should actually build successfully** (default parameter
covers the old call sites unchanged). If it does not, it means a call site
passes by a different overload resolution path — re-check the signature
edit for a typo before proceeding.

- [ ] **Step 4: Commit**

```bash
git add source/renderer/renderer.h
git commit -m "feat(ui): add LightmapButton widget kind to the property panel layout"
```

---

### Task 9: Wire the button — gate, draw, click handler

**Files:**
- Modify: `source/app_input_mouse.cpp` (pass the gate bool to `BuildLayout`,
  add click handler)
- Modify: `source/renderer/renderer_draw.cpp` (pass the gate bool to
  `BuildLayout`, draw the button)

**Interfaces:**
- Consumes: `PropPanel::WidgetKind::LightmapButton` (Task 8),
  `App::CalculateLightmapForSelectedObject()` (Task 7).
- Produces: a clickable, visible button. Terminal task for the GUI surface.

- [ ] **Step 1: Gate + click handler in `app_input_mouse.cpp`**

Change:

```cpp
						int animBoneHierarchy; std::vector<int> animIds; int animActiveId; bool animIsPlaying;
						ComputePropAnimUiState(animBoneHierarchy, animIds, animActiveId, animIsPlaying);
						PropPanel::Layout L = PropPanel::BuildLayout(schema, is_ai, children, animBoneHierarchy, animIds);
```

to:

```cpp
						int animBoneHierarchy; std::vector<int> animIds; int animActiveId; bool animIsPlaying;
						ComputePropAnimUiState(animBoneHierarchy, animIds, animActiveId, animIsPlaying);
						bool showLightmapButton = (obj.type == "Building" || obj.type == "EditRigidObj");
						PropPanel::Layout L = PropPanel::BuildLayout(schema, is_ai, children, animBoneHierarchy, animIds, showLightmapButton);
```

Then add the click handler — change:

```cpp
								if (w.kind == K::AnimIdButton) {
									if (w.comp >= 0) ToggleAnimationForObject(selected_object_index_, w.comp);
									return;
								} else if (w.kind == K::NoteBox) {
```

to:

```cpp
								if (w.kind == K::AnimIdButton) {
									if (w.comp >= 0) ToggleAnimationForObject(selected_object_index_, w.comp);
									return;
								} else if (w.kind == K::LightmapButton) {
									CalculateLightmapForSelectedObject();
									return;
								} else if (w.kind == K::NoteBox) {
```

- [ ] **Step 2: Gate + draw in `renderer_draw.cpp`**

Change:

```cpp
          PropPanel::Layout L = PropPanel::BuildLayout(schema, task_tree_view.selected_obj_is_ai, child_schemas,
                                                        task_tree_view.prop_anim_bone_hierarchy_,
                                                        task_tree_view.prop_anim_ids_);
```

to:

```cpp
          bool showLightmapButton = (obj.type == "Building" || obj.type == "EditRigidObj");
          PropPanel::Layout L = PropPanel::BuildLayout(schema, task_tree_view.selected_obj_is_ai, child_schemas,
                                                        task_tree_view.prop_anim_bone_hierarchy_,
                                                        task_tree_view.prop_anim_ids_,
                                                        showLightmapButton);
```

Then draw the button — change:

```cpp
              } else if (w.kind == K::AIScriptText) {
                  bool ed = resolveEdit(sel) &&
                            task_tree_view.prop_text_edit_field_ == PropPanel::kAIScriptTextField;
                  const char* label = task_tree_view.ai_script_dirty_
                                          ? "AI Script (modified -- save to compile):"
                                          : "AI Script:";
                  draw_text(w.x1, w.y1 - PropPanel::kRowH + 12, label,
                            task_tree_view.ai_script_dirty_ ? 1.0f : 0.8f,
                            task_tree_view.ai_script_dirty_ ? 0.6f : 0.8f,
                            task_tree_view.ai_script_dirty_ ? 0.2f : 1.0f);
                  quad(w.x1, w.y1, w.x2, w.y2, 0.0f, 0.0f, 0.0f, 0.40f);
                  border(w.x1, w.y1, w.x2, w.y2, 1.0f, ed ? 0.95f : 1.0f, ed ? 0.2f : 1.0f);
                  draw_edit_box(w, PropPanel::kAIScriptTextField,
                                task_tree_view.ai_script_text_, true,
                                task_tree_view.ai_script_vscroll_, 0);
              }
              y = w.y2 + 6;
          }
```

to:

```cpp
              } else if (w.kind == K::AIScriptText) {
                  bool ed = resolveEdit(sel) &&
                            task_tree_view.prop_text_edit_field_ == PropPanel::kAIScriptTextField;
                  const char* label = task_tree_view.ai_script_dirty_
                                          ? "AI Script (modified -- save to compile):"
                                          : "AI Script:";
                  draw_text(w.x1, w.y1 - PropPanel::kRowH + 12, label,
                            task_tree_view.ai_script_dirty_ ? 1.0f : 0.8f,
                            task_tree_view.ai_script_dirty_ ? 0.6f : 0.8f,
                            task_tree_view.ai_script_dirty_ ? 0.2f : 1.0f);
                  quad(w.x1, w.y1, w.x2, w.y2, 0.0f, 0.0f, 0.0f, 0.40f);
                  border(w.x1, w.y1, w.x2, w.y2, 1.0f, ed ? 0.95f : 1.0f, ed ? 0.2f : 1.0f);
                  draw_edit_box(w, PropPanel::kAIScriptTextField,
                                task_tree_view.ai_script_text_, true,
                                task_tree_view.ai_script_vscroll_, 0);
              } else if (w.kind == K::LightmapButton) {
                  quad(w.x1, w.y1, w.x2, w.y2, 0.0f, 0.0f, 0.0f, 0.40f);
                  border(w.x1, w.y1, w.x2, w.y2, 1.0f, 1.0f, 1.0f);
                  draw_text(w.x1 + 6, w.y1 + 12, "Calculate Light Mapping", 1.0f, 0.9f, 0.2f);
              }
              y = w.y2 + 6;
          }
```

- [ ] **Step 3: Build**

```
cmake --build build --target igi-editor --config Debug
```

Expected: builds cleanly.

- [ ] **Step 4: Manual smoke test — the full feature, end to end**

```
build\bin\Debug\igi1ed.exe
```

1. Load `D:\IGI1\missions\location0\level1` (level 1).
2. Right-click the WaterTower building (task 1104, model `435_01_1` — the
   exact model/level confirmed working against the real CLI in Task 5/7).
   The property panel opens.
3. Confirm a **"Calculate Light Mapping"** button is visible at the bottom
   of the panel (after any AI script section, since this is a `Building`
   task with no AI script — it should be the last widget).
4. Click it. Confirm:
   - `igi1ed.log` shows `[Lightmap] Resolving lightmap for model=435_01_1
     taskId=1104 ...` followed by `[Lightmap] Resolved 11 .olm file(s).`
     and `[Lightmap] Uploaded 11 lightmap texture(s) for taskId=1104`.
   - The status message line shows `Lightmap calculated: 11 texture(s)
     applied`.
   - The WaterTower's appearance visibly darkens/varies across its surface
     (the baked lightmap multiplying the diffuse texture) instead of the
     flat uniform lighting it had before.
5. Right-click a non-building object (e.g. any AI/prop). Confirm **no**
   "Calculate Light Mapping" button appears.

If step 4's visual change doesn't appear, check (in order): the shader
compiled without error (Task 3 step 6), `u_useLightmap` is actually being
set to 1 (add a temporary log if needed, then remove it), and the PNG
conversion actually produced non-empty files (check the temp dir before
`std::filesystem::remove` runs — temporarily comment out that line to
inspect).

- [ ] **Step 5: Commit**

```bash
git add source/app_input_mouse.cpp source/renderer/renderer_draw.cpp
git commit -m "feat(ui): wire the Calculate Light Mapping button into the property panel"
```

---

### Task 10: Full verification, Release build, deploy

**Files:** none (build/test/deploy only).

**Interfaces:** none — this task only verifies and ships everything from
Tasks 1-9.

- [ ] **Step 1: Run the full automated test suite (excluding the
  level-verification integration tests, which spawn `igi1ed.exe` and are
  known to hang in this environment per existing project notes — filter
  them out)**

```
cmake --build build --target igi_tests --config Debug
build\bin\Debug\igi_tests.exe --gtest_filter=-VerifyLevelIntegration.*
```

Expected: all tests pass (or SKIP for the two real-corpus fixture tests if
run on a machine without `D:\IGI1`).

- [ ] **Step 2: Build the Release configuration**

```
cmake --build build --target igi-editor --config Release
```

Expected: builds cleanly, output at `build\bin\Release\igi1ed.exe`.

- [ ] **Step 3: Copy the Release build output to `D:\IGI1`**

```
robocopy build\bin\Release D:\IGI1 igi1ed.exe freeglut.dll glew32.dll /XO
robocopy build\bin\Release\editor D:\IGI1\editor /E /XO
```

(`/XO` skips files that are already newer at the destination, so this is
safe to re-run.)

- [ ] **Step 4: Final manual verification against the deployed copy**

```
D:\IGI1\igi1ed.exe
```

Repeat Task 9 Step 4's manual smoke test against this deployed copy to
confirm the deployment is the same working build.

- [ ] **Step 5: Commit any remaining changes (e.g. version bump, if this
  project's convention is to bump a version file per feature — check
  `git log -1 --stat` on the two most recent commits for the pattern before
  deciding; if no version file changed in those commits, skip this step)**

```bash
git status
git add -A
git commit -m "chore: finalize Calculate Light Mapping feature for release"
```

(Only run this if `git status` shows tracked changes beyond what Tasks 1-9
already committed — e.g. a version string. If there is nothing to commit,
skip this step entirely rather than creating an empty commit.)
