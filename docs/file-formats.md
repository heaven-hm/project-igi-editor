# IGI 1 File Format Reference

> Technical documentation of the binary file formats used by *Project I.G.I.: I'm Going In* (2000, Innerloop Studios).
> Derived from the parser implementations in [project-igi-editor](https://github.com/Jones-HM/project-igi-editor).

---

## Table of Contents

1. [ILFF Container](#1-ilff-container)
2. [MEF -- 3D Model](#2-mef----3d-model)
3. [TEX -- Texture](#3-tex----texture)
4. [QVM -- Script Bytecode](#4-qvm----script-bytecode)
5. [RES -- Resource Archive](#5-res----resource-archive)
6. [MTP -- Model-Texture Package](#6-mtp----model-texture-package)
7. [DAT -- Asset List](#7-dat----asset-list)
8. [QSC -- Script Source](#8-qsc----script-source)

---

## 1. ILFF Container

ILFF ("Innerloop File Format") is the shared binary container used by MEF and RES files. It consists of a 20-byte file header followed by a linked chain of 16-byte chunks.

### 1.1 File Header (20 bytes)

| Offset | Size | Type     | Description                               |
|--------|------|----------|-------------------------------------------|
| 0x00   | 4    | char[4]  | Magic: `"ILFF"` (ASCII, 0x494C4646)      |
| 0x04   | 4    | uint32   | Total file size (must equal actual size)  |
| 0x08   | 4    | uint32   | Alignment (always 4)                      |
| 0x0C   | 4    | uint32   | Skip (always 0 in the outer header)       |
| 0x10   | 4    | char[4]  | Format ID (e.g. `"HSEM"` for MEF, `"IRES"` for RES) |

### 1.2 Chunk Header (16 bytes)

Each chunk inside the ILFF container begins with a 16-byte header:

| Offset | Size | Type     | Description                                         |
|--------|------|----------|-----------------------------------------------------|
| 0x00   | 4    | char[4]  | FourCC chunk identifier (e.g. `"XTRV"`, `"DNER"`)  |
| 0x04   | 4    | uint32   | Data size (bytes following this header)             |
| 0x08   | 4    | uint32   | Alignment                                           |
| 0x0C   | 4    | uint32   | Skip (offset from chunk start to next chunk; 0 = last chunk) |

**Chunk linking:** If `skip == 0`, the chunk is the last in the chain and data follows immediately. Otherwise, the next chunk header begins at `chunk_start + skip`. This linked-list approach allows the engine to skip unknown chunks efficiently.

### 1.3 Example: Minimal ILFF file

```
Offset  Hex                                         ASCII
0x0000  49 4C 46 46  00 01 00 00  04 00 00 00  00 00 00 00   ILFF............
0x0010  48 53 45 4D                                           HSEM
0x0014  [chunk headers and data follow]
```

---

## 2. MEF -- 3D Model

**Extension:** `.mef`
**Container:** ILFF with format ID `"HSEM"` (at offset 0x10)

MEF files store 3D geometry for buildings, props, characters, and vehicles. The game uses a Z-up coordinate system. Vertex positions in MEF files are stored in native game units; the editor applies a scale factor of **1.0 / 40.96** when importing to convert to meters.

```c
constexpr float kNativeMefImportScale = 1.0f / 40.96f;
```

### 2.1 Chunk Overview

A typical MEF file contains these chunks (order may vary):

| FourCC | Name                | Description                              |
|--------|---------------------|------------------------------------------|
| `HSEM` | Mesh Info           | Model metadata including model type      |
| `XTRV` | Vertices (render)   | Interleaved vertex data for rendering    |
| `DNER` | Render blocks       | Triangle indices + per-block metadata    |
| `ECAF` | Face indices         | Separate index buffer (used by Type 1)   |
| `D3DR` | D3D Render info     | Face/mesh/vertex counts                  |
| `XTVC` | Collision vertices  | Collision mesh vertices (16 bytes each)  |
| `ECFC` | Collision faces     | Collision mesh face indices (8 bytes each)|

### 2.2 HSEM Chunk -- Mesh Info

The HSEM chunk contains model metadata. The model type field is critical for determining vertex layout.

| Offset (in chunk data) | Size | Type   | Description           |
|------------------------|------|--------|-----------------------|
| 0x00                   | 32   | -      | Reserved / unknown    |
| 0x20                   | 4    | uint32 | **Model type** (0, 1, or 3) |

The chunk must be at least 36 bytes to contain the model type field.

### 2.3 Model Types and Vertex Layouts

#### Type 0 -- Rigid Model (32-byte vertex)

Used for static props and buildings with no skeletal animation.

| Byte Offset | Size | Type    | Field         |
|-------------|------|---------|---------------|
| 0x00        | 4    | float32 | Position X    |
| 0x04        | 4    | float32 | Position Y    |
| 0x08        | 4    | float32 | Position Z    |
| 0x0C        | 4    | float32 | Normal X      |
| 0x10        | 4    | float32 | Normal Y      |
| 0x14        | 4    | float32 | Normal Z      |
| 0x18        | 4    | float32 | UV U          |
| 0x1C        | 4    | float32 | UV V          |

**Total: 32 bytes per vertex.** UV offset within vertex: 24.

#### Type 1 -- Bone/Skeletal Model (40-byte vertex)

Used for animated characters and objects with skeletal rigs.

| Byte Offset | Size | Type    | Field             |
|-------------|------|---------|-------------------|
| 0x00        | 4    | float32 | Position X        |
| 0x04        | 4    | float32 | Position Y        |
| 0x08        | 4    | float32 | Position Z        |
| 0x0C        | 4    | float32 | Normal X          |
| 0x10        | 4    | float32 | Normal Y          |
| 0x14        | 4    | float32 | Normal Z          |
| 0x18        | 4    | float32 | UV0 U             |
| 0x1C        | 4    | float32 | UV0 V             |
| 0x20        | 4    | float32 | UV1 U / bone data |
| 0x24        | 4    | float32 | UV1 V / bone data |

**Total: 40 bytes per vertex.** Primary UV offset within vertex: 24.

#### Type 3 -- Lightmap Model (40-byte vertex)

Used for level geometry with lightmap UVs.

| Byte Offset | Size | Type    | Field              |
|-------------|------|---------|-------------------|
| 0x00        | 4    | float32 | Position X         |
| 0x04        | 4    | float32 | Position Y         |
| 0x08        | 4    | float32 | Position Z         |
| 0x0C        | 4    | float32 | UV0 U (primary)    |
| 0x10        | 4    | float32 | UV0 V (primary)    |
| 0x14        | 4    | float32 | UV1 U (lightmap)   |
| 0x18        | 4    | float32 | UV1 V (lightmap)   |
| 0x1C        | 12   | -       | Additional data    |

**Total: 40 bytes per vertex.** Primary UV offset within vertex: 12.

#### Summary Table

| Model Type | Vertex Size | UV Offset | Description |
|------------|-------------|-----------|-------------|
| 0          | 32 bytes    | 24        | Rigid (pos + normal + uv) |
| 1          | 40 bytes    | 24        | Bone/skeletal (pos + normal + uv0 + uv1) |
| 3          | 40 bytes    | 12        | Lightmap (pos + uv0 + uv1 + ...) |

The vertex count is determined by: `XTRV.chunk_data_size / vertex_size`.

### 2.4 D3DR Chunk -- Render Info

The D3DR chunk stores face, mesh, and vertex counts. Its layout depends on the model type.

#### Type 0 (Rigid) -- D3DR Layout

| Offset | Size | Type   | Field       |
|--------|------|--------|-------------|
| 0x00   | 4    | uint32 | (unknown)   |
| 0x04   | 4    | uint32 | numFaces    |
| 0x08   | 4    | uint32 | numMeshes   |
| 0x0C   | 4    | uint32 | numVerts    |

Minimum chunk size: 16 bytes.

#### Type 1 (Bone) -- D3DR Layout

| Offset | Size | Type   | Field       |
|--------|------|--------|-------------|
| 0x00   | 4    | uint32 | (unknown)   |
| 0x04   | 4    | uint32 | numFaces    |
| 0x08   | 4    | uint32 | numMeshes   |
| 0x0C   | 4    | uint32 | verts0      |
| 0x10   | 4    | uint32 | verts1      |
| 0x14   | 4    | uint32 | numVerts    |

Minimum chunk size: 24 bytes.

#### Type 3 (Lightmap) -- D3DR Layout

| Offset | Size | Type   | Field       |
|--------|------|--------|-------------|
| 0x00   | 4    | uint32 | (unknown)   |
| 0x04   | 4    | uint32 | (unknown)   |
| 0x08   | 4    | uint32 | numFaces    |
| 0x0C   | 4    | uint32 | numMeshes   |
| 0x10   | 4    | uint32 | numVerts    |

Minimum chunk size: 20 bytes.

### 2.5 DNER Chunk -- Render Blocks / Triangle Indices

The DNER chunk encodes triangle indices organized into render blocks (one per material/submesh). There are two parsing strategies depending on the model type.

#### 2.5.1 Packed DNER (Type 0 and Type 3, fallback for Type 1)

Each render block has a variable-length header followed by packed uint16 index triples.

**Block header (28 bytes for Type 0/1, 32 bytes for Type 3):**

| Offset | Size | Type   | Field                    | Notes                    |
|--------|------|--------|--------------------------|--------------------------|
| 0x00   | 12   | -      | Reserved/unknown         |                          |
| 0x0C   | 2    | int16  | indexCount               | Number of uint16 indices |
| 0x0E   | 2    | int16  | nextoffs                 | -1 = last block          |
| 0x10   | 2    | int16  | materialSlot (Type 3)    | Only present in Type 3   |
| 0x12   | 2    | uint16 | vertsOffset              | Base vertex offset       |
| 0x14   | 2    | uint16 | vertsCount               | Vertex count for block   |

For Type 0/1, `materialSlot` is the block's sequential index. For Type 0/1, `vertsOffset` is at byte 18 and `vertsCount` at byte 20 (0-indexed within the block header).

**Index data:** Immediately following the header, `indexCount` uint16 values are packed. Every 3 consecutive indices define one triangle. The final vertex index for each triangle is `vertsOffset + local_index`.

The linked iteration terminates when `nextoffs == -1`.

#### 2.5.2 Split Bone DNER (Type 1 with ECAF)

When a Type 1 model has both DNER and ECAF chunks, the DNER chunk contains fixed-size bone records (32 or 28 bytes each), and the ECAF chunk contains the actual triangle indices.

**DNER bone record (28-byte and 32-byte variants):**

| Offset (28-byte) | Offset (32-byte) | Size | Type    | Field        | Description                                  |
|------------------|------------------|------|---------|--------------|----------------------------------------------|
| 0x00             | 0x00             | 12   | float32 | `px, py, pz` | Joint rest-pose local position translation  |
| 0x0C             | 0x0C             | 2    | uint16  | `numFace`    | Index count (in bytes: `numFace * 2` index)   |
| 0x0E             | 0x0E             | 2    | int16   | `skip`       | Offset index                                 |
| -                | 0x10             | 2    | int16   | `td`         | Texture descriptor index                     |
| 0x10             | 0x12             | 2    | uint16  | `offVerts`   | Base vertex offset                           |
| 0x12             | 0x14             | 2    | uint16  | `numVerts`   | Vertex count in this bone submesh            |
| 0x14             | 0x16             | 2    | uint16  | `rawOpacity` | Base material opacity index                  |
| 0x16             | 0x18             | 1    | uint8   | `eflame`     | Emissive flame multiplier                    |
| 0x17             | 0x19             | 1    | uint8   | `mshine`     | Material shininess factor                    |
| 0x18             | 0x1A             | 1    | uint8   | `scolor`     | Specular color (or diffuse color index)      |
| 0x19             | 0x1B             | 1    | uint8   | `opacitd`    | Opacity detail                               |
| -                | 0x1C             | 4    | uint32  | `_0`         | Padding/alignment zero bytes                 |

* **Record stride detection:** The record size is auto-detected: if `DNER.size % 32 == 0` use 32-byte records; else if `DNER.size % 28 == 0` use 28-byte records; otherwise fall back to packed DNER parsing.

* **ECAF index buffer:** A flat array of uint16 values. For each bone record, read `numFace` indices starting at `ECAF.data + offsetIndex * 2`. Indices in ECAF are **global** vertex indices (no vertsOffset addition needed).

#### 2.5.3 Skeletal Archetypes & The "Missing Skeleton" Problem

A major limitation of the original binary `.mef` file format for skeletal models (`modelType == 1`) is that it **completely lacks skeletal tree hierarchy definitions**. The binary files only contain vertex blending weight tables (mapping which vertex is influenced by which raw bone index). They do not store:
1. **Bone Names** (e.g. `"head"`, `"left hand"`, `"shoulders"`).
2. **Parent-Child Linkages** (which joints are connected to which, which is necessary for forward kinematics and joint rotations).
3. **Rest-Pose Bone Lengths/Offsets** (`px, py, pz`).

To solve this, the game engine originally compiled the default character skeletons directly into the executable binary (`igi.exe`/`loop.dll`). The Level Editor restores these missing hierarchies at runtime by matching model filenames or the maximum bone counts to hardcoded **Skeletal Archetypes** via the `BoneRigType` enum:

```cpp
enum class BoneRigType {
    JonesCinematic = 0,    // Type 0: David Jones and key cinematic actors
    StandardSoldier = 1,   // Type 1: Default soldiers and player model
    HeavySoldier = 6,      // Type 6: Heavy/special soldier models
    AdvancedFingerRig = 48 // Type 48: Character models with advanced hand rig
};
```

* **JonesCinematic (Type 0):** A 32-bone skeletal rig with custom bone proportions tailored for David Jones (`000_01_1`), `009_02_1`, and `008_01_1` models.
* **StandardSoldier (Type 1):** The default 32-bone rig used by the player model (`001_01_1`) and standard combat NPC AI soldiers.
* **HeavySoldier (Type 6):** An adjusted 32-bone rig with wider shoulder and elbow structures designed to prevent heavy armor plates from clipping on special guards (`012_01_1`, `015_01_1`, `028_01_1`).
* **AdvancedFingerRig (Type 48):** Selected automatically when `maxBoneIdx` matches `48`, mapping a highly articulate 48-bone hand and attachment skeleton.

### 2.6 ECAF Chunk -- Face Index Buffer

A flat array of `uint16` triangle indices, used by Type 1 (bone) models with split DNER parsing. Total index count: `ECAF.size / 2`.

### 2.7 Collision Mesh Chunks

When no render geometry is available (no XTRV/DNER), the parser falls back to collision geometry.

#### XTVC -- Collision Vertices (16 bytes each)

| Offset | Size | Type    | Field      |
|--------|------|---------|------------|
| 0x00   | 4    | float32 | Position X |
| 0x04   | 4    | float32 | Position Y |
| 0x08   | 4    | float32 | Position Z |
| 0x0C   | 4    | -       | Padding    |

Vertex count: `XTVC.size / 16`. UVs are synthesized as `(x * 0.1, z * 0.1)`.

#### ECFC -- Collision Faces (8 bytes each)

| Offset | Size | Type   | Field       |
|--------|------|--------|-------------|
| 0x00   | 2    | uint16 | Index A     |
| 0x02   | 2    | uint16 | Index B     |
| 0x04   | 2    | uint16 | Index C     |
| 0x06   | 2    | -      | Padding     |

Face count: `ECFC.size / 8`.

### 2.8 Parsing Algorithm Summary

```
1. Read ILFF header; validate "ILFF" magic and size
2. Parse chunk linked list (follow skip pointers)
3. Read model type from HSEM chunk (offset 0x20)
4. Read D3DR for face/mesh/vertex counts
5. Parse XTRV vertices using model-type-specific stride
6. Parse triangles:
   a. Type 1 with ECAF + valid D3DR -> split bone parse (DNER records + ECAF indices)
   b. Otherwise -> packed DNER parse (inline indices per block)
7. If no render geometry found, fall back to XTVC/ECFC collision mesh
8. Scale all positions by 1.0/40.96
```

### 2.9 Text-Based MEF Format (Exported)

The editor also supports a text-based MEF representation (parsed by `mef_parser.cpp`). This is a line-oriented script format:

```
NewObject("building_01");
Material(0, "concrete", 0.8, 0.8, 0.8, 0.1, 0.1, 0.1, 0.9, 0.9, 0.9, 0.0, 0.0, 0.0, 1.0);
MaterialShininess(0, 32.0);
Vertex(0, 100.5, 200.3, 50.0);
Normal(0, 0.0, 0.0, 1.0);
Face(0, 0, 1, 2, 0, 1, 2, 0);
UV(0, 0.0, 1.0);
BreakScript();
```

**Commands:**

| Command             | Arguments                                                   |
|---------------------|-------------------------------------------------------------|
| `NewObject`         | `(name)`                                                    |
| `Material`          | `(index, name, diffR,G,B, ambR,G,B, specR,G,B, emR,G,B, hasCollision)` |
| `MaterialShininess` | `(index, shininess)`                                        |
| `Vertex`            | `(index, x, y, z)`                                         |
| `Normal`            | `(index, nx, ny, nz)`                                      |
| `Face`              | `(faceIdx, v0, v1, v2, n0, n1, n2, materialIdx)`           |
| `UV`                | `(index, u, v, ...)`                                        |
| `BreakScript`       | `()` -- separator, no action                                |

Lines starting with `#` or `//` are comments. Empty lines are skipped.

---

## 3. TEX -- Texture

**Extension:** `.tex`
**Container:** None (raw structured binary)

TEX files store texture image data. All versions share a common 8-byte header prefix, followed by version-specific fields and pixel data.

### 3.1 Common Header (8 bytes)

| Offset | Size | Type   | Description                                            |
|--------|------|--------|--------------------------------------------------------|
| 0x00   | 4    | uint32 | Magic: `"LOOP"` (0x504F4F4C, fourcc `'L','O','O','P'`) |
| 0x04   | 4    | int32  | Version (2, 7, 9, or 11)                              |

### 3.2 Color Modes

| Mode ID | Name       | Bits/Pixel | Pixel Format                                                 |
|---------|------------|------------|--------------------------------------------------------------|
| 2       | ARGB1555   | 16         | `ABBBBBGGGGGRRRRR` -- 5 bits each for B, G, R (bits 0-14), alpha bit 15 ignored |
| 3       | RGB24/32   | 24 or 32   | 24-bit: 3 bytes per pixel (swizzled); 32-bit: 4 bytes per pixel (swizzled with alpha) |
| 67      | BGRA8888   | 32         | 4 bytes per pixel: B, G, R, A                               |

**Pixel decoding notes (all modes):**
- Rows are stored top-to-bottom; the loader flips vertically for OpenGL (bottom-left origin).
- Mode 2: Each 16-bit pixel is decoded as `B = bits[4:0], G = bits[9:5], R = bits[14:10]`, each scaled from 5-bit to 8-bit range (`value * 255.0 / 31.0`). Alpha is forced to 255.
- Mode 3: If `line_width / image_width == 4`, treat as 32-bit (BGRA swizzled). Otherwise treat as 24-bit (BGR swizzled to RGB). The output swizzle is `dst[R,G,B,A] = src[channel2, channel0, channel1, alpha]`.
- Mode 67: Treated identically to 32-bit Mode 3 (BGRA swizzled to RGBA).

### 3.3 Version 2 -- Simple Single-Layer

**Header structure: `tex_head_v2_s` (20 bytes)**

| Offset | Size | Type   | Field             |
|--------|------|--------|-------------------|
| 0x00   | 4    | uint32 | ident (`"LOOP"`)  |
| 0x04   | 4    | int32  | version (2)       |
| 0x08   | 4    | int32  | image_mode        |
| 0x0C   | 4    | int32  | unk0              |
| 0x10   | 2    | int16  | image_line_width  |
| 0x12   | 2    | int16  | image_width       |
| 0x14   | 2    | int16  | image_height      |
| 0x16   | 2    | int16  | bytes_per_pixel   |

**Pixel data:** Immediately follows the header at offset 0x18.

**Validation:** `image_line_width / image_width` must equal 2 (16-bit pixels only in v2).

**Pixel data size:** `image_width * image_height * 2` bytes.

### 3.4 Version 7 -- Multi-Layer (Shared Mode)

**Header structure: `tex_head_v7_s` (52 bytes)**

| Offset | Size | Type   | Field             |
|--------|------|--------|-------------------|
| 0x00   | 4    | uint32 | ident (`"LOOP"`)  |
| 0x04   | 4    | int32  | version (7)       |
| 0x08   | 4    | int32  | unk0              |
| 0x0C   | 4    | int32  | unk1              |
| 0x10   | 4    | int32  | unk2              |
| 0x14   | 4    | int32  | unk3              |
| 0x18   | 4    | int32  | unk4              |
| 0x1C   | 4    | int32  | footer_offset     |
| 0x20   | 4    | int32  | layer_count       |
| 0x24   | 4    | int32  | unk5              |
| 0x28   | 4    | int32  | image_width       |
| 0x2C   | 4    | int32  | image_height      |
| 0x30   | 4    | int32  | image_mode        |

**Layer descriptors** follow at offset 0x34 (immediately after header). Each layer is `tex_layer_v7_s` (40 bytes):

| Offset | Size | Type   | Field             |
|--------|------|--------|-------------------|
| 0x00   | 4    | int32  | image_offset      |
| 0x04   | 4    | int32  | image_line_width  |
| 0x08   | 2    | int16  | image_width       |
| 0x0A   | 2    | int16  | unk0              |
| 0x0C   | 2    | int16  | image_height      |
| 0x0E   | 26   | -      | reserved          |

All layers share the same `image_mode` from the file header. Pixel data for each layer starts at `file_start + image_offset`.

### 3.5 Version 9 -- Multi-Layer (Per-Layer Mode)

**Header structure: `tex_head_v9_s` (52 bytes)**

Identical layout to v7 header (same offsets and fields).

**Layer descriptors** follow at offset 0x34. Each layer is `tex_layer_s` (32 bytes):

| Offset | Size | Type   | Field             |
|--------|------|--------|-------------------|
| 0x00   | 4    | int32  | image_offset      |
| 0x04   | 4    | int32  | image_mode        |
| 0x08   | 2    | int16  | image_line_width  |
| 0x0A   | 2    | int16  | image_width       |
| 0x0C   | 2    | int16  | image_height      |
| 0x0E   | 2    | int16  | unk0              |
| 0x10   | 16   | -      | reserved          |

**Key difference from v7:** Each layer has its own `image_mode` field (at layer offset 0x04), rather than inheriting from the file header.

**Footer:** A validation footer exists at `file_start + footer_offset`:

| Offset | Size | Type   | Field             |
|--------|------|--------|-------------------|
| 0x00   | 4    | uint32 | ident (`"LOOP"`)  |
| 0x04   | 4    | int32  | version           |
| 0x08   | 2    | int16  | unk0              |
| 0x0A   | 2    | int16  | unk1              |
| 0x0C   | 2    | int16  | unk2              |
| 0x0E   | 2    | int16  | unk3              |
| 0x10   | 4    | int32  | count_x           |
| 0x14   | 4    | int32  | count_y           |

The footer `ident` must match `TEX_IDENT` (`"LOOP"`) or the file is rejected.

### 3.6 Version 11 -- Mipmapped

**Header structure: `tex_head_v11_s` (32 bytes)**

| Offset | Size | Type   | Field              |
|--------|------|--------|--------------------|
| 0x00   | 4    | uint32 | ident (`"LOOP"`)   |
| 0x04   | 4    | int32  | version (11)       |
| 0x08   | 4    | int32  | image_mode         |
| 0x0C   | 4    | int32  | unk0               |
| 0x10   | 4    | int32  | unk1               |
| 0x14   | 2    | int16  | unk2               |
| 0x16   | 2    | int16  | image_width        |
| 0x18   | 2    | int16  | image_height       |
| 0x1A   | 2    | int16  | unk3               |
| 0x1C   | 2    | int16  | unk4               |
| 0x1E   | 2    | int16  | bytes_per_pixel    |

**Pixel data:** Starts at offset 0x20 (immediately after header). Only the first mip level is loaded.

**Line width:** Computed as `unk3 * bytes_per_pixel`. This is the byte stride per row.

---

## 4. QVM -- Script Bytecode

**Extension:** `.qvm`
**Container:** None (raw structured binary)
**Signature:** `"LOOP"` (same magic as TEX)

QVM is a compiled bytecode format for IGI's scripting virtual machine (version "0.5", stored as major=8, minor=5 in the header).

### 4.1 Header (60 bytes)

| Offset | Size | Type     | Field         | Description                            |
|--------|------|----------|---------------|----------------------------------------|
| 0x00   | 4    | char[4]  | signature     | `"LOOP"` (0x4C4F4F50)                 |
| 0x04   | 4    | uint32   | ver_major     | Must be 8                              |
| 0x08   | 4    | uint32   | ver_minor     | Must be 5                              |
| 0x0C   | 4    | uint32   | of_itable     | Offset to identifier table             |
| 0x10   | 4    | uint32   | of_ivalue     | Offset to identifier strings           |
| 0x14   | 4    | uint32   | sz_itable     | Size of identifier table               |
| 0x18   | 4    | uint32   | sz_ivalue     | Size of identifier string pool         |
| 0x1C   | 4    | uint32   | of_stable     | Offset to string table                 |
| 0x20   | 4    | uint32   | of_svalue     | Offset to string value pool            |
| 0x24   | 4    | uint32   | sz_stable     | Size of string table                   |
| 0x28   | 4    | uint32   | sz_svalue     | Size of string value pool              |
| 0x2C   | 4    | uint32   | of_ctable     | Offset to code (bytecode) section      |
| 0x30   | 4    | uint32   | sz_ctable     | Size of code section                   |
| 0x34   | 4    | uint32   | unknown_1     | Unknown                                |
| 0x38   | 4    | uint32   | unknown_2     | Unknown                                |

### 4.2 String Pools

The identifier pool (`of_ivalue`, `sz_ivalue`) and string pool (`of_svalue`, `sz_svalue`) are both arrays of null-terminated strings packed sequentially. The parser splits on `\0` bytes to recover individual strings.

### 4.3 Opcodes (49 total)

All opcodes are encoded as a single byte. The operand (if any) follows immediately in little-endian format.

| Value | Name     | Operand Size | Description                           |
|-------|----------|--------------|---------------------------------------|
| 0x00  | BRK      | 0            | Break / debug trap                    |
| 0x01  | NOP      | 0            | No operation                          |
| 0x02  | PUSH     | 4 (uint32)   | Push 32-bit integer                   |
| 0x03  | PUSHB    | 1 (uint8)    | Push byte as integer                  |
| 0x04  | PUSHW    | 2 (uint16)   | Push 16-bit word as integer           |
| 0x05  | PUSHF    | 4 (float32)  | Push 32-bit float                     |
| 0x06  | PUSHA    | 0            | Push address                          |
| 0x07  | PUSHS    | 0            | Push string ref                       |
| 0x08  | PUSHSI   | 4 (uint32)   | Push string identifier (32-bit index) |
| 0x09  | PUSHSIB  | 1 (uint8)    | Push string identifier (8-bit index)  |
| 0x0A  | PUSHSIW  | 2 (uint16)   | Push string identifier (16-bit index) |
| 0x0B  | PUSHI    | 0            | Push immediate                        |
| 0x0C  | PUSHII   | 4 (uint32)   | Push indirect integer (32-bit)        |
| 0x0D  | PUSHIIB  | 1 (uint8)    | Push indirect integer (8-bit)         |
| 0x0E  | PUSHIIW  | 2 (uint16)   | Push indirect integer (16-bit)        |
| 0x0F  | PUSH0    | 0            | Push constant 0                       |
| 0x10  | PUSH1    | 0            | Push constant 1                       |
| 0x11  | PUSHM    | 0            | Push memory ref                       |
| 0x12  | POP      | 0            | Pop top of stack                      |
| 0x13  | RET      | 0            | Return from subroutine                |
| 0x14  | BRA      | 4 (int32)    | Branch always (relative offset)       |
| 0x15  | BF       | 4 (int32)    | Branch if false                       |
| 0x16  | BT       | 4 (int32)    | Branch if true                        |
| 0x17  | JSR      | 0            | Jump to subroutine                    |
| 0x18  | CALL     | special      | Native function call (see below)      |
| 0x19  | ADD      | 0            | Integer addition                      |
| 0x1A  | SUB      | 0            | Integer subtraction                   |
| 0x1B  | MUL      | 0            | Integer multiplication                |
| 0x1C  | DIV      | 0            | Integer division                      |
| 0x1D  | SHL      | 0            | Shift left                            |
| 0x1E  | SHR      | 0            | Shift right                           |
| 0x1F  | AND      | 0            | Bitwise AND                           |
| 0x20  | OR       | 0            | Bitwise OR                            |
| 0x21  | XOR      | 0            | Bitwise XOR                           |
| 0x22  | LAND     | 0            | Logical AND                           |
| 0x23  | LOR      | 0            | Logical OR                            |
| 0x24  | EQ       | 0            | Equal comparison                      |
| 0x25  | NE       | 0            | Not equal comparison                  |
| 0x26  | LT       | 0            | Less than                             |
| 0x27  | LE       | 0            | Less than or equal                    |
| 0x28  | GT       | 0            | Greater than                          |
| 0x29  | GE       | 0            | Greater than or equal                 |
| 0x2A  | ASSIGN   | 0            | Assignment                            |
| 0x2B  | PLUS     | 0            | Unary plus                            |
| 0x2C  | MINUS    | 0            | Unary minus (negate)                  |
| 0x2D  | INV      | 0            | Bitwise invert                        |
| 0x2E  | NOT      | 0            | Logical NOT                           |
| 0x2F  | BLK      | 0            | Block marker                          |
| 0x30  | ILLEGAL  | 0            | Illegal / invalid opcode              |

### 4.4 CALL Instruction Encoding

The CALL opcode has variable-length encoding:

```
[0x18] [uint32 count] [int32 target_0] [int32 target_1] ... [int32 target_(count-1)]
```

| Offset | Size        | Type     | Description                   |
|--------|-------------|----------|-------------------------------|
| 0      | 1           | uint8    | Opcode (0x18)                 |
| 1      | 4           | uint32   | count -- number of call targets |
| 5      | count * 4   | int32[]  | Array of call target offsets  |

**Total instruction size:** `1 + 4 + (count * 4)` bytes.

### 4.5 Instruction Encoding Summary

```
[opcode: 1 byte] [operand: 0, 1, 2, or 4 bytes depending on opcode]
```

Most instructions are 1 byte (opcode only). Branch instructions (BRA, BF, BT) use 4-byte signed offsets. Push variants use 1, 2, or 4-byte operands as indicated in the table above.

---

## 5. RES -- Resource Archive

**Extension:** `.res`
**Container:** ILFF with format ID `"IRES"` (at offset 0x10)

RES files bundle multiple named resources (models, textures, scripts, etc.) into a single archive.

### 5.1 Structure

```
[ILFF Header (20 bytes)]
  magic:     "ILFF"
  size:      total file size
  align:     4
  skip:      0
  format_id: "IRES"

[Repeated chunk pairs:]
  NAME chunk:
    fourcc: "NAME" (0x454D414E LE)
    size:   uint32 (length of name string including null terminator)
    data:   null-terminated filename string

  BODY chunk:
    fourcc: "BODY" (0x59444F42 LE)
    size:   uint32 (length of resource data)
    data:   raw binary resource data
```

### 5.2 Chunk Layout

| Field  | FourCC (LE hex) | Description                              |
|--------|-----------------|------------------------------------------|
| NAME   | 0x454D414E      | Resource name (null-terminated string)   |
| BODY   | 0x59444F42      | Resource data (raw bytes)                |

Chunks alternate NAME/BODY pairs. If a BODY chunk appears without a preceding NAME, it is assigned a synthetic name `<unnamed_N>`.

### 5.3 Alignment

All chunks are aligned to 4-byte boundaries. If a chunk's data does not end on a 4-byte boundary, 1-3 padding bytes are skipped before the next chunk.

### 5.4 RES Chunk Detail

Unlike MEF's linked-list chunks, RES uses a simpler 8-byte chunk header:

| Offset | Size | Type     | Description            |
|--------|------|----------|------------------------|
| 0x00   | 4    | uint32   | FourCC identifier      |
| 0x04   | 4    | uint32   | Data size              |

Data follows immediately at offset 0x08 from the chunk start.

### 5.5 Example

```
Offset  Description
0x0000  ILFF header: "ILFF" + file_size + 4 + 0 + "IRES"
0x0014  NAME chunk: "NAME" + 12 + "model_01.mef\0"
0x0028  BODY chunk: "BODY" + 1024 + [1024 bytes of MEF data]
0x042C  NAME chunk: "NAME" + 11 + "texture.tex\0"
0x043F  (1 byte padding to align to 4)
0x0440  BODY chunk: "BODY" + 2048 + [2048 bytes of TEX data]
...
```

---

## 6. MTP -- Model-Texture Package

**Extension:** `.mtp`
**Container:** FORM/IFF (EA IFF-85 variant with big-endian sizes)

MTP files define which textures belong to which models for a given level or asset group.

### 6.1 File Header (12 bytes)

| Offset | Size | Type         | Description                         |
|--------|------|--------------|-------------------------------------|
| 0x00   | 4    | char[4]      | Magic: `"FORM"`                     |
| 0x04   | 4    | uint32 (BE)  | FORM payload size (big-endian)      |
| 0x08   | 4    | char[4]      | Format ID: `"MTP "` (with trailing space) |

### 6.2 IFF Chunk Header (8 bytes)

| Offset | Size | Type         | Description                    |
|--------|------|--------------|--------------------------------|
| 0x00   | 4    | char[4]      | FourCC chunk type              |
| 0x04   | 4    | uint32 (BE)  | Data size (big-endian)         |

Chunks are aligned to 2-byte boundaries (standard IFF). If data size is odd, one padding byte follows.

### 6.3 Chunk Types

| FourCC | Description                            | Data Format              |
|--------|----------------------------------------|--------------------------|
| `BANM` | Bone animation names                   | String array             |
| `SNDS` | Sound names                            | String array             |
| `SVOL` | Shadow volume names                    | String array             |
| `MODS` | Model filenames                        | String array             |
| `VNAM` | Vertex names                           | String array             |
| `TEXF` | Texture filenames                      | String array             |
| `PALF` | Palette filenames                      | String array             |
| `GTT`  | (Unknown purpose)                      | -                        |
| `INST` | Model-texture instance mappings        | Instance array (see below) |

### 6.4 String Array Format

All string-list chunks (BANM, SVOL, MODS, TEXF, etc.) use this layout:

| Offset | Size | Type   | Description                                    |
|--------|------|--------|------------------------------------------------|
| 0x00   | 4    | uint32 (LE) | count -- number of strings                |
| 0x04   | var  | char[] | `count` null-terminated strings packed sequentially |

Note: The count field is little-endian despite the IFF chunk sizes being big-endian.

### 6.5 INST Chunk -- Instance Mappings

The INST chunk maps each model to its texture list:

| Offset | Size | Type        | Description                           |
|--------|------|-------------|---------------------------------------|
| 0x00   | 4    | uint32 (LE) | count -- number of mapping entries    |

Each entry (variable length):

| Offset | Size       | Type        | Description                           |
|--------|------------|-------------|---------------------------------------|
| 0x00   | 2          | uint16 (LE) | modelIdx -- index into MODS array    |
| 0x02   | 2          | uint16 (LE) | texCount -- number of textures       |
| 0x04   | texCount*2 | uint16 (LE)[] | Array of texture indices into TEXF |

### 6.6 Example Parsing Flow

```
1. Validate "FORM" magic and read BE size
2. Validate "MTP " format ID at offset 8
3. Iterate chunks from offset 12:
   - Parse MODS -> list of model names
   - Parse TEXF -> list of texture names
   - Parse INST -> resolve model/texture indices to names
4. Each INST entry: model_name = MODS[modelIdx], textures = TEXF[texIdx] for each texIdx
```

---

## 7. DAT -- Asset List

**Extension:** `.dat`
**Container:** None (plain text)
**Location:** `missions/location0/levelN/levelN.dat`

DAT files are text-based lookup tables that map model IDs to their texture IDs within a specific level.

### 7.1 Format

The file is a sequence of whitespace-separated tokens, one per line. Lines starting with `***` are comments and are skipped. Empty lines are skipped.

```
<total_texture_entry_count>
<model_id_1>
<texture_count_1>
<texture_id_1a>
<texture_id_1b>
...
<model_id_2>
<texture_count_2>
<texture_id_2a>
...
```

### 7.2 Token Sequence

| Token Index | Description                                          |
|-------------|------------------------------------------------------|
| 0           | Total texture entry count (informational, not used for parsing) |
| 1           | First model ID (string, e.g. `"300_01_1"`)           |
| 2           | Number of textures for first model (integer)         |
| 3..N        | Texture IDs for first model (strings)                |
| N+1         | Second model ID                                      |
| ...         | Repeating pattern                                    |

### 7.3 Example

```
*** Level 1 texture assignments ***
15
300_01_1
3
wall_brick
wall_concrete
floor_tile
301_01_1
1
metal_door
```

This maps:
- Model `300_01_1` to textures: `wall_brick`, `wall_concrete`, `floor_tile`
- Model `301_01_1` to texture: `metal_door`

The texture IDs correspond to `.tex` files in the level's `textures/` directory (e.g., `wall_brick.tex`).

---

## 8. QSC -- Script Source

**Extension:** `.qsc`
**Container:** None (plain text)

QSC files contain the source-level scripting language for IGI's game logic. Unlike QVM (compiled bytecode), QSC is human-readable and uses a function-call syntax.

### 8.1 Syntax

QSC is a semicolon-terminated, function-call-based language:

```
FunctionName(arg1, arg2, NestedFunc(innerArg), "string arg");
```

**Argument types:**
- **String:** Enclosed in double quotes (`"hello"`). Supports `\"` escape sequences.
- **Number:** Integer or floating-point (e.g., `42`, `-3.14`, `1e5`).
- **Boolean:** `TRUE` or `FALSE` (case-sensitive).
- **Function:** Nested function calls act as arguments (evaluated recursively).

### 8.2 Structure

A QSC file consists of a sequence of top-level function calls separated by semicolons:

```
Task(1, "patrol",
    StatusText(1, "Guard patrolling"),
    AIGraph(1, "patrol_route_01"),
    Loop(3,
        Goto(100.0, 200.0, 50.0),
        Wait(5.0)
    )
);
```

### 8.3 Parser Limits

| Constant       | Value  | Description                         |
|----------------|--------|-------------------------------------|
| MAX_QSC_FUNCS  | 4096   | Maximum function nodes              |
| MAX_QSC_ARGS   | 65536  | Maximum argument nodes              |

---

## Appendix A: FourCC Reference

| FourCC | Hex (LE)   | Context   | Description                     |
|--------|------------|-----------|---------------------------------|
| ILFF   | 0x46464C49 | Container | ILFF file magic                 |
| HSEM   | 0x4D455348 | MEF       | Mesh info                       |
| XTRV   | 0x56525458 | MEF       | Render vertices                 |
| DNER   | 0x52454E44 | MEF       | Render blocks / triangle data   |
| ECAF   | 0x46414345 | MEF       | Face index buffer               |
| D3DR   | 0x52443344 | MEF       | D3D render info                 |
| XTVC   | 0x43565458 | MEF       | Collision vertices              |
| ECFC   | 0x43464345 | MEF       | Collision faces                 |
| IRES   | 0x53455249 | RES       | Resource archive format ID      |
| NAME   | 0x454D414E | RES       | Resource name chunk             |
| BODY   | 0x59444F42 | RES       | Resource body chunk             |
| FORM   | -          | MTP       | IFF FORM container magic        |
| MTP    | -          | MTP       | Model-Texture Package format ID |
| BANM   | -          | MTP       | Bone animation names            |
| SVOL   | -          | MTP       | Shadow volume names             |
| MODS   | -          | MTP       | Model filenames                 |
| TEXF   | -          | MTP       | Texture filenames               |
| INST   | -          | MTP       | Instance mappings               |
| LOOP   | 0x504F4F4C | TEX/QVM   | TEX & QVM file magic            |

## Appendix B: Constants

| Constant                | Value          | Description                         |
|-------------------------|----------------|-------------------------------------|
| ILFF header size        | 20 bytes       | File header                         |
| ILFF chunk header size  | 16 bytes       | Per-chunk header (MEF style)        |
| RES chunk header size   | 8 bytes        | Per-chunk header (RES style)        |
| IFF chunk header size   | 8 bytes        | Per-chunk header (MTP style)        |
| MEF import scale        | 1.0 / 40.96    | ~0.024414 (native units to meters)  |
| MEF render scale        | 40.96          | Meters to native units              |
| ILFF alignment          | 4 bytes        | Standard alignment                  |
| IFF alignment           | 2 bytes        | Standard IFF alignment              |
| QVM version             | 8.5            | Expected ver_major=8, ver_minor=5   |
| TEX versions supported  | 2, 7, 9, 11   | Known TEX format versions           |

## Appendix C: Parsing Quick Reference

### Reading an MEF file

```python
def parse_mef(data: bytes):
    # 1. Validate ILFF header
    assert data[0:4] == b"ILFF"
    file_size = struct.unpack_from("<I", data, 4)[0]
    assert file_size == len(data)

    # 2. Parse chunks (linked list via skip field)
    chunks = {}
    pos = 20  # after ILFF header
    while pos + 16 <= len(data):
        fourcc = data[pos:pos+4].decode("ascii")
        size, align, skip = struct.unpack_from("<III", data, pos + 4)
        chunks[fourcc] = (pos + 16, size)  # (data_offset, data_size)
        if skip == 0:
            break
        pos += skip

    # 3. Read model type from HSEM
    hsem_off, hsem_sz = chunks["HSEM"]
    model_type = struct.unpack_from("<I", data, hsem_off + 32)[0]

    # 4. Read vertices from XTRV
    xtrv_off, xtrv_sz = chunks["XTRV"]
    vertex_size = {0: 32, 1: 40, 3: 40}[model_type]
    uv_offset   = {0: 24, 1: 24, 3: 12}[model_type]
    vertex_count = xtrv_sz // vertex_size

    vertices = []
    for i in range(vertex_count):
        base = xtrv_off + i * vertex_size
        x, y, z = struct.unpack_from("<fff", data, base)
        u, v = struct.unpack_from("<ff", data, base + uv_offset)
        vertices.append((x / 40.96, y / 40.96, z / 40.96, u, v))

    # 5. Parse triangles from DNER (packed path shown)
    # ... (see section 2.5 for full algorithm)
```

### Reading a TEX file

```python
def parse_tex(data: bytes):
    assert data[0:4] == b"LOOP"
    version = struct.unpack_from("<i", data, 4)[0]

    if version == 2:
        mode, _, line_w, w, h, bpp = struct.unpack_from("<iihhHH", data, 8)
        pixels = data[20:]  # 16-bit ARGB1555
    elif version == 11:
        mode = struct.unpack_from("<i", data, 8)[0]
        w = struct.unpack_from("<h", data, 22)[0]
        h = struct.unpack_from("<h", data, 24)[0]
        pixels = data[32:]
    # ... handle v7/v9 with layer descriptors
```
