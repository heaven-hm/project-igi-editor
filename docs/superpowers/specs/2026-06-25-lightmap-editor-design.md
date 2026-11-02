# "Calculate Light Mapping" Editor Button — Design

**Status**: Approved. Branch: `feat/lightmap-editor` (off `feat/animation-system`).

## 1. Goal

Right-clicking a placed building (`Building`/`EditRigidObj` task) opens the
existing property editor panel. Add a **"Calculate Light Mapping"** button
there. Clicking it resolves and applies that exact placement's baked
lightmap onto the building's 3D model in the editor's live viewport.

## 2. Architecture Decision: Shell Out, Don't Port

`docs/APPLY_LIGHTMAP_EDITOR_GUIDE.md` (the original survey doc) proposed
porting igi1conv's OLM binary parser and QSC AST binding-extractor into this
editor's C++. That porting work is **not needed**: the bundled
`assets/editor/tools/igi1conv/igi1conv.exe` already ships `lightmap
list/resolve` and `olm info/to-png/to-tga` subcommands (verified working by
running `--help` against the actual bundled exe). This editor has an
established convention for this — `source/utils_igi1conv.h`:

> "All file conversion in the editor MUST go through this helper instead of
> in-process parsers."

So this feature follows that convention: new wrapper functions in
`utils_igi1conv.h/.cpp`, no new binary/script parser code.

What **cannot** be shelled out — it's per-frame GPU state — is the
rendering-side plumbing: a second UV channel, a second texture sampler, and
the shader blend. Those are ported in-process, matching the original guide's
Steps 1–3 exactly (verified by reading the current parser: byte offset
+32..+39 of the 40-byte XTRV record for `modelType==3` is read by nothing
today).

## 3. Per-Instance Correctness

`Renderer_Objects::GetOrLoadMesh()` caches `Mesh` **per modelId**, shared by
every placement of that model in the level. The original guide's plan
(`SubMesh::lightmapTextureID` baked into the cached mesh) would make two
placements of the same building model show whichever lightmap was
calculated last — wrong whenever a model is reused.

Fix: lightmap textures are **not** stored on `SubMesh`. They live in a new
`Renderer_Objects` member:

```cpp
std::unordered_map<std::string /*taskId*/, std::vector<GLuint>> lightmap_textures_by_task_;
```

The draw loop looks this up using each object's own `taskId` at bind time,
independent of the shared mesh cache. Recalculating a placement's lightmap
replaces (and `glDeleteTextures`s) only that taskId's entry.

## 4. No Ambiguity, No Picker Dialog

`LevelObject` already carries the exact `taskId` and `pos` of the selected
placement, and `Level` already tracks the current level's `qsc_path_`
(levels work from `.qsc` directly — no decompile step needed). The button
always calls:

```
igi1conv lightmap resolve --model <modelId> --qsc <qsc_path_> --task-id <obj.taskId>
```

`--task-id` is always known and always passed, so the CLI's ambiguous-match
exit code (4) never triggers in this flow. No disambiguation UI is needed
(this is the editor's structural advantage over igi1conv's standalone GUI,
which can't know which placement a `.mef` viewed in isolation refers to).

## 5. Pipeline (triggered by button click)

`App::CalculateLightmapForSelectedObject()`:

1. Guard: selected object's schema type must be `Building`/`EditRigidObj`
   (same gate as the button's visibility). Log + no-op otherwise.
2. `igi1conv::LightmapResolve(modelId, qscPath, taskId, err)` → new wrapper
   in `utils_igi1conv.{h,cpp}`, parses stdout for the printed `.olm` file
   paths (text format already fixed by the real CLI — confirmed by running
   `lightmap resolve --help`). On exit code 3 (no binding/no files), log and
   stop — this object simply has no baked lightmap.
3. For each `.olm` path: `igi1conv::OlmToPng(olmPath, tempPngPath, err)` —
   new wrapper, writes to a temp path via the existing
   `igi1conv::MakeTempPath()` helper.
4. Load each resulting PNG through the **existing** texture path
   (`Tex_Load` → `GL_RegisterTexture`, same call already used for regular
   object textures) to get a `GLuint`.
5. Free any previously-stored textures for this `taskId`, store the new
   vector in `lightmap_textures_by_task_[taskId]`.
6. Log every step (`Log::Info` for each command run and texture loaded,
   `Log::Error` on failure) — addresses the "add more logs" requirement.

## 6. Rendering Changes

| File | Change |
|---|---|
| `source/renderer/mef_native.h` | Add `RenderVertex::uv2` (`glm::vec2`) |
| `source/renderer/mef_native.cpp` | In `ParseRenderVertices()`, for `modelType==3` read `uv2` from `base+32`/`base+36` (currently dead bytes for that case) |
| `source/renderer/model.h` | No `SubMesh` change (lightmap textures live in `Renderer_Objects`, not on `SubMesh` — see §3) |
| `source/renderer/model.cpp` | `BuildMeshFromGeometry()`: pack 10 floats/vertex (was 8) when `modelType==3` (0,0 padding otherwise to keep stride uniform); add VAO attribute slot 3 for `uv2` |
| `source/renderer/renderer_objects.cpp` | `OBJ_VERT_SRC`: add `a_uv2` (location 3) / `v_uv2`. `OBJ_FRAG_SRC`: add `uniform sampler2D u_lightmap; uniform int u_useLightmap;`, multiply-blend `texColor.rgb *= texture(u_lightmap, v_uv2).rgb` when set. Draw loop: bind texture unit 1 from `lightmap_textures_by_task_[obj.taskId][i]` when present, else `u_useLightmap=0` |
| `source/renderer/renderer_objects.h` | Add `loc_lightmap`/`loc_useLightmap` uniform locations; add the `lightmap_textures_by_task_` map and a method to set/clear/query it |

## 7. GUI Changes

| File | Change |
|---|---|
| `source/renderer/renderer.h` (`PropPanel`) | New `WidgetKind::LightmapButton`; in `BuildLayout()`, emit it (same button-row pattern as `SnapGround`/`SnapObject`) only when the task type is `Building`/`EditRigidObj` |
| `source/renderer/renderer_draw.cpp` | Draw the new widget kind, reusing the existing button-draw code path (border/quad/label, same as `AnimIdButton`/`SnapGround`) |
| `source/app_input_mouse.cpp` | New case in the widget-click switch calling `App::CalculateLightmapForSelectedObject()` |
| `source/app.h` / `source/app_*.cpp` | New method `CalculateLightmapForSelectedObject()` implementing §5 |

## 8. New `utils_igi1conv` Wrappers

```cpp
// igi1conv lightmap resolve --model <id> --qsc <path> --task-id <id>
// Parses stdout for printed .olm file paths.
std::vector<std::string> LightmapResolve(const std::string& modelId,
                                          const std::string& qscPath,
                                          const std::string& taskId,
                                          std::string& err);

// igi1conv olm to-png <input.olm> -o <out.png>
bool OlmToPng(const std::string& olmPath, const std::string& outPng, std::string& err);
```

(`LightmapList`/`OlmInfo` are not needed for this flow — `--task-id` removes
the need to list candidates — but may be added later if a "list bindings"
debug view is wanted.)

## 9. Tests

- `tests/test_mef_native_lightmap.cpp` — synthetic 40-byte XTRV vertex
  bytes, modelType 3, assert `uv2` reads the expected floats from offset
  +32/+36 and that modelType 0/1 are unaffected.
- `tests/test_igi1conv_lightmap.cpp` — feed the real CLI's documented stdout
  format (`lightmap: resolved   task N "Name" -> objNNNNN @ (...)` /
  `lightmap: N .olm file(s): <paths...>`, and the exit-3 "no .olm files on
  disk" case) into the new parser function; assert correct path extraction
  and error propagation. No process spawn in this test — pure string
  parsing, fed fixed sample text.

## 10. Workflow

Small commits per logical step (UV2 plumbing → shader → utils_igi1conv
wrappers + tests → App pipeline method → GUI button/layout/click handling →
draw-loop binding → final polish/logs). Build and smoke-test after each
step; fix any error before moving on. Final Release build, copy build
output to `D:\IGI1` (existing deployment target per project memory).

## 11. Out of Scope

- Multi-model batch "calculate lightmaps for entire level" (button is
  single-selected-object only, per the request).
- A "list all placements" debug picker UI (not needed — see §4).
- Caching calculated lightmaps to disk across editor restarts (in-memory
  GL textures only, recalculated on demand via the button).
