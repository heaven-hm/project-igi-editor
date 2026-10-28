#pragma once
#include "../pch.h"
#include <string>
#include <vector>

// Reads an IGI1 `.olm` lightmap straight from disk and uploads EVERY page as its
// own RGBA GL texture, in page order (`*pages` receives one GLuint per page).
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
// The magic (0x3DF5C28F) and version (0.1f) are the two file-side conjuncts of
// the engine's accept test (lightmapbuild.c:26, :63, :98); every one of the
// 25,337 shipped `.olm` entries and all 2,667 shipped `.mef` MESH headers carry
// exactly those bits, so checking them here cannot reject retail data.
//
// This is the fast load path for applying baked lightmaps: it avoids spawning
// `igi1conv olm to-png` once per .olm (which was hundreds of subprocesses per
// building). The igi1conv converter remains the tool for WRITING / recalculating
// .olm files. The R/B channel swap igi1conv applies on export is reproduced.
//
// Returns false on any failure, with `err` set and `*pages` emptied; any texture
// already created for the file is deleted before returning.
bool LoadOlmPages(const std::string& olmPath, std::vector<GLuint>* pages, std::string* err);
