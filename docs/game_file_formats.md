# IGI File Format Reference

> Technical documentation of the binary file formats used by *Project I.G.I.: I'm Going In* (2000) and *IGI 2: Covert Strike* (2003), both by Innerloop Studios.
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
9. [MagicObject System](#9-magicobject-system)
10. [FNT -- Font Format](#10-fnt----font-format)
11. [Graph DAT -- Navigation Graph](#11-graph-dat----navigation-graph)

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

| FourCC | Name                | Description                                               |
|--------|---------------------|-----------------------------------------------------------|
| `HSEM` | Mesh Info           | Model metadata including model type                       |
| `XTRV` | Vertices (render)   | Interleaved vertex data for rendering                     |
| `DNER` | Render blocks       | Triangle indices + per-block metadata                     |
| `ECAF` | Face indices        | Separate index buffer (used by Type 1 bone models)        |
| `D3DR` | D3D Render info     | Face/mesh/vertex counts                                   |
| `XTVC` | Collision vertices  | Collision mesh vertices (16 bytes each, IGI 1)            |
| `ECFC` | Collision faces     | Collision mesh face indices (8 bytes each, IGI 1)         |
| `TAMC` | Material config     | Per-face material properties (opacity, diffuse, portal)   |
| `ATTA` | Attachments         | Sub-model attachment points with transform matrix (68 bytes each) |
| `XTVM` | Magic vertices      | Special-purpose vertices for game events (16 bytes each)  |
| `REIH` | Bone hierarchy      | Bone parent/child relationships and rest-pose pivots      |
| `MANB` | Bone names          | Bone name strings (16 bytes each, null-padded)            |
| `TROP` | Portal records      | Portal zone entries (20 bytes each)                       |
| `XVTP` | Portal vertices     | Portal mesh vertex positions (12 bytes each)              |
| `CFTP` | Portal faces        | Portal mesh triangle indices (12 bytes each)              |
| `PMTL` | Portal materials    | Material IDs for portal zones (16 bytes each)             |

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

### 2.8 ATTA Chunk -- Attachment Points (68 bytes each)

ATTA records place named sub-models onto a parent model with a local position and 3x3 rotation matrix. Sub-models can be static visual parts or MagicObjects with runtime behavior (see [Section 9](#9-magicobject-system)).

| Offset | Size | Type     | Field     | Description                                        |
|--------|------|----------|-----------|----------------------------------------------------|
| 0x00   | 16   | char[16] | name      | Sub-model name (null-padded, e.g. `"600_02_1"`)    |
| 0x10   | 4    | float32  | px        | Local position X (raw MEF units, NOT scaled)       |
| 0x14   | 4    | float32  | py        | Local position Y                                   |
| 0x18   | 4    | float32  | pz        | Local position Z                                   |
| 0x1C   | 4    | float32  | r00       | Rotation matrix row 0, col 0                       |
| 0x20   | 4    | float32  | r01       | Rotation matrix row 0, col 1                       |
| 0x24   | 4    | float32  | r02       | Rotation matrix row 0, col 2                       |
| 0x28   | 4    | float32  | r03       | Rotation matrix row 1, col 0                       |
| 0x2C   | 4    | float32  | r04       | Rotation matrix row 1, col 1                       |
| 0x30   | 4    | float32  | r05       | Rotation matrix row 1, col 2                       |
| 0x34   | 4    | float32  | r06       | Rotation matrix row 2, col 0                       |
| 0x38   | 4    | float32  | r07       | Rotation matrix row 2, col 1                       |
| 0x3C   | 4    | float32  | r08       | Rotation matrix row 2, col 2                       |
| 0x40   | 4    | int32    | boneId    | Parent bone index (-1 = not bone-attached)         |

**Total: 68 bytes per record.** Count: `ATTA.size / 68`.

Positions are in raw MEF units (divide by 40.96 to get meters). The parent model contains one ATTA record per logical sub-model slot; the same sub-model name can appear multiple times at different offsets.

### 2.9 XTVM Chunk -- Magic Vertices (16 bytes each)

XTVM records mark special-purpose positions within a model used by the game engine for events: gun fire origins, ladder interaction zones, particle emitters, etc. These are *not* rendered — they are invisible game logic hooks.

| Offset | Size | Type    | Field       | Description                                                      |
|--------|------|---------|-------------|------------------------------------------------------------------|
| 0x00   | 4    | float32 | px          | Position X (raw MEF units, NOT scaled)                           |
| 0x04   | 4    | float32 | py          | Position Y                                                       |
| 0x08   | 4    | float32 | pz          | Position Z                                                       |
| 0x0C   | 4    | int32   | magicType   | Magic vertex type ID (unconfirmed; see TASKTYPE constants below) |

**Total: 16 bytes per vertex.** Count: `XTVM.size / 16`.

The `magicType` field meaning is unconfirmed from binary analysis. It likely maps to a `TASKTYPE_*` constant indicating what engine system uses this vertex (gun clip position, ladder zone, etc.). Many entries have `magicType == 0`.

### 2.10 Parsing Algorithm Summary

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
8. Parse ATTA for sub-model attachment points
9. Parse XTVM for magic vertex positions
10. Scale all render positions by 1.0/40.96
```

### 2.11 Text-Based MEF Format (Exported)

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

The INST chunk maps each model to its texture list. It has **no count prefix**:
it holds exactly one entry per model, so the entry count equals the MODS model
count.

Each entry (variable length):

| Offset | Size       | Type          | Description                           |
|--------|------------|---------------|---------------------------------------|
| 0x00   | 4          | uint32 (LE)   | modelIdx -- index into MODS array     |
| 0x04   | 4          | uint32 (LE)   | texCount -- number of textures        |
| 0x08   | texCount*4 | uint32 (LE)[] | Array of texture indices into TEXF    |

Note: INST appears **before** TEXF in file order, so a parser must read all
chunks first and resolve INST -> texture names afterward (the TEXF names are not
yet known when the INST chunk is encountered).

### 6.6 Example Parsing Flow

```
1. Validate "FORM" magic and read BE size
2. Validate "MTP " format ID at offset 8
3. Iterate chunks from offset 12, collecting raw chunk data:
   - Parse MODS -> list of model names
   - Parse TEXF -> list of texture names
   - Defer INST (it appears before TEXF, so TEXF names aren't known yet)
4. After all chunks are read, resolve each INST entry:
   model_name = MODS[modelIdx], textures = TEXF[texIdx] for each texIdx
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

## 9. MagicObject System

MagicObjects are mesh sub-parts with runtime behavior. They are the engine's system for giving interactivity to parts of a 3D model — a door that swings, glass that shatters, a ladder you can climb, helicopter rotors that spin.

The system is split across two data sources that the engine combines at load time:

| Data Source          | Role                                                        |
|----------------------|-------------------------------------------------------------|
| `magicobjconfig.qsc` | Defines the **behavior** (what type of interactive object)  |
| Parent model ATTA    | Defines the **placement** (transform in the parent mesh)    |

### 9.1 MagicObject Config Files

Two formats exist depending on game version:

**IGI 1** — `magicobjconfig.qsc` uses `Task_New`:
```qsc
Task_New(-1, "MagicObjConfig",
    "name",       // lookup key — matches ATTA entry name
    "model_id",   // which .mef mesh to use
    TASKTYPE_XXX  // what behavior to apply
);
```

**IGI 2** — `magicobj.qvm` (compiled from `magicobj.qsc`) uses `DefineMagicObj`:
```qsc
DefineMagicObj("model_id", "model_id", TASKTYPE_XXX);
// Some task types have extra parameters:
DefineMagicObj("614_02_1", "614_02_1", TASKTYPE_AISTATIONARYGUN, "614_03_1", 361, 15, -4, 10000, 15, 15, "tank_turret", 0, 6);
DefineMagicObj("610_02_1", "610_02_1", TASKTYPE_CARDOOR, 0, 30, -118);
DefineMagicObj("700_05_1", "700_05_1", TASKTYPE_HELIDOOR, 1, 0.6, 2.65);
```

The engine checks every ATTA sub-model name against this registry at spawn time. If found, it spawns a MagicObject with the configured behavior; if not found, it spawns a static visual sub-part.

The **parent** vehicle models (`614_01_1`, `622_01_1`, `700_01_1`) are NOT listed in magicobj — they are plain parent meshes. Only their ATTA sub-models (turrets, wheels, rotors, doors) are registered as MagicObjects.

`editormagicobj.qvm` is a parallel file used by the level editor only — it shipped empty in the retail game.

### 9.2 Task Types

179 total MagicObj entries across all levels, using 15 distinct task types:

| TASKTYPE               | Count | Description                                                                 |
|------------------------|-------|-----------------------------------------------------------------------------|
| `SHADOWVOLUME`         | 72    | Simplified geometry for stencil shadow casting. Most common — nearly every weapon and prop has one. Rendered separately from the main mesh. |
| `GLASS`                | 64    | Breakable glass panels. Shatters on bullet impact or explosion.             |
| `LADDER`               | 15    | Climbable surface. Player can interact to climb up/down.                   |
| `DEATHZONE`            | 5     | Invisible kill volume. Instant death on contact (helicopter blades, fall zones). Models: `killbox`, `Killair`, `603_13`, `603_14`, `610_04`. |
| `WHEEL`                | 5     | Rotating wheel/tire. Spins based on vehicle movement. Models: `600_05`, `600_06`, `616_02`, `661_02`, `663_02`. |
| `AISTATIONARYGUN`      | 4     | Mounted gun position. AI or player can man it. Models: `313_09` (tripod gun), `661_03`, `700_01`, `720_06` (heli gun). |
| `GRENADEPIN`           | 3     | Grenade pin that detaches on throw. Used by explosive, smoke, and flashbang grenades. |
| `ROTOR`                | 3     | Helicopter rotor blade. Spins continuously. Models: `711_01`, `711_02`, `712_01`. |
| `HITZONE`              | 2     | Damageable area (e.g. vehicle fuel tank). Has max damage and smoke threshold. Models: `709_02`, `709_03`. |
| `CARDOOR`              | 1     | Hinged vehicle door. Rotation axis, 30 deg/sec speed, −118 deg max angle. Model: `610_02`. |
| `DRAWER`               | 1     | Openable drawer. Model: `221_02`.                                           |
| `RPGROCKET`            | 1     | RPG rocket projectile in flight. Model: `140_02`.                          |
| `BOMBBACKPACK`         | 1     | Explosive backpack. Model: `113_02`.                                        |
| `WEAPONMAGICOBJ`       | 1     | Generic weapon attachment point. `model=none` (virtual, no mesh).          |
| `PRIMARYMAGICOBJ`      | 1     | Generic primary attachment point. `model=none` (virtual, no mesh).         |

**Shadow volumes dominate:** 72 of 179 entries (40%) are `SHADOWVOLUME`. In the early 2000s, stencil shadow volumes were the standard real-time shadow technique. The engine needed a separate simplified mesh to project shadows — using the full-detail model was too expensive. These shadow meshes are stored as ATTA sub-parts; `magicobjconfig` tells the engine "this sub-part is a shadow volume, don't render it normally."

### 9.3 XTVM -- Magic Vertices vs MagicObjects

XTVM magic vertices and the MagicObject system serve different purposes and are **independent**:

| Feature        | XTVM Magic Vertices                             | ATTA MagicObjects                              |
|----------------|-------------------------------------------------|------------------------------------------------|
| Storage        | XTVM chunk in parent `.mef`                     | ATTA chunk + `magicobjconfig.qsc`              |
| What it is     | A 3D position within the model                  | A separate sub-mesh with behavior              |
| Purpose        | Engine event hook (gun fire origin, etc.)        | Interactive sub-object (door, glass, rotor...) |
| Rendering      | Not rendered — invisible to player              | Has its own `.mef` mesh, may render            |
| Relationship   | No direct relationship to ATTA entries          | Referenced by ATTA name in parent model        |

A model can have both: e.g., an AK47 has XTVM magic vertices for muzzle/clip positions *and* an ATTA sub-model registered as `SHADOWVOLUME`.

### 9.4 Examples

#### Guard Tower (600 family)

```
600_01_1.mef  (main structure — building mesh)
│
├── ATTA "600_02_1"  → magicobjconfig TASKTYPE_GLASS     (breakable window)
├── ATTA "600_03_1"  → magicobjconfig TASKTYPE_GLASS     (another window)
├── ATTA "600_04_1"  → magicobjconfig TASKTYPE_GLASS     (×2, two more panels)
├── ATTA "600_05_1"  → magicobjconfig TASKTYPE_WHEEL     (rotating parts, ×2)
├── ATTA "600_06_1"  → magicobjconfig TASKTYPE_WHEEL     (more rotating parts, ×4)
├── ATTA "killbox"   → magicobjconfig TASKTYPE_DEATHZONE (invisible kill volume)
├── ATTA "truckshade" → NOT in magicobjconfig            (static shadow mesh)
│
└── XTVM: 8 magic vertices  (independent — unknown purpose)
```

#### AK47 (107 family)

```
107_01_1.mef  (gun body)
│
├── ATTA "107_02_1"  → NOT in magicobjconfig   (scope/sight — static visual)
├── ATTA "107_03_1"  → NOT in magicobjconfig   (magazine — static visual)
├── ATTA "107_04_1"  → magicobjconfig TASKTYPE_SHADOWVOLUME
│                      (simplified shadow mesh for stencil shadow rendering)
│
└── XTVM: 4 magic vertices  (2 active positions, 2 null/zero entries)

107_05_1  → also in magicobjconfig TASKTYPE_SHADOWVOLUME
            (not referenced by 107_01_1 ATTA — used by LOD variants)
```

### 9.5 Engine Load Sequence

```
1. Load parent mesh (e.g. 600_01_1.mef)
2. Parse ATTA chunk → list of sub-model names + transforms
3. For each ATTA entry:
   a. Check name against magicobjconfig registry
   b. If found  → spawn MagicObject(model, transform, taskType)
   c. If not found → spawn static visual sub-part at transform
4. Parse XTVM chunk → register magic vertex positions for engine events
   (gun clip positions, ladder interaction zones, etc.)
```

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
| ATTA   | 0x41545441 | MEF       | Attachment points               |
| XTVM   | 0x4D565458 | MEF       | Magic vertices                  |
| REIH   | 0x48494552 | MEF       | Bone hierarchy                  |
| MANB   | 0x424E414D | MEF       | Bone names                      |
| TAMC   | 0x434D4154 | MEF       | Material config                 |
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

---

## 10. FNT -- Font Format

FNT files store bitmap fonts used for in-game text rendering. Each file is an ILFF container (content type `FONT`) holding a texture atlas with glyph images and metadata that maps character codes to regions within that atlas.

### 10.1 Structure Overview

```text
┌─────────────────────────────────────┐
│ ILFF Header (16 bytes)              │
│   Signature: "ILFF"                 │
│   File size, version, reserved      │
├─────────────────────────────────────┤
│ Content type: "FONT" (4 bytes)      │
├─────────────────────────────────────┤
│ FNTH Chunk — Font header            │
├─────────────────────────────────────┤
│ ANMF Chunk — Glyph metrics          │
├─────────────────────────────────────┤
│ TRN2 Chunk — Character code mapping │
├─────────────────────────────────────┤
│ TEXH Chunk — Texture header         │
├─────────────────────────────────────┤
│ BODY Chunk — RGBA texture atlas     │
└─────────────────────────────────────┘
```

Each chunk follows the standard ILFF chunk layout:

| Field     | Type   | Description                           |
|-----------|--------|---------------------------------------|
| FourCC    | 4s     | Chunk signature (e.g. `FNTH`, `ANMF`) |
| Length    | uint32 | Content length in bytes               |
| Alignment | uint32 | Padding alignment (always 4)          |
| Offset    | uint32 | Offset to next chunk (0 if last)      |

### 10.2 FNTH — Font Header

24 bytes, 6 × uint32 little-endian.

| Offset | Type   | Field       | Description                   |
|--------|--------|-------------|-------------------------------|
| 0      | uint32 | version     | Format version (always 1)     |
| 4      | uint32 | num_glyphs  | Number of glyphs in the font  |
| 8      | uint32 | cell_height | Line height in pixels         |
| 12     | uint32 | unknown_01  | Unknown                       |
| 16     | uint32 | unknown_02  | Unknown (always 1)            |
| 20     | uint32 | unknown_03  | Unknown (same as cell_height) |

Typical values across game files:

| Font file           | Glyphs | Cell height | Texture size |
|---------------------|--------|-------------|--------------|
| font2.fnt           | 158    | 11          | 128 × 64     |
| font1.fnt           | 158    | 13          | 128 × 128    |
| fontmp.fnt          | 154    | 16          | 128 × 128    |
| loadfontbig1024.fnt | 158    | 20          | 256 × 128    |
| loadfontbig1280.fnt | 158    | 25          | 256 × 256    |

### 10.3 ANMF — Glyph Metrics

`num_glyphs × 40` bytes. Each entry describes one glyph's position in the texture atlas and its pixel dimensions.

#### Glyph entry (40 bytes)

| Offset | Type   | Field      | Description                                   |
|--------|--------|------------|-----------------------------------------------|
| 0      | float  | v_top      | Top edge V coordinate (normalized 0–1)        |
| 4      | float  | u_left     | Left edge U coordinate (normalized 0–1)       |
| 8      | float  | v_offset   | Vertical offset (normalized, purpose unclear) |
| 12     | float  | u_right    | Right edge U coordinate (normalized 0–1)      |
| 16     | float  | v_bottom   | Bottom edge V coordinate (normalized 0–1)     |
| 20     | uint16 | pad_0      | Padding (always 0)                            |
| 22     | uint16 | width      | Glyph width in pixels                         |
| 24     | uint16 | height     | Glyph height in pixels                        |
| 26     | uint16 | advance_x  | Horizontal advance (typically width + 1)      |
| 28     | uint16 | height_2   | Duplicate of height                           |
| 30     | uint16 | pad_1      | Padding (always 0)                            |
| 32     | uint32 | pad_2      | Padding (usually 0)                           |
| 36     | int32  | unknown_01 | Unknown flag or kerning value                 |

UV coordinates are normalized to the texture dimensions from the TEXH chunk. To convert to pixel coordinates:

```text
pixel_x = int(u_left  × texture_width)
pixel_y = int(v_top   × texture_height)
pixel_w = width
pixel_h = height
```

#### Glyph packing

Glyphs are packed left-to-right into rows within the texture atlas. When a row is full, packing continues on a new row. The `v_top` / `v_bottom` coordinates reflect the vertical position of each glyph's row.

### 10.4 TRN2 — Character Code Mapping

`num_glyphs × 2` bytes. An array of uint16 values mapping each glyph index to its character code point.

| Glyph index | Char code | Character |
|-------------|-----------|-----------|
| 0           | 33        | !         |
| 1           | 34        | "         |
| 2           | 35        | #         |

The mapping covers ASCII printable characters (33–126) and extended Latin characters (codes 128–252) for European language support.

### 10.5 TEXH — Texture Header

24 bytes, 12 × uint16 little-endian.

| Offset | Type   | Field       | Description                          |
|--------|--------|-------------|--------------------------------------|
| 0      | uint16 | format      | Texture format (always 3 = ARGB8888) |
| 2–12   | uint16 | unknown     | 6 × padding (always 0)               |
| 14     | uint16 | width       | Texture width in pixels              |
| 16     | uint16 | height      | Texture height in pixels             |
| 18     | uint16 | width_2     | Duplicate of width                   |
| 20     | uint16 | height_2    | Duplicate of height                  |
| 22     | uint16 | pixel_depth | Bits per pixel info (always 32)      |

### 10.6 BODY — Texture Atlas

`width × height × 4` bytes of raw BGRA pixel data (4 bytes per pixel).

The texture stores glyphs as follows:

| Channel | Content                                           |
|---------|---------------------------------------------------|
| B, G, R | Glyph color (typically white `0xFF` or grayscale) |
| A       | Glyph shape / opacity                             |

Some fonts (e.g. `font2.fnt`) are strictly binary — only values `0x00` and `0xFF` — producing sharp pixel fonts. Others (e.g. `fontmp.fnt`) use grayscale alpha values for anti-aliased rendering.

---

## 11. Graph DAT -- Navigation Graph

**File type**: Binary AI navigation graph (confirmed by reverse engineering).

**Location**: `missions/location0/level<N>/graphs/graph<taskId>.dat`

**Purpose**: Defines a network of pathfinding nodes (nav mesh) used by the AI system for unit movement. Each level can have multiple graphs (one per `AIGraph` task); most are sparse (1–10 nodes), but some can be large (100–266 nodes).

### 11.1 Header

| Offset | Size | Type     | Value / Description                       |
|--------|------|----------|-------------------------------------------|
| 0x00   | 4    | uint32   | Magic: `0xFFEEDDCC` (bytes `CC DD EE FF` in file, little-endian read) |
| 0x04   | 12   | record   | MaxNodes header (8-byte record header + int32 value indicating total node capacity) |
| 0x10   | 12   | record   | Secondary header (signature `0x040D`, unused)  |
| 0x1C   | 2    | padding  | Two zero bytes |
| 0x1E   | —    | table    | **Adjacency table (APSP)**: `MaxNodes × MaxNodes × 8` bytes |

### 11.2 Tagged Record Header (8 bytes each)

Every field (node or edge) is preceded by:

| Offset | Size | Description                     |
|--------|------|---------------------------------|
| 0x00   | 2    | Signature (big-endian uint16)   |
| 0x02   | 2    | Sub-tag (unused by the editor)  |
| 0x04   | 4    | Unknown (unused)                |

Data begins at `header_offset + 8`.

### 11.3 Adjacency Table (APSP)

Occupies bytes `0x1E` to `0x1E + (MaxNodes × MaxNodes × 8)`.

Pre-computed All-Pairs Shortest Path (Floyd–Warshall) routing table: each entry is `(int32 nodeRef, float32 distance)` = 8 bytes. A value of `nodeRef == -1` with `distance == -1.0f` indicates no connection between two nodes. The editor **preserves this table verbatim** during save.

### 11.4 Node Records (Tagged)

After the adjacency table, one node group per active node:

| Signature (BE) | Payload type    | Field          |
|----------------|-----------------|----------------|
| `0x04CE`       | int32           | Node ID        |
| `0x0495`       | double × 3      | X, Y, Z (24 bytes, local to AIGraph task world origin) |
| `0x049C`       | float32         | Gamma (orientation angle, radians) |
| `0x0423`       | float32         | Radius (influence radius, also scales 3D visual in editor) |
| `0x0429`       | int32           | Material (surface type, 0–23) |
| `0x04E5`       | pascal string   | Criteria (e.g. `"NODECRITERIA_DOOR"`, `"NODECRITERIA_VIEW"`, `"NODECRITERIA_STAIR"`) |

**Criteria string format**: 1 length byte + `<length>` bytes of data (last byte is null; empty string = length 1, byte 0x00).

### 11.5 Edge Records (Tagged)

After all node records:

| Signature (BE) | Payload type | Field              |
|----------------|--------------|-------------------|
| `0x044A`       | int32        | First node ID      |
| `0x04F6`       | int32        | Second node ID     |
| `0x0423` (alt) | int32        | Link type (engine-defined) |

Note: Link type reuses the `0x0423` signature but with a different sub-tag.

### 11.6 Node Criteria Classification

| Criteria substring | Constant               | Visual colour (editor) |
|-------------------|------------------------|------------------------|
| `"DOOR"`          | `NODECRITERIA_DOOR`    | Yellow                 |
| `"VIEW"`          | `NODECRITERIA_VIEW`    | Cyan                   |
| `"STAIR"`         | `NODECRITERIA_STAIR`   | Magenta                |
| (none / other)    | Default                | Red                    |

Precedence (when multiple flags present): **Door > Stair > View > Default**.

### 11.7 Coordinate System

All node X, Y, Z values are **local to the AIGraph task's world position** in the level's `objects.qsc`. To convert to absolute world coordinates:

```
world_pos = aiGraph_task.pos + graph_node.xyz
```

IGI world units scale to GL render units via `× 0.001`.

### 11.8 Parsing and Editing

**Full documentation** of the graph editor interface, 3D rendering, node selection, properties panel, keybindings, and backup/reset is in [graph_editor.md](graph_editor.md).

**API**:
- `GRAPH_Parse(filepath)` → `GraphFile` struct with all nodes and edges
- `GRAPH_Save(srcPath, outPath, graph)` → patch node positions in-place only (preserves adjacency table and all other fields)
- `GRAPH_Write(srcPath, outPath, graph)` → full serializer (supports add/remove nodes and edges)

**Tests**:
- `test_graph_parser.cpp` — round-trip parse + write tests
- `test_graph_overlay.cpp` — 3D projection and hit-test math (unit-tested, pure functions)
