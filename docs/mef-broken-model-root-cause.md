# MEF Broken Character Model Root Cause Notes

Date: 2026-05-25
Branch checked: `develop`

## Scope

This note documents the parser and renderer causes behind the reported broken
skeletal MEF character artifacts, and records what could be verified from the
local corpus available in this workspace.

Target models from the investigation plan:

| Model | Expected rig | Expected source |
| --- | --- | --- |
| `000_01_1` | `JonesCinematic` | `source/renderer/hardcoded_bones.h` |
| `008_01_1` | `JonesCinematic` | `source/renderer/hardcoded_bones.h` |
| `001_01_1` | `StandardSoldier` | `source/renderer/hardcoded_bones.h` |
| `003_02_1` | `StandardSoldier` | `source/renderer/hardcoded_bones.h` |
| `012_01_1` | `HeavySoldier` | `source/renderer/hardcoded_bones.h` |
| `015_01_1` | `HeavySoldier` | `source/renderer/hardcoded_bones.h` |

## Source-Confirmed Root Causes

### BUG-1: ECAF indices are treated as local indices

File: `source/parsers/mef_native.cpp`

`ParseSplitBoneTriangles()` reads Type 1 split `DNER`/`ECAF` records and pushes
triangle indices as:

```cpp
{ a + vertsOffset, c + vertsOffset, b + vertsOffset }
```

The documented split bone format says `ECAF` indices are global vertex indices,
so `vertsOffset` must not be added on this path. When a split bone record has a
non-zero `vertsOffset`, the parser points triangles at the wrong region of the
already-global vertex buffer. This directly explains duplicated limbs,
distorted faces, malformed character geometry, and texture-coordinate artifacts
that follow the wrong vertices.

This bug only affects files that actually enter the split path:

```cpp
if (modelType == 1 && ecaf && d3drInfo.valid && d3drInfo.numMeshes > 0)
```

### BUG-2: split bone `DNER` stride is hardcoded to 32 bytes

File: `source/parsers/mef_native.cpp`

`ParseSplitBoneTriangles()` currently uses:

```cpp
const size_t kBoneRecordSize = 32;
```

The project format documentation requires split Type 1 `DNER` record stride
detection:

1. `DNER.size % 32 == 0` means 32-byte records.
2. Else `DNER.size % 28 == 0` means 28-byte records.
3. Otherwise the parser should fall back to packed `DNER`.

When a split Type 1 file uses 28-byte records, walking the `DNER` chunk in
32-byte steps misaligns every later record. The symptom overlaps BUG-1:
incorrect block metadata, wrong material/vertex ranges, and malformed geometry.

### BUG-3: Type 3 UV offset is wrong, but out of scope for the six characters

File: `source/parsers/mef_native.cpp`

`ParseRenderVertices()` sets `uvOffset = 24` for `modelType == 3`. The documented
Type 3 layout has no normal field and places UV0 at offset 12. This is a level
geometry/lightmap texture issue, not a skeletal character issue.

### BUG-4: Type 3 normal read is wrong, but out of scope for the six characters

File: `source/parsers/mef_native.cpp`

`ParseRenderVertices()` reads normals at offsets 12, 16, and 20 for all model
types. Type 3 uses offset 12 for UV0, so this produces invalid normals for Type
3 level geometry. It does not explain the six Type 1 skeletal character models.

## Archetype Assignment

File: `source/renderer/hardcoded_bones.h`

The rig selection code defaults to `StandardSoldier`, switches to
`AdvancedFingerRig` only when `maxBoneIdx == 48`, and otherwise matches model
names for the cinematic and heavy rigs:

```cpp
if (maxBoneIdx == 48) AdvancedFingerRig
else if 000_01_1 / 009_02_1 / 008_01_1 JonesCinematic
else if 012_01_1 / 015_01_1 / 028_01_1 HeavySoldier
else StandardSoldier
```

`003_02_1` is not present in the named lists, so it correctly falls through to
`StandardSoldier` unless its vertex data reports `maxBoneIdx == 48`.

The verified `_V02\Bone Models\003_02_1.mef` binary has `maxBoneIdx = 30`, so
it does not hit the advanced-finger branch. `StandardSoldier` is the correct
archetype for `003_02_1` in this corpus.

## Missing Texture Compliance Gap

File: `source/renderer/renderer_objects.cpp`

Texture load failure is already logged:

```cpp
[TEX Native] Texture search FAILED for ID: ...
```

The draw path does not currently skip missing-texture submeshes. In the submesh
loop, `sub.textureID == 0` falls through to an untextured hash/material fallback
and still calls `glDrawArrays()`. This does not match the stated requirement:
"If textures are missing: skip rendering the object, add warning logs."

This is a documented renderer compliance gap. It was not changed in this pass
because the plan explicitly limits this work to root-cause documentation and
avoids unrelated renderer edits.

## Local Corpus Verification

Expected corpus path checked:

```text
%APPDATA%\QEditor\QFiles\IGI_QSC\
```

No loose files named `000_01_1.mef`, `008_01_1.mef`, `001_01_1.mef`,
`003_02_1.mef`, `012_01_1.mef`, or `015_01_1.mef` were present under the QSC
tree in this environment.

A complete six-model corpus was available at:

```text
D:\IGI-Tools\GM_123\IGI MEF CONV\Resource\IGI 1\_V02\Bone Models\
```

All six `_V02` files are Type 1, contain both `DNER` and `ECAF`, and parse
through `type1 split ECAF/DNER`. At least one split block after block 0 has
`vertsOffset > 0` in every file, so BUG-1 actively corrupts all six models when
the current `a + vertsOffset` / `c + vertsOffset` / `b + vertsOffset` logic runs.

| Model | Type | Has DNER | Has ECAF | D3DR meshes | DNER size | `%32` | `%28` | Correct stride | ECAF size | Max bone | Non-zero `vertsOffset` blocks | CLI layout |
| --- | ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `000_01_1` | 1 | yes | yes | 10 | 320 | 0 | 12 | 32 | 5868 | 30 | 9 | `type1 split ECAF/DNER` |
| `008_01_1` | 1 | yes | yes | 12 | 384 | 0 | 20 | 32 | 3732 | 30 | 11 | `type1 split ECAF/DNER` |
| `001_01_1` | 1 | yes | yes | 12 | 384 | 0 | 20 | 32 | 4164 | 30 | 11 | `type1 split ECAF/DNER` |
| `003_02_1` | 1 | yes | yes | 11 | 352 | 0 | 16 | 32 | 3684 | 30 | 10 | `type1 split ECAF/DNER` |
| `012_01_1` | 1 | yes | yes | 13 | 416 | 0 | 24 | 32 | 5304 | 30 | 12 | `type1 split ECAF/DNER` |
| `015_01_1` | 1 | yes | yes | 8 | 256 | 0 | 4 | 32 | 5544 | 30 | 7 | `type1 split ECAF/DNER` |

BUG-2 is source-confirmed but dormant for these six `_V02` files because every
verified `DNER` size is divisible by 32. A separate Type 1 split file with
`DNER.size % 28 == 0 && DNER.size % 32 != 0` is still needed to prove the
28-byte active-failure case from bytes.

Additional packed-path copies were also found in the workspace:

```text
scratch\res_test\LOCAL_models\000_01_1.mef
scratch\res_test\LOCAL_models\001_01_1.mef
scratch\res_test\LOCAL_models\015_01_1.mef
```

Those three files parse as Type 1 skeletal models, but they do not contain
`ECAF`, so they do not exercise BUG-1 or the split `DNER` stride code path. The
CLI reports `type1 packed DNER` for each.

| Model | Local path | Type | Has DNER | Has ECAF | D3DR meshes | DNER size | `%32` | `%28` | Max bone | CLI layout |
| --- | --- | ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| `000_01_1` | `scratch\res_test\LOCAL_models\000_01_1.mef` | 1 | yes | no | 10 | 6148 | 4 | 16 | 31 | `type1 packed DNER` |
| `001_01_1` | `scratch\res_test\LOCAL_models\001_01_1.mef` | 1 | yes | no | 12 | 4500 | 20 | 20 | 31 | `type1 packed DNER` |
| `015_01_1` | `scratch\res_test\LOCAL_models\015_01_1.mef` | 1 | yes | no | 8 | 5768 | 8 | 0 | 31 | `type1 packed DNER` |

## Root Cause Summary

| Symptom | Root cause | Bug ID | File |
| --- | --- | --- | --- |
| Duplicated hands | Split `ECAF` global indices receive an extra `vertsOffset` | BUG-1 | `source/parsers/mef_native.cpp` |
| Distorted faces | Split `ECAF` indices point at the wrong vertex ranges | BUG-1 | `source/parsers/mef_native.cpp` |
| Malformed geometry | BUG-1, plus BUG-2 on split files with 28-byte `DNER` records | BUG-1/BUG-2 | `source/parsers/mef_native.cpp` |
| Character texture glitches | Geometry indexes the wrong vertices/UVs | BUG-1 | `source/parsers/mef_native.cpp` |
| Missing textures still render | Renderer falls back to untextured hash/material color | Gap | `source/renderer/renderer_objects.cpp` |
| Level texture glitches | Type 3 UV offset reads UV1/lightmap instead of UV0 | BUG-3 | `source/parsers/mef_native.cpp` |

## Follow-Up Fix Scope

The character-model fix pass should be limited to `source/parsers/mef_native.cpp`:

1. In `ParseSplitBoneTriangles()`, stop adding `vertsOffset` to `ECAF` indices.
2. In `ParseSplitBoneTriangles()`, detect split `DNER` stride as 32 or 28 bytes
   before iterating records, and fall back safely when neither stride matches.

Type 3 UV/normal fixes and missing-texture renderer skipping should be separate
changes because they affect level geometry and render policy rather than the
six skeletal model corruption.
