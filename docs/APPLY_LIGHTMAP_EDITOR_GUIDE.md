# Applying Object Lightmaps in project-igi-editor — Implementation Guide

**Status**: Not yet implemented in this codebase. This document is the exact
port plan of the lightmap feature already shipped in `project-igi-conv`
(igi1conv v1.10.0), rewritten against **this editor's actual files, structs,
and rendering pipeline** (which differs architecturally from igi1conv — see
§0). Every file path, struct field, and code snippet below refers to real
code read from this repo on 2026-06-25.

---

## 0. Why This Isn't a Drag-and-Drop Port

| | `project-igi-conv` (igi1conv GUI) | `project-igi-editor` (this repo) |
|---|---|---|
| Purpose | Single-model viewer/converter | Live multi-object **level** renderer |
| Object rendering | One model loaded on demand, modern GL shader per-model | Hundreds of `LevelObject`s drawn every frame from a shared object shader (`source/renderer/renderer_objects.cpp`) |
| Vertex format | `RenderVertex{pos, uv}` → GPU buffer `pos+normal+uv` (32 bytes/vert: 8 floats) | Identical layout already: `BuildMeshFromGeometry()` in `source/renderer/model.cpp:37-90` emits 8 floats/vertex (`pos.xyz, normal.xyz, uv.xy`) |
| Shader | `#version 330` written for igi1conv's viewer | `OBJ_VERT_SRC` / `OBJ_FRAG_SRC`, inline GLSL strings in `source/renderer/renderer_objects.cpp:17-95` — **same GLSL version, same attribute layout**, so the shader edit ports directly |
| Script binding source | One-shot CLI parse of `objects.qsc` (`qsc_object_parser.cpp`, regex/string scan) | Already has a **proper recursive-descent AST parser**: `source/level/qsc_parser.{h,cpp}` (`qsc::Node` tree, `NodeKind::Call` with nested `children`) — **better foundation than igi1conv's**, no regex hacking needed |
| Existing lightmap awareness | None until v1.10.0 | `LevelObject::isNested` already has the comment *"True if this is a sub-call (like LightmapInfo inside Building)"* — `source/level/level_objects.h:72`. The nested `LightmapInfo` task is currently parsed as a generic, **discarded** nested object. Nobody reads its `"objNNNNN"` filename argument yet. |
| OLM parser | `source/parsers/lightmap_resolver.{h,cpp}`, `cmd_olm.cpp` (igi1conv) | **Does not exist in this repo.** Must be ported. |

**Bottom line**: this editor already has 2 of 4 components half-built
(vertex layout, AST parser) and is *missing* 2 (OLM binary reader, lightmap
binding extraction + GPU upload). The work is smaller than starting from
scratch, but it plugs into a level-wide render loop instead of a single
"right-click → apply" action.

---

## 1. How igi1conv Solved It (recap, for cross-reference)

Full technical writeup: `D:\Code\project-igi-conv\docs\APPLY_LIGHTMAP_GUI_GUIDE.md` §10,
and the binary format spec: `D:\Code\project-igi-conv\docs\Lightmap_docs.md`.

Three pieces, each independently testable:

1. **Script parser** — find, for a given model id, the nested `Task_New(-1,
   "LightmapInfo", ..., "objNNNNN")` that shares a `Task_New` tree with that
   model id. Produces `model_id → logical_id` (e.g. `"435_01_1" → "obj00000"`).
2. **File resolver** — `logical_id` + level dir → list of
   `lightmaps/lightmaps_unpacked/obj00000_NNNNN.olm` paths (lazily unpacking
   `lightmaps.res` if needed).
3. **MEF + GPU** — parse the model's **second UV channel** (lightmap atlas
   UV, present only when `modeltype == 3`), upload each `.olm`'s pixels as a
   GL texture, and blend `finalColor = diffuse * lightmap` in the fragment
   shader.

The model's `modelType` field (HSEM offset +32) gates everything: only
`modelType == 3` ("MODEL_LIGHTMAP", typically buildings) carries this second
UV channel and gets a binding at all.

---

## 2. What This Editor Already Has (verified by reading the code)

### 2.1 Vertex layout — `source/renderer/model.cpp`

`BuildMeshFromGeometry()` (lines 17-90+) emits a flat `float` buffer per
sub-mesh: `pos.x,pos.y,pos.z, normal.x,normal.y,normal.z, uv.x,uv.y` — 8
floats/vertex, bound to the shader's `layout(location=0/1/2)` attributes in
`OBJ_VERT_SRC`. There is **no second UV slot** in the buffer yet.

Important existing comment, `model.cpp:25-27`:
```cpp
// Type 1 (skeletal) XTRV vertices carry valid per-vertex smooth normals.
// Other types (Type 3 lightmap) store UV data in the normal field slot, so we
// fall back to the computed face normal for those.
const bool useVertexNormals = (geometry.modelType == 1);
```
This confirms modelType 3 already gets special-cased — its "normal" field is
known to be semantically different — but the second UV at byte offset +32 of
the 40-byte XTRV record is **never read**. `ParseRenderVertices()` in
`source/renderer/mef_native.cpp:225-279` only extracts `uv` at `uvOffset=24`
for every model type, including type 3 (line 238-239); the lightmap UV bytes
(`+32..+39`) are silently skipped.

### 2.2 Geometry struct — `source/renderer/mef_native.h`

```cpp
struct RenderVertex {
    glm::vec3 pos{0.f};
    glm::vec3 rawPos{0.f};
    glm::vec3 normal{0.f};
    glm::vec2 uv{0.f};
    uint16_t boneIndex{0};
    uint16_t localVertexId{0};
    float    weight{1.0f};
};
```
No `uv2` field. `ParsedGeometry::renderBlocks[i]` (line 104-109 of the same
file) already gives per-submesh `{triangleStart, triangleCount,
materialSlot, opacity}` — exactly what's needed to map render block index →
`.olm` file index (same as igi1conv).

### 2.3 Script parsing — `source/level/qsc_parser.h` + `level_objects.cpp`

This editor already has a real AST, not a regex scan:
```cpp
enum class NodeKind : uint8_t { Program, Block, ExprStmt, If, While,
    Call, Binary, Unary, IntLit, FloatLit, BoolLit, StringLit, IdentLit };

struct Node {
    NodeKind kind;
    std::string s_val;             // Call: callee name ("Task_New")
    std::vector<std::unique_ptr<Node>> children;  // Call: argument expressions
};
```
`source/level/level_objects.cpp` walks this tree and builds `LevelObject`s
(`level_objects.h:8-79`). It already special-cases `"Building"` /
`"EditRigidObj"` task types (`level_objects.cpp:107-108`) and tags nested
sub-calls with `isNested = true` (`level_objects.h:72`) — **this is where
`LightmapInfo` already lands today**, just without anyone reading its
logical-id argument.

### 2.4 Object render loop — `source/renderer/renderer_objects.cpp`

Single shared shader program (`shader_program_`, built once in `Init()`,
line 207-238) draws every `LevelObject` per frame (loop visible around lines
440-707). For each sub-mesh it sets `loc_tex`/`u_texture` (texture unit 0)
and draws (`renderer_objects.cpp:645-671`). This is the single choke point
where a second texture unit can be bound for **every** object draw call.

---

## 3. The Port Plan — Concrete Steps

### Step 1 — Add `uv2` to the vertex pipeline

**File: `source/renderer/mef_native.h`**
```cpp
struct RenderVertex {
    glm::vec3 pos{0.f};
    glm::vec3 rawPos{0.f};
    glm::vec3 normal{0.f};
    glm::vec2 uv{0.f};
    glm::vec2 uv2{0.f};          // NEW — lightmap atlas UV, modelType==3 only
    uint16_t boneIndex{0};
    uint16_t localVertexId{0};
    float    weight{1.0f};
};
```

**File: `source/renderer/mef_native.cpp`**, inside `ParseRenderVertices()`
(currently lines 225-279), add the second UV read for modelType 3 — this is
*exactly* the fix igi1conv applied (`source/parsers/mef_native.cpp` there,
commit `d2bfa0f feat: parse lightmap UV2 channel for MEF modelType 3`):

```cpp
case 3:
    vertexSize = 40;
    uvOffset   = 24;
    break;
```
becomes (add the read after the existing `uv` assignment, ~line 266-269):
```cpp
vertices[i].uv = glm::vec2(
    ReadValue<float>(bytes, base + uvOffset),
    ReadValue<float>(bytes, base + uvOffset + 4)
);

if (modelType == 3) {
    vertices[i].uv2 = glm::vec2(
        ReadValue<float>(bytes, base + 32),
        ReadValue<float>(bytes, base + 36)
    );
}
```
This mirrors `Lightmap_docs.md` §4 ("XTRV Chunk Stride... offset +32:
Lightmap Atlas UV").

### Step 2 — Carry `uv2` into the GPU buffer

**File: `source/renderer/model.cpp`**, `BuildMeshFromGeometry()` currently
reserves `triangleCount * 3 * 8` floats/triangle (pos3+normal3+uv2). Bump to
10 and append `uv2` per vertex when `geometry.modelType == 3`:
```cpp
verts.reserve(triangleCount * 3 * 10);   // was 8
...
verts.push_back(v.uv2.x);
verts.push_back(v.uv2.y);
```
For non-type-3 models, write `(0,0)` for the extra two floats so the vertex
stride stays uniform — simplest, avoids a second VAO layout.

**Add attribute 3** to the VAO setup (wherever `glVertexAttribPointer` for
location 2 (`uv`) is currently configured for sub-meshes — search
`model.cpp` for `glVertexAttribPointer(2,`):
```cpp
glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));
glEnableVertexAttribArray(3);
```

### Step 3 — Add `uv2` to the object shader

**File: `source/renderer/renderer_objects.cpp`**, `OBJ_VERT_SRC` (lines
17-44):
```glsl
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec2 a_uv2;     // NEW

out vec2 v_uv2;
...
void main() {
    ...
    v_uv  = a_uv;
    v_uv2 = a_uv2;
    ...
}
```

`OBJ_FRAG_SRC` (lines 46-95) — add a second sampler and multiply-blend,
gated by a new uniform so non-lightmapped objects render exactly as before:
```glsl
in vec2 v_uv2;
uniform sampler2D u_lightmap;
uniform int u_useLightmap;   // NEW — 0 = no lightmap bound for this submesh
...
vec4 texColor = (u_useTexture != 0) ? texture(u_texture, v_uv) : u_baseColor;

if (u_useLightmap != 0) {
    vec4 lmColor = texture(u_lightmap, v_uv2);
    texColor.rgb *= lmColor.rgb;     // multiply blend — same formula as igi1conv
}
...
fragColor = vec4(light * texColor.rgb * u_tint, finalAlpha);
```
Query the new uniform locations next to the existing ones (wherever
`loc_useTex`/`loc_tex` are queried via `glGetUniformLocation`, e.g.
`renderer_objects.h:170-171` parameter list and its `.cpp` definition).

### Step 4 — Port the OLM binary reader

This repo has **no OLM parser at all**. Port `cmd_olm.cpp`'s read logic
(igi1conv) into a new pair of files, e.g.
`source/renderer/olm_parser.{h,cpp}`, using the binary layout documented in
`D:\Code\project-igi-conv\docs\Lightmap_docs.md` §2.3-2.6 (88-byte main
header + 16-byte IGI1 layer descriptor + raw RGBA pixels):

```cpp
// source/renderer/olm_parser.h
struct OlmImage {
    uint16_t width = 0, height = 0;
    std::vector<uint8_t> rgba;   // width*height*4 bytes
};
std::optional<OlmImage> ParseOlmFile(const std::string& path);
```
```cpp
// source/renderer/olm_parser.cpp (core read — IGI1 single-layer only,
// matches the "zero multi-layer files observed" finding in Lightmap_docs.md §2.2)
std::optional<OlmImage> ParseOlmFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    if (bytes.size() < 88 + 16) return std::nullopt;

    auto rd_u16 = [&](size_t o){ uint16_t v; std::memcpy(&v, &bytes[o], 2); return v; };
    uint16_t w = rd_u16(88 + 12);   // layer descriptor +0x0C
    uint16_t h = rd_u16(88 + 14);   // layer descriptor +0x0E
    size_t pixelsStart = 88 + 16;   // IGI1: header(88) + layer desc(16)
    size_t need = static_cast<size_t>(w) * h * 4;
    if (bytes.size() < pixelsStart + need) return std::nullopt;

    OlmImage img;
    img.width = w; img.height = h;
    img.rgba.assign(bytes.begin() + pixelsStart, bytes.begin() + pixelsStart + need);
    return img;
}
```
Upload via the editor's existing texture helper (`GL_RegisterTexture` is
already used for `.tex` files in `renderer_objects_texture.cpp:363` — reuse
the same function with `GL_RGBA` source data instead of routing through
`Tex_Load`/`pic_s`).

### Step 5 — Extract the `LightmapInfo` binding from the AST

The AST already exists; this step only needs a **reader**, not a new
parser. Add a function (e.g. in a new `source/level/lightmap_binding.{h,cpp}`)
that walks `qsc::Node` the same way `level_objects.cpp` already does for
`Task_New`, but instead of building a `LevelObject`, returns:

```cpp
struct LightmapBinding {
    std::string modelId;     // e.g. "435_01_1"
    std::string logicalId;   // e.g. "obj00000" — LightmapInfo's last string arg
    int taskId = -1;
    glm::dvec3 pos{0.0};
};
std::vector<LightmapBinding> ExtractLightmapBindings(const qsc::Node& program);
```

Algorithm (mirrors `qsc_object_parser.cpp`'s `LightmapBindingSet::parse` in
igi1conv, adapted to a real AST instead of token scanning):
1. Walk every top-level `Call` node named `"Task_New"`.
2. Recursively scan its `children` (the args) for nested `Call` nodes.
3. Within that subtree, find any `StringLit` matching a target model id, AND
   a nested `Call` named `"Task_New"` whose first string arg is
   `"LightmapInfo"`.
4. `LightmapInfo`'s **last** argument (a `StringLit`) is the logical id.
5. Record `{modelId, logicalId, taskId, pos}` for that tree.

This is strictly easier here than in igi1conv because `qsc::Node` already
gives you typed children instead of opaque token blobs — no new lexing.

### Step 6 — Resolve logical id → `.olm` file paths

Small new helper, e.g. `source/level/lightmap_resolver.{h,cpp}`:
```cpp
std::vector<std::string> ResolveOlmFiles(const std::string& levelDir,
                                          const std::string& logicalId);
```
Mirrors igi1conv's `lightmap_resolver.cpp`:
1. Check `<levelDir>/lightmaps/lightmaps_unpacked/` exists.
2. If not, but `<levelDir>/lightmaps/lightmaps.res` exists, unpack it. This
   editor already has a `.res` reader/writer (`source/renderer/res_compiler.cpp`,
   `res_writer.cpp`) — reuse the unpack path from there rather than writing
   a new one.
3. Glob `<logicalId>_*.olm`, sort by the numeric suffix.

### Step 7 — Wire it into the per-object render path

In `Renderer_Objects::GetOrLoadMesh()` (`source/renderer/renderer_objects_mesh.cpp:26-110`),
after the existing `ApplyTexturesToMesh(mesh, modelId)` call (line 56), add:
```cpp
if (geometry.modelType == 3) {   // gate on MODEL_LIGHTMAP, same as igi1conv
    ApplyLightmapToMesh(mesh, modelId, obj /* or its bound task id/pos */);
}
```
`ApplyLightmapToMesh` looks up the model's `LightmapBinding` (built once per
level load — see Step 8), resolves its `.olm` files, uploads them as GL
textures, and assigns `SubMesh::lightmapTextureID` per render block by index
(matching igi1conv's "renderBlocks[i] → olm file i" rule, with the same
fallback-to-file-0 if counts mismatch).

**File: `source/renderer/model.h`** — add to `SubMesh`:
```cpp
struct SubMesh {
    GLuint VAO = 0;
    GLuint VBO = 0;
    int    vertexCount = 0;
    GLuint textureID   = 0;
    GLuint lightmapTextureID = 0;   // NEW — 0 = no lightmap for this block
    int    alphaMode   = 0;
    glm::vec4 baseColorFactor = glm::vec4(1.0f);
    int    materialSlot = -1;
};
```

In the render loop (`renderer_objects.cpp:645-671`), right next to the
existing texture-unit-0 bind, bind unit 1 when present:
```cpp
if (sub.lightmapTextureID > 0) {
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, sub.lightmapTextureID);
    glUniform1i(loc_lightmap, 1);
    glUniform1i(loc_useLightmap, 1);
} else {
    glUniform1i(loc_useLightmap, 0);
}
```

### Step 8 — Where bindings get built (per level load, not per frame)

The level already has a one-time load pass (`LevelObjects::Load(...)` in
`source/level/level_objects.cpp`). Add a single call there:
```cpp
g_lightmapBindings = ExtractLightmapBindings(*qsc_program_ast);
```
storing a `model_id (+ disambiguating task id/pos, same rule as igi1conv's
`lightmap resolve --task-id/--pos`) → LightmapBinding` map for the whole
level, computed once. `GetOrLoadMesh` then just looks up this map — no
per-frame re-parsing, no per-object picker dialog needed (unlike igi1conv's
GUI), because **the level already knows every object's task id and world
position** — disambiguation is automatic and exact, not a user choice.

---

## 4. Disambiguation Is Automatic Here (Editor Advantage)

In igi1conv's GUI, a `.mef` model viewed in isolation doesn't know which
of its N level placements you mean — hence the picker dialog
(`OLM_LIGHTMAP_CLI_GUIDE.md` §4, `APPLY_LIGHTMAP_GUI_GUIDE.md` §6).

**This editor never has that ambiguity.** Every `LevelObject` already
carries its own `taskId` and `pos` (`level_objects.h:8,28-29`). When
resolving a binding for a specific object instance, match the
`LightmapBinding` whose `taskId` equals that *exact* object's `taskId` (the
binding extraction in Step 5 should key off task id, not just model id) —
no UI is needed at all. This is strictly simpler than the GUI converter's
flow.

---

## 5. Testing Plan (mirrors igi1conv's, adapted to this repo's test layout)

This repo's tests live in `tests/*.cpp` (gtest, see `D:\Code\project-igi-editor\tests\`).
Add, following existing naming (`test_qsc_parser.cpp`, `test_mtp_parser.cpp`, etc.):

- `tests/test_olm_parser.cpp` — parse a known `.olm` from the real IGI1
  corpus, assert width/height/pixel count. (Same corpus path used by
  igi1conv: `D:\IGI1\missions\location0\level1\lightmaps\lightmaps_unpacked\`.)
- `tests/test_lightmap_binding.cpp` — feed a real decompiled `objects.qsc`
  through `qsc::Parse()` then `ExtractLightmapBindings()`; assert the
  WaterTower → `obj00000` chain resolves (same fixture igi1conv's
  `test_lightmap_resolver.cpp` uses).
- Extend `tests/test_res_parser.cpp` coverage if `lightmaps.res` unpacking
  reuses/extends the existing `.res` reader.
- GPU blend itself is not unit-testable — verify visually by launching the
  editor against a real level and comparing a building's appearance before
  and after the change (same caveat igi1conv's plan noted:
  "GUI/shader rendering verified manually... not unit-testable").

---

## 6. File Checklist (new + modified)

| File | Change |
|---|---|
| `source/renderer/mef_native.h` | Add `RenderVertex::uv2` |
| `source/renderer/mef_native.cpp` | Read `uv2` for `modelType == 3` in `ParseRenderVertices()` |
| `source/renderer/model.h` | Add `SubMesh::lightmapTextureID` |
| `source/renderer/model.cpp` | Emit `uv2` into vertex buffer; add VAO attribute 3 |
| `source/renderer/renderer_objects.cpp` | `OBJ_VERT_SRC`/`OBJ_FRAG_SRC`: add `a_uv2`/`v_uv2`, `u_lightmap`, `u_useLightmap`; bind texture unit 1 in the draw loop |
| `source/renderer/renderer_objects.h` | Add `loc_lightmap`/`loc_useLightmap` uniform location members |
| `source/renderer/olm_parser.h` / `.cpp` | **New** — binary `.olm` reader (port of igi1conv `cmd_olm.cpp`) |
| `source/level/lightmap_binding.h` / `.cpp` | **New** — AST walk extracting `model_id/task_id → logical_id` |
| `source/level/lightmap_resolver.h` / `.cpp` | **New** — `logical_id` → `.olm` file paths, reusing `.res` unpack logic |
| `source/level/level_objects.cpp` | Call `ExtractLightmapBindings()` once per level load |
| `source/renderer/renderer_objects_mesh.cpp` | Call lightmap-apply step in `GetOrLoadMesh()` for `modelType == 3` |
| `tests/test_olm_parser.cpp` | **New** |
| `tests/test_lightmap_binding.cpp` | **New** |

---

## 7. References

- Binary format spec (authoritative): `D:\Code\project-igi-conv\docs\Lightmap_docs.md`
- igi1conv CLI workflow this mirrors conceptually: `D:\Code\project-igi-conv\docs\OLM_LIGHTMAP_CLI_GUIDE.md`
- igi1conv GUI feature + "how we solved it" writeup: `D:\Code\project-igi-conv\docs\APPLY_LIGHTMAP_GUI_GUIDE.md`
- igi1conv commit history for this feature (`project-igi-conv` repo, `git log --oneline`):
  `ac46951` parse bindings from objects.qsc → `09bd952` resolve .olm files →
  `d2bfa0f` parse UV2 → `23a0cd3` GUI action + shader blend →
  `4d9af55` fix binding-tree scope → `650d36d` CLI commands → `d1620a4` CLI docs
