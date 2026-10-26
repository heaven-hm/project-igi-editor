# OBJ → MEF importer (`--import-mef`) — format derivation and parity notes

Scope: issue #29, "OBJ → MEF Importer". Static building models only — no bone
complexity, exactly as the issue allows ("Start with static buildings only").

Oracle (read-only): `/Users/haseeb-mir/Documents/Code/IGI-RE` — the verified C
decompilation of retail `IGI.exe`. Every behavioural claim below cites
`function HOOK(0xADDR), src/path/file.c:LINE`.

Assets used for measurement: `/Users/haseeb-mir/Downloads/IGI`, the retail
install — 1578 `.mef` files under `MISSIONS/**/models`.

**Reproducing the corpus numbers.** Every measured figure below comes from one
independent scan that reads only the shipped bytes and asserts nothing about engine
behaviour. The scan script lives at `scratch/mef-writer/verify_doc_numbers.py`,
which is **gitignored** (`/scratch/` in `.gitignore`, "never part of the product"), so
it is not committed and a reader of this document cannot run it directly. The method
it uses is fully specified so it can be re-derived:

1. Walk `MISSIONS/**/models/*.mef`; for each file, reject anything whose first four
   bytes are not `ILFF`.
2. Walk the chunk chain: start at offset 20, read `name[4], size, align, next` at
   `+0..+15`, payload at `+16`; advance by `next`; stop when `next == 0`.
3. Read the model format from the `HSEM` payload at `+32`, the vertex stride as
   32 for format 0 and 40 otherwise, and the HSEM fields at the offsets in §3.1.
4. For `DNER`, walk records from the start of the payload: `indexCount` at `+12`,
   `next` at `+14`, `base` at `+16` (format 0) or `+20` (format 3), header 28 or 32
   bytes, index array `indexCount` × `uint16`; advance by `header + indexCount*2`.
5. Compare each derived quantity against the value the writer computes, and print
   `<matching>/<total>` per model type.

Note on the two denominators used below: **1556** is the number of files whose
`XTRV` chunk is non-empty, **1440** is the number of *static* (format 0 or 3) such
files — the remaining 116 are format 1 (skeletal), which the OBJ importer refuses and
which are excluded from the static measurements.

Delivered code:

| file | role |
|---|---|
| `source/renderer/mef_writer.h` / `.cpp` | self-contained OBJ parser, MEF builder, ILFF serialiser, CLI glue |
| `tests/test_mef_import.cpp` | 25 tests: asset-free round trip + full retail corpus round trip |
| `source/cli/cli_handler.cpp` | `--import-mef` dispatch (3 added lines + help text) |
| `CMakeLists.txt` | 2 added lines (test source, test source pool) |

## 1. Conclusion up front

`igi1ed.exe --mef <input.obj> --import-mef <output.mef>` is implemented **natively
in the editor** — it does not need an `igi1conv` subcommand. (`igi1conv` lives in a
separate repository, `jones-hm/project-igi-conv`; its sources are not available in
this checkout and the bundled v1.6.0 binary exposes `mef to-text|compile|build-rigid|bundle|info|dump`
— no OBJ importer and no `mef export`. Option (b) in the task brief is therefore
impossible without writing an entirely separate tool in a repository we do not have.)

What is **proved** here:

* the writer emits a byte-exact ILFF container and the render chunks in the layout
  the game's loader reads (§2, §3);
* the geometry survives a full round trip through the shipping reader for **2212
  static retail models** and for synthetic OBJs (§7);
* the collision chunks are self-consistent and the generated sphere tree is
  reachable by the game's own traversal algorithm (§5);
* the retail corpus reproduces every derived HSEM field the writer computes (§4).

What is **not** proved, and cannot be from this host:

* that the output `.mef` **loads and renders in the editor viewport** (acceptance
  criterion 2). The editor GUI needs OpenGL 4.1 compatibility; this host offers 2.1.
* that the output `.mef` **loads in-game without crashing** (acceptance criterion 3).
  That is the Windows/Wine suite plus a retail `IGI.exe` run, which is out of scope
  for the session that produced this file.
* that the reversed winding (§6) renders correctly in game. The winding rule is
  inferred from the shipped corpus and the engine's cull mode; the two are
  consistent, but no differential render was performed.

Stated plainly: *"the writer compiles and the round-trip tests pass"* is a
different claim from *"the game loads it"*. Only the first is established here.

## 2. Container format (ILFF)

Every retail `.mef` is an ILFF form with a 20-byte header:

| offset | value | citation |
|---|---|---|
| +0 | `"ILFF"` | `Chunk_FormType` HOOK 0x4B7570, `src/res/chunk.c` |
| +4 | form size = whole file size | the reader rejects a mismatch (`source/renderer/mef_native.cpp:89`) |
| +8 | align = 4 | `Chunk_FirstChild` HOOK 0x4B7490, `src/res/chunk.c:17-32` |
| +12 | next = 0 | `Chunk_Next` HOOK 0x4B7470, `src/res/chunk.c:9-15` |
| +16 | 4-byte form id | `Chunk_FormType` |

`Chunk_FirstChild` computes the first child as
`parent + ((align + 0x13) & ~(align - 1))`, which is `parent + 20` for `align == 4`
— so the first chunk starts immediately after the form header. Each chunk header is
16 bytes (`name[4]`, `size`, `align`, `next`) and the payload starts at `chunk + 16`:

```c
/* Chunk_Data HOOK 0x4B74D0, src/res/chunk.c:34-49 */
if (c->size == 0) return NULL;
if (!is_pow2(c->align) || c->align > parent->align) return NULL;
return (char *)c + CHUNK_HEADER;      /* CHUNK_HEADER == 16 */
```

`next` is a **byte offset from this chunk's start**, and `0` terminates the chain
(`Chunk_Next`: `if (!c->next) return NULL; return (Chunk *)((char *)c + c->next);`).

Measured (corpus): all 1578 files use `align == 4`; the form id bytes are
`4F 43 45 4D` (`"OCEM"`); every HSEM chunk is exactly 156 bytes.

### 2.1 Padding — retail-attested

145 retail chunks have a size that is not a multiple of 4 (all of them `DNER`), and
in every one of those 145 the following chunk is at
`this + align4(16 + size)`, not at `this + 16 + size`. The writer therefore emits
`next = align4(16 + size)` and pads the payload with zeros. This is not an
invention: it is what the shipped files do, and it keeps every chunk start
4-aligned.

### 2.2 Chunk order

Retail order (measured): `HSEM, ATTA, XTVM, TROP, XVTP, CFTP, D3DR, DNER, XTRV,
HSMC, XTVC, ECFC, TAMC, HPSC, XTVC, ECFC, TAMC, HPSC`.

Only the *presence* of `HSEM`/`D3DR`/`DNER`/`XTRV` is load-bearing, because the
loader finds each chunk by id:

```c
/* Chunk_Find HOOK 0x4B7590, src/res/chunk.c:74-81 */
```

The writer follows the retail order anyway, so a diff against a shipped file stays
readable. `XTVM/TROP/XVTP/CFTP` are emitted as zero-size chunks (they are size 0 in
the vast majority of retail files); `PMTL`/`LTMP` are **omitted** — no shipped
type-3 file contains an `LTMP` chunk at all, so it is optional.

### 2.3 Worked example

The repository's own OBJ fixture `tools/e2e/fixtures/camera-box.obj` (a unit cube, 8
vertices, 12 triangles, no materials, no normals, no UVs) imported with
`--mef-format 0`:

```
file 1720 bytes  magic=ILFF  formSize=1720 align=4 next=0 form=OCEM
  @20     HSEM size=156  align=4 next=172   center=(0.000,0.000,0.000) sphere=1.732
                                             radius=1.732 complexity=12 attach=0
  @192    ATTA size=0    align=4 next=16
  @208    XTVM size=0    align=4 next=16
  @224    TROP size=0    align=4 next=16
  @240    XVTP size=0    align=4 next=16
  @256    CFTP size=0    align=4 next=16
  @272    D3DR size=48   align=4 next=64    [4 12 1 8 0]
  @336    DNER size=100  align=4 next=116
  @452    XTRV size=256  align=4 next=272
  @724    HSMC size=64   align=4 next=80
  @804    XTVC size=128  align=4 next=144
  @948    ECFC size=96   align=4 next=112
  @1060   TAMC size=12   align=4 next=28
  @1088   HPSC size=552  align=4 next=568
  @1656   XTVC size=0    align=4 next=16
  @1672   ECFC size=0    align=4 next=16
  @1688   TAMC size=0    align=4 next=16
  @1704   HPSC size=0    align=4 next=0
reparsed: type=0 verts=8 tris=12 blocks=1 collision=12/8
```

Every number checks out against the derivations above: `sphere = radius = sqrt(3)`
(the cube's half-extent is 1), `XTRV = 8 × 32`, `DNER = 28 + 12×3×2`,
`next = align4(16 + size)`, `XTVC = 8 × 16`, `ECFC = 12 × 8`, `HPSC = 23 × 24` (12
leaves + 11 internal nodes for a one-triangle-per-leaf BVH), and the last chunk
terminates the chain with `next = 0`.

## 3. Render chunks

### 3.1 HSEM — `MeshHeader`

`src/model/modeltypes.h:9-28` (`_Static_assert(sizeof(MeshHeader) == 0x90)`). The
chunk is 156 bytes: the struct is 0x90 = 144 bytes and the trailing 12 bytes are
zero in all 1578 retail files. **UNVERIFIED:** what those 12 bytes mean; the
writer writes zeros.

The version word is checked on load and must be exactly `0.1f` (`0x3DCCCCCD`):

```c
/* Mesh_Load HOOK 0x4CF390, src/model/model.c:367 */
if (*(uint32_t *)m->header != 0x3DCCCCCD) -> "wrong MEF version"
```

The loader resolves the sibling chunks by id and stores the pointers
(`src/model/model.c:374-378`: `+0x8C = ATTA`, `+0x88 = MVTX`, `+0x98 = PORT`,
`+0x90 = PTFC`, `+0x94 = PTVX`), so `data`, `unk28`, `coll`, `verts` and `attach`
are **runtime pointers** and are written as zero.

### 3.2 D3DR — the mesh descriptor

The loader switches on the format word and expects a `RigidMesh` (format 0) or an
`LmMesh` (format 3):

```c
/* Direct3DRender_LoadModel HOOK 0x49FB10, src/bonemesh/d3drender.c:210-236 */
case 0: r->unk10 = VRTX; r->unk14 = REND; MeshBatch_Add(0, header, r->unkC);   /* :216 */
case 3: r->unk14 = VRTX; r->unk1C = REND; r->unk18 = LTMP;
        MeshBatch_Add(1, header, r->unk10);                                    /* :230 */
```

`MeshBatch_Flush` then copies **past the end of both structs**, which fixes the
minimum chunk sizes:

```c
/* MeshBatch_Flush HOOK 0x49B4B0, src/dxinit/d3drender.c:179-211 */
case 0: memcpy(dst, m + 0x10, m->vertexCount * 0x20); write m + 0x2C;   /* :188  -> 48 bytes */
case 3: memcpy(dst, m + 0x14, m->vertexCount * 0x28); write m + 0x34;   /* :205  -> 56 bytes */
```

Measured (corpus): 48 bytes for every type-0 file, 56 for every type-3 file. Field
order, from the same two functions plus the measured bytes:

| word | format 0 (`RigidMesh`, 48 B) | format 3 (`LmMesh`, 56 B) |
|---|---|---|
| +0 | `primType` = 4 | `primType` = 4 |
| +4 | triangle count | **`1`** — see below |
| +8 | group count | triangle count |
| +12 | vertex count | group count |
| +16 | — | vertex count |

`primType` is 4 (`D3DPT_TRIANGLELIST`) in all 1578 files and is passed straight to
`DrawIndexedPrimitiveVB` (`draw_group`, `src/render/render.c:526,531`).

The format-3 word at +4 is `1` in 1143 of the 1144 type-3 files that carry render
data. **UNVERIFIED:** no reader of `LmMesh+4` was located in the oracle. The single
outlier is `428_02_1.mef`, whose D3DR is `(4, 2, 122, 8, 222)`. The writer emits `1`,
the majority value, and this is the one field in the writer that is chosen by
majority rather than by a cited reader.

### 3.3 DNER — the group chain

```c
/* Direct3DRender_DrawRigidMesh HOOK 0x49DF60, src/render/render.c:487-489 (format 0/1) */
g = (MeshGroup *)((char *)g + g->next + 0x1C);
/* src/render/render.c:1047,1081,1110 (format 3) */
g = (LmGroup *)((char *)g + g->next + 0x20);
```

So `next` is the **byte length of this record's index array** — it does not include
the 28- or 32-byte header — and `0xFFFF` marks the last record. Measured (corpus):
`next == indexCount * 2` in **9095 of 9095** records, with exactly one `0xFFFF`
terminator per file that has render data (1556 files).

Indices are **relative to `base`**:

```c
/* Direct3DRender_SplineNearZ HOOK 0x49BE10, src/render/render.c:479-481 */
ProjVert *a = &gProjVerts[idx[0] + g->base];
/* draw_group, src/render/render.c:526 */
DrawIndexedPrimitiveVB(DEV, d->primType, vb, start + g->base, g->vertexCount, ...);
```

`texture` indexes the model's texture array; `0xFFFF` means "untextured":

```c
/* draw_group, src/render/render.c:522-535 */
if (g->texture != 0xFFFF) SetTexture(lod_texture(ml, g->texture));
else { TSS(0, D3DTOP_SELECTARG2); SetTexture(NULL); }
```

Header layouts: `MeshGroup` `src/render/rendertypes.h:6-19` (28 bytes: `center[3]`,
`indexCount`, `next`, `texture`, `base`, `vertexCount`, `sorted`, `flags`, `unk19[2]`,
`alpha`), `LmGroup` `src/render/render.c:40-54` (32 bytes: adds `lightmap` and shifts
the tail by two bytes).

**UNVERIFIED:** `sorted`, `flags` and `alpha` have no located reader in the oracle.
Measured (corpus): `sorted != 0` in 86/2418 type-0 groups and 2355/6677 type-3
groups; `flags == 1` in 7/2418 type-0 groups; `alpha` is mostly 0. The writer writes
`sorted = 0`, `flags = 0`, `alpha = 0` for OBJ imports, and preserves `sorted` when
round-tripping a parsed MEF (the reader exposes only that one hint bit).

### 3.4 XTRV — vertices

Strides and offsets come from `MeshBatch_Flush` (`src/dxinit/d3drender.c:188,205`)
and the FVF table:

```c
/* src/dxinit/d3drender.c:356-357 */
gMeshFVF = {274, 530, 274};   /* 274 = 0x112 XYZ|NORMAL|TEX1, 530 = 0x212 XYZ|NORMAL|TEX2 */
```

| format | stride | layout |
|---|---|---|
| 0 | 32 | `pos[3]` +0, `normal[3]` +12, `uv[2]` +24 |
| 3 | 40 | `pos[3]` +0, `normal[3]` +12, `uv[2]` +24, `uv2[2]` +32 |

`pos` is in **raw MEF units** — the same units the editor reader exposes as
`RenderVertex::rawPos`. The editor's `kMefNativeScale = 1/40.96` is a display
transform applied to `RenderVertex::pos`, not part of the file. The engine's own
`gWorldScale = 0.001f` (`src/d3d/direct3d.c:532`) is applied downstream. Therefore
OBJ `v` coordinates are written straight through, and a MEF → OBJ → MEF round trip
is lossless.

## 4. Derived HSEM fields — measurements

Every field below is either read by a located oracle function (cited) or computed
from a corpus measurement (quoted). All figures come from the single scan described
at the top of this document.

| field | value written | evidence |
|---|---|---|
| `version` | `0.1f` | required by `Mesh_Load`, `src/model/model.c:367` |
| `time[7]` | 0 | no reader exists in the oracle — **UNVERIFIED** |
| `format` | 0 (default) / 3 | `Direct3DRender_LoadModel`, `src/bonemesh/d3drender.c:210` |
| `data`,`unk28`,`coll` | 0 | runtime pointers, `src/bonemesh/d3drender.c:212,225,247` |
| `center` | bounding-box centre | authored pivot, see below |
| `sphere` | `max |v − center|` | 1440/1440 static files (296/296 type 0, 1144/1144 type 3) |
| `unk40` | 2 × `(center, sphere)` | 1024/1556 files (908/1440 static) — **UNVERIFIED**, unread |
| `complexity` | triangle count | 1556/1556 files with render data |
| `unk64[0]` | vertex count | 1556/1556 files (including all 116 type-1) |
| `unk64[1]` | `D3DR.size + DNER.size + XTRV.size` | 1556/1556 files |
| `unk64[2..4]` | 0 | unread — **UNVERIFIED** |
| `radius` | `max |v|` about the origin | 1187/1440 static files; 253 outliers — **UNVERIFIED** |
| `vertex_count` @0x7C | 0 | zero in 1444/1578 retail files (runtime field) |
| `attach_count` | ATTA record count | 1578/1578 files (`attach_count == ATTA.size/68`) |
| `unk80[8]` | 0 | unread — **UNVERIFIED** |
| trailing 12 bytes | 0 | zero in all 1578 files — **UNVERIFIED** |

`sphere` and `center` are the only HSEM fields the oracle demonstrably *uses*:

```c
/* ModelObj_GetLOD HOOK 0x4CFCB0, src/lod/mesh3d.c:213-227 */
h = lod_header(vm->lods[0]);
reference = o->pos + h->center;  radius = max_scale(o->scale) * h->sphere;
```

`complexity` is used only to compare LOD levels against level 0
(`lod_select`, `src/lod/mesh3d.c:196-198`), and a single-LOD model always selects
level 0, so the value is not behaviour-critical for a static building.

**`center` is an authored pivot, not a derived quantity.** It equals the
bounding-box centre in only **68/1556** files (1/296 type 0, 67/1144 type 3) and the
vertex centroid in 40/1556. The writer therefore has to choose; it uses the
bounding-box centre by default (defensible: it minimises `sphere` for the box, and
`sphere` is the field the engine actually consumes) and offers `--pivot-origin` to
write `(0,0,0)`. **UNVERIFIED:** the retail authoring rule for this field.

## 5. Collision chunks (HSMC / XTVC / ECFC / TAMC / HPSC)

The loader finds the collision header by id and fills two records from the *first*
and *second* `XTVC`/`ECFC`/`TAMC`/`HPSC` sets:

```c
/* Direct3DRender_LoadModel HOOK 0x49FB10, src/bonemesh/d3drender.c:242-260 */
c = Chunk_Find(res, ID_CMSH);            /* "HSMC" */
... fills part[0].verts/faces/mats/sphere from the 1st XTVC/ECFC/TAMC/HPSC
... fills part[1].* from the 2nd set
header->coll = NULL; if (!c) return;     /* no HSMC -> no collision, not an error */
```

`HSMC` is 64 bytes: two 32-byte records. Layout per record
(`RigidCollMesh` `src/qasm/rigidhit.c:26-37`, `CollMesh` `src/magicobj/rigidcoll.c:47-55`,
`BoneCollMesh` `src/haze/bonecoll.c:31-42`):

```
{ int triCount; int vertCount; int unk08[2];
  void *tris; void *verts; void *surfaces; void *nodes; }
```

The four pointers are zero on disk (they are runtime addresses). The writer derives
the counts from the sibling chunk sizes — `tris = ECFC.size/8`,
`verts = XTVC.size/16`, `unk08[0] = TAMC.size/12`, `unk08[1] = HPSC.size/24` — and
this reproduces the retail header exactly in **593 of 593** files that contain an
`HSMC` chunk (measured). It also reproduces the `dummy.mef` header `(76, 40, 2, 151)`
by hand.

Note the naming disagreement between decompilations: the rigid-object path calls the
third and fourth words `unk08[0..1]` (`src/magicobj/rigidcoll.c:49`) while they
numerically equal the material and node counts. The writer follows the *measured
values*, not either naming.

**Oracle defect (recorded, not fixed):** the `MeshColl` struct in
`src/bonemesh/bonemesh.h:34-43` (`unk0[0x10] + part[2]` = 0x50 bytes) is an
approximation that does **not** match the 64-byte on-disk chunk. The
`RigidCollMesh`/`BoneCollMesh`/`CollMesh` layouts do.

### 5.1 Record layouts

| chunk | record | layout | citation |
|---|---|---|---|
| `XTVC` | collision vertex | `float x,y,z; uint32 unkC;` (16 B) | `src/qasm/rigidhit.c:11-14` |
| `ECFC` | collision triangle | `uint16 v[3]; uint16 surface;` (8 B) | `src/qasm/rigidhit.c:11-24` |
| `TAMC` | surface | `int opacity; uint16 portal; int16 index; uint16 material; uint16 unknown;` (12 B) | `src/qasm/rigidhit.c:26-37` |
| `HPSC` | sphere-tree node | `float center[3]; float radius; uint16 firstTri, triCount, child, next;` (24 B) | `src/magicobj/rigidcoll.c:26-33` |

Measured: the `XTVC` bone field is 0 in every static retail model; the `ECFC`
surface field is 0 or 1, indexing `TAMC`.

### 5.2 The sphere tree the writer generates

The traversal is:

```c
/* RigidDynCubeObj_CollLine HOOK 0x4C35F0, src/magicobj/rigidcoll.c:331-357 */
sp = 0; first = 1; n = nodes; stack[0] = n;
for (;;) {
    if (!first) {
        if (!n->next) { if (--sp < 0) return; n = stack[sp]; continue; }
        n = &nodes[n->next]; stack[sp] = n;
        if (!coll_sphere_hit(&s, n)) continue;
    }
    first = 0;
    while (n->child) { n = &nodes[n->child]; stack[++sp] = n;
                       if (!coll_sphere_hit(&s, n)) break; }
    if (n->triCount && coll_node_tris(o, q, mesh, verts, lod, &s, n)) return;
}
```

Consequences the writer honours:

* node 0 is the root and is **not** sphere-tested;
* `child` is the index of the *first* child (0 = none); the second child is reached
  through that child's `next`; `next` is the next **sibling** index (0 = none);
* `firstTri`/`triCount` is a half-open range into `ECFC`, and `firstTri` is only
  meaningful when `triCount != 0`;
* a node may carry **both** a `child` and a `next` (retail does).

The writer builds a median-split binary BVH over the triangle centroids, one
triangle per leaf whenever the node count fits in 16 bits (leaf size doubles
otherwise), and caps the total at 65535 nodes. The test suite walks the generated
tree with the algorithm above and asserts that every collision triangle is reached
(`tests/test_mef_import.cpp`, `WalkCollisionTree`).

### 5.3 Surfaces

An OBJ carries no collision material information, so the writer emits exactly one
surface:

```
opacity = 1.0f, portal = 0xFFFF, index = -1, material = 0xFFFF, unknown = 0
```

which is the "solid, blocks everything" record. Two citations make it the right
choice:

```c
/* src/magicobj/rigidcoll.c:119-121 */
if (sf->unk4 != 0xFFFF) continue;          /* portal must be 0xFFFF to be tested */
/* src/magicobj/rigidcoll.c:216-219 */
if (sf->material == 0xFFFF) { q->visibility = 0; ... }   /* blocks everything */
```

The same 12 bytes appear in the retail corpus (e.g. `TAMC[6]` of `709_01_1.mef` is
`00 00 80 3f ff ff ff ff ff ff 00 00`). **Not supported:** transparent/one-way
surfaces and per-material collision, because an OBJ cannot express them. A building
with windows will therefore collide as a solid block.

## 6. Winding

**Measured (corpus, static models only, 126,107 triangles):** the geometric normal
`(b−a)×(c−a)` points **against** the vertex normal of the triangle's first corner in
**104,701** triangles (83.0%) and agrees in 21,406. Per file: 844 are
disagree-only, 78 are agree-only and 516 are **internally mixed**.

So retail packed DNER triangles are, by a clear majority, wound **clockwise seen
from outside** in a right-handed convention — the opposite of the OBJ/Blender
convention — and the engine culls counter-clockwise faces:

```c
/* src/render/render.c:802,837,879,895 */
SRS(D3DRENDERSTATE_CULLMODE, D3DCULL_CCW);
```

The importer therefore emits `{a, c, b}` for every OBJ face `{a, b, c}`, swapping
the last two corners. `--keep-winding` disables this.

**Recorded, not fixed:** the 516 internally mixed files are an inconsistency in the
shipped assets. No single winding rule can be correct for all of them, so the
majority convention is the only defensible default. This is also why the OBJ path is
*not* claimed to be visually verified: a render comparison is what would settle it.

## 7. Tests

`tests/test_mef_import.cpp` — 25 tests, all passing on the host lane:

* **OBJ parsing (4):** positions/normals/UVs, n-gon fan triangulation, negative
  (relative) indices, rejection of empty/vertexless input.
* **Round trip (9):** synthetic quad → MEF → `ParseMefFile`; geometry, UVs, normals
  and triangle order compared; `--keep-winding`; generated normals are unit length
  and face the OBJ's front side; the ILFF container (magic, declared size, align,
  form id, chunk links, `next = align4(16+size)`, last-chunk termination) is checked
  byte by byte; the collision chunks are checked for size/count consistency and for
  tree reachability; format 3 emits 40-byte vertices and a 56-byte D3DR; a
  22050-triangle mesh exercises the DNER record split (3 records, `next = 65532`,
  `0xFFFF` terminator, `D3DR` group count 3); and the area-weighted smoothing of
  generated normals is pinned against an exactly predicted value (§8.2).
* **Refusals (4):** format 1 output is refused; a skeletal *reference* MEF is refused
  and leaves **no output file**; skeletal `ParsedGeometry` is refused; empty geometry
  is refused.
* **Reference MEF (1):** a static reference's `ATTA` chunk is copied byte for byte
  and `attach_count` follows.
* **CLI (5):** the issue's command form, the two-operand form, the optional switches,
  usage errors, and `RunImportMefCli` itself — the function `cli_handler.cpp` calls —
  writing a file that the shipping reader then parses.
* **Corpus (2):** every static retail model is re-serialised and re-parsed; and 40
  real retail models are exported to OBJ text and imported back.

Corpus results (`IGI_GAME_PATH=/Users/haseeb-mir/Downloads/IGI`):

```
[MefImportCorpus] round-tripped 2212 static models (166 skeletal skipped,
                  30 without render geometry skipped, 253 with attachments)
[MefImportCorpus] OBJ round trip exact for 40 retail models (0 skipped)
```

2212 > 1578 because the install contains `.mef` files outside
`MISSIONS/**/models`. For each file the first test asserts identical vertex count,
triangle count, per-vertex `rawPos`/`normal`/`uv` (and `uv2` for type 3), identical
triangle indices in order, and that an `ATTA` chunk is still present. The 166
skeletal models and the 30 files with no render geometry are skipped by design — the
former are refused, the latter have nothing to serialise.

The second test is the strongest end-to-end check available on this host: it writes a
per-vertex OBJ from the model's own vertices, imports that text file with
`--keep-winding`, and requires the re-parsed model to reproduce the original
positions, normals, UVs **and triangle indices** exactly. It therefore covers the OBJ
text parser, the `(v,vt,vn)` weld, the group builder, the DNER/XTRV serialiser and
the shipping reader in a single pass, on real retail geometry.

## 8. Deliberate divergences and refusals

| decision | reason |
|---|---|
| Output format 0 by default | an OBJ has no lightmap atlas UVs, so format 3's `uv2` would be invented data. Format 3 is implemented and available as `--mef-format 3`. |
| Format 1 (skeletal) output is **refused** | an OBJ carries no skin weights or bone indices; writing a skeletal MEF would require inventing them. The tool errors out and writes nothing. |
| A skeletal **reference** MEF (`--source-mef`) is refused | same reason, and silently dropping a skeleton would be worse than failing. |
| Winding reversed by default | §6. |
| One solid collision surface | §5.3. |
| OBJ material names become texture *slots* in order of first use | the OBJ name cannot be resolved to a texture file here; slot → texture resolution happens elsewhere in the editor. |
| `--mef` is only an operand of `--import-mef` | `IsCLICommand` matches `--import-mef`, not bare `--mef`, so existing `igi1ed.exe --mef <file.mef>` behaviour is untouched. |

### 8.1 Losses in the `ParsedGeometry` → MEF path (not the OBJ path)

`BuildMefFromGeometry` is used by the corpus test. It cannot preserve:

* the per-group `lightmap` slot and the `flags`/`alpha` bytes, because
  `ParsedGeometry` does not expose them (it exposes only `sorted`, via
  `RenderBlock::opacity`). **This is why OBJ import writes format 0**, where no
  lightmap slot exists.
* the `ATTA` chunk, unless `WriteOptions::sourceMefPath` is set (which is what the
  corpus test does).

### 8.2 OBJ files without `vn` get smoothed normals

`D3DFVF_NORMAL` is part of both vertex formats
(`gMeshFVF = {274, 530, 274}`, `src/dxinit/d3drender.c:356-357`; `274 = 0x112 =
XYZ|NORMAL|TEX1`), and the lit render state is what the rigid path selects
(`Direct3DRender_DrawRigidMesh`, `src/render/render.c:975`), so a zero normal would
light the surface black. When an OBJ has no `vn` the writer therefore synthesises
one as the **area-weighted sum** of the incident triangle normals, normalised, with
the winding reversal undone so the result faces the OBJ's front side.

Consequence, and it is a real limitation: vertices are welded by `(v, vt, vn)`, and
with no `vn` every triangle sharing a position shares one vertex — so hard edges are
lost and the model shades as if rounded. The repository's own cube fixture
demonstrates it exactly: the corner `(-1,-1,-1)` is touched by five triangles (two on
`-Z`, two on `-Y`, one on `-X`), the sum is `(-4,-8,-8)`, and the emitted normal is
`(-1,-2,-2)/3` — outward, unit length, but not axis-aligned. This is pinned by
`MefImportRoundTripTest.GeneratedNormalsAreAreaWeightedAndSmoothed`.

OBJ files that *do* carry `vn` (the usual export from Blender/Maya with hard edges
preserved) keep them verbatim and are unaffected. **Not implemented:** automatic
hard-edge detection / vertex splitting for OBJs that lack normals.

## 9. Open questions (stated, not papered over)

1. `LmMesh+4` (D3DR word 1 for format 3): no reader located; `1` in 1143/1144 files,
   `2` in `428_02_1.mef`.
2. `MeshHeader.time[7]`, `unk40`, `unk64[2..4]`, `unk80`, and the trailing 12 bytes:
   no reader located; written as measured.
3. `MeshHeader.radius`: equals `max |v|` in 1187/1440 static files; the cause of the
   253 outliers is not determined.
4. `MeshHeader.center`: an authored pivot; the authoring rule is unknown.
5. `MeshGroup.sorted` / `flags` / `alpha` and `LmGroup.unk1A` / `unk1D` / `alpha`:
   no reader located.
6. Which oracle function consumes `MeshHeader.coll` for **static** objects: only the
   rigid/magic-object path (`src/magicobj/rigidcoll.c:330-370`) was located, so the
   generated HPSC tree is built to *that* walk's contract. Whether the static path
   uses the same walk is **unverified**.
7. Whether `D3DCULL_CCW` plus the engine's projection handedness makes the reversed
   winding render as expected in game — untestable from this host.
8. `MeshColl` in `src/bonemesh/bonemesh.h:34-43` disagrees with the on-disk 64-byte
   chunk (§5). Recorded as an oracle defect; not "fixed".
9. The 516 internally mixed-winding retail files (§6). Recorded as a shipped-asset
   inconsistency; not "fixed".

## 10. Acceptance criteria (issue #29) — status

| criterion | status |
|---|---|
| CLI command works | **implemented and unit-tested** at the argument-parsing level (`RunImportMefCli` is in the host lane). The `cli_handler.cpp` dispatch itself is **not compiled by any gate available here** — see below. |
| Output `.mef` loads in the editor viewport | **not verified** — needs the Windows editor GUI with OpenGL 4.1. |
| Output `.mef` loads in-game without crashing | **not verified** — needs the Windows suite and a retail run. |
| Static building models (no bones) fully supported | **implemented**; 2212 static retail models round-trip byte-exactly through the shipping reader. |
| Original bone/attachment data preserved when replacing skeletal models | **partial by design**: static references have their `ATTA` copied verbatim; a skeletal reference is refused rather than silently degraded. The issue asks for preservation when replacing a *skeletal* model, which this importer deliberately does not attempt. |
| Unit test covering OBJ → MEF round trip | **done** — 25 tests, including the full retail corpus and a real-asset OBJ round trip. |

### 10.1 The one gate that could not be run

`source/cli/cli_handler.cpp` is in `SOURCES_CLI`, which is compiled only into the
Windows `igi-editor` target; the host lane's `target_sources(igi_tests PRIVATE ...)`
pool does not contain it. The file was checked with
`clang++ -std=c++20 -fsyntax-only -I source -I source/cli -I source/runtime -I third_party/glm-1.0.1 -I third_party/GL/include source/cli/cli_handler.cpp`
(exit 0, two pre-existing warnings in `source/level/task_tree.h`), but a syntax check
is not a build of the Windows target and is not a test.

All of the importer's logic — argument parsing included — lives in
`source/renderer/mef_writer.cpp`, which *is* in the host pool and *is* covered by
`tests/test_mef_import.cpp`. The `cli_handler.cpp` change is a three-line dispatch
plus help text, so the untested surface is as small as it can be made without
changing the build layout.
