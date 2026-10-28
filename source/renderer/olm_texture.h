#pragma once
#include "../pch.h"
#include "../runtime/lightmap_rules.h"
#include <string>
#include <vector>

// Reads an IGI1 `.olm` lightmap straight from disk and uploads EVERY page as its
// own RGBA GL texture, in page order (`*pages` receives one GLuint per page).
//
// THE ENGINE'S ACCEPT TEST RUNS HERE. `mesh` is the MESH side of the four
// conjuncts `Lightmap_BuildLod` evaluates before it will use a file on disk
// (lightmapbuild.c:63-64):
//
//     file->magic == 0x3DF5C28F
//     && feq(mh->version, file->version)
//     && DateTime_Compare(mh->time, file->time) < 0
//     && mesh->texCount == file->count
//
// and it is evaluated by `igi::lightmap_rules::OlmHeaderAccepted`, the same
// predicate the unit tests pin. On a rejection the engine does NOT use the file:
// it flushes it (`Resource_Flush(file)`, lightmapbuild.c:86) and builds a fresh
// `Lightmaps` in memory (:90-100). The editor cannot bake in process (the bake is
// `igi1conv lightmap recalc`), so the equivalent outcome is that the rejected
// file binds nothing and the LOD draws dynamic lighting -- exactly what a missing
// or corrupt `.olm` already does. It never falls back to another file or to
// another page.
//
// `mesh == nullptr` means the CALLER could not identify the mesh of the ref this
// file belongs to (see `igi::lightmap::RefMeshIsIdentified`), so the engine's test
// cannot be run; the file is loaded as before. That case is named and counted by
// the caller rather than silently accepted: the engine always has the ref's mesh.
//
// WHY EVERY PAGE, AND NOT JUST THE FIRST. The engine's `Lightmaps` struct holds
// `count` `LightmapTex` records (src/litmap/litmaptypes.h:36-48) and the draw path
// binds the page the DRAWN RENDER GROUP names:
//
//   IDirect3DDevice7_SetTexture(DEV, 1, set->tex[g->lightmap].texture.surface);
//   HOOK(0x49DF60, Direct3DRender_DrawRigidMesh)  src/render/render.c:1033
//
// with `lm->count = mesh->texCount` written by the builder
// (HOOK(0x523D40, Lightmap_BuildLod), src/dirlight/lightmapbuild.c:91). A reader
// that keeps only page 0 therefore draws the wrong atlas for every group whose
// `LmGroup.lightmap` is not 0.
//
// THE LAYOUT IS THE ENGINE'S, NOT A DESCRIPTOR GUESS. Header 0x3C bytes, then
// `count` records of 0x2C bytes, then the pixel blocks in page order
// (lightmapbuild.c:72-78: `px = (char *)&file->tex[file->count]`, `px += n * 4`
// per page). The previous reader assumed an "88-byte header + 16-byte layer
// descriptor" layout; for `count == 1` the two agree exactly (60 + 44 == 104,
// and the two page shorts land on 100/102), which is why it worked -- and for
// `count > 1` it silently dropped pages 1..n. Measured on the retail install:
// 13 shipped `.olm` files carry more than one page (12 with 2, one with 3), and
// model `428_02_1.mef` is the one whose render groups name page 1 (7 of its
// blocks; see docs/igi-re-parity/LIGHTMAP_BAKE.md).
//
// This is the fast load path for applying baked lightmaps: it avoids spawning
// `igi1conv olm to-png` once per .olm (which was hundreds of subprocesses per
// building). The igi1conv converter remains the tool for WRITING / recalculating
// .olm files. The R/B channel swap igi1conv applies on export is reproduced.
//
// Returns false on any failure, with `err` set and `*pages` emptied; any texture
// already created for the file is deleted before returning.
bool LoadOlmPages(const std::string& olmPath,
                  const igi::lightmap_rules::OlmAcceptInput* mesh,
                  std::vector<GLuint>* pages, std::string* err);
