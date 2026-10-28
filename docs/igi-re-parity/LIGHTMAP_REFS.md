# Lightmap ref indexing — the engine's rule, and what the editor was missing

Oracle: **IGI-RE**, the verified C decompilation of retail `IGI.exe`. Every claim
below cites function + address + line. Nothing here comes from web research or from
the open-igi C# port.

## The defect this closes

`ObjectLightmapManager::GetLightmapTexture` (`source/renderer/object_lightmap.cpp:125-137`)
builds the `.olm` name from a single caller-supplied integer and always returns page 0:

```cpp
GLuint ObjectLightmapManager::GetLightmapTexture(const std::string& object_name, int draw_record_index) {
    if (render_mode_ == LightmapRenderMode::Dynamic) return 0;
    char buf[128];
    snprintf(buf, sizeof(buf), "%s_%05d.olm", object_name.c_str(), draw_record_index);
    auto it = lightmaps_.find(buf);
    if (it != lightmaps_.end() && !it->second.empty()) return it->second[0].texture_id;
    return 0;
}
```

The function has **zero callers** — nothing in the repository computes the integer it
takes. That integer is not a draw-record index; it is the object's **lightmap ref
index**, and the engine derives it from the model's LOD chain. Without that rule the
function cannot be wired up correctly, which is why it sat unused.

## The rule

### One ref per LOD, not one per object

`fill_refs` — `src/sector/sector.c:544-563` (a `static` function: it has **no** hook address; it is
reached only from `SectorObject_FillMagicRefs`):

```c
static void fill_refs(int *n, LightmapSet *set, RigidDynCubeObj *o)   /* :544 */
{
    int i;
    o->lightmapRefs = &set->refs[*n];                                 /* :548 */
    for (i = 0; i < ((VirModel *)o->model)->lod_count; i++) {         /* :549 */
        LightmapRef *r = &set->refs[*n];
        r->lodIndex = i;                                              /* :557 */
        r->lodCount = ((VirModel *)o->model)->lod_count;              /* :558 */
        r->lod = ((VirModel *)o->model)->lods[i];                     /* :559 */
        r->uploaded = 0;
        (*n)++;                                                       /* :561 */
    }
}

void SectorObject_FillMagicRefs(int *n, LightmapSet *set, Task *t, int top)  /* :565-575 */
HOOK(0x4DC050, SectorObject_FillMagicRefs);                                  /* :576 */
```

So an object with `lod_count` LODs appends exactly `lod_count` consecutive refs, each
carrying its own `lodIndex` (`sector.c:557`) and its own `ModelLod` (`sector.c:559`).
Objects are walked in task order — the task list, then each task's children
(`sector.c:565-575`).

### The ref index names the `.olm` file

`Lightmap_BuildLod` — declared `src/dirlight/dirlight.h:39`, defined
`src/dirlight/lightmapbuild.c:44-45`:

```c
Lightmaps *Lightmap_BuildLod(void *lodp, int lodIndex, float scale, const char *name, int index, ...)
{
    MeshHeader *mh = ((ModelLod *)lodp)->mesh->header;
    ...
    if (mh->format != 3) return NULL;                                       /* :53 */
    mesh = (LightmapMesh *)mh->data;
    ...
    crt_sprintf(path, "%s/%s_%05d.olm", Lightmap_GetDir(), name, index);    /* :59 */
    ...
    lm->count = mesh->texCount;                                             /* :91 */
```

The `index` parameter is the ref's position in the set — the value `fill_refs`
advances at `sector.c:561`. The name is `%s_%05d.olm`, so the ref index is the
five-digit field.

### An object with no LODs contributes nothing

`VirModel_InitLods` — `src/model/model.c:145-163`, `HOOK(0x4CED50)` — sets
`lod_count` to the number of names that resolved, so a model whose own name is
missing has `lod_count == 0`. The loop bound at `sector.c:549` is then false
immediately, so the object contributes **no refs and does not advance the cursor**.
Reserving a slot for it shifts every later object's lightmap by one — the easy way to
get this wrong.

## What the editor now has

`source/renderer/lightmap_ref_index.h` — a pure, host-testable unit implementing
exactly the three rules above:

| Function | Rule | Oracle |
|---|---|---|
| `ReserveRefs(lod_count, cursor)` | reserves `lod_count` refs, advancing the cursor; returns `base = -1` and does not advance for `lod_count <= 0` | `sector.c:549`, `:561` |
| `RefIndexForLod(layout, lod_index)` | `base + lod_index`, or `-1` outside `0..lod_count-1` | `sector.c:557` |
| `OlmFileName(name, ref_index)` | `"%s_%05d.olm"` | `lightmapbuild.c:59` |
| `RefMeshIsIdentified(owner_binding, owner_lod_count, entry)` | which resolved `.olm` entries name a ref whose mesh the editor can read: the owner's own LODs `0..lod_count-1`, and only when the list came from the object's own binding | `sector.c:548-561`, `:565-575`; see `LIGHTMAP_BAKE.md` §5c |

It is header-only and has no dependency on GL, Windows, or the renderer, so
`tests/test_lightmap_ref_index.cpp` exercises it natively.

`RefMeshIsIdentified` is the limit of the engine's four-conjunct `.olm` accept test
(`lightmapbuild.c:63-64`) on the editor's side: ref `k` of a set is the **owner's**
LOD `k` only while `k < lod_count`, and the refs after that belong to the object's
magic parts and rigid children, whose order the editor does not walk. Measured over
the retail install: of the 25,337 shipped `.olm` files, 1,120 are an owner's own
LOD; all 1,120 pass the accept test against that LOD's mesh (`LIGHTMAP_BAKE.md`
§5c).

## Measured against the retail corpus

`tests/test_lightmap_ref_index.cpp` walks every model of every retail level bundle
(`MISSIONS/location0/LEVEL<n>/LEVEL<n>.MTP`, the `VNAM` name table that becomes
`ModelLod.name`, `src/model/model.c:274-289`), builds each model's chain with the
`VirModel_InitLods` rule, and lays the refs out. Verbatim output:

```
[lightmap-refs] levels=14 objects=6605 multiLod=3981 maxLod=5 totalRefs=14355 higherLodRefs=7750
```

Read that as the size of the defect:

* **6605** models across 14 retail level bundles, **14355** lightmap refs in total.
* **3981** models (60%) have more than one LOD, so they contribute more than one ref.
* **7750** of the 14355 refs (54%) belong to LOD 1 or higher — a different `.olm`
  file from the object's LOD 0.
* `maxLod = 5`, matching `VIRMODEL_LODS = 5` (`src/model/model.h:11`).

So an implementation that ignores the LOD dimension, or that looks only at an
object's first ref, is wrong for **more than half** of all retail lightmap refs. The
test asserts the layout is contiguous, that bases advance by exactly the previous
object's `lod_count`, that the numeric field in each name round-trips to the ref
index, and that `lod_count == 0` neither reserves nor advances.

## Deliberately NOT decided here

**Which page inside the `.olm` a drawn LOD uses.** `lm->count = mesh->texCount`
(`lightmapbuild.c:91`) gives the page count, and the loader
(`source/renderer/object_lightmap.cpp:96-118`) keeps every page of a file, but the key
the engine indexes `file->tex[i]` by on upload is not established from
`lightmapbuild.c` alone. No rule is asserted for it, and
`GetLightmapTexture` still returns page 0.

**Wiring.** This change supplies the missing rule; it does not add a caller. Wiring
`GetLightmapTexture` needs the ref index threaded from the sector-object walk, which
means touching `source/renderer/renderer_objects*.cpp` and the lightmap upload path.
That work is **not** done and is **not** claimed — the header and its tests are the
verifiable half, and the page-selection rule above must be settled first or the wiring
will encode a guess.

## Resolved later: the page inside the `.olm` (rule 5)

The question this file left open — which page of a LOD's `.olm` a drawn group
samples — is settled by the **draw** path, not the builder, and is now
implemented:

```c
IDirect3DDevice7_SetTexture(DEV, 1, set->tex[g->lightmap].texture.surface);
    HOOK(0x49DF60, Direct3DRender_DrawRigidMesh)  src/render/render.c:1033
```

`LmGroup.lightmap` (`+0x12`, `src/render/render.c:40-54`) indexes the file's page
array directly, with `0xFFFF` meaning "no page" (tested at `render.c:1098`,
`:1114`), and the file is the drawn LOD's ref (`render.c:1019`); a LOD with no
bake binds nothing (`render.c:1052-1054`). The editor now carries that index on
`SubMesh.lightmapPage` (`source/renderer/model.h`, set from the already-parsed
`ParsedGeometry::RenderBlock::lightmapSlot`) and binds the matching page
(`source/renderer/renderer_objects.cpp`), with `LoadOlmPages` uploading every page
of the file. Measured: 11,484 format-3 render blocks in 2,027 retail models, 7 of
them naming page 1 (all in `428_02_1.mef`), none carrying the sentinel.

`ObjectLightmapManager::GetLightmapTexture` (this file's "still unused" claim) is
**still** unused — the live path is `App::ResolveAndApplyLightmap` →
`LoadOlmPages` → `Renderer_Objects::SetLightmapForTask`. See
`docs/igi-re-parity/LIGHTMAP_BAKE.md` §5b.

## Verification status

| Claim | Status |
|---|---|
| The three rules match IGI-RE | verified — citations above, quoted from the decompilation |
| The rule is exercised natively | verified — `12 passed 0 failed`, `exit 0` |
| The corpus numbers | verified — printed by the test, 14 level bundles read |
| `GetLightmapTexture` is still unused | verified — zero callers in the repository |
| Page selection within a `.olm` | **verified** — `render.c:1033`; 7 retail blocks name page 1 (`offPage0=7`), none names `0xFFFF` |
| Any rendered lightmap is correct | **unverified** — no GL run is possible on this host |
